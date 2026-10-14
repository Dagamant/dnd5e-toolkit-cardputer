#!/usr/bin/env python3
"""Drive the device through its serial debug console.

    tools/dev.py "key 7|wait:1|type abol|key enter|shot:aboleth"

Commands are separated by | (or given as separate arguments):
  any console command    key enter, type text, mem, perf, ls /gmtools, ...
  wait:SEC               pause
  shot:NAME[:SCALE]      save the screen as NAME.png in the current directory
                         (SCALE 2 = half size; uses the compressed format)
  shotraw:NAME           the same with the older uncompressed format
The port is the first /dev/serial/by-id device, or $DEV_PORT.
"""
import sys, time, serial, struct, zlib, os
OUT = os.getcwd()
PORT = os.environ.get('DEV_PORT') or os.path.join('/dev/serial/by-id', sorted(os.listdir('/dev/serial/by-id'))[0])
s = serial.Serial(PORT, 115200, timeout=0.2)
time.sleep(0.2); s.reset_input_buffer()

def png(path, w, h, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                            + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))

def readline(timeout=3):
    end = time.time() + timeout; buf = b''
    while time.time() < end:
        c = s.read(1)
        if not c: continue
        if c == b'\n': return buf.decode(errors='replace')
        buf += c
    return None

args = [c for a in sys.argv[1:] for c in a.split("|")]
for cmd in args:
    if cmd.startswith('wait:'): time.sleep(float(cmd[5:])); continue
    if cmd.startswith('shot:'):
        # shot:name[:scale] - run-length encoded (debug console "shot <scale>")
        parts = cmd[5:].split(':'); name = parts[0]; scale = int(parts[1]) if len(parts) > 1 else 1
        s.reset_input_buffer(); s.write(f'shot {scale}\n'.encode())
        line = readline()
        while line is not None and not line.startswith('SHOTRLE'): line = readline()
        if line is None: print('NO RESPONSE (hung?)'); sys.exit(1)
        _, w, h = line.split(); w, h = int(w), int(h); rows = []
        for _ in range(h):
            l = (readline() or '').strip(); px = []
            for i in range(0, len(l), 6):
                n = int(l[i:i+2], 16); v = int(l[i+2:i+6], 16)
                px += [(v >> 8) & 0xf8, (v >> 3) & 0xfc, (v << 3) & 0xf8] * n
            rows.append(px)
        p = os.path.join(OUT, name + '.png'); png(p, w, h, rows); print('saved', p); continue
    if cmd.startswith('shotraw:'):
        s.reset_input_buffer(); s.write(b'shot\n')
        line = readline()
        while line is not None and not line.startswith('SHOT'): line = readline()
        if line is None: print('NO RESPONSE (hung?)'); sys.exit(1)
        _, w, h = line.split(); w, h = int(w), int(h); rows = []
        for _ in range(h):
            l = readline(); px = []
            for i in range(w):
                v = int(l[i*4:i*4+4], 16)
                px += [(v >> 8) & 0xf8, (v >> 3) & 0xfc, (v << 3) & 0xf8]
            rows.append(px)
        p = os.path.join(OUT, cmd[8:] + '.png'); png(p, w, h, rows); print('saved', p); continue
    s.write((cmd + '\n').encode()); r = readline(2); print(cmd, '->', r)
    time.sleep(0.15)
