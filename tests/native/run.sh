#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
zmk_headers=${ZMK_DIR:-"$test_root/../zmk"}/app/include
test_build=$(mktemp -d "${TMPDIR:-/tmp}/adv360-resolver-tests.XXXXXX")
python3 "$test_root/tests/native/check_num_media.py"
test_source="$test_root/tests/native/resolver_test.c"
if [ "${TEST_TRACE_HOOKS:-0}" = 1 ]; then
  test_source="$test_root/tests/native/resolver_trace_test.c"
fi
"${CC:-cc}" -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-const-variable \
  -fsanitize=address,undefined -g -I"$test_root/tests/native/stubs" \
  -DADV2_RESOLVER_SOURCE=\"${ADV2_RESOLVER_SOURCE:-$test_root/src/behaviors/behavior_adv2_resolver.c}\" \
  -DNUM_MOD_MORPH_SOURCE=\"${ZMK_DIR:-$test_root/../zmk}/app/src/behaviors/behavior_mod_morph.c\" \
  -I"$test_root/include" -I"$zmk_headers" \
  "$test_source" -o "$test_build/resolver_test"
"$test_build/resolver_test"
