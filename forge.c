/*
 * forge.c — FORGE Version Control System
 *
 * A minimal distributed VCS designed as a research and experimental
 * implementation inspired by Linus's early Git architecture.
 * This explores content addressable storage (SHA-1) and simplified
 * reference handling to better understand core version control concepts.
 *
 * Author: Shahriar Dhrubo
 *
 *  Commands:
 *    forge init              — initialise a new repository
 *    forge put <files>       — stage files          (kinda like git add)
 *    forge put -a            — stage all files
 *    forge msg -m <text>     — commit staged files  (awa git commit)
 *    forge status            — show working tree status
 *    forge log               — show commit history
 *    forge diff              — diff working tree vs index
 *    forge branch [<n>]      — list or create branches
 *    forge branch -d <n>     — delete a branch
 *    forge checkout <branch> — switch branch
 *    forge remote add <n> <url> — add a named remote
 *    forge remote list       — list remotes
 *    forge shoot [<remote>]  — push to remote       (awa git push)
 *    forge fetch [<remote>]  — fetch from remote    (awa git pull)
 *    forge cat-obj <sha1>    — dump an object (debug)
 *
 *  Author/identity read from .forge/config:
 *    [user]
 *        name  = Your Name
 *        email = you@example.com
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
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

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
    write(fd, buf, len);
    close(fd);
    return 0;
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
    getcwd(cwd, sizeof(cwd));
    snprintf(out, size, "%s", cwd);
    // Walk up until we find .forge 
    while (1) {
        char check[MAX_PATH_LEN];
        snprintf(check, sizeof(check), "%s/.forge", out);
        struct stat st;
        if (stat(check, &st) == 0 && S_ISDIR(st.st_mode)) return;
        // Go up one level 
        char *slash = strrchr(out, '/');
        if (!slash || slash == out) break;
        *slash = '\0';
    }
    snprintf(out, size, "%s", cwd);//if fallback: stay in cwd
}


static void config_get(const char *key, char *out, size_t size,
                       const char *def)
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
                eq++;
                while (*eq == ' ') eq++;
                snprintf(out, size, "%s", eq);
                break;
            }
        }
    }
    fclose(f);
}


static int cmd_init(int argc, char *argv[])
{
    const char *dir = (argc > 0) ? argv[0] : ".";

    if (strcmp(dir, ".") != 0) {
        if (mkdir(dir, 0755) != 0 && errno != EEXIST)
            die("cannot create directory '%s': %s", dir, strerror(errno));
        if (chdir(dir) != 0)
            die("cannot enter '%s'", dir);
    }

    if (is_forge_repo()) {
        printf("forge: repository already exists at %s\n", FORGE_DIR);
        return 0;
    }

    /* Create directory skeleton */
    const char *dirs[] = {
        FORGE_DIR, FORGE_OBJECTS, FORGE_REFS,
        FORGE_HEADS, ".forge/refs/tags", NULL
    };
    for (int i = 0; dirs[i]; i++) {
        if (mkdir(dirs[i], 0755) != 0)
            die("mkdir '%s' failed: %s", dirs[i], strerror(errno));
    }

    /* HEAD → refs/heads/main */
    write_file_str(FORGE_HEAD, "ref: refs/heads/main\n");

    /* Description */
    write_file_str(FORGE_DESCRIPTION,
                   "Unnamed FORGE repository. Edit this file to name it.\n");

    /* Default config */
    write_file_str(FORGE_CONFIG,
                   "[core]\n"
                   "    repositoryformatversion = 0\n"
                   "    filemode = true\n"
                   "\n"
                   "[user]\n"
                   "    name  = Anonymous\n"
                   "    email = anon@forge.local\n");

    /* Default .forgeignore */
    if (access(".forgeignore", F_OK) != 0) {
        write_file_str(".forgeignore",
                       "# Files and patterns ignored by forge\n"
                       ".forge\n"
                       "*.o\n"
                       "*.a\n"
                       "*.so\n"
                       "*.d\n");
    }

    char cwd[MAX_PATH_LEN];
    getcwd(cwd, sizeof(cwd));
    printf("Initialised empty FORGE repository at %s/.forge/\n", cwd);
    printf("Edit %s to set your name and email.\n", FORGE_CONFIG);
    return 0;
}


