#!/usr/bin/env python3
"""Extract D&D SRD 5.2.1 data from the PDF into SD-card data files.

Usage:  python3 tools/srd_extract.py SRD_CC_v5.2.1.pdf sdcard/gmtools/srd

Output format (all files): records separated by a blank line, one "key: value"
per line. Long text fields keep paragraph breaks as the two characters "\\n".
An .idx file next to each data file lists "offset<TAB>name<TAB>..." so the
device can list and search without loading every description.

This work includes material from the System Reference Document 5.2.1
("SRD 5.2.1") by Wizards of the Coast LLC, available at
https://www.dndbeyond.com/srd. The SRD 5.2.1 is licensed under the Creative
Commons Attribution 4.0 International License.
"""
import re
import sys
from pathlib import Path

import pymupdf  # pip install pymupdf  (see tools/.venv)
from srd_habitats import habitat
import srd_tables

DOC = None

class Run:
    """A piece of text in one font. kind: name|head|label|body|bold|bolditalic|italic|table|para"""
    __slots__ = ("kind", "text", "font", "size", "x", "y", "page", "x1")
    def __init__(self, kind, text, font="", size=0, x=0, y=0, page=0, x1=0):
        self.kind, self.text, self.font, self.size, self.x, self.y, self.page = kind, text, font, size, x, y, page
        self.x1 = x1   # right edge
    def __repr__(self):
        return f"{self.kind}:{self.text!r}"

def classify(font, size):
    if "GillSans" in font:
        if size >= 11.5: return "name"
        if "SemiBold" in font or "Bold" in font: return "label"
        return "table"
    if "BoldItalic" in font: return "bolditalic"
    if "Bold" in font: return "bold"
    if "Italic" in font: return "italic"
    return "body"

def runs(first, last, tables=True):
    """Font-tagged text for a page range in reading order (left column, then right).
    With tables, each table is rebuilt from its layout (srd_tables) and comes back
    as one "tbl" run holding its text form."""
    out = []
    for pno in range(first - 1, last):
        page = DOC[pno]
        mid = page.rect.width / 2
        blocks = [b for b in page.get_text("dict")["blocks"] if b.get("lines") and b["bbox"][1] < 735]
        if tables:
            done, extra = set(), []
            for ids in srd_tables.group_tables(blocks, mid):
                title, rows, head = srd_tables.build_table(blocks, ids)
                if not rows: continue
                done.update(ids)
                x0 = min(blocks[i]["bbox"][0] for i in ids); y0 = min(blocks[i]["bbox"][1] for i in ids)
                x1 = max(blocks[i]["bbox"][2] for i in ids)
                # a table spanning both columns reads with the left one
                extra.append({"bbox": (x0 if x1 > mid + 5 or x0 < mid - 10 else x0, y0, x1, y0),
                              "tbl": srd_tables.serialize(title, rows, head), "lines": []})
            blocks = [b for i, b in enumerate(blocks) if i not in done] + extra
        # a paragraph's box can start a few px above the table row printed before it
        # (Confusion, Augury); nudge body text down so the row comes first
        def body(b):
            return "tbl" not in b and not any("GillSans" in sp["font"] for ln in b["lines"] for sp in ln["spans"] if sp["text"].strip())
        blocks.sort(key=lambda b: (b["bbox"][0] >= mid - 10, b["bbox"][1] + (5 if body(b) else 0)))
        for b in blocks:
            out.append(Run("para", "", page=pno + 1))
            if "tbl" in b:
                out.append(Run("tbl", b["tbl"], x=b["bbox"][0], y=b["bbox"][1], page=pno + 1))
                out.append(Run("para", "", page=pno + 1))
                continue
            for line in b["lines"]:
                first_span = True
                for sp in line["spans"]:
                    t = sp["text"]
                    if t.strip() == "" and "\t" in t:
                        out.append(Run("para", "", page=pno + 1)); continue
                    if not t: continue
                    # small-caps headings ("Acid Splash") come as alternating large/small
                    # pieces; join them into one run at the large size
                    if "SC700" in sp["font"] and not first_span and out[-1].font == sp["font"]:
                        out[-1].text += t
                        out[-1].x1 = sp["bbox"][2]
                        if sp["size"] > out[-1].size:
                            out[-1].size = sp["size"]; out[-1].kind = classify(sp["font"], sp["size"])
                        continue
                    k = classify(sp["font"], sp["size"])
                    out.append(Run(k, t, sp["font"], sp["size"], sp["bbox"][0], sp["bbox"][1], pno + 1, sp["bbox"][2]))
                    if first_span:
                        out[-1].x = sp["bbox"][0]
                    first_span = False
                out.append(Run("eol", "", page=pno + 1))
    return out

def flatten(rs):
    """Join runs into text: 'eol' joins lines (removing soft hyphens), 'para' -> newline.
    Table cells (GillSans runs) are separated by ' | ', table rows by newlines."""
    txt = ""
    held = []       # rebuilt tables, put back after the whitespace clean-up
    prev_kind = None
    prev_y = -1
    prev_x1 = 0
    row_x = 0       # left edge of the current table row
    saw_eol = False
    for r in rs:
        if r.kind == "eol":
            saw_eol = True
            continue
        if r.kind == "para":
            if txt and not txt.endswith("\n"): txt += "\n"
            prev_kind = None
            continue
        if r.kind == "tbl":
            if txt and not txt.endswith("\n"): txt += "\n"
            txt += f"\x00{len(held)}\x00\n"
            held.append(r.text)
            prev_kind = None
            continue
        t = r.text
        tab = r.kind in ("table", "label")
        if tab and prev_kind == "tab" and txt and not txt.endswith("\n"):
            if abs(r.y - prev_y) < 3:
                # pieces of one cell (a bold label and its text) touch; cells have a gap
                if r.x - prev_x1 < 6 and r.x1 > 0:
                    if r.x - prev_x1 > 1.5 and not txt.endswith(" ") and not t.startswith(" "): txt += " "
                else:
                    txt += " | "
            elif r.x > row_x + 12 and r.x1 > 0 and r.y - prev_y < 16:
                # a cell that wraps onto the next line (indented past the row's first column)
                if not (txt.endswith("-") and t[:1].islower()) and not txt.endswith(" "): txt += " "
            else:
                txt += "\n"
                row_x = r.x
        elif tab and r.x1 > 0 and not (prev_kind == "tab" and not txt.endswith("\n")):
            row_x = r.x
        elif not tab and prev_kind == "tab" and txt and not txt.endswith("\n"):
            txt += "\n"
        if txt.endswith("-") and t[:1].islower() and not txt.endswith(" -"):
            txt = txt[:-1]  # hyphenated line break: "descrip-" + "tion"
        txt += t
        prev_kind = "tab" if tab else "text"
        prev_y = r.y
        prev_x1 = r.x1
        saw_eol = False
    txt = re.sub(r"[ \t]+", " ", txt)
    txt = re.sub(r" *\n *", "\n", txt)
    txt = re.sub(r"( \| )+", " | ", txt)
    txt = re.sub(r"(?<=\S) *\u2022\s*", "\n\u2022 ", txt)   # each bullet on its own line
    txt = re.sub(r"\u2022[ \t]+", "\u2022 ", txt)
    txt = re.sub("\x00(\\d+)\x00", lambda m: held[int(m.group(1))], txt)
    return fix_charge_tables(fix_abilities(txt.strip()))

CHARGE_HEAD = re.compile(r"Spell(?: \| |\n+)Charge(?:\n+Spell\n+Charge)?(?: |\n+)Cost(?:\n+Cost)?\n")

def fix_charge_tables(txt):
    """Staffs' and wands' Spell / Charge Cost tables: the heading comes out split over several
    lines, and some print two spell/cost pairs per row. One header, one spell per row."""
    pos, last_end = 0, -1
    while True:
        m = CHARGE_HEAD.search(txt, pos)
        if not m: return txt
        lines = txt[m.end():].split("\n")
        rows, i = [], 0
        while i < len(lines) and " | " in lines[i]:
            line = lines[i]; i += 1
            while line.count("(") > line.count(")") and i < len(lines):   # a cell that wrapped
                line += " " + lines[i]; i += 1
            cells = [c.strip() for c in line.split(" | ")]
            rows += [" | ".join(cells[k:k + 2]) for k in range(0, len(cells), 2)]
        if not rows: pos = m.end(); continue
        before = txt[:m.start()]
        if last_end >= 0 and not before[last_end:].strip():   # the table continues in the next column
            before, head = before[:last_end], "\n"
        else:
            head = "Spell | Charge Cost\n"
        body = head + "\n".join(rows)
        txt = before + body + ("\n" + "\n".join(lines[i:]) if i < len(lines) else "")
        last_end = pos = len(before) + len(body)

