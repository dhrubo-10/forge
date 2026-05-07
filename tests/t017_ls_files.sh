#!/bin/bash
# tests/t017_ls_files.sh
# forge ls-files tests

# Author: Ayman Chowdhury & Shahriar Dhrubo.

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge ls-files"

setup_repo
forge init >/dev/null 2>&1

test_expect_success "ls-files empty before first commit" "
    output=\$(forge ls-files 2>&1) &&
    test -z \"\$output\"
"

echo "hello" > hello.c
echo "world" > world.c
echo "foo"   > foo.c
forge put hello.c world.c foo.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_output_contains "ls-files shows hello.c" \
    "forge ls-files" \
    "hello.c"

test_output_contains "ls-files shows world.c" \
    "forge ls-files" \
    "world.c"

test_output_contains "ls-files shows foo.c" \
    "forge ls-files" \
    "foo.c"

test_output_contains "ls-files shows correct count" \
    "forge ls-files | wc -l" \
    "3"

test_expect_success "ls-files updates after rm" "
    forge rm foo.c >/dev/null 2>&1 &&
    forge msg -m 'remove foo' >/dev/null 2>&1 &&
    forge ls-files | grep -v 'foo.c' | grep -q 'hello.c'
"

test_expect_success "ls-files does not show untracked files" "
    echo 'untracked' > untracked.c &&
    ! forge ls-files | grep -q 'untracked.c'
"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]