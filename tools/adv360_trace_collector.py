#!/usr/bin/env python3
"""Local, explicit, <=60s collector for the scoped diagnostic firmware.

--self-test never opens hardware. --probe only requests INFO (does not arm).
Capture requires explicit serial ports verified as this keyboard beforehand.
No general serial transcript is saved: every exported record is allowlisted.
"""
import argparse
import datetime
import fcntl
import json
import os
import select
import signal
import struct
import termios
import time
import tty

POSITIONS = {35, 38, 52, 64, *range(66, 75)}
USAGES = {0x2A, 0x4C, *range(0x4F, 0x53), *range(0xE0, 0xE8)}
POSITION_STAGES = {1, 2, 3, 4, 9, 10}
USB_STAGES = {6, 7, 8}


def parse_record(line):
    parts = line.split()
    if len(parts) != 7 or parts[0] != "T":
        raise ValueError("Unexpected diagnostic record; raw text not retained")
    us, stage, code, down, a, b = map(int, parts[1:])
    if not (0 <= us <= 60_000_000 and down in (0, 1)):
        raise ValueError("Invalid diagnostic time/state")
    if not (-(2**31) <= a < 2**31 and -(2**31) <= b < 2**31):
        raise ValueError("Invalid diagnostic metadata")
    allowed = ((stage in POSITION_STAGES and code in POSITIONS) or
               (stage == 5 and code in USAGES) or
               (stage in USB_STAGES and 0 <= code <= 0x3FFF and down == 0))
    if not allowed:
        raise ValueError("Record rejected by key/stage allowlist")
    return dict(us=us, stage=stage, code=code, down=bool(down), a=a, b=b)


class Port:
    def __init__(self, path):
        if not path.startswith("/dev/cu.usbmodem"):
            raise ValueError("Only explicit macOS USB serial ports are accepted")
        self.path = path
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.buffer = b""
        self.role = None
        self.armed = False
        try:
            tty.setraw(self.fd)
            attrs = termios.tcgetattr(self.fd)
            attrs[4] = attrs[5] = termios.B115200
            attrs[2] |= termios.CLOCAL | termios.CREAD
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            fcntl.ioctl(self.fd, termios.TIOCMBIS, struct.pack("i", termios.TIOCM_DTR))
            self.send("INFO")
            parts = self.line(3).split()
            if len(parts) != 5 or parts[:2] != ["INFO", "1"] or parts[2] not in {"left", "right"}:
                raise ValueError("Device is not the expected diagnostic firmware")
            if parts[3:] != ["2048", "60000"]:
                raise ValueError("Unexpected trace limits")
            self.role = parts[2]
        except BaseException:
            self.close()
            raise

    def send(self, command):
        data = ("ADVTRACE " + command + "\n").encode("ascii")
        deadline = time.monotonic() + 2
        while data:
            if time.monotonic() >= deadline:
                raise TimeoutError("Serial command could not be sent")
            _, ready, _ = select.select([], [self.fd], [], 0.1)
            if ready:
                data = data[os.write(self.fd, data):]

    def line(self, timeout):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.buffer:
            if len(self.buffer) > 512:
                raise ValueError("Unexpected oversized frame; raw text not retained")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Diagnostic device did not reply")
            ready, _, _ = select.select([self.fd], [], [], remaining)
            if ready:
                chunk = os.read(self.fd, 4096)
                if not chunk:
                    raise OSError("Diagnostic device disconnected")
                self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        if len(line) > 512:
            raise ValueError("Unexpected oversized frame; raw text not retained")
        return line.decode("ascii").strip()

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None


