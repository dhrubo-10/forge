/* objects.c — Content-addressed object store
 * Copyright (C) 2026 Shahriar Dhrubo
 * GNU General Public License v3 — see LICENSE
 *
 * Stores objects as zlib_compress("<type> <size>\0<content>").
 * Provides blob, tree, and commit read/write primitives.
 */

#include "objects.h"
#include "sha1.h"
#include <zlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>


static int zlib_compress(const uint8_t *in, size_t in_len,
                          uint8_t **out, size_t *out_len)
{
    uLongf dest = compressBound((uLong)in_len);
    *out = malloc((size_t)dest);
    if (!*out) return -1;
    if (compress2((Bytef *)*out, &dest, (const Bytef *)in,
                  (uLong)in_len, Z_BEST_SPEED) != Z_OK) {
        free(*out); return -1;
    }
    *out_len = (size_t)dest;
    return 0;
}

static int zlib_decompress(const uint8_t *in, size_t in_len,
                            uint8_t **out, size_t *out_len)
{
    size_t buf_sz = in_len * 4 + 1024;
    *out = malloc(buf_sz);
    if (!*out) return -1;

    for (;;) {
        uLongf dest = (uLongf)buf_sz;
        int rc = uncompress((Bytef *)*out, &dest,
                            (const Bytef *)in, (uLong)in_len);
        if (rc == Z_OK) { *out_len = (size_t)dest; return 0; }
        if (rc == Z_BUF_ERROR) {
            buf_sz *= 2;
            uint8_t *tmp = realloc(*out, buf_sz);
            if (!tmp) { free(*out); return -1; }
            *out = tmp;
        } else {
            free(*out); return -1;
        }
    }
}


static const char *type_str(ObjType t)
{
    switch (t) {
        case OBJ_BLOB:   return "blob";
        case OBJ_TREE:   return "tree";
        case OBJ_COMMIT: return "commit";
        default:         return "unknown";
    }
}

static ObjType type_from_str(const char *s)
{
    if (!strcmp(s, "blob"))   return OBJ_BLOB;
    if (!strcmp(s, "tree"))   return OBJ_TREE;
    if (!strcmp(s, "commit")) return OBJ_COMMIT;
    return 0;
}


void obj_path(const char *sha1_hex, char path_out[MAX_PATH_LEN])
{
    snprintf(path_out, MAX_PATH_LEN,
             "%s/%.2s/%s", FORGE_OBJECTS, sha1_hex, sha1_hex + 2);
}

 // Build the full store buffer: "<type> <size>\0<content>" and hash it 
static void build_store_buf(const uint8_t *data, size_t len, ObjType type,
                             uint8_t **full_out, size_t *full_len_out,
                             uint8_t sha1_raw[20])
{
    char hdr[64];
    int hlen = snprintf(hdr, sizeof(hdr), "%s %zu", type_str(type), len);
    size_t full_len = (size_t)hlen + 1 + len;   // +1 for \0 
    uint8_t *full = malloc(full_len);
    if (!full) { perror("malloc"); exit(1); }
    memcpy(full, hdr, (size_t)hlen + 1);         
    memcpy(full + (size_t)hlen + 1, data, len); 
    sha1_of_buf(full, full_len, sha1_raw);
    *full_out     = full;
    *full_len_out = full_len;
}

void obj_hash(const uint8_t *data, size_t len, ObjType type,
              char sha1_out[SHA1_HEX_SIZE])
{
    uint8_t *full; size_t flen; uint8_t raw[20];
    build_store_buf(data, len, type, &full, &flen, raw);
    sha1_hex(raw, sha1_out);
    free(full);
}

