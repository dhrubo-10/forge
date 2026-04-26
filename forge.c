/* forge.c — FORGE Version Control System
 * Copyright (C) 2026 Shahriar Dhrubo
 * GNU General Public License v3 — see LICENSE
 *
 * FORGE is a content-addressed version control system built around
 * SHA-1 object storage, zlib-compressed loose objects, and plain-text
 * refs. It works entirely from the terminal and supports SSH-based
 * remote transport via rsync.
 *
 * Commands:
 *   forge init                         — initialise a new repository
 *   forge put <files> | -a             — stage files
 *   forge msg -m <text>                — commit staged files
 *   forge status                       — show working tree status
 *   forge log [--oneline]              — show commit history
 *   forge show [<sha1>]                — inspect a commit or object
 *   forge diff                         — diff staged vs working tree
 *   forge branch [<n>]                 — list or create branches
 *   forge branch -d <n>                — delete a branch
 *   forge checkout [-b] <branch>       — switch branch
 *   forge rm [--cached] <file>         — remove file from index / disk
 *   forge reset [--soft|--hard] <sha1> — reset HEAD
 *   forge tag <n> [<sha1>]             — create a lightweight tag
 *   forge tag -l | -d <n>              — list or delete tags
 *   forge clone <url> [<dir>]          — clone a remote repository
 *   forge remote add <n> <url>         — add a named remote
 *   forge remote remove <n>            — remove a named remote
 *   forge remote list                  — list remotes
 *   forge shoot [<remote>] [<branch>]  — push to remote over SSH
 *   forge fetch [<remote>] [<branch>]  — fetch from remote over SSH
 *   forge serve [--port <n>]           — serve repo over TCP
 *   forge cat-obj <sha1>               — dump raw object
 *   forge hash-obj [-w] <file>         — hash a file (optionally store)
 *   forge list-objects                 — list all stored objects
 *
 * Identity is read from .forge/config:
 *   [user]
 *       name  = Your Name
 *       email = you@example.com
 */

#include "forge.h"
#include "sha1.h"
#include "objects.h"
#include "index.h"
#include "refs.h"
#include "remote.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

void die(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "forge: fatal: ");
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

void *xmalloc(size_t sz)
{
    void *p = malloc(sz);
    if (!p) die("out of memory");
    return p;
}

char *xstrdup(const char *s)
{
    char *p = strdup(s);
    if (!p) die("out of memory");
    return p;
}

int mkdirp(const char *path, mode_t mode)
{
    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, mode);
            *p = '/';
        }
    }
    return mkdir(tmp, mode);
}

int read_file(const char *path, uint8_t **buf, size_t *len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    struct stat st;
    fstat(fd, &st);
    *buf = malloc((size_t)st.st_size + 1);
    if (!*buf) { close(fd); return -1; }
    ssize_t n = read(fd, *buf, (size_t)st.st_size);
    close(fd);
    if (n < 0) { free(*buf); return -1; }
    (*buf)[n] = '\0';
    *len = (size_t)n;
    return 0;
}

int write_file(const char *path, const uint8_t *buf, size_t len)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    ssize_t n = write(fd, buf, len);
    close(fd);
    return (n == (ssize_t)len) ? 0 : -1;
}

int write_file_str(const char *path, const char *str)
{
    return write_file(path, (const uint8_t *)str, strlen(str));
}

char *read_file_str(const char *path)
{
    uint8_t *buf; size_t len;
    if (read_file(path, &buf, &len) != 0) return NULL;
    return (char *)buf;
}

int is_forge_repo(void)
{
    struct stat st;
    return (stat(FORGE_DIR, &st) == 0 && S_ISDIR(st.st_mode));
}

void forge_find_root(char *out, size_t size)
{
    char cwd[MAX_PATH_LEN];
    if (!getcwd(cwd, sizeof(cwd))) { snprintf(out, size, "."); return; }
    snprintf(out, size, "%s", cwd);
    while (1) {
        char check[MAX_PATH_LEN];
        snprintf(check, sizeof(check), "%s/.forge", out);
        struct stat st;
        if (stat(check, &st) == 0 && S_ISDIR(st.st_mode)) return;
        char *slash = strrchr(out, '/');
        if (!slash || slash == out) break;
        *slash = '\0';
    }
    snprintf(out, size, "%s", cwd);
}


static void config_get(const char *key, char *out, size_t size, const char *def)
{
    snprintf(out, size, "%s", def);
    FILE *f = fopen(FORGE_CONFIG, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        rtrim(line);
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, key, strlen(key)) == 0) {
            char *eq = strchr(p, '=');
            if (eq) {
                eq++; while (*eq == ' ') eq++;
                snprintf(out, size, "%s", eq);
                break;
            }
        }
    }
    fclose(f);
}

static void parse_commit(const char *data, size_t len,
                         char tree[SHA1_HEX_SIZE],
                         char parent[SHA1_HEX_SIZE],
                         char author[512],
                         char *message, size_t msg_size)
{
    tree[0] = parent[0] = author[0] = message[0] = '\0';
    const char *p = data, *end = data + len;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl) nl = end;
        size_t llen = (size_t)(nl - p);

        if (llen == 0) {
            /* blank line — everything after is the message */
            p = nl + 1;
            size_t mlen = (size_t)(end - p);
            if (mlen >= msg_size) mlen = msg_size - 1;
            memcpy(message, p, mlen);
            message[mlen] = '\0';
            rtrim(message);
            break;
        }

        char line[1024];
        size_t copy = llen < sizeof(line) - 1 ? llen : sizeof(line) - 1;
        memcpy(line, p, copy); line[copy] = '\0';

        if (strncmp(line, "tree ", 5) == 0)
            snprintf(tree, SHA1_HEX_SIZE, "%.40s", line + 5);
        else if (strncmp(line, "parent ", 7) == 0)
            snprintf(parent, SHA1_HEX_SIZE, "%.40s", line + 7);
        else if (strncmp(line, "author ", 7) == 0)
            snprintf(author, 512, "%.510s", line + 7);
        p = nl + 1;
    }
}
/* Hash a file on disk without storing it as an object */
static void hash_wt_file(const char *path, char sha1_out[SHA1_HEX_SIZE])
{
    sha1_out[0] = '\0';
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    struct stat st; fstat(fd, &st);
    uint8_t *data = malloc((size_t)st.st_size + 1);
    if (!data) { close(fd); return; }
    ssize_t n = read(fd, data, (size_t)st.st_size);
    close(fd);
    if (n >= 0) obj_hash(data, (size_t)n, OBJ_BLOB, sha1_out);
    free(data);
}

/* Recursively collect regular files under a directory into a list */
static void collect_files(const char *dir, char **list, int *cnt, int cap)
{
    const char *open_dir = (dir[0] && strcmp(dir, ".") != 0) ? dir : ".";
    DIR *d = opendir(open_dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && *cnt < cap) {
        if (de->d_name[0] == '.') continue;
        char path[MAX_PATH_LEN];
        if (dir[0] && strcmp(dir, ".") != 0)
            snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        else
            snprintf(path, sizeof(path), "%s", de->d_name);
        if (is_ignored(path)) continue;
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode))
            collect_files(path, list, cnt, cap);
        else if (S_ISREG(st.st_mode))
            list[(*cnt)++] = xstrdup(path);
    }
    closedir(d);
}

/* Flatten a tree object into a linear IndexEntry array (recursive) */
static void flatten_tree(const char *tree_sha, const char *prefix,
                         IndexEntry **out, int *cnt, int *cap)
{
    TreeEntry *entries; int ecnt;
    if (tree_read(tree_sha, &entries, &ecnt) != 0) return;
    for (int i = 0; i < ecnt; i++) {
        char full[MAX_PATH_LEN];
        if (prefix[0])
            snprintf(full, sizeof(full), "%s/%s", prefix, entries[i].name);
        else
            snprintf(full, sizeof(full), "%s", entries[i].name);

        if (entries[i].mode == MODE_DIR) {
            flatten_tree(entries[i].sha1, full, out, cnt, cap);
        } else {
            if (*cnt >= *cap) {
                *cap *= 2;
                IndexEntry *tmp = realloc(*out, (size_t)*cap * sizeof(IndexEntry));
                if (!tmp) { free(entries); return; }
                *out = tmp;
            }
            (*out)[*cnt].mode = entries[i].mode;
            snprintf((*out)[*cnt].sha1, SHA1_HEX_SIZE, "%s", entries[i].sha1);
            snprintf((*out)[*cnt].path, MAX_PATH_LEN, "%s", full);
            (*cnt)++;
        }
    }
    free(entries);
}

/* Return the full file list from a commit as a flat IndexEntry array.
 * Caller must free() the returned pointer. */
static IndexEntry *get_committed_files(const char *commit_sha, int *cnt_out)
{
    int cap = 256; *cnt_out = 0;
    IndexEntry *committed = malloc((size_t)cap * sizeof(IndexEntry));
    if (!committed) return NULL;
    if (!commit_sha || !commit_sha[0]) return committed;

    ObjType type; uint8_t *data; size_t dlen;
    if (obj_read(commit_sha, &type, &data, &dlen) != 0) return committed;
    if (type != OBJ_COMMIT) { free(data); return committed; }

    char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE], author[512], msg[4096];
    parse_commit((char *)data, dlen, tree, parent, author, msg, sizeof(msg));
    free(data);
    if (tree[0]) flatten_tree(tree, "", &committed, cnt_out, &cap);
    return committed;
}

static int cmd_init(int argc, char *argv[])
{
    const char *dir = (argc > 0) ? argv[0] : ".";
    if (strcmp(dir, ".") != 0) {
        if (mkdir(dir, 0755) != 0 && errno != EEXIST)
            die("cannot create '%s': %s", dir, strerror(errno));
        if (chdir(dir) != 0) die("cannot enter '%s'", dir);
    }
    if (is_forge_repo()) {
        printf("forge: repository already initialised at %s\n", FORGE_DIR);
        return 0;
    }

    const char *dirs[] = {
        FORGE_DIR, FORGE_OBJECTS, FORGE_REFS,
        FORGE_HEADS, ".forge/refs/tags", NULL
    };
    for (int i = 0; dirs[i]; i++)
        if (mkdir(dirs[i], 0755) != 0)
            die("mkdir '%s': %s", dirs[i], strerror(errno));

    write_file_str(FORGE_HEAD, "ref: refs/heads/main\n");
    write_file_str(FORGE_DESCRIPTION, "Unnamed FORGE repository.\n");
    write_file_str(FORGE_CONFIG,
                   "[core]\n"
                   "    repositoryformatversion = 0\n"
                   "    filemode = true\n"
                   "\n"
                   "[user]\n"
                   "    name  = Anonymous\n"
                   "    email = anon@forge.local\n");
    if (access(".forgeignore", F_OK) != 0)
        write_file_str(".forgeignore",
                       "# FORGE ignore file\n"
                       ".forge\n"
                       "*.o\n"
                       "*.a\n"
                       "*.so\n");

    char cwd[MAX_PATH_LEN];
    if (!getcwd(cwd, sizeof(cwd))) snprintf(cwd, sizeof(cwd), ".");
    printf("Initialised empty FORGE repository at \033[1m%s/.forge/\033[0m\n", cwd);
    printf("Edit \033[90m%s\033[0m to set your name and email.\n", FORGE_CONFIG);
    return 0;
}


