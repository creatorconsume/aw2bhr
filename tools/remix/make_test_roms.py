"""Rebuild the verification ROMs used for docs/remix/ROM_NOTES.md (never writes into the repo).

usage: python3 -I tools/remix/make_test_roms.py ORIGINAL.gba OUTDIR
Produces in OUTDIR:
  t1_map.gba     Cleanup: BH Infantry(12,3)->Tank, OS Infantry(2,9)->Md Tank, forests at (6,6),(6,7)
                 (terrain recompressed + relocated to free space 0x620000, entry +0x00/+0x2C repointed)
  t2_fog.gba     Cleanup with fog flag (entry +0x17) set
  t3_visual.gba  new-save default Visual = "No Visual" (0x016A6A: 01 22 -> 00 22)
  t4_speed.gba   map move speed 8 px/frame in MAPSPRITE_TABLE (+0x18 of every entry)
  boot_cleanup.txt  gbadbg script: power-on -> new campaign -> Cleanup map, shots 'map' and 'options'
Run e.g.:  gbadbg OUTDIR/t1_map.gba OUTDIR/boot_cleanup.txt OUTDIR/t1   (writes t1-map.ppm, t1-options.ppm)
"""
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from aw2lib import *  # noqa: E402,F403


def boot_script():
    L = ["wait 600", "press T 4", "wait 200", "press A 4", "wait 100", "press A 4", "wait 200",
         "press A 4", "wait 200"]
    for _ in range(16 * 8 + 15 * 6):       # prologue + Cleanup intro dialogue
        L += ["press A 4", "wait 40"]
    L += ["press B 4", "wait 60", "shot map",
          # cursor right, A on empty tile -> map menu, Options, show Visual setting
          "press > 4", "wait 20", "press A 4", "wait 30", "press D 4", "wait 10", "press D 4", "wait 10",
          "press A 4", "wait 40", "shot options"]
    return "\n".join(L) + "\n"


def main():
    src, outdir = sys.argv[1], sys.argv[2]
    base = open(src, "rb").read()
    assert hashlib.sha1(base).hexdigest() == ROM_SHA1, "unexpected ROM"
    os.makedirs(outdir, exist_ok=True)

    # t1: terrain + units of Cleanup (map 138)
    rom = bytearray(base)
    e = MapEntry(rom, 138)
    data, _ = lz77_decompress(rom, off(e.terrain_normal))
    d = bytearray(data)
    w = d[0]

    def put(x, y, v):
        struct.pack_into("<H", d, 2 + 2 * (y * w + x), v)
    put(6, 6, 0x087); put(7, 6, 0x021); put(6, 7, 0x087); put(7, 7, 0x021)  # wood + shadowed plain
    comp = lz77_compress(bytes(d))
    new = 0x620000
    assert all(b == 0xFF for b in rom[new:new + len(comp)])
    rom[new:new + len(comp)] = comp
    struct.pack_into("<I", rom, e.o + 0x00, ROM_BASE + new)
    struct.pack_into("<I", rom, e.o + 0x2C, ROM_BASE + new)
    o = off(e.units_normal)
    while rom[o] != 0xFF:
        r = rom[o:o + 12]
        if r[0] == 12 and r[1] == 3:
            rom[o:o + 12] = unit_record(12, 3, 5, ai=r[9], rom=rom)
        if r[0] == 2 and r[1] == 9:
            rom[o:o + 12] = unit_record(2, 9, 3, ai=r[9], rom=rom)
        o += 12
    open(os.path.join(outdir, "t1_map.gba"), "wb").write(rom)

    rom = bytearray(base)
    rom[MAP_TABLE + 138 * MAP_ENTRY_SIZE + 0x17] = 1
    open(os.path.join(outdir, "t2_fog.gba"), "wb").write(rom)

    rom = bytearray(base)
    assert rom[OPT_VISUAL_DEFAULT_INSN:OPT_VISUAL_DEFAULT_INSN + 2] == b"\x01\x22"
    rom[OPT_VISUAL_DEFAULT_INSN] = 0x00
    open(os.path.join(outdir, "t3_visual.gba"), "wb").write(rom)

    rom = bytearray(base)
    for t in range(UNIT_SLOTS):
        struct.pack_into("<h", rom, MAPSPRITE_TABLE + 36 * t + 0x18, 8)
    open(os.path.join(outdir, "t4_speed.gba"), "wb").write(rom)

    open(os.path.join(outdir, "boot_cleanup.txt"), "w").write(boot_script())
    print("wrote test ROMs to", outdir)


if __name__ == "__main__":
    main()
