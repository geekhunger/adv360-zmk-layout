#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_build=$(mktemp -d "${TMPDIR:-/tmp}/adv360-bt-control-tests.XXXXXX")
"${CC:-cc}" -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-const-variable \
  -fsanitize=address,undefined -g -I"$test_root/tests/native/bt_control_stubs" \
  -I"$test_root/tests/native/stubs" -I"$test_root/include" \
  "$test_root/tests/native/bt_control_test.c" -o "$test_build/bt_control_test"
"$test_build/bt_control_test"