static int cmd_put(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");
    if (argc == 0) {
        fprintf(stderr, "usage: forge put <file> [...]\n"
                        "       forge put -a\n");
        return 1;
    }
    if (argc == 1 && strcmp(argv[0], "-a") == 0) {
        printf("Staging all files...\n");
        return index_add_all();
    }
    int rc = 0;
    for (int i = 0; i < argc; i++)
        if (index_add(argv[i]) != 0) rc = 1;
    return rc;
}


static int cmd_msg(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    char message[4096] = "";
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
            snprintf(message, sizeof(message), "%s", argv[++i]);
    if (!message[0]) {
        fprintf(stderr, "usage: forge msg -m \"commit message\"\n");
        return 1;
    }

    /* Only the files the user explicitly staged since last commit */
    IndexEntry *staged; int staged_cnt;
    if (index_read(&staged, &staged_cnt) != 0) die("cannot read index");

    char parent_sha1[SHA1_HEX_SIZE] = "";
    head_resolve(parent_sha1);

    /* Get parent snapshot, then apply staged changes on top.
     * This ensures every commit carries the full tracked file set
     * forward — not just the files touched this round. */
    int parent_cnt;
    IndexEntry *parent_files = get_committed_files(
        parent_sha1[0] ? parent_sha1 : NULL, &parent_cnt);

    /* Filter out staged-only files (no-op on first commit) */
    int has_changes = 0;
    for (int i = 0; i < staged_cnt; i++) {
        IndexEntry *p = index_find(parent_files, parent_cnt, staged[i].path);
        if (!p || strcmp(p->sha1, staged[i].sha1) != 0) { has_changes = 1; break; }
    }
    if (staged_cnt == 0 && parent_cnt == 0) {
        fprintf(stderr, "forge: nothing staged — use '\033[1mforge put\033[0m' first\n");
        free(staged); free(parent_files); return 1;
    }
    if (staged_cnt == 0) {
        fprintf(stderr, "forge: nothing new to commit\n");
        free(staged); free(parent_files); return 1;
    }
    (void)has_changes;

    /* Build merged file list: parent + staged overrides */
    int merged_cap = parent_cnt + staged_cnt + 1;
    IndexEntry *entries = malloc((size_t)merged_cap * sizeof(IndexEntry));
    if (!entries) die("out of memory");
    int cnt = 0;

    for (int i = 0; i < parent_cnt; i++) {
        int overridden = 0;
        for (int j = 0; j < staged_cnt; j++)
            if (strcmp(parent_files[i].path, staged[j].path) == 0) {
                overridden = 1; break;
            }
        if (!overridden) entries[cnt++] = parent_files[i];
    }
    for (int i = 0; i < staged_cnt; i++) entries[cnt++] = staged[i];
    free(parent_files);

    char tree_sha1[SHA1_HEX_SIZE];
    if (tree_build_from_index(entries, cnt, "", tree_sha1) != 0)
        die("failed to build tree object");

    char author_name[256], author_email[256];
    config_get("name",  author_name,  sizeof(author_name),  "Anonymous");
    config_get("email", author_email, sizeof(author_email), "anon@forge.local");

    time_t now = time(NULL);
    struct tm *tm_local = localtime(&now);
    char tz[8]; strftime(tz, sizeof(tz), "%z", tm_local);
    char ts[64]; snprintf(ts, sizeof(ts), "%ld %s", (long)now, tz);

    char commit_buf[8192]; int commit_len = 0;
    commit_len += snprintf(commit_buf + commit_len,
                           sizeof(commit_buf) - (size_t)commit_len,
                           "tree %s\n", tree_sha1);
    if (parent_sha1[0])
        commit_len += snprintf(commit_buf + commit_len,
                               sizeof(commit_buf) - (size_t)commit_len,
                               "parent %s\n", parent_sha1);
    commit_len += snprintf(commit_buf + commit_len,
                           sizeof(commit_buf) - (size_t)commit_len,
                           "author %s <%s> %s\n"
                           "committer %s <%s> %s\n\n%s\n",
                           author_name, author_email, ts,
                           author_name, author_email, ts,
                           message);

    char commit_sha1[SHA1_HEX_SIZE];
    if (obj_write((uint8_t *)commit_buf, (size_t)commit_len,
                  OBJ_COMMIT, commit_sha1) != 0)
        die("failed to write commit object");

    if (head_advance(commit_sha1) != 0) die("failed to update HEAD");

    /* Write the full committed snapshot back to the index.
     * Next 'put' will only add deltas on top of this. */
    index_write(entries, cnt);
    free(staged);
    free(entries);

    char branch[256] = "main"; head_branch(branch);
    printf("[\033[33m%.8s\033[0m] (\033[1m%s\033[0m) %s\n",
           commit_sha1, branch, message);
    printf("  tree:   %s\n", tree_sha1);
    if (parent_sha1[0]) printf("  parent: %.8s...\n", parent_sha1);
    printf("  \033[32m%d file%s in snapshot\033[0m\n", cnt, cnt == 1 ? "" : "s");
    return 0;
}


static int cmd_status(int argc, char *argv[])
{
    (void)argc; (void)argv;
    if (!is_forge_repo()) die("not a forge repository");

    char branch[256] = "(detached)";
    head_branch(branch);
    printf("On branch \033[1m%s\033[0m\n", branch);

    /* Full index (contains entire committed snapshot + any new staged files) */
    IndexEntry *idx; int idx_cnt;
    if (index_read(&idx, &idx_cnt) != 0) die("cannot read index");

    /* What HEAD actually committed */
    char head_sha1[SHA1_HEX_SIZE] = "";
    head_resolve(head_sha1);
    int committed_cnt;
    IndexEntry *committed = get_committed_files(head_sha1, &committed_cnt);

    /* Staged section: index entries that DIFFER from the last commit.
     * Files identical to HEAD are carried through transparently and
     * are not shown — they aren't pending any change. */
    int n_staged = 0;
    for (int i = 0; i < idx_cnt; i++) {
        IndexEntry *prev = index_find(committed, committed_cnt, idx[i].path);
        int is_new      = (prev == NULL);
        int is_modified = (prev && strcmp(prev->sha1, idx[i].sha1) != 0);
        if (!is_new && !is_modified) continue;

        if (n_staged++ == 0) {
            printf("\nChanges staged for commit:\n");
            printf("  \033[90m(use 'forge msg -m \"...\"' to commit)\033[0m\n\n");
        }
        printf("  \033[32m%-10s %s\033[0m\n",
               is_new ? "new file:" : "modified:", idx[i].path);
    }

    /* Working-tree section: walk disk, compare against index + committed */
    char **wt_files = malloc(MAX_ENTRIES * sizeof(char *));
    if (!wt_files) die("out of memory");
    int wt_cnt = 0;
    collect_files(".", wt_files, &wt_cnt, MAX_ENTRIES);

    int n_mod = 0, n_unt = 0, n_del = 0;

    for (int i = 0; i < wt_cnt; i++) {
        const char *p = wt_files[i];
        IndexEntry *in_idx  = index_find(idx, idx_cnt, p);
        IndexEntry *in_head = index_find(committed, committed_cnt, p);
        char wt_sha1[SHA1_HEX_SIZE];
        hash_wt_file(p, wt_sha1);

        if (in_idx) {
            /* Tracked — check if modified on disk after last staging */
            if (wt_sha1[0] && strcmp(in_idx->sha1, wt_sha1) != 0) {
                if (n_mod++ == 0)
                    printf("\nModified after staging "
                           "\033[90m(re-run 'forge put' to restage)\033[0m:\n\n");
                printf("  \033[33mmodified:  %s\033[0m\n", p);
            }
        } else if (in_head) {
            /* Was committed but not in current index — unusual, show as modified */
            if (wt_sha1[0] && strcmp(in_head->sha1, wt_sha1) != 0) {
                if (n_mod++ == 0)
                    printf("\nNot staged for commit "
                           "\033[90m(use 'forge put' to stage)\033[0m:\n\n");
                printf("  \033[33mmodified:  %s\033[0m\n", p);
            }
        } else {
            if (n_unt++ == 0)
                printf("\nUntracked files "
                       "\033[90m(use 'forge put <file>' to track)\033[0m:\n\n");
            printf("  \033[31muntracked: %s\033[0m\n", p);
        }
        free(wt_files[i]);
    }

    /* Files that were committed but have been deleted from disk */
    for (int i = 0; i < committed_cnt; i++) {
        if (index_find(idx, idx_cnt, committed[i].path)) continue;
        if (access(committed[i].path, F_OK) != 0) {
            if (n_del++ == 0)
                printf("\nDeleted "
                       "\033[90m(use 'forge rm' to stage removal)\033[0m:\n\n");
            printf("  \033[31mdeleted:   %s\033[0m\n", committed[i].path);
        }
    }

    if (n_staged == 0 && n_mod == 0 && n_unt == 0 && n_del == 0)
        printf("\n  \033[32mnothing to commit, working tree clean\033[0m\n");

    free(idx); free(committed); free(wt_files);
    return 0;
}

/* Parse "Name <email> EPOCH TZ" and return a human-readable date string */
static void format_timestamp(const char *author_str, char *out, size_t size)
{
    out[0] = '\0';
    /* Walk backwards: last token is tz, second-to-last is epoch */
    const char *end = author_str + strlen(author_str);
    const char *tz_start = end;
    while (tz_start > author_str && *(tz_start-1) != ' ') tz_start--;
    if (tz_start == author_str) return;
    const char *ep_end = tz_start - 1;
    const char *ep_start = ep_end;
    while (ep_start > author_str && *(ep_start-1) != ' ') ep_start--;
    if (ep_start == author_str) return;

    long epoch = atol(ep_start);
    time_t t = (time_t)epoch;
    struct tm *tm_info = localtime(&t);
    strftime(out, size, "%a %b %d %H:%M:%S %Y %z", tm_info);
}

static int cmd_log_graph(void); /* forward decl */

