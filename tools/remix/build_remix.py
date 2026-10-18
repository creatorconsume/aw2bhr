"""Build "Advance Wars 2 - Remix" (+ a BPS patch) from the original AW2 (USA) ROM.

usage:
  python3 -I tools/remix/build_remix.py [options]

  --rom PATH          original ROM (sha1 14dd0b22...; default $AW2_BASE_ROM or the scratchpad copy)
  --out PATH          output ROM   (default <scratch>/remix/Advance Wars 2 - Remix.gba)
  --bps PATH          output patch (default <scratch>/remix/aw2-remix.bps); --no-bps to skip
  --only-maps IDS     only apply these map files, e.g. 139,141 or 138-142; 'none' = no maps
  --maps-dir DIR      map text files (default tools/remix/maps; only *.txt directly inside)
  --no-balance        skip tools/remix/balance.py
  --no-qol            skip the speed / animation patches
  --visual N          new-save default battle animation: 0 No Visual (default), 1 A, 2 B, 3 C
  --move-speed N      map move speed px/frame for every unit (stock 2; doubled when Visual != A)
  --start-map ID      TEST ROM: Campaign > New starts directly at mission ID (138-179), see
                      docs/remix/BUILD.md.  Output defaults to '... (test start ID).gba', no BPS.
  --resolved-dir DIR  also write each map file with its final (auto-tiled) [tiles] grid
  --alloc-start HEX   first free-space offset handed out (default 0x620000)
  --strict            treat map warnings as errors

Pipeline (each step logs what it changes):
  1. load + sha1-check the base ROM, work on a bytearray copy
  2. free-space allocator over [alloc-start, 0x800000) (0xFF-filled tail of the 8 MiB ROM), 4-byte aligned
  3. QoL: No Visual by default (0x016A6A), faster map movement (0x49CD88 + 36*type + 0x18)
  4. balance.apply(rom, alloc) from tools/remix/balance.py when present
  5. map files tools/remix/maps/<id>_<slug>.txt -> LZ77 terrain + unit list in free space, entry repointed
  6. optional --start-map test patch (campaign flow node 0, data only)
  7. safety checks (header, idle loop, unique terrain pointers), write ROM + BPS, verify BPS round trip
"""
import argparse
import glob
import hashlib
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from aw2lib import (ROM_SHA1, ROM_BASE, MAP_TABLE, MAP_ENTRY_SIZE, MAP_COUNT, MAPSPRITE_TABLE,  # noqa: E402
                    OPT_VISUAL_DEFAULT_INSN, FREE_SPACE_START, FREE_SPACE_END, UNIT_SLOTS,
                    lz77_compress, lz77_decompress, off, u16, u32)
import bps  # noqa: E402
import mapfile  # noqa: E402

SCRATCH = os.environ.get("AW2_SCRATCH", "/tmp/claude-0/-home-user/86302a06-deac-5458-85c6-a93e0ff517ea/scratchpad")
DEFAULT_ROM = os.environ.get("AW2_BASE_ROM") or os.path.join(SCRATCH, "rom", "Advance Wars 2 - Black Hole Rising (USA).gba")
DEFAULT_OUT = os.path.join(SCRATCH, "remix", "Advance Wars 2 - Remix.gba")
DEFAULT_BPS = os.path.join(SCRATCH, "remix", "aw2-remix.bps")

REMIX_DATA_START = 0x620000      # ROM_NOTES 4: leave a margin after the last real data (0x617EE4)
DEFAULT_MOVE_SPEED = 4           # px/frame; x2 with No Visual -> 8 px/frame = 2 frames per tile (see BUILD.md)
STOCK_MOVE_SPEED = 2
CAMPAIGN_FLOW = 0x615194         # gUnknown_08615194: 48-byte campaign nodes, node i = map 138+i (0..41)
FLOW_NODE_SIZE = 0x30
PROTECTED = [
    (0x000000, 0x0000C0, "cartridge header (game code AW2E, mGBA save/idle overrides key on it)"),
    (0x036E00, 0x036E40, "idle loop at 0x08036E08 (mGBA idle-loop optimisation)"),
]


class BuildError(Exception):
    pass


