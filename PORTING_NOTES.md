# Advantage 2 → Advantage 360 porting notes

This repository ports geekhunger's long-used Advantage 2 QMK layout to an
unmodified Kinesis Advantage 360 Pro running ZMK.

## Preserved intentionally

- macOS is the base profile; Windows is a sparse persistent overlay.
- Holding the right NUM thumb key momentarily activates the home-row numpad.
- While NUM is held, tapping the physical 360 `Esc` position toggles Windows.
- `&trans` inherits from the active profile; `&none` deliberately emits nothing.
- Letter holds and all modifier-aware symbol decisions mirror the QMK source.
- Windows and macOS use different host sequences for the same intended symbol.
- QMK timing is preserved: 90 ms for modifier tap/hold and 170 ms for letters.
- The letter keys use a custom QMK-compatible Tap-Dance engine rather than ZMK
  hold-taps: 170 ms starts the hold action, another physical key resolves the
  pending key immediately as a tap, and another press of the same key increases
  its tap count. Rapid double letters are therefore emitted twice without the
  global hold-tap event backlog.
- Delete and Backspace use independent 90 ms mod-taps. Tap versus Alt is never
  inferred from keyboard side, opposite-hand use, or already-held modifiers.
- The first unresolved mod-tap owns chronological event order, as in QMK.
  Following position events wait until that key is released as a tap or reaches
  90 ms as Alt. `IGNORE_MOD_TAP_INTERRUPT` is therefore preserved instead of
  turning another key press into a hold decision.
- After a completed tap, pressing the same Delete/Backspace key again within
  90 ms immediately holds its navigation key down. This reproduces the original
  tap, tap-and-hold gesture and lets the host repeat deletion continuously.
- Alt can coexist with Cmd/Ctrl/Shift on either half, including macOS
  Alt+Cmd+Esc; existing modifiers never force a tap/hold result.
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
QMK-style letter Tap-Dance counter, and the chronological 90 ms modifier state
machine.

Hardware scanning, Bluetooth, USB, split communication, bootloader handling,
power management, and the Advantage 360 board definition remain Kinesis ZMK.

## Intentional implementation boundary

Only the layout's semantic behaviors are custom. Matrix scanning, split
transport, USB/Bluetooth HID, power management, and bootloader behavior remain
unchanged Kinesis/ZMK code.
