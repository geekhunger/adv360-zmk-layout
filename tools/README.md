# Advantage 360 diagnostic tools

## `adv360_hid_logger.swift`

A deliberately device-scoped macOS HID event logger for timing diagnostics.
It matches only `herrsch-adv360` (`VendorID 0x1d50`, `ProductID 0x615e`) and
records HID usage, key-down/key-up state, elapsed time, and inter-event delay as
NDJSON. It does not match other keyboards.

Build:

```sh
swiftc -module-cache-path /private/tmp/adv360-swift-module-cache \
  tools/adv360_hid_logger.swift \
  -o /private/tmp/adv360_hid_logger
```

Run with an explicit output file:

```sh
/private/tmp/adv360_hid_logger /private/tmp/adv360-hid-session.ndjson
```

macOS requires the launching application to be enabled under **System
Settings → Privacy & Security → Input Monitoring**. Stop the logger with
`Ctrl+C`. Diagnostic logs can contain typed key sequences and must not be
published without reviewing them first.
