#!/bin/bash
# tests/t014_rm.sh 
# forge rm tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge rm"

setup_repo
forge init >/dev/null 2>&1
echo "hello" > hello.c
echo "world" > world.c
forge put hello.c world.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_expect_success "rm removes file from disk" "
    forge rm hello.c &&
    test ! -f hello.c
"

test_expect_success "rm removes file from index" "
    ! grep -q 'hello.c' .forge/index
"

test_expect_success "rm --cached removes from index only" "
    forge rm --cached world.c
"

test_expect_success "file still exists on disk after --cached" "
    test -f world.c
"

test_expect_success "status shows deleted files" "
    forge status 2>&1 | grep -qE 'deleted|hello'
"

test_expect_success "rm fails on untracked file" "
    echo 'new' > new.c &&
    forge rm new.c 2>&1 | grep -q 'not in index'
"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]