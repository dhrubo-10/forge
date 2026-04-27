/**
 * index.c — Staging area implementation
 * Copyright (C) 2026 Shahriar Dhrubo
 * GNU General Public License v3 — see LICENSE
 *
 * Manages .forge/index: the set of files staged for the next commit.
 * Format: one entry per line — <mode>\t<sha1>\t<path>
 *
 */

#include "index.h"
#include "objects.h"
#include "lock.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

/* Minimal glob: * matches any chars, ? matches one char */
static int glob_match(const char *pat, const char *str)
{
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (!*pat) return 1;
            while (*str)
                if (glob_match(pat, str++)) return 1;
            return 0;
        } else if (*pat == '?') {
            if (!*str) return 0;
            pat++; str++;
        } else {
            if (*pat != *str) return 0;
            pat++; str++;
        }
    }
    return (*str == '\0');
}

int is_ignored(const char *path)
{
    if (strncmp(path, ".forge", 6) == 0 &&
        (path[6] == '/' || path[6] == '\0')) return 1;
    if (strncmp(path, "./.forge", 8) == 0) return 1;

    FILE *f = fopen(".forgeignore", "r");
    if (!f) return 0;

    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;

    char line[MAX_PATH_LEN];
    while (fgets(line, sizeof(line), f)) {
        rtrim(line);
        if (!line[0] || line[0] == '#') continue;

        size_t plen = strlen(line);

        /* Trailing slash = directory pattern */
        if (line[plen - 1] == '/') {
            line[--plen] = '\0';
            if (strncmp(path, line, plen) == 0 &&
                (path[plen] == '/' || path[plen] == '\0'))
                { fclose(f); return 1; }
            continue;
        }

        /* Glob wildcard: match against basename */
        if (strchr(line, '*') || strchr(line, '?')) {
            if (glob_match(line, basename)) { fclose(f); return 1; }
            continue;
        }

        /* Exact or directory-prefix match */
        if (strcmp(path, line) == 0 ||
            (strncmp(path, line, plen) == 0 && path[plen] == '/'))
            { fclose(f); return 1; }
    }
    fclose(f);
    return 0;
}

int index_read(IndexEntry **entries_out, int *count_out)
{
    *entries_out = NULL;
    *count_out   = 0;

    FILE *f = fopen(FORGE_INDEX, "r");
    if (!f) return 0;   /* empty index is fine */

    int cap = 256, cnt = 0;
    IndexEntry *entries = malloc((size_t)cap * sizeof(IndexEntry));
    if (!entries) { fclose(f); return -1; }

    char line[MAX_PATH_LEN + 128];
    while (fgets(line, sizeof(line), f)) {
        rtrim(line);
        if (!line[0]) continue;

        if (cnt >= cap) {
            cap *= 2;
            IndexEntry *tmp = realloc(entries, (size_t)cap * sizeof(IndexEntry));
            if (!tmp) { free(entries); fclose(f); return -1; }
            entries = tmp;
        }
        /* Format: mode\tsha1\tpath */
        unsigned int mode;
        char sha1[SHA1_HEX_SIZE];
        char path[MAX_PATH_LEN];
        if (sscanf(line, "%o\t%40s\t%4095[^\n]", &mode, sha1, path) != 3)
            continue;
        entries[cnt].mode = mode;
        snprintf(entries[cnt].sha1, SHA1_HEX_SIZE, "%s", sha1);
        snprintf(entries[cnt].path, MAX_PATH_LEN, "%s", path);
        cnt++;
    }
    fclose(f);
    *entries_out = entries;
    *count_out   = cnt;
    return 0;
}

int index_write(const IndexEntry *entries, int count)
{
    LockFile lk;
    if (lock_file(&lk, FORGE_INDEX) != 0) return -1;

    char line[MAX_PATH_LEN + 128];
    for (int i = 0; i < count; i++) {
        int n = snprintf(line, sizeof(line), "%o\t%s\t%s\n",
                         entries[i].mode, entries[i].sha1, entries[i].path);
        if (lock_write(&lk, line, (size_t)n) != 0) {
            rollback_lock(&lk);
            return -1;
        }
    }

    return commit_lock(&lk);
}

int index_add(const char *path)
{
    if (is_ignored(path)) return 0;

    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "forge: cannot stat '%s': %s\n", path, strerror(errno));
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "forge: '%s' is not a regular file\n", path);
        return -1;
    }

    char sha1[SHA1_HEX_SIZE];
    if (blob_from_file(path, sha1) != 0) {
        fprintf(stderr, "forge: failed to hash '%s'\n", path);
        return -1;
    }

    uint32_t mode = (st.st_mode & S_IXUSR) ? MODE_EXEC : MODE_FILE;

    /* Read existing index */
    IndexEntry *entries; int cnt;
    if (index_read(&entries, &cnt) != 0) return -1;

    /* Update or append */
    int found = 0;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(entries[i].path, path) == 0) {
            entries[i].mode = mode;
            snprintf(entries[i].sha1, SHA1_HEX_SIZE, "%s", sha1);
            found = 1; break;
        }
    }
    if (!found) {
        IndexEntry *tmp = realloc(entries, (size_t)(cnt + 1) * sizeof(IndexEntry));
        if (!tmp) { free(entries); return -1; }
        entries = tmp;
        entries[cnt].mode = mode;
        snprintf(entries[cnt].sha1, SHA1_HEX_SIZE, "%s", sha1);
        snprintf(entries[cnt].path, MAX_PATH_LEN, "%s", path);
        cnt++;
    }

    int rc = index_write(entries, cnt);
    free(entries);
    printf("  staged: %s\n", path);
    return rc;
}

static int add_recursive(const char *dir)
{
    DIR *d = opendir(dir[0] ? dir : ".");
    if (!d) return -1;

    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;

        char full[MAX_PATH_LEN];
        if (dir[0] && strcmp(dir, ".") != 0)
            snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
        else
            snprintf(full, sizeof(full), "%s", de->d_name);

        if (is_ignored(full)) continue;

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            add_recursive(full);
        } else if (S_ISREG(st.st_mode)) {
            index_add(full);
        }
    }
    closedir(d);
    return 0;
}

int index_add_all(void)
{
    return add_recursive(".");
}

int index_remove(const char *path)
{
    IndexEntry *entries; int cnt;
    if (index_read(&entries, &cnt) != 0) return -1;

    int found = 0;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(entries[i].path, path) == 0) {
            /* Shift remaining entries down */
            memmove(&entries[i], &entries[i+1],
                    (size_t)(cnt - i - 1) * sizeof(IndexEntry));
            cnt--; found = 1; break;
        }
    }
    if (!found) { free(entries); return -1; }
    int rc = index_write(entries, cnt);
    free(entries);
    return rc;
}

IndexEntry *index_find(IndexEntry *entries, int count, const char *path)
{
    for (int i = 0; i < count; i++)
        if (strcmp(entries[i].path, path) == 0) return &entries[i];
    return NULL;
}

void index_dump(void)
{
    IndexEntry *entries; int cnt;
    if (index_read(&entries, &cnt) != 0) return;
    for (int i = 0; i < cnt; i++)
        printf("  %o  %s  %s\n", entries[i].mode, entries[i].sha1, entries[i].path);
    free(entries);
}