# ---------------------------------------------------------------------------------------------
class FreeSpace:
    """Bump allocator over 0xFF-filled free space; shared by every patch step.

    alloc(nbytes, align=4) -> ROM *file offset* (add 0x08000000 for a pointer).
    alloc.put(data, align=4, label=...) allocates and writes in one go.
    """

    def __init__(self, rom, start=REMIX_DATA_START, end=FREE_SPACE_END):
        assert FREE_SPACE_START <= start < end <= len(rom), (hex(start), hex(end))
        self.rom = rom
        self.start = self.pos = start
        self.end = end
        self.blocks = []        # (offset, size, label)

    def __call__(self, nbytes, align=4, label=None):
        if nbytes <= 0:
            raise BuildError("alloc of %d bytes" % nbytes)
        p = (self.pos + align - 1) // align * align
        if p + nbytes > self.end:
            raise BuildError("free space exhausted (%d bytes requested at 0x%X)" % (nbytes, p))
        if any(b != 0xFF for b in self.rom[p:p + nbytes]):
            raise BuildError("free space at 0x%X is not 0xFF (something wrote there without alloc)" % p)
        self.pos = p + nbytes
        self.blocks.append((p, nbytes, label or _caller_label()))
        return p

    def put(self, data, align=4, label=None):
        p = self(len(data), align, label)
        self.rom[p:p + len(data)] = data
        return p

    @property
    def used(self):
        return self.pos - self.start


def _caller_label():
    f = sys._getframe(2)
    return "%s:%d" % (os.path.basename(f.f_code.co_filename), f.f_lineno)


def patch(rom, o, expect, new, log, what):
    cur = bytes(rom[o:o + len(expect)])
    if cur != bytes(expect):
        raise BuildError("%s: expected %s at 0x%06X, found %s" % (what, bytes(expect).hex(), o, cur.hex()))
    rom[o:o + len(new)] = new
    log.append("%s: 0x%06X %s -> %s" % (what, o, bytes(expect).hex(), bytes(new).hex()))


# ---------------------------------------------------------------------------------------------
def apply_qol(rom, visual, move_speed, log):
    if visual is not None:
        assert 0 <= visual <= 3
        patch(rom, OPT_VISUAL_DEFAULT_INSN, b"\x01\x22", bytes([visual, 0x22]), log,
              "qol: new-save default battle animation = %s" % ["No Visual", "Visual A", "Visual B", "Visual C"][visual])
    if move_speed and move_speed != STOCK_MOVE_SPEED:
        for t in range(UNIT_SLOTS):
            o = MAPSPRITE_TABLE + 36 * t + 0x18
            if struct.unpack_from("<h", rom, o)[0] != STOCK_MOVE_SPEED:
                raise BuildError("qol: move speed of map sprite %d is not stock (0x%06X)" % (t, o))
            struct.pack_into("<h", rom, o, move_speed)
        log.append("qol: map move speed %d -> %d px/frame for all %d unit types (0x%06X + 36*t + 0x18); "
                   "doubled when Visual != A" % (STOCK_MOVE_SPEED, move_speed, UNIT_SLOTS, MAPSPRITE_TABLE))


def apply_balance(rom, alloc, log):
    path = os.path.join(HERE, "balance.py")
    if not os.path.exists(path):
        log.append("balance: tools/remix/balance.py not found, skipped")
        return
    import importlib
    try:
        balance = importlib.import_module("balance")
        lines = balance.apply(rom, alloc)
    except BuildError:
        raise
    except Exception as e:  # noqa: BLE001 - surface any balance.py failure as a clean build error
        raise BuildError("balance.py failed: %s: %s" % (type(e).__name__, e))
    log.extend(lines or [])
    if hasattr(balance, "verify"):
        problems = balance.verify(rom)
        if problems:
            raise BuildError("balance.verify failed:\n  " + "\n  ".join(problems))
        log.append("balance: verify() ok")


# ---------------------------------------------------------------------------------------------
def parse_ids(s):
    if s is None:
        return None
    if s.strip().lower() == "none":
        return set()
    ids = set()
    for part in s.split(","):
        part = part.strip()
        if not part:
            continue
        m = re.fullmatch(r"(\d+)-(\d+)", part)
        ids |= set(range(int(m.group(1)), int(m.group(2)) + 1)) if m else {int(part, 0)}
    return ids


