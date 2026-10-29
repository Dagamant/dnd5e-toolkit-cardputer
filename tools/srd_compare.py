"""Compare our SRD data with the 5e-bits database (2024 SRD as JSON).

Usage: python3 tools/srd_compare.py sdcard/gmtools/srd PATH/5e-database/src/2024/en [-v]

Matches records by name and lists, per kind: names only one side has, fields
that disagree (levels, costs, AC, HP, CR...), and descriptions that differ a lot
(likely a parsing slip on one side). -v also lists small text differences.
https://github.com/5e-bits/5e-database (MIT)
"""
import difflib
import json
import re
import sys
from pathlib import Path

OURS, THEIRS = Path(sys.argv[1]), Path(sys.argv[2])
VERBOSE = "-v" in sys.argv


def load_ours(name):
    recs = []
    for block in (OURS / f"{name}.txt").read_text().split("\n\n"):
        if not block.strip(): continue
        r = {}
        for line in block.split("\n"):
            k, _, v = line.partition(": ")
            r[k] = v
        recs.append(r)
    return recs


def load_theirs(name):
    return json.loads((THEIRS / f"5e-SRD-{name}.json").read_text())


def key(name):
    s = name.lower().replace("’", "'").replace("’", "'")
    return re.sub(r"[^a-z0-9+]", "", s)


def plain(t):
    """Text for comparing: no markdown, quotes or whitespace differences."""
    t = (t or "").replace("\\n", " ").replace("’", "'").replace("—", " - ").replace("–", "-")
    t = re.sub(r"[*_#|]", " ", t)
    t = re.sub(r"\s+", " ", t)
    return t.strip().lower()


def ratio(a, b):
    a, b = plain(a), plain(b)
    if not a and not b: return 1.0
    return difflib.SequenceMatcher(None, a, b, autojunk=False).ratio()


class Report:
    def __init__(self, title):
        self.title = title
        self.lines = []
        self.counts = {}

    def add(self, kind, msg):
        self.counts[kind] = self.counts.get(kind, 0) + 1
        self.lines.append(f"  [{kind}] {msg}")

    def show(self):
        print(f"\n=== {self.title}: " + (", ".join(f"{v} {k}" for k, v in self.counts.items()) or "no differences"))
        for l in self.lines: print(l)


def pair(ours, theirs, rep, okey=lambda r: r["name"], tkey=lambda r: r["name"]):
    o = {key(okey(r)): r for r in ours}
    t = {key(tkey(r)): r for r in theirs}
    for k in sorted(set(o) - set(t)): rep.add("only ours", okey(o[k]))
    for k in sorted(set(t) - set(o)): rep.add("only theirs", tkey(t[k]))
    return [(o[k], t[k]) for k in sorted(set(o) & set(t))]


def same(rep, name, field, a, b, norm=lambda x: str(x).strip().lower()):
    if norm(a) != norm(b): rep.add("field", f"{name}: {field} ours={a!r} theirs={b!r}")


def text(rep, name, a, b, limit=0.80):
    r = ratio(a, b)
    if r < limit: rep.add("text", f"{name}: description {r:.0%} alike (ours {len(plain(a))} chars, theirs {len(plain(b))})")
    elif VERBOSE and r < 0.97: rep.add("text~", f"{name}: {r:.0%} alike")


