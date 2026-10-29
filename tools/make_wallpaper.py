#!/usr/bin/env python3
"""Turns a picture into the Tab5 home screen wallpaper.

    tools/make_wallpaper.py picture.png [crop_top]

Crops it to 16:9 (from crop_top px down, default: centred), scales it to
1280x720 and writes assets/wallpaper.jpg and src/board/wallpaper_jpg.h.
A /wallpaper.jpg (1280x720) on the SD card replaces it without rebuilding.
"""
import io
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

im = Image.open(sys.argv[1]).convert('RGB')
w, h = im.size
ch = round(w * 9 / 16)
if ch <= h:
    top = int(sys.argv[2]) if len(sys.argv) > 2 else (h - ch) // 2
    im = im.crop((0, top, w, top + ch))
else:   # taller than wide enough: crop the sides
    cw = round(h * 16 / 9)
    im = im.crop(((w - cw) // 2, 0, (w - cw) // 2 + cw, h))
im = im.resize((1280, 720), Image.LANCZOS)

buf = io.BytesIO()
im.save(buf, 'JPEG', quality=90, subsampling=0, optimize=True)
data = buf.getvalue()
os.makedirs(os.path.join(ROOT, 'assets'), exist_ok=True)
open(os.path.join(ROOT, 'assets', 'wallpaper.jpg'), 'wb').write(data)
with open(os.path.join(ROOT, 'src', 'board', 'wallpaper_jpg.h'), 'w') as f:
    f.write('// The Tab5 home screen picture, 1280x720 JPEG. Made from assets/wallpaper.jpg\n'
            '// by tools/make_wallpaper.py; do not edit.\n#pragma once\n#include <stdint.h>\n\n')
    f.write('static const uint8_t WALLPAPER_JPG[%d] = {\n' % len(data))
    for i in range(0, len(data), 20):
        f.write('    ' + ','.join(str(x) for x in data[i:i + 20]) + ',\n')
    f.write('};\n')
print('%d bytes' % len(data))
