"""Self-tests for the remix build system (stdlib only; never writes into the repo).

usage: python3 -I tools/remix/test_remix.py [--rom BASE.gba]

  bps        varints, all four BPS actions (hand-made patch), encode/decode on the real ROM
  text       export -> text -> parse -> rebuild is byte-identical for every stock map variant
  rom        build a ROM from the exported originals (138-142, hard 172-176) and read the
             relocated terrain/unit lists back: identical to the original data
  validate   bad map files are rejected (unit on impassable terrain, overlap, owner, moved
             player unit, size change, bad symbol)
  build      default release build: protected regions intact, BPS applies to the same sha1
"""
import argparse
import glob
import hashlib
import os
import random
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from aw2lib import ROM_SHA1, MAP_TABLE, MAP_ENTRY_SIZE, lz77_decompress, off, u32  # noqa: E402
import bps  # noqa: E402
import build_remix  # noqa: E402
import export_map  # noqa: E402
import mapfile  # noqa: E402

FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def test_bps(base):
    print("bps")
    for n in (0, 1, 127, 128, 129, 16383, 16384, 16511, 16512, 2 ** 32 + 5):
        check(bps.dec_varint(bps.enc_varint(n), 0)[0] == n, "varint %d" % n)
    # hand-made patch exercising SourceRead, TargetRead, SourceCopy (+/- offsets) and overlapping TargetCopy
    src = bytes(range(64))
    body = bytearray()
    body += bps.enc_varint(((4 - 1) << 2) | 0)                      # SourceRead 4  -> 00 01 02 03
    body += bps.enc_varint(((3 - 1) << 2) | 1) + b"xyz"             # TargetRead 3
    body += bps.enc_varint(((5 - 1) << 2) | 2) + bps._signed(40)    # SourceCopy 5 from 40
    body += bps.enc_varint(((2 - 1) << 2) | 2) + bps._signed(-20)   # SourceCopy 2 from 45-20=25
    body += bps.enc_varint(((6 - 1) << 2) | 3) + bps._signed(4)     # TargetCopy 6 from 4 (overlap ok)
    tgt = bytes(range(4)) + b"xyz" + src[40:45] + src[25:27]
    t = bytearray(tgt)
    for i in range(6):
        t.append(t[4 + i])
    tgt = bytes(t)
    import zlib
    p = bytearray(b"BPS1") + bps.enc_varint(len(src)) + bps.enc_varint(len(tgt)) + bps.enc_varint(0) + body
    p += struct.pack("<II", zlib.crc32(src) & 0xFFFFFFFF, zlib.crc32(tgt) & 0xFFFFFFFF)
    p += struct.pack("<I", zlib.crc32(p) & 0xFFFFFFFF)
    check(bps.decode(src, bytes(p)) == tgt, "decode hand-made patch with all four actions")
    rnd = random.Random(1)
    for k in range(20):
        s = bytes(rnd.getrandbits(8) for _ in range(rnd.randint(0, 3000)))
        tt = bytearray(s)
        for _ in range(rnd.randint(0, 10)):
            if tt:
                i = rnd.randrange(len(tt))
                tt[i:i + rnd.randint(1, 50)] = bytes(rnd.getrandbits(8) for _ in range(rnd.randint(0, 60)))
        tt = bytes(tt)
        if bps.decode(s, bps.encode(s, tt)) != tt:
            check(False, "random encode/decode %d" % k)
            break
    else:
        check(True, "20 random encode/decode round trips (shrink/grow)")
    rom = bytearray(base)
    rom[0x016A6A] = 0
    rom[0x700000:0x700010] = b"0123456789abcdef"
    pt = bps.encode(base, bytes(rom))
    check(bps.decode(base, pt) == bytes(rom), "8 MiB ROM encode/decode (%d byte patch)" % len(pt))
    try:
        bps.decode(base[:-1] + b"\0", pt)
        check(False, "wrong source rejected")
    except bps.BPSError:
        check(True, "wrong source rejected")


def test_text(base):
    print("text")
    tiler = mapfile.AutoTiler(base)
    n = bad = 0
    for m in range(1, 192):
        for v in ("normal", "hard"):
            if export_map.has_variant(base, m, v):
                n += 1
                ok, R, extra = export_map.roundtrip(base, m, v, tiler)
                if not ok:
                    bad += 1
                    print("     map %d %s: %s %s" % (m, v, R.errors, extra))
    check(bad == 0, "%d stock map variants round-trip byte-identically (with and without [tiles])" % n)
    return tiler