# ---------------------------------------------------------------- spells
def spells():
    rep = Report("Spells")
    for o, t in pair(load_ours("spells"), load_theirs("Spells"), rep):
        n = o["name"]
        same(rep, n, "level", o.get("level"), t["level"])
        same(rep, n, "school", o.get("school"), t["school"]["name"])
        same(rep, n, "classes", o.get("classes"), ", ".join(sorted(c["name"] for c in t.get("classes", []))),
             lambda x: ",".join(sorted(s.strip().lower() for s in str(x).split(","))))
        same(rep, n, "ritual", bool(o.get("ritual")), bool(t.get("ritual")))
        same(rep, n, "concentration", bool(o.get("conc")), bool(t.get("concentration")))
        same(rep, n, "casting time", o.get("time"), t.get("casting_time"))
        same(rep, n, "range", o.get("range"), t.get("range"))
        same(rep, n, "duration", o.get("duration"), t.get("duration"),
             lambda x: re.sub(r"^concentration,?\s*", "", str(x).strip().lower()))
        comps = ", ".join(t.get("components", []))
        same(rep, n, "components", re.sub(r"\s*\(.*", "", o.get("components", "")), comps, lambda x: x.replace(" ", "").lower())
        if t.get("material"):
            m = re.search(r"\((.*)\)", o.get("components", ""))
            same(rep, n, "material", m.group(1) if m else "", t["material"], lambda x: plain(x).rstrip("."))
        dmg = t.get("damage", {})
        dice = dmg.get("damage_at_slot_level") or dmg.get("damage_at_character_level") or {}
        if dice:
            first = dice[sorted(dice, key=int)[0]]
            same(rep, n, "damage", o.get("damage", ""), first, lambda x: re.sub(r"\s|\+mod", "", str(x).lower()))
        if dmg.get("damage_type"): same(rep, n, "damage type", o.get("dtype", ""), dmg["damage_type"]["name"])
        elif o.get("dtype"): rep.add("field", f"{n}: damage type ours={o['dtype']!r} theirs=none")
        dc = t.get("dc", {}).get("dc_type", {}).get("name", "")
        if dc:   # theirs leaves most saves out
            same(rep, n, "save", (o.get("save") or "")[:3], dc[:3])
        text(rep, n, o.get("text"), t.get("description"))
        if t.get("higher_level") or o.get("higher"):
            text(rep, n + " (higher levels)", re.sub(r"^Using a Higher-Level Spell Slot\. |^Cantrip Upgrade\. ", "", o.get("higher", "")),
                 t.get("higher_level", ""), 0.75)
    rep.show()


# ---------------------------------------------------------------- monsters
def cr_text(v):
    return {0.125: "1/8", 0.25: "1/4", 0.5: "1/2"}.get(v, str(int(v)) if v == int(v) else str(v))


def monsters():
    rep = Report("Monsters")
    for o, t in pair(load_ours("monsters"), load_theirs("Monsters"), rep):
        n = o["name"]
        same(rep, n, "size", o.get("size"), t.get("size").split(" or ")[0])   # "Medium or Small": ours keeps the first
        same(rep, n, "type", o.get("type"), t.get("type"))
        same(rep, n, "AC", o.get("ac"), t["armor_class"][0]["value"] if t.get("armor_class") else "")
        same(rep, n, "HP", o.get("hp"), t.get("hit_points"))
        same(rep, n, "hit dice", o.get("hpdice"), t.get("hit_points_roll") or t.get("hit_dice"), lambda x: str(x).replace(" ", "").lower())
        same(rep, n, "CR", o.get("cr"), cr_text(t.get("challenge_rating", 0)))
        if "xp" in t: same(rep, n, "XP", o.get("xp"), t["xp"])
        scores = ",".join(str(t.get(a, "")) for a in ("strength", "dexterity", "constitution", "intelligence", "wisdom", "charisma"))
        same(rep, n, "scores", o.get("scores"), scores)
        sp = t.get("speed", {})
        theirs_speed = sorted(re.sub(r"\D", "", v) + k[0] for k, v in sp.items() if k != "hover" and isinstance(v, str))
        ours_speed = sorted(re.search(r"(\d+)", p).group(1) + (re.match(r"\s*([A-Za-z])", p).group(1).lower() if re.match(r"\s*[A-Za-z]", p) else "w")
                            for p in o.get("speed", "").split(",") if re.search(r"\d", p))
        if ours_speed != theirs_speed: rep.add("field", f"{n}: speed ours={o.get('speed')!r} theirs={sp}")
        skills = {p["proficiency"]["name"].split(": ")[1]: p["value"] for p in t.get("proficiencies", []) if p["proficiency"]["name"].startswith("Skill")}
        ours_sk = dict(re.match(r"(.+?) ([+-]\d+)$", s.strip()).groups() for s in o.get("skills", "").split(",") if re.match(r".+? [+-]\d+$", s.strip()))
        if {k: int(v) for k, v in ours_sk.items()} != skills: rep.add("field", f"{n}: skills ours={o.get('skills')!r} theirs={skills}")
        same(rep, n, "languages", o.get("languages", ""), t.get("languages", ""), lambda x: plain(x).replace("none", "").strip(" -"))
        ours_names = [re.sub(r" \(.*\)$", "", e.split(". ")[0]) for e in re.split(r"\\n", o.get("actions", "")) if ". " in e]
        theirs_names = [a["name"] for a in t.get("actions", [])]
        if [key(x) for x in ours_names] != [key(x) for x in theirs_names]:
            rep.add("field", f"{n}: actions ours={ours_names} theirs={theirs_names}")
    rep.show()


