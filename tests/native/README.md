# Resolver regression checks

Run `sh tests/native/run.sh` from the repository. Set `ZMK_DIR` to a checkout
of the ZMK revision in `config/west.yml`; by default it is the sibling `../zmk`.
`CC` selects the native C compiler. The CI legacy build runs the same checks.

The harness includes the production resolver C file directly. It replaces
Zephyr delayed work with a deterministic clock and records encoded HID events
and modifier reference counts. It models the capture listener, semantic
listener, and keymap dispatch in their production order, including event replay.
It uses ZMK's real keycode definitions and runs with AddressSanitizer and UBSan.

Coverage includes:

- Modifier release/repress while an arrow, Space, or another dual-role key is down.
- Early Shift release, opposite Shift+Tab, Alt+Cmd+Esc, and every release ordering
  of an Alt+Cmd+Shift+arrow chord.
- Double letters followed immediately by shortcuts, Space, or five individual
  Backspaces, swept over 60 event timings.
- Single Backspace durations, modified Backspace repetitions, tap-tap-hold
  repeat, semantic holds, Windows navigation, and representative symbol mappings.
- Delayed pulse modifier snapshots and 2,000 seeded mixed sequences of 100
  events each, plus 50,000 events without resetting the resolver. Once all
  physical keys are released and timers settle, the HID
  modifier counts, held keys, resolver states, and deferred queues must be empty.

These tests verify software state and ordering. They do not model radio loss,
USB transfer timing, matrix scanning, host key repeat, or OS text interpretation.
The physical keymap subset in the harness is explicit and must be kept in sync
if positions in `config/adv360.keymap` change.

Use `TEST_FILTER='Alt+Right' TEST_TRACE=1 sh tests/native/run.sh` to inspect a
case. Set `ADV2_RESOLVER_SOURCE` to another resolver file to compare the same
tests against a prior version without modifying the working tree.
