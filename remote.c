/*
 * remote.c — Remote transport over SSH
 *
 * Author: Shahriar Dhrubo
 * Implements shoot (push) and fetch (pull) using ssh + rsync.
 * Named remotes are stored in .forge/remotes as "<name>\t<url>" lines.
 */

#include "remote.h"
#include "refs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// url part...
int remote_parse_url(const char *url,
                     char *user_host, size_t uh_size,
                     char *path, size_t path_size)
{
    // format: user@host:path 
    const char *colon = strchr(url, ':');
    if (!colon) {
        fprintf(stderr, "forge: invalid remote URL '%s' (expected user@host:path)\n", url);
        return -1;
    }
    size_t uh_len = (size_t)(colon - url);
    if (uh_len >= uh_size) return -1;
    memcpy(user_host, url, uh_len);
    user_host[uh_len] = '\0';
    snprintf(path, path_size, "%s", colon + 1);
    return 0;
}

//named remote/.

int remote_add(const char *name, const char *url)
{
    /* Check for duplicates */
    FILE *f = fopen(FORGE_REMOTES, "r");
    if (f) {
        char line[MAX_PATH_LEN];
        while (fgets(line, sizeof(line), f)) {
            char n[128], u[MAX_PATH_LEN];
            if (sscanf(line, "%127s\t%4095[^\n]", n, u) == 2) {
                if (strcmp(n, name) == 0) {
                    fclose(f);
                    fprintf(stderr, "forge: remote '%s' already exists\n", name);
                    return -1;
                }
            }
        }
        fclose(f);
    }

    f = fopen(FORGE_REMOTES, "a");
    if (!f) { perror(FORGE_REMOTES); return -1; }
    fprintf(f, "%s\t%s\n", name, url);
    fclose(f);
    printf("forge: remote '%s' added → %s\n", name, url);
    return 0;
}

void remote_list(void)
{
    FILE *f = fopen(FORGE_REMOTES, "r");
    if (!f) { printf("  (no remotes)\n"); return; }
    char line[MAX_PATH_LEN];
    while (fgets(line, sizeof(line), f)) {
        rtrim(line);
        char name[128], url[MAX_PATH_LEN];
        if (sscanf(line, "%127s\t%4095s", name, url) == 2)
            printf("  %-20s  %s\n", name, url);
    }
    fclose(f);
}

int remote_resolve(const char *name_or_url, char *url_out, size_t url_size)
{
    /* If it contains ':' it's already a URL */
    if (strchr(name_or_url, ':')) {
        snprintf(url_out, url_size, "%s", name_or_url);
        return 0;
    }
    /* Look up in remotes file */
    FILE *f = fopen(FORGE_REMOTES, "r");
    if (!f) {
        fprintf(stderr, "forge: no remotes configured\n");
        return -1;
    }
    char line[MAX_PATH_LEN];
    while (fgets(line, sizeof(line), f)) {
        rtrim(line);
        char name[128], url[MAX_PATH_LEN];
        if (sscanf(line, "%127s\t%4095s", name, url) == 2) {
            if (strcmp(name, name_or_url) == 0) {
                fclose(f);
                snprintf(url_out, url_size, "%s", url);
                return 0;
            }
        }
    }
    fclose(f);
    fprintf(stderr, "forge: remote '%s' not found\n", name_or_url);
    return -1;
}

// upload awa shoot.. like git push..

