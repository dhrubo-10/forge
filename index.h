/* index.h — Staging area interface for FORGE
 * Copyright (C) 2026 Shahriar Dhrubo
 * GNU General Public License v3 — see LICENSE
 *
 * Declares the index read/write/add/remove API and the is_ignored()
 * check. The index file lives at .forge/index.
 * f.e.:
 *   100644	a3f4b2c1...	src/main.c
 *   100644	deadbeef...	README.md
 */
 

#ifndef INDEX_H
#define INDEX_H

#include "forge.h"

 // Read all index entries into a malloc'd array.
int index_read(IndexEntry **entries_out, int *count_out);

 // Write entries to the index file (overwrites existing).
int index_write(const IndexEntry *entries, int count);

int index_add(const char *path);

 // Stage all modified/untracked files recursively from cwd. checks .ignore too
int index_add_all(void);

int index_remove(const char *path);

void index_dump(void);

//Find an entry by path. Returns pointer into entries array or NULL.
IndexEntry *index_find(IndexEntry *entries, int count, const char *path);

/* Compare index against last commit tree.
 * Fills three sets: staged (new/changed), deleted (in commit, not index),
 * unmodified (same sha1). */
void index_diff_commit(IndexEntry *idx, int idx_cnt,
                       const char *commit_sha1);

/* Check whether a path should be ignored (reads .forgeignore) */
int is_ignored(const char *path);

#endif // done fn,,