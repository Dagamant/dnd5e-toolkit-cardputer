"""Rebuild tables in the SRD PDF from where their text sits on the page.

The SRD prints tables in GillSans (9-9.5 pt cells, SemiBold headings) inside the
two-column Cambria text. PyMuPDF hands them back as loose blocks, so a table
that wraps a cell, has a heading over two lines, or spans both page columns
comes out scrambled. find_tables() groups a page's table blocks into tables and
rebuilds each one as rows of cells.
"""
import re

FIELD_LABEL = re.compile(r"^\s*(Casting Time|Range|Components?|Duration|Creature Type|Size|Speed):\s*$")


def _spans(b):
    return [s for l in b["lines"] for s in l["spans"] if s["text"].strip()]


def is_table_block(b):
    sp = _spans(b)
    if not sp: return False
    for s in sp:
        f = s["font"]
        if "GillSans" not in f or "SC700" in f: return False      # body text, stat block grids
        if not 8.5 <= s["size"] <= 9.9: return False              # headings (10.5+), tiny grid labels
    if any("SemiBold" in s["font"] and FIELD_LABEL.match(s["text"]) for s in sp): return False   # spell/species fields
    return True


def is_title_block(b):
    sp = _spans(b)
    return (len(sp) >= 1 and all("GillSans-SemiBold" in s["font"] and 10.2 <= s["size"] <= 11 for s in sp)
            and len({round(s["bbox"][1]) for s in sp}) == 1)


def group_tables(blocks, mid):
    """Groups table blocks (with an optional title just above) into tables.
    Returns a list of lists of block indexes."""
    tab = [i for i, b in enumerate(blocks) if is_table_block(b)]
    tab.sort(key=lambda i: blocks[i]["bbox"][1])
    groups = []
    for i in tab:
        x0, y0, x1, y1 = blocks[i]["bbox"]
        side = "L" if x1 <= mid + 5 else "R" if x0 >= mid - 10 else "W"
        for g in groups:
            if y0 - g["y1"] < 14 and (g["side"] == side or "W" in (g["side"], side)):
                g["ids"].append(i); g["y1"] = max(g["y1"], y1)
                if side == "W" or g["side"] == "W": g["side"] = "W"
                break
        else:
            groups.append({"ids": [i], "y0": y0, "y1": y1, "side": side})
    # a wide table whose blocks sit on both sides: groups that touch a wide one belong to it
    merged = True
    while merged:
        merged = False
        for a in groups:
            for b in groups:
                if a is b or "W" not in (a["side"], b["side"]): continue
                if b["y0"] - a["y1"] < 14 and a["y0"] - b["y1"] < 14:
                    a["ids"] += b["ids"]; a["y0"] = min(a["y0"], b["y0"]); a["y1"] = max(a["y1"], b["y1"])
                    a["side"] = "W"; groups.remove(b); merged = True
                    break
            if merged: break
    out = []
    for g in groups:
        ids = g["ids"]
        # title right above
        gx0 = min(blocks[i]["bbox"][0] for i in ids)
        for j, b in enumerate(blocks):
            if j in ids or not b.get("lines") or not is_title_block(b): continue
            bx0, by0, bx1, by1 = b["bbox"]
            if 0 <= g["y0"] - by1 < 8 and abs(bx0 - gx0) < 12:
                ids.insert(0, j)
        out.append(ids)
    return out


def _lines(spans):
    """Spans -> lines (lists of spans sharing a baseline), top to bottom."""
    spans = sorted(spans, key=lambda s: (round(s["bbox"][3]), s["bbox"][0]))
    lines = []
    for s in spans:
        if lines and abs(s["bbox"][3] - lines[-1][0]["bbox"][3]) < 3:
            lines[-1].append(s)
        else:
            lines.append([s])
    for l in lines: l.sort(key=lambda s: s["bbox"][0])
    return lines


def _join(spans):
    t = ""
    for k, s in enumerate(spans):
        txt = s["text"]
        if k and spans[k]["bbox"][0] - spans[k - 1]["bbox"][2] > 1.5 and not t.endswith(" ") and not txt.startswith(" "):
            t += " "
        if t.endswith("-") and txt[:1].islower() and not t.endswith(" -"): t = t[:-1]
        t += txt
    return re.sub(r"\s+", " ", t.replace("​", "")).strip()


def _cells_x0(line):
    """x positions where cells start in one line (a gap of more than 6 px starts a cell)."""
    xs, prev_x1 = [], None
    for s in line:
        if prev_x1 is None or s["bbox"][0] - prev_x1 > 6: xs.append(s["bbox"][0])
        prev_x1 = s["bbox"][2]
    return xs


