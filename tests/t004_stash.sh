#!/bin/bash
# tests/t004_stash.sh: forge stash tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge stash"

setup_repo
forge init >/dev/null 2>&1
echo "hello" > hello.c
forge put hello.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_expect_success "stash saves working tree changes" "
    echo 'new stuff' >> hello.c &&
    forge stash 2>&1 | grep -q 'stashed'
"

test_expect_success "stash cleans working tree" "
    ! grep -q 'new stuff' hello.c
"

test_expect_success "stash pop restores changes" "
    forge stash pop &&
    grep -q 'new stuff' hello.c
"

test_expect_success "stash with no changes does nothing" "
    forge put hello.c >/dev/null 2>&1 &&
    forge msg -m 'commit stashed changes' >/dev/null 2>&1 &&
    forge stash 2>&1 | grep -q 'no local changes'
"

test_expect_success "stash drop discards stash" "
    echo 'change' >> hello.c &&
    forge stash >/dev/null 2>&1 &&
    forge stash drop 2>&1 | grep -q 'dropped'
"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]