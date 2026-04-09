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


static int cmd_diff(int argc, char *argv[])
{
    (void)argc; (void)argv;
    if (!is_forge_repo()) die("not a forge repository");

    IndexEntry *idx; int idx_cnt;
    if (index_read(&idx, &idx_cnt) != 0) die("cannot read index");

    if (idx_cnt == 0) {
        printf("forge: nothing staged to diff against\n");
        free(idx);
        return 0;
    }

    for (int i = 0; i < idx_cnt; i++) {
        /* Write staged blob to a temp file */
        char tmp_staged[64];
        snprintf(tmp_staged, sizeof(tmp_staged), "/tmp/forge_a_%d", getpid());
        blob_to_file(idx[i].sha1, tmp_staged, idx[i].mode);

        char cmd[MAX_PATH_LEN * 2];
        struct stat st;
        if (stat(idx[i].path, &st) == 0) {
            /* diff staged vs working tree */
            snprintf(cmd, sizeof(cmd),
                     "diff --label 'a/%s' --label 'b/%s' -u '%s' '%s'",
                     idx[i].path, idx[i].path,
                     tmp_staged, idx[i].path);
        } else {
            /* File deleted */
            snprintf(cmd, sizeof(cmd),
                     "diff --label 'a/%s' --label 'b/%s' -u '%s' /dev/null",
                     idx[i].path, idx[i].path, tmp_staged);
        }
        system(cmd);
        unlink(tmp_staged);
    }
    free(idx);
    return 0;
}


static int cmd_branch(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    if (argc == 0) {
        branch_list();
        return 0;
    }
    if (argc == 2 && strcmp(argv[0], "-d") == 0) {
        return branch_delete(argv[1]);
    }
    if (argc == 1) {
        if (branch_create(argv[0], NULL) == 0) {
            printf("forge: created branch '%s'\n", argv[0]);
            return 0;
        }
        return 1;
    }
    fprintf(stderr, "usage: forge branch [<name>]\n"
                    "       forge branch -d <name>\n");
    return 1;
}


static int cmd_checkout(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");
    if (argc < 1) {
        fprintf(stderr, "usage: forge checkout <branch>\n");
        return 1;
    }

    const char *target = argv[0];

    /* Resolve branch */
    char sha1[SHA1_HEX_SIZE];
    if (branch_resolve(target, sha1) != 0) {
        /* Try creating it (forge checkout -b name) */
        if (argc == 2 && strcmp(argv[0], "-b") == 0) {
            target = argv[1];
            if (branch_create(target, NULL) != 0) return 1;
            if (branch_resolve(target, sha1) != 0) {
                /* Brand new repo, no commits yet */
                sha1[0] = '\0';
            }
        } else {
            fprintf(stderr, "forge: branch '%s' not found\n", target);
            return 1;
        }
    }

    /* Restore working tree from the commit tree */
    if (sha1[0]) {
        ObjType type; uint8_t *data; size_t dlen;
        if (obj_read(sha1, &type, &data, &dlen) != 0)
            die("cannot read commit %s", sha1);

        char tree[SHA1_HEX_SIZE], parent[SHA1_HEX_SIZE];
        char author[512], message[4096];
        parse_commit((char *)data, dlen, tree, parent, author, message, sizeof(message));
        free(data);

        if (tree[0] && tree_checkout(tree, NULL) != 0)
            fprintf(stderr, "forge: warning: tree restore had errors\n");
    }

    /* Clear index */
    write_file_str(FORGE_INDEX, "");

    printf("Switched to branch '\033[1m%s\033[0m'\n", target);
    return 0;
}


static int cmd_remote(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    if (argc == 0 || (argc == 1 && strcmp(argv[0], "list") == 0)) {
        remote_list(); return 0;
    }
    if (argc == 3 && strcmp(argv[0], "add") == 0) {
        return remote_add(argv[1], argv[2]);
    }
    fprintf(stderr, "usage: forge remote add <name> <url>\n"
                    "       forge remote list\n");
    return 1;
}


static int cmd_shoot(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    const char *remote = (argc > 0) ? argv[0] : "origin";
    const char *branch = (argc > 1) ? argv[1] : NULL;

    return remote_shoot(remote, branch);
}


static int cmd_fetch(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");

    const char *remote = (argc > 0) ? argv[0] : "origin";
    const char *branch = (argc > 1) ? argv[1] : NULL;

    return remote_fetch(remote, branch);
}