def self_test():
    for p in range(128):
        try:
            parse_record(f"T 100 1 {p} 1 0 0")
            assert p in POSITIONS
        except ValueError:
            assert p not in POSITIONS
    for u in range(256):
        try:
            parse_record(f"T 100 5 {u} 1 8 0")
            assert u in USAGES
        except ValueError:
            assert u not in USAGES
    for invalid in ("typed text", "T 1 99 38 1 0 0", "T 60000001 1 38 1 0 0",
                    "T 1 7 65535 0 0 0", "T 1 5 4 1 0 0", "T 1 1 65 1 0 0"):
        try:
            parse_record(invalid)
        except ValueError:
            continue
        raise AssertionError("Unsafe record accepted")
    assert parse_record("T 100 7 264 0 0 33")["code"] == 264
    print("PASS host allowlists, bounded timestamps and malformed-record rejection; no hardware opened")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--self-test", action="store_true")
    modes.add_argument("--probe", action="store_true")
    modes.add_argument("--capture", metavar="NEW_NDJSON_PATH")
    parser.add_argument("--port", action="append", default=[])
    parser.add_argument("--seconds", type=int, default=60)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not 1 <= args.seconds <= 60 or not 1 <= len(args.port) <= 2:
        parser.error("Specify 1-2 verified keyboard ports and a duration of 1-60 seconds")
    ports = []
    out = None
    stop_requested = False

    def request_stop(signum, frame):
        nonlocal stop_requested
        stop_requested = True

    def save(record):
        out.write(json.dumps(record, separators=(",", ":")) + "\n")
        out.flush()

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)
    try:
        for path in args.port:
            port = Port(path)
            ports.append(port)
        if len({p.role for p in ports}) != len(ports):
            raise ValueError("Duplicate half; refusing to arm")
        if args.probe:
            for p in ports:
                print(json.dumps(dict(port=p.path, role=p.role, recorder_armed=False)))
            return
        out = os.fdopen(os.open(args.capture, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "w")
        save(dict(type="session", schema=1, started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  max_seconds=args.seconds, keys="navigation/modifiers/NUM only", clocks="independent per half"))
        deadline = time.monotonic() + args.seconds
        for p in ports:
            if stop_requested or time.monotonic() >= deadline:
                break
            before = time.monotonic_ns()
            p.send("START")
            p.armed = True  # Also STOP if the acknowledgement is lost.
            if p.line(3) != f"START 1 {p.role} 60000":
                raise ValueError("Capture start not acknowledged")
            save(dict(type="armed", role=p.role, before_ns=before, ack_ns=time.monotonic_ns()))
        print("Recording scoped firmware timings; automatic stop within 60 seconds.", flush=True)
        while not stop_requested and time.monotonic() < deadline:
            time.sleep(min(0.1, max(0, deadline - time.monotonic())))
        # Stop BOTH halves before any potentially slow dump/export.
        for p in ports:
            if p.armed:
                p.send("STOP")
        for p in ports:
            if not p.armed:
                continue
            if p.line(3) != "STOP":
                raise ValueError("Capture stop not acknowledged")
            p.armed = False
            p.send("DUMP")
            begin = p.line(5).split()
            if len(begin) != 5 or begin[:3] != ["BEGIN", "1", p.role]:
                raise ValueError("Unexpected dump header")
            count, dropped = map(int, begin[3:])
            if not (0 <= count <= 2048 and 0 <= dropped <= 1):
                raise ValueError("Unexpected dump size")
            last_us = 0
            for i in range(count):
                record = parse_record(p.line(5))
                if record["us"] < last_us:
                    raise ValueError("Non-monotonic trace")
                last_us = record["us"]
                save(dict(type="firmware_event", role=p.role, **record))
            if p.line(5) != "END":
                raise ValueError("Incomplete diagnostic dump")
            save(dict(type="half_complete", role=p.role, records=count, overflow=bool(dropped)))
        save(dict(type="complete", stopped_by_user=stop_requested))
        print("Capture stopped; filtered diagnostics saved locally.", flush=True)
    finally:
        for p in ports:
            if p.armed:
                try:
                    p.send("STOP")
                except (OSError, TimeoutError):
                    pass  # Firmware's independent 60s limit still applies.
            p.close()
        if out:
            out.close()


if __name__ == "__main__":
    main()
