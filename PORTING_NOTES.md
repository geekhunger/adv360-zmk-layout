# Advantage 2 → Advantage 360 porting notes

This repository ports geekhunger's long-used Advantage 2 QMK layout to an
unmodified Kinesis Advantage 360 Pro running ZMK.

## Local ZMK split-transport patch

The pinned `refil/zmk` revision drains all queued right-half matrix snapshots
through `bt_gatt_notify()` in one worker invocation. Once Zephyr's Bluetooth TX
buffers are exhausted, later calls return an error and the upstream code has
already removed those snapshots from its queue. The same upstream path can
also block right-half matrix processing for up to 100 ms when its queue fills.

`patches/zmk-split-notify-backpressure.patch` is applied by both GitHub Actions
build jobs after `west update`. It permits one outstanding position
notification, advances the queue from the completion callback, retries
temporary TX backpressure, and makes the full-queue fallback non-blocking. It
does not change the keymap, resolver semantics, tapping terms, BLE connection
interval, or peripheral latency.

## Preserved intentionally

- macOS is the base profile; Windows is a sparse persistent overlay.
- Holding the right NUM thumb key momentarily activates the home-row numpad.
- While NUM is held, tapping the physical 360 `Esc` position toggles Windows.
- `&trans` inherits from the active profile; `&none` deliberately emits nothing.
- Letter holds and all modifier-aware symbol decisions mirror the QMK source.
- Windows and macOS use different host sequences for the same intended symbol.
- QMK timing is preserved: 90 ms for modifier tap/hold and 170 ms for letters.
- The letter keys use a custom semantic hold engine rather than ZMK hold-taps.
  Like QMK Tap Dance, equal consecutive taps accumulate until another key
  interrupts the dance or the 170 ms term expires. An uninterrupted key that is
  still down at 170 ms gets its hold symbol. The captured modifier state follows
  a delayed dance even when its dual-role modifier is released slightly early.
  Plain text outputs stay pressed for one 12 ms HID report window; different
  keys run in parallel, while repeated equal outputs are queued with a report
  gap so double letters remain distinct. Modifier-aware branches that use
  QMK's explicit `key_report(state->count, ...)` also retain that repeat count;
  this includes repeated punctuation such as `Alt + .` producing repeated `!`.
- Delete/Alt, Backspace/Alt, Tab/Shift, both Esc/Cmd-or-Ctrl keys, and
  Enter/Shift share one parallel 90 ms resolver. Each key owns its timer, so
  simultaneous modifiers never accumulate serial 90 ms delays.
- Tap versus modifier is never inferred from keyboard side, opposite-hand use,
  or a fixed hand rule. Overlapping action keys use balanced release order: when
  the dual-role key is released first it remains a tap, while an action completed
  first promotes the still-held dual-role key to its modifier. This preserves
  fast Backspace-to-letter, Enter-to-letter, and ordinary text rolls without
  making quick Alt/Shift/Cmd chords wait for the full 90 ms.
- Arrow presses are decisive live actions and promote pending modifiers on key
  down so selection and navigation remain responsive while held.
- In an already detected multi-modifier chord, a released candidate remains
  latched for 12 ms. A following action can therefore still receive Cmd/Shift/
  Alt/Ctrl when natural finger release order differs by only a few milliseconds.
- Releasing a dual-role target inside a chord promotes the older candidates and
  taps the target. This explicitly covers Cmd+Delete, Alt+Cmd+Esc, Shift+Tab,
  and arbitrary combinations of the same six physical dual-role positions.
- Events waiting behind unresolved decisions retain chronological order. A
  modifier used by a delayed semantic key remains latched until that target key
  is resolved, even if the physical modifier was released slightly earlier.
  Every concurrently active target is tracked separately, so releasing the
  first key in a fast arrow or letter roll cannot release the modifier from the
  keys that are still held.
- A physically released but still latched modifier can acquire the next target
  in an overlapping arrow rollover. Releasing the older arrow therefore cannot
  remove Alt/Shift/Cmd from the newer arrow.
- After a completed tap, pressing the same Delete/Backspace key again within
  120 ms immediately holds its navigation key down. This reproduces the original
  tap, tap-and-hold gesture and lets the host repeat deletion continuously.
  A single synthetic tap uses its own 12 ms pulse rather than borrowing the
  physical hold state, keeping every key-down paired with exactly one key-up.
  If the same key is pressed again during the 12 ms post-release grace, the
  preceding tap resolves immediately and the new press starts normally instead
  of being discarded.
- Alt can coexist with Cmd/Ctrl/Shift on either half, including macOS
  Alt+Cmd+Esc; existing modifiers never force a tap/hold result.
- When opposite arrows overlap on one axis, the old arrow is released before
  the new arrow is pressed. Its later physical release is suppressed, preventing
  ambiguous host-side Left+Right or Up+Down states.
- German remains the required host keyboard layout.

## Deliberate hardware normalization

Only the source Advantage 2 wiring defects `F1↔F2`, `F4↔F5`, and `F7↔F8`
are treated as accidental. No other unusual key placement is normalized.

The Advantage 360 has no dedicated Advantage 2 function strip, so those
physical F1–F12, brightness, and media positions have no direct target. They
are not silently relocated in this first firmware.

## Deliberately disabled

The eight physical positions that exist only on the Advantage 360 are `&none`
on every layer: positions 6, 7, 20, 21, 34, 39, 60, and 75.

## Custom code and why it exists

`src/behaviors/behavior_adv2_resolver.c` is the only custom runtime component.
Plain ZMK hold-tap can detect a hold, but the original QMK callbacks also
inspect arbitrary combinations of Shift, Alt, Ctrl, and GUI, temporarily
suppress selected modifiers, select an OS-specific sequence, and remap
Windows navigation. The resolver implements that compatibility logic, the
release-driven semantic letter handling, the parallel 90 ms modifier state
machine with balanced rolls and chord grace, Delete/Backspace repeat gesture,
and arrow serialization.

Hardware scanning, Bluetooth, USB, split communication, bootloader handling,
power management, and the Advantage 360 board definition remain Kinesis ZMK.

The queue capacities are deliberately tuned without changing split connection
timing: each matrix scan queue holds 32 events, the right peripheral holds 64
position snapshots, and the left central holds 64 received position events.
The pinned fork defaults to 4/10/5 respectively; under a fast right-half burst,
the ten-entry peripheral queue can block for up to 100 ms before discarding an
old snapshot, while the five-entry central queue can silently drop a new event.
The larger queues absorb temporary scheduler/BLE stalls while retaining the
fork's original connection interval, slave latency, and power behavior.

## Intentional implementation boundary

Only the layout's semantic behaviors are custom. Matrix scanning, split
transport, USB/Bluetooth HID, power management, and bootloader behavior remain
unchanged Kinesis/ZMK code.