static int cmd_log(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    int oneline = 0, graph = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--oneline") == 0) oneline = 1;
        if (strcmp(argv[i], "--graph")   == 0) graph   = 1;
    }
    if (graph) return cmd_log_graph();

    char sha1[SHA1_HEX_SIZE];
    if (head_resolve(sha1) != 0 || !sha1[0]) {
        printf("forge: no commits yet\n"); return 0;
    }

    while (sha1[0]) {
        ObjType type; uint8_t *data; size_t dlen;
        if (obj_read(sha1, &type, &data, &dlen) != 0) break;
        if (type != OBJ_COMMIT) { free(data); break; }

        char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
        char author[512], message[4096];
        parse_commit((char *)data, dlen, tree, parent, author, message, sizeof(message));
        free(data);

        char date_str[128] = "";
        format_timestamp(author, date_str, sizeof(date_str));

        if (oneline)
            printf("\033[33m%.8s\033[0m %s\n", sha1, message);
        else {
            printf("\033[33mcommit %s\033[0m\n", sha1);
            printf("Author: %s\n", author);
            if (date_str[0]) printf("Date:   %s\n", date_str);
            printf("\n    %s\n\n", message);
        }

        if (parent[0]) snprintf(sha1, SHA1_HEX_SIZE, "%s", parent);
        else sha1[0] = '\0';
    }
    return 0;
}

static int cmd_show(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    char sha1[SHA1_HEX_SIZE];
    if (argc < 1) {
        if (head_resolve(sha1) != 0 || !sha1[0]) {
            fprintf(stderr, "forge: no commits yet\n"); return 1;
        }
    } else {
        snprintf(sha1, SHA1_HEX_SIZE, "%.40s", argv[0]);
    }

    ObjType type; uint8_t *data; size_t dlen;
    if (obj_read(sha1, &type, &data, &dlen) != 0) {
        fprintf(stderr, "forge: object %s not found\n", sha1); return 1;
    }

    if (type == OBJ_COMMIT) {
        char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
        char author[512], message[4096];
        parse_commit((char *)data, dlen, tree, parent, author, message, sizeof(message));
        free(data);

        printf("\033[33mcommit %s\033[0m\n", sha1);
        if (parent[0]) printf("Parent: %s\n", parent);
        printf("Author: %s\nTree:   %s\n\n    %s\n\n", author, tree, message);

        /* Diff this commit against its parent */
        int parent_cnt = 0, this_cnt = 0;
        IndexEntry *parent_files = get_committed_files(parent[0] ? parent : NULL, &parent_cnt);
        IndexEntry *this_files   = get_committed_files(sha1, &this_cnt);

        for (int i = 0; i < this_cnt; i++) {
            IndexEntry *p = index_find(parent_files, parent_cnt, this_files[i].path);
            if (!p)
                printf("  \033[32m+++ %s  (new file)\033[0m\n", this_files[i].path);
            else if (strcmp(p->sha1, this_files[i].sha1) != 0)
                printf("  \033[33m~~~ %s  (modified)\033[0m\n", this_files[i].path);
        }
        for (int i = 0; i < parent_cnt; i++) {
            if (!index_find(this_files, this_cnt, parent_files[i].path))
                printf("  \033[31m--- %s  (deleted)\033[0m\n", parent_files[i].path);
        }
        free(parent_files); free(this_files);

    } else if (type == OBJ_BLOB) {
        printf("blob %s (%zu bytes)\n\n", sha1, dlen);
        fwrite(data, 1, dlen, stdout);
        if (dlen > 0 && data[dlen-1] != '\n') putchar('\n');
        free(data);

    } else if (type == OBJ_TREE) {
        free(data);
        printf("tree %s\n\n", sha1);
        TreeEntry *entries; int cnt;
        if (tree_read(sha1, &entries, &cnt) == 0) {
            for (int i = 0; i < cnt; i++)
                printf("  %06o  %s  %s\n",
                       entries[i].mode, entries[i].sha1, entries[i].name);
            free(entries);
        }
    }
    return 0;
}

static void run_diff(const char *label, const char *blob_sha,
                     uint32_t mode, const char *wt_path)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "/tmp/forge_diff_%d", getpid());
    blob_to_file(blob_sha, tmp, mode);
    char cmd[MAX_PATH_LEN * 2];
    if (access(wt_path, F_OK) == 0)
        snprintf(cmd, sizeof(cmd),
                 "diff --label 'a/%s' --label 'b/%s' -u '%s' '%s'",
                 label, label, tmp, wt_path);
    else
        snprintf(cmd, sizeof(cmd),
                 "diff --label 'a/%s' --label 'b/%s' -u '%s' /dev/null",
                 label, label, tmp);
    if (system(cmd) < 0)
        fprintf(stderr, "forge: diff failed for %s\n", label);
    unlink(tmp);
}

static int cmd_diff(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    /* --cached: diff staged blobs vs HEAD (what will go into the next commit)
     * default:  diff working tree files vs HEAD (what has changed since last commit) */
    int cached = 0;
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], "--cached") == 0 || strcmp(argv[i], "--staged") == 0)
            cached = 1;

    char head_sha1[SHA1_HEX_SIZE] = "";
    head_resolve(head_sha1);
    int committed_cnt;
    IndexEntry *committed = get_committed_files(head_sha1, &committed_cnt);

    IndexEntry *idx; int idx_cnt;
    if (index_read(&idx, &idx_cnt) != 0) die("cannot read index");

    int diffed = 0;

    if (cached) {
        /* --cached: staged blob vs HEAD blob */
        for (int i = 0; i < idx_cnt; i++) {
            IndexEntry *prev = index_find(committed, committed_cnt, idx[i].path);
            if (prev && strcmp(prev->sha1, idx[i].sha1) == 0) continue;

            char tmp_head[64], tmp_staged[64];
            snprintf(tmp_head,   sizeof(tmp_head),   "/tmp/forge_a_%d_%d", getpid(), i);
            snprintf(tmp_staged, sizeof(tmp_staged),  "/tmp/forge_b_%d_%d", getpid(), i);

            /* Write HEAD version (or /dev/null for new files) */
            int is_new = (prev == NULL);
            if (!is_new) blob_to_file(prev->sha1, tmp_head, prev->mode);
            blob_to_file(idx[i].sha1, tmp_staged, idx[i].mode);

            char cmd[MAX_PATH_LEN * 2];
            snprintf(cmd, sizeof(cmd),
                     "diff --label 'a/%s' --label 'b/%s' -u '%s' '%s'",
                     idx[i].path, idx[i].path,
                     is_new ? "/dev/null" : tmp_head, tmp_staged);
            if (system(cmd) < 0)
                fprintf(stderr, "forge: diff failed for %s\n", idx[i].path);
            if (!is_new) unlink(tmp_head);
            unlink(tmp_staged);
            diffed++;
        }
    } else {
        /* default: working tree vs HEAD */
        for (int i = 0; i < committed_cnt; i++) {
            char wt_sha1[SHA1_HEX_SIZE];
            hash_wt_file(committed[i].path, wt_sha1);

            if (!wt_sha1[0]) {
                /* file deleted — diff against /dev/null */
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "/tmp/forge_d_%d", getpid());
                blob_to_file(committed[i].sha1, tmp, committed[i].mode);
                char cmd[MAX_PATH_LEN * 2];
                snprintf(cmd, sizeof(cmd),
                         "diff --label 'a/%s' --label 'b/%s' -u '%s' /dev/null",
                         committed[i].path, committed[i].path, tmp);
                if (system(cmd) < 0)
                    fprintf(stderr, "forge: diff failed for %s\n", committed[i].path);
                unlink(tmp); diffed++;
                continue;
            }
            if (strcmp(committed[i].sha1, wt_sha1) != 0) {
                run_diff(committed[i].path, committed[i].sha1,
                         committed[i].mode, committed[i].path);
                diffed++;
            }
        }
        /* Also show new untracked files that were put'd but not yet in HEAD */
        for (int i = 0; i < idx_cnt; i++) {
            if (!index_find(committed, committed_cnt, idx[i].path)) {
                /* new file in index, not in HEAD */
                char wt_sha1[SHA1_HEX_SIZE];
                hash_wt_file(idx[i].path, wt_sha1);
                if (wt_sha1[0] && strcmp(idx[i].sha1, wt_sha1) != 0)
                    run_diff(idx[i].path, idx[i].sha1, idx[i].mode, idx[i].path);
                diffed++;
            }
        }
    }

    if (diffed == 0)
        printf("forge: nothing to diff%s\n",
               cached ? " (index matches HEAD)" : " (working tree matches HEAD)");

    free(committed);
    free(idx);
    return 0;
}


static int cmd_branch(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");
    if (argc == 0) { branch_list(); return 0; }
    if (argc == 2 && strcmp(argv[0], "-d") == 0) return branch_delete(argv[1]);
    if (argc == 1) {
        if (branch_create(argv[0], NULL) == 0) {
            printf("forge: created branch '\033[1m%s\033[0m'\n", argv[0]);
            return 0;
        }
        return 1;
    }
    fprintf(stderr, "usage: forge branch [<n>]\n"
                    "       forge branch -d <n>\n");
    return 1;
}


static int cmd_checkout(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    int create = 0;
    const char *target = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0) create = 1;
        else target = argv[i];
    }
    if (!target) {
        fprintf(stderr, "usage: forge checkout [-b] <branch>\n"); return 1;
    }
    if (create && branch_create(target, NULL) != 0) return 1;

    char sha1[SHA1_HEX_SIZE];
    if (branch_resolve(target, sha1) != 0) {
        fprintf(stderr, "forge: branch '%s' not found\n", target); return 1;
    }

    /* Restore the working tree from the branch's commit tree */
    if (sha1[0]) {
        ObjType type; uint8_t *data; size_t dlen;
        if (obj_read(sha1, &type, &data, &dlen) == 0 && type == OBJ_COMMIT) {
            char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
            char author[512], message[4096];
            parse_commit((char *)data, dlen, tree, parent,
                         author, message, sizeof(message));
            free(data);
            if (tree[0] && tree_checkout(tree, NULL) != 0)
                fprintf(stderr, "forge: warning: tree restore had errors\n");
        } else if (data) free(data);
    }

    head_set_ref(target);
    /* Write the branch committed snapshot to index — status and diff
     * need this as their baseline, not an empty file. */
    {
        int snap_cnt;
        IndexEntry *snap = get_committed_files(sha1[0] ? sha1 : NULL, &snap_cnt);
        index_write(snap, snap_cnt);
        free(snap);
    }
    printf("Switched to branch '\033[1m%s\033[0m'\n", target);
    return 0;
}


static int cmd_rm(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");
    if (argc == 0) {
        fprintf(stderr, "usage: forge rm [--cached] <file> [...]\n"); return 1;
    }
    int cached = 0, start = 0;
    if (strcmp(argv[0], "--cached") == 0) { cached = 1; start = 1; }

    int rc = 0;
    for (int i = start; i < argc; i++) {
        if (index_remove(argv[i]) != 0) {
            fprintf(stderr, "forge: '%s' not in index\n", argv[i]);
            rc = 1; continue;
        }
        if (!cached && unlink(argv[i]) != 0 && errno != ENOENT) {
            perror(argv[i]); rc = 1;
        }
        printf("  removed: %s\n", argv[i]);
    }
    return rc;
}


