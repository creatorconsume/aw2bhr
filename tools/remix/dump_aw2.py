"""Dump AW2 (USA) campaign maps / units / COs / unit stats in human readable form.

usage: python3 -I tools/remix/dump_aw2.py ROM [maps|units|cos|terrain|all] [map ids...]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from aw2lib import *  # noqa: E402,F401,F403

LETTER = {1: ".", 2: "~", 3: "^", 4: "T", 5: "=", 6: "c", 7: " ", 8: "H", 10: "a", 11: "p",
          12: "#", 13: ",", 14: "b", 15: "P", 16: "P", 17: "S", 18: "s", 19: "r", 20: "L"}


def dump_map(rom, m):
    e = MapEntry(rom, m)
    print("== map %d (0x%02X) %r  entry@0x%06X" % (m, m, text(rom, e.name_text), e.o))
    print("   terrain normal=0x%08X hard=0x%08X  units normal=0x%08X hard=0x%08X" %
          (e.terrain_normal, e.terrain_alt, e.units_normal, e.units_alt))
    print("   COs=%s armies=%s teams=%s fog=%d day_limit=%d par_days=%d/%d chapter=%d" % (
        [CO_NAMES[c] if c < len(CO_NAMES) else ("choose" if c == 0xFF else c) for c in e.cos],
        e.armies, e.teams, e.raw[0x17], u16(e.raw, 0x24), u16(e.raw, 0x20), u16(e.raw, 0x22), e.chapter))
    if not e.terrain_normal:
        return
    w, h, g, cl = read_terrain(rom, e.terrain_normal)
    print("   %dx%d (LZ77 %d bytes)" % (w, h, cl))
    for y in range(h):
        row = []
        for x in range(w):
            t, own = tile_terrain(rom, g[y][x])
            ch = LETTER.get(t, "?")
            if own and t in (6, 8, 10, 11, 14, 20):
                ch = str(own) if t != 8 else "H"
            row.append(ch)
        print("   %2d %s   %s" % (y, "".join(row), " ".join("%03x" % v for v in g[y])))
    if e.units_normal:
        us, ln = read_units(rom, e.units_normal)
        for pl, r in us:
            print("   P%d %-10s (%2d,%2d) hp=%3d ammo=%d fuel=%d ai=%d raw=%s" % (
                pl, UNIT_NAMES.get(r[2]), r[0], r[1], r[4], r[5], r[6], r[9], r.hex()))


def dump_units(rom):
    for t in range(UNIT_SLOTS):
        if not UNIT_NAMES[t]:
            continue
        s = unit_stats(rom, t)
        print("%2d %-10s cost=%5d mv=%d ammo=%d vis=%d rng=%d-%d fuel=%d domain=%d movetype=%d co_class=%d" % (
            t, s["name"], s["cost"], s["move"], s["ammo"], s["vision"], s["min_range"], s["max_range"],
            s["fuel"], s["domain"], s["movetype"], s["co_class"]))


def dump_cos(rom):
    for c in range(len(CO_NAMES)):
        o = CO_TABLE + c * CO_ENTRY_SIZE
        r = rom[o:o + CO_ENTRY_SIZE]
        print("%2d %-7s COP stars=%d SCOP stars=%d army=%d" % (c, text(rom, u32(r, 0)), u32(r, 0xC), u32(r, 0x10), r[0x16]))
        for b in range(3):
            bo = 0x38 + b * 0x44
            name = text(rom, u32(r, bo)) if u32(r, bo) else "(day-to-day)"
            mods = []
            for i in range(8):
                p = u32(r, bo + 0x24 + 4 * i)
                a, d, mv, rg = struct.unpack_from("<hhhh", rom, off(p))
                if a or d or mv or rg:
                    mods.append("%s:atk%+d/def%+d/mv%+d/rng%+d" % (CO_SLOT_NAMES[i], a, d, mv, rg))
            print("    %-17s fn=0x%08X luck=+%d/-%d counter%+d cost%+d capture%+d %s" % (
                name, u32(r, bo + 4), struct.unpack_from("<h", r, bo + 0xE)[0], struct.unpack_from("<h", r, bo + 0x10)[0],
                struct.unpack_from("<h", r, bo + 0x12)[0], struct.unpack_from("<h", r, bo + 0x14)[0],
                struct.unpack_from("<h", r, bo + 0x16)[0], " ".join(mods)))


def dump_terrain(rom):
    for t in range(1, 32):
        o = TERRAIN_TABLE + 20 * t
        print("0x%02X %-12s def=%d move(foot,mech,tread,tire,air,sea,lander)=%s" % (
            t, text(rom, u16(rom, o + 0xC)), rom[o + 0x10],
            [rom[MOVECOST_CLEAR + mt * 32 + t] for mt in range(7)]))


def main():
    rom = open(sys.argv[1], "rb").read()
    what = sys.argv[2] if len(sys.argv) > 2 else "all"
    ids = [int(a, 0) for a in sys.argv[3:]] or OS_CAMPAIGN
    if what in ("maps", "all"):
        for m in ids:
            dump_map(rom, m)
    if what in ("units", "all"):
        dump_units(rom)
    if what in ("cos", "all"):
        dump_cos(rom)
    if what in ("terrain", "all"):
        dump_terrain(rom)


if __name__ == "__main__":
    main()
