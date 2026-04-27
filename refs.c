/* refs.c — Reference management
 * Copyright (C) 2026 Shahriar Dhrubo
 * GNU General Public License v3 — see LICENSE
 *
 * Reads and writes HEAD and branch refs under .forge/refs/.
 * Each ref is a plain text file containing a 40-char SHA-1 hex string.
 */

#include "refs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>


 // Build .forge/<name> path and read its singleline content 
static int forge_read_line(const char *relpath, char *buf, size_t size)
{
    char full[MAX_PATH_LEN];
    snprintf(full, sizeof(full), "%s/%s", FORGE_DIR, relpath);
    FILE *f = fopen(full, "r");
    if (!f) return -1;
    if (!fgets(buf, (int)size, f)) { fclose(f); return -1; }
    fclose(f);
    rtrim(buf);
    return 0;
}

static int forge_write_line(const char *relpath, const char *content)
{
    char full[MAX_PATH_LEN];
    snprintf(full, sizeof(full), "%s/%s", FORGE_DIR, relpath);

    // check if dic exists
    char dir[MAX_PATH_LEN];
    snprintf(dir, sizeof(dir), "%s", full);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdir(dir, 0755); }

    LockFile lk;
    if (lock_file(&lk, full) != 0) return -1;

    char buf[MAX_PATH_LEN + 2];
    int n = snprintf(buf, sizeof(buf), "%s\n", content);
    if (lock_write(&lk, buf, (size_t)n) != 0) {
        rollback_lock(&lk);
        return -1;
    }

    return commit_lock(&lk);
}


int ref_read(const char *name, char sha1_out[SHA1_HEX_SIZE])
{
    return forge_read_line(name, sha1_out, SHA1_HEX_SIZE);
}

int ref_write(const char *name, const char *sha1)
{
    return forge_write_line(name, sha1);
}


int head_read_raw(char *buf, size_t size)
{
    return forge_read_line("HEAD", buf, size);
}

int head_resolve(char sha1_out[SHA1_HEX_SIZE])
{
    char buf[MAX_PATH_LEN];
    sha1_out[0] = '\0';
    if (forge_read_line("HEAD", buf, sizeof(buf)) != 0) return -1;

    if (strncmp(buf, "ref: ", 5) == 0) {
       
        const char *refname = buf + 5;
        if (forge_read_line(refname, sha1_out, SHA1_HEX_SIZE) != 0) {
            sha1_out[0] = '\0';  
            return 0;
        }
    } else {
        // Detached HEAD 
        snprintf(sha1_out, SHA1_HEX_SIZE, "%s", buf);
    }
    return 0;
}

int head_branch(char branch_out[256])
{
    char buf[MAX_PATH_LEN];
    if (forge_read_line("HEAD", buf, sizeof(buf)) != 0) return -1;
    if (strncmp(buf, "ref: refs/heads/", 16) != 0) return -1;
    snprintf(branch_out, 256, "%s", buf + 16);
    return 0;
}

int head_set_ref(const char *branch)
{
    char ref[MAX_PATH_LEN];
    snprintf(ref, sizeof(ref), "ref: refs/heads/%s", branch);
    return forge_write_line("HEAD", ref);
}

int head_advance(const char *sha1)
{
    char buf[MAX_PATH_LEN];
    if (forge_read_line("HEAD", buf, sizeof(buf)) != 0) return -1;

    if (strncmp(buf, "ref: ", 5) == 0) {
        return forge_write_line(buf + 5, sha1);
    } else {
        // detach and update HEAD
        return forge_write_line("HEAD", sha1);
    }
}


void branch_list(void)
{
    char current[256] = "";
    head_branch(current);

    DIR *d = opendir(FORGE_HEADS);
    if (!d) { printf("  (no branches)\n"); return; }

    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        if (strcmp(de->d_name, current) == 0)
            printf("* \033[32m%s\033[0m\n", de->d_name);
        else
            printf("  %s\n", de->d_name);
    }
    closedir(d);
}

int branch_create(const char *name, const char *sha1)
{
    // Check if exists 2
    char existing[SHA1_HEX_SIZE];
    char refname[MAX_PATH_LEN];
    snprintf(refname, sizeof(refname), "refs/heads/%s", name);

    if (ref_read(refname, existing) == 0) {
        fprintf(stderr, "forge: branch '%s' already exists\n", name);
        return -1;
    }

    char resolved[SHA1_HEX_SIZE];
    if (sha1) {
        snprintf(resolved, sizeof(resolved), "%s", sha1);
    } else {
        if (head_resolve(resolved) != 0 || !resolved[0]) {
            fprintf(stderr, "forge: no commits yet; cannot create branch\n");
            return -1;
        }
    }
    return ref_write(refname, resolved);
}

int branch_delete(const char *name)
{
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", FORGE_HEADS, name);
    if (unlink(path) != 0) {
        fprintf(stderr, "forge: branch '%s' not found\n", name);
        return -1;
    }
    return 0;
}

int branch_resolve(const char *name, char sha1_out[SHA1_HEX_SIZE])
{
    char refname[MAX_PATH_LEN];
    snprintf(refname, sizeof(refname), "refs/heads/%s", name);
    return ref_read(refname, sha1_out);
}