int obj_write(const uint8_t *data, size_t len, ObjType type,
              char sha1_out[SHA1_HEX_SIZE])
{
    uint8_t *full; size_t flen; uint8_t raw[20];
    build_store_buf(data, len, type, &full, &flen, raw);
    sha1_hex(raw, sha1_out);

    char path[MAX_PATH_LEN];
    obj_path(sha1_out, path);

    if (access(path, F_OK) == 0) { free(full); return 0; }

    char dir[MAX_PATH_LEN];
    snprintf(dir, sizeof(dir), "%s/%.2s", FORGE_OBJECTS, sha1_out);
    mkdir(dir, 0755);

    uint8_t *compressed; size_t comp_len;
    if (zlib_compress(full, flen, &compressed, &comp_len) != 0) {
        free(full); return -1;
    }
    free(full);

    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0444);
    if (fd < 0) {
        free(compressed);
        return (errno == EEXIST) ? 0 : -1;
    }
    if (write(fd, compressed, comp_len) != (ssize_t)comp_len) {
        close(fd); free(compressed); return -1;
    }
    close(fd);
    free(compressed);
    return 0;
}

int obj_read(const char *sha1_hex, ObjType *type_out,
             uint8_t **data_out, size_t *len_out)
{
    char path[MAX_PATH_LEN];
    obj_path(sha1_hex, path);

    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return -1; }

    struct stat st;
    fstat(fd, &st);
    uint8_t *compressed = malloc((size_t)st.st_size);
    if (!compressed) { close(fd); return -1; }
    if (read(fd, compressed, (size_t)st.st_size) != st.st_size) {
        close(fd); free(compressed); return -1;
    }
    close(fd);

    uint8_t *full; size_t flen;
    if (zlib_decompress(compressed, (size_t)st.st_size, &full, &flen) != 0) {
        free(compressed); return -1;
    }
    free(compressed);

    // Parse header: "type size\0..." 
    char *nul = memchr(full, '\0', flen);
    if (!nul) { free(full); return -1; }

    char type_str_buf[16]; size_t csize;
    if (sscanf((char *)full, "%15s %zu", type_str_buf, &csize) != 2) {
        free(full); return -1;
    }
    *type_out = type_from_str(type_str_buf);

    size_t hdr_len = (size_t)(nul - (char *)full) + 1;
    *len_out  = flen - hdr_len;
    *data_out = malloc(*len_out + 1);
    if (!*data_out) { free(full); return -1; }
    memcpy(*data_out, full + hdr_len, *len_out);
    (*data_out)[*len_out] = '\0';   
    free(full);
    return 0;
}

int obj_exists(const char *sha1_hex)
{
    char path[MAX_PATH_LEN];
    obj_path(sha1_hex, path);
    return access(path, F_OK) == 0;
}


int blob_from_file(const char *filepath, char sha1_out[SHA1_HEX_SIZE])
{
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) { perror(filepath); return -1; }

    struct stat st;
    fstat(fd, &st);
    uint8_t *data = malloc((size_t)st.st_size + 1);
    if (!data) { close(fd); return -1; }

    ssize_t n = read(fd, data, (size_t)st.st_size);
    close(fd);
    if (n < 0) { free(data); return -1; }

    int rc = obj_write(data, (size_t)n, OBJ_BLOB, sha1_out);
    free(data);
    return rc;
}

int blob_to_file(const char *sha1_hex, const char *filepath, uint32_t mode)
{
    ObjType type; uint8_t *data; size_t len;
    if (obj_read(sha1_hex, &type, &data, &len) != 0) return -1;
    if (type != OBJ_BLOB) { free(data); return -1; }

    char dir[MAX_PATH_LEN];
    snprintf(dir, sizeof(dir), "%s", filepath);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdir(dir, 0755); }

    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    int perm  = (mode & 0111) ? 0755 : 0644;
    int fd = open(filepath, flags, perm);
    if (fd < 0) { free(data); return -1; }
    ssize_t written = write(fd, data, len);
    if (written != (ssize_t)len)
        fprintf(stderr, "forge: write incomplete for %s\n", filepath);
    close(fd);
    free(data);
    return 0;
}


/*
 * Tree binary format (same as git):
 *   For each entry:  "<mode> <name>\0<sha1_20bytes>"
 * Entries sorted lexicographically by name.
 */

