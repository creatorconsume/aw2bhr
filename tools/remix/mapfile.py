"""AW2 Remix map text format: parse, write, export from ROM, autotile, validate, encode.

Format spec: docs/remix/MAP_FORMAT.md.  Used by build_remix.py and export_map.py.
Run tools with: python3 -I tools/remix/<tool>.py
"""
import math
import os
import re
import struct
from collections import Counter, defaultdict

from aw2lib import (MAP_TABLE, MAP_ENTRY_SIZE, MAP_COUNT, UNIT_TABLE, UNIT_ENTRY_SIZE, UNIT_NAMES,
                    MOVECOST_CLEAR, TILE_TO_TERRAIN, CO_NAMES, MapEntry, lz77_decompress, off, text,
                    u16, u32)

FORMAT_VERSION = 1

# ---------------------------------------------------------------------------------------------
# symbols
PLAIN_SYMBOLS = {".": 1, "r": 2, "^": 3, "T": 4, "=": 5, "~": 7, "+": 12, ",": 13, "P": 15, "X": 16,
                 "S": 17, "s": 18, "%": 19}
PROPERTY_SYMBOLS = {"H": 8, "b": 14, "c": 6, "a": 10, "p": 11, "L": 20}
SPECIAL_SYMBOL = "?"
SYMBOL_OF_TYPE = {v: k for k, v in PLAIN_SYMBOLS.items()}
SYMBOL_OF_TYPE.update({v: k for k, v in PROPERTY_SYMBOLS.items()})
PROPERTY_TYPES = set(PROPERTY_SYMBOLS.values())
TERRAIN_LABEL = {1: "Plain", 2: "River", 3: "Mountain", 4: "Wood", 5: "Road", 6: "City", 7: "Sea", 8: "HQ",
                 10: "Airport", 11: "Port", 12: "Bridge", 13: "Shoal", 14: "Base", 15: "Pipeline",
                 16: "Pipe seam", 17: "Missile silo", 18: "Silo (used)", 19: "Reef", 20: "Lab"}

ARMY_NAMES = {0: "--", 1: "OS", 2: "BM", 3: "GE", 4: "YC", 5: "BH"}
ARMY_IDS = {v: k for k, v in ARMY_NAMES.items()}

# unit type codes used in map files (canonical name first)
UNIT_CODES = {
    1: ["Infantry", "inf"], 2: ["Mech", "mec"], 3: ["MdTank", "md", "mediumtank"], 5: ["Tank", "tnk"],
    6: ["Recon", "rcn"], 7: ["APC"], 8: ["Neotank", "neo"], 10: ["Artillery", "art", "arty"],
    11: ["Rockets", "rkt", "rocket"], 14: ["AntiAir", "aa", "aair", "a-air"], 15: ["Missiles", "msl", "missile"],
    16: ["Fighter", "ftr"], 17: ["Bomber", "bmb"], 19: ["BCopter", "bcop", "bcopter", "bc"],
    20: ["TCopter", "tcop", "tc"], 21: ["Battleship", "bship", "bs"], 22: ["Cruiser", "cru", "crs"],
    23: ["Lander", "lan", "lnd"], 24: ["Sub", "submarine"],
}
OVERLAY_LETTER = {1: "i", 2: "m", 3: "M", 5: "t", 6: "R", 7: "A", 8: "N", 10: "a", 11: "r", 14: "x",
                  15: "s", 16: "F", 17: "B", 19: "b", 20: "c", 21: "S", 22: "C", 23: "L", 24: "U"}


def _norm(s):
    return re.sub(r"[\s_\-.]", "", s.lower())


UNIT_LOOKUP = {}
for _t, _names in UNIT_CODES.items():
    for _n in _names + [UNIT_NAMES[_t]]:
        UNIT_LOOKUP[_norm(_n)] = _t
CO_LOOKUP = {_norm(n): i for i, n in enumerate(CO_NAMES)}
CO_LOOKUP["choose"] = 0xFF


class MapFileError(Exception):
    pass


