#!/bin/bash
# tests/t015_show.sh 
# forge show tests


. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge show"

setup_repo
forge init >/dev/null 2>&1
echo "hello forge" > hello.c
forge put hello.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

HEAD_SHA=$(forge_head_sha)

test_expect_success "show with no args shows HEAD commit" "
    forge show 2>&1 | grep -q 'commit'
"

test_output_contains "show displays commit sha" \
    "forge show" \
    "$HEAD_SHA"

test_output_contains "show displays author" \
    "forge show" \
    "Author"

test_output_contains "show displays commit message" \
    "forge show" \
    "initial commit"

test_output_contains "show displays tree" \
    "forge show" \
    "Tree"

test_expect_success "show with explicit sha works" "
    forge show $HEAD_SHA 2>&1 | grep -q 'commit'
"

test_expect_success "show marks new files with +++" "
    forge show 2>&1 | grep -q '+++'
"

test_expect_success "show on unknown sha fails" "
    forge show 0000000000000000000000000000000000000000 2>&1 | grep -q 'not found'
"
test_expect_success "show second commit shows parent" "
    echo 'second' >> hello.c &&
    forge put hello.c >/dev/null 2>&1 &&
    forge msg -m 'second commit' >/dev/null 2>&1 &&
    forge show 2>&1 | grep -q 'Parent'
"
teardown_repo

print_results
[ "$FAIL" -eq 0 ]