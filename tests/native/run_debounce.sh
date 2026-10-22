#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
zmk_root=${ZMK_DIR:-"$test_root/../zmk"}
test_build=$(mktemp -d "${TMPDIR:-/tmp}/adv360-debounce-tests.XXXXXX")

# Compile the actual pinned debounce.c AND its real debounce.h. The only
# Zephyr utility needed by that header is BIT_MASK, generated as a build stub.
mkdir -p "$test_build/zephyr/sys"
printf '%s\n' '#pragma once' '#define BIT_MASK(n) ((1UL << (n)) - 1UL)' \
  > "$test_build/zephyr/sys/util.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-sign-compare \
  -fsanitize=address,undefined -g \
  -I"$test_build" -I"$zmk_root/app/module/include" \
  "$test_root/tests/native/debounce_test.c" \
  "$zmk_root/app/module/lib/zmk_debounce/debounce.c" \
  -o "$test_build/debounce_test"
"$test_build/debounce_test"