# ---------------------------------------------------------------------------------------------
# data model
class Unit:
    __slots__ = ("owner", "utype", "x", "y", "hp", "ammo", "fuel", "ai", "extra", "line")

    def __init__(self, owner, utype, x, y, hp=100, ammo=99, fuel=99, ai=0, extra=None, line=0):
        self.owner, self.utype, self.x, self.y = owner, utype, x, y
        self.hp, self.ammo, self.fuel, self.ai = hp, ammo, fuel, ai
        self.extra = dict(extra or {})     # raw byte index -> value for bytes 3,7,8,10,11
        self.line = line

    def record(self):
        r = bytearray([self.x, self.y, self.utype, 0, self.hp, self.ammo, self.fuel, 0, 0, self.ai, 0, 0])
        for k, v in self.extra.items():
            r[k] = v
        return bytes(r)


class MapSpec:
    """Content of one map text file (or an export of a ROM map)."""

    def __init__(self):
        self.map_id = None
        self.variant = "normal"
        self.name = ""
        self.width = self.height = None
        self.players = None
        self.armies = None
        self.cos = None            # list of CO ids per slot (only the given slots)
        self.fog = None
        self.day_limit = None
        self.par_days = None
        self.events_checked = False
        self.symbols = None        # [y][x] -> token string
        self.tiles = None          # [y][x] -> int or None (auto); None grid = section absent
        self.units = []            # list of Unit
        self.units_none = False    # explicit NULL unit-list pointer
        self.path = None

    # -- pointer slots in the map entry for this variant
    @property
    def vi(self):
        return 0 if self.variant == "normal" else 1


def parse_token(tok, where):
    """terrain token -> (type, owner) or ('?', None)"""
    if tok == SPECIAL_SYMBOL:
        return ("?", None)
    if len(tok) == 1 and tok in PLAIN_SYMBOLS:
        return (PLAIN_SYMBOLS[tok], 0)
    if len(tok) == 2 and tok[0] in PROPERTY_SYMBOLS and tok[1] in "01234":
        return (PROPERTY_SYMBOLS[tok[0]], int(tok[1]))
    if len(tok) == 1 and tok in PROPERTY_SYMBOLS:
        raise MapFileError("%s: property %r needs an owner digit 0-4 (e.g. %s0 neutral, %s1 player 1)"
                           % (where, tok, tok, tok))
    raise MapFileError("%s: unknown terrain symbol %r" % (where, tok))


def _int(s, where, lo=None, hi=None):
    try:
        v = int(s, 0)
    except ValueError:
        raise MapFileError("%s: expected a number, got %r" % (where, s))
    if (lo is not None and v < lo) or (hi is not None and v > hi):
        raise MapFileError("%s: %d out of range %s..%s" % (where, v, lo, hi))
    return v


def parse_unit_type(s, where):
    if re.fullmatch(r"\d+", s):
        t = int(s)
        if UNIT_NAMES.get(t):
            return t
    t = UNIT_LOOKUP.get(_norm(s))
    if t is None:
        raise MapFileError("%s: unknown unit type %r (use e.g. %s)" % (
            where, s, ", ".join(v[0] for v in UNIT_CODES.values())))
    return t


def parse_file(path):
    with open(path, encoding="utf-8") as f:
        return parse_text(f.read(), path)


