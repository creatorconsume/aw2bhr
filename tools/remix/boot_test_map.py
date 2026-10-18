"""Boot a (test) ROM headless to the first player turn of a campaign mission; save state + screenshot.

usage:
  python3 -I tools/remix/boot_test_map.py ROM OUTPREFIX [--map ID] [--gbadbg PATH]

Drives tools/remix/gbadbg (built from gbadbg.c, script read from stdin) interactively:
  1. power-on -> title -> Start -> Campaign -> New (fixed key presses, as in make_test_roms.py),
  2. presses A (every 44 frames) through the prologue / world map until the map id byte
     0x03003FC2 equals ID (the world-map A press that selects the mission writes it),
  3. advances the mission's intro dialogue with A, probing after each line whether the player
     has control: press Right (then Left) and check that the map cursor X (u16 0x030033E4)
     follows; the probe is repeated after a 60-frame pause before the next A, so A is never
     pressed on the live map,
  4. writes OUTPREFIX-map.ppm/.png (screenshot), OUTPREFIX.ss (mGBA savestate at the first player
     turn; use 'load OUTPREFIX.ss' in a gbadbg script) and checks that the map buffer in RAM
     (*0x03003F68) equals the terrain the ROM's map entry points to.
ID defaults to 138.  For --start-map test ROMs pass the same ID (build_remix.py --start-map ID).
Exit status 0 = reached player control on the expected map with matching terrain.
"""
import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from aw2lib import MAP_TABLE, MAP_ENTRY_SIZE, lz77_decompress, off, u32  # noqa: E402

SCRATCH = os.environ.get("AW2_SCRATCH", "/tmp/claude-0/-home-user/86302a06-deac-5458-85c6-a93e0ff517ea/scratchpad")
DEFAULT_GBADBG = os.environ.get("GBADBG") or os.path.join(SCRATCH, "gbadbg")
MAP_ID_ADDR = 0x03003FC2
CURSOR_X = 0x030033E4
MAPBUF_PTR = 0x03003F68
BOOT = ["wait 600", "press T 4", "wait 200", "press A 4", "wait 100", "press A 4", "wait 200",
        "press A 4", "wait 200"]


class Emu:
    def __init__(self, gbadbg, rom, prefix):
        self.prefix = prefix
        self.frames = 0
        self.p = subprocess.Popen([gbadbg, rom, "-", prefix], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.n = 0

    def cmd(self, *lines):
        self.n += 1
        tag = "SYNC%d" % self.n
        for l in lines:
            w = l.split()
            if w[0] == "wait":
                self.frames += int(w[1])
            elif w[0] == "press":
                self.frames += int(w[2]) + 2
        self.p.stdin.write("\n".join(lines) + "\necho %s\n" % tag)
        self.p.stdin.flush()
        out = []
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("gbadbg exited unexpectedly")
            if line.strip() == tag:
                return out
            out.append(line.rstrip("\n"))

    def read(self, addr, n):
        f = self.prefix + ".tmp.bin"
        self.cmd("dump 0x%X %d %s" % (addr, n, f))
        with open(f, "rb") as fh:
            data = fh.read()
        os.remove(f)
        return data

    def u8(self, addr):
        return self.read(addr, 1)[0]

    def u16(self, addr):
        return struct.unpack("<H", self.read(addr, 2))[0]

    def close(self):
        self.p.stdin.close()
        self.p.wait()


def has_control(e):
    x = e.u16(CURSOR_X)
    for k1, k2, d in ((">", "<", 1), ("<", ">", -1)):
        e.cmd("press %s 2" % k1, "wait 8")
        x1 = e.u16(CURSOR_X)
        if x1 == x + d:
            e.cmd("press %s 2" % k2, "wait 8")
            if e.u16(CURSOR_X) == x:
                return True
            return False
        if x1 != x:
            return False
    return False


def ppm_to_png(ppm):
    try:
        from PIL import Image
    except ImportError:
        return ppm
    png = ppm[:-4] + ".png"
    Image.open(ppm).save(png)
    return png


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("prefix")
    ap.add_argument("--map", type=int, default=138)
    ap.add_argument("--gbadbg", default=DEFAULT_GBADBG)
    ap.add_argument("--max-presses", type=int, default=700)
    a = ap.parse_args()
    rom = open(a.rom, "rb").read()
    e = Emu(a.gbadbg, a.rom, a.prefix)
    try:
        e.cmd(*BOOT)
        presses = 0
        while e.u8(MAP_ID_ADDR) != a.map:
            e.cmd("press A 4", "wait 40")
            presses += 1
            if presses > a.max_presses:
                print("FAIL: map id never became %d (now %d)" % (a.map, e.u8(MAP_ID_ADDR)))
                e.cmd("shot fail")
                return 1
        print("map id %d selected after %d A presses (frame %d)" % (a.map, presses, e.frames))
        e.cmd("wait 40")
        intro = 0
        while True:
            if has_control(e):
                break
            e.cmd("wait 60")
            if has_control(e):
                break
            e.cmd("press A 4", "wait 40")
            intro += 1
            if intro > a.max_presses:
                print("FAIL: no player control after %d intro presses" % intro)
                e.cmd("shot fail")
                return 1
        e.cmd("wait 30", "shot map", "save %s.ss" % a.prefix)
        print("player control after %d intro A presses (frame %d)" % (intro, e.frames))
        # verify the map buffer against the ROM entry
        mid = e.u8(MAP_ID_ADDR)
        buf = struct.unpack("<I", e.read(MAPBUF_PTR, 4))[0]
        w, h = e.read(buf, 2)
        ram = e.read(buf, 2 + 2 * w * h)
        eo = MAP_TABLE + mid * MAP_ENTRY_SIZE
        matches = []
        for vi, name in ((0, "normal"), (1, "hard")):
            p = u32(rom, eo + 0x2C + 4 * vi)
            if p and lz77_decompress(rom, off(p))[0] == ram:
                matches.append(name)
        ok = mid == a.map and bool(matches)
        print("RAM: map id %d, %dx%d map buffer @0x%08X %s ROM terrain of map %d (%s)" % (
            mid, w, h, buf, "matches" if matches else "DOES NOT match", mid, "/".join(matches) or "-"))
        print("wrote %s, %s.ss" % (ppm_to_png(a.prefix + "-map.ppm"), a.prefix))
        return 0 if ok else 1
    finally:
        e.close()


if __name__ == "__main__":
    sys.exit(main())
