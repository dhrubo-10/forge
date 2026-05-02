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
    forge stash
"

test_expect_success "stash cleans working tree" "
    grep -q 'new stuff' hello.c && exit 1 || exit 0
"

test_output_contains "stash list shows entry" \
    "forge stash list" \
    "stash@{0}"

test_expect_success "stash pop restores changes" "
    forge stash pop &&
    grep -q 'new stuff' hello.c
"

test_expect_failure "stash list is empty after pop" \
    "forge stash list | grep -q 'stash@{0}'"

test_expect_success "stash with no changes does nothing" "
    forge stash 2>&1 | grep -q 'no local changes'
"

test_expect_success "stash drop discards stash" "
    echo 'change' >> hello.c &&
    forge stash &&
    forge stash drop
"

test_expect_success "stash list empty after drop" "
    forge stash list | grep -q 'no stash' || ! forge stash list | grep -q 'stash@{0}'
"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]