static int cmd_put(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository (missing .forge)");

    if (argc == 0) {
        fprintf(stderr, "usage: forge put <file> [files...]\n"
                        "       forge put -a\n");
        return 1;
    }

    if (argc == 1 && strcmp(argv[0], "-a") == 0) {
        printf("Staging all files...\n");
        return index_add_all();
    }

    int rc = 0;
    for (int i = 0; i < argc; i++) {
        if (index_add(argv[i]) != 0) rc = 1;
    }
    return rc;
}


static int cmd_msg(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    char message[4096] = "";

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            snprintf(message, sizeof(message), "%s", argv[++i]);
        }
    }
    if (!message[0]) {
        fprintf(stderr, "usage: forge msg -m \"your commit message\"\n");
        return 1;
    }

    /* Read index */
    IndexEntry *entries; int cnt;
    if (index_read(&entries, &cnt) != 0) die("cannot read index");
    if (cnt == 0) {
        fprintf(stderr, "forge: nothing staged — use 'forge put' first\n");
        free(entries);
        return 1;
    }

    /* Build root tree */
    char tree_sha1[SHA1_HEX_SIZE];
    if (tree_build_from_index(entries, cnt, "", tree_sha1) != 0)
        die("failed to build tree object");

    /* Get parent commit */
    char parent_sha1[SHA1_HEX_SIZE] = "";
    head_resolve(parent_sha1);   /* empty string if first commit */

    /* Get author/committer identity */
    char author_name[256], author_email[256];
    config_get("name",  author_name,  sizeof(author_name),  "Anonymous");
    config_get("email", author_email, sizeof(author_email), "anon@forge.local");

    /* Timestamp */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[64];
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat"
    strftime(ts, sizeof(ts), "%s %z", tm);
    #pragma GCC diagnostic pop

    /* Build commit object text */
    char commit_buf[8192];
    int  commit_len = 0;

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
                           "committer %s <%s> %s\n"
                           "\n"
                           "%s\n",
                           author_name, author_email, ts,
                           author_name, author_email, ts,
                           message);

    char commit_sha1[SHA1_HEX_SIZE];
    if (obj_write((uint8_t *)commit_buf, (size_t)commit_len,
                  OBJ_COMMIT, commit_sha1) != 0)
        die("failed to write commit object");

    /* Advance HEAD */
    if (head_advance(commit_sha1) != 0)
        die("failed to update HEAD");

    /* Clear index */
    write_file_str(FORGE_INDEX, "");

    free(entries);

    printf("[%s] %s\n", commit_sha1 + 0, message);
    printf("  tree:   %s\n", tree_sha1);
    if (parent_sha1[0])
        printf("  parent: %s\n", parent_sha1);
    printf("  %d file%s staged\n", cnt, cnt == 1 ? "" : "s");
    return 0;
}
static void collect_files(const char *dir, char **list, int *cnt, int cap)
{
    DIR *d = opendir(dir[0] ? dir : ".");
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && *cnt < cap) {
        if (de->d_name[0] == '.') continue;
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path),
                 "%s%s%s", dir[0] ? dir : "", dir[0] ? "/" : "", de->d_name);
        if (is_ignored(path)) continue;
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            collect_files(path, list, cnt, cap);
        } else if (S_ISREG(st.st_mode)) {
            list[(*cnt)++] = xstrdup(path);
        }
    }
    closedir(d);
}

