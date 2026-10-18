"""Helpers for reading/patching Advance Wars 2: Black Hole Rising (USA) ROM data.

Offsets are ROM file offsets unless suffixed _ADDR (GBA bus address = 0x08000000 + offset).
See docs/remix/ROM_NOTES.md for the documented formats.
Run scripts with: python3 -I tools/remix/<script>.py ...
"""
import struct

ROM_SHA1 = "14dd0b22c894865867aff89e8116b2dffae25605"
ROM_BASE = 0x08000000

MAP_TABLE = 0x5C77A0          # 192 entries x 0x5C bytes (gUnknown_085C77A0)
MAP_ENTRY_SIZE = 0x5C
MAP_COUNT = 192
UNIT_TABLE = 0x5D5ABC         # unit type records, 0x5C bytes each (gUnknown_085D5ABC)
UNIT_ENTRY_SIZE = 0x5C
UNIT_SLOTS = 25               # type 0..24 (0,4,9,12,13,18 are unused placeholders)

UNIT_NAMES = {
    0: None, 1: "Infantry", 2: "Mech", 3: "Md Tank", 4: None, 5: "Tank", 6: "Recon",
    7: "APC", 8: "Neotank", 9: None, 10: "Artillery", 11: "Rockets", 12: None, 13: None,
    14: "A-Air", 15: "Missiles", 16: "Fighter", 17: "Bomber", 18: None, 19: "B Copter",
    20: "T Copter", 21: "Battleship", 22: "Cruiser", 23: "Lander", 24: "Sub",
}

# Campaign map ids (index into MAP_TABLE); "chapter" byte at entry+0x58
OS_CAMPAIGN = list(range(0x8A, 0x92))  # 138..145


def u8(b, o): return b[o]
def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]


def off(addr):
    """GBA ROM address -> file offset."""
    assert ROM_BASE <= addr < ROM_BASE + 0x2000000, hex(addr)
    return addr - ROM_BASE


def lz77_decompress(rom, o):
    """GBA BIOS LZ77 (type 0x10) decompress starting at file offset o. Returns (data, compressed_len)."""
    hdr = u32(rom, o)
    assert hdr & 0xF0 == 0x10, "not LZ77 at %x (hdr %08x)" % (o, hdr)
    size = hdr >> 8
    out = bytearray()
    p = o + 4
    while len(out) < size:
        flags = rom[p]; p += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                b1, b2 = rom[p], rom[p + 1]; p += 2
                n = (b1 >> 4) + 3
                disp = ((b1 & 0xF) << 8 | b2) + 1
                for _ in range(n):
                    out.append(out[-disp])
            else:
                out.append(rom[p]); p += 1
    return bytes(out[:size]), p - o


def lz77_compress(data):
    """Simple greedy GBA-compatible LZ77 (type 0x10) compressor (VRAM-safe: disp>=2)."""
    out = bytearray(struct.pack("<I", 0x10 | (len(data) << 8)))
    i = 0
    n = len(data)
    while i < n:
        flag_pos = len(out); out.append(0); flags = 0
        for bit in range(8):
            if i >= n:
                break
            best_len, best_disp = 0, 0
            for disp in range(2, min(i, 0x1000) + 1):
                l = 0
                while l < 18 and i + l < n and data[i + l] == data[i - disp + l]:
                    l += 1
                if l > best_len:
                    best_len, best_disp = l, disp
                    if l == 18:
                        break
            if best_len >= 3:
                flags |= 0x80 >> bit
                d = best_disp - 1
                out.append(((best_len - 3) << 4) | (d >> 8)); out.append(d & 0xFF)
                i += best_len
            else:
                out.append(data[i]); i += 1
        out[flag_pos] = flags
    while len(out) % 4:
        out.append(0)
    return bytes(out)


class MapEntry:
    """One 0x5C-byte record of the map table (see ROM_NOTES.md)."""
    def __init__(self, rom, idx):
        self.idx = idx
        self.o = MAP_TABLE + idx * MAP_ENTRY_SIZE
        self.raw = rom[self.o:self.o + MAP_ENTRY_SIZE]
        r = self.raw
        self.ptr_terrain0 = u32(r, 0x00)
        self.ptr_04 = u32(r, 0x04)
        self.ptr_08 = u32(r, 0x08)
        self.ptr_10 = u32(r, 0x10)
        self.name_text = u16(r, 0x14)
        self.f16 = u16(r, 0x16)
        self.terrain_normal = u32(r, 0x2C)
        self.terrain_alt = u32(r, 0x30)
        self.units_normal = u32(r, 0x34)
        self.units_alt = u32(r, 0x38)
        self.cos = list(r[0x3C:0x40])
        self.armies = list(r[0x40:0x44])
        self.teams = list(r[0x44:0x48])
        self.chapter = r[0x58]


def read_terrain(rom, ptr):
    data, clen = lz77_decompress(rom, off(ptr))
    w, h = data[0], data[1]
    grid = [[u16(data, 2 + 2 * (y * w + x)) for x in range(w)] for y in range(h)]
    return w, h, grid, clen


def read_units(rom, ptr):
    """Returns list of (player, rec12bytes) and total byte length incl. terminator."""
    o = off(ptr)
    res = []
    player = None
    start = o
    while True:
        r = rom[o:o + 12]
        if r[0] == 0xFF:
            o += 12
            break
        if r[0] == 0xFE:
            player = r[1]
        else:
            res.append((player, bytes(r)))
        o += 12
    return res, o - start


