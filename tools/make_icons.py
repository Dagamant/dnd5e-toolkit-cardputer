#!/usr/bin/env python3
"""Draws the Tab5 home screen icons as 24x24 pixel art.

    tools/make_icons.py [preview.png]

Each icon is drawn with flat shapes, then outlined in one dark colour, and
written to src/core/icons_px.h as RGB565 (0xF81F = see-through). With a file
name it also saves an 8x preview sheet.
"""
import math
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
N = 24

OUT = (12, 14, 26)
WHITE = (240, 234, 214)
GOLD, GOLD_HI, GOLD_DK = (236, 160, 50), (255, 214, 120), (150, 84, 24)
RED, RED_HI, RED_DK = (206, 58, 42), (248, 120, 80), (112, 24, 20)
SILVER, SILVER_HI, SILVER_DK = (176, 184, 204), (226, 232, 244), (98, 106, 130)
BROWN, BROWN_HI, BROWN_DK = (150, 94, 48), (192, 132, 72), (88, 52, 28)
PARCH, PARCH_DK = (236, 214, 164), (190, 156, 104)
GREEN, GREEN_HI, GREEN_DK = (110, 186, 70), (170, 226, 110), (44, 104, 48)
BLUE, BLUE_HI, BLUE_DK = (80, 140, 228), (150, 200, 250), (36, 66, 140)
VIOLET, VIOLET_HI, VIOLET_DK = (146, 104, 226), (196, 168, 255), (72, 44, 136)
YELLOW = (255, 232, 110)
INK = (70, 56, 50)
DARK = (34, 40, 62)


def new():
    im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def poly(d, pts, c):
    d.polygon(pts, fill=c)


def line(d, a, b, c, w=1):
    d.line([a, b], fill=c, width=w)


def rect(d, x0, y0, x1, y1, c):
    d.rectangle([x0, y0, x1, y1], fill=c)


def ell(d, x0, y0, x1, y1, c):
    d.ellipse([x0, y0, x1, y1], fill=c)


def px(im, x, y, c):
    if 0 <= x < N and 0 <= y < N:
        im.putpixel((x, y), c + (255,))


def outline(im):
    """A one-pixel dark outline around everything drawn."""
    src = im.copy()
    for y in range(N):
        for x in range(N):
            if src.getpixel((x, y))[3]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < N and 0 <= yy < N and src.getpixel((xx, yy))[3]:
                    im.putpixel((x, y), OUT + (255,))
                    break
    return im


# ---------------------------------------------------------------- the icons

def dice():
    im, d = new()
    hexa = [(12, 1), (21, 6), (21, 17), (12, 22), (3, 17), (3, 6)]
    poly(d, hexa, GOLD)
    poly(d, [(12, 1), (3, 6), (7, 8)], GOLD_HI)               # lit top-left facet
    poly(d, [(21, 17), (12, 22), (17, 16)], GOLD_DK)          # shaded bottom-right
    tri = [(12, 6), (17, 16), (7, 16)]
    poly(d, tri, GOLD_HI)
    for a, b in ((hexa[0], tri[0]), (hexa[1], tri[1]), (hexa[2], tri[1]), (hexa[3], tri[1]), (hexa[3], tri[2]),
                 (hexa[4], tri[2]), (hexa[5], tri[2]), (hexa[5], tri[0]), (hexa[1], tri[0])):
        line(d, a, b, GOLD_DK)
    d.line(tri + [tri[0]], fill=GOLD_DK)
    return outline(im)


def sword(d, im, flip):
    def P(x, y):
        return (23 - x, y) if flip else (x, y)
    # blade from top corner to the guard
    for i in range(3, 16):
        px(im, *P(i, i), SILVER_HI)
        px(im, *P(i + 1, i), SILVER)
        px(im, *P(i, i + 1), SILVER_DK)
    px(im, *P(2, 2), SILVER_HI)
    # guard across, grip, pommel
    for k in range(-3, 4):
        px(im, *P(16 + k, 16 - k), GOLD)
        px(im, *P(17 + k, 16 - k), GOLD_DK)
    for i in range(17, 20):
        px(im, *P(i, i), BROWN)
        px(im, *P(i + 1, i), BROWN_DK)
    for x, y in ((20, 20), (21, 20), (20, 21), (21, 21)):
        px(im, *P(x, y), GOLD)