def parse_text(txt, path="<text>"):
    spec = MapSpec()
    spec.path = path
    section = None
    rows = {"terrain": [], "tiles": []}
    seen_sections = set()
    for ln, raw in enumerate(txt.splitlines(), 1):
        where = "%s:%d" % (os.path.basename(path), ln)
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        m = re.fullmatch(r"\[(\w+)\]", line)
        if m:
            section = m.group(1).lower()
            if section not in ("terrain", "tiles", "units"):
                raise MapFileError("%s: unknown section [%s]" % (where, section))
            if section in seen_sections:
                raise MapFileError("%s: duplicate section [%s]" % (where, section))
            seen_sections.add(section)
            continue
        toks = line.split()
        if section is None:
            key, args = toks[0].lower(), toks[1:]
            if key == "map":
                spec.map_id = _int(args[0], where, 1, MAP_COUNT - 1)
            elif key == "variant":
                if args[0].lower() not in ("normal", "hard"):
                    raise MapFileError("%s: variant must be normal or hard" % where)
                spec.variant = args[0].lower()
            elif key == "name":
                spec.name = " ".join(args)
            elif key == "size":
                m2 = re.fullmatch(r"(\d+)x(\d+)", args[0].lower())
                if not m2:
                    raise MapFileError("%s: size must look like 15x10" % where)
                spec.width, spec.height = int(m2.group(1)), int(m2.group(2))
            elif key == "players":
                spec.players = _int(args[0], where, 2, 4)
            elif key == "armies":
                try:
                    spec.armies = [ARMY_IDS[a.upper()] for a in args]
                except KeyError as e:
                    raise MapFileError("%s: unknown army %s (OS BM GE YC BH)" % (where, e))
            elif key == "cos":
                cos = []
                for a in args:
                    c = CO_LOOKUP.get(_norm(a))
                    if c is None:
                        if re.fullmatch(r"\d+", a) and int(a) < len(CO_NAMES):
                            c = int(a)
                        else:
                            raise MapFileError("%s: unknown CO %r" % (where, a))
                    cos.append(c)
                if not 1 <= len(cos) <= 4:
                    raise MapFileError("%s: cos takes 1-4 names" % where)
                spec.cos = cos
            elif key == "fog":
                v = args[0].lower()
                if v not in ("on", "off", "0", "1"):
                    raise MapFileError("%s: fog on|off" % where)
                spec.fog = 1 if v in ("on", "1") else 0
            elif key == "day_limit":
                spec.day_limit = _int(args[0], where, 0, 99)
            elif key == "par_days":
                spec.par_days = _int(args[0], where, 0, 999)
            elif key == "events_checked":
                spec.events_checked = args[0].lower() in ("yes", "1", "true")
            elif key == "format":
                if _int(args[0], where) != FORMAT_VERSION:
                    raise MapFileError("%s: unsupported format version %s" % (where, args[0]))
            else:
                raise MapFileError("%s: unknown header key %r" % (where, key))
        elif section == "terrain":
            rows["terrain"].append((where, toks))
        elif section == "tiles":
            rows["tiles"].append((where, toks))
        elif section == "units":
            if toks[0].lower() == "none" and len(toks) == 1:
                spec.units_none = True
                continue
            spec.units.append(parse_unit_line(toks, where, ln))
    if spec.map_id is None:
        raise MapFileError("%s: missing 'map <id>' header" % path)
    if spec.width is None:
        raise MapFileError("%s: missing 'size WxH' header" % path)
    if "terrain" not in seen_sections:
        raise MapFileError("%s: missing [terrain] section" % path)
    if spec.units_none and spec.units:
        raise MapFileError("%s: [units] has both 'none' and unit lines" % path)
    w, h = spec.width, spec.height
    # terrain grid
    if len(rows["terrain"]) != h:
        raise MapFileError("%s: [terrain] has %d rows, size says %d" % (path, len(rows["terrain"]), h))
    spec.symbols = []
    for where, toks in rows["terrain"]:
        if len(toks) != w:
            raise MapFileError("%s: [terrain] row has %d cells, size says %d" % (where, len(toks), w))
        for t in toks:
            parse_token(t, where)          # syntax check
        spec.symbols.append(toks)
    if "tiles" in seen_sections:
        if len(rows["tiles"]) != h:
            raise MapFileError("%s: [tiles] has %d rows, size says %d" % (path, len(rows["tiles"]), h))
        spec.tiles = []
        for where, toks in rows["tiles"]:
            if len(toks) != w:
                raise MapFileError("%s: [tiles] row has %d cells, size says %d" % (where, len(toks), w))
            row = []
            for t in toks:
                if re.fullmatch(r"-+|auto", t.lower()):
                    row.append(None)
                elif re.fullmatch(r"[0-9a-fA-F]{1,3}", t):
                    row.append(int(t, 16))
                else:
                    raise MapFileError("%s: bad tile id %r (3 hex digits, or --- for auto)" % (where, t))
            spec.tiles.append(row)
    return spec


