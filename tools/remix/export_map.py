"""Export ORIGINAL AW2 maps into the remix map text format (docs/remix/MAP_FORMAT.md).

usage:
  python3 -I tools/remix/export_map.py [--rom BASE.gba] [--outdir DIR] [--variant normal|hard|both] IDS...
  python3 -I tools/remix/export_map.py --rom BASE.gba --orig            # 138-142 + hard 172-176 -> maps/orig/
  python3 -I tools/remix/export_map.py --rom BASE.gba --roundtrip [IDS] # export->parse->rebuild self-test

IDS: map table indices, e.g. 139 141 or 138-142.  Files are named <id>_<slug>[_hard].txt.
'--variant both' writes a _hard file too when the entry has a hard variant (+0x30/+0x38).
The hard ("Advance") campaign plays maps 172-179 in the campaign slots of 138-145
(campaign flow nodes 34-41 mirror nodes 0-7): 138->172 ... 142->176, see MAP_FORMAT.md.
"""
import argparse
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from aw2lib import ROM_SHA1, MAP_COUNT, MapEntry, text, u32  # noqa: E402
import mapfile  # noqa: E402

DEFAULT_ROM = os.environ.get("AW2_BASE_ROM") or (
    "/tmp/claude-0/-home-user/86302a06-deac-5458-85c6-a93e0ff517ea/scratchpad/rom/"
    "Advance Wars 2 - Black Hole Rising (USA).gba")
ORIG_SET = [(m, "normal") for m in range(138, 143)] + [(m, "hard") for m in range(172, 177)]
HARD_COUNTERPART = {138 + i: 172 + i for i in range(8)}   # campaign slot -> Advance Campaign map


def parse_ids(args):
    ids = []
    for a in args:
        for part in a.split(","):
            if not part:
                continue
            m = re.fullmatch(r"(\d+)-(\d+)", part)
            ids += list(range(int(m.group(1)), int(m.group(2)) + 1)) if m else [int(part, 0)]
    return ids


def slug(name):
    s = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")
    return s or "map"


def load_rom(path):
    rom = open(path, "rb").read()
    if hashlib.sha1(rom).hexdigest() != ROM_SHA1:
        sys.exit("error: %s is not the expected AW2 (USA) ROM (sha1 %s)" % (path, ROM_SHA1))
    return rom


def has_variant(rom, m, variant):
    e = MapEntry(rom, m)
    if variant == "normal":
        return bool(e.terrain_normal)
    return bool(e.terrain_alt or e.units_alt)


def filename(rom, m, variant):
    e = MapEntry(rom, m)
    nm = text(rom, e.name_text) if e.name_text else "map"
    return "%d_%s%s.txt" % (m, slug(nm), "_hard" if variant == "hard" else "")


def export_one(rom, m, variant, outdir):
    spec = mapfile.export_spec(rom, m, variant)
    note = "Exported from the ORIGINAL ROM: map %d %r, %s variant." % (m, spec.name, variant)
    if variant == "normal" and m in HARD_COUNTERPART:
        note += "\nAdvance (hard) campaign plays map %d in this campaign slot." % HARD_COUNTERPART[m]
    if variant == "hard" and m in HARD_COUNTERPART.values():
        slot = [k for k, v in HARD_COUNTERPART.items() if v == m][0]
        note += "\nAdvance (hard) campaign map; occupies the campaign slot of normal map %d." % slot
    if variant == "hard" and not u32(rom, mapfile.terrain_ptr_off(m, 1)):
        note += "\n(+0x30 is NULL: the game uses the normal terrain for the hard variant.)"
    txt = mapfile.format_spec(spec, comment=note)
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, filename(rom, m, variant))
    open(path, "w", encoding="utf-8").write(txt)
    return path


def roundtrip(rom, m, variant, tiler):
    """export -> text -> parse -> resolve; terrain and unit bytes must equal the originals."""
    spec = mapfile.export_spec(rom, m, variant)
    txt = mapfile.format_spec(spec)
    spec2 = mapfile.parse_text(txt, "%d_%s.txt" % (m, variant))
    R = mapfile.resolve(spec2, rom, rom, tiler)
    w, h, tiles, tp, ub, up = mapfile.read_map_variant(rom, m, variant)
    ok = not R.errors and R.terrain_bytes == mapfile.encode_terrain(w, h, tiles) and R.units_bytes == ub
    # also: symbols-only (no [tiles]) must reproduce the same tiles (unchanged types keep the original)
    spec3 = mapfile.parse_text(re.sub(r"^\[tiles\].*?(?=^\[units\])", "", txt, flags=re.S | re.M), "notiles.txt")
    R3 = mapfile.resolve(spec3, rom, rom, tiler)
    ok3 = not R3.errors and R3.terrain_bytes == R.terrain_bytes
    return ok and ok3, R, (R3.errors if not ok3 else [])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default=DEFAULT_ROM)
    ap.add_argument("--outdir", default=os.path.join(HERE, "maps", "orig"))
    ap.add_argument("--variant", choices=("normal", "hard", "both"), default="normal")
    ap.add_argument("--orig", action="store_true", help="export 138-142 (normal) and 172-176 (hard)")
    ap.add_argument("--roundtrip", action="store_true", help="self-test instead of writing files")
    ap.add_argument("ids", nargs="*")
    a = ap.parse_args()
    rom = load_rom(a.rom)
    if a.roundtrip:
        tiler = mapfile.AutoTiler(rom)
        targets = []
        ids = parse_ids(a.ids) or [m for m in range(1, MAP_COUNT)]
        for m in ids:
            for v in ("normal", "hard"):
                if has_variant(rom, m, v):
                    targets.append((m, v))
        bad = 0
        for m, v in targets:
            ok, R, extra = roundtrip(rom, m, v, tiler)
            if not ok:
                bad += 1
                print("FAIL map %d %s: %s %s" % (m, v, R.errors, extra))
        print("round trip: %d/%d map variants byte-identical (terrain + unit list)" % (len(targets) - bad, len(targets)))
        sys.exit(1 if bad else 0)
    if a.orig:
        targets = ORIG_SET
    else:
        ids = parse_ids(a.ids)
        if not ids:
            ap.error("give map ids or --orig")
        vs = ("normal", "hard") if a.variant == "both" else (a.variant,)
        targets = [(m, v) for m in ids for v in vs if has_variant(rom, m, v) or (v == a.variant)]
    for m, v in targets:
        if not has_variant(rom, m, v):
            print("skip map %d %s: no such variant" % (m, v))
            continue
        print("wrote", export_one(rom, m, v, a.outdir))


if __name__ == "__main__":
    main()
