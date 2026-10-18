"""Lint dialogue lines against the text box in text.c dialogue().

Finds every `{ PT_xxx|NONE, "text" }` in the given files (missions.c, or design
docs that hold paste-ready dialogue) and lays it out exactly like dialogue():
greedy word wrap, 23 columns next to a portrait or 28 without, 5 rows. Any text
that does not fit is reported, since the game silently drops it.
Usage: python3 -I deadwars/docs/design/tools/check_dialogue.py deadwars/src/missions.c [more files...]
"""
import re, sys

ROWS = 5

def layout(text, tw):
    out, row, cx, i = [""], 0, 0, 0
    while i < len(text) and row < ROWS:
        j = i
        while j < len(text) and text[j] not in " \n":
            j += 1
        word = text[i:j]
        if cx + len(word) > tw:
            row, cx = row + 1, 0
            if row >= ROWS:
                break
            out.append("")
        if len(word) > tw:
            return None, "word longer than %d columns: %r" % (tw, word)
        out[row] += word
        cx += len(word)
        i = j
        if i < len(text) and text[i] == "\n":
            row, cx = row + 1, 0
            out.append("")
            i += 1
        elif i < len(text) and text[i] == " ":
            if cx < tw:
                out[row] += " "
                cx += 1
            i += 1
    if i < len(text):
        return out, "overflows %d rows, dropped: %r" % (ROWS, text[i:])
    return out, None

bad = 0
for path in sys.argv[1:]:
    src = open(path).read()
    for m in re.finditer(r'\{\s*(PT_\w+|NONE)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}', src):
        who, text = m.group(1), m.group(2).replace("\\n", "\n").replace('\\"', '"')
        line = src.count("\n", 0, m.start()) + 1
        _, err = layout(text, 28 if who == "NONE" else 23)
        if err:
            bad += 1
            print("%s:%d: %s %s" % (path, line, who, err))
print("dialogue: %s" % ("OK" if not bad else "%d problem(s)" % bad))
sys.exit(1 if bad else 0)