def parse_unit_line(toks, where, ln):
    """P1 Infantry x y [hp ammo fuel ai] [bN=V ...]"""
    extra = {}
    plain = []
    for t in toks:
        m = re.fullmatch(r"b(\d+)=(\w+)", t)
        if m:
            idx = int(m.group(1))
            if idx not in (3, 7, 8, 10, 11):
                raise MapFileError("%s: only raw bytes b3,b7,b8,b10,b11 can be set" % where)
            extra[idx] = _int(m.group(2), where, 0, 255)
        else:
            plain.append(t)
    if len(plain) < 4:
        raise MapFileError("%s: unit line needs at least: owner type x y" % where)
    m = re.fullmatch(r"[pP]?([1-4])", plain[0])
    if not m:
        raise MapFileError("%s: owner must be P1..P4" % where)
    owner = int(m.group(1))
    utype = parse_unit_type(plain[1], where)
    x = _int(plain[2], where, 0, 255)
    y = _int(plain[3], where, 0, 255)
    vals = plain[4:]
    if len(vals) > 4:
        raise MapFileError("%s: too many fields (owner type x y hp ammo fuel ai)" % where)

    def field(i, default, lo, hi):
        if i >= len(vals) or vals[i] in ("-",):
            return default
        if vals[i].lower() == "max":
            return 99
        return _int(vals[i], where, lo, hi)
    hp = field(0, 100, 1, 100)
    ammo = field(1, 99, 0, 255)
    fuel = field(2, 99, 0, 255)
    ai = field(3, 0, 0, 255)
    return Unit(owner, utype, x, y, hp, ammo, fuel, ai, extra, ln)


# ---------------------------------------------------------------------------------------------
# ROM access
def entry_off(map_id):
    return MAP_TABLE + map_id * MAP_ENTRY_SIZE


def terrain_ptr_off(map_id, vi):
    return entry_off(map_id) + 0x2C + 4 * vi


def units_ptr_off(map_id, vi):
    return entry_off(map_id) + 0x34 + 4 * vi


def read_map_variant(rom, map_id, variant):
    """-> (w, h, tiles[y][x], terrain_ptr, unit_list_bytes or None, units_ptr)"""
    vi = 0 if variant == "normal" else 1
    tp = u32(rom, terrain_ptr_off(map_id, vi))
    if vi == 1 and tp == 0:
        tp = u32(rom, terrain_ptr_off(map_id, 0))     # game falls back to the normal terrain
    if not tp:
        raise MapFileError("map %d has no terrain" % map_id)
    data, _ = lz77_decompress(rom, off(tp))
    w, h = data[0], data[1]
    tiles = [[u16(data, 2 + 2 * (y * w + x)) for x in range(w)] for y in range(h)]
    up = u32(rom, units_ptr_off(map_id, vi))
    ub = unit_list_bytes(rom, up) if up else None
    return w, h, tiles, tp, ub, up


def unit_list_bytes(rom, ptr):
    o = start = off(ptr)
    while rom[o] != 0xFF:
        o += 12
    return bytes(rom[start:o + 12])


def decode_unit_list(b):
    units, owner = [], None
    for i in range(0, len(b), 12):
        r = b[i:i + 12]
        if r[0] == 0xFF:
            break
        if r[0] == 0xFE:
            owner = r[1]
            continue
        extra = {k: r[k] for k in (3, 7, 8, 10, 11) if r[k]}
        units.append(Unit(owner, r[2], r[0], r[1], r[4], r[5], r[6], r[9], extra))
    return units


def encode_unit_list(units):
    """Group by owner (ascending, stable within owner) -> FE/records/FF bytes."""
    out = bytearray()
    for p in sorted({u.owner for u in units}):
        out += bytes([0xFE, p]) + bytes(10)
        for u in units:
            if u.owner == p:
                out += u.record()
    out += bytes([0xFF]) + bytes(11)
    return bytes(out)


def encode_terrain(w, h, tiles):
    out = bytearray([w, h])
    for y in range(h):
        for x in range(w):
            out += struct.pack("<H", tiles[y][x])
    return bytes(out)


def tile_info(rom, t):
    v = rom[TILE_TO_TERRAIN + t]
    return v & 0x1F, v >> 5


def property_tile(rom, ttype, owner):
    for t in range(0x1C0, 0x1E0):
        if tile_info(rom, t) == (ttype, owner):
            return t
    raise MapFileError("no tile for terrain %d owner %d" % (ttype, owner))


