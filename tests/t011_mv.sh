#!/bin/bash
# tests/t011_mv.sh forge mv tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"
echo "forge mv"

setup_repo
forge init >/dev/null 2>&1
echo "int main() { return 0; }" > main.c
forge put main.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_expect_success "mv renames file on disk" "
    forge mv main.c renamed.c &&
    test -f renamed.c
"

test_expect_failure "mv removes old file from disk" \
    "test -f main.c"

test_expect_success "mv updates the index" "
    forge status 2>&1 | grep -q 'renamed.c'
"

test_expect_success "mv renamed file can be committed" "
    forge msg -m 'rename main to renamed' >/dev/null 2>&1
"

test_output_contains "log shows rename commit" \
    "forge log --oneline" \
    "rename main to renamed"
test_expect_success "mv fails on nonexistent file" "
    forge mv nosuchfile.c other.c 2>&1 | grep -q 'does not exist'
"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]