# ---------------------------------------------------------------- equipment
def money(c):
    return f"{c['quantity']:,} {c['unit'].upper()}" if c else ""


def items():
    rep = Report("Equipment")
    theirs = [t for t in load_theirs("Equipment")]
    for o, t in pair(load_ours("items"), theirs, rep):
        n = o["name"]
        same(rep, n, "cost", o.get("cost"), money(t.get("cost")), lambda x: str(x).replace(",", "").lower())
        if "weight" in t:
            w = o.get("weight", "")
            m = re.match(r"([\d ]+(?:1/2|1/4)?|[\d.]+)", w.replace(" 1/2", ".5").replace("1/2", "0.5").replace(" 1/4", ".25").replace("1/4", "0.25"))
            ow = float(m.group(1)) if m and m.group(1).strip() else 0
            if abs(ow - float(t["weight"])) > 0.01: rep.add("field", f"{n}: weight ours={w!r} theirs={t['weight']}")
        if "damage" in t:
            same(rep, n, "damage", o.get("damage"), t["damage"]["damage_dice"])
            same(rep, n, "damage type", o.get("dtype"), t["damage"]["damage_type"]["name"])
        if t.get("mastery"): same(rep, n, "mastery", o.get("mastery"), t["mastery"]["name"])
        if t.get("properties"):
            tp = sorted(p["name"].lower() for p in t["properties"])
            op = sorted(re.sub(r"\s*\(.*", "", p).strip().lower() for p in o.get("props", "").split(",") if p.strip())
            if tp != op: rep.add("field", f"{n}: properties ours={o.get('props')!r} theirs={tp}")
        if t.get("armor_class"):
            ac = t["armor_class"]
            same(rep, n, "AC", o.get("ac_base"), ac["base"])
        if t.get("str_minimum"): same(rep, n, "Str needed", o.get("str"), t["str_minimum"])
        if "stealth_disadvantage" in t: same(rep, n, "stealth", bool(o.get("stealth")), bool(t["stealth_disadvantage"]))
        if t.get("description") and o.get("text"): text(rep, n, o["text"], t["description"], 0.7)
        elif t.get("description"): rep.add("blank", f"{n}: we have no description, theirs has {len(t['description'])} chars")
    rep.show()


def magic():
    rep = Report("Magic items")
    theirs = load_theirs("Magic-Items")
    for o, t in pair(load_ours("magic"), theirs, rep):
        n = o["name"]
        same(rep, n, "rarity", o.get("rarity"), t["rarity"]["name"])
        att = bool(t.get("attunement"))
        same(rep, n, "attunement", bool(o.get("attune")), att)
        same(rep, n, "type", o.get("type"), t["equipment_category"]["name"],
             lambda x: {"wondrous items": "wondrous item", "weapons": "weapon", "potions": "potion", "rings": "ring", "rods": "rod",
                        "scrolls": "scroll", "staffs": "staff", "wands": "wand"}.get(str(x).lower(), str(x).lower()))
        desc = re.sub(r"^[^\n]*\n", "", t.get("desc", ""), count=1)   # theirs starts with the type line
        text(rep, n, o.get("text"), desc, 0.75)
    rep.show()