static int cmd_cat_obj(int argc, char *argv[])
{
    if (!is_forge_repo()) die("not a forge repository");
    if (argc < 1) {
        fprintf(stderr, "usage: forge cat-obj <sha1>\n");
        return 1;
    }

    const char *sha1 = argv[0];
    ObjType type; uint8_t *data; size_t len;
    if (obj_read(sha1, &type, &data, &len) != 0) {
        fprintf(stderr, "forge: object %s not found\n", sha1);
        return 1;
    }

    const char *names[] = { "", "blob", "tree", "commit" };
    printf("type:   %s\nsize:   %zu\n\n", names[type], len);

    if (type == OBJ_TREE) {
        TreeEntry *entries; int cnt;
        if (tree_read(sha1, &entries, &cnt) == 0) {
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


static int cmd_list_objects(int argc, char *argv[])
{
    (void)argc; (void)argv;
    char **sha1s; int cnt;
    if (obj_list_all(&sha1s, &cnt) != 0) return 1;
    for (int i = 0; i < cnt; i++) {
        printf("%s\n", sha1s[i]);
        free(sha1s[i]);
} 


static void usage(void)
{
    // created by TAAG
    puts(
        "\n"
        "  ███████╗ ██████╗ ██████╗  ██████╗ ███████╗\n"
        "  ██╔════╝██╔═══██╗██╔══██╗██╔════╝ ██╔════╝\n"
        "  █████╗  ██║   ██║██████╔╝██║  ███╗█████╗  \n"
        "  ██╔══╝  ██║   ██║██╔══██╗██║   ██║██╔══╝  \n"
        "  ██║     ╚██████╔╝██║  ██║╚██████╔╝███████╗\n"
        "  ╚═╝      ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚══════╝\n"
        "  Fast, content-addressed version control.\n"
        "\n"
        "Usage: forge <command> [options]\n"
        "\n"
        "Repository:\n"
        "  init              Create a new FORGE repository\n"
        "\n"
        "Working tree:\n"
        "  put <file> [...]  Stage file(s) for commit      [≡ git add]\n"
        "  put -a            Stage all files\n"
        "  status            Show working tree status\n"
        "  diff              Diff staged vs working tree\n"
        "\n"
        "History:\n"
        "  msg -m <text>     Record a new commit           [≡ git commit]\n"
        "  log               Show commit log\n"
        "  log --oneline     Compact one-line log\n"
        "  cat-obj <sha1>    Dump raw object (debug)\n"
        "\n"
        "Branches:\n"
        "  branch            List branches\n"
        "  branch <name>     Create a branch\n"
        "  branch -d <name>  Delete a branch\n"
        "  checkout <branch> Switch branch\n"
        "\n"
        "Remote (SSH):\n"
        "  remote add <n> <user@host:path>   Add a named remote\n"
        "  remote list                       List remotes\n"
        "  shoot [<remote>]  Push to remote              [≡ git push]\n"
        "  fetch [<remote>]  Fetch from remote           [≡ git pull]\n"
        "\n"
        "Remote URL format:  user@host:/path/to/repo\n"
        "Requires:           ssh, rsync (standard on any Linux box)\n"
    );
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(); return 1; }

    const char *cmd = argv[1];
    int  sub_argc = argc - 2;
    char **sub_argv = argv + 2;

    if (!strcmp(cmd, "init"))         return cmd_init(sub_argc, sub_argv);
    if (!strcmp(cmd, "put"))          return cmd_put(sub_argc, sub_argv);
    if (!strcmp(cmd, "msg"))          return cmd_msg(sub_argc, sub_argv);
    if (!strcmp(cmd, "status"))       return cmd_status(sub_argc, sub_argv);
    if (!strcmp(cmd, "log"))          return cmd_log(sub_argc, sub_argv);
    if (!strcmp(cmd, "diff"))         return cmd_diff(sub_argc, sub_argv);
    if (!strcmp(cmd, "branch"))       return cmd_branch(sub_argc, sub_argv);
    if (!strcmp(cmd, "checkout"))     return cmd_checkout(sub_argc, sub_argv);
    if (!strcmp(cmd, "remote"))       return cmd_remote(sub_argc, sub_argv);
    if (!strcmp(cmd, "shoot"))        return cmd_shoot(sub_argc, sub_argv);
    if (!strcmp(cmd, "fetch"))        return cmd_fetch(sub_argc, sub_argv);
    if (!strcmp(cmd, "cat-obj"))      return cmd_cat_obj(sub_argc, sub_argv);
    if (!strcmp(cmd, "list-objects")) return cmd_list_objects(sub_argc, sub_argv);
    if (!strcmp(cmd, "help") || !strcmp(cmd, "--help")) { usage(); return 0; }

    fprintf(stderr, "forge: unknown command '%s'. Run 'forge help'.\n", cmd);
    return 1;
}