def tile_symbol(rom, t):
    ty, own = tile_info(rom, t)
    if ty in PROPERTY_TYPES:
        return "%s%d" % (SYMBOL_OF_TYPE[ty], own)
    if own == 0 and ty in SYMBOL_OF_TYPE:
        return SYMBOL_OF_TYPE[ty]
    return SPECIAL_SYMBOL


# ---------------------------------------------------------------------------------------------
# export
def export_spec(rom, map_id, variant):
    e = MapEntry(rom, map_id)
    w, h, tiles, tp, ub, up = read_map_variant(rom, map_id, variant)
    s = MapSpec()
    s.map_id, s.variant = map_id, variant
    s.name = text(rom, e.name_text).strip() if e.name_text else ""
    s.width, s.height = w, h
    s.players = u16(e.raw, 0x18)
    s.armies = list(e.raw[0x40:0x40 + s.players])
    s.cos = list(e.raw[0x3C:0x3C + s.players])
    s.fog = 1 if e.raw[0x17] else 0
    s.day_limit = u16(e.raw, 0x24)
    s.par_days = u16(e.raw, 0x20 + 2 * (0 if variant == "normal" else 1))
    s.symbols = [[tile_symbol(rom, tiles[y][x]) for x in range(w)] for y in range(h)]
    s.tiles = [row[:] for row in tiles]
    s.units = decode_unit_list(ub) if ub is not None else []
    s.units_none = ub is None
    return s


def co_name(c):
    return "choose" if c == 0xFF else (CO_NAMES[c] if c < len(CO_NAMES) else str(c))


def unit_name(t):
    return UNIT_CODES[t][0] if t in UNIT_CODES else str(t)


def format_spec(s, rom=None, comment=None):
    L = []
    L.append("# Advance Wars 2 - Remix map file (format v%d). Spec: docs/remix/MAP_FORMAT.md" % FORMAT_VERSION)
    if comment:
        for c in comment.splitlines():
            L.append("# " + c)
    L.append("format     %d" % FORMAT_VERSION)
    L.append("map        %d" % s.map_id)
    L.append("variant    %s" % s.variant)
    if s.name:
        L.append("name       %s" % s.name)
    L.append("size       %dx%d             # must match the original map" % (s.width, s.height))
    if s.players is not None:
        L.append("players    %d                 # informational (entry +0x18)" % s.players)
    if s.armies:
        L.append("armies     %-17s # informational: army per slot (entry +0x40)" %
                 " ".join(ARMY_NAMES.get(a, str(a)) for a in s.armies))
    if s.cos is not None:
        L.append("cos        %-17s # CO per slot (entry +0x3C); 'choose' = player picks" %
                 " ".join(co_name(c) for c in s.cos))
    if s.fog is not None:
        L.append("fog        %s" % ("on" if s.fog else "off"))
    if s.day_limit is not None:
        L.append("day_limit  %-17d # 0 = none (entry +0x24)" % s.day_limit)
    if s.par_days is not None:
        L.append("par_days   %-17d # speed-rank par (entry +0x%02X)" % (s.par_days, 0x20 if s.variant == "normal" else 0x22))
    if s.events_checked:
        L.append("events_checked yes")
    w, h = s.width, s.height
    ruler = "#  x: " + " ".join("%-2d" % x for x in range(w))
    L.append("")
    L.append("[terrain]")
    L.append("# . plain  T wood  ^ mountain  = road  + bridge  r river  ~ sea  , shoal  % reef")
    L.append("# P pipeline  X pipe seam  S silo  s used silo  ? special (tile from [tiles])")
    L.append("# Hn HQ  cn city  bn base  an airport  pn port  Ln lab   (n = owner slot, 0 = neutral)")
    L.append(ruler)
    for y in range(h):
        L.append("      " + " ".join("%-2s" % t for t in s.symbols[y]).rstrip() + "   # y=%d" % y)
    if s.tiles is not None:
        L.append("")
        L.append("[tiles]")
        L.append("# exact graphic tile ids (hex). '---' = pick automatically from [terrain].")
        L.append("# A cell whose tile does not match its [terrain] symbol is re-picked automatically.")
        L.append("#  x: " + " ".join("%-3d" % x for x in range(w)))
        for y in range(h):
            L.append("     " + " ".join("---" if t is None else "%03x" % t for t in s.tiles[y]) + "   # y=%d" % y)
    L.append("")
    L.append("[units]")
    if s.units_none:
        L.append("none            # NULL unit-list pointer (no predeployed units)")
    else:
        # overlay
        occ = {(u.x, u.y): u for u in s.units}
        L.append("# overlay (comment only): <owner><letter>  i Inf m Mech M MdTank t Tank R Recon A APC N Neo")
        L.append("#   a Art r Rkt x AA s Msl F Ftr B Bmb b BCop c TCop S BShip C Cru L Lander U Sub")
        L.append("#" + ruler[1:])
        for y in range(h):
            cells = []
            for x in range(w):
                u = occ.get((x, y))
                cells.append("%d%s" % (u.owner, OVERLAY_LETTER.get(u.utype, "?")) if u else ("%-2s" % s.symbols[y][x]))
            L.append("#     " + " ".join(cells).rstrip())
        L.append("#")
        L.append("# owner type         x   y   hp ammo fuel  ai   (ammo/fuel 99 or 'max' = full)")
        for u in s.units:
            extra = " ".join("b%d=%d" % (k, v) for k, v in sorted(u.extra.items()))
            L.append(("  P%d    %-11s %3d %3d  %3d  %3d  %3d %3d   %s" % (
                u.owner, unit_name(u.utype), u.x, u.y, u.hp, u.ammo, u.fuel, u.ai, extra)).rstrip())
    return "\n".join(L) + "\n"