static int cmd_reset(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    int hard = 0;
    const char *target_sha1 = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--hard") == 0) hard = 1;
        else if (strcmp(argv[i], "--soft") == 0) { /* move HEAD only */ }
        else target_sha1 = argv[i];
    }

    if (!target_sha1) {
        /* No target: just clear the index (unstage everything) */
        write_file_str(FORGE_INDEX, "");
        printf("forge: index cleared (HEAD unchanged)\n");
        return 0;
    }

    char sha1[SHA1_HEX_SIZE];
    if (strcmp(target_sha1, "HEAD") == 0) {
        if (head_resolve(sha1) != 0 || !sha1[0]) {
            fprintf(stderr, "forge: HEAD is not set\n"); return 1;
        }
    } else if (strlen(target_sha1) == SHA1_HEX_LEN) {
        snprintf(sha1, sizeof(sha1), "%s", target_sha1);
    } else if (branch_resolve(target_sha1, sha1) != 0) {
        fprintf(stderr, "forge: cannot resolve '%s'\n", target_sha1); return 1;
    }

    ObjType type; uint8_t *data; size_t dlen;
    if (obj_read(sha1, &type, &data, &dlen) != 0 || type != OBJ_COMMIT) {
        fprintf(stderr, "forge: '%s' is not a commit\n", sha1);
        if (data) free(data); return 1;
    }
    if (hard) {
        char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
        char author[512], message[4096];
        parse_commit((char *)data, dlen, tree, parent,
                     author, message, sizeof(message));
        if (tree[0]) tree_checkout(tree, NULL);
        /* Write the reset target snapshot to index */
        {
            int snap_cnt;
            IndexEntry *snap = get_committed_files(sha1, &snap_cnt);
            index_write(snap, snap_cnt);
            free(snap);
        }
        printf("forge: hard reset \xe2\x86\x92 %s (working tree restored)\n", sha1);
    } else {
        printf("forge: soft reset \xe2\x86\x92 %s (index preserved)\n", sha1);
    }
    free(data);
    head_advance(sha1);
    return 0;
}


static int cmd_tag(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    if (argc == 0 || (argc == 1 && strcmp(argv[0], "-l") == 0)) {
        DIR *d = opendir(".forge/refs/tags");
        if (!d) { printf("  (no tags)\n"); return 0; }
        struct dirent *de;
        while ((de = readdir(d)))
            if (de->d_name[0] != '.') printf("  %s\n", de->d_name);
        closedir(d);
        return 0;
    }

    if (argc >= 2 && strcmp(argv[0], "-d") == 0) {
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), ".forge/refs/tags/%s", argv[1]);
        if (unlink(path) != 0) {
            fprintf(stderr, "forge: tag '%s' not found\n", argv[1]); return 1;
        }
        printf("forge: deleted tag '%s'\n", argv[1]);
        return 0;
    }

    /* Create tag pointing to sha1 (or HEAD if none given) */
    char sha1[SHA1_HEX_SIZE];
    if (argc >= 2)
        snprintf(sha1, sizeof(sha1), "%.40s", argv[1]);
    else if (head_resolve(sha1) != 0 || !sha1[0]) {
        fprintf(stderr, "forge: no commits yet\n"); return 1;
    }

    char ref_path[MAX_PATH_LEN];
    snprintf(ref_path, sizeof(ref_path), "refs/tags/%s", argv[0]);
    if (ref_write(ref_path, sha1) != 0) {
        fprintf(stderr, "forge: failed to create tag '%s'\n", argv[0]); return 1;
    }
    printf("forge: tag '\033[1m%s\033[0m' \xe2\x86\x92 %.8s...\n", argv[0], sha1);
    return 0;
}


static int cmd_clone(int argc, char *argv[])
{
    if (argc < 1) {
        fprintf(stderr, "usage: forge clone <user@host:path> [<dir>]\n");
        return 1;
    }
    const char *url = argv[0];

    char local_dir[MAX_PATH_LEN];
    if (argc >= 2) {
        snprintf(local_dir, sizeof(local_dir), "%s", argv[1]);
    } else {
        const char *colon = strchr(url, ':');
        const char *rpath = colon ? colon + 1 : url;
        const char *slash = strrchr(rpath, '/');
        const char *base  = slash ? slash + 1 : rpath;
        snprintf(local_dir, sizeof(local_dir), "%s",
                 base[0] ? base : "forge_clone");
    }
    printf("forge clone: %s \xe2\x86\x92 %s\n", url, local_dir);

    char user_host[512], rpath[MAX_PATH_LEN];
    if (remote_parse_url(url, user_host, sizeof(user_host),
                         rpath, sizeof(rpath)) != 0) return 1;

    if (mkdir(local_dir, 0755) != 0) {
        fprintf(stderr, "forge: '%s' already exists\n", local_dir); return 1;
    }
    if (chdir(local_dir) != 0) die("cannot enter '%s'", local_dir);

    const char *dirs[] = { FORGE_DIR, FORGE_OBJECTS, FORGE_REFS,
                            FORGE_HEADS, ".forge/refs/tags", NULL };
    for (int i = 0; dirs[i]; i++) mkdir(dirs[i], 0755);

    write_file_str(FORGE_CONFIG,
                   "[core]\n    repositoryformatversion = 0\n"
                   "[user]\n    name  = Anonymous\n    email = anon@forge.local\n");

    char cmd[MAX_PATH_LEN * 2];

    /* Pull objects from remote */
    snprintf(cmd, sizeof(cmd),
             "rsync -az --progress %s:%s/.forge/objects/ %s/",
             user_host, rpath, FORGE_OBJECTS);
    printf("  fetching objects...\n");
    if (system(cmd) != 0) { fprintf(stderr, "forge: rsync failed\n"); return 1; }

    /* Pull refs from remote */
    snprintf(cmd, sizeof(cmd),
             "rsync -az %s:%s/.forge/refs/ %s/", user_host, rpath, FORGE_REFS);
    if (system(cmd) != 0) {
        fprintf(stderr, "forge: failed to fetch refs\n"); return 1;
    }

    /* Copy HEAD from remote */
    snprintf(cmd, sizeof(cmd), "ssh %s 'cat %s/.forge/HEAD'", user_host, rpath);
    FILE *pipe = popen(cmd, "r");
    char head_content[MAX_PATH_LEN] = "ref: refs/heads/main";
    if (pipe) {
        if (fgets(head_content, sizeof(head_content), pipe)) rtrim(head_content);
        pclose(pipe);
    }
    write_file_str(FORGE_HEAD, head_content);

    /* Restore working tree from HEAD commit */
    char sha1[SHA1_HEX_SIZE] = "";
    head_resolve(sha1);
    if (sha1[0]) {
        ObjType type; uint8_t *data; size_t dlen;
        if (obj_read(sha1, &type, &data, &dlen) == 0 && type == OBJ_COMMIT) {
            char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
            char author[512], message[4096];
            parse_commit((char *)data, dlen, tree, parent,
                         author, message, sizeof(message));
            free(data);
            if (tree[0]) tree_checkout(tree, NULL);
        } else if (data) free(data);
    }

    write_file_str(FORGE_REMOTES, "");
    remote_add("origin", url);
    write_file_str(".forgeignore", "# FORGE ignore\n.forge\n*.o\n*.a\n");
    /* Write cloned commit snapshot to index */
    {
        int snap_cnt;
        IndexEntry *snap = get_committed_files(sha1[0] ? sha1 : NULL, &snap_cnt);
        index_write(snap, snap_cnt);
        free(snap);
    }

    char cwd[MAX_PATH_LEN];
    if (!getcwd(cwd, sizeof(cwd))) snprintf(cwd, sizeof(cwd), "%s", local_dir);
    printf("forge: cloned into '\033[1m%s\033[0m'\n", cwd);
    return 0;
}


static int cmd_remote(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    if (argc == 0 || (argc == 1 && strcmp(argv[0], "list") == 0)) {
        remote_list(); return 0;
    }
    if (argc == 3 && strcmp(argv[0], "add") == 0)
        return remote_add(argv[1], argv[2]);
    if (argc == 2 && strcmp(argv[0], "remove") == 0) {
        FILE *f = fopen(FORGE_REMOTES, "r");
        if (!f) { fprintf(stderr, "forge: no remotes\n"); return 1; }
        char buf[65536]; int blen = 0;
        char line[MAX_PATH_LEN];
        while (fgets(line, sizeof(line), f)) {
            char name[128]; sscanf(line, "%127s", name);
            if (strcmp(name, argv[1]) != 0) {
                int n = (int)strlen(line);
                if (blen + n < (int)sizeof(buf)) {
                    memcpy(buf + blen, line, (size_t)n); blen += n;
                }
            }
        }
        fclose(f);
        f = fopen(FORGE_REMOTES, "w");
        if (f) {
            if (fwrite(buf, 1, (size_t)blen, f) != (size_t)blen) { /* best effort */ }
            fclose(f);
        }
        printf("forge: removed remote '%s'\n", argv[1]);
        return 0;
    }
    fprintf(stderr, "usage: forge remote add <n> <url>\n"
                    "       forge remote remove <n>\n"
                    "       forge remote list\n");
    return 1;
}


static int cmd_shoot(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");
    return remote_shoot(argc > 0 ? argv[0] : "origin",
                        argc > 1 ? argv[1] : NULL);
}

static int cmd_fetch(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");
    return remote_fetch(argc > 0 ? argv[0] : "origin",
                        argc > 1 ? argv[1] : NULL);
}

/*
 * Minimal TCP object server.
 *
 * Protocol (line-oriented text over TCP):
 *   REFS            server replies REF <refname> <sha1> lines, then OK
 *   HAVE <sha1>     client declares it already has this object
 *   WANT <sha1>     client requests this object
 *   DONE            server sends all WANTed objects missing from HAVE set:
 *                     OBJ <sha1> <type> <size>\n<raw_bytes>\n
 *                   then OK
 *   QUIT            close connection
 */

