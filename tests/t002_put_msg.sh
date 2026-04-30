#!/bin/bash
# tests/t002_put_msg.sh: forge put and forge msg tests

. "$(dirname "$0")/lib.sh"

echo "forge put + forge msg"

setup_repo
forge init >/dev/null 2>&1

test_expect_success "put stages a file" "
    echo 'hello forge' > hello.c &&
    forge put hello.c
"

test_expect_success "index file exists after put" \
    "test -f .forge/index"

test_expect_success "msg creates a commit" "
    forge msg -m 'initial commit'
"

test_expect_success "HEAD ref is set after commit" "
    sha=\$(cat .forge/HEAD | sed 's/ref: //')
    sha_file=\".forge/\$sha\"
    test -f \"\$sha_file\"
"

test_output_contains "log shows commit message" \
    "forge log --oneline" \
    "initial commit"

test_expect_success "put -a stages multiple files" "
    echo 'int x;' > a.c &&
    echo 'int y;' > b.c &&
    forge put -a
"

test_expect_success "msg commits multiple files" "
    forge msg -m 'add a and b'
"

test_output_contains "log shows second commit" \
    "forge log --oneline" \
    "add a and b"

test_expect_success "msg with nothing new to commit fails" "
    forge reset >/dev/null 2>&1 &&
    forge msg -m 'empty' 2>&1 | grep -q 'nothing'
"
teardown_repo

print_results
[ "$FAIL" -eq 0 ]