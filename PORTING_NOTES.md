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
- QMK's `IGNORE_MOD_TAP_INTERRUPT` is represented by ZMK's `tap-preferred`
  decision rule for modifier tap-holds.
- QMK's 90 ms sequential-tap window is represented by `quick-tap-ms = <90>`,
  so tap, then press-and-hold repeats Backspace/Delete instead of becoming Alt.
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
Windows navigation. The resolver implements only that compatibility logic.

Hardware scanning, Bluetooth, USB, split communication, bootloader handling,
power management, and the Advantage 360 board definition remain Kinesis ZMK.

## One unavoidable timing-model difference

QMK Tap Dance batches a rapid multi-tap count and then emits that many copies.
ZMK hold-tap resolves each released tap individually. A single tap, a long
hold, modifier combinations, and repeated manual tapping have the same intended
result, but the exact delivery timing of very rapid repeated taps can differ.
