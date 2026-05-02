#!/bin/bash
# tests/t003_branch.sh: forge branch and forge checkout tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge branch + forge checkout"

setup_repo
forge init >/dev/null 2>&1
echo "hello" > hello.c
forge put hello.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_expect_success "branch creates a new branch" "
    forge branch feature-x
"

test_expect_success "branch ref file exists" \
    "test -f .forge/refs/heads/feature-x"

test_output_contains "branch list shows feature-x" \
    "forge branch" \
    "feature-x"

test_expect_success "checkout switches branch" "
    forge checkout feature-x
"

test_output_contains "status shows correct branch" \
    "forge status" \
    "feature-x"

test_expect_success "checkout -b creates and switches" "
    forge checkout -b another-branch
"

test_output_contains "status shows another-branch" \
    "forge status" \
    "another-branch"

test_expect_success "branch -d deletes a branch" "
    forge checkout main >/dev/null 2>&1
    forge branch -d another-branch
"

test_expect_failure "deleted branch ref is gone" \
    "test -f .forge/refs/heads/another-branch"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]