# ---------------------------------------------------------------------------------------------
# autotiler (learned from the stock maps of the base ROM)
COARSE = {1: "L", 2: "r", 3: "M", 4: "F", 5: "R", 6: "P", 7: "w", 8: "P", 9: "B", 10: "P", 11: "P", 12: "R",
          13: "s", 14: "P", 15: "p", 16: "p", 17: "S", 18: "S", 19: "w", 20: "P"}
D8 = [(0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1)]
# Woods: only the single-tree tiles are picked automatically (087 normal, 086 shadowed from the west);
# the big-forest clump tiles (024-027, 044-047, 064-067) only make sense in hand-made 2x2 groups.
AUTO_RESTRICT = {4: {0x086, 0x087}}


def _coarse(t):
    return "X" if t < 0 else COARSE.get(t, "B")


class AutoTiler:
    def __init__(self, base_rom):
        self.rom = base_rom
        self.used = Counter()
        self.exact = defaultdict(Counter)
        self.coarse = defaultdict(Counter)
        self.prior = defaultdict(Counter)
        self.cond = defaultdict(Counter)
        seen = set()
        for m in range(1, MAP_COUNT):
            o = entry_off(m)
            for vi in (0, 1):
                p = u32(base_rom, o + 0x2C + 4 * vi)
                if not p or p in seen:
                    continue
                seen.add(p)
                data, _ = lz77_decompress(base_rom, off(p))
                w, h = data[0], data[1]
                tiles = [[u16(data, 2 + 2 * (y * w + x)) for x in range(w)] for y in range(h)]
                tg = [[tile_info(base_rom, t) for t in row] for row in tiles]
                for y in range(h):
                    for x in range(w):
                        t = tiles[y][x]
                        ty, own = tg[y][x]
                        nb = self._nb(tg, w, h, x, y)
                        self.used[t] += 1
                        self.exact[(ty, own, nb)][t] += 1
                        self.coarse[(ty, own, tuple(map(_coarse, nb)))][t] += 1
                        self.prior[(ty, own)][t] += 1
                        for d in range(8):
                            self.cond[(t, d)][_coarse(nb[d])] += 1

    @staticmethod
    def _nb(tg, w, h, x, y):
        return tuple(tg[y + dy][x + dx][0] if 0 <= x + dx < w and 0 <= y + dy < h else -1 for dx, dy in D8)

    def pick(self, tg, w, h, x, y):
        ty, own = tg[y][x]
        if ty in PROPERTY_TYPES:
            return property_tile(self.rom, ty, own)
        allowed = AUTO_RESTRICT.get(ty)
        nb = self._nb(tg, w, h, x, y)
        for tab, key in ((self.exact, (ty, own, nb)), (self.coarse, (ty, own, tuple(map(_coarse, nb))))):
            c = tab.get(key)
            if c:
                cands = [(n, t) for t, n in c.items() if not allowed or t in allowed]
                if cands:
                    return max(cands)[1]
        # naive Bayes over the 8 neighbour classes
        best, bs = None, -1e18
        for t, n in self.prior[(ty, own)].items():
            if allowed and t not in allowed:
                continue
            s = math.log(n)
            for d in range(8):
                c = self.cond[(t, d)][_coarse(nb[d])]
                s += math.log((c + 0.1) / (n + 1.0))
            if s > bs:
                best, bs = t, s
        if best is None:
            raise MapFileError("no known tile for terrain type %d" % ty)
        return best

    def consistent(self, t, tg, w, h, x, y):
        """Is tile t plausible in this neighbourhood (each neighbour class seen often enough for t)?"""
        nb = self._nb(tg, w, h, x, y)
        total = self.used.get(t) or 1
        for d in range(8):
            c = self.cond[(t, d)][_coarse(nb[d])]
            if c < max(2, 0.003 * total):
                return False
        return True


