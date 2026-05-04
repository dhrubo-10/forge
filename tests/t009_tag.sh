#!/bin/bash
# tests/t009_tag.sh 
# forge tag tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge tag"

setup_repo
forge init >/dev/null 2>&1
echo "hello" > hello.c
forge put hello.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_expect_success "tag creates a tag" "
    forge tag v1.0
"

test_expect_success "tag ref file exists" \
    "test -f .forge/refs/tags/v1.0"

test_output_contains "tag list shows v1.0" \
    "forge tag -l" \
    "v1.0"

test_expect_success "tag on specific sha works" "
    SHA=\$(forge log --oneline | head -1 | awk '{print \$1}') &&
    forge tag v0.1 \$SHA
"

test_output_contains "tag list shows v0.1" \
    "forge tag -l" \
    "v0.1"

test_expect_success "tag -d deletes a tag" "
    forge tag -d v0.1
"

test_expect_failure "deleted tag ref is gone" \
    "test -f .forge/refs/tags/v0.1"

test_output_contains "tag list no longer shows v0.1" \
    "forge tag -l" \
    "v1.0"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]