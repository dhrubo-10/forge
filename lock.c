/* lock.c — Lock file protocol
 * Copyright (C) 2026 Shahriar Dhrubo
 * GNU General Public License v3 — see LICENSE
 *
 * Prevents index and ref corruption from concurrent or interrupted writes.
 * Pattern: lock_file() -> lock_write() -> commit_lock() or rollback_lock().
 */

#include "lock.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static LockFile *active_locks[8];
static int       active_count = 0;

static void rollback_all(void)
{
    for (int i = 0; i < active_count; i++)
        if (active_locks[i])
            rollback_lock(active_locks[i]);
}

static void ensure_atexit(void)
{
    static int done = 0;
    if (!done) { atexit(rollback_all); done = 1; }
}

int lock_file(LockFile *lk, const char *path)
{
    ensure_atexit();
    snprintf(lk->path,      sizeof(lk->path),      "%s",      path);
    snprintf(lk->lock_path, sizeof(lk->lock_path), "%s.lock", path);
    lk->fd = -1;

    lk->fd = open(lk->lock_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (lk->fd < 0) {
        if (errno == EEXIST)
            fprintf(stderr,
                "forge: %s is locked\n"
                "  Another forge process may be running. If not, remove %s manually.\n",
                path, lk->lock_path);
        else
            fprintf(stderr, "forge: cannot create lock %s: %s\n",
                    lk->lock_path, strerror(errno));
        return -1;
    }
    lock_register(lk);
    return 0;
}

int lock_write(LockFile *lk, const void *buf, size_t len)
{
    if (lk->fd < 0) return -1;
    return (write(lk->fd, buf, len) == (ssize_t)len) ? 0 : -1;
}

int commit_lock(LockFile *lk)
{
    if (lk->fd < 0) return -1;
    fsync(lk->fd);
    close(lk->fd);
    lk->fd = -1;

    if (rename(lk->lock_path, lk->path) != 0) {
        fprintf(stderr, "forge: commit lock failed %s -> %s: %s\n",
                lk->lock_path, lk->path, strerror(errno));
        unlink(lk->lock_path);
        return -1;
    }
    lock_unregister(lk);
    return 0;
}

void rollback_lock(LockFile *lk)
{
    if (lk->fd >= 0) { close(lk->fd); lk->fd = -1; }
    if (lk->lock_path[0]) unlink(lk->lock_path);
    lock_unregister(lk);
}

void lock_register(LockFile *lk)
{
    ensure_atexit();
    for (int i = 0; i < active_count; i++)
        if (active_locks[i] == lk) return;
    if (active_count < 8)
        active_locks[active_count++] = lk;
}

void lock_unregister(LockFile *lk)
{
    for (int i = 0; i < active_count; i++) {
        if (active_locks[i] == lk) {
            active_locks[i] = active_locks[--active_count];
            return;
        }
    }
}