#!/bin/bash
# tests/t006_cherry_pick.sh: forge cherry-pick tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge cherry-pick"

setup_repo
forge init >/dev/null 2>&1
echo "hello" > hello.c
forge put hello.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_expect_success "create feature branch with a commit" "
    forge branch feature-x &&
    forge checkout feature-x &&
    echo 'feature line' >> hello.c &&
    forge put hello.c &&
    forge msg -m 'feature commit'
"

forge checkout feature-x >/dev/null 2>&1
PICK_SHA=$(forge log | sed 's/\x1b\[[0-9;]*m//g' | grep "^commit" | head -1 | awk '{print $2}')
forge checkout main >/dev/null 2>&1

test_expect_success "cherry-pick feature commit onto main" "
    forge cherry-pick $PICK_SHA
"

test_expect_success "cherry-picked content is in working tree" "
    grep -q 'feature line' hello.c
"

test_output_contains "log shows cherry-picked commit" \
    "forge log --oneline" \
    "feature commit"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]