ABIL_ROW = re.compile(r"\b(Str|Dex|Con|Int|Wis|Cha)\b[\s|]*(\d+)[\s|]*([+\-\u2212]\d+)[\s|]*([+\-\u2212]\d+)")

def fix_abilities(txt):
    """A stat block's six-column ability grid ('MOD | | SAVE | MOD ... Str | 16 | | +3 | +3 | | Dex ...')
    becomes one table row per ability."""
    while True:
        h = re.search(r"MOD[\s|]*SAVE(?:[\s|]*MOD[\s|]*SAVE){2}", txt)
        if not h: return txt
        pos, rows = h.end(), []
        for want in ("Str", "Dex", "Con", "Int", "Wis", "Cha"):
            m = ABIL_ROW.search(txt, pos)
            if not m or m.group(1) != want or txt[pos:m.start()].strip(" |\n"): break
            rows.append(" | ".join(m.groups()))
            pos = m.end()
        if len(rows) != 6: return txt
        rest = txt[pos:].lstrip(" |")
        txt = txt[:h.start()].rstrip(" ") + ("\n" if txt[:h.start()].strip() and not txt[:h.start()].endswith("\n") else "") + \
            "Ability | Score | Mod | Save\n" + "\n".join(rows) + "\n" + rest.lstrip("\n ")

SPELL_HEAD = re.compile(r"^(?:Level (\d) ([A-Z][a-z]+)|([A-Z][a-z]+) Cantrip) \((.*)$")
SCHOOLS = {"Abjuration", "Conjuration", "Divination", "Enchantment", "Evocation",
           "Illusion", "Necromancy", "Transmutation"}
ABILITIES = ["Strength", "Dexterity", "Constitution", "Intelligence", "Wisdom", "Charisma"]
DAMAGE_TYPES = ["Acid", "Bludgeoning", "Cold", "Fire", "Force", "Lightning", "Necrotic",
                "Piercing", "Poison", "Psychic", "Radiant", "Slashing", "Thunder"]

SP_DICE = r"\d+d\d+(?: ?\+ ?\d+d\d+)?(?: ?\+ ?\d+(?!d))?"
SP_MOD = r"(?: plus your spellcasting ability modifier)?"

def spell_damage(text):
    """(dice, type) for a spell's main damage roll, or None. Handles the SRD's phrasings:
    '8d6 Fire damage', '10d6 + 40 Force damage', 'Force damage equal to 1d8 plus your
    spellcasting ability modifier', '3d8 damage of the chosen type', '5d10 Radiant or
    Necrotic damage', 'base damage is 12d6', 'an extra 2d8 damage'."""
    types = "|".join(DAMAGE_TYPES)
    def clean(d):
        return d.replace(" plus your spellcasting ability modifier", "+MOD").replace(" ", "")
    m = re.search(rf"({SP_DICE}{SP_MOD})\s+({types}) damage", text)
    if m: return clean(m.group(1)), m.group(2)
    m = re.search(rf"({types}) damage equal to ({SP_DICE}{SP_MOD})", text)
    if m: return clean(m.group(2)), m.group(1)
    m = re.search(rf"({SP_DICE}) ((?:{types})(?:,? (?:or )?(?:{types}))+) damage", text)
    if m:
        kinds = re.findall(types, m.group(2))
        return clean(m.group(1)), "/".join(kinds) if len(kinds) == 2 else "Chosen"
    m = re.search(rf"({SP_DICE}) damage of (?:the chosen type|a type you choose|the spirit['\u2019]s type|the type)", text)
    if m: return clean(m.group(1)), "Chosen"
    m = re.search(rf"base damage is ({SP_DICE})", text)
    if m:
        t = re.search(rf"({types}) damage", text)
        return clean(m.group(1)), t.group(1) if t else ""
    m = re.search(rf"extra ({SP_DICE}) damage", text)
    if m: return clean(m.group(1)), "Extra"
    return None

def spell_rolls(text):
    """Pull the main roll out of a spell description for one-tap rolling."""
    r = {}
    if re.search(r"(melee|ranged) spell attack", text):
        r["attack"] = re.search(r"(melee|ranged) spell attack", text).group(1)
    m = re.search(r"(" + "|".join(ABILITIES) + r") saving throw", text)
    if m:
        r["save"] = m.group(1)
    dmg = spell_damage(text)
    if dmg:
        r["damage"], r["dtype"] = dmg
    else:
        m = re.search(r"regains? (?:a number of )?Hit Points equal to (\d+d\d+)( plus your spellcasting ability modifier)?", text)
        if m:
            r["heal"] = m.group(1) + ("+MOD" if m.group(2) else "")
    return r

def write_db(path, records, index_fields):
    """records: list of dicts (ordered). index_fields: keys copied into the .idx."""
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray()
    idx = []
    for r in records:
        off = len(data)
        block = "".join(f"{k}: {v}\n" for k, v in r.items() if v not in (None, ""))
        data += block.encode("utf-8") + b"\n"
        idx.append("\t".join([str(off), r["name"]] + [str(r.get(k, "")) for k in index_fields]))
    path.write_bytes(bytes(data))
    path.with_suffix(".idx").write_text("\n".join(idx) + "\n", encoding="utf-8")
    print(f"{path.name}: {len(records)} records, {len(data)//1024} KB")

def ascii(s):
    # the device fonts are ASCII; normalise typography
    s = (s.replace("\u2019", "'").replace("\u2018", "'").replace("\u201c", '"').replace("\u201d", '"')
          .replace("\u2013", "-").replace("\u2014", " - ").replace("\u00d7", "x").replace("\u2022", "*")
          .replace("\u2026", "...").replace("\u00a0", " ").replace("\u00bd", "1/2").replace("\u00bc", "1/4").replace("\u2212", "-").replace("\u00ad", ""))
    return s.encode("ascii", "replace").decode().replace("?", "?")

def split_entries(rs, is_start):
    """Group runs into entries; a new entry starts at runs where is_start(run)."""
    entries, cur = [], None
    for r in rs:
        if is_start(r):
            cur = [r]; entries.append(cur)
        elif cur is not None:
            cur.append(r)
    return entries

STAT_LABELS = ("AC", "HP", "Speed", "Skills", "Resistances", "Vulnerabilities", "Immunities", "Gear", "Senses",
               "Languages", "CR")

def stat_lines(txt):
    """Put each stat block entry (AC, HP, Speed, Senses, ...) on its own line; the PDF runs
    them together ("AC 15HP 10", "PoisonedSenses"). Only the part before the first section."""
    m = re.search(r"\n(TRAITS|ACTIONS|BONUS ACTIONS|REACTIONS)\n", txt)
    head, tail = (txt[:m.start()], txt[m.start():]) if m else (txt, "")
    head = re.sub(r"(?<=[^\n])(?<!\| )\s*\b(" + "|".join(STAT_LABELS) + r") (?=[\dA-Z+(-])", r"\n\1 ", head)
    head = re.sub(r"(?<=[a-z0-9).])(" + "|".join(STAT_LABELS) + r") (?=[\dA-Z+(-])", r"\n\1 ", head)
    # rejoin values that wrapped: a line that doesn't start an entry continues the one above
    starts = re.compile(r"^(" + "|".join(STAT_LABELS) + r"|Ability \||(Str|Dex|Con|Int|Wis|Cha) \|) ")
    lines = []
    for i, l in enumerate(head.split("\n")):
        if i >= 3 and lines and l.strip() and not starts.match(l) and starts.match(lines[-1]) and \
                not lines[-1].startswith(("Ability", "Str", "Dex", "Con", "Int", "Wis", "Cha")):
            lines[-1] += " " + l.strip()
        else:
            lines.append(l)
    head = "\n".join(lines)
    return head + tail

# Tables whose PDF layout (headings over two lines) can't be rebuilt by flatten().
TELEPORT_TABLE = """Teleportation Outcome
Familiarity | Mishap | Similar Area | Off Target | On Target
Permanent circle | - | - | - | 01-00
Linked object | - | - | - | 01-00
Very familiar | 01-05 | 06-13 | 14-24 | 25-00
Seen casually | 01-33 | 34-43 | 44-53 | 54-00
Viewed once or described | 01-43 | 44-53 | 54-73 | 74-00
False destination | 01-50 | 51-00 | - | -"""

CONTROL_WEATHER_TABLES = """Temperature
Stage | Condition
1 | Heat wave
2 | Hot
3 | Warm
4 | Cool
5 | Cold
6 | Freezing
Wind
Stage | Condition
1 | Calm
2 | Moderate wind
3 | Strong wind
4 | Gale
5 | Storm"""

