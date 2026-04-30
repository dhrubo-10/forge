```
  ███████╗ ██████╗ ██████╗  ██████╗ ███████╗
  ██╔════╝██╔═══██╗██╔══██╗██╔════╝ ██╔════╝
  █████╗  ██║   ██║██████╔╝██║  ███╗█████╗
  ██╔══╝  ██║   ██║██╔══██╗██║   ██║██╔══╝
  ██║     ╚██████╔╝██║  ██║╚██████╔╝███████╗
  ╚═╝      ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚══════╝
```

FORGE is a minimal distributed version control system written in C,
directly inspired by initial GIT. It uses the same object model:
content-addressed SHA-1 blobs, trees, and commits stored zlib-compressed
in `.forge/objects/`, with human-readable refs in `.forge/refs/`. It is
architecturally compatible with git's loose object format.

**Author:** Shahriar Dhrubo
**License:** GNU General Public License v3

---

## Build

```bash
# Install zlib dev headers if needed
sudo apt install zlib1g-dev

cd forge/
make
make install      # copies to ~/.local/bin/forge
make test         # run the test suite
```

Object files are placed in `objects/` and cleaned with `make clean`.

---

## Command Reference

### Working Tree

| FORGE command                      | git equivalent      | Description                        |
|------------------------------------|---------------------|------------------------------------|
| `forge init`                       | `git init`          | Initialise a new repository        |
| `forge clone <url> [<dir>]`        | `git clone`         | Clone a remote repository          |
| `forge put <files>`                | `git add`           | Stage files for commit             |
| `forge put -a`                     | `git add -A`        | Stage all files                    |
| `forge rm [--cached] <file>`       | `git rm`            | Remove file from index or disk     |
| `forge mv <src> <dst>`             | `git mv`            | Rename a tracked file              |
| `forge restore [--source] <file>`  | `git restore`       | Restore file to committed state    |
| `forge status`                     | `git status`        | Show working tree status           |
| `forge diff`                       | `git diff`          | Working tree vs HEAD               |
| `forge diff --cached`              | `git diff --cached` | Staged changes vs HEAD             |
| `forge grep [-n] [-i] <pattern>`   | `git grep`          | Search tracked files               |

### History

| FORGE command                      | git equivalent      | Description                        |
|------------------------------------|---------------------|------------------------------------|
| `forge msg -m "..."`               | `git commit`        | Commit staged snapshot             |
| `forge log`                        | `git log`           | Show commit history                |
| `forge log --oneline`              | `git log --oneline` | Compact one-line history           |
| `forge log --graph`                | `git log --graph`   | ASCII branch graph                 |
| `forge show [<sha1>]`              | `git show`          | Inspect a commit or object         |
| `forge reset`                      | `git reset`         | Clear staging area                 |
| `forge reset --soft <sha1>`        | `git reset --soft`  | Move HEAD, keep index              |
| `forge reset --hard <sha1>`        | `git reset --hard`  | Move HEAD, restore working tree    |
| `forge stash`                      | `git stash`         | Save changes, clean working tree   |
| `forge stash pop`                  | `git stash pop`     | Restore last stash                 |
| `forge stash list`                 | `git stash list`    | Show stashed snapshots             |
| `forge stash drop`                 | `git stash drop`    | Discard stash                      |

### Branches & Tags

| FORGE command                      | git equivalent      | Description                        |
|------------------------------------|---------------------|------------------------------------|
| `forge branch`                     | `git branch`        | List branches                      |
| `forge branch <n>`                 | `git branch <n>`    | Create a branch                    |
| `forge branch -d <n>`              | `git branch -d`     | Delete a branch                    |
| `forge checkout <b>`               | `git checkout`      | Switch branch                      |
| `forge checkout -b <b>`            | `git checkout -b`   | Create and switch branch           |
| `forge merge <branch>`             | `git merge`         | Fast-forward merge                 |
| `forge cherry-pick <sha1>`         | `git cherry-pick`   | Apply a commit onto HEAD           |
| `forge shortlog`                   | `git shortlog`      | Commit summary by author           |
| `forge tag <n> [<sha1>]`           | `git tag`           | Create a lightweight tag           |
| `forge tag -l`                     | `git tag -l`        | List tags                          |
| `forge tag -d <n>`                 | `git tag -d`        | Delete a tag                       |

### Remote

| FORGE command                      | git equivalent      | Description                        |
|------------------------------------|---------------------|------------------------------------|
| `forge remote add <n> <url>`       | `git remote add`    | Add a named remote                 |
| `forge remote remove <n>`          | `git remote remove` | Remove a named remote              |
| `forge remote list`                | `git remote -v`     | List remotes                       |
| `forge shoot [<remote>]`           | `git push`          | Push to remote over SSH            |
| `forge fetch [<remote>]`           | `git pull`          | Fetch from remote over SSH         |

### Server & Plumbing

| FORGE command                      | git equivalent      | Description                        |
|------------------------------------|---------------------|------------------------------------|
| `forge serve [--port <n>]`         | `git daemon`        | Serve repo over TCP                |
| `forge config <key> [<value>]`     | `git config`        | Get or set config values           |
| `forge ls-files`                   | `git ls-files`      | List all tracked files             |
| `forge cat-obj <sha1>`             | `git cat-file`      | Dump raw object                    |
| `forge hash-obj [-w] <file>`       | `git hash-object`   | Hash a file, optionally store it   |
| `forge list-objects`               | `git rev-list --objects` | List all stored objects       |

---

## Typical Workflow

```bash
# Initialise
mkdir myproject && cd myproject
forge init

# Set your identity in .forge/config:
#   [user]
#       name  = Dhrubo
#       email = dhrubo@example.com

# Create some files
echo "hello forge" > hello.c

# Stage and commit
forge put hello.c
forge msg -m "initial commit"

# Check status and history
forge status
forge log

# Work on a feature branch
forge branch feature-x
forge checkout feature-x
echo "new stuff" >> hello.c
forge put hello.c
forge msg -m "add new stuff"

# Merge back
forge checkout main
forge merge feature-x

# Push to a remote server over SSH
forge remote add origin dhrubo@myserver.io:~/repos/myproject
forge shoot origin

# Pull updates
forge fetch origin
```

---

## Ignore Files

Create `.forgeignore` in your repo root:

```
*.o
*.a
build/
secrets/
```

FORGE always ignores its own `.forge/` directory.

---

## Object Model

```
Commit -> Tree -> Blob (file content)
  |          \-> Tree \-> Blob (subdir)
  \-> parent Commit (chain)
```

All objects are stored at:

```
.forge/objects/<XX>/<remaining-38-hex-chars>
```

This is identical to git's loose object storage format. Writes are
atomic — FORGE uses lock files to prevent index and ref corruption
from concurrent or interrupted operations.

---

## Remote Transport

FORGE uses SSH + rsync for remote operations — the same method early
git users employed before the git wire protocol existed:

```bash
forge shoot user@host:path    # push
forge fetch user@host:path    # pull
```

Requires `ssh` and `rsync` on both sides. The remote server does **not**
need FORGE installed — only the local client does.

FORGE also has a built-in TCP server for local network use:

```bash
forge serve --port 9418
```

---

## Identity

Set your name and email in `.forge/config`:

```ini
[user]
    name  = Shahriar Dhrubo
    email = dhrubo@example.com
```

Or use the config command:

```bash
forge config user.name "Shahriar Dhrubo"
forge config user.email "dhrubo@example.com"
```