static void serve_client(int fd)
{
    FILE *in  = fdopen(dup(fd), "r");
    FILE *out = fdopen(dup(fd), "w");
    if (!in || !out) return;

    char wants[1024][SHA1_HEX_SIZE];
    char haves[1024][SHA1_HEX_SIZE];
    int  want_cnt = 0, have_cnt = 0;
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), in)) {
        rtrim(line);
        if (!line[0]) continue;

        if (strcmp(line, "QUIT") == 0) break;

        else if (strcmp(line, "REFS") == 0) {
            DIR *d = opendir(FORGE_HEADS);
            if (d) {
                struct dirent *de;
                while ((de = readdir(d))) {
                    if (de->d_name[0] == '.') continue;
                    char sha1[SHA1_HEX_SIZE], refname[MAX_PATH_LEN];
                    snprintf(refname, sizeof(refname),
                             "refs/heads/%s", de->d_name);
                    if (ref_read(refname, sha1) == 0)
                        fprintf(out, "REF %s %s\n", refname, sha1);
                }
                closedir(d);
            }
            DIR *td = opendir(".forge/refs/tags");
            if (td) {
                struct dirent *de;
                while ((de = readdir(td))) {
                    if (de->d_name[0] == '.') continue;
                    char sha1[SHA1_HEX_SIZE], refname[MAX_PATH_LEN];
                    snprintf(refname, sizeof(refname),
                             "refs/tags/%s", de->d_name);
                    if (ref_read(refname, sha1) == 0)
                        fprintf(out, "REF %s %s\n", refname, sha1);
                }
                closedir(td);
            }
            fprintf(out, "OK\n"); fflush(out);
        }
        else if (strncmp(line, "HAVE ", 5) == 0 && have_cnt < 1024) {
            snprintf(haves[have_cnt++], SHA1_HEX_SIZE, "%.40s", line + 5);
            fprintf(out, "OK\n"); fflush(out);
        }
        else if (strncmp(line, "WANT ", 5) == 0 && want_cnt < 1024) {
            snprintf(wants[want_cnt++], SHA1_HEX_SIZE, "%.40s", line + 5);
            fprintf(out, "OK\n"); fflush(out);
        }
        else if (strcmp(line, "DONE") == 0) {
            for (int i = 0; i < want_cnt; i++) {
                int skip = 0;
                for (int j = 0; j < have_cnt; j++)
                    if (strcmp(wants[i], haves[j]) == 0) { skip = 1; break; }
                if (skip) continue;

                ObjType type; uint8_t *data; size_t dlen;
                if (obj_read(wants[i], &type, &data, &dlen) != 0) {
                    fprintf(out, "ERR object %s not found\n", wants[i]);
                    fflush(out); continue;
                }
                const char *tnames[] = { "", "blob", "tree", "commit" };
                fprintf(out, "OBJ %s %s %zu\n", wants[i], tnames[type], dlen);
                fwrite(data, 1, dlen, out);
                fputc('\n', out); fflush(out);
                free(data);
            }
            fprintf(out, "OK\n"); fflush(out);
            want_cnt = 0; have_cnt = 0;
        }
        else {
            fprintf(out, "ERR unknown command '%s'\n", line); fflush(out);
        }
    }
    fclose(in); fclose(out);
}

static int cmd_serve(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    int port = 9418;
    for (int i = 0; i < argc - 1; i++)
        if (strcmp(argv[i], "--port") == 0)
            port = atoi(argv[i + 1]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) die("socket: %s", strerror(errno));

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        die("bind port %d: %s", port, strerror(errno));
    if (listen(server_fd, 16) < 0)
        die("listen: %s", strerror(errno));

    signal(SIGCHLD, SIG_IGN);

    char cwd[MAX_PATH_LEN];
    if (!getcwd(cwd, sizeof(cwd))) snprintf(cwd, sizeof(cwd), ".");
    printf("forge serve: \033[1mlistening on port %d\033[0m\n", port);
    printf("  repo: %s\n  Ctrl-C to stop.\n\n", cwd);

    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) continue;
        printf("  [+] %s\n", inet_ntoa(caddr.sin_addr));
        pid_t pid = fork();
        if (pid == 0) {
            close(server_fd);
            serve_client(cfd);
            close(cfd);
            exit(0);
        } else if (pid > 0) {
            close(cfd);
        } else {
            serve_client(cfd);
            close(cfd);
        }
    }
}


static int cmd_cat_obj(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");
    if (argc < 1) { fprintf(stderr, "usage: forge cat-obj <sha1>\n"); return 1; }
    ObjType type; uint8_t *data; size_t len;
    if (obj_read(argv[0], &type, &data, &len) != 0) {
        fprintf(stderr, "forge: object %s not found\n", argv[0]); return 1;
    }
    const char *names[] = { "", "blob", "tree", "commit" };
    printf("type: %s\nsize: %zu\n\n", names[type], len);
    if (type == OBJ_TREE) {
        TreeEntry *entries; int cnt;
        if (tree_read(argv[0], &entries, &cnt) == 0) {
            for (int i = 0; i < cnt; i++)
                printf("  %06o  %s  %s\n",
                       entries[i].mode, entries[i].sha1, entries[i].name);
            free(entries);
        }
    } else {
        fwrite(data, 1, len, stdout);
    }
    free(data);
    return 0;
}

static int cmd_hash_obj(int argc, char *argv[])
{
    if (argc < 1) { fprintf(stderr, "usage: forge hash-obj [-w] <file>\n"); return 1; }
    int store = (strcmp(argv[0], "-w") == 0);
    const char *file = store ? (argc > 1 ? argv[1] : NULL) : argv[0];
    if (!file) { fprintf(stderr, "forge hash-obj -w <file>\n"); return 1; }
    char sha1[SHA1_HEX_SIZE];
    if (store) {
        if (!is_forge_repo()) die("not a forge repository");
        blob_from_file(file, sha1);
    } else {
        uint8_t *data; size_t len;
        if (read_file(file, &data, &len) != 0) { perror(file); return 1; }
        obj_hash(data, len, OBJ_BLOB, sha1);
        free(data);
    }
    printf("%s\n", sha1);
    return 0;
}

static int cmd_list_objects(int argc, char *argv[])
{
    (void)argc; (void)argv;
    char **sha1s; int cnt;
    if (obj_list_all(&sha1s, &cnt) != 0) return 1;
    for (int i = 0; i < cnt; i++) { printf("%s\n", sha1s[i]); free(sha1s[i]); }
    free(sha1s);
    return 0;
}

/* Fast-forward only. Refuses if branches have diverged. */

static int cmd_merge(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");
    if (argc < 1) {
        fprintf(stderr, "usage: forge merge <branch>\n"); return 1;
    }

    /* Resolve the target to a commit sha1 */
    char target_sha1[SHA1_HEX_SIZE];
    if (branch_resolve(argv[0], target_sha1) != 0) {
        snprintf(target_sha1, sizeof(target_sha1), "%.40s", argv[0]);
        if (!obj_exists(target_sha1)) {
            fprintf(stderr, "forge: branch '%s' not found\n", argv[0]); return 1;
        }
    }

    char head_sha1[SHA1_HEX_SIZE] = "";
    head_resolve(head_sha1);

    if (head_sha1[0] && strcmp(head_sha1, target_sha1) == 0) {
        printf("forge: already up to date.\n"); return 0;
    }

    /* Walk target ancestry — if HEAD appears, this is a fast-forward */
    int ff = 0;
    if (!head_sha1[0]) {
        ff = 1; /* no commits yet, just advance */
    } else {
        char walk[SHA1_HEX_SIZE];
        snprintf(walk, sizeof(walk), "%s", target_sha1);
        for (int depth = 0; walk[0] && depth < 100000; depth++) {
            if (strcmp(walk, head_sha1) == 0) { ff = 1; break; }
            ObjType t; uint8_t *d; size_t dl;
            if (obj_read(walk, &t, &d, &dl) != 0) break;
            if (t != OBJ_COMMIT) { free(d); break; }
            char tr[SHA1_HEX_SIZE], pa[SHA1_HEX_SIZE], au[512], ms[4096];
            parse_commit((char *)d, dl, tr, pa, au, ms, sizeof(ms));
            free(d);
            if (pa[0]) snprintf(walk, sizeof(walk), "%s", pa);
            else walk[0] = '\0';
        }
    }

    if (!ff) {
        /* Check the reverse: is target already behind HEAD? */
        char walk[SHA1_HEX_SIZE];
        snprintf(walk, sizeof(walk), "%s", head_sha1);
        for (int depth = 0; walk[0] && depth < 100000; depth++) {
            if (strcmp(walk, target_sha1) == 0) {
                printf("forge: already up to date.\n"); return 0;
            }
            ObjType t; uint8_t *d; size_t dl;
            if (obj_read(walk, &t, &d, &dl) != 0) break;
            if (t != OBJ_COMMIT) { free(d); break; }
            char tr[SHA1_HEX_SIZE], pa[SHA1_HEX_SIZE], au[512], ms[4096];
            parse_commit((char *)d, dl, tr, pa, au, ms, sizeof(ms));
            free(d);
            if (pa[0]) snprintf(walk, sizeof(walk), "%s", pa);
            else walk[0] = '\0';
        }
        fprintf(stderr,
                "forge: merge: not a fast-forward\n"
                "  hint: branches have diverged\n");
        return 1;
    }

    /* Fast-forward: restore target working tree and advance HEAD */
    ObjType type; uint8_t *data; size_t dlen;
    if (obj_read(target_sha1, &type, &data, &dlen) != 0 || type != OBJ_COMMIT) {
        fprintf(stderr, "forge: cannot read target commit\n");
        if (data) free(data); return 1;
    }
    char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE], author[512], msg[4096];
    parse_commit((char *)data, dlen, tree, parent, author, msg, sizeof(msg));
    free(data);

    if (tree[0] && tree_checkout(tree, NULL) != 0)
        fprintf(stderr, "forge: warning: tree restore had errors\n");

    head_advance(target_sha1);

    /* Update index to merged snapshot */
    int snap_cnt;
    IndexEntry *snap = get_committed_files(target_sha1, &snap_cnt);
    index_write(snap, snap_cnt);
    free(snap);

    printf("forge: fast-forward \xe2\x86\x92 \033[33m%.8s\033[0m\n", target_sha1);
    printf("  %s\n", msg);
    return 0;
}


static int cmd_config(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");
    if (argc < 1) {
        fprintf(stderr,
                "usage: forge config <key>          -- get a value\n"
                "       forge config <key> <value>  -- set a value\n"
                "  common keys: user.name  user.email\n");
        return 1;
    }

    /* Strip "user." prefix for the config file lookup */
    const char *key_full  = argv[0];
    const char *key_field = (strncmp(key_full, "user.", 5) == 0)
                            ? key_full + 5 : key_full;

    if (argc == 1) {
        char val[512];
        config_get(key_field, val, sizeof(val), "");
        if (!val[0]) {
            fprintf(stderr, "forge: config key '%s' not set\n", key_full);
            return 1;
        }
        printf("%s\n", val);
        return 0;
    }

    const char *value = argv[1];

    /* Rewrite .forge/config updating the matching key */
    FILE *f = fopen(FORGE_CONFIG, "r");
    if (!f) { fprintf(stderr, "forge: cannot open config\n"); return 1; }

    char lines[256][512];
    int  nlines = 0;
    while (nlines < 255 && fgets(lines[nlines], sizeof(lines[0]), f))
        nlines++;
    fclose(f);

    int found = 0;
    for (int i = 0; i < nlines; i++) {
        char tmp[512]; snprintf(tmp, sizeof(tmp), "%s", lines[i]);
        rtrim(tmp);
        char *p = tmp; while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, key_field, strlen(key_field)) == 0 && strchr(p, '=')) {
            snprintf(lines[i], sizeof(lines[0]),
                     "    %s = %s\n", key_field, value);
            found = 1;
        }
    }
    if (!found)
        snprintf(lines[nlines++], sizeof(lines[0]),
                 "    %s = %s\n", key_field, value);

    f = fopen(FORGE_CONFIG, "w");
    if (!f) { fprintf(stderr, "forge: cannot write config\n"); return 1; }
    for (int i = 0; i < nlines; i++) fputs(lines[i], f);
    fclose(f);

    printf("forge: %s = %s\n", key_full, value);
    return 0;
}