int remote_shoot(const char *remote, const char *branch)
{
    char url[MAX_PATH_LEN];
    if (remote_resolve(remote, url, sizeof(url)) != 0) return -1;

    char user_host[512], rpath[MAX_PATH_LEN];
    if (remote_parse_url(url, user_host, sizeof(user_host),
                         rpath, sizeof(rpath)) != 0) return -1;

    // Determine branch to push 
    char br[256];
    if (branch) {
        snprintf(br, sizeof(br), "%s", branch);
    } else {
        if (head_branch(br) != 0) {
            fprintf(stderr, "forge: cannot determine current branch\n");
            return -1;
        }
    }

    /// Resolve current HEAD 
    char sha1[SHA1_HEX_SIZE];
    if (head_resolve(sha1) != 0 || !sha1[0]) {
        fprintf(stderr, "forge: no commits to push\n");
        return -1;
    }

    printf("forge shoot → %s [branch: %s]\n", url, br);

    // s1: Ensure remote repo exists ( if no init)
    char cmd[MAX_PATH_LEN * 2];
    snprintf(cmd, sizeof(cmd),
             "ssh %s \"mkdir -p %s/.forge/objects %s/.forge/refs/heads %s/.forge/refs && "
             "[ -f %s/.forge/HEAD ] || echo 'ref: refs/heads/%s' > %s/.forge/HEAD\"",
             user_host, rpath, rpath, rpath, rpath, br, rpath);
    printf("  initialising remote...\n");
    if (system(cmd) != 0) {
        fprintf(stderr, "forge: failed to initialise remote repository\n");
        return -1;
    }

    // s2: rsync objects to remote (only new ones)
    snprintf(cmd, sizeof(cmd),
             "rsync -az --progress %s/ %s:%s/.forge/objects/",
             FORGE_OBJECTS, user_host, rpath);
    printf("  syncing objects...\n");
    if (system(cmd) != 0) {
        fprintf(stderr, "forge: rsync failed\n");
        return -1;
    }

    /// s3: Update remote branch ref 
    snprintf(cmd, sizeof(cmd),
             "ssh %s \"echo %s > %s/.forge/refs/heads/%s\"",
             user_host, sha1, rpath, br);
    printf("  updating remote ref refs/heads/%s → %s\n", br, sha1);
    if (system(cmd) != 0) {
        fprintf(stderr, "forge: failed to update remote ref\n");
        return -1;
    }

    printf("forge: shoot complete. %s:%s is now at %s\n",
           user_host, rpath, sha1);
    return 0;
}

// pull files, fetch..

int remote_fetch(const char *remote, const char *branch)
{
    char url[MAX_PATH_LEN];
    if (remote_resolve(remote, url, sizeof(url)) != 0) return -1;

    char user_host[512], rpath[MAX_PATH_LEN];
    if (remote_parse_url(url, user_host, sizeof(user_host),
                         rpath, sizeof(rpath)) != 0) return -1;

    char br[256];
    if (branch) {
        snprintf(br, sizeof(br), "%s", branch);
    } else {
        if (head_branch(br) != 0) snprintf(br, sizeof(br), "main");
    }

    printf("forge fetch ← %s [branch: %s]\n", url, br);

    // s1: rsync remote objects to local 
    char cmd[MAX_PATH_LEN * 2];
    snprintf(cmd, sizeof(cmd),
             "rsync -az --progress %s:%s/.forge/objects/ %s/",
             user_host, rpath, FORGE_OBJECTS);
    printf("  fetching objects...\n");
    if (system(cmd) != 0) {
        fprintf(stderr, "forge: rsync failed\n");
        return -1;
    }

    // s2: Get remote branch ref 
    char remote_sha1[SHA1_HEX_SIZE];
    snprintf(cmd, sizeof(cmd),
             "ssh %s \"cat %s/.forge/refs/heads/%s\"",
             user_host, rpath, br);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) { fprintf(stderr, "forge: ssh failed\n"); return -1; }
    if (!fgets(remote_sha1, sizeof(remote_sha1), pipe)) {
        pclose(pipe);
        fprintf(stderr, "forge: could not read remote ref\n");
        return -1;
    }
    pclose(pipe);
    rtrim(remote_sha1);

    if (!remote_sha1[0]) {
        printf("forge: remote branch '%s' is empty\n", br);
        return 0;
    }

    printf("  remote %s is at %s\n", br, remote_sha1);

    // s3: ff local branch ref 
    char local_sha1[SHA1_HEX_SIZE];
    int has_local = (branch_resolve(br, local_sha1) == 0);

    if (has_local && strcmp(local_sha1, remote_sha1) == 0) {
        printf("forge: already up to date.\n");
        return 0;
    }

    // update local ref
    char refname[MAX_PATH_LEN];
    snprintf(refname, sizeof(refname), "refs/heads/%s", br);
    ref_write(refname, remote_sha1);

    /// If this is the current branch, ..advance HEAD 
    char cur_br[256] = "";
    head_branch(cur_br);
    if (strcmp(cur_br, br) == 0) {
        head_advance(remote_sha1);
        printf("forge: fast-forwarded '%s' to %s\n", br, remote_sha1);
    } else {
        printf("forge: fetched '%s' → %s (not checked out)\n", br, remote_sha1);
    }

    return 0;
}