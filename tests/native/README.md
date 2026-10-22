# Resolver regression checks

Run `sh tests/native/run.sh` from the repository. Set `ZMK_DIR` to a checkout
of the ZMK revision in `config/west.yml`; by default it is the sibling `../zmk`.
`CC` selects the native C compiler. The CI legacy build runs the same checks.

The harness includes the production resolver C file directly. It replaces
Zephyr delayed work with a deterministic clock and records encoded HID events
and modifier reference counts. It dispatches the listeners actually registered
by the production source, followed by keymap dispatch, including AT/AFTER replay.
`TEST_REVERSE_LISTENERS=1` reverses registration order, as optimized GCC did in
v0.3.8. The resolver now has only one subscription and explicitly calls capture
before semantic interruption, so this order cannot change with optimization.
It uses ZMK's real keycode definitions and runs with AddressSanitizer and UBSan.

Coverage includes:

- Modifier release/repress while an arrow, Space, or another dual-role key is down.
- Early Shift release, opposite Shift+Tab, Alt+Cmd+Esc, and every release ordering
  of an Alt+Cmd+Shift+arrow chord.
- Double letters followed immediately by shortcuts, Space, or five individual
  Backspaces, swept over 60 event timings, with and without held modifiers.
- The last letter's release overlapping a modified Backspace; correction taps
  behind a repeated-letter barrier, checked individually while Alt/Cmd stay down.
- A word buffered behind an unresolved modifier must appear before its following
  Backspace, including when that Backspace resolves the earlier modifier.
- Blocking HID sends from 1 through 30 ms while words and corrections arrive.
- Message-queue batches without a timer callback between every position: 3,906
  word/correction combinations plus 2,000 mixed 100-event sequences.
- Nested modifier-queue releases during pulse replay: distinct letter presses
  and arrow/Space releases must not be mistaken for the current replay head.
  Timing sweeps also cover Delete/Backspace, delayed reports, and cleanup.
- Single Backspace durations, modified Backspace repetitions, tap-tap-hold
  repeat, semantic holds, Windows navigation, and representative symbol mappings.
- All semantic keys with either Alt and additional Shift/Cmd/Ctrl: early Alt
  release must match the existing symbol output, with no Delete/Backspace.
  Alt+either Shift is checked in both press orders and all six release orders.
- A letter before one/two Backspace taps preserves their exact count, even with
  overlapping release and an additional modifier. An intervening key cancels
  the old Delete/Backspace repeat window; consecutive tap-tap-hold still works.
- Eight successive rolled Alt+A chords yield eight plus signs, not `a` or
  Backspace, under both OS modes and either Alt/A release order.
- Delayed pulse modifier snapshots and 2,000 seeded mixed sequences of 100
  events each, plus 50,000 events without resetting the resolver. Once all
  physical keys are released and timers settle, the HID
  modifier counts, held keys, resolver states, and deferred queues must be empty.
- NUM media and brightness selection under both OS layers and either/both Shift
  keys, repeated presses, and all release permutations of NUM/Shift/the arrow.
  The harness includes the pinned ZMK `behavior_mod_morph.c` directly. It records
  backlight commands, not physical LED or split-radio effects. Keymap dispatch
  retains each key's NUM state at press for release, like ZMK's layer cache.

`check_num_media.py` runs before native tests. It verifies exactly six changed
NUM positions against v0.3.11, both Shift selectors and all four morph bindings,
and unchanged base, Windows and Bluetooth layers. CI also passes the built
`zephyr.dts` and Zephyr's dtlib path to verify resolved phandles and HID usages.

These tests verify software state and ordering. The artificial report delays do
not reproduce real USB transfers, radio loss, matrix scanning, host key repeat,
or OS text interpretation.
The physical keymap subset in the harness is explicit and must be kept in sync
if positions in `config/adv360.keymap` change.

Use `TEST_FILTER='Alt+Right' TEST_TRACE=1 sh tests/native/run.sh` to inspect a
case. Set `ADV2_RESOLVER_SOURCE` to another resolver file to compare the same
tests against a prior version without modifying the working tree.

## Matrix debounce

Run `sh tests/native/run_debounce.sh` against the pinned sibling ZMK checkout.
It exercises ZMK's actual debounce implementation with synthetic electrical
levels at 1 ms intervals, before any position event or resolver is involved.
The 15/15 ms fork defaults are compared with the explicit 5/5 ms configuration
for short presses, short release gaps and brief noise. These tests verify the
software filter, not the user's physical switches or actual scan scheduling.
CI separately checks both built `.config` files for the required 5/5 values.

## USB report ownership

Run `sh tests/native/run_usb.sh` against a patched ZMK checkout. To compare the
unmodified sibling checkout without changing it, use:

```sh
USB_HID_PATCH="$PWD/patches/zmk-usb-hid-stable-buffer.patch" sh tests/native/run_usb.sh
```

This harness mechanically extracts the actual send function, semaphore and
transmit-buffer declarations from ZMK's `usb_hid.c`. Its simulated nrfx
endpoint keeps the supplied pointer until completion instead of copying on
submit. It checks short taps, distinct modifier/key snapshots, semaphore
timeouts, failed submissions, all supported report storage sizes and
reconnection after USB reset aborts an in-flight transfer.

The unpatched pinned source fails nine assertions (including the missing
Backspace down); the patched source passes. This establishes the live-buffer
ownership defect. It does not measure real radio/USB latency or prove that
every hardware symptom has the same cause. The earlier resolver-only harness
records emitted events immediately and cannot detect this class of defect.

## Compiled-firmware check

CI also runs `check_firmware_symbols.sh ELF NM` on each built left firmware. It
requires exactly one `adv2` position subscription and verifies that it precedes
ZMK's keymap subscription. It also requires the stable `hid_tx_report` buffer
in the linked image. This checks the actual linked ELF, not just the mock.

The v0.3.8 UF2 (commit `2d9d3a8`, left SHA-256
`fb87933a57de8432d65828473ecffd51f13391edb2630dbadbdee3a972c18624`)
contained semantic subscription `0x5e5c0` (callback `0x2b05d`) before capture
subscription `0x5e5c8` (callback `0x2bad1`). The old harness hard-coded the
opposite order. Replaying the actual compiled order reproduces lost Alt and an
unwanted Delete in the double-letter-to-Alt+Right regression. The new harness
and linked-symbol check prevent that false-positive test result.

A second regression checks the timing of modifier promotion: replaying buffered
letters from inside the Backspace binding was too late for that press's semantic
interruption. One or more letters could appear after Backspace, 170 ms later.
Promotion now takes place in capture, before semantic interruption of the new
press. Tests cover this order with both Cmd keys, Alt, and both Shift keys.
