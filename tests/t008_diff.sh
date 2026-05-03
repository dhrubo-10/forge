#!/bin/bash
# tests/t008_diff.sh: forge diff tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge diff"

setup_repo
forge init >/dev/null 2>&1
echo "int main() { return 0; }" > main.c
forge put main.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_expect_success "diff shows nothing when clean" "
    forge diff 2>&1 | grep -q 'nothing to diff'
"

test_expect_success "diff shows changes after modification" "
    echo 'int x = 1;' >> main.c &&
    forge diff 2>&1 | grep -qv 'nothing to diff'
"

test_output_contains "diff output contains filename" \
    "forge diff" \
    "main.c"

test_expect_success "diff --cached shows nothing before staging" "
    forge diff --cached 2>&1 | grep -q 'nothing to diff'
"

test_expect_success "diff --cached shows changes after staging" "
    forge put main.c >/dev/null 2>&1 &&
    forge diff --cached 2>&1 | grep -qv 'nothing to diff'
"

test_output_contains "diff --cached output contains filename" \
    "forge diff --cached" \
    "main.c"

test_expect_success "diff clean after commit" "
    forge msg -m 'add x' >/dev/null 2>&1 &&
    forge diff 2>&1 | grep -q 'nothing to diff'
"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]