def test_rom(base, tmp):
    print("rom")
    mdir = os.path.join(tmp, "maps")
    os.makedirs(mdir)
    for m, v in export_map.ORIG_SET:
        export_map.export_one(base, m, v, mdir)
    out = os.path.join(tmp, "rt.gba")
    build_remix.main(["--rom", ROM, "--maps-dir", mdir, "--out", out, "--no-bps", "--no-balance", "--no-qol", "--quiet"])
    rom = open(out, "rb").read()
    for m, v in export_map.ORIG_SET:
        vi = 0 if v == "normal" else 1
        eo = MAP_TABLE + m * MAP_ENTRY_SIZE
        nt, bt = u32(rom, eo + 0x2C + 4 * vi), u32(base, eo + 0x2C + 4 * vi)
        nu, bu = u32(rom, eo + 0x34 + 4 * vi), u32(base, eo + 0x34 + 4 * vi)
        same_t = lz77_decompress(rom, off(nt))[0] == lz77_decompress(base, off(bt))[0]
        same_u = (nu == 0 and bu == 0) or (nu and bu and mapfile.unit_list_bytes(rom, nu) == mapfile.unit_list_bytes(base, bu))
        moved = nt != bt and off(nt) >= build_remix.REMIX_DATA_START
        check(same_t and same_u and moved and (vi or u32(rom, eo) == nt),
              "map %d %s relocated to 0x%06X, terrain + units identical after rebuild" % (m, v, off(nt)))
    return mdir


def expect_error(base, tiler, txt, needle, what):
    try:
        spec = mapfile.parse_text(txt, "139_test.txt")
        R = mapfile.resolve(spec, base, base, tiler)
        errs = R.errors
    except mapfile.MapFileError as e:
        errs = [str(e)]
    check(any(needle in e for e in errs), "%s -> %r" % (what, (errs or ["no error"])[0][:90]))


def test_validate(base, tiler, mdir):
    print("validate")
    txt = open(glob.glob(os.path.join(mdir, "139_*.txt"))[0]).read()
    expect_error(base, tiler, txt.replace("P2    Infantry     12   3", "P2    Tank          9   0"),
                 "cannot stand on", "tank on a mountain")
    expect_error(base, tiler, txt.replace("P2    Infantry     12   3", "P2    Infantry     14   3"),
                 "overlaps", "overlapping units")
    expect_error(base, tiler, txt.replace("P2    Tank          3   5", "P3    Tank          3   5"),
                 "players", "owner slot beyond player count")
    expect_error(base, tiler, txt.replace("P1    Tank          2   7", "P1    Tank          5   7"),
                 "events_checked", "moved player unit without events_checked")
    expect_error(base, tiler, txt.replace("size       15x10", "size       16x10"),
                 "", "size change")
    expect_error(base, tiler, txt.replace("T  T  ~  ~  r", "T  T  ~  ~  Q", 1), "unknown terrain symbol", "bad symbol")
    expect_error(base, tiler, txt.replace("H2 c2", "H2 c3", 1), "players", "property owner beyond player count")
    ok_txt = txt.replace("P1    Tank          2   7", "P1    Tank          5   7").replace(
        "par_days", "events_checked yes\npar_days")
    spec = mapfile.parse_text(ok_txt, "139_ok.txt")
    R = mapfile.resolve(spec, base, base, tiler)
    check(not R.errors and any("events_checked" in w for w in R.warnings), "'events_checked yes' accepts the moved unit")


def test_build(base, tmp):
    print("build")
    out, pt = os.path.join(tmp, "r.gba"), os.path.join(tmp, "r.bps")
    build_remix.main(["--rom", ROM, "--out", out, "--bps", pt, "--quiet"])
    rom = open(out, "rb").read()
    for a, b, what in build_remix.PROTECTED:
        check(rom[a:b] == base[a:b], "unchanged: " + what)
    check(rom[0x016A6A] == 0, "No Visual default")
    check(struct.unpack_from("<h", rom, 0x49CD88 + 0x18)[0] == build_remix.DEFAULT_MOVE_SPEED, "move speed")
    check(hashlib.sha1(bps.decode(base, open(pt, "rb").read())).digest() == hashlib.sha1(rom).digest(),
          "BPS applies to the output sha1")
    try:
        build_remix.main(["--rom", ROM, "--out", os.path.join(HERE, "x.gba"), "--quiet"])
        check(False, "refuses to write into the repo")
    except SystemExit as e:
        check("refusing" in str(e), "refuses to write into the repo")


ROM = None


def main():
    global ROM
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", default=build_remix.DEFAULT_ROM)
    a = ap.parse_args()
    ROM = a.rom
    base = open(ROM, "rb").read()
    assert hashlib.sha1(base).hexdigest() == ROM_SHA1
    tmp = tempfile.mkdtemp(prefix="aw2remix-test-", dir=build_remix.SCRATCH if os.path.isdir(build_remix.SCRATCH) else None)
    try:
        test_bps(base)
        tiler = test_text(base)
        mdir = test_rom(base, tmp)
        test_validate(base, tiler, mdir)
        test_build(base, tmp)
    finally:
        shutil.rmtree(tmp)
    print("\n%s" % ("ALL TESTS PASSED" if not FAILS else "%d FAILED: %s" % (len(FAILS), FAILS)))
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
