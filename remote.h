/* remote.h - SSH remote transport interface for FORGE
 * Copyright (C) 2026 Shahriar Dhrubo
 * GNU General Public License v3 — see LICENSE
 *
 * SSH based remote operations for FORGE
 *
 * Remote URLs use the SSH syntax:  user@host:path
 * f.e.  git@myserver.io:~/repos/project
 *   shoot push:
 *     1. SSH to remote, init if needed.
 *     2. rsync .forge/objects/ to remote (only missing objects).
 *     3. Transfer .forge/refs/ to remote.
 *     4. Update remote HEAD via SSH command.
 *
 *   fetch pull:
 *     1. rsync remote .forge/objects/ to local.
 *     2. rsync remote .forge/refs/ to local.
 *     3. Merge remote branch into current branch ...."fast forward only.
 *
 * Named remotes are stored in .forge/remotes:
 *   <name>\t<url>\n
 */


#ifndef REMOTE_H
#define REMOTE_H


#include "forge.h"

// Parse "user@host:path" into components.
int remote_parse_url(const char *url,
                     char *user_host, size_t uh_size,
                     char *path, size_t path_size);

int remote_add(const char *name, const char *url);

void remote_list(void);

int remote_resolve(const char *name_or_url,
                   char *url_out, size_t url_size);

/* Push local repo to remote via SSH + rsync.
 * remote: name or "user@host:path"
 * branch: branch to push (NULL = current branch)
 * **/
int remote_shoot(const char *remote, const char *branch);

// Fetch from remote and update local refs.

int remote_fetch(const char *remote, const char *branch);

#endif 