static int tree_entry_cmp(const void *a, const void *b)
{
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

int tree_write(TreeEntry *entries, int count, char sha1_out[SHA1_HEX_SIZE])
{
    qsort(entries, (size_t)count, sizeof(TreeEntry), tree_entry_cmp);

    // Calculate buffer size 
    size_t sz = 0;
    for (int i = 0; i < count; i++) {
        char mode_str[16];
        snprintf(mode_str, sizeof(mode_str), "%o", entries[i].mode);
        sz += strlen(mode_str) + 1 + strlen(entries[i].name) + 1 + SHA1_RAW_SIZE;
    }

    uint8_t *buf = malloc(sz);
    if (!buf) return -1;
    size_t off = 0;

    for (int i = 0; i < count; i++) {
         // "<mode> <name>\0" 
        char prefix[MAX_PATH_LEN];
        int plen = snprintf(prefix, sizeof(prefix),
                            "%o %s", entries[i].mode, entries[i].name);
        memcpy(buf + off, prefix, (size_t)plen + 1);
        off += (size_t)plen + 1;

         // 20 raw SHA-1 bytes 
        uint8_t raw[20];
        for (int j = 0; j < 20; j++) {
            unsigned int v;
            sscanf(entries[i].sha1 + j * 2, "%02x", &v);
            raw[j] = (uint8_t)v;
        }
        memcpy(buf + off, raw, SHA1_RAW_SIZE);
        off += SHA1_RAW_SIZE;
    }

    int rc = obj_write(buf, off, OBJ_TREE, sha1_out);
    free(buf);
    return rc;
}

int tree_read(const char *tree_sha, TreeEntry **entries_out, int *count_out)
{
    ObjType type; uint8_t *data; size_t len;
    if (obj_read(tree_sha, &type, &data, &len) != 0) return -1;
    if (type != OBJ_TREE) { free(data); return -1; }

    //  Count entries first 
    int cap = 64, cnt = 0;
    TreeEntry *entries = malloc((size_t)cap * sizeof(TreeEntry));
    if (!entries) { free(data); return -1; }

    size_t off = 0;
    while (off < len) {
        if (cnt >= cap) {
            cap *= 2;
            TreeEntry *tmp = realloc(entries, (size_t)cap * sizeof(TreeEntry));
            if (!tmp) { free(entries); free(data); return -1; }
            entries = tmp;
        }
         // Parse "<mode> <name>\0<20bytes>" 
        char *sp = memchr(data + off, ' ', len - off);
        if (!sp) break;
        *sp = '\0';
        unsigned int mode_val;
        sscanf((char *)(data + off), "%o", &mode_val);
        entries[cnt].mode = mode_val;
        off = (size_t)(sp - (char *)data) + 1;

        char *nul = memchr(data + off, '\0', len - off);
        if (!nul) break;
        snprintf(entries[cnt].name, MAX_PATH_LEN, "%s", (char *)(data + off));
        off = (size_t)(nul - (char *)data) + 1;

        if (off + SHA1_RAW_SIZE > len) break;
        sha1_hex(data + off, entries[cnt].sha1);
        off += SHA1_RAW_SIZE;
        cnt++;
    }

    free(data);
    *entries_out = entries;
    *count_out   = cnt;
    return 0;
}


int tree_build_from_index(IndexEntry *entries, int count,
                          const char *prefix, char sha1_out[SHA1_HEX_SIZE])
{
    int cap = 64, ncnt = 0;
    TreeEntry *te = malloc((size_t)cap * sizeof(TreeEntry));
    if (!te) return -1;

    size_t plen = strlen(prefix);

    for (int i = 0; i < count; i++) {
        const char *p = entries[i].path;
        // Must start with prefix 
        if (plen > 0 && strncmp(p, prefix, plen) != 0) continue;
        const char *rel = p + plen;

        char *slash = strchr(rel, '/');

        if (ncnt >= cap) {
            cap *= 2;
            TreeEntry *tmp = realloc(te, (size_t)cap * sizeof(TreeEntry));
            if (!tmp) { free(te); return -1; }
            te = tmp;
        }

        if (!slash) {
            te[ncnt].mode = entries[i].mode;
            snprintf(te[ncnt].sha1, SHA1_HEX_SIZE, "%s", entries[i].sha1);
            snprintf(te[ncnt].name, MAX_PATH_LEN, "%s", rel);
            ncnt++;
        } else {
           
            char subdir[MAX_PATH_LEN];
            size_t dlen = (size_t)(slash - rel);
            memcpy(subdir, rel, dlen);
            subdir[dlen] = '\0';

            // Check if we already added this subtree 
            int found = 0;
            for (int j = 0; j < ncnt; j++) {
                if (te[j].mode == MODE_DIR && !strcmp(te[j].name, subdir)) {
                    found = 1; break;
                }
            }
            if (!found) {
                // Build sub-prefix and recurse 
                char sub_prefix[MAX_PATH_LEN];
                snprintf(sub_prefix, sizeof(sub_prefix), "%s%s/", prefix, subdir);
                char sub_sha1[SHA1_HEX_SIZE];
                if (tree_build_from_index(entries, count, sub_prefix, sub_sha1) != 0) {
                    free(te); return -1;
                }
                te[ncnt].mode = MODE_DIR;
                snprintf(te[ncnt].sha1, SHA1_HEX_SIZE, "%s", sub_sha1);
                snprintf(te[ncnt].name, MAX_PATH_LEN, "%s", subdir);
                ncnt++;
            }
        }
    }

    int rc = (ncnt == 0) ? (memset(sha1_out, '0', SHA1_HEX_LEN), sha1_out[SHA1_HEX_LEN]='\0', 0)
                         : tree_write(te, ncnt, sha1_out);
    free(te);
    return rc;
}


int tree_checkout(const char *sha1_hex, const char *base_path)
{
    TreeEntry *entries; int count;
    if (tree_read(sha1_hex, &entries, &count) != 0) return -1;

    for (int i = 0; i < count; i++) {
        char full[MAX_PATH_LEN];
        if (base_path && *base_path)
            snprintf(full, sizeof(full), "%s/%s", base_path, entries[i].name);
        else
            snprintf(full, sizeof(full), "%s", entries[i].name);

        if (entries[i].mode == MODE_DIR) {
            mkdir(full, 0755);
            if (tree_checkout(entries[i].sha1, full) != 0) {
                free(entries); return -1;
            }
        } else {
            if (blob_to_file(entries[i].sha1, full, entries[i].mode) != 0) {
                free(entries); return -1;
            }
        }
    }
    free(entries);
    return 0;
}


int obj_list_all(char ***sha1s_out, int *count_out)
{
    int cap = 256, cnt = 0;
    char **list = malloc((size_t)cap * sizeof(char *));
    if (!list) return -1;

    DIR *d = opendir(FORGE_OBJECTS);
    if (!d) { free(list); *sha1s_out = NULL; *count_out = 0; return 0; }

    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        if (strlen(de->d_name) != 2) continue;

        char bucket[MAX_PATH_LEN];
        snprintf(bucket, sizeof(bucket), "%s/%s", FORGE_OBJECTS, de->d_name);
        DIR *bd = opendir(bucket);
        if (!bd) continue;

        struct dirent *be;
        while ((be = readdir(bd))) {
            if (be->d_name[0] == '.') continue;
            if (cnt >= cap) {
                cap *= 2;
                char **tmp = realloc(list, (size_t)cap * sizeof(char *));
                if (!tmp) { closedir(bd); closedir(d); free(list); return -1; }
                list = tmp;
            }
            char sha1[SHA1_HEX_SIZE];
            sha1[0] = '\0';
            strncat(sha1, de->d_name,  2);
            strncat(sha1, be->d_name, 38);
            sha1[SHA1_HEX_LEN] = '\0';
            list[cnt++] = strdup(sha1);
        }
        closedir(bd);
    }
    closedir(d);

    *sha1s_out  = list;
    *count_out  = cnt;
    return 0;
}