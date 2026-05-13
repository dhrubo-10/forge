#!/bin/bash
# tests/t018_hash_obj.sh
# forge hash-obj and cat-obj tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge hash-obj + cat-obj"

setup_repo
forge init >/dev/null 2>&1
echo "hello forge" > hello.c

test_expect_success "hash-obj prints a sha" "
    SHA=\$(forge hash-obj hello.c) &&
    test -n \"\$SHA\" &&
    test \${#SHA} -eq 40
"

test_expect_success "hash-obj does not store without -w" "
    SHA=\$(forge hash-obj hello.c) &&
    ! test -f .forge/objects/\${SHA:0:2}/\${SHA:2}
"

test_expect_success "hash-obj -w stores the object" "
    SHA=\$(forge hash-obj -w hello.c) &&
    test -f .forge/objects/\${SHA:0:2}/\${SHA:2}
"

test_expect_success "same file always gives same sha" "
    SHA1=\$(forge hash-obj hello.c) &&
    SHA2=\$(forge hash-obj hello.c) &&
    test \"\$SHA1\" = \"\$SHA2\"
"

test_expect_success "different content gives different sha" "
    echo 'other content' > other.c &&
    SHA1=\$(forge hash-obj hello.c) &&
    SHA2=\$(forge hash-obj other.c) &&
    test \"\$SHA1\" != \"\$SHA2\"
"

forge put hello.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

BLOB_SHA=$(forge hash-obj hello.c)

test_output_contains "cat-obj shows blob type" \
    "forge cat-obj $BLOB_SHA" \
    "blob"

test_output_contains "cat-obj shows file content" \
    "forge cat-obj $BLOB_SHA" \
    "hello forge"

test_expect_success "cat-obj fails on unknown sha" "
    forge cat-obj 0000000000000000000000000000000000000000 2>&1 | grep -q 'not found'
"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]