# ---------------------------------------------------------------------------------------------
# resolve + validate
class Resolved:
    def __init__(self):
        self.tiles = None
        self.terrain_bytes = None
        self.units_bytes = None       # None = NULL pointer
        self.notes = []               # informational messages
        self.warnings = []
        self.errors = []
        self.auto_cells = []          # (x, y, old, new, reason)


def resolve(spec, base_rom, work_rom, autotiler):
    """Turn a parsed spec into final terrain/unit bytes, validating against the original map.

    base_rom: the original ROM (reference map).  work_rom: the ROM being built (unit stats/move
    costs may already be rebalanced, used for passability checks).
    """
    R = Resolved()
    E, W = R.errors, R.warnings
    mid, var = spec.map_id, spec.variant
    if not (1 <= mid < MAP_COUNT):
        E.append("map id %d out of range" % mid)
        return R
    try:
        ow, oh, otiles, _, oub, oup = read_map_variant(base_rom, mid, var)
    except MapFileError as e:
        E.append(str(e))
        return R
    w, h = spec.width, spec.height
    if (w, h) != (ow, oh):
        E.append("size %dx%d differs from the original %dx%d (map dimensions must stay unchanged)" % (w, h, ow, oh))
        return R
    if 2 + 2 * w * h > 0xA14:
        E.append("map too large for the 0xA14-byte map buffer")
    entry = MapEntry(base_rom, mid)
    players = u16(entry.raw, 0x18)
    armies = list(entry.raw[0x40:0x44])
    otg = [[tile_info(base_rom, t) for t in row] for row in otiles]

    # 1) target terrain types from symbols
    tg = [[None] * w for _ in range(h)]
    want_tile = [[None] * w for _ in range(h)]
    pinned = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            sym = parse_token(spec.symbols[y][x], "%s (%d,%d)" % (os.path.basename(spec.path or ""), x, y))
            given = spec.tiles[y][x] if spec.tiles is not None else otiles[y][x]
            if given is not None and not (0 <= given < 0x200 and tile_info(base_rom, given)[0] != 0):
                E.append("(%d,%d): tile %03x is not a valid map tile" % (x, y, given))
                given = None
            if sym[0] == "?":
                if given is None:
                    E.append("(%d,%d): '?' needs an explicit tile id in [tiles]" % (x, y))
                    tg[y][x] = otg[y][x]
                    continue
                tg[y][x] = tile_info(base_rom, given)
                want_tile[y][x] = given
                pinned[y][x] = True
                continue
            ty, own = sym
            if ty in PROPERTY_TYPES and own > players:
                E.append("(%d,%d): %s owned by slot %d but the map has %d players" % (
                    x, y, TERRAIN_LABEL[ty], own, players))
            tg[y][x] = (ty, own)
            if given is not None and tile_info(base_rom, given) == (ty, own):
                want_tile[y][x] = given
                pinned[y][x] = given != otiles[y][x]
            # else: auto (want_tile None)
    if E:
        return R
    # 2) autotile
    changed = {(x, y) for y in range(h) for x in range(w) if tg[y][x] != otg[y][x]}
    near = set()
    for (x, y) in changed:
        for dx, dy in D8:
            X, Y = x + dx, y + dy
            if 0 <= X < w and 0 <= Y < h and (X, Y) not in changed:
                near.add((X, Y))
    tiles = [[None] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            t = want_tile[y][x]
            if t is None:
                nt = autotiler.pick(tg, w, h, x, y)
                reason = "terrain changed" if (x, y) in changed else "tile/symbol mismatch or auto"
                R.auto_cells.append((x, y, otiles[y][x], nt, reason))
                t = nt
            elif (x, y) in near and not pinned[y][x] and t == otiles[y][x] and tg[y][x][0] not in PROPERTY_TYPES \
                    and not autotiler.consistent(t, tg, w, h, x, y):
                nt = autotiler.pick(tg, w, h, x, y)
                if nt != t:
                    R.auto_cells.append((x, y, t, nt, "neighbour changed"))
                    t = nt
            tiles[y][x] = t
            if not autotiler.used.get(t):
                W.append("(%d,%d): tile %03x never appears in a stock map (check it on screen)" % (x, y, t))
    R.tiles = tiles
    R.terrain_bytes = encode_terrain(w, h, tiles)

    # 3) units
    if spec.units_none:
        R.units_bytes = None
    else:
        occ = {}
        cnt = Counter()
        for u in spec.units:
            tag = "unit line %d (P%d %s at %d,%d)" % (u.line, u.owner, unit_name(u.utype), u.x, u.y)
            if not (0 <= u.x < w and 0 <= u.y < h):
                E.append("%s: outside the %dx%d map" % (tag, w, h))
                continue
            if u.owner > players:
                E.append("%s: owner slot %d but the map has %d players" % (tag, u.owner, players))
            if (u.x, u.y) in occ:
                E.append("%s: overlaps unit line %d" % (tag, occ[(u.x, u.y)].line))
            occ[(u.x, u.y)] = u
            cnt[u.owner] += 1
            mt = work_rom[UNIT_TABLE + u.utype * UNIT_ENTRY_SIZE + 0x19]
            ty = tiles[u.y][u.x]
            terr = tile_info(work_rom, ty)[0]
            cost = work_rom[MOVECOST_CLEAR + mt * 32 + terr]
            if cost == 0xFF:
                E.append("%s: cannot stand on %s (movement type %d, terrain %d)" % (
                    tag, TERRAIN_LABEL.get(terr, "terrain %d" % terr), mt, terr))
            if not 1 <= u.hp <= 100:
                E.append("%s: hp must be 1..100" % tag)
        for p, n in cnt.items():
            if n > 50:
                E.append("player %d has %d predeployed units (unit cap is 50)" % (p, n))
        R.units_bytes = encode_unit_list(spec.units)
        # the player's (non-Black-Hole) predeployed units may be referenced by event scripts
        if oub is not None:
            human = {p for p in range(1, players + 1) if armies[p - 1] != 5}
            orig = {(u.x, u.y, u.utype) for u in decode_unit_list(oub) if u.owner in human}
            new = {(u.x, u.y, u.utype) for u in spec.units if u.owner in human}
            missing = sorted(orig - new)
            if missing:
                msg = ("player-side predeployed units moved/removed/retyped %s; event scripts (+0x04/+0x08) "
                       "may reference them" % ", ".join("%s@%d,%d" % (unit_name(t), x, y) for x, y, t in missing))
                if spec.events_checked:
                    W.append(msg + " (accepted: events_checked yes)")
                else:
                    E.append(msg + " -- playtest the mission and add 'events_checked yes' to accept")
    # 4) HQs
    hq = Counter(own for row in tg for (ty, own) in row if ty == 8)
    for p in range(1, players + 1):
        if not hq.get(p):
            ohq = any(ty == 8 and own == p for row in otg for (ty, own) in row)
            W.append("player slot %d has no HQ%s" % (p, " (the original had one)" if ohq else ""))
    return R
