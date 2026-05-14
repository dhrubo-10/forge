#!/bin/bash
# tests/t010_grep.sh: forge grep tests

. "$(dirname "$0")/lib.sh"
export PATH="$FORGE_ROOT:$PATH"
echo "forge grep"

setup_repo
forge init >/dev/null 2>&1
echo "int main() { return 0; }" > main.c
echo "int helper() { return 1; }" > helper.c
forge put main.c helper.c >/dev/null 2>&1
forge msg -m "initial commit" >/dev/null 2>&1

test_output_contains "grep finds pattern in file" \
    "forge grep main" \
    "main.c"

test_output_contains "grep finds pattern across files" \
    "forge grep int" \
    "main.c"

test_output_contains "grep -n shows line numbers" \
    "forge grep -n main | sed 's/\x1b\[[0-9;]*m//g'" \
    "main.c:1:int"

test_expect_success "grep -i is case insensitive" "
    forge grep -i MAIN 2>&1 | grep -q 'main.c'
"

test_expect_success "grep returns 1 when no matches" "
    forge grep 'zzznomatch' 2>&1
    test \$? -ne 0 || forge grep 'zzznomatch' 2>&1 | grep -q 'no matches'
"

test_output_contains "grep finds pattern in helper file" \
    "forge grep helper" \
    "helper.c"

teardown_repo
print_results
[ "$FAIL" -eq 0 ]