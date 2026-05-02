#!/bin/bash
# tests/t005_merge.sh: forge merge tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge merge"

setup_repo
forge init >/dev/null 2>&1
echo "hello" > hello.c
forge put hello.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_expect_success "create and switch to feature branch" "
    forge branch feature-x &&
    forge checkout feature-x
"

test_expect_success "commit on feature branch" "
    echo 'feature work' >> hello.c &&
    forge put hello.c &&
    forge msg -m 'feature commit'
"

test_expect_success "switch back to main" "
    forge checkout main
"

test_expect_success "fast-forward merge succeeds" "
    forge merge feature-x
"

test_output_contains "log shows feature commit after merge" \
    "forge log --oneline" \
    "feature commit"

test_expect_success "working tree has feature changes after merge" "
    grep -q 'feature work' hello.c
"

test_expect_success "merge already up to date" "
    forge merge feature-x 2>&1 | grep -q 'up to date'
"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]