static int cmd_ls_files(int argc, char *argv[])
{
    (void)argc; (void)argv;
    if (!is_forge_repo()) die("not a forge repository");

    char head_sha1[SHA1_HEX_SIZE] = "";
    head_resolve(head_sha1);
    int cnt;
    IndexEntry *files = get_committed_files(head_sha1, &cnt);
    for (int i = 0; i < cnt; i++)
        printf("%s\n", files[i].path);
    free(files);
    return 0;
}

/*
 * Saves working tree modifications since last commit to .forge/stash,
 * then resets the working tree to HEAD.
 *
 *   forge stash           — save changes, clean working tree
 *   forge stash pop       — restore last stash and delete it
 *   forge stash list      — show what is stashed
 *   forge stash drop      — discard the stash without restoring
 */

static int cmd_stash(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    char stash_path[MAX_PATH_LEN];
    snprintf(stash_path, sizeof(stash_path), "%s/stash", FORGE_DIR);

    int do_pop  = (argc > 0 && strcmp(argv[0], "pop")  == 0);
    int do_list = (argc > 0 && strcmp(argv[0], "list") == 0);
    int do_drop = (argc > 0 && strcmp(argv[0], "drop") == 0);

    /* ── list ── */
    if (do_list) {
        FILE *f = fopen(stash_path, "r");
        if (!f) { printf("  (no stash)\n"); return 0; }
        char line[512]; int n = 0;
        while (fgets(line, sizeof(line), f)) {
            rtrim(line);
            if (strncmp(line, "MSG:", 4) == 0)
                printf("  stash@{%d}: %s\n", n++, line + 4);
        }
        fclose(f);
        return 0;
    }

    /* ── drop ── */
    if (do_drop) {
        if (unlink(stash_path) == 0) printf("forge: stash dropped\n");
        else fprintf(stderr, "forge: no stash to drop\n");
        return 0;
    }

    /* ── pop ── */
    if (do_pop) {
        FILE *f = fopen(stash_path, "r");
        if (!f) { fprintf(stderr, "forge: no stash to pop\n"); return 1; }

        IndexEntry *stash_entries = malloc(MAX_ENTRIES * sizeof(IndexEntry));
        if (!stash_entries) die("out of memory");
        int stash_cnt = 0;
        char line[MAX_PATH_LEN + 128];
        while (fgets(line, sizeof(line), f) && stash_cnt < MAX_ENTRIES) {
            rtrim(line);
            if (!line[0] || strncmp(line, "MSG:", 4) == 0) continue;
            unsigned int mode;
            char sha1[SHA1_HEX_SIZE], path[MAX_PATH_LEN];
            if (sscanf(line, "%o\t%40s\t%4095[^\n]", &mode, sha1, path) != 3)
                continue;
            stash_entries[stash_cnt].mode = mode;
            snprintf(stash_entries[stash_cnt].sha1, SHA1_HEX_SIZE, "%s", sha1);
            snprintf(stash_entries[stash_cnt].path, MAX_PATH_LEN, "%s", path);
            stash_cnt++;
        }
        fclose(f);

        /* Restore changed files to working tree */
        char head_sha1[SHA1_HEX_SIZE] = "";
        head_resolve(head_sha1);
        int committed_cnt;
        IndexEntry *committed = get_committed_files(head_sha1, &committed_cnt);

        int restored = 0;
        for (int i = 0; i < stash_cnt; i++) {
            IndexEntry *base = index_find(committed, committed_cnt,
                                          stash_entries[i].path);
            if (base && strcmp(base->sha1, stash_entries[i].sha1) == 0) continue;
            if (blob_to_file(stash_entries[i].sha1, stash_entries[i].path,
                             stash_entries[i].mode) == 0) {
                printf("  restored: %s\n", stash_entries[i].path);
                restored++;
            }
        }

        /* Merge stash entries on top of committed snapshot in index */
        int merged_cap = committed_cnt + stash_cnt + 1;
        IndexEntry *merged = malloc((size_t)merged_cap * sizeof(IndexEntry));
        if (!merged) die("out of memory");
        int mcnt = 0;
        for (int i = 0; i < committed_cnt; i++) {
            int over = 0;
            for (int j = 0; j < stash_cnt; j++)
                if (strcmp(committed[i].path, stash_entries[j].path) == 0)
                    { over = 1; break; }
            if (!over) merged[mcnt++] = committed[i];
        }
        for (int i = 0; i < stash_cnt; i++) merged[mcnt++] = stash_entries[i];
        index_write(merged, mcnt);
        free(merged); free(committed); free(stash_entries);

        unlink(stash_path);
        printf("forge: stash popped (%d file%s changed)\n",
               restored, restored == 1 ? "" : "s");
        return 0;
    }

    /* ── save (default) ── */
    char head_sha1[SHA1_HEX_SIZE] = "";
    head_resolve(head_sha1);
    int committed_cnt;
    IndexEntry *committed = get_committed_files(head_sha1, &committed_cnt);

    char **wt_files = malloc(MAX_ENTRIES * sizeof(char *));
    if (!wt_files) die("out of memory");
    int wt_cnt = 0;
    collect_files(".", wt_files, &wt_cnt, MAX_ENTRIES);

    FILE *sf = fopen(stash_path, "w");
    if (!sf) {
        fprintf(stderr, "forge: cannot write stash\n");
        free(committed); free(wt_files); return 1;
    }

    time_t now = time(NULL);
    char branch[256] = "HEAD"; head_branch(branch);
    fprintf(sf, "MSG:WIP on %s %ld\n", branch, (long)now);

    int stashed = 0;
    for (int i = 0; i < wt_cnt; i++) {
        const char *p = wt_files[i];
        char wt_sha1[SHA1_HEX_SIZE]; hash_wt_file(p, wt_sha1);
        IndexEntry *base = index_find(committed, committed_cnt, p);
        if (!base || (wt_sha1[0] && strcmp(base->sha1, wt_sha1) != 0)) {
            char stored[SHA1_HEX_SIZE];
            if (blob_from_file(p, stored) == 0) {
                struct stat st; stat(p, &st);
                uint32_t mode = (st.st_mode & S_IXUSR) ? MODE_EXEC : MODE_FILE;
                fprintf(sf, "%o\t%s\t%s\n", mode, stored, p);
                stashed++;
            }
        }
        free(wt_files[i]);
    }
    fclose(sf);
    free(committed);
    free(wt_files);

    if (stashed == 0) {
        unlink(stash_path);
        printf("forge: no local changes to stash\n");
        return 0;
    }

    /* Reset working tree and index back to HEAD */
    if (head_sha1[0]) {
        ObjType type; uint8_t *data; size_t dlen;
        if (obj_read(head_sha1, &type, &data, &dlen) == 0 && type == OBJ_COMMIT) {
            char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
            char author[512], message[4096];
            parse_commit((char *)data, dlen, tree, parent,
                         author, message, sizeof(message));
            free(data);
            if (tree[0]) tree_checkout(tree, NULL);
        } else if (data) free(data);
    }
    int snap_cnt;
    IndexEntry *snap = get_committed_files(head_sha1, &snap_cnt);
    index_write(snap, snap_cnt);
    free(snap);

    printf("forge: stashed %d file%s \xe2\x80\x94 working tree clean\n",
           stashed, stashed == 1 ? "" : "s");
    return 0;
}

/* Restore a working-tree file to its state in HEAD (or a given commit).
 * Does not touch the index — the file just goes back to what was committed. */

static int cmd_restore(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    const char *from_sha1 = NULL;
    int file_start = 0;

    /* Optional --source <sha1|branch> */
    for (int i = 0; i < argc; i++) {
        if ((strcmp(argv[i], "--source") == 0 || strcmp(argv[i], "-s") == 0)
            && i + 1 < argc) {
            from_sha1 = argv[++i];
            file_start = i + 1;
        }
    }
    if (file_start == 0) file_start = 0;

    /* Collect actual file arguments */
    const char **files = NULL;
    int nfiles = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--source") == 0 || strcmp(argv[i], "-s") == 0) {
            i++; continue;
        }
        if (!files) files = malloc((size_t)argc * sizeof(char *));
        files[nfiles++] = argv[i];
    }

    if (nfiles == 0) {
        fprintf(stderr, "usage: forge restore [--source <sha1>] <file> [...]\n");
        free(files); return 1;
    }

    /* Resolve source commit */
    char commit_sha1[SHA1_HEX_SIZE] = "";
    if (from_sha1) {
        if (strlen(from_sha1) == SHA1_HEX_LEN)
            snprintf(commit_sha1, sizeof(commit_sha1), "%s", from_sha1);
        else if (strcmp(from_sha1, "HEAD") == 0)
            head_resolve(commit_sha1);
        else
            branch_resolve(from_sha1, commit_sha1);
    } else {
        head_resolve(commit_sha1);
    }

    if (!commit_sha1[0]) {
        fprintf(stderr, "forge: no commits yet\n");
        free(files); return 1;
    }

    /* Get file list from that commit */
    int cnt;
    IndexEntry *snapshot = get_committed_files(commit_sha1, &cnt);

    int rc = 0;
    for (int i = 0; i < nfiles; i++) {
        IndexEntry *e = index_find(snapshot, cnt, files[i]);
        if (!e) {
            fprintf(stderr, "forge: '%s' not found in commit %s\n",
                    files[i], commit_sha1);
            rc = 1; continue;
        }
        if (blob_to_file(e->sha1, e->path, e->mode) != 0) {
            fprintf(stderr, "forge: failed to restore '%s'\n", files[i]);
            rc = 1; continue;
        }
        printf("  restored: %s\n", files[i]);
    }

    free(snapshot); free(files);
    return rc;
}
/* Rename a tracked file: move the file on disk, update the index. */

