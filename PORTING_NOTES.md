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
build jobs after `west update`. It sends one snapshot per invocation, paces
successful submissions by 4 ms, retries temporary TX backpressure after 1 ms,
and makes the full-queue fallback non-blocking. It does not count outstanding
notifications: a missing completion callback must not stop the split transport.
The patch does not change the
keymap, Space behavior, resolver semantics, tapping terms, BLE connection
interval, or peripheral latency.

## Local ZMK USB report-ownership patch

`patches/zmk-usb-hid-stable-buffer.patch` protects each submitted USB report
until the completion callback releases its semaphore. The pinned fork passes
the live HID state directly to `hid_int_ep_write()`. On the nrfx USB driver the
buffer is not copied before returning, so another press/release can change an
already submitted report before DMA transmits it. The semaphore gated the next
send, but not changes to the live keyboard state before that send.

This is the documented upstream bug
[zmkfirmware/zmk#2253](https://github.com/zmkfirmware/zmk/issues/2253), also
reported on an Advantage360. The fix follows the stable-buffer approach in
[#2257](https://github.com/zmkfirmware/zmk/pull/2257), with an additional check
that a semaphore timeout never permits reuse of an in-flight buffer. The
upstream PR was closed without merging. A local union covers the actual
keyboard, consumer, optional boot and pointing report types; oversized reports
are rejected rather than overflowing that storage. USB bus reset/disconnect
releases the semaphore after the controller aborts the old transfer; otherwise
a missing completion callback could block input after reconnecting the cable.

This changes USB transport ownership only. It adds no new key delay or output
queue and changes no resolver code, layout binding, 90/170 ms tapping term,
12 ms pulse, repeat window, Bluetooth control or split notification pacing.
Both CI variants and the Docker build apply the patch. Native tests compile
the actual send function with a deferred-read endpoint; the unpatched source
fails nine assertions, while the patched source preserves the report snapshots
and permits reconnection after an aborted transfer.
This reproduces the buffer defect, not proof that every observed physical
Backspace delay has the same cause. Real-device comparison remains required.

## Preserved intentionally

- macOS is the base profile; Windows is a sparse persistent overlay.
- Holding the right NUM thumb key momentarily activates the home-row numpad.
- While NUM is held, tapping the physical 360 `Esc` position toggles Windows.
- `&trans` inherits from the active profile; `&none` deliberately emits nothing.
- Letter holds and all modifier-aware symbol decisions mirror the QMK source.
- Windows and macOS use different host sequences for the same intended symbol.
- QMK timing is preserved at 90 ms for every dual-role modifier and 170 ms for
  letters. An unused Delete/Backspace hold is retro-tapped on release, so the
  90 ms Alt decision cannot swallow a solitary correction.
- The letter keys use a custom semantic hold engine rather than ZMK hold-taps.
  Like QMK Tap Dance, equal consecutive taps accumulate until another key
  interrupts the dance or the 170 ms term expires. An uninterrupted key that is
  still down at 170 ms gets its hold symbol. The captured modifier state follows
  a delayed dance even when its dual-role modifier is released slightly early.
  Plain text outputs stay pressed for one 12 ms HID report window; different
  keys run in parallel, while repeated equal outputs are queued with a report
  gap so double letters remain distinct. A following physical event is held
  behind that repeated sequence until its second down/up pair is complete, so
  Space or the next letter cannot overtake the second equal letter. That
  barrier watches only the repeated HID usage; unrelated in-flight letter
  pulses cannot postpone later input until typing becomes idle.
  Modifier-aware branches that use
  QMK's explicit `key_report(state->count, ...)` also retain that repeat count;
  this includes repeated punctuation such as `Alt + .` producing repeated `!`.
- Delete/Alt, Backspace/Alt, Tab/Shift, both Esc/Cmd-or-Ctrl keys, and
  Enter/Shift share one parallel 90 ms resolver. Each key owns its state, so
  simultaneous modifiers never accumulate serial delays.
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
- At 90 ms a held Delete/Backspace becomes Alt, matching QMK and allowing its
  modifier report to precede a following chord action. Releasing it without a
  following physical action removes Alt and retro-taps Delete/Backspace, so a
  slow or transport-delayed solitary correction is not swallowed. Before the
  90 ms decision, releasing the key first emits its navigation tap and promotes
  any older unresolved dual-role keys before that action (for example
  Cmd+Backspace).
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

Seven physical positions that exist only on the Advantage 360 remain `&none`
on the ordinary layers: positions 6, 20, 21, 34, 39, 60, and 75. Position 7 is
the original right-side Kinesis `Mod` key and now activates the dedicated
Bluetooth control layer.

## Sequential Bluetooth control

- Position 7 arms layer 3 for 1500 ms and produces no host key.
- The NUM momentary key passes through without consuming the control state.
- With NUM active, physical home-row positions `1`–`5` select ZMK Bluetooth
  profiles 0–4. The same positions without NUM cancel the state and retain
  their ordinary `A`, `S`, `D`, `T`, and `G` behavior.
- Either physical Delete/Alt or Backspace/Alt position clears only the selected
  Bluetooth profile.
- Every other press deactivates the Bluetooth layer and remains transparent to
  the unchanged lower-layer binding. A second Mod press and the timeout also
  cancel the state.
- This is implemented separately in
  `src/behaviors/behavior_adv360_bt_control.c`; it does not enter or alter the
  typing resolver's tap/hold, pulse, rollover, or modifier state machines.

## Custom code and why it exists

`src/behaviors/behavior_adv2_resolver.c` remains the custom typing component.
Plain ZMK hold-tap can detect a hold, but the original QMK callbacks also
inspect arbitrary combinations of Shift, Alt, Ctrl, and GUI, temporarily
suppress selected modifiers, select an OS-specific sequence, and remap
Windows navigation. The resolver implements that compatibility logic, the
release-driven semantic letter handling, the parallel 90 ms modifier state
machine with balanced rolls and chord grace, Delete/Backspace repeat gesture,
and arrow serialization.

`src/behaviors/behavior_adv360_bt_control.c` is a separate, small runtime
component for the timed Bluetooth layer described above. It calls ZMK's
standard selected-profile APIs; bonding storage and advertising remain ZMK.

Hardware scanning, bonding, bootloader handling, power management, and the
Advantage 360 board definition remain Kinesis ZMK. USB report ownership and
split notification backpressure have the narrow patches documented above.

The queue capacities are deliberately tuned without changing split connection
timing: each matrix scan queue holds 32 events, the right peripheral holds 64
position snapshots, and the left central holds 64 received position events.
The pinned fork defaults to 4/10/5 respectively; under a fast right-half burst,
the ten-entry peripheral queue can block for up to 100 ms before discarding an
old snapshot, while the five-entry central queue can silently drop a new event.
The larger queues absorb temporary scheduler/BLE stalls while retaining the
fork's original connection interval, slave latency, and power behavior.

## Intentional implementation boundary

Customizations are the semantic resolver, Bluetooth command selection, queue
capacities, explicit matrix debounce settings, and the two transport patches
documented above. The scan algorithm, radio connection parameters, HID descriptor,
power management, bonding and bootloader behavior remain unchanged.

## Matrix debounce compatibility (v0.3.11)

The pinned Kinesis ZMK fork defaults both `ZMK_KSCAN_DEBOUNCE_PRESS_MS` and
`ZMK_KSCAN_DEBOUNCE_RELEASE_MS` to 15. The actual trace1 build logs, run
39759034539, confirm 15/15 on both halves. The source Advantage 2 QMK board
sets `DEBOUNCE 5`; its global deferred algorithm is not identical to ZMK's
per-key integrator, so matching the number is not a claim of identical scanning.

This firmware explicitly sets both ZMK debounce thresholds to 5 ms. With the
real pinned debounce implementation and a 1 ms sample period, the old threshold
rejects short taps and merges successive presses separated by short releases.
The native regression exercises the implementation itself, including rejection
of brief noise. Both compiled configurations must contain 5/5 or CI fails.
Lowering the threshold reduces filtering of contact bounce as well as latency;
this is deliberately not zero-delay/eager scanning.

The 90 ms modifier and 170 ms semantic hold thresholds, all resolver code,
the keymap, and transport patches remain byte-for-byte unchanged from v0.3.10.
The correction affects both physical scanners, so both halves require updating.
This ordinary build contains no scoped-trace instrumentation or capture interface;
the temporary v0.3.10-trace1 source, release and local recordings remain archived.

The earlier finite trace showed five debounced Backspace presses and five USB
deliveries without a growing queue. It did not count raw switch attempts, because
its first hook runs after debouncing. The test therefore establishes a real
pre-resolver filtering path, not proof that it explains every reported hitch.

Additional audit found a separate USB timeout/suspend report-loss path and
overflow-only split queue loss cases. They are not mixed into this narrow change:
the recorded Backspaces had no such errors. Fault-injection evidence is retained
locally for subsequent transport work if needed.
