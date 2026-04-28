#!/bin/bash
# tests/run_tests.sh - run all tests

TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
FORGE_BIN="$TESTS_DIR/../forge"

if [ ! -x "$FORGE_BIN" ]; then
    echo "forge: binary not found at $FORGE_BIN"
    echo "run 'make' first"
    exit 1
fi

export PATH="$TESTS_DIR/..:$PATH"

TOTAL_PASS=0
TOTAL_FAIL=0

for t in "$TESTS_DIR"/t[0-9]*.sh; do
    [ -f "$t" ] || continue
    name=$(basename "$t")
    echo "=== $name ==="
    bash "$t"
    status=$?
    if [ $status -ne 0 ]; then
        TOTAL_FAIL=$((TOTAL_FAIL + 1))
    else
        TOTAL_PASS=$((TOTAL_PASS + 1))
    fi
    echo ""
done


printf "suites passed: \033[32m%d\033[0m  failed: \033[31m%d\033[0m\n" \
    "$TOTAL_PASS" "$TOTAL_FAIL"


[ "$TOTAL_FAIL" -eq 0 ]