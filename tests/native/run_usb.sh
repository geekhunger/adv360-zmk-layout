#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
usb_source=${USB_HID_SOURCE:-${ZMK_DIR:-"$test_root/../zmk"}/app/src/usb_hid.c}
test_build=$(mktemp -d "${TMPDIR:-/tmp}/adv360-usb-tests.XXXXXX")
if [ -n "${USB_HID_PATCH:-}" ]; then
  mkdir -p "$test_build/candidate/app/src" "$test_build/candidate/app/include/zmk"
  cp "$usb_source" "$test_build/candidate/app/src/usb_hid.c"
  cp "$(dirname "$usb_source")/usb.c" "$test_build/candidate/app/src/usb.c"
  cp "$(dirname "$usb_source")/../include/zmk/usb_hid.h" "$test_build/candidate/app/include/zmk/usb_hid.h"
  patch -s -p1 -d "$test_build/candidate" < "$USB_HID_PATCH"
  usb_source="$test_build/candidate/app/src/usb_hid.c"
fi
# Mechanically extract the production state/callback and send function, not a
# reimplementation. Exclude descriptor/control-request code irrelevant here.
awk '
  /^static const struct device \*hid_dev;/ { copying=1 }
  /^#define HID_GET_REPORT_TYPE_MASK/ { copying=0 }
  /^static int zmk_usb_hid_send_report\(/ { copying=1 }
  /^int zmk_usb_hid_send_keyboard_report\(/ { copying=0 }
  /^void zmk_usb_hid_reset_transport\(/ { print "#define ADV360_USB_RESET_HOOK 1" }
  copying { print }
' "$usb_source" > "$test_build/usb_send.inc"
"${CC:-cc}" -std=c11 -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined -g \
  -DUSB_TRANSPORT_SOURCE=\"$test_build/usb_send.inc\" \
  "$test_root/tests/native/usb_hid_transport_test.c" -o "$test_build/usb_hid_test"
"$test_build/usb_hid_test"