def build_units(groups):
    """groups: dict player -> list of 12-byte records. Returns bytes incl. terminators."""
    out = bytearray()
    for p in sorted(groups):
        out += bytes([0xFE, p]) + bytes(10)
        for r in groups[p]:
            assert len(r) == 12
            out += r
    out += bytes([0xFF]) + bytes(11)
    return bytes(out)


def unit_record(x, y, utype, hp=100, ammo=None, fuel=None, ai=0, rom=None, extra=b"\0\0"):
    """Build a 12-byte predeployed-unit record. ammo/fuel default to the unit table max."""
    if rom is not None and (ammo is None or fuel is None):
        u = UNIT_TABLE + utype * UNIT_ENTRY_SIZE
        ammo = rom[u + 0x0B] if ammo is None else ammo
        fuel = rom[u + 0x10] if fuel is None else fuel
    return bytes([x, y, utype & 0xFF, utype >> 8, hp, ammo or 0, fuel or 0, 0, 0, ai]) + extra

TEXT_TABLE = 0x610A38         # u32 pointers to NUL-terminated ASCII strings, indexed by text id


def text(rom, tid):
    a = off(u32(rom, TEXT_TABLE + tid * 4))
    e = rom.find(b"\0", a)
    return rom[a:e].decode("latin1")

# ---------------------------------------------------------------------------
# More tables (see ROM_NOTES.md for field layouts and confidence levels)
TILE_TO_TERRAIN = 0x0C1BC4    # u8[0x200]: map tile id -> terrain type | owner<<5
TERRAIN_TABLE = 0x5D583C      # 20-byte records indexed by terrain type (0..0x1F)
MOVECOST_CLEAR = 0x5D511C     # u8[7][32]: [movetype][terrain type], 0xFF = impassable
MOVECOST_SNOW = 0x5D51FC
MOVECOST_RAIN = 0x5D52DC
MOVECOST_ALL1 = 0x5D53BC      # used by Sturm / Lash powers
CO_TABLE = 0x5D3DD0           # 19 COs x 0x104 bytes
CO_ENTRY_SIZE = 0x104
CO_NAMES = ["Nell", "Andy", "Max", "Olaf", "Sami", "Grit", "Kanbei", "Sonja", "Eagle", "Drake",
            "Sturm", "Flak", "Lash", "Adder", "Hawke", "Hachi", "Colin", "Jess", "Sensei"]
CO_SLOT_NAMES = ["foot", "vehicle", "air", "copter", "sea", "direct", "indirect", "transport"]
MAPSPRITE_TABLE = 0x49CD88    # 25 x 36 bytes; +0x18 s16 map move speed (px/frame)
OPT_VISUAL_DEFAULT_INSN = 0x016A6A  # 'movs r2,#1' (01 22): new-save default Visual (1=A,2=B,3=C,0=off)
FUNDS_PER_PROPERTY_INSN = 0x0347E0  # 'movs r0,#250; lsls r0,#2' -> 1000
FREE_SPACE_START = 0x617EE4   # 0xFF fill to end of 8 MiB ROM
FREE_SPACE_END = 0x800000

TERRAIN_NAMES = {
    1: "Plain", 2: "River", 3: "Mountain", 4: "Wood", 5: "Road", 6: "City", 7: "Sea", 8: "HQ",
    9: "(BH structure filler)", 10: "Airport", 11: "Port", 12: "Bridge", 13: "Shoal", 14: "Base",
    15: "Pipeline", 16: "Pipeline (weak/seam)", 17: "Silo", 18: "Silo (used)", 19: "Reef", 20: "Lab",
    21: "Minicannon", 22: "Minicannon", 23: "Minicannon", 24: "Minicannon", 25: "Laser",
    26: "Cannon", 27: "Cannon", 28: "Volcano", 29: "Factory (BH)", 30: "Deathray", 31: "Blocked",
}
# property tiles: 0x1C0 + 5*owner + kind  (owner 0 = neutral, 1..4 = player slot)
PROPERTY_KINDS = ["HQ", "Base", "City", "Airport", "Port"]


def property_tile(kind, owner):
    return 0x1C0 + 5 * owner + PROPERTY_KINDS.index(kind)


def tile_terrain(rom, tile):
    v = rom[TILE_TO_TERRAIN + tile]
    return v & 0x1F, v >> 5


def unit_stats(rom, t):
    u = UNIT_TABLE + t * UNIT_ENTRY_SIZE
    r = rom[u:u + UNIT_ENTRY_SIZE]
    return {
        "type": t, "name": text(rom, u16(r, 0)), "cost": u16(r, 6) * 10, "move": r[0x0A],
        "ammo": r[0x0B], "vision": r[0x0C], "min_range": r[0x0E], "max_range": r[0x0F],
        "fuel": r[0x10], "domain": r[0x18], "movetype": r[0x19], "class_mask": r[0x1A],
        "co_class": r[0x1B], "transport_ptr": u32(r, 0x14),
        "dmg_primary": list(r[0x1E:0x1E + 26]), "dmg_secondary": list(r[0x38:0x38 + 26]),
    }
