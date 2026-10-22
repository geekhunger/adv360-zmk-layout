#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_build=$(mktemp -d "${TMPDIR:-/tmp}/adv360-profile-os-tests.XXXXXX")
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$test_root/tests/native/profile_os_stubs" \
  "$test_root/tests/native/profile_os_test.c" -o "$test_build/profile_os_test"
"$test_build/profile_os_test"
