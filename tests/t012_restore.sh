#!/bin/bash
# tests/t012_restore.sh
# forge restore tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"



echo "forge restore"

setup_repo
forge init >/dev/null 2>&1
echo "original content" > hello.c
forge put hello.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_expect_success "restore brings file back to HEAD state" "
    echo 'modified content' > hello.c &&
    forge restore hello.c &&
    grep -q 'original content' hello.c
"

test_expect_failure "modified content is gone after restore" "
    grep -q 'modified content' hello.c
"

test_expect_success "restore works after multiple commits" "
    echo 'second version' > hello.c &&
    forge put hello.c >/dev/null 2>&1 &&
    forge msg -m 'second commit' >/dev/null 2>&1 &&
    echo 'dirty' > hello.c &&
    forge restore hello.c &&
    grep -q 'second version' hello.c
"

FIRST=$(forge log | sed 's/\x1b\[[0-9;]*m//g' | grep "^commit" | tail -1 | awk '{print $2}')

test_expect_success "restore --source restores from specific commit" "
    forge restore --source $FIRST hello.c &&
    grep -q 'original content' hello.c
"

test_expect_success "restore fails on untracked file" "
    forge restore nosuchfile.c 2>&1 | grep -q 'not found'
"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]