def initiative():
    im, d = new()
    sword(d, im, False)
    sword(d, im, True)
    ell(d, 10, 9, 13, 12, RED)
    px(im, 11, 10, RED_HI)
    return outline(im)


def characters():
    im, d = new()
    poly(d, [(11, 0), (14, 0), (16, 3), (13, 6), (11, 6)], RED)       # plume
    px(im, 12, 1, RED_HI)
    px(im, 13, 2, RED_HI)
    ell(d, 4, 4, 19, 20, SILVER)
    rect(d, 4, 12, 19, 21, SILVER)
    ell(d, 6, 5, 11, 10, SILVER_HI)
    rect(d, 15, 12, 19, 21, SILVER_DK)
    rect(d, 6, 11, 17, 12, DARK)                                     # visor slit
    rect(d, 11, 11, 12, 18, SILVER_DK)                               # nose guard
    for y in (15, 17, 19):
        px(im, 7, y, DARK)
        px(im, 16, y, DARK)
    line(d, (4, 21), (19, 21), SILVER_DK)
    return outline(im)


def notes():
    im, d = new()
    poly(d, [(3, 4), (16, 4), (16, 22), (3, 22)], PARCH)
    rect(d, 14, 4, 16, 22, PARCH_DK)
    for y in (8, 11, 14, 17):
        line(d, (5, y), (12, y), INK)
    line(d, (5, 20), (9, 20), INK)
    # quill
    poly(d, [(22, 0), (23, 2), (13, 15), (11, 15), (11, 13)], WHITE)
    line(d, (21, 2), (12, 13), SILVER)
    line(d, (12, 14), (10, 18), GOLD_DK, 1)
    px(im, 10, 18, INK)
    return outline(im)


def campaign():
    im, d = new()
    poly(d, [(1, 6), (11, 4), (11, 21), (1, 21)], BROWN_DK)          # covers
    poly(d, [(22, 6), (12, 4), (12, 21), (22, 21)], BROWN_DK)
    poly(d, [(2, 5), (11, 3), (11, 19), (2, 20)], PARCH)
    poly(d, [(21, 5), (12, 3), (12, 19), (21, 20)], PARCH)
    poly(d, [(12, 3), (13, 3), (13, 19), (12, 19)], PARCH_DK)
    for y in (8, 11, 14):
        line(d, (4, y), (9, y - 1), INK)
        line(d, (14, y - 1), (19, y), INK)
    poly(d, [(16, 3), (18, 3), (18, 23), (17, 21), (16, 23)], RED)    # ribbon
    return outline(im)


def encounters():
    im, d = new()
    for x0, h in ((1, 15), (8, 20), (15, 17)):                       # pines
        top = 22 - h
        poly(d, [(x0 + 4, top), (x0 + 8, 22), (x0, 22)], GREEN_DK)
        poly(d, [(x0 + 4, top), (x0 + 5, top + 3), (x0 + 3, top + 3)], GREEN)
    ell(d, 4, 15, 19, 23, DARK)                                      # the bush
    for x in (8, 14):                                                # eyes
        px(im, x, 18, YELLOW)
        px(im, x + 1, 18, YELLOW)
        px(im, x, 19, RED)
        px(im, x + 1, 19, RED)
    px(im, 20, 3, YELLOW)                                            # a star
    return outline(im)


def monsters():
    im, d = new()
    poly(d, [(4, 9), (0, 0), (8, 6)], WHITE)                         # horns
    poly(d, [(19, 9), (23, 0), (15, 6)], WHITE)
    px(im, 1, 1, PARCH_DK)
    px(im, 22, 1, PARCH_DK)
    ell(d, 3, 4, 20, 21, RED)
    ell(d, 5, 5, 11, 10, RED_HI)
    rect(d, 13, 12, 20, 18, RED_DK)
    poly(d, [(6, 9), (10, 11), (6, 12)], YELLOW)                     # eyes
    poly(d, [(17, 9), (13, 11), (17, 12)], YELLOW)
    rect(d, 7, 15, 16, 18, DARK)                                     # mouth
    for x in (8, 11, 14):
        px(im, x, 15, WHITE)
        px(im, x + 1, 18, WHITE)
    return outline(im)