def load_map_specs(maps_dir, only):
    specs = []
    for path in sorted(glob.glob(os.path.join(maps_dir, "*.txt"))):
        m = re.match(r"(\d+)_", os.path.basename(path))
        if not m:
            raise BuildError("%s: map files must be named <id>_<slug>.txt" % path)
        fid = int(m.group(1))
        if only is not None and fid not in only:
            continue
        try:
            spec = mapfile.parse_file(path)
        except mapfile.MapFileError as e:
            raise BuildError(str(e))
        if spec.map_id != fid:
            raise BuildError("%s: header says map %d but the file name says %d" % (path, spec.map_id, fid))
        specs.append(spec)
    if only:
        missing = sorted(only - {s.map_id for s in specs})
        if missing:
            print("WARNING: --only-maps: no map file for %s in %s" % (", ".join(map(str, missing)), maps_dir))
    seen = {}
    for s in specs:
        k = (s.map_id, s.variant)
        if k in seen:
            raise BuildError("two files for map %d %s: %s and %s" % (s.map_id, s.variant, seen[k], s.path))
        seen[k] = s.path
    return specs


def apply_maps(base, rom, alloc, specs, log, warn, resolved_dir=None, strict=False):
    if not specs:
        log.append("maps: no map files applied")
        return
    tiler = mapfile.AutoTiler(base)
    results, errors = [], []
    for s in specs:
        R = mapfile.resolve(s, base, rom, tiler)
        tag = "%s (map %d %s)" % (os.path.relpath(s.path, REPO) if s.path.startswith(REPO) else s.path, s.map_id, s.variant)
        errors += ["%s: %s" % (tag, e) for e in R.errors]
        ws = ["%s: %s" % (tag, w) for w in R.warnings]
        warn += ws
        if strict:
            errors += ws
        results.append((s, R, tag))
    if errors:
        raise BuildError("map validation failed:\n  " + "\n  ".join(errors))
    shared = {}    # per-entry fields shared by both variants
    for s, R, tag in results:
        eo = MAP_TABLE + s.map_id * MAP_ENTRY_SIZE
        vi = s.vi
        comp = lz77_compress(R.terrain_bytes)
        if lz77_decompress(comp, 0)[0] != R.terrain_bytes:
            raise BuildError("%s: LZ77 self-check failed" % tag)
        t_off = alloc.put(comp, label="map %d %s terrain (LZ77)" % (s.map_id, s.variant))
        ptr = ROM_BASE + t_off
        slots = [0x2C + 4 * vi] + ([0x00] if vi == 0 else [])
        for so in slots:
            struct.pack_into("<I", rom, eo + so, ptr)
        if R.units_bytes is None:
            struct.pack_into("<I", rom, eo + 0x34 + 4 * vi, 0)
            udesc = "unit list NULL"
        else:
            u_off = alloc.put(R.units_bytes, label="map %d %s units" % (s.map_id, s.variant))
            struct.pack_into("<I", rom, eo + 0x34 + 4 * vi, ROM_BASE + u_off)
            udesc = "%d units @0x%06X" % ((len(R.units_bytes) // 12) - 1 - len({u.owner for u in s.units}), u_off)
        log.append("map %d %s %r: terrain %dx%d LZ77 %d bytes @0x%06X (+%s), %s; %d auto-tiled cells" % (
            s.map_id, s.variant, s.name, s.width, s.height, len(comp), t_off,
            "/".join("0x%02X" % x for x in slots), udesc, len(R.auto_cells)))
        for x, y, old, new, why in R.auto_cells:
            log.append("    auto tile (%d,%d): %03x -> %03x (%s)" % (x, y, old, new, why))
        # entry options
        fields = []
        if s.cos is not None:
            fields.append(("cos", 0x3C, bytes(s.cos), 1))
        if s.fog is not None:
            fields.append(("fog", 0x17, bytes([s.fog]), 1))
        if s.day_limit is not None:
            fields.append(("day_limit", 0x24, struct.pack("<H", s.day_limit), 1))
        if s.par_days is not None:
            fields.append(("par_days", 0x20 + 2 * vi, struct.pack("<H", s.par_days), 0))
        for name, fo, val, is_shared in fields:
            if is_shared:
                k = (s.map_id, name)
                if k in shared and shared[k][0] != val:
                    raise BuildError("%s: %s conflicts with %s" % (tag, name, shared[k][1]))
                shared[k] = (val, tag)
            cur = bytes(rom[eo + fo:eo + fo + len(val)])
            if cur != val:
                rom[eo + fo:eo + fo + len(val)] = val
                log.append("    entry +0x%02X %s: %s -> %s" % (fo, name, cur.hex(), val.hex()))
        if resolved_dir:
            os.makedirs(resolved_dir, exist_ok=True)
            s.tiles = R.tiles
            open(os.path.join(resolved_dir, os.path.basename(s.path)), "w").write(
                mapfile.format_spec(s, comment="RESOLVED by build_remix.py from %s" % os.path.basename(s.path)))


# ---------------------------------------------------------------------------------------------
def apply_start_map(rom, target, alloc, log):
    """TEST ONLY: make Campaign > New start at mission `target` (data-only patch).

    The campaign flow table gUnknown_08615194 has 48-byte nodes; node i plays map 138+i
    (+0x00 u16 map id, +0x02..+0x05 mission flags, +0x06/+0x08 world-map x/y, +0x0C world-map
    struct, +0x10.. mission data/script pointers).  A new campaign starts at node 0 (Cleanup).
    We copy node (target-138) over node 0 but keep node 0's world-map position and +0x0C struct,
    so the world-map cursor still lands on the mission and the first A press selects it.  The game
    then loads the real map id with its own entry, events and intro dialogue.
    Hard ("Advance") maps 172-179 are played in normal mode here: their +0x30/+0x38 (hard)
    terrain/units are copied into +0x2C/+0x34 of the test ROM.
    """
    if not 138 <= target <= 179:
        raise BuildError("--start-map: campaign maps are 138-179")
    if u16(rom, CAMPAIGN_FLOW) != 138:
        raise BuildError("--start-map: campaign flow node 0 is not Cleanup (unexpected ROM)")
    i = target - 138
    node = bytearray(rom[CAMPAIGN_FLOW + FLOW_NODE_SIZE * i:CAMPAIGN_FLOW + FLOW_NODE_SIZE * (i + 1)])
    if u16(node, 0) != target:
        raise BuildError("--start-map: flow node %d is map %d, expected %d" % (i, u16(node, 0), target))
    node[0x06:0x10] = rom[CAMPAIGN_FLOW + 0x06:CAMPAIGN_FLOW + 0x10]
    rom[CAMPAIGN_FLOW:CAMPAIGN_FLOW + FLOW_NODE_SIZE] = node
    log.append("TEST start-map: campaign flow node 0 (0x%06X) <- node %d (map %d), keeping node 0 world-map "
               "x/y and +0x0C" % (CAMPAIGN_FLOW, i, target))
    if target >= 172:
        eo = MAP_TABLE + target * MAP_ENTRY_SIZE
        hard_t, hard_u = u32(rom, eo + 0x30), u32(rom, eo + 0x38)
        if hard_t and hard_t != u32(rom, eo + 0x2C):
            raise BuildError("--start-map %d: hard terrain differs from normal; not supported" % target)
        struct.pack_into("<I", rom, eo + 0x34, hard_u)
        struct.pack_into("<H", rom, eo + 0x20, u16(rom, eo + 0x22))
        log.append("TEST start-map: map %d hard units 0x%08X copied to the normal slot +0x34" % (target, hard_u))


# ---------------------------------------------------------------------------------------------
def final_checks(base, rom, log):
    if len(rom) != len(base):
        raise BuildError("ROM size changed")
    for a, b, what in PROTECTED:
        if rom[a:b] != base[a:b]:
            raise BuildError("protected region 0x%06X-0x%06X changed: %s" % (a, b, what))
    owner = {}
    for m in range(MAP_COUNT):
        eo = MAP_TABLE + m * MAP_ENTRY_SIZE
        p0, pn, ph = u32(rom, eo), u32(rom, eo + 0x2C), u32(rom, eo + 0x30)
        if p0 != pn:
            raise BuildError("map %d: +0x00 (0x%08X) != +0x2C (0x%08X)" % (m, p0, pn))
        for p in {pn, ph} - {0}:
            if p in owner and owner[p] != m:
                raise BuildError("terrain pointer 0x%08X shared by maps %d and %d" % (p, owner[p], m))
            owner[p] = m
            try:
                data, _ = lz77_decompress(rom, off(p))
            except Exception as e:  # noqa: BLE001
                raise BuildError("map %d: terrain at 0x%08X does not decompress: %s" % (m, p, e))
            if 2 + 2 * data[0] * data[1] != len(data) or len(data) > 0xA14:
                raise BuildError("map %d: bad terrain blob size" % m)
    log.append("checks: size, header, idle loop unchanged; %d terrain pointers unique; all decompress" % len(owner))


def diff_ranges(a, b, gap=16):
    rs = []
    i, n = 0, len(a)
    while i < n:
        if a[i] != b[i]:
            j = i
            last = i
            while j < n and j - last <= gap:
                if a[j] != b[j]:
                    last = j
                j += 1
            rs.append((i, last + 1))
            i = last + 1
        else:
            # fast skip of equal chunks
            k = i + 4096
            if k <= n and a[i:k] == b[i:k]:
                i = k
            else:
                i += 1
    return rs


def audit(base, rom, alloc, log):
    blocks = alloc.blocks
    lines = []
    for s, e in diff_ranges(base, rom):
        lab = [l for (p, n, l) in blocks if p < e and s < p + n]
        lines.append("  0x%06X-0x%06X %5d bytes  %s" % (s, e, e - s, "; ".join(lab) if lab else "in place"))
    log.append("audit: %d changed ranges, free space used %d bytes (0x%06X-0x%06X)" % (
        len(lines), alloc.used, alloc.start, alloc.pos))
    log.extend(lines)


# ---------------------------------------------------------------------------------------------
def inside_repo(path):
    p = os.path.realpath(path)
    return p == REPO or p.startswith(REPO + os.sep)


def build(a):
    log, warn = [], []
    base = open(a.rom, "rb").read()
    sha = hashlib.sha1(base).hexdigest()
    if sha != ROM_SHA1:
        raise BuildError("%s: sha1 %s is not the AW2 (USA) ROM %s" % (a.rom, sha, ROM_SHA1))
    rom = bytearray(base)
    log.append("base: %s sha1 %s" % (a.rom, sha))
    alloc = FreeSpace(rom, start=a.alloc_start)
    if not a.no_qol:
        apply_qol(rom, a.visual, a.move_speed, log)
    if not a.no_balance:
        apply_balance(rom, alloc, log)
    only = parse_ids(a.only_maps)
    specs = load_map_specs(a.maps_dir, only)
    apply_maps(base, rom, alloc, specs, log, warn, a.resolved_dir, a.strict)
    if a.start_map is not None:
        apply_start_map(rom, a.start_map, alloc, log)
    final_checks(base, rom, log)
    audit(base, rom, alloc, log)
    return base, bytes(rom), log, warn


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default=DEFAULT_ROM)
    ap.add_argument("--out")
    ap.add_argument("--bps")
    ap.add_argument("--no-bps", action="store_true")
    ap.add_argument("--only-maps")
    ap.add_argument("--maps-dir", default=os.path.join(HERE, "maps"))
    ap.add_argument("--no-balance", action="store_true")
    ap.add_argument("--no-qol", action="store_true")
    ap.add_argument("--visual", type=int, default=0, choices=(0, 1, 2, 3))
    ap.add_argument("--move-speed", type=int, default=DEFAULT_MOVE_SPEED)
    ap.add_argument("--start-map", type=int)
    ap.add_argument("--resolved-dir")
    ap.add_argument("--alloc-start", type=lambda s: int(s, 0), default=REMIX_DATA_START)
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args(argv)
    test = a.start_map is not None
    if a.out is None:
        a.out = DEFAULT_OUT if not test else os.path.join(
            os.path.dirname(DEFAULT_OUT), "Advance Wars 2 - Remix (test start %d).gba" % a.start_map)
    if a.bps is None and not test and not a.no_bps:
        a.bps = DEFAULT_BPS
    if a.no_bps:
        a.bps = None
    for p in (a.out, a.bps, a.resolved_dir):
        if p and inside_repo(p):
            sys.exit("error: refusing to write %s inside the repo (ROMs/patches stay out of git)" % p)
    try:
        base, rom, log, warn = build(a)
    except BuildError as e:
        sys.exit("BUILD FAILED: %s" % e)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    open(a.out, "wb").write(rom)
    out_sha = hashlib.sha1(rom).hexdigest()
    log.append("wrote %s (sha1 %s, crc32 %08X)%s" % (a.out, out_sha, zlib.crc32(rom) & 0xFFFFFFFF,
                                                       "  [TEST ROM]" if test else ""))
    if a.bps:
        p = bps.encode(base, rom, metadata=b"")
        back = bps.decode(base, p)
        if hashlib.sha1(back).hexdigest() != out_sha:
            sys.exit("BUILD FAILED: BPS round trip mismatch")
        open(a.bps, "wb").write(p)
        log.append("wrote %s (%d bytes); verified: applying it to the base ROM gives sha1 %s" % (a.bps, len(p), out_sha))
    logpath = os.path.splitext(a.out)[0] + ".build-log.txt"
    open(logpath, "w").write("\n".join(log + ["", "warnings:"] + (warn or ["  (none)"])) + "\n")
    if not a.quiet:
        print("\n".join(l for l in log if not l.startswith("  0x")))
    for w in warn:
        print("WARNING:", w)
    print("log: %s" % logpath)


if __name__ == "__main__":
    main()
