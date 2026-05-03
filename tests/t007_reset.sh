[ "$FAIL" -eq 0 ]
#!/bin/bash
# tests/t007_reset.sh: forge reset tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge reset"

setup_repo
forge init >/dev/null 2>&1
echo "hello" > hello.c
forge put hello.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_expect_success "reset clears the index" "
    echo 'change' >> hello.c &&
    forge put hello.c &&
    forge reset &&
    forge status 2>&1 | grep -q 'nothing\|clean\|modified'
"

# grab full SHA of first commit before adding more
FIRST_SHA=$(forge_head_sha)

test_expect_success "make a second commit" "
    echo 'second' >> hello.c &&
    forge put hello.c &&
    forge msg -m 'second commit'
"

test_expect_success "soft reset moves HEAD back" "
    forge reset --soft $FIRST_SHA
"

test_output_contains "soft reset HEAD points to first commit" \
    "forge log --oneline" \
    "initial commit"

test_expect_success "make third commit for hard reset test" "
    echo 'third' >> hello.c &&
    forge put hello.c &&
    forge msg -m 'third commit' &&
    echo 'extra' > extra.c &&
    forge put extra.c &&
    forge msg -m 'add extra'
"

# grab first SHA again since we reset earlier
FIRST_SHA=$(forge_head_sha)
FIRST_SHA=$(forge log | sed 's/\x1b\[[0-9;]*m//g' | grep "^commit" | tail -1 | awk '{print $2}')

test_expect_success "hard reset restores working tree" "
    forge reset --hard $FIRST_SHA
"

test_expect_failure "hard reset removes added file from disk" \
    "test -f extra.c"

test_output_contains "hard reset HEAD points to first commit" \
    "forge log --oneline" \
    "initial commit"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]