def dungeon():
    im, d = new()
    ell(d, 2, 1, 21, 20, SILVER_DK)                                  # arch
    rect(d, 2, 10, 21, 22, SILVER_DK)
    for y in range(3, 22, 4):                                        # mortar, only on the stone
        for x in range(N):
            if im.getpixel((x, y))[3]:
                px(im, x, y, DARK)
    for y in range(4, 22, 8):
        for x in (4, 19):
            for k in range(3):
                if im.getpixel((x, y + k))[3]:
                    px(im, x, y + k, DARK)
    ell(d, 6, 5, 17, 16, BROWN)                                      # door
    rect(d, 6, 11, 17, 22, BROWN)
    for x in (9, 12, 15):
        line(d, (x, 7), (x, 22), BROWN_DK)
    line(d, (6, 12), (17, 12), SILVER)
    line(d, (6, 18), (17, 18), SILVER)
    px(im, 14, 15, GOLD)
    return outline(im)


def dungeon23():
    im, d = new()
    rect(d, 1, 5, 22, 22, DARK)                                      # the dark below
    for i in range(5):                                               # steps going down
        x0, y0 = 1 + i * 4, 8 + i * 3
        rect(d, x0, y0, x0 + 6, y0 + 2, SILVER if i % 2 else SILVER_HI)
        line(d, (x0, y0 + 2), (x0 + 6, y0 + 2), SILVER_DK)
    rect(d, 13, 1, 22, 9, PARCH)                                     # a calendar page
    rect(d, 13, 1, 22, 3, RED)
    for x in (15, 18, 21):
        px(im, x, 6, INK)
        px(im, x - 1, 8, INK)
    return outline(im)


def tables():
    im, d = new()
    rect(d, 4, 3, 19, 20, PARCH)                                     # sheet
    ell(d, 2, 1, 6, 5, PARCH_DK)                                     # rolled ends
    ell(d, 17, 18, 21, 22, PARCH_DK)
    rect(d, 4, 1, 19, 3, PARCH_DK)
    rect(d, 4, 20, 19, 22, PARCH_DK)
    for y in (7, 10, 13, 16):
        line(d, (6, y), (8, y), RED)
        line(d, (10, y), (17, y), INK)
    line(d, (9, 6), (9, 17), PARCH_DK)
    # a little d6
    rect(d, 15, 13, 21, 19, WHITE)
    px(im, 16, 14, RED_DK)
    px(im, 18, 16, RED_DK)
    px(im, 20, 18, RED_DK)
    return outline(im)


def travel():
    im, d = new()
    ell(d, 1, 1, 22, 22, BROWN_DK)                                   # compass
    ell(d, 3, 3, 20, 20, PARCH)
    for a in range(0, 360, 45):
        r = 8 if a % 90 == 0 else 6
        x, y = 11.5 + r * math.sin(math.radians(a)), 11.5 - r * math.cos(math.radians(a))
        px(im, int(round(x)), int(round(y)), PARCH_DK)
    poly(d, [(12, 3), (14, 11), (10, 11)], RED)                      # needle north
    poly(d, [(12, 20), (14, 12), (10, 12)], SILVER)
    px(im, 12, 4, RED_HI)
    ell(d, 11, 10, 13, 12, GOLD)
    return outline(im)


def shop():
    im, d = new()
    poly(d, [(8, 6), (15, 6), (19, 12), (20, 19), (16, 22), (6, 22), (2, 19), (3, 12)], BROWN)   # sack
    poly(d, [(15, 6), (19, 12), (20, 19), (16, 22), (14, 22), (17, 15)], BROWN_DK)
    rect(d, 7, 3, 16, 6, BROWN_HI)                                   # neck
    line(d, (6, 7), (17, 7), GOLD)                                   # string
    ell(d, 8, 12, 14, 18, GOLD)                                      # coin mark
    px(im, 11, 15, GOLD_DK)
    for i, y in enumerate((20, 18, 16)):                             # coin stack
        rect(d, 16, y, 22, y + 2, GOLD)
        line(d, (16, y + 2), (22, y + 2), GOLD_DK)
    return outline(im)


