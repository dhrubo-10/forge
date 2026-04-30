#!/bin/bash
# tests/lib.sh - just a test env

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FORGE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FORGE_BIN="$FORGE_ROOT/forge"

PASS=0
FAIL=0
SKIP=0
TEST_DIR=""

setup_repo() {
    TEST_DIR=$(mktemp -d)
    cd "$TEST_DIR" || exit 1
    export PATH="$FORGE_ROOT:$PATH"
}

teardown_repo() {
    cd /tmp || exit 1
    rm -rf "$TEST_DIR"
}

test_expect_success() {
    local desc="$1"
    local cmd="$2"
    if eval "$cmd" >/dev/null 2>&1; then
        printf "  \033[32mok\033[0m  %s\n" "$desc"
        PASS=$((PASS + 1))
    else
        printf "  \033[31mFAIL\033[0m %s\n" "$desc"
        FAIL=$((FAIL + 1))
    fi
}

test_expect_failure() {
    local desc="$1"
    local cmd="$2"
    if ! eval "$cmd" >/dev/null 2>&1; then
        printf "  \033[32mok\033[0m  %s\n" "$desc"
        PASS=$((PASS + 1))
    else
        printf "  \033[31mFAIL\033[0m %s\n" "$desc"
        FAIL=$((FAIL + 1))
    fi
}

test_output_contains() {
    local desc="$1"
    local cmd="$2"
    local pattern="$3"
    local out
    out=$(eval "$cmd" 2>&1)
    if echo "$out" | grep -q "$pattern"; then
        printf "  \033[32mok\033[0m  %s\n" "$desc"
        PASS=$((PASS + 1))
    else
        printf "  \033[31mFAIL\033[0m %s\n" "$desc"
        printf "       expected: %s\n" "$pattern"
        printf "       got:      %s\n" "$out"
        FAIL=$((FAIL + 1))
    fi
}

print_results() {
    echo ""
    printf "  passed: \033[32m%d\033[0m  failed: \033[31m%d\033[0m\n" "$PASS" "$FAIL"
    echo ""
}