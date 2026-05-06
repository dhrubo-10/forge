#!/bin/bash
# tests/t013_config.sh 
#forge config tests



. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge config"

setup_repo
forge init >/dev/null 2>&1

test_expect_success "config sets user.name" "
    forge config user.name 'Test User'
"

test_output_contains "config get returns user.name" \
    "forge config user.name" \
    "Test User"

test_expect_success "config sets user.email" "
    forge config user.email 'test@forge.local'
"

test_output_contains "config get returns user.email" \
    "forge config user.email" \
    "test@forge.local"

test_expect_success "commit uses configured identity" "
    echo 'hello' > hello.c &&
    forge put hello.c >/dev/null 2>&1 &&
    forge msg -m 'identity test' >/dev/null 2>&1
"

test_output_contains "log shows configured author" \
    "forge log" \
    "Test User"

test_expect_success "config get fails on unknown key" "
    forge config user.nosuchkey 2>&1 | grep -q 'not set'
"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]