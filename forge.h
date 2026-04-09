#ifndef FORGE_H
#define FORGE_H

/*
 * Core definitions for FORGE VCS
 *
 *  Self reminder:

 *  commnds:
 *    forge put     -  git add
 *    forge msg     -  git commit
 *    forge shoot   -  git push
 *    forge fetch   -  git pull
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define FORGE_DIR         ".forge"
#define FORGE_OBJECTS     ".forge/objects"
#define FORGE_REFS        ".forge/refs"
#define FORGE_HEADS       ".forge/refs/heads"
#define FORGE_HEAD        ".forge/HEAD"
#define FORGE_INDEX       ".forge/index"
#define FORGE_CONFIG      ".forge/config"
#define FORGE_DESCRIPTION ".forge/description"
#define FORGE_REMOTES     ".forge/remotes"

#define SHA1_HEX_LEN   40
#define SHA1_HEX_SIZE  41   
#define SHA1_RAW_SIZE  20
#define MAX_PATH_LEN   4096
#define MAX_LINE       8192
#define MAX_ENTRIES    65536

// File modes (matching POSIX + git conventions) 
#define MODE_FILE      0100644   // regular file 
#define MODE_EXEC      0100755   // executable 
#define MODE_DIR       0040000   // directory / subtree 
#define MODE_SYMLINK   0120000   //symbolic link 

typedef struct {
    uint32_t mode;
    char     sha1[SHA1_HEX_SIZE];   // hex SHA-1 of blob 
    char     path[MAX_PATH_LEN];
} IndexEntry;

typedef enum {
    OBJ_BLOB   = 1,
    OBJ_TREE   = 2,
    OBJ_COMMIT = 3,
} ObjType;

void  die(const char *fmt, ...);
void *xmalloc(size_t sz);
char *xstrdup(const char *s);
int   mkdirp(const char *path, mode_t mode);
int   read_file(const char *path, uint8_t **buf, size_t *len);
int   write_file(const char *path, const uint8_t *buf, size_t len);
int   write_file_str(const char *path, const char *str);
char *read_file_str(const char *path);            
void  forge_find_root(char *out, size_t size);    
int   is_forge_repo(void);

// newl trimmer
static inline void rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' '))
        s[--n] = '\0';
}

#endif 