static int cmd_status(int argc, char *argv[])
{
    (void)argc; (void)argv;
    if (!is_forge_repo()) die("not a forge repository");

    /* Branch info */
    char branch[256] = "(detached)";
    head_branch(branch);
    printf("On branch \033[1m%s\033[0m\n", branch);

    /* Staged files */
    IndexEntry *idx; int idx_cnt;
    if (index_read(&idx, &idx_cnt) != 0) die("cannot read index");

    /* Last commit's tree (to detect what changed) */
    char head_sha1[SHA1_HEX_SIZE] = "";
    head_resolve(head_sha1);

    if (idx_cnt > 0) {
        printf("\nChanges to be committed (staged):\n");
        printf("  \033[32m(use 'forge msg -m \"...\"' to commit)\033[0m\n\n");
        for (int i = 0; i < idx_cnt; i++)
            printf("  \033[32mstaged:   %s\033[0m\n", idx[i].path);
    } else {
        printf("\nNothing staged.\n");
    }

    /* Unstaged changes: walk working tree, compare against index */
    char *wt_files[MAX_ENTRIES]; int wt_cnt = 0;
    collect_files("", wt_files, &wt_cnt, MAX_ENTRIES);

    int has_unstaged = 0;
    for (int i = 0; i < wt_cnt; i++) {
        const char *p = wt_files[i];
        IndexEntry *e = index_find(idx, idx_cnt, p);

        /* Hash working tree file to compare */
        char wt_sha1[SHA1_HEX_SIZE];
        obj_hash(NULL, 0, OBJ_BLOB, wt_sha1);   /* placeholder */

        /* Re hash file without writing */
        int fd = open(p, O_RDONLY);
        if (fd < 0) { free(wt_files[i]); continue; }
        struct stat st; fstat(fd, &st);
        uint8_t *data = malloc((size_t)st.st_size);
        read(fd, data, (size_t)st.st_size);
        close(fd);
        obj_hash(data, (size_t)st.st_size, OBJ_BLOB, wt_sha1);
        free(data);

        if (!e) {
            if (!has_unstaged) {
                printf("\nUntracked files:\n");
                printf("  \033[31m(use 'forge put <file>' to stage)\033[0m\n\n");
                has_unstaged = 1;
            }
            printf("  \033[31muntracked: %s\033[0m\n", p);
        } else if (strcmp(e->sha1, wt_sha1) != 0) {
            /* Modified after staging */
            if (!has_unstaged) {
                printf("\nModified but not re-staged:\n");
                has_unstaged = 1;
            }
            printf("  \033[33mmodified:  %s\033[0m\n", p);
        }
        free(wt_files[i]);
    }

    if (idx_cnt == 0 && !has_unstaged)
        printf("  nothing to commit, working tree clean\n");

    free(idx);
    return 0;
}