def fix_spell_tables(name, desc):
    if name == "Control Weather":   # the Temperature and Wind tables are printed side by side
        new = re.sub(r"(?:Wind\n)?Temperature\n(?:Wind\n)?Stage \| Condition[^\n]*\n(?:\d \|[^\n]*(?:\n|$))+",
                     CONTROL_WEATHER_TABLES + "\n", desc).rstrip("\n")
        if new == desc: print("  warning: Control Weather tables not replaced", file=sys.stderr)
        return new
    if name == "Teleport":
        new = re.sub(r"Teleportation Outcome\n.*?False destination[^\n]*", TELEPORT_TABLE, desc, flags=re.S)
        if new == desc or "Similar\nOff" in desc and "Similar\nOff" in new:
            print("  warning: Teleport table not replaced", file=sys.stderr)
        return new
    return desc

def extract_spells():
    rs = runs(107, 175)
    head_re = re.compile(r"^(Level \d [A-Z][a-z]+|[A-Z][a-z]+ Cantrip) \(")
    starts = set()
    for i, r in enumerate(rs):
        if r.kind != "name": continue
        j = i + 1
        while j < len(rs) and rs[j].kind in ("eol", "para", "name"): j += 1
        if j < len(rs) and rs[j].kind == "italic" and head_re.match(rs[j].text.strip()):
            if not (i > 0 and rs[i - 1].kind == "name"):  # second line of a wrapped name
                starts.add(id(r))
    entries = split_entries(rs, lambda r: id(r) in starts)
    spells = []
    for e in entries:
        name = e[0].text.strip()
        i = 1
        # merge a name that wraps across two name runs
        while i < len(e) and e[i].kind in ("name", "eol"):
            if e[i].kind == "name": name += " " + e[i].text.strip()
            i += 1
        head = ""
        while i < len(e) and e[i].kind in ("italic", "eol", "para"):
            if e[i].kind == "italic": head += e[i].text
            i += 1
        head = re.sub(r"\s+", " ", head).strip()
        m = re.match(r"^(?:Level (\d) ([A-Z][a-z]+)|([A-Z][a-z]+) Cantrip) \((.*)\)$", head)
        if not m:
            print("  skip (no spell header):", name, "|", head[:50], file=sys.stderr); continue
        level = int(m.group(1)) if m.group(1) else 0
        school = m.group(2) or m.group(3)
        classes = m.group(4)
        fields, key = {}, None
        FIELD = ("Casting Time", "Range", "Components", "Component", "Duration")
        body, higher, creature = [], [], []
        target = body
        in_fields = True
        for r in e[i:]:
            t = r.text.strip()
            if in_fields:
                if r.kind not in ("eol", "para") and not t: continue   # stray blank piece
                if r.kind in ("label", "bold") and t.rstrip(":") in FIELD:
                    key = "Components" if t.startswith("Component") else t.rstrip(":")
                    fields[key] = ""; continue
                if r.kind in ("eol", "para"):
                    if r.kind == "para" and "Duration" in fields: in_fields = False
                    continue
                if r.kind in ("table", "body") and key:
                    # value text; Components may wrap onto the next line
                    if key == "Duration" and fields[key] and r.kind == "body" and fields[key].strip():
                        in_fields = False
                    else:
                        v = fields[key]
                        if v.endswith("-") and r.text[:1].islower(): fields[key] = v[:-1] + r.text
                        else: fields[key] = v + ("" if v.endswith(" ") or r.text.startswith(" ") or not v else " ") + r.text
                        continue
                else:
                    in_fields = False
            if r.kind == "name":   # a stat block inside the spell (e.g. a summoned creature)
                target = creature
                target.append(Run("para", "")); target.append(Run("body", r.text.strip().upper())); target.append(Run("para", "")); continue
            if r.kind == "bolditalic" and t.startswith(("Using a Higher-Level Spell Slot", "Cantrip Upgrade")):
                target = higher
            target.append(r)
        desc = flatten(body)
        ct = re.sub(r"\s+", " ", fields.get("Casting Time", "")).strip()
        dur = re.sub(r"\s+", " ", fields.get("Duration", "")).strip()
        rec = {
            "name": ascii(name), "level": level, "school": school, "classes": classes,
            "time": ascii(ct.replace(" or Ritual", "")), "ritual": 1 if "Ritual" in ct else "",
            "range": ascii(fields.get("Range", "").strip()),
            "components": ascii(re.sub(r"\s+", " ", fields.get("Components", "")).strip()),
            "duration": ascii(dur), "conc": 1 if dur.startswith("Concentration") else "",
        }
        desc = fix_spell_tables(rec["name"], desc)
        rec.update(spell_rolls(desc.replace("\n", " ")))
        rec["text"] = ascii(desc).replace("\n", "\\n")
        if higher: rec["higher"] = ascii(flatten(higher)).replace("\n", "\\n")
        if creature: rec["creature"] = ascii(stat_lines(flatten(creature))).replace("\n", "\\n")
        spells.append(rec)
    return spells

# ---------------------------------------------------------------- tables
def grid(pno, colx, x0=0, x1=9999, y0=0, y1=735):
    """Rows of a positioned table. colx = [(key, x_start), ...] left to right.
    Returns [(cells dict, is_heading)] where is_heading marks bold/italic group rows."""
    page = DOC[pno - 1]
    spans = []
    for b in page.get_text("dict")["blocks"]:
        for l in b.get("lines", []):
            for sp in l["spans"]:
                x, y = sp["bbox"][0], sp["bbox"][1]
                if sp["text"].strip() and x0 <= x < x1 and y0 <= y < y1:
                    spans.append((round(y), x, sp["text"], sp["font"]))
    rows = {}
    for y, x, t, f in spans:
        key = next((y2 for y2 in rows if abs(y2 - y) <= 3), y)
        rows.setdefault(key, []).append((x, t, f))
    out = []
    for y in sorted(rows):
        cells, fonts = {}, set()
        for x, t, f in sorted(rows[y]):
            col = colx[0][0]
            for k, cx in colx:
                if x >= cx - 6: col = k
            cells[col] = (cells.get(col, "") + t)
            fonts.add(f)
        cells = {k: ascii(re.sub(r"\s+", " ", v).strip()) for k, v in cells.items()}
        heading = len(cells) == 1 and any("Italic" in f or "SemiBold" in f for f in fonts)
        out.append((cells, heading))
    return out

def merge_continuations(rows, key_col):
    """Rows without a value in key_col continue the previous row's cells."""
    out = []
    for cells, heading in rows:
        if not heading and key_col not in cells and out:
            prev = out[-1][0]
            for k, v in cells.items():
                prev[k] = (prev.get(k, "") + " " + v).strip()
        else:
            out.append((cells, heading))
    return out

def dash(v):
    return "" if v in ("-", " - ", "--") or v.strip() == "-" else v

def extract_weapons():
    cols = [("name", 50), ("damage", 135), ("props", 213), ("mastery", 428), ("weight", 470), ("cost", 505)]
    rows = merge_continuations(grid(91, cols, y0=50), "damage")
    out, cat = [], ""
    for cells, heading in rows:
        if heading or "damage" not in cells:
            cat = cells.get("name", cat); continue
        if cells.get("name") in ("Name",): continue
        dmg = cells["damage"].split(" ", 1)
        props = dash(cells.get("props", ""))
        r = {"name": cells["name"], "kind": "weapon",
             "category": cat.replace(" Weapons", ""),  # e.g. "Simple Melee"
             "damage": dmg[0], "dtype": dmg[1] if len(dmg) > 1 else "",
             "props": props, "mastery": cells.get("mastery", ""),
             "weight": dash(cells.get("weight", "")), "cost": cells.get("cost", "")}
        m = re.search(r"Versatile \((\d+d\d+)\)", props)
        if m: r["versatile"] = m.group(1)
        m = re.search(r"Range (\d+/\d+)", props)
        if m: r["range"] = m.group(1)
        out.append(r)
    return out

def extract_armor():
    cols = [("name", 60), ("ac", 185), ("str", 312), ("stealth", 378), ("weight", 455), ("cost", 495)]
    rows = grid(92, cols, y0=345, y1=620)
    out, cat = [], ""
    for cells, heading in rows:
        if heading:
            cat = cells.get("name", "").split(" (")[0].replace(" Armor", ""); continue
        if "ac" not in cells: continue
        ac = cells["ac"]
        m = re.match(r"(\d+)", ac.replace("+", ""))
        r = {"name": cells["name"], "kind": "armor" if cat != "Shield" else "shield",
             "category": cat, "ac": ac, "ac_base": m.group(1) if m else "",
             "dex": "max2" if "max 2" in ac else ("yes" if "Dex" in ac else "no"),
             "str": dash(cells.get("str", "")).replace("Str ", ""),
             "stealth": "disadvantage" if "Disadvantage" in cells.get("stealth", "") else "",
             "weight": cells.get("weight", ""), "cost": cells.get("cost", "")}
        out.append(r)
    return out

