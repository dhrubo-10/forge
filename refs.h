/* refs.h - Reference management interface for FORGE
 * Copyright (C) 2026 Shahriar Dhrubo
 * GNU General Public License v3 — see LICENSE
 *
 * Declares HEAD and branch ref read/write API.
 * Refs live at .forge/refs/heads/<branch>.
 */

#ifndef REFS_H
#define REFS_H


#include "forge.h"

// Read a ref by full name (e.g. "refs/heads/main") → sha1[41].
int ref_read(const char *name, char sha1_out[SHA1_HEX_SIZE]);

// Write a ref by full name.
int ref_write(const char *name, const char *sha1);

// Read the raw content of HEAD into buf (e.g. "ref: refs/heads/main").
int head_read_raw(char *buf, size_t size);

/* Resolve HEAD -> actual SHA-1 (follows symbolic refs).
 *sha1_out empty string if no commits yet. */
int head_resolve(char sha1_out[SHA1_HEX_SIZE]);

/* Get the current branch name from HEAD.
**/
int head_branch(char branch_out[256]);

/* Write HEAD as a symbolic ref (for branch switch). **/
int head_set_ref(const char *branch);

// update HEAD
int head_advance(const char *sha1);

// lst bracnh
void branch_list(void);

/* Create a new branch at sha1 (or current HEAD if sha1 is NULL).
*/
int branch_create(const char *name, const char *sha1);

 // Del a branch
int branch_delete(const char *name);

//Resolve a branch name to its SHA-1
int branch_resolve(const char *name, char sha1_out[SHA1_HEX_SIZE]);

#endif