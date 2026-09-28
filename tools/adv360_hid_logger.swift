import Foundation
import IOKit.hid

private let adv360VendorID = 0x1d50
private let adv360ProductID = 0x615e
private let keyboardUsagePage = 0x07
private let consumerUsagePage = 0x0c

private var outputHandle: FileHandle?
private var startedAt = DispatchTime.now().uptimeNanoseconds
private var previousAt = startedAt

private let usageNames: [Int: String] = [
    0x04: "A", 0x05: "B", 0x06: "C", 0x07: "D", 0x08: "E", 0x09: "F",
    0x0a: "G", 0x0b: "H", 0x0c: "I", 0x0d: "J", 0x0e: "K", 0x0f: "L",
    0x10: "M", 0x11: "N", 0x12: "O", 0x13: "P", 0x14: "Q", 0x15: "R",
    0x16: "S", 0x17: "T", 0x18: "U", 0x19: "V", 0x1a: "W", 0x1b: "X",
    0x1c: "Y", 0x1d: "Z",
    0x1e: "1", 0x1f: "2", 0x20: "3", 0x21: "4", 0x22: "5",
    0x23: "6", 0x24: "7", 0x25: "8", 0x26: "9", 0x27: "0",
    0x28: "ENTER", 0x29: "ESC", 0x2a: "BACKSPACE", 0x2b: "TAB", 0x2c: "SPACE",
    0x2d: "MINUS", 0x2e: "EQUAL", 0x2f: "LEFT_BRACKET", 0x30: "RIGHT_BRACKET",
    0x31: "BACKSLASH", 0x33: "SEMICOLON", 0x34: "APOSTROPHE", 0x35: "GRAVE",
    0x36: "COMMA", 0x37: "DOT", 0x38: "SLASH", 0x4c: "DELETE",
    0x4f: "RIGHT", 0x50: "LEFT", 0x51: "DOWN", 0x52: "UP",
    0xe0: "LCTRL", 0xe1: "LSHIFT", 0xe2: "LALT", 0xe3: "LGUI",
    0xe4: "RCTRL", 0xe5: "RSHIFT", 0xe6: "RALT", 0xe7: "RGUI",
]

private func logLine(_ line: String) {
    let data = Data((line + "\n").utf8)
    outputHandle?.write(data)
    try? outputHandle?.synchronize()
    print(line)
    fflush(stdout)
}

private let inputCallback: IOHIDValueCallback = { _, _, _, value in
    let element = IOHIDValueGetElement(value)
    let page = Int(IOHIDElementGetUsagePage(element))
    guard page == keyboardUsagePage || page == consumerUsagePage else { return }

    let usage = Int(IOHIDElementGetUsage(element))
    let pressed = IOHIDValueGetIntegerValue(value) != 0
    let now = DispatchTime.now().uptimeNanoseconds
    let elapsedMs = Double(now - startedAt) / 1_000_000.0
    let deltaMs = Double(now - previousAt) / 1_000_000.0
    previousAt = now

    let name: String
    if page == keyboardUsagePage {
        name = usageNames[usage] ?? String(format: "KEY_0x%02x", usage)
    } else {
        name = String(format: "CONSUMER_0x%03x", usage)
    }

    logLine(String(
        format: "{\"t_ms\":%.3f,\"delta_ms\":%.3f,\"page\":%d,\"usage\":%d,\"key\":\"%@\",\"state\":\"%@\"}",
        elapsedMs, deltaMs, page, usage, name, pressed ? "down" : "up"
    ))
}

guard CommandLine.arguments.count == 2 else {
    fputs("Usage: adv360_hid_logger <output.ndjson>\n", stderr)
    exit(2)
}

let outputPath = CommandLine.arguments[1]
FileManager.default.createFile(atPath: outputPath, contents: nil)
guard let handle = FileHandle(forWritingAtPath: outputPath) else {
    fputs("Cannot open log file: \(outputPath)\n", stderr)
    exit(2)
}
outputHandle = handle

let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
let matching: [String: Any] = [
    kIOHIDVendorIDKey as String: adv360VendorID,
    kIOHIDProductIDKey as String: adv360ProductID,
    kIOHIDDeviceUsagePageKey as String: 0x01,
    kIOHIDDeviceUsageKey as String: 0x06,
]
IOHIDManagerSetDeviceMatching(manager, matching as CFDictionary)
IOHIDManagerRegisterInputValueCallback(manager, inputCallback, nil)
IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), CFRunLoopMode.defaultMode.rawValue)

let openResult = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))
guard openResult == kIOReturnSuccess else {
    fputs(String(format: "Cannot open HID manager: 0x%08x\n", openResult), stderr)
    exit(3)
}

let deviceCount = (IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice>)?.count ?? 0
guard deviceCount > 0 else {
    fputs("No matching herrsch-adv360 keyboard found.\n", stderr)
    exit(4)
}

startedAt = DispatchTime.now().uptimeNanoseconds
previousAt = startedAt
logLine("{\"event\":\"logger_started\",\"device\":\"herrsch-adv360\",\"vendor\":7504,\"product\":24926}")
CFRunLoopRun()