def entries_by_name(first, last, name_pred=lambda r: True):
    """Entries that start at a 12pt GillSans name run (wrapped names merged)."""
    rs = runs(first, last)
    ents = []
    i = 0
    while i < len(rs):
        r = rs[i]
        if r.kind == "name" and r.size < 14 and name_pred(r):
            name = r.text
            j = i + 1
            while j < len(rs) and rs[j].kind in ("eol", "name") and (rs[j].kind == "eol" or rs[j].size < 14):
                if rs[j].kind == "name": name += " " + rs[j].text
                j += 1
            ents.append([re.sub(r"\s+", " ", name).strip(), []])
            i = j
            continue
        if ents:
            ents[-1][1].append(r)
        i += 1
    return ents

def extract_gear():
    # two side-by-side tables on p95 (and a continuation of the right one on p96? no)
    items = {}
    for (x0, x1, cols) in [(60, 300, [("name", 60), ("weight", 200), ("cost", 255)]),
                           (300, 600, [("name", 315), ("weight", 455), ("cost", 505)])]:
        for cells, heading in grid(95, cols, x0=x0, x1=x1, y0=50):
            n = cells.get("name", "")
            if heading or n in ("Item", "") or "cost" not in cells: continue
            items[n] = {"name": n, "kind": "gear", "weight": dash(cells.get("weight", "")), "cost": cells["cost"]}
    # descriptions: "Name (cost)" headings on pp95-100
    descs = {}
    for name, body in entries_by_name(94, 100):
        m = re.match(r"^(.*?) \((.*?)\)$", name)
        if not m: continue
        descs[ascii(m.group(1))] = ascii(flatten(body))
    for n, it in items.items():
        d = descs.get(n)
        if d is None:
            # "Lantern, Bullseye" style names vs headings like "Lantern, Bullseye"
            d = next((v for k, v in descs.items() if k.lower() == n.lower()), "")
        if d:
            it["text"] = d.replace("\n", "\\n")
            m = re.search(r"(\d+d\d+(?: ?\+ ?\d+)?) (" + "|".join(DAMAGE_TYPES) + r") damage", d)
            if m: it["damage"] = m.group(1).replace(" ", ""); it["dtype"] = m.group(2)
            m = re.search(r"regains? (\d+d\d+(?: ?\+ ?\d+)?) Hit Points", d)
            if m: it["heal"] = m.group(1).replace(" ", "")
    return sorted(items.values(), key=lambda r: r["name"])

def extract_tools():
    out = []
    LABELS = {"Ability": "ability", "Weight": "weight", "Utilize": "utilize", "Craft": "craft", "Variants": "variants"}
    for name, body in entries_by_name(93, 94):
        m = re.match(r"^(.*?) \((.*?)\)$", name)
        if not m: continue
        fields, key = {}, None
        for r in body:
            t = r.text.strip()
            if r.kind == "label" and t.rstrip(":") in LABELS:
                key = LABELS[t.rstrip(":")]; fields[key] = ""; continue
            if r.kind == "table" and key:
                fields[key] += r.text; continue
            if r.kind in ("eol", "para"):
                continue
            if r.kind not in ("table", "label"):
                key = None
                if fields: break
        if "ability" not in fields: continue  # adventuring gear descriptions also start on p94
        r = {"name": ascii(m.group(1)), "kind": "tool", "cost": m.group(2)}
        for k, v in fields.items():
            v = re.sub(r"\s+", " ", v).strip()
            v = re.sub(r"([a-z])- ?([a-z])", r"\1\2", v)  # "Lad- der"
            r[k] = ascii(v)
        out.append(r)
    return out

ITEM_TYPE = re.compile(r"^(Armor|Potion|Ring|Rod|Scroll|Staff|Wand|Weapon|Wondrous Item)\b")
RARITIES = ["Common", "Uncommon", "Rare", "Very Rare", "Legendary", "Artifact"]

def extract_magic_items():
    out = []
    for name, body in entries_by_name(209, 253):
        # the italic type line may be anywhere near the top due to block order
        type_line, rest, taken = "", [], False
        for r in body:
            if not taken and r.kind == "italic" and (type_line or ITEM_TYPE.match(r.text.strip())):
                type_line += r.text
                continue
            if type_line and r.kind not in ("italic", "eol", "para"):
                taken = True
            rest.append(r)
        type_line = ascii(re.sub(r"\s+", " ", type_line)).strip()
        if not type_line:
            # a stat block (Traits/Actions...) belonging to the previous item
            if out:
                out[-1]["text"] += "\\n" + ascii(name).upper() + "\\n" + ascii(flatten(body)).replace("\n", "\\n")
            continue
        rar = next((x for x in reversed(RARITIES) if x.lower() in type_line.lower()), "")
        if "Varies" in type_line: rar = "Varies"
        att = re.search(r"\(Requires Attunement(?: by ([^)]*))?\)", type_line)
        txt = ascii(flatten(rest))
        r = {"name": ascii(name), "kind": "magic", "type": type_line.split(",")[0].split(" (")[0],
             "typeline": type_line, "rarity": rar,
             "attune": (att.group(1) or "yes") if att else ""}
        m = re.search(r"\+(\d) bonus to attack (?:rolls )?and damage rolls", txt)
        if m: r["bonus"] = m.group(1)
        m = re.search(r"\+(\d) bonus to (?:its )?(?:Armor Class|AC)", txt)
        if m: r["acbonus"] = m.group(1)
        m = re.search(r"has (\d+) charges", txt)
        if m: r["charges"] = m.group(1)
        m = re.search(r"regains? (?:a number of )?(?:Hit Points equal to )?(\d+d\d+(?: ?\+ ?\d+)?)(?: Hit Points)?", txt)
        if m and "Potion" in type_line: r["heal"] = m.group(1).replace(" ", "")
        m = re.search(r"(\d+d\d+) (" + "|".join(DAMAGE_TYPES) + r") damage", txt)
        if m: r["damage"] = m.group(1); r["dtype"] = m.group(2)
        r["text"] = txt.replace("\n", "\\n")
        out.extend(split_variants(r, txt))
    return out

def split_variants(r, txt):
    """Turn combined entries into the concrete items a character carries."""
    m = re.match(r"^(.*), \+1, \+2, or \+3$", r["name"])
    if m:
        base = m.group(1)
        res = []
        for n in (1, 2, 3):
            v = dict(r)
            v["name"] = f"{base} +{n}"
            rm = re.search(r"(" + "|".join(RARITIES) + r") \(\+" + str(n) + r"\)", r["typeline"])
            v["rarity"] = rm.group(1) if rm else r["rarity"]
            if base in ("Armor", "Shield"): v["acbonus"] = str(n)
            elif base.startswith("Wand"): v["spellbonus"] = str(n)
            else: v["bonus"] = str(n)
            res.append(v)
        return res
    if r["name"] == "Potions of Healing":
        res = []
        for label, dice, rar in re.findall(r"Potion of Healing[\s|]*(?:\((\w+)\))?[\s|]*(\d+d\d+ \+ \d+)[\s|]*(" + "|".join(RARITIES) + ")", txt.replace("\n", " ")):
            v = dict(r)
            v["name"] = f"Potion of {label.capitalize()} Healing" if label else "Potion of Healing"
            v["rarity"] = rar
            v["heal"] = dice.replace(" ", "")
            v["typeline"] = f"Potion, {rar}"
            res.append(v)
        if len(res) == 4:
            return res
        print("  warning: healing potion table not parsed", file=sys.stderr)
    return [r]

# ---------------------------------------------------------------- classes
CLASS_NAMES = ["Barbarian", "Bard", "Cleric", "Druid", "Fighter", "Monk", "Paladin", "Ranger",
               "Rogue", "Sorcerer", "Warlock", "Wizard"]
SKILLS = ["Acrobatics", "Animal Handling", "Arcana", "Athletics", "Deception", "History", "Insight",
          "Intimidation", "Investigation", "Medicine", "Nature", "Perception", "Performance",
          "Persuasion", "Religion", "Sleight of Hand", "Stealth", "Survival"]

def page_spans(pno):
    out = []
    for b in DOC[pno - 1].get_text("dict")["blocks"]:
        for l in b.get("lines", []):
            for sp in l["spans"]:
                if sp["text"].strip():
                    out.append((sp["bbox"][0], sp["bbox"][1], sp["bbox"][2], sp["text"], sp["font"], sp["size"]))
    return out

