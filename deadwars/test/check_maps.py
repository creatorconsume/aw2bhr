"""Sanity-check mission maps in src/missions.c: row widths, unit positions."""
import re, sys
src = open(sys.argv[1]).read()
impass = set("~#")
for m in re.finditer(r"static const char \*const (m\d)_rows\[\] = \{(.*?)\};", src, re.S):
    name, body = m.group(1), m.group(2)
    rows = re.findall(r'"([^"]*)"', body)
    widths = {len(r) for r in rows}
    ok = len(widths) == 1
    w = rows[0].__len__()
    print(f"{name}: {w}x{len(rows)} {'OK' if ok else 'BAD WIDTHS ' + str([len(r) for r in rows])}")
    um = re.search(rf"{name}_units\[\] = \{{(.*?)END", src, re.S).group(1)
    seen = set()
    for t, x, y, side in re.findall(r"\{ (UT_\w+), (\d+), (\d+), (\w)", um):
        x, y = int(x), int(y)
        if y >= len(rows) or x >= w:
            print("  OUT OF MAP", t, x, y); continue
        c = rows[y][x]
        veh = t in ("UT_PICKUP", "UT_BUS", "UT_ARMOR")
        if c in impass or (veh and c in "^rw"):
            print("  BAD TERRAIN", t, x, y, repr(c))
        if (x, y) in seen:
            print("  OVERLAP", t, x, y)
        seen.add((x, y))