# ---------------------------------------------------------------- feats, features, species, backgrounds, classes
def feats():
    rep = Report("Feats")
    for o, t in pair(load_ours("feats"), load_theirs("Feats"), rep):
        n = o["name"]
        same(rep, n, "category", o.get("category"), t.get("type"), lambda x: str(x).lower().replace(" feat", "").replace("-", " "))
        same(rep, n, "repeatable", bool(o.get("repeatable")), bool(t.get("repeatable")))
        text(rep, n, o.get("text"), t.get("description"))
    rep.show()


def features():
    rep = Report("Class features")
    ours = load_ours("features")
    theirs = load_theirs("Features")
    tk = lambda r: f"{r['class']['name']} {r.get('subclass', {}).get('name', '')} {r['level']['name'].split()[-1]} {r['name']}"
    ok = lambda r: f"{r['class']} {r.get('subclass', '')} {r['level']} {r['name']}"
    for o, t in pair(ours, theirs, rep, ok, tk):
        text(rep, ok(o), o.get("text"), t.get("description"), 0.75)
    rep.show()


def species():
    rep = Report("Species")
    traits = {key(t["name"]): t for t in load_theirs("Traits")}
    for o, t in pair(load_ours("species"), load_theirs("Species"), rep):
        n = o["name"]
        same(rep, n, "speed", o.get("speed"), t.get("speed"))
        names = [o[k].split(":")[0] for k in sorted(o) if k.startswith("trait")]
        theirs_names = [x["name"] for x in t.get("traits", [])]
        if sorted(map(key, names)) != sorted(map(key, theirs_names)): rep.add("field", f"{n}: traits ours={names} theirs={theirs_names}")
        for k in sorted(o):
            if not k.startswith("trait"): continue
            tn, _, body = o[k].partition(": ")
            tr = traits.get(key(tn))
            if tr: text(rep, f"{n} / {tn}", re.sub(r"\\n[^\\]*\|.*", "", body), tr.get("description", ""), 0.6)
    rep.show()


def backgrounds():
    rep = Report("Backgrounds")
    for o, t in pair(load_ours("backgrounds"), load_theirs("Backgrounds"), rep):
        n = o["name"]
        same(rep, n, "abilities", o.get("abilities"), ", ".join(a["name"] for a in t.get("ability_scores", [])),
             lambda x: ",".join(sorted(s.strip().lower()[:3] for s in str(x).split(","))))
        same(rep, n, "feat", o.get("feat"), t.get("feat", {}).get("feat", {}).get("name", "") if isinstance(t.get("feat"), dict) else t.get("feat"),
             lambda x: key(str(x)))
        sk = sorted(p["name"].split(": ")[1] for p in t.get("proficiencies", []) if p["name"].startswith("Skill"))
        same(rep, n, "skills", o.get("skills"), ", ".join(sk), lambda x: ",".join(sorted(s.strip().lower() for s in str(x).split(","))))
    rep.show()


def classes():
    rep = Report("Classes")
    for o, t in pair(load_ours("classes"), load_theirs("Classes"), rep):
        n = o["name"]
        same(rep, n, "hit die", o.get("hitdie"), f"d{t.get('hit_die')}")
        same(rep, n, "saves", o.get("saves"), ", ".join(s["name"] for s in t.get("saving_throws", [])),
             lambda x: ",".join(sorted(s.strip().lower()[:3] for s in str(x).split(","))))
    rep.show()


for fn in (spells, monsters, items, magic, feats, features, species, backgrounds, classes):
    fn()
