#!/usr/bin/env python3
"""Copy files to the Cardputer's SD card over USB serial (firmware debug console).

Usage: tools/sdsync.py <local_dir> <sd_dir> [port]
  e.g. tools/sdsync.py sdcard/gmtools /gmtools
"""
import base64
import sys
import time
from pathlib import Path

import serial

PORT = "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_50:78:7D:CE:86:78-if00"

def wait_for(s, want, timeout=10):
    end = time.time() + timeout
    buf = b""
    while time.time() < end:
        c = s.read(1)
        if not c:
            continue
        if c == b"\n":
            line = buf.decode(errors="replace").strip()
            buf = b""
            if line.startswith(want) or line.startswith("ERR"):
                return line
        else:
            buf += c
    return None

def put(s, local, remote):
    data = Path(local).read_bytes()
    s.reset_input_buffer()
    s.write(f"put {remote}\n".encode())
    r = wait_for(s, "READY")
    if r != "READY":
        raise SystemExit(f"{remote}: {r}")
    for i in range(0, len(data), 384):
        s.write(base64.b64encode(data[i:i + 384]) + b"\n")
        r = wait_for(s, "k")
        if r != "k":
            raise SystemExit(f"{remote}: {r} at byte {i}")
    s.write(b"end\n")
    r = wait_for(s, "OK")
    print(f"{remote}: {r}")

def main():
    src, dst = Path(sys.argv[1]), sys.argv[2].rstrip("/")
    port = sys.argv[3] if len(sys.argv) > 3 else PORT
    s = serial.Serial(port, 115200, timeout=0.2)
    time.sleep(0.2)
    files = [src] if src.is_file() else sorted(p for p in src.rglob("*") if p.is_file())
    t0 = time.time()
    for f in files:
        rel = f.name if src.is_file() else f.relative_to(src).as_posix()
        put(s, f, f"{dst}/{rel}")
    print(f"done in {time.time() - t0:.1f}s")

if __name__ == "__main__":
    main()