def build_table(blocks, ids):
    """-> (title or None, rows or None); rows[0] is the heading when the table has one.
    rows is None when the blocks don't form a real table."""
    title = None
    spans = []
    for i in ids:
        if i == ids[0] and is_title_block(blocks[i]):
            title = _join(_spans(blocks[i])); continue
        spans += _spans(blocks[i])
    lines = _lines(spans)
    if not lines: return title, None, False
    bold = lambda s: "SemiBold" in s["font"] or "Bold" in s["font"]
    nhead = 0   # heading lines: all-bold lines at the top
    # "Primary Ability | Strength" style tables (every first-column cell bold) have no heading
    left = min(l[0]["bbox"][0] for l in lines)
    label_rows = all(bold(l[0]) for l in lines if l[0]["bbox"][0] - left < 30)
    while nhead < len(lines) and nhead < 5 and not label_rows and (
            sum(bold(s) for s in lines[nhead]) * 2 >= len(lines[nhead]) or
            (nhead == 0 and any(bold(s) for s in lines[0]))):   # Travel Terrain mixes weights in its heading
        nhead += 1
    if nhead == len(lines): nhead = 0
    body = lines[nhead:]
    # columns: where body cells line up
    xs = sorted(x for l in body for x in _cells_x0(l))
    clusters = []
    for x in xs:
        if clusters and x - clusters[-1][-1] <= 8: clusters[-1].append(x)
        else: clusters.append([x])
    need = max(1, int(len(body) * 0.2))
    cols = [min(c) for c in clusters if len(c) >= need]
    if not cols: return title, None, False
    # right-aligned or centred columns start at different x on different rows: neighbouring
    # positions that never hold cells in the same line are one column
    merged = True
    while merged and len(cols) > 1:
        merged = False
        sets = []
        for l in body:
            ks = set()
            for x in _cells_x0(l):
                k = 0
                for c, cx in enumerate(cols):
                    if x >= cx - 6: k = c
                ks.add(k)
            sets.append(ks)
        for k in range(len(cols) - 1):
            if not any(k in st and k + 1 in st for st in sets):
                del cols[k + 1]; merged = True; break
    if len(cols) == 1 and nhead:   # a one-column body: trust the heading instead
        cols = sorted({round(x) for l in lines[:nhead] for x in _cells_x0(l)})
    def col_of(x):
        k = 0
        for c, cx in enumerate(cols):
            if x >= cx - 6: k = c
        return k
    def cells(l):
        out = [[] for _ in cols]
        for s in l: out[col_of(s["bbox"][0])].append(s)
        return out
    rows = []
    if nhead:
        def overlap(c, x0, x1):
            lo = cols[c] - 6
            hi = cols[c + 1] - 6 if c + 1 < len(cols) else 1e9
            return min(x1, hi) - max(x0, lo)
        head = [[] for _ in cols]
        pieces = []
        for l in lines[:nhead]:
            for sp in l:
                x0, x1 = sp["bbox"][0], sp["bbox"][2]
                covered = [c for c, cx in enumerate(cols) if x0 - 6 <= cx < x1 - 2]
                if len(covered) >= 2:   # a heading over several columns ("Spell Slots per Spell Level")
                    label = _join([sp]).strip("\u2014- ")
                    short = "Slot" if "Slot" in label else label
                    for c in covered: head[c].insert(0, {"text": short + " ", "bbox": (0, -1, 0, -1), "font": ""})
                else:
                    pieces.append(sp)
        # words of one heading stacked on several lines ("Proficiency / Bonus") are placed together
        groups = []
        for sp in pieces:
            for g in groups:
                if any(sp["bbox"][0] < o["bbox"][2] and o["bbox"][0] < sp["bbox"][2] and
                       abs(sp["bbox"][3] - o["bbox"][3]) >= 3 for o in g):
                    g.append(sp); break
            else:
                groups.append([sp])
        for g in groups:
            x0, x1 = min(sp["bbox"][0] for sp in g), max(sp["bbox"][2] for sp in g)
            head[max(range(len(cols)), key=lambda c: overlap(c, x0, x1))] += g
        # pieces from different heading lines are words of their own
        def join_head(h):
            by_line = {}
            for sp in h: by_line.setdefault(round(sp["bbox"][3]), []).append(sp)
            return " ".join(_join(by_line[k]) for k in sorted(by_line)).strip()
        rows.append([join_head(h) for h in head])
    prev_y = None
    for l in body:
        cs = [_join(c) for c in cells(l)]
        y = l[0]["bbox"][3]
        filled = [c for c in cs if c]
        first_only = len(filled) == 1 and cs[0]
        has_rows = len(rows) > (1 if nhead else 0)
        cont = has_rows and (not cs[0] or (first_only and prev_y is not None and y - prev_y <= 13.5
                                           and sum(1 for c in rows[-1] if c) >= 2))
        if cont:
            r = rows[-1]
            for c, v in enumerate(cs):
                if not v: continue
                if r[c].endswith("-") and v[:1].islower(): r[c] = r[c][:-1] + v
                else: r[c] = (r[c] + " " + v).strip()
        else:
            rows.append(cs)
        prev_y = y
    if len(rows) < 2 or sum(1 for r in rows if sum(1 for c in r if c) >= 2) < 2:
        return title, None, False
    if nhead: rows = unfold(rows)
    return title, rows, bool(nhead)


def unfold(rows):
    """A table printed as two side-by-side halves under a repeated heading
    ("Spell | Charge Cost | Spell | Charge Cost") becomes one list: left half, then right."""
    n = len(rows[0])
    if n < 4 or n % 2: return rows
    h = n // 2
    if [c.lower() for c in rows[0][:h]] != [c.lower() for c in rows[0][h:]]: return rows
    left = [r[:h] for r in rows[1:] if any(r[:h])]
    right = [r[h:] for r in rows[1:] if any(r[h:])]
    return [rows[0][:h]] + left + right


def serialize(title, rows, head=True):
    """Text form used in the data files: an optional title line, then one line per
    row with cells joined by " | ", the first row being the heading. Empty cells stay
    empty. Two-column tables without a heading ("Primary Ability | Strength") read
    better as "Label: value" lines."""
    out = [title] if title else []
    if not head and len(rows[0]) == 2:
        out += [f"{a}: {b}" if b else a for a, b in rows]
    else:
        out += [" | ".join(r) for r in rows]
    return "\n".join(out)