def spells():
    im, d = new()
    rect(d, 3, 2, 19, 21, VIOLET_DK)                                 # book
    rect(d, 5, 2, 19, 20, VIOLET)
    rect(d, 17, 3, 20, 21, PARCH)                                    # pages
    line(d, (18, 3), (18, 21), PARCH_DK)
    ell(d, 7, 6, 16, 15, VIOLET_HI)                                  # rune circle
    ell(d, 8, 7, 15, 14, VIOLET)
    poly(d, [(11, 7), (15, 13), (8, 13)], GOLD)
    poly(d, [(11, 9), (13, 12), (10, 12)], VIOLET)
    for x, y in ((21, 1), (22, 4)):                                  # sparkles
        px(im, x, y, YELLOW)
    return outline(im)


def items():
    im, d = new()
    ell(d, 2, 4, 21, 13, BROWN)                                      # lid
    rect(d, 2, 9, 21, 21, BROWN)
    rect(d, 2, 11, 21, 12, BROWN_DK)
    for x in (5, 18):                                                # bands
        rect(d, x, 5, x + 1, 21, GOLD)
    line(d, (2, 21), (21, 21), BROWN_DK)
    rect(d, 10, 10, 13, 15, GOLD)                                    # lock
    px(im, 11, 13, OUT)
    ell(d, 5, 5, 9, 8, BROWN_HI)
    return outline(im)


def rules():
    im, d = new()
    rect(d, 11, 3, 12, 20, GOLD)                                     # post
    rect(d, 7, 20, 16, 22, GOLD_DK)                                  # base
    line(d, (3, 6), (20, 6), GOLD)                                   # beam
    ell(d, 10, 1, 13, 4, GOLD_HI)
    for cx in (4, 19):
        line(d, (cx, 6), (cx - 3, 13), GOLD_DK)
        line(d, (cx, 6), (cx + 3, 13), GOLD_DK)
        poly(d, [(cx - 4, 13), (cx + 4, 13), (cx + 2, 16), (cx - 2, 16)], BLUE)
        line(d, (cx - 3, 14), (cx + 2, 14), BLUE_HI)
    return outline(im)


def hazards():
    im, d = new()
    rect(d, 9, 1, 14, 6, SILVER_DK)                                  # neck
    rect(d, 8, 0, 15, 2, BROWN)                                      # cork
    ell(d, 2, 5, 21, 23, GREEN_DK)                                   # flask
    ell(d, 3, 9, 20, 22, GREEN)
    ell(d, 5, 7, 9, 11, GREEN_HI)
    ell(d, 7, 11, 16, 18, WHITE)                                     # skull
    rect(d, 9, 17, 14, 19, WHITE)
    px(im, 9, 14, OUT); px(im, 10, 14, OUT)
    px(im, 13, 14, OUT); px(im, 14, 14, OUT)
    px(im, 11, 18, OUT); px(im, 12, 16, OUT)
    for x, y in ((18, 8), (20, 4), (17, 2)):                         # bubbles
        px(im, x, y, GREEN_HI)
    return outline(im)


def archive():
    im, d = new()
    for y, c in ((3, PARCH), (5, WHITE), (7, PARCH)):                # papers
        rect(d, 4 + (y % 3), y, 18 + (y % 3), y + 6, c)
    rect(d, 1, 10, 22, 22, BROWN)                                    # box
    rect(d, 1, 10, 22, 12, BROWN_HI)
    rect(d, 17, 12, 22, 22, BROWN_DK)
    rect(d, 8, 15, 15, 18, BROWN_DK)                                 # handle
    rect(d, 9, 16, 14, 17, OUT)
    return outline(im)


def web():
    im, d = new()
    ell(d, 2, 2, 21, 21, BLUE)                                       # globe
    ell(d, 4, 4, 10, 9, BLUE_HI)
    poly(d, [(7, 7), (12, 6), (13, 10), (9, 14), (6, 12)], GREEN)    # land
    poly(d, [(14, 14), (18, 11), (19, 16), (15, 19)], GREEN)
    d.arc([2, 2, 21, 21], 0, 360, fill=BLUE_DK)
    d.arc([7, 2, 16, 21], 90, 270, fill=BLUE_DK)
    line(d, (3, 11), (20, 11), BLUE_DK)
    d.arc([0, 9, 20, 17], 160, 380, fill=GOLD)                       # a ring around it
    return outline(im)


