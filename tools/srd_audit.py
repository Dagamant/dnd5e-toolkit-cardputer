# Lists every table in the SRD data (runs of "a | b" lines) and flags ones that look broken.
import sys, os, re, glob
d = sys.argv[1]; show_all = "-a" in sys.argv
n_tab = n_flag = 0
for path in sorted(glob.glob(d + "/*.txt")):
    rec = None
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("name: "): rec = line[6:]
        if ": " not in line: continue
        key, val = line.split(": ", 1)
        if " | " not in val: continue
        lines = val.split("\\n")
        i = 0
        while i < len(lines):
            if " | " not in lines[i]: i += 1; continue
            j = i
            while j < len(lines) and " | " in lines[j]: j += 1
            block = lines[i:j]; pre = lines[max(0, i - 4):i]; post = lines[j:j + 2]
            counts = [l.count(" | ") + 1 for l in block]
            flags = []
            if len(set(counts)) > 1: flags.append(f"cols {counts}")
            short = [p for p in pre if p and len(p.split()) <= 3 and not p.endswith((".", ":"))]
            if len(short) >= 2: flags.append(f"split header? {short}")
            if any(c.strip() == "" for l in block for c in l.split(" | ")): flags.append("empty cell")
            if post and post[0] and len(post[0].split()) <= 4 and not post[0].endswith("."): flags.append(f"stray after: {post[0]!r}")
            if len(block) == 1: flags.append("single row")
            n_tab += 1
            if flags or show_all:
                n_flag += bool(flags)
                print(f"=== {os.path.basename(path)} | {rec} | {key} | {'; '.join(flags)}")
                for p in pre[-2:]: print("   .. " + p[:110])
                for b in block[:8]: print("   " + b[:150])
                if len(block) > 8: print(f"   ... {len(block)} rows")
                for p in post[:1]: print("   .. " + p[:110])
            i = j
print(f"{n_tab} tables, {n_flag} flagged", file=sys.stderr)
