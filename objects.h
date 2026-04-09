#ifndef OBJECTS_H
#define OBJECTS_H

/*
 * Content-addressed object store for FORGE
 *
 * Objects are stored as:
 *   zlib_compress( "<type> <size>\0<raw_content>" )
 * at path:
 *   .forge/objects/<XX>/<remaining_38_hex_chars>
 *
 * where <XX> are the first two hex characters of the SHA-1.
 */

#include "forge.h"

// Compute the store path for a hex SHA-1
void obj_path(const char *sha1_hex, char path_out[MAX_PATH_LEN]);

 // Hash data as an object (does NOT write); fills sha1_out[41] 
void obj_hash(const uint8_t *data, size_t len, ObjType type,
              char sha1_out[SHA1_HEX_SIZE]);

// Write object to store; idempotent means skip if already exists...

int obj_write(const uint8_t *data, size_t len, ObjType type,
              char sha1_out[SHA1_HEX_SIZE]);

// Read object from store. On success: *type_out, *data_out (malloc'd, caller frees), *len_out set.
int obj_read(const char *sha1_hex, ObjType *type_out,
             uint8_t **data_out, size_t *len_out);

int obj_exists(const char *sha1_hex);

 // Hash a file and store it as a blob; fills sha1_out[41]. ret 0 on success. 
int blob_from_file(const char *filepath, char sha1_out[SHA1_HEX_SIZE]);

 // Write blob content to a file path (restore working tree file). ret 0 on success. 
int blob_to_file(const char *sha1_hex, const char *filepath, uint32_t mode);

// Tree entry (used when building/reading tree objects) 
typedef struct {
    uint32_t mode;
    char     sha1[SHA1_HEX_SIZE];
    char     name[MAX_PATH_LEN];   // fname
} TreeEntry;

/* Encode tree entries into a tree object and write it.
 * entries: sorted array of TreeEntry
 * count: number of entries
 * sha1_out: receives the 40+1 hex SHA-1 of the new tree object 
 */
int tree_write(TreeEntry *entries, int count, char sha1_out[SHA1_HEX_SIZE]);

 // Read and parse a tree object 
int tree_read(const char *sha1_hex, TreeEntry **entries_out, int *count_out);

/* 
    Recursively build tree objects from index entries.
 * prefix: directory prefix to filter (pass "" for root)
 * fills sha1_out with the root tree SHA-1. 
 */
int tree_build_from_index(IndexEntry *entries, int count,
                          const char *prefix, char sha1_out[SHA1_HEX_SIZE]);

int tree_checkout(const char *sha1_hex, const char *base_path);

int obj_list_all(char ***sha1s_out, int *count_out);

#endif 