static int cmd_mv(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");
    if (argc < 2) {
        fprintf(stderr, "usage: forge mv <src> <dst>\n"); return 1;
    }
    const char *src = argv[0], *dst = argv[1];

    /* Check source exists */
    if (access(src, F_OK) != 0) {
        fprintf(stderr, "forge: '%s' does not exist\n", src); return 1;
    }

    /* Read current index */
    IndexEntry *entries; int cnt;
    if (index_read(&entries, &cnt) != 0) die("cannot read index");

    /* Find source in index */
    int found = 0;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(entries[i].path, src) == 0) {
            /* Rename the disk file */
            if (rename(src, dst) != 0) {
                fprintf(stderr, "forge: rename failed: %s\n", strerror(errno));
                free(entries); return 1;
            }
            /* Update the path in the index entry */
            snprintf(entries[i].path, MAX_PATH_LEN, "%s", dst);
            found = 1; break;
        }
    }

    if (!found) {
        /* Not indexed yet — just rename on disk and stage both paths */
        if (rename(src, dst) != 0) {
            fprintf(stderr, "forge: rename failed: %s\n", strerror(errno));
            free(entries); return 1;
        }
        fprintf(stderr, "forge: warning: '%s' was not tracked; "
                        "renamed on disk, stage '%s' manually\n", src, dst);
        free(entries);
        return 0;
    }

    if (index_write(entries, cnt) != 0) {
        fprintf(stderr, "forge: failed to update index\n");
        free(entries); return 1;
    }
    free(entries);
    printf("  renamed: %s -> %s\n", src, dst);
    return 0;
}

/* Search for a pattern in all tracked files (working tree versions). */

static int cmd_grep(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    int show_line_num = 0;
    int ignore_case   = 0;
    const char *pattern = NULL;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0)      show_line_num = 1;
        else if (strcmp(argv[i], "-i") == 0) ignore_case   = 1;
        else if (!pattern)                    pattern = argv[i];
    }

    if (!pattern) {
        fprintf(stderr, "usage: forge grep [-n] [-i] <pattern>\n"); return 1;
    }

    /* Get tracked files from HEAD */
    char head_sha1[SHA1_HEX_SIZE] = "";
    head_resolve(head_sha1);
    int cnt;
    IndexEntry *files = get_committed_files(head_sha1, &cnt);

    int total_matches = 0;

    for (int i = 0; i < cnt; i++) {
        FILE *f = fopen(files[i].path, "r");
        if (!f) continue;

        char line[8192]; int lineno = 0;
        while (fgets(line, sizeof(line), f)) {
            lineno++;
            /* Case-insensitive search using strcasestr if available,
             * otherwise fall back to our own scan */
            char *match = NULL;
            if (ignore_case) {
                /* Simple case-insensitive scan */
                char low_line[8192], low_pat[256];
                snprintf(low_pat, sizeof(low_pat), "%s", pattern);
                for (char *p = low_pat; *p; p++) *p = (char)(*p | 0x20);
                snprintf(low_line, sizeof(low_line), "%s", line);
                for (char *p = low_line; *p; p++) *p = (char)(*p | 0x20);
                match = strstr(low_line, low_pat);
            } else {
                match = strstr(line, pattern);
            }

            if (match) {
                rtrim(line);
                if (show_line_num)
                    printf("\033[35m%s\033[0m:\033[32m%d\033[0m:%s\n",
                           files[i].path, lineno, line);
                else
                    printf("\033[35m%s\033[0m:%s\n", files[i].path, line);
                total_matches++;
            }
        }
        fclose(f);
    }
    free(files);

    if (total_matches == 0) {
        printf("forge: no matches for '%s'\n", pattern);
        return 1;
    }
    return 0;
}

/* Apply the changes introduced by a specific commit onto HEAD. */

static int cmd_cherry_pick(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");
    if (argc < 1) {
        fprintf(stderr, "usage: forge cherry-pick <sha1>\n"); return 1;
    }

    /* Resolve target */
    char pick_sha1[SHA1_HEX_SIZE];
    if (strlen(argv[0]) == SHA1_HEX_LEN)
        snprintf(pick_sha1, sizeof(pick_sha1), "%s", argv[0]);
    else if (branch_resolve(argv[0], pick_sha1) != 0) {
        fprintf(stderr, "forge: cannot resolve '%s'\n", argv[0]); return 1;
    }

    ObjType type; uint8_t *data; size_t dlen;
    if (obj_read(pick_sha1, &type, &data, &dlen) != 0 || type != OBJ_COMMIT) {
        fprintf(stderr, "forge: '%s' is not a commit\n", pick_sha1);
        if (data) free(data); return 1;
    }

    char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
    char author[512], message[4096];
    parse_commit((char *)data, dlen, tree, parent, author, message, sizeof(message));
    free(data);

    /* Get what the picked commit changed (this commit vs its parent) */
    int pick_cnt, pick_parent_cnt;
    IndexEntry *pick_files        = get_committed_files(pick_sha1, &pick_cnt);
    IndexEntry *pick_parent_files = get_committed_files(parent[0] ? parent : NULL,
                                                         &pick_parent_cnt);

    /* Apply the delta: files that changed between pick_parent and pick */
    char head_sha1[SHA1_HEX_SIZE] = "";
    head_resolve(head_sha1);
    int current_cnt;
    IndexEntry *current = get_committed_files(head_sha1, &current_cnt);

    int changed = 0;
    for (int i = 0; i < pick_cnt; i++) {
        IndexEntry *p = index_find(pick_parent_files, pick_parent_cnt,
                                   pick_files[i].path);
        if (p && strcmp(p->sha1, pick_files[i].sha1) == 0) continue;
        /* Apply to working tree */
        if (blob_to_file(pick_files[i].sha1, pick_files[i].path,
                         pick_files[i].mode) == 0)
            changed++;
    }

    /* Build new snapshot: current HEAD + cherry-picked changes */
    int new_cap = current_cnt + pick_cnt + 1;
    IndexEntry *new_snap = malloc((size_t)new_cap * sizeof(IndexEntry));
    if (!new_snap) die("out of memory");
    int new_cnt = 0;

    for (int i = 0; i < current_cnt; i++) {
        int overridden = 0;
        for (int j = 0; j < pick_cnt; j++) {
            IndexEntry *p = index_find(pick_parent_files, pick_parent_cnt,
                                       pick_files[j].path);
            if (strcmp(current[i].path, pick_files[j].path) == 0 &&
                !(p && strcmp(p->sha1, pick_files[j].sha1) == 0)) {
                overridden = 1; break;
            }
        }
        if (!overridden) new_snap[new_cnt++] = current[i];
    }
    for (int i = 0; i < pick_cnt; i++) {
        IndexEntry *p = index_find(pick_parent_files, pick_parent_cnt,
                                   pick_files[i].path);
        if (p && strcmp(p->sha1, pick_files[i].sha1) == 0) continue;
        new_snap[new_cnt++] = pick_files[i];
    }

    /* Build and write new commit */
    char tree_sha1[SHA1_HEX_SIZE];
    if (tree_build_from_index(new_snap, new_cnt, "", tree_sha1) != 0)
        die("failed to build tree");

    char author_name[256], author_email[256];
    config_get("name",  author_name,  sizeof(author_name),  "Anonymous");
    config_get("email", author_email, sizeof(author_email), "anon@forge.local");
    time_t now = time(NULL);
    struct tm *tm_local = localtime(&now);
    char tz[8]; strftime(tz, sizeof(tz), "%z", tm_local);
    char ts[64]; snprintf(ts, sizeof(ts), "%ld %s", (long)now, tz);

    char commit_buf[8192]; int commit_len = 0;
    commit_len += snprintf(commit_buf + commit_len,
                           sizeof(commit_buf) - (size_t)commit_len,
                           "tree %s\n", tree_sha1);
    if (head_sha1[0])
        commit_len += snprintf(commit_buf + commit_len,
                               sizeof(commit_buf) - (size_t)commit_len,
                               "parent %s\n", head_sha1);
    commit_len += snprintf(commit_buf + commit_len,
                           sizeof(commit_buf) - (size_t)commit_len,
                           "author %s <%s> %s\n"
                           "committer %s <%s> %s\n\n%s\n",
                           author_name, author_email, ts,
                           author_name, author_email, ts, message);

    char new_commit[SHA1_HEX_SIZE];
    if (obj_write((uint8_t *)commit_buf, (size_t)commit_len,
                  OBJ_COMMIT, new_commit) != 0)
        die("failed to write cherry-pick commit");

    head_advance(new_commit);
    index_write(new_snap, new_cnt);

    free(current); free(pick_files); free(pick_parent_files); free(new_snap);

    char branch[256] = ""; head_branch(branch);
    printf("[\033[33m%.8s\033[0m] (\033[1m%s\033[0m) %s\n",
           new_commit, branch, message);
    printf("  cherry-picked %.8s \xe2\x86\x92 %.8s (%d file%s changed)\n",
           pick_sha1, new_commit, changed, changed == 1 ? "" : "s");
    return 0;
}

/* Summarise commit count and messages grouped by author. */

static int cmd_shortlog(int argc, char *argv[])
{
    (void)argc; (void)argv;
    if (!is_forge_repo()) die("not a forge repository");

    char sha1[SHA1_HEX_SIZE];
    if (head_resolve(sha1) != 0 || !sha1[0]) {
        printf("forge: no commits yet\n"); return 0;
    }

    /* Collect all commits */
    typedef struct { char author[512]; char msgs[64][256]; int cnt; } AuthorEntry;
    AuthorEntry *authors = NULL;
    int nauthors = 0, auth_cap = 32;
    authors = malloc((size_t)auth_cap * sizeof(AuthorEntry));
    if (!authors) die("out of memory");

    while (sha1[0]) {
        ObjType type; uint8_t *data; size_t dlen;
        if (obj_read(sha1, &type, &data, &dlen) != 0) break;
        if (type != OBJ_COMMIT) { free(data); break; }

        char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
        char author[512], message[4096];
        parse_commit((char *)data, dlen, tree, parent, author, message, sizeof(message));
        free(data);

        /* Trim timestamp from author: "Name <email> EPOCH TZ" -> "Name <email>" */
        char author_clean[512];
        snprintf(author_clean, sizeof(author_clean), "%s", author);
        /* strip trailing " EPOCH TZ" */
        char *p = strrchr(author_clean, ' ');
        if (p) { *p = '\0'; p = strrchr(author_clean, ' '); if (p) *p = '\0'; }

        /* Find or create author entry */
        int found = 0;
        for (int i = 0; i < nauthors; i++) {
            if (strcmp(authors[i].author, author_clean) == 0) {
                if (authors[i].cnt < 64)
                    snprintf(authors[i].msgs[authors[i].cnt++], 256, "%s", message);
                else
                    authors[i].cnt++;
                found = 1; break;
            }
        }
        if (!found) {
            if (nauthors >= auth_cap) {
                auth_cap *= 2;
                authors = realloc(authors, (size_t)auth_cap * sizeof(AuthorEntry));
                if (!authors) die("out of memory");
            }
            snprintf(authors[nauthors].author, 512, "%s", author_clean);
            snprintf(authors[nauthors].msgs[0], 256, "%s", message);
            authors[nauthors].cnt = 1;
            nauthors++;
        }

        if (parent[0]) snprintf(sha1, sizeof(sha1), "%s", parent);
        else sha1[0] = '\0';
    }

    for (int i = 0; i < nauthors; i++) {
        printf("\033[1m%s\033[0m (%d commit%s)\n",
               authors[i].author, authors[i].cnt, authors[i].cnt == 1 ? "" : "s");
        int show = authors[i].cnt < 64 ? authors[i].cnt : 64;
        for (int j = 0; j < show; j++)
            printf("      %s\n", authors[i].msgs[j]);
        if (authors[i].cnt > 64) printf("      ... and %d more\n", authors[i].cnt - 64);
        printf("\n");
    }
    free(authors);
    return 0;
}

