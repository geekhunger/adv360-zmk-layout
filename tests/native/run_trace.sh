#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_build=$(mktemp -d "${TMPDIR:-/tmp}/adv360-trace-tests.XXXXXX")
# Keep the actual implementation; replace only OS/driver interfaces in the test.
awk '/^#define TRACE_CAPACITY/ {copying=1} copying {print}' \
  "$test_root/src/diagnostics/adv360_trace.c" > "$test_build/trace.inc"
"${CC:-cc}" -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
  -fsanitize=address,undefined -g -I"$test_root/include" \
  -DTRACE_SOURCE=\"$test_build/trace.inc\" \
  "$test_root/tests/native/trace_test.c" -o "$test_build/trace_test"
"$test_build/trace_test"