static void parse_commit(const char *data, size_t len,
                         char tree[SHA1_HEX_SIZE],
                         char parent[SHA1_HEX_SIZE],
                         char author[512],
                         char *message, size_t msg_size)
{
    tree[0] = parent[0] = author[0] = message[0] = '\0';

    const char *p = data;
    const char *end = data + len;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl) nl = end;
        size_t llen = (size_t)(nl - p);

        if (llen == 0) {
            /* Blank line: rest is message */
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

static int cmd_log(int argc, char *argv[])
{
    (void)argc; (void)argv;
    if (!is_forge_repo()) die("not a forge repository");

    char sha1[SHA1_HEX_SIZE];
    if (head_resolve(sha1) != 0 || !sha1[0]) {
        printf("forge: no commits yet\n");
        return 0;
    }

    int oneline = (argc > 0 && strcmp(argv[0], "--oneline") == 0);

    while (sha1[0]) {
        ObjType type; uint8_t *data; size_t dlen;
        if (obj_read(sha1, &type, &data, &dlen) != 0)
            die("cannot read commit %s", sha1);
        if (type != OBJ_COMMIT) die("expected commit object");

        char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
        char author[512], message[4096];
        parse_commit((char *)data, dlen, tree, parent, author, message, sizeof(message));
        free(data);

        if (oneline) {
            printf("\033[33m%.8s\033[0m %s\n", sha1, message);
        } else {
            printf("\033[33mcommit %s\033[0m\n", sha1);
            printf("Author: %s\n", author);
            printf("\n    %s\n\n", message);
        }

        if (parent[0]) {
            snprintf(sha1, SHA1_HEX_SIZE, "%s", parent);
        } else {
            sha1[0] = '\0';
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
    (void)argc; (void)argv;
    if (!is_forge_repo()) die("not a forge repository");
 
    IndexEntry *idx; int idx_cnt;
    if (index_read(&idx, &idx_cnt) != 0) die("cannot read index");
 
    if (idx_cnt > 0) {
        for (int i = 0; i < idx_cnt; i++)
            run_diff(idx[i].path, idx[i].sha1, idx[i].mode, idx[i].path);
    } else {
        char head_sha1[SHA1_HEX_SIZE] = "";
        head_resolve(head_sha1);
        int committed_cnt;
        IndexEntry *committed = get_committed_files(head_sha1, &committed_cnt);
        if (committed_cnt == 0)
            printf("forge: nothing to diff\n");
        else
            for (int i = 0; i < committed_cnt; i++)
                run_diff(committed[i].path, committed[i].sha1,
                         committed[i].mode, committed[i].path);
        free(committed);
    }
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
    fprintf(stderr, "usage: forge branch [<n>]\n       forge branch -d <n>\n");
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
    if (sha1[0]) {
        ObjType type; uint8_t *data; size_t dlen;
        if (obj_read(sha1, &type, &data, &dlen) == 0 && type == OBJ_COMMIT) {
            char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
            char author[512], message[4096];
            parse_commit((char *)data, dlen, tree, parent, author, message, sizeof(message));
            free(data);
            if (tree[0] && tree_checkout(tree, NULL) != 0)
                fprintf(stderr, "forge: warning: tree restore had errors\n");
        } else if (data) free(data);
    }
    head_set_ref(target);
    write_file_str(FORGE_INDEX, "");
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
        else if (strcmp(argv[i], "--soft") == 0) { /* default */ }
        else target_sha1 = argv[i];
    }
    if (!target_sha1) {
        write_file_str(FORGE_INDEX, "");
        printf("forge: index cleared (HEAD unchanged)\n");
        return 0;
    }
 
    char sha1[SHA1_HEX_SIZE];
    if (strlen(target_sha1) == SHA1_HEX_LEN)
        snprintf(sha1, sizeof(sha1), "%s", target_sha1);
    else if (branch_resolve(target_sha1, sha1) != 0) {
        fprintf(stderr, "forge: cannot resolve '%s'\n", target_sha1); return 1;
    }
 
    ObjType type; uint8_t *data; size_t dlen;
    if (obj_read(sha1, &type, &data, &dlen) != 0 || type != OBJ_COMMIT) {
        fprintf(stderr, "forge: '%s' is not a commit\n", sha1);
        if (type && data) free(data); return 1;
    }
    if (hard) {
        char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
        char author[512], message[4096];
        parse_commit((char *)data, dlen, tree, parent, author, message, sizeof(message));
        if (tree[0]) tree_checkout(tree, NULL);
        write_file_str(FORGE_INDEX, "");
        printf("forge: hard reset → %s (working tree restored)\n", sha1);
    } else {
        printf("forge: soft reset → %s (index preserved)\n", sha1);
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
        printf("forge: deleted tag '%s'\n", argv[1]); return 0;
    }
    /* Create tag */
    char sha1[SHA1_HEX_SIZE];
    if (argc >= 2) snprintf(sha1, sizeof(sha1), "%.40s", argv[1]);
    else if (head_resolve(sha1) != 0 || !sha1[0]) {
        fprintf(stderr, "forge: no commits yet\n"); return 1;
    }
    char ref_path[MAX_PATH_LEN];
    snprintf(ref_path, sizeof(ref_path), "refs/tags/%s", argv[0]);
    if (ref_write(ref_path, sha1) != 0) {
        fprintf(stderr, "forge: failed to create tag '%s'\n", argv[0]); return 1;
    }
    printf("forge: tag '\033[1m%s\033[0m' → %.8s...\n", argv[0], sha1);
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
        snprintf(local_dir, sizeof(local_dir), "%s", base[0] ? base : "forge_clone");
    }
    printf("forge clone: %s → %s\n", url, local_dir);
 
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
    snprintf(cmd, sizeof(cmd),
             "rsync -az --progress %s:%s/.forge/objects/ %s/",
             user_host, rpath, FORGE_OBJECTS);
    printf("  fetching objects...\n");
    if (system(cmd) != 0) { fprintf(stderr, "forge: rsync failed\n"); return 1; }
 
    snprintf(cmd, sizeof(cmd),
             "rsync -az %s:%s/.forge/refs/ %s/", user_host, rpath, FORGE_REFS);
    if (system(cmd) != 0) {
        fprintf(stderr, "forge: failed to fetch refs\n"); return 1;
    }
 
    snprintf(cmd, sizeof(cmd), "ssh %s 'cat %s/.forge/HEAD'", user_host, rpath);
    FILE *pipe = popen(cmd, "r");
    char head_content[MAX_PATH_LEN] = "ref: refs/heads/main";
    if (pipe) {
        if (fgets(head_content, sizeof(head_content), pipe)) rtrim(head_content);
        pclose(pipe);
    }
    write_file_str(FORGE_HEAD, head_content);
 
    char sha1[SHA1_HEX_SIZE] = "";
    head_resolve(sha1);
    if (sha1[0]) {
        ObjType type; uint8_t *data; size_t dlen;
        if (obj_read(sha1, &type, &data, &dlen) == 0 && type == OBJ_COMMIT) {
            char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
            char author[512], message[4096];
            parse_commit((char *)data, dlen, tree, parent, author, message, sizeof(message));
            free(data);
            if (tree[0]) tree_checkout(tree, NULL);
        } else if (data) free(data);
    }
 
    write_file_str(FORGE_REMOTES, "");
    remote_add("origin", url);
    write_file_str(".forgeignore", "# FORGE ignore\n.forge\n*.o\n*.a\n");
    write_file_str(FORGE_INDEX, "");
 
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
        if (f) { if (fwrite(buf, 1, (size_t)blen, f) != (size_t)blen) {/* ok */} fclose(f); }
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
 * forge serve  — minimal TCP object server
 *
 * Protocol (line-oriented text over TCP):
 *   Client sends:
 *     REFS          → server replies with "REF <refname> <sha1>" lines, then "OK"
 *     HAVE <sha1>   → client declares it has this object; server replies "OK"
 *     WANT <sha1>   → client requests this object; server replies "OK"
 *     DONE          → server sends all WANTed objects not in HAVE set:
 *                     "OBJ <sha1> <type> <size>\n<raw_bytes>\n"
 *                     then "OK"
 *     QUIT          → close connection
 *  */
 
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
            /* Send all heads and tags */
            DIR *d = opendir(FORGE_HEADS);
            if (d) {
                struct dirent *de;
                while ((de = readdir(d))) {
                    if (de->d_name[0] == '.') continue;
                    char sha1[SHA1_HEX_SIZE], refname[MAX_PATH_LEN];
                    snprintf(refname, sizeof(refname), "refs/heads/%s", de->d_name);
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
                    snprintf(refname, sizeof(refname), "refs/tags/%s", de->d_name);
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
 
    signal(SIGCHLD, SIG_IGN);   /* auto-reap children */
 
    char cwd[MAX_PATH_LEN];
    if (!getcwd(cwd, sizeof(cwd))) snprintf(cwd, sizeof(cwd), ".");
    printf("forge serve: \033[1mlistening on port %d\033[0m\n", port);
    printf("  repo:  %s\n  Press Ctrl-C to stop.\n\n", cwd);
 
    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) continue;
        printf("  [+] connection from %s\n", inet_ntoa(caddr.sin_addr));
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

// debug 
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
        "\n"
        "Usage: \033[1mforge\033[0m <command> [options]\n"
        "\n"
        "\033[4mRepository\033[0m\n"
        "  init                      Create a new FORGE repository\n"
        "  clone <url> [<dir>]       Clone a remote repository\n"
        "\n"
        "\033[4mWorking tree\033[0m\n"
        "  put <file> [...]          Stage file(s)              \033[90m[≡ git add]\033[0m\n"
        "  put -a                    Stage all files\n"
        "  rm [--cached] <file>      Remove file from index (+ disk)\n"
        "  status                    Show working tree status\n"
        "  diff                      Show unstaged changes\n"
        "\n"
        "\033[4mHistory\033[0m\n"
        "  msg -m <text>             Record a new commit        \033[90m[≡ git commit]\033[0m\n"
        "  log [--oneline]           Show commit log\n"
        "  show [<sha1>]             Inspect a commit or object\n"
        "  reset                     Clear staging area\n"
        "  reset --soft <sha1>       Move HEAD, keep index\n"
        "  reset --hard <sha1>       Move HEAD, restore working tree\n"
        "\n"
        "\033[4mBranches & Tags\033[0m\n"
        "  branch                    List branches\n"
        "  branch <n>             Create branch\n"
        "  branch -d <n>          Delete branch\n"
        "  checkout [-b] <branch>    Switch (or create+switch) branch\n"
        "  tag [-l]                  List tags\n"
        "  tag <n> [<sha1>]       Create lightweight tag\n"
        "  tag -d <n>             Delete tag\n"
        "\n"
        "\033[4mRemote (SSH + rsync)\033[0m\n"
        "  remote add <n> <url>      Add a named remote\n"
        "  remote remove <n>         Remove a remote\n"
        "  remote list               List remotes\n"
        "  shoot [<remote>]          Push to remote             \033[90m[≡ git push]\033[0m\n"
        "  fetch [<remote>]          Fetch from remote          \033[90m[≡ git pull]\033[0m\n"
        "\n"
        "\033[4mServer\033[0m\n"
        "  serve [--port <n>]        Serve repo over TCP        \033[90m[≡ git daemon]\033[0m\n"
        "\n"
        "\033[4mDebug\033[0m\n"
        "  cat-obj <sha1>            Dump raw object\n"
        "  hash-obj [-w] <file>      Hash a file (optionally store)\n"
        "  list-objects              List all stored objects\n"
        "\n"
        "Remote URL:  \033[1muser@host:/path/to/repo\033[0m\n"
        "Requires:    ssh + rsync (standard on any Linux machine)\n"
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
    if (!strcmp(cmd, "remote"))       return cmd_remote(sub_argc, sub_argv);
    if (!strcmp(cmd, "shoot"))        return cmd_shoot(sub_argc, sub_argv);
    if (!strcmp(cmd, "fetch"))        return cmd_fetch(sub_argc, sub_argv);
    if (!strcmp(cmd, "serve"))        return cmd_serve(sub_argc, sub_argv);
    if (!strcmp(cmd, "cat-obj"))      return cmd_cat_obj(sub_argc, sub_argv);
    if (!strcmp(cmd, "hash-obj"))     return cmd_hash_obj(sub_argc, sub_argv);
    if (!strcmp(cmd, "list-objects")) return cmd_list_objects(sub_argc, sub_argv);
    if (!strcmp(cmd, "help") || !strcmp(cmd, "--help")) { usage(); return 0; }
 
    fprintf(stderr, "forge: unknown command '\033[1m%s\033[0m'. "
                    "Run '\033[1mforge help\033[0m'.\n", cmd);
    return 1;
}