/* Tiny ASCII branch graph alongside the one-line log. Used internally by
 * cmd_log when --graph is passed. */

static int cmd_log_graph(void)
{
    if (!is_forge_repo()) die("not a forge repository");

    /* Collect all branches and their tip commits */
    typedef struct { char name[256]; char sha1[SHA1_HEX_SIZE]; } BranchTip;
    BranchTip tips[256]; int ntips = 0;

    DIR *d = opendir(FORGE_HEADS);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) && ntips < 256) {
            if (de->d_name[0] == '.') continue;
            snprintf(tips[ntips].name, 256, "%s", de->d_name);
            char refname[MAX_PATH_LEN];
            snprintf(refname, sizeof(refname), "refs/heads/%s", de->d_name);
            ref_read(refname, tips[ntips].sha1);
            ntips++;
        }
        closedir(d);
    }

    char cur_branch[256] = ""; head_branch(cur_branch);
    char sha1[SHA1_HEX_SIZE];
    head_resolve(sha1);

    int depth = 0;
    while (sha1[0]) {
        ObjType type; uint8_t *data; size_t dlen;
        if (obj_read(sha1, &type, &data, &dlen) != 0) break;
        if (type != OBJ_COMMIT) { free(data); break; }

        char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
        char author[512], message[4096];
        parse_commit((char *)data, dlen, tree, parent, author, message, sizeof(message));
        free(data);

        /* Check which branch tips point at this commit */
        char labels[512] = "";
        for (int i = 0; i < ntips; i++) {
            if (strcmp(tips[i].sha1, sha1) == 0) {
                if (labels[0]) strncat(labels, ", ", sizeof(labels) - strlen(labels) - 1);
                if (strcmp(tips[i].name, cur_branch) == 0)
                    strncat(labels, "\033[32m", sizeof(labels) - strlen(labels) - 1);
                strncat(labels, tips[i].name, sizeof(labels) - strlen(labels) - 1);
                if (strcmp(tips[i].name, cur_branch) == 0)
                    strncat(labels, "\033[33m", sizeof(labels) - strlen(labels) - 1);
            }
        }

        /* Simple linear graph: * for commit node, | for continuation */
        const char *node = (depth == 0) ? "*" : "*";
        if (labels[0])
            printf("\033[33m%s\033[0m (\033[33m%.8s\033[0m) (\033[90m%s\033[0m) %s\n",
                   node, sha1, labels, message);
        else
            printf("\033[33m%s\033[0m (\033[33m%.8s\033[0m) %s\n",
                   node, sha1, message);

        if (parent[0]) {
            printf("|\n");
            snprintf(sha1, sizeof(sha1), "%s", parent);
        } else sha1[0] = '\0';
        depth++;
    }
    return 0;
}


static void usage(void)
{
    puts(
        "\n"
        "  \033[1m███████╗ ██████╗ ██████╗  ██████╗ ███████╗\033[0m\n"
        "  \033[1m██╔════╝██╔═══██╗██╔══██╗██╔════╝ ██╔════╝\033[0m\n"
        "  \033[1m█████╗  ██║   ██║██████╔╝██║  ███╗█████╗  \033[0m\n"
        "  \033[1m██╔══╝  ██║   ██║██╔══██╗██║   ██║██╔══╝  \033[0m\n"
        "  \033[1m██║     ╚██████╔╝██║  ██║╚██████╔╝███████╗\033[0m\n"
        "  \033[1m╚═╝      ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚══════╝\033[0m\n"
        "  Fast, content-addressed version control.\n"
        "  Author: Shahriar Dhrubo\n"
        "\n"
        "Usage: \033[1mforge\033[0m <command> [options]\n"
        "\n"
        "\033[4mRepository\033[0m\n"
        "  init                      Create a new FORGE repository\n"
        "  clone <url> [<dir>]       Clone a remote repository\n"
        "\n"
        "\033[4mWorking tree\033[0m\n"
        "  put <file> [...]          Stage file(s)\n"
        "  put -a                    Stage all files\n"
        "  rm [--cached] <file>      Remove file from index (+ disk)\n"
        "  mv <src> <dst>            Rename a tracked file\n"
        "  restore [--source <sha1>] <file>  Restore file from HEAD\n"
        "  grep [-n] [-i] <pattern>  Search tracked files\n"
        "  status                    Show working tree status\n"
        "  diff                      Show working tree changes\n"
        "  diff --cached             Show staged changes vs HEAD\n"
        "\n"
        "\033[4mHistory\033[0m\n"
        "  msg -m <text>             Record a new commit\n"
        "  log [--oneline]           Show commit log\n"
        "  log --graph               Show branch graph\n"
        "  show [<sha1>]             Inspect a commit or object\n"
        "  reset                     Clear staging area\n"
        "  reset --soft <sha1>       Move HEAD, keep index\n"
        "  reset --hard <sha1>       Move HEAD, restore working tree\n"
        "\n"
        "\033[4mBranches & Tags\033[0m\n"
        "  branch                    List branches\n"
        "  branch <n>                Create branch\n"
        "  branch -d <n>             Delete branch\n"
        "  checkout [-b] <branch>    Switch (or create+switch) branch\n"
        "  merge <branch>            Fast-forward merge\n"
        "  cherry-pick <sha1>        Apply a commit onto HEAD\n"
        "  shortlog                  Commit summary by author\n"
        "  tag [-l]                  List tags\n"
        "  tag <n> [<sha1>]          Create lightweight tag\n"
        "  tag -d <n>                Delete tag\n"
        "\n"
        "\033[4mRemote (SSH + rsync)\033[0m\n"
        "  remote add <n> <url>      Add a named remote\n"
        "  remote remove <n>         Remove a remote\n"
        "  remote list               List remotes\n"
        "  shoot [<remote>]          Push to remote over SSH\n"
        "  fetch [<remote>]          Fetch from remote over SSH\n"
        "\n"
        "\033[4mServer\033[0m\n"
        "  serve [--port <n>]        Serve repo over TCP\n"
        "\n"
        "\033[4mDebug\033[0m\n"
        "  ls-files                  List all tracked files\n"
        "  config <key> [<value>]    Get or set config values\n"
        "  stash                     Stash working tree changes\n"
        "  stash pop                 Restore last stash\n"
        "  stash list | drop         List or discard stash\n"
        "  cat-obj <sha1>            Dump raw object\n"
        "  hash-obj [-w] <file>      Hash a file (optionally store)\n"
        "  list-objects              List all stored objects\n"
        "\n"
        "Remote URL: \033[1muser@host:/path/to/repo\033[0m\n"
        "Requires:   ssh + rsync\n"
    );
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(); return 1; }

    const char *cmd  = argv[1];
    int   sub_argc   = argc - 2;
    char **sub_argv  = argv + 2;

    if (!strcmp(cmd, "init"))         return cmd_init(sub_argc, sub_argv);
    if (!strcmp(cmd, "clone"))        return cmd_clone(sub_argc, sub_argv);
    if (!strcmp(cmd, "put"))          return cmd_put(sub_argc, sub_argv);
    if (!strcmp(cmd, "msg"))          return cmd_msg(sub_argc, sub_argv);
    if (!strcmp(cmd, "status"))       return cmd_status(sub_argc, sub_argv);
    if (!strcmp(cmd, "log"))          return cmd_log(sub_argc, sub_argv);
    if (!strcmp(cmd, "show"))         return cmd_show(sub_argc, sub_argv);
    if (!strcmp(cmd, "diff"))         return cmd_diff(sub_argc, sub_argv);
    if (!strcmp(cmd, "branch"))       return cmd_branch(sub_argc, sub_argv);
    if (!strcmp(cmd, "checkout"))     return cmd_checkout(sub_argc, sub_argv);
    if (!strcmp(cmd, "rm"))           return cmd_rm(sub_argc, sub_argv);
    if (!strcmp(cmd, "reset"))        return cmd_reset(sub_argc, sub_argv);
    if (!strcmp(cmd, "tag"))          return cmd_tag(sub_argc, sub_argv);
    if (!strcmp(cmd, "merge"))        return cmd_merge(sub_argc, sub_argv);
    if (!strcmp(cmd, "remote"))       return cmd_remote(sub_argc, sub_argv);
    if (!strcmp(cmd, "shoot"))        return cmd_shoot(sub_argc, sub_argv);
    if (!strcmp(cmd, "fetch"))        return cmd_fetch(sub_argc, sub_argv);
    if (!strcmp(cmd, "serve"))        return cmd_serve(sub_argc, sub_argv);
    if (!strcmp(cmd, "cat-obj"))      return cmd_cat_obj(sub_argc, sub_argv);
    if (!strcmp(cmd, "hash-obj"))     return cmd_hash_obj(sub_argc, sub_argv);
    if (!strcmp(cmd, "list-objects"))  return cmd_list_objects(sub_argc, sub_argv);
    if (!strcmp(cmd, "ls-files"))     return cmd_ls_files(sub_argc, sub_argv);
    if (!strcmp(cmd, "config"))       return cmd_config(sub_argc, sub_argv);
    if (!strcmp(cmd, "stash"))        return cmd_stash(sub_argc, sub_argv);
    if (!strcmp(cmd, "restore"))      return cmd_restore(sub_argc, sub_argv);
    if (!strcmp(cmd, "mv"))           return cmd_mv(sub_argc, sub_argv);
    if (!strcmp(cmd, "grep"))         return cmd_grep(sub_argc, sub_argv);
    if (!strcmp(cmd, "cherry-pick"))  return cmd_cherry_pick(sub_argc, sub_argv);
    if (!strcmp(cmd, "shortlog"))     return cmd_shortlog(sub_argc, sub_argv);
    if (!strcmp(cmd, "help") || !strcmp(cmd, "--help")) { usage(); return 0; }

    fprintf(stderr, "forge: unknown command '\033[1m%s\033[0m'. "
                    "Run '\033[1mforge help\033[0m'.\n", cmd);
    return 1;
}