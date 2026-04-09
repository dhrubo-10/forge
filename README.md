```
  ███████╗ ██████╗ ██████╗  ██████╗ ███████╗
  ██╔════╝██╔═══██╗██╔══██╗██╔════╝ ██╔════╝
  █████╗  ██║   ██║██████╔╝██║  ███╗█████╗
  ██╔══╝  ██║   ██║██╔══██╗██║   ██║██╔══╝
  ██║     ╚██████╔╝██║  ██║╚██████╔╝███████╗
  ╚═╝      ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚══════╝
```

FORGE is a minimal distributed version control system written in C,
directly inspired by intial GIT.

It uses the same object model: content-addressed SHA-1 blobs, trees,
and commits stored zlib-compressed in `.forge/objects/`, with human-readable
refs in `.forge/refs/`. It is architecturally compatible with git's loose
object format.

---

## Build

```bash
# Install zlib dev headers if needed
sudo apt install zlib1g-dev

cd forge/
make
make install      # copies to ~/.local/bin/forge
```

---

## Command Reference

| FORGE command        | git equivalent | Description                    |
|----------------------|----------------|--------------------------------|
| `forge init`         | `git init`     | Initialise a new repository    |
| `forge put <files>`  | `git add`      | Stage files for commit         |
| `forge put -a`       | `git add -A`   | Stage all files                |
| `forge msg -m "..."` | `git commit`   | Commit staged snapshot         |
| `forge status`       | `git status`   | Show working tree status       |
| `forge log`          | `git log`      | Show commit history            |
| `forge diff`         | `git diff`     | Diff staged vs working tree    |
| `forge branch <n>`   | `git branch`   | Create/list branches           |
| `forge checkout <b>` | `git checkout` | Switch branch                  |
| `forge shoot <r>`    | `git push`     | Push to remote via SSH         |
| `forge fetch <r>`    | `git pull`     | Fetch from remote via SSH      |
| `forge remote add`   | `git remote`   | Add a named remote             |
| `forge cat-obj <s>`  | `git cat-file` | Dump raw object (debug)        |

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
  |          \_> Tree \_> Blob (subdir)
  \> parent Commit (chain)
```

All objects are stored at:
```
.forge/objects/<XX>/<remaining-38-hex-chars>
```

This is identical to git's loose object storage format.

---

## Remote Transport

FORGE uses SSH + rsync for remote operations — the same method early git
users employed before the git wire protocol existed:

```
forge shoot user@host:path    # push
forge fetch user@host:path    # pull
```

Requires `ssh` and `rsync` on both sides. The remote server does **not**
need FORGE installed — only the local client does.

---