def foundry():
    im, d = new()
    poly(d, [(2, 9), (21, 9), (21, 12), (17, 14), (15, 18), (8, 18), (6, 14), (2, 12)], SILVER_DK)  # anvil
    rect(d, 2, 9, 21, 10, SILVER)
    poly(d, [(0, 9), (3, 9), (3, 11)], SILVER_DK)                    # horn
    rect(d, 5, 18, 18, 21, DARK)                                     # base
    rect(d, 9, 3, 19, 6, SILVER)                                     # hammer head
    rect(d, 16, 3, 19, 6, SILVER_DK)
    line(d, (11, 6), (7, 8), BROWN, 2)                               # handle
    for x, y, c in ((4, 6, YELLOW), (6, 4, GOLD), (3, 3, RED_HI), (21, 7, GOLD), (8, 2, YELLOW)):
        px(im, x, y, c)
    return outline(im)


def settings():
    im, d = new()
    for a in range(0, 360, 45):                                      # teeth
        x, y = 11.5 + 9 * math.cos(math.radians(a)), 11.5 + 9 * math.sin(math.radians(a))
        rect(d, int(round(x)) - 2, int(round(y)) - 2, int(round(x)) + 1, int(round(y)) + 1, SILVER)
    ell(d, 4, 4, 19, 19, SILVER)
    ell(d, 5, 5, 12, 12, SILVER_HI)
    d.arc([4, 4, 19, 19], 0, 90, fill=SILVER_DK)
    ell(d, 8, 8, 15, 15, SILVER_DK)
    ell(d, 9, 9, 14, 14, DARK)
    return outline(im)


ICONS = [("Dice", dice), ("Initiative", initiative), ("Characters", characters), ("Notes", notes), ("Dungeon23", dungeon23),
         ("Campaign", campaign), ("Encounters", encounters), ("Monsters", monsters), ("5-Room Dungeon", dungeon),
         ("Random Tables", tables), ("Travel", travel), ("Magic Shop", shop), ("Spells", spells), ("Items", items),
         ("Rules", rules), ("Hazards", hazards), ("Archive", archive), ("Web", web), ("Foundry", foundry),
         ("Settings", settings)]


def rgb565(c):
    r, g, b = c[:3]
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main():
    imgs = [(name, fn()) for name, fn in ICONS]
    with open(os.path.join(ROOT, 'src', 'core', 'icons_px.h'), 'w') as f:
        f.write('// Home screen icons: 24x24 pixel art, RGB565, 0xF81F = see-through.\n'
                '// Made by tools/make_icons.py; do not edit.\n#pragma once\n#include <stdint.h>\n\n'
                'namespace iconpx {\n\nconstexpr int SIZE = %d;\nconstexpr uint16_t CLEAR = 0xF81F;\n\n'
                'struct Icon {\n    const char* app;\n    uint16_t px[SIZE * SIZE];\n};\n\n'
                'static const Icon ICONS[] = {\n' % N)
        for name, im in imgs:
            vals = [rgb565(p) if p[3] else 0xF81F for p in im.getdata()]
            f.write('    {"%s", {\n' % name)
            for i in range(0, len(vals), N):
                f.write('        ' + ','.join('0x%04X' % v for v in vals[i:i + N]) + ',\n')
            f.write('    }},\n')
        f.write('};\n\n}  // namespace iconpx\n')
    if len(sys.argv) > 1:
        S, cols = 8, 7
        rows = (len(imgs) + cols - 1) // cols
        sheet = Image.new('RGB', (cols * (N * S + 16), rows * (N * S + 16)), (20, 30, 52))
        for i, (_, im) in enumerate(imgs):
            big = im.resize((N * S, N * S), Image.NEAREST)
            sheet.paste(big, ((i % cols) * (N * S + 16) + 8, (i // cols) * (N * S + 16) + 8), big)
        sheet.save(sys.argv[1])


main()