def features_table(cls):
    """Find '<Class> Features' table and return (headers, rows[level] -> list of cells)."""
    for pno in range(28, 83):
        sp = page_spans(pno)
        title = [s for s in sp if s[3].strip() == f"{cls} Features" and s[5] < 11]
        if not title: continue
        tx, ty = title[0][0], title[0][1]
        col_lo, col_hi = (tx - 10, 600) if tx < 200 else (tx - 10, 600)
        region = [s for s in sp if s[1] > ty + 5 and s[0] >= col_lo - 5]
        hdr = [s for s in region if "SemiBold" in s[4] and s[1] < ty + 45]
        right = max(s[2] for s in hdr) + 12 if hdr else 600
        region = [s for s in region if s[0] < right and "GillSans" in s[4]]
        # data rows: first cell is the level number at the left edge of the table
        lvl_rows = {}
        for s in region:
            if re.fullmatch(r"\d{1,2}", s[3].strip()) and s[0] < tx + 30 and "SemiBold" not in s[4]:
                lv = int(s[3]);
                if 1 <= lv <= 20 and lv not in lvl_rows: lvl_rows[lv] = s[1]
        if len(lvl_rows) < 20: continue
        first_y = lvl_rows[1]
        last_y = lvl_rows[20]
        # columns from the level-1 row
        row1 = sorted([s for s in region if abs(s[1] - first_y) < 3], key=lambda s: s[0])
        colx = [s[0] for s in row1]
        def col_of(x):
            return min(range(len(colx)), key=lambda i: abs(colx[i] - x) if x >= colx[i] - 12 else 999)
        heads = [""] * len(colx)
        for s in sorted([s for s in region if s[1] < first_y - 2 and "SemiBold" in s[4]], key=lambda s: (s[1], s[0])):
            # header text is often centred over its column: use its centre
            cx = (s[0] + s[2]) / 2
            i = min(range(len(colx)), key=lambda i: abs(colx[i] + 8 - cx))
            if i == 0 and abs(s[0] - colx[0]) > 40: i = col_of(s[0])
            heads[i] = (heads[i] + " " + s[3].strip()).strip()
        rows = {}
        ys = sorted(lvl_rows.items(), key=lambda kv: kv[1])
        for idx, (lv, y) in enumerate(ys):
            ny = ys[idx + 1][1] if idx + 1 < len(ys) else last_y + 14
            cells = [""] * len(colx)
            for s in region:
                if y - 3 <= s[1] < ny - 3:
                    cells[col_of(s[0])] = (cells[col_of(s[0])] + " " + s[3].strip()).strip()
            rows[lv] = [ascii(c).replace("—", "-") for c in cells]
        heads = [re.sub(r"\s*\u2014?\s*Spell Slots per Spell Level\s*\u2014?\s*", " ", h).strip() for h in heads]
        heads = [re.sub(r"^[\u2014-]+\s*|\s*[\u2014-]+$", "", h).strip() for h in heads]
        heads = [re.sub(r"^[\s-]+(?=\d+$)", "", ascii(h)) for h in heads]   # "-  5" -> "5"
        return heads, rows, pno
    return None, None, None

def parse_equipment(txt):
    """'(A) Greataxe, 4 Handaxes, Explorer's Pack, and 15 GP; or (B) 75 GP' -> {'A': [...], 'B': [...]}"""
    opts = {}
    for letter, body in re.findall(r"\(([A-C])\)\s*(.*?)(?=;?\s*(?:or\s*)?\([A-C]\)|$)", txt):
        parts = re.split(r",\s*(?:and\s+)?|\s+and\s+", body.strip().rstrip(";").strip())
        opts[letter] = [p.strip() for p in parts if p.strip()]
    return opts

def extract_classes():
    rs = runs(28, 82)
    raw = runs(28, 82, tables=False)   # the Core Traits label/value pairs
    raw_groups, cur = {}, None
    for r in raw:
        if r.kind == "name" and r.size >= 17 and r.text.strip() in CLASS_NAMES:
            cur = r.text.strip(); raw_groups[cur] = []; continue
        if cur: raw_groups[cur].append(r)
    # split at 18pt class headings
    groups, cur = {}, None
    for r in rs:
        if r.kind == "name" and r.size >= 17 and r.text.strip() in CLASS_NAMES:
            cur = r.text.strip(); groups[cur] = []; continue
        if cur: groups[cur].append(r)
    classes, features, subclasses = [], [], []
    for cls in CLASS_NAMES:
        g = groups[cls]
        # ---- core traits: label run followed by table text until next label
        traits, key = {}, None
        for r in raw_groups[cls]:
            t = r.text.strip()
            if r.kind == "label" and r.size < 10 and t:
                if key is not None and not traits[key].strip():
                    traits[key + " " + t] = traits.pop(key); key = key + " " + t   # wrapped label
                else:
                    key = t; traits[key] = ""
                continue
            if r.kind == "table" and key is not None:
                traits[key] += r.text; continue
            if r.kind == "name" and "Class Features" in t: break
        traits = {k.strip(): re.sub(r"([a-z])- ?([a-z])", r"\1\2", ascii(re.sub(r"\s+", " ", v)).strip())
                  for k, v in traits.items()}
        def tv(*names):
            for n in names:
                for k, v in traits.items():
                    if k.startswith(n): return v
            return ""
        skills_txt = tv("Skill Proficiencies")
        m = re.search(r"Choose (?:any )?(\d+)(?: skills)?:?\s*(.*)", skills_txt)
        n_sk = m.group(1) if m else "0"
        sk_list = m.group(2) if m else skills_txt
        sk_list = re.sub(r"([a-z])- ([a-z])", r"\1\2", sk_list)
        if re.search(r"\bany\b", skills_txt.lower()) and "skill" in skills_txt.lower():
            sk = SKILLS
        else:
            sk = [x for x in SKILLS if x in sk_list]
        heads, table, tpage = features_table(cls)
        rec = {"name": cls, "hitdie": tv("Hit Point Die").split(" ")[0].lower().replace("d", "d"),
               "primary": tv("Primary Ability"), "saves": tv("Saving Throw").replace(" and ", ", "),
               "skills_n": n_sk, "skills": ", ".join(sk),
               "weapons": tv("Weapon Proficiencies"), "tools": tv("Tool Proficiencies"),
               "armor": tv("Armor Training"), "equipment": tv("Starting Equipment").replace("Choose A or B: ", "").replace("Choose A, B, or C: ", "")}
        eq = parse_equipment(rec["equipment"])
        for k, v in eq.items():
            rec["equip_" + k.lower()] = "|".join(v)
        if heads:
            rec["cols"] = "|".join(heads)
            for lv in range(1, 21):
                rec[f"L{lv}"] = "|".join(table[lv])
        # ---- features
        sub = None
        i = 0
        cur_feat = None
        in_spell_list = False
        for r in g:
            t = r.text.strip()
            if r.kind == "name" and r.size >= 13.5:
                in_spell_list = "Spell List" in t
                if "Subclass:" in t:
                    sub = t.split("Subclass:")[1].strip()
                    cur_feat = None
                    continue
                if sub is not None and cur_feat is None and not in_spell_list and t and "Options" not in t:
                    sub = (sub + " " + t).strip()  # wrapped subclass name
                    continue
                if "Options" in t and cur_feat:   # e.g. Metamagic Options: keep with the feature text
                    cur_feat["_body"].append(Run("para", "")); cur_feat["_body"].append(Run("body", t.upper())); continue
                cur_feat = None if in_spell_list else cur_feat
                continue
            m = re.match(r"^Level (\d+): (.*)$", t) if r.kind == "name" else None
            if m:
                cur_feat = {"name": ascii(m.group(2).strip()), "class": cls, "subclass": sub or "",
                            "level": int(m.group(1)), "_body": []}
                features.append(cur_feat)
                if sub and not any(sc["name"] == sub for sc in subclasses):
                    subclasses.append({"name": ascii(sub), "class": cls})
                continue
            if r.kind == "name" and cur_feat and r.size < 13.5 and not t.startswith("Level"):
                # wrapped feature title or a sub-heading inside the feature
                if cur_feat["_body"] and any(x.kind not in ("eol", "para") for x in cur_feat["_body"]):
                    cur_feat["_body"].append(Run("para", "")); cur_feat["_body"].append(Run("body", t + ":"))
                else:
                    cur_feat["name"] += " " + ascii(t)
                continue
            if r.kind == "tbl" and re.search(r"(^|\n)Level \| Proficiency Bonus \| Class Features", r.text):
                continue   # the class table (kept in classes.txt as cols/L1..L20)
            if r.kind == "tbl" and re.match(r"Core \w+ Traits\n", r.text):
                continue
            if cur_feat is not None and not in_spell_list:
                cur_feat["_body"].append(r)
        # spellcasting ability
        sc = next((f for f in features if f["class"] == cls and f["name"] in ("Spellcasting", "Pact Magic")), None)
        if sc:
            txt = flatten(sc["_body"])
            m = re.search(r"(Intelligence|Wisdom|Charisma) is your spellcasting ability", txt)
            if m: rec["spell_ability"] = m.group(1)
        classes.append(rec)
    for f in features:
        f["text"] = ascii(flatten(f.pop("_body"))).replace("\n", "\\n")
    return classes, features, subclasses

