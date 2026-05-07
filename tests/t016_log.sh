#!/bin/bash
# tests/t016_log.sh
# forge log tests

# Author: Raihan Mahmud

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge log"

setup_repo
forge init >/dev/null 2>&1
echo "first" > first.c
forge put first.c >/dev/null 2>&1
forge msg -m "first commit" >/dev/null 2>&1
echo "second" > second.c
forge put second.c >/dev/null 2>&1
forge msg -m "second commit" >/dev/null 2>&1
echo "third" > third.c
forge put third.c >/dev/null 2>&1
forge msg -m "third commit" >/dev/null 2>&1

test_output_contains "log shows all commits" \
    "forge log" \
    "third commit"

test_output_contains "log shows author" \
    "forge log" \
    "Author"

test_output_contains "log shows date" \
    "forge log" \
    "Date"

test_output_contains "log --oneline shows short format" \
    "forge log --oneline" \
    "third commit"

test_expect_success "log --oneline is shorter than full log" "
    full=\$(forge log | wc -l) &&
    short=\$(forge log --oneline | wc -l) &&
    test \$short -lt \$full
"

test_output_contains "log --oneline shows all three commits" \
    "forge log --oneline | wc -l" \
    "3"

test_output_contains "log --graph shows commit nodes" \
    "forge log --graph" \
    "*"

test_expect_success "log on empty repo shows no commits" "
    setup_repo
    forge init >/dev/null 2>&1
    forge log 2>&1 | grep -q 'no commits'
    teardown_repo
    setup_repo
    forge init >/dev/null 2>&1
"
teardown_repo

print_results
[ "$FAIL" -eq 0 ]