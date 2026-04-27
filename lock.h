/* lock.h — Lock file protocol for FORGE
 * Copyright (C) 2026 Shahriar Dhrubo
 * GNU General Public License v3 — see LICENSE
 *
 * Atomic file updates via a .lock companion file.
 * Write to the lock, then commit (rename) or rollback (unlink).
 */
#ifndef LOCK_H
#define LOCK_H

#include "forge.h"

typedef struct {
    char path[MAX_PATH_LEN];
    char lock_path[MAX_PATH_LEN];
    int  fd;
} LockFile;

/* Create <path>.lock and open it for writing. */
int  lock_file(LockFile *lk, const char *path);

/* Write bytes into the open lock file. */
int  lock_write(LockFile *lk, const void *buf, size_t len);

/* Atomically rename <path>.lock -> <path>. */
int  commit_lock(LockFile *lk);

/* Discard the lock without updating the target. */
void rollback_lock(LockFile *lk);

/* Register a lock to be rolled back automatically on die(). */
void lock_register(LockFile *lk);
void lock_unregister(LockFile *lk);

#endif