# ---------------------------------------------------------------- origins & feats
def labelled_fields(body):
    """label: value pairs from GillSans label/table runs (values may wrap)."""
    fields, key = {}, None
    for r in body:
        t = r.text.strip()
        if r.kind == "label" and t.endswith(":"):
            key = t.rstrip(":"); fields[key] = ""; continue
        if r.kind == "table" and key:
            fields[key] += r.text; continue
        if r.kind in ("eol", "para"): continue
        key = None
    return {k: re.sub(r"([a-z])- ?([a-z])", r"\1\2", ascii(re.sub(r"\s+", " ", v)).strip()) for k, v in fields.items()}

def extract_backgrounds():
    out = []
    for name, body in entries_by_name(83, 83):
        f = labelled_fields(body)
        if "Ability Scores" not in f: continue
        eq = f.get("Equipment", "").replace("Choose A or B: ", "")
        r = {"name": ascii(name), "abilities": f["Ability Scores"],
             "feat": re.sub(r"\s*\(see \"Feats\"\)", "", f.get("Feat", "")),
             "skills": f.get("Skill Proficiencies", "").replace(" and ", ", "),
             "tool": f.get("Tool Proficiency", ""), "equipment": eq}
        for k, v in parse_equipment(eq).items():
            r["equip_" + k.lower()] = "|".join(v)
        out.append(r)
    return out

def extract_species():
    out = []
    for name, body in entries_by_name(84, 86):
        f = labelled_fields(body)
        if "Creature Type" not in f: continue
        # traits: bold-italic run-in headings
        traits, cur = [], None
        for r in body:
            if r.kind == "bolditalic" and r.text.strip():
                cur = [r.text.strip().rstrip("."), []]; traits.append(cur); continue
            if cur is not None:
                cur[1].append(r)
        rec = {"name": ascii(name), "type": f["Creature Type"], "size": f.get("Size", ""),
               "speed": re.sub(r"\D", "", f.get("Speed", "")) or "30"}
        txt_all = ""
        for i, (tn, tb) in enumerate(traits):
            t = ascii(flatten(tb)).replace("\n", "\\n")
            rec[f"trait{i+1}"] = f"{ascii(tn)}: {t}"
            txt_all += t
        m = re.search(r"Darkvision with a range of (\d+) feet", txt_all)
        if m: rec["darkvision"] = m.group(1)
        out.append(rec)
    return out

def extract_feats():
    out = []
    for name, body in entries_by_name(87, 88):
        cat = ""
        rest = []
        for r in body:
            if r.kind == "italic" and (not cat or cat.endswith(" ") or cat.count("(") > cat.count(")")) and not rest:
                cat += r.text; continue
            if r.kind == "italic" and cat and not any(x.kind not in ("eol", "para") for x in rest):
                cat += r.text; continue
            rest.append(r)
        cat = ascii(re.sub(r"\s+", " ", cat)).strip()
        if "Feat" not in cat: continue
        m = re.match(r"^(\w[\w ]*?) Feat(?: \(Prerequisite: (.*)\))?$", cat)
        txt = ascii(flatten(rest))
        r = {"name": ascii(name), "category": m.group(1) if m else cat, "prereq": m.group(2) if m and m.group(2) else ""}
        m = re.search(r"Increase (?:your|one ability score)[^.]*?(Strength|Dexterity|Constitution|Intelligence|Wisdom|Charisma)?[^.]*? by (\d)", txt)
        abil = re.findall(r"\b(Strength|Dexterity|Constitution|Intelligence|Wisdom|Charisma)\b", txt.split("Ability Score Increase.")[1].split(".")[0]) if "Ability Score Increase." in txt else []
        if abil: r["asi"] = ", ".join(dict.fromkeys(abil))
        if "Repeatable." in txt: r["repeatable"] = 1
        r["text"] = txt.replace("\n", "\\n")
        out.append(r)
    return out

# ---------------------------------------------------------------- roll tables
NUMCELL = re.compile(r"^(\d{1,3}|\d{1,3}\s*[–-]\s*\d{1,3}|—)$")

def raw_spans(pno):
    out = []
    for b in DOC[pno - 1].get_text("dict")["blocks"]:
        for l in b.get("lines", []):
            for sp in l["spans"]:
                if sp["text"].strip() and sp["bbox"][1] < 735:
                    out.append(dict(x=sp["bbox"][0], y=sp["bbox"][1], t=sp["text"], f=sp["font"]))
    return out

def weight_of(cell, die):
    cell = cell.replace("–", "-").replace(" ", "")
    if cell == "—": return 0
    if "-" in cell:
        lo, hi = cell.split("-")
        lo, hi = int(lo), int(hi)
        if hi == 0: hi = 100          # "97-00"
        if lo == 0: lo = 100
        return hi - lo + 1
    return 1

def join_cells(cells):
    txt = ""
    for c in cells:
        t = c["t"]
        if txt.endswith("-") and t.strip()[:1].islower():
            txt = txt[:-1] + t.strip()
        elif txt and not txt.endswith(" ") and not t.startswith(" ") and t.strip()[:1] not in ";,.:)":
            txt += " " + t
        else:
            txt += t
    txt = re.sub(r"\s+([;,.:)])", r"\1", re.sub(r"\s+", " ", txt))
    return ascii(txt).strip()

def dtable(regions):
    """Rows of a 'dN | text' table from (page, x0, x1, y0, y1) regions.
    Side-by-side sub-tables inside one region are handled via the header columns."""
    rows = []
    for pno, x0, x1, y0, y1 in regions:
        ss = [s for s in raw_spans(pno) if x0 <= s["x"] < x1 and y0 <= s["y"] < y1 and "GillSans" in s["f"]]
        hdr = sorted([s for s in ss if "Semi" in s["f"] and re.fullmatch(r"1?d\d+", s["t"].strip())], key=lambda s: s["x"])
        numcols = sorted({round(h["x"]) for h in hdr})
        def colidx(x):
            i = 0
            for k, cx in enumerate(numcols):
                if x >= cx - 14: i = k
            return i
        starts = [s for s in ss if "Semi" not in s["f"] and NUMCELL.match(s["t"].strip())
                  and any(abs(s["x"] - cx) < 16 for cx in numcols)]
        for ci in range(len(numcols)):
            col = sorted([s for s in starts if colidx(s["x"]) == ci], key=lambda s: s["y"])
            right = numcols[ci + 1] - 14 if ci + 1 < len(numcols) else x1
            for i, st in enumerate(col):
                ny = col[i + 1]["y"] - 3 if i + 1 < len(col) else y1
                cells = sorted([s for s in ss if s is not st and st["y"] - 3 <= s["y"] < ny and st["x"] + 10 < s["x"] < right
                                and not ("Semi" in s["f"] and re.fullmatch(r"1?d\d+|[A-Z][a-z]+( [A-Z][a-z]+)?", s["t"].strip()) and s["y"] < st["y"] - 1)],
                               key=lambda s: (round(s["y"] / 3), s["x"]))
                rows.append((st["t"].strip(), join_cells(cells)))
    return rows

