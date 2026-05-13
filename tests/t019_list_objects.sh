#!/bin/bash
# tests/t019_list_objects.sh 
# forge list-objects tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"

echo "forge list-objects"

setup_repo
forge init >/dev/null 2>&1

test_expect_success "list-objects empty before first commit" "
    output=\$(forge list-objects 2>&1) &&
    test -z \"\$output\"
"

echo "hello" > hello.c
forge put hello.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_expect_success "list-objects shows objects after commit" "
    count=\$(forge list-objects | wc -l) &&
    test \$count -gt 0
"

test_output_contains "list-objects output is 40 char shas" \
    "forge list-objects | head -1 | wc -c" \
    "41"

test_expect_success "list-objects count grows after second commit" "
    echo 'second' > second.c &&
    forge put second.c >/dev/null 2>&1 &&
    forge msg -m 'second commit' >/dev/null 2>&1 &&
    count=\$(forge list-objects | wc -l) &&
    test \$count -gt 3
"

test_expect_success "each object path exists on disk" "
    forge list-objects | while read sha; do
        dir=\${sha:0:2}
        file=\${sha:2}
        test -f .forge/objects/\$dir/\$file || exit 1
    done
"

teardown_repo

print_results
[ "$FAIL" -eq 0 ]