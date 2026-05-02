#!/bin/bash
# tests/t001_init.sh: forge init tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge init"

setup_repo
test_expect_success "init creates .forge/" \
    "forge init && test -d .forge"

test_expect_success "init creates objects dir" \
    "test -d .forge/objects"

test_expect_success "init creates refs/heads" \
    "test -d .forge/refs/heads"

test_expect_success "init creates HEAD" \
    "test -f .forge/HEAD"

test_output_contains "HEAD points to main" \
    "cat .forge/HEAD" \
    "refs/heads/main"

test_expect_success "init creates config" \
    "test -f .forge/config"

test_expect_success "init is idempotent" \
    "forge init"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]