def write_table(path, title, die, rows, note=""):
    """Tables-app format: '# comment' lines, then 'weight<TAB>text' entries."""
    lines = [f"# {title} (SRD 5.2.1){' - ' + note if note else ''}", f"# die: {die}"]
    for w, t in rows:
        if w > 0 and t: lines.append(f"{w}\t{t}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"  table {path.name}: {len(lines) - 2} entries")

def extract_tables(out, magic, toolbox):
    t = out / "tables"
    def d(rows, die):
        return [(weight_of(n, die), txt) for n, txt in rows]
    write_table(t / "Trinket.txt", "Trinkets", "d100",
                d(dtable([(26, 300, 600, 50, 735), (27, 0, 300, 40, 735), (27, 300, 600, 40, 735)]), "d100"))
    write_table(t / "Language_(standard).txt", "Standard Languages", "d12",
                d(dtable([(20, 300, 600, 215, 400)]), "d12"), "everyone also knows Common")
    write_table(t / "Language_(rare).txt", "Rare Languages", "pick",
                [(1, x) for x in ["Abyssal", "Celestial", "Deep Speech", "Druidic", "Infernal",
                                  "Primordial (Aquan, Auran, Ignan or Terran)", "Sylvan", "Thieves' Cant", "Undercommon"]])
    write_table(t / "Wand_of_Wonder.txt", "Wand of Wonder Effects", "d100",
                d(dtable([(252, 0, 300, 40, 735), (252, 300, 600, 40, 735)]), "d100"))
    for name, y0, y1 in (("Gray", 330, 415), ("Rust", 430, 515), ("Tan", 530, 612)):
        write_table(t / f"Bag_of_Tricks_({name}).txt", f"{name} Bag of Tricks", "d8",
                    d(dtable([(212, 300, 600, y0, y1)]), "d8"))
    write_table(t / "Sentient_Item_Alignment.txt", "Sentient Item's Alignment", "d100", d(dtable([(208, 0, 300, 170, 272)]), "d100"))
    write_table(t / "Sentient_Item_Communication.txt", "Sentient Item's Communication", "d10", d(dtable([(208, 0, 300, 285, 395)]), "d10"))
    write_table(t / "Sentient_Item_Senses.txt", "Sentient Item's Senses", "d4", d(dtable([(208, 0, 300, 405, 520)]), "d4"))
    sp = dtable([(208, 0, 300, 505, 735), (208, 300, 600, 40, 285)])
    write_table(t / "Sentient_Item_Purpose.txt", "Sentient Item's Special Purpose", "d10", d(sp, "d10"))
    # Mysterious Deck (22-card numbering) with each card's effect from the item text
    deck = next((m for m in magic if m["name"] == "Mysterious Deck"), None)
    effects = {}
    if deck:
        for para in deck["text"].split("\\n"):
            m = re.match(r"^([A-Z][a-z]+)\. (.*)", para)
            if m: effects[m.group(1)] = m.group(2)
    cards = []
    for pno, x0, x1, y0, y1 in ((231, 0, 300, 470, 735), (231, 300, 600, 75, 170)):
        ss = [s for s in raw_spans(pno) if x0 <= s["x"] < x1 and y0 <= s["y"] < y1 and "GillSans" in s["f"]]
        byrow = {}
        for s in ss:
            byrow.setdefault(round(s["y"] / 4), []).append(s)
        for _, cells in sorted(byrow.items()):
            cells.sort(key=lambda s: s["x"])
            if len(cells) >= 3:
                card = ascii(cells[-1]["t"].strip())
                eff = effects.get(card, "")
                cards.append((weight_of(cells[1]["t"].strip(), "d100"), f"{card}: {eff}" if eff else card))
    write_table(t / "Mysterious_Deck.txt", "Mysterious Deck (22 cards)", "d100", cards)
    # fear and mental stress samples: rows anchored on the DC cell
    def anchored(pno, x0, x1, y0, y1, dcx):
        ss = [s for s in raw_spans(pno) if x0 <= s["x"] < x1 and y0 <= s["y"] < y1 and "GillSans" in s["f"] and "Semi" not in s["f"]]
        dcs = sorted([s for s in ss if s["x"] >= dcx and re.fullmatch(r"\d+", s["t"].strip())], key=lambda s: s["y"])
        out = []
        for i, dc in enumerate(dcs):
            ny = dcs[i + 1]["y"] - 3 if i + 1 < len(dcs) else y1
            txt = join_cells(sorted([s for s in ss if s["x"] < dcx - 5 and dc["y"] - 3 <= s["y"] < ny], key=lambda s: (round(s["y"]), s["x"])))
            dmg = next((s["t"].strip() for s in ss if s["x"] > dcx + 20 and abs(s["y"] - dc["y"]) < 3 and re.fullmatch(r"\d+d\d+", s["t"].strip())), "")
            out.append((txt, dc["t"].strip(), dmg))
        return out
    write_table(t / "Fear_Effect.txt", "Sample Fear DCs", "pick",
                [(1, f"{x} (DC {dc} Wisdom save or Frightened)") for x, dc, _ in anchored(196, 60, 300, 300, 420, 250)])
    write_table(t / "Mental_Stress.txt", "Sample Mental Stress Effects", "pick",
                [(1, f"{x} (DC {dc} Wisdom save, [{dm}] Psychic damage)") for x, dc, dm in anchored(196, 300, 600, 238, 372, 470)])
    # magic items by rarity, with the SRD's typical value
    values = {"Common": "100 GP", "Uncommon": "400 GP", "Rare": "4,000 GP", "Very Rare": "40,000 GP", "Legendary": "200,000 GP"}
    for rar in ["Common", "Uncommon", "Rare", "Very Rare", "Legendary"]:
        names = [m["name"] for m in magic if m.get("rarity") == rar]
        write_table(t / f"Magic_Item_({rar}).txt", f"{rar} magic items", "pick",
                    [(1, f"{n} (~{values[rar]})") for n in names])
    # toolbox
    for cat, fname in (("Trap", "Trap"), ("Poison", "Poison"), ("Contagion", "Contagion"), ("Hazard", "Environmental_Hazard")):
        rows = []
        for r in toolbox:
            if r["category"] != cat: continue
            bits = [r["name"]]
            if r.get("subtype"): bits.append(r["subtype"])
            if r.get("save"): bits.append(r["save"])
            if r.get("damage"): bits.append(f"[{r['damage']}] {r.get('dtype', '')} damage".strip())
            if r.get("cost"): bits.append(r["cost"])
            rows.append((1, " - ".join(bits)))
        write_table(t / f"{fname}.txt", cat + "s", "pick", rows)

TOOLBOX_HAZARDS = ["Deep Water", "Extreme Cold", "Extreme Heat", "Frigid Water", "Heavy Precipitation",
                   "High Altitude", "Slippery Ice", "Strong Wind", "Thin Ice"]

def toolbox_rolls(rec, txt):
    m = re.search(r"DC (\d+) (Strength|Dexterity|Constitution|Intelligence|Wisdom|Charisma)(?: \(\w+\))? (saving throw|check)", txt)
    if m: rec["save"] = f"DC {m.group(1)} {m.group(2)} {'save' if m.group(3).startswith('saving') else 'check'}"
    m = re.search(r"\((\d+d\d+(?: ?\+ ?\d+)?)\) (" + "|".join(DAMAGE_TYPES) + r") damage", txt) or \
        re.search(r"(\d+d\d+(?: ?\+ ?\d+)?) (" + "|".join(DAMAGE_TYPES) + r") damage", txt)
    if m: rec["damage"] = m.group(1).replace(" ", ""); rec["dtype"] = m.group(2)
    m = re.search(r"DC (\d+) Wisdom \(Perception\)", txt)
    if m: rec["detect"] = f"DC {m.group(1)} Perception"

def extract_toolbox():
    out = []
    for name, body in entries_by_name(193, 201):
        sub = ""
        rest = []
        for r in body:
            if r.kind == "italic" and not any(x.kind not in ("eol", "para") for x in rest):
                sub += r.text; continue
            rest.append(r)
        sub = ascii(re.sub(r"\s+", " ", sub)).strip()
        if "Trap" in sub: cat = "Trap"
        elif "Poison" in sub: cat = "Poison"
        elif "Contagion" in sub: cat = "Contagion"
        else: continue
        nm, cost = name, ""
        m = re.match(r"^(.*) \((.*GP)\)$", name)
        if m: nm, cost = m.group(1), m.group(2)
        # bold "Trigger:/Duration:" labels become their own lines
        txt = ascii(flatten(rest))
        r = {"name": ascii(nm), "category": cat, "subtype": sub, "cost": ascii(cost)}
        toolbox_rolls(r, txt)
        r["text"] = txt.replace("\n", "\\n")
        out.append(r)
    # environmental hazards: 14pt headings on p195
    rs = runs(195, 195)
    cur = None
    for r in rs:
        if r.kind == "name" and r.text.strip() in TOOLBOX_HAZARDS:
            cur = {"name": r.text.strip(), "category": "Hazard", "_b": []}; out.append(cur); continue
        if r.kind == "name" and r.size >= 13: cur = None; continue
        if cur is not None: cur["_b"].append(r)
    for r in out:
        if "_b" in r:
            txt = ascii(flatten(r.pop("_b")))
            toolbox_rolls(r, txt)
            r["subtype"] = "Environmental hazard"
            r["text"] = txt.replace("\n", "\\n")
    return out

# ---------------------------------------------------------------- monsters
SIZES = ("Tiny", "Small", "Medium", "Large", "Huge", "Gargantuan")
MON_FIELDS = ["AC", "Initiative", "HP", "Speed", "Skills", "Resistances", "Vulnerabilities", "Immunities",
              "Gear", "Senses", "Languages", "CR"]
MON_SECTIONS = ["Traits", "Actions", "Bonus Actions", "Reactions", "Legendary Actions"]

def parse_rolls(entry_name, txt):
    """Rollable parts of a stat block entry -> 'name|atk|+9|2d6+5|Bludgeoning|extra dice|extra type'."""
    t = txt.replace("−", "-")
    out = []
    m = re.search(r"(Melee or Ranged|Melee|Ranged) Attack Roll: ([+-]\d+)", t)
    if m:
        h = re.search(r"Hit: \d+ \(([0-9d +-]+)\) (\w+) damage", t) or re.search(r"Hit: (\d+) (\w+) damage", t)
        x = re.search(r"Hit: [^.]*? plus \d+ \(([0-9d +-]+)\) (\w+) damage", t)
        if h:
            out.append("|".join([entry_name, "atk", m.group(2), h.group(1).replace(" ", ""), h.group(2),
                                 x.group(1).replace(" ", "") if x else "", x.group(2) if x else ""]))
        return out
    m = re.search(r"(Strength|Dexterity|Constitution|Intelligence|Wisdom|Charisma) Saving Throw: DC (\d+)", t)
    if m:
        f = re.search(r"Failure: \d+ \(([0-9d +-]+)\) (\w+) damage", t)
        out.append("|".join([entry_name, "save", f"DC {m.group(2)} {m.group(1)[:3].upper()}",
                             f.group(1).replace(" ", "") if f else "", f.group(2) if f else "", "", ""]))
    return out

def extract_monsters():
    rs = runs(258, len(DOC))
    starts = []
    for i, r in enumerate(rs):
        if r.kind == "name" and 13 <= r.size < 15:
            j = i + 1
            while j < len(rs) and rs[j].kind in ("eol", "para"): j += 1
            if j < len(rs) and rs[j].kind == "italic" and rs[j].text.strip().startswith(SIZES):
                # names can be split into several runs ("Gray" "Ooze")
                while i > 0 and rs[i - 1].kind == "name" and abs(rs[i - 1].size - r.size) < 0.1: i -= 1
                if not starts or starts[-1] != i: starts.append(i)
    mons = []
    for k, si in enumerate(starts):
        end = starts[k + 1] if k + 1 < len(starts) else len(rs)
        block = [r for r in rs[si:end] if not (r.kind == "name" and r.size >= 17)]
        i = 0
        name = ""
        while i < len(block) and block[i].kind == "name":
            name += " " + block[i].text.strip(); i += 1
        name = ascii(re.sub(r"\s+", " ", name)).strip()
        typeline = ""
        while i < len(block) and block[i].kind in ("eol", "para", "italic"):
            if block[i].kind == "italic": typeline += block[i].text
            i += 1
        typeline = ascii(re.sub(r"\s+", " ", typeline)).strip()
        fields, key = {}, None
        nums = []
        section = None
        sections = {}
        for r in block[i:]:
            t = r.text.strip()
            if section is None:
                if r.kind == "bold" and t in MON_FIELDS:
                    key = t; fields[key] = ""; continue
                if r.kind in ("table", "label"):
                    if re.fullmatch(r"[+−-]?\d+", t): nums.append(int(t.replace("−", "-")))
                    continue
                if r.kind == "name" and t in MON_SECTIONS:
                    section = t; sections[section] = []; continue
                if key and r.kind not in ("eol", "para"):
                    fields[key] += r.text
                continue
            if r.kind == "name" and t in MON_SECTIONS:
                section = t; sections[section] = []; continue
            sections[section].append(r)
        fields = {k: ascii(re.sub(r"\s+", " ", v)).strip() for k, v in fields.items()}
        rec = {"name": name, "typeline": typeline}
        size = typeline.split(" ")[0] if typeline else ""
        ctype = re.sub(r"\s*\(.*?\)", "", typeline.split(",")[0])[len(size):].strip() if typeline else ""
        ctype = re.sub(r"^or (Tiny|Small|Medium|Large|Huge) ", "", ctype)
        rec["size"] = size
        rec["type"] = ctype
        hab = habitat(name)
        if not hab: print("  no habitat:", name)
        rec["habitat"] = hab or "Any"
        rec["ac"] = re.match(r"\d+", fields.get("AC", "10")).group(0) if re.match(r"\d+", fields.get("AC", "")) else fields.get("AC", "")
        if fields.get("AC", "") != rec["ac"]: rec["ac_note"] = fields.get("AC", "")
        m = re.match(r"([+-]\d+)", fields.get("Initiative", "+0"))
        rec["init"] = m.group(1) if m else "+0"
        m = re.match(r"(\d+)(?: \(([^)]+)\))?", fields.get("HP", "1"))
        rec["hp"] = m.group(1) if m else "1"
        rec["hpdice"] = m.group(2).replace(" ", "") if m and m.group(2) else ""
        rec["speed"] = fields.get("Speed", "")
        if len(nums) >= 18:
            ab = [nums[i * 3:(i + 1) * 3] for i in range(6)]
            rec["scores"] = ",".join(str(a[0]) for a in ab)
            rec["saves"] = ",".join(("+" if a[2] >= 0 else "") + str(a[2]) for a in ab)
        for f in ("Skills", "Resistances", "Vulnerabilities", "Immunities", "Gear", "Senses", "Languages"):
            if fields.get(f): rec[f.lower()] = fields[f]
        cr = fields.get("CR", "")
        m = re.match(r"([\d/]+)\s*\((?:XP )?([\d,]+)", cr)
        rec["cr"] = m.group(1) if m else cr
        rec["xp"] = m.group(2).replace(",", "") if m else "0"
        m = re.search(r"PB \+(\d)", cr)
        if m: rec["pb"] = m.group(1)
        rolls = []
        for sec in MON_SECTIONS:
            if sec not in sections: continue
            # split into entries at bold-italic names
            entries, cur = [], None
            intro = []
            for r in sections[sec]:
                if r.kind == "bolditalic" and r.text.strip().endswith(".") and r.text.strip()[:1].isupper():
                    cur = [r.text.strip().rstrip("."), []]; entries.append(cur); continue
                (cur[1] if cur else intro).append(r)
            parts = []
            if intro:
                it = ascii(flatten(intro))
                if it: parts.append(it)
            for en, body in entries:
                txt = ascii(flatten(body)).replace("\n", " ")
                parts.append(f"{ascii(en)}. {txt}")
                rolls.extend(parse_rolls(ascii(en), txt))
            key = sec.lower().replace(" ", "_")
            rec[key] = "\\n".join(parts)
        for i, rl in enumerate(rolls[:12]):
            rec[f"roll{i + 1}"] = rl
        mons.append(rec)
    return mons

# ---------------------------------------------------------------- rules glossary
def extract_glossary():
    out = []
    for name, body in entries_by_name(176, 191):
        m = re.match(r"^(.*?)\s*\[(.*?)\]$", name)
        nm, tag = (m.group(1), m.group(2)) if m else (name, "")
        txt = ascii(flatten(body))
        if not txt: continue
        out.append({"name": ascii(nm), "tag": ascii(tag) or "Rule", "text": txt.replace("\n", "\\n")})
    return out

def main():
    global DOC
    DOC = pymupdf.open(sys.argv[1])
    out = Path(sys.argv[2])
    spells = extract_spells()
    write_db(out / "spells.txt", spells, ["level", "school", "classes", "ritual", "conc"])
    items = extract_weapons() + extract_armor() + extract_gear() + extract_tools()
    write_db(out / "items.txt", items, ["kind", "category", "cost", "weight"])
    classes, feats, subs = extract_classes()
    write_db(out / "classes.txt", classes, ["hitdie", "spell_ability"])
    write_db(out / "features.txt", feats, ["class", "subclass", "level"])
    write_db(out / "subclasses.txt", subs, ["class"])
    write_db(out / "backgrounds.txt", extract_backgrounds(), ["feat"])
    write_db(out / "species.txt", extract_species(), ["size", "speed"])
    write_db(out / "feats.txt", extract_feats(), ["category", "prereq"])
    magic = extract_magic_items()
    write_db(out / "magic.txt", magic, ["kind", "type", "rarity", "attune"])
    monsters = extract_monsters()
    write_db(out / "monsters.txt", monsters, ["cr", "type", "xp", "hp", "ac", "init", "size", "habitat"])
    write_db(out / "glossary.txt", extract_glossary(), ["tag"])
    toolbox = extract_toolbox()
    write_db(out / "toolbox.txt", toolbox, ["category", "subtype"])
    extract_tables(out, magic, toolbox)

if __name__ == "__main__":
    main()
