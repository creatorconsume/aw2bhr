"""Advance Wars 2 - Remix: a modest, data-only CO and unit balance pass.

Library use (build_remix.py):
    import balance
    log = balance.apply(rom, alloc)   # rom: bytearray of the sha1-checked base ROM
                                       # alloc(nbytes, align=4) -> ROM offset of free (0xFF) space
    problems = balance.verify(rom)     # [] once apply() has run

Stand-alone test (writes OUT only; never point OUT into the repo):
    python3 -I tools/remix/balance.py BASE.gba OUT.gba [--alloc-at 0x7F0000]
    python3 -I tools/remix/balance.py --table        # print the change list as Markdown

Every change is a frozen record that holds the expected `before` value and the new `after` value.
apply() checks every `before` against the ROM before it writes anything. Running it twice, or on
an unexpected ROM, raises ValueError and leaves the ROM untouched. To revert a change, delete
its record. To retune one, edit its `after` value.

Data layout (docs/remix/ROM_NOTES.md section 2.4; the getters are in the decomp):
  CO table 0x5D3DD0, 19 x 0x104. Header u32 +0x0C COP stars and +0x10 SCOP stars
    (sub_080441D4 / sub_08044208: charge needed = star value x stars).
  Power blocks: D2D +0x38, COP +0x7C, SCOP +0xC0, 0x44 bytes each. The block in use is
    the player's power state (player struct +0x1E).
    s16 +0x0E good luck  (sub_08042E2C; roll = rand % good)
    s16 +0x10 bad luck   (sub_08042E84; roll = rand % bad, subtracted)
    s16 +0x12 counter %  (sub_08042FC4)
    s16 +0x14 cost %     (sub_08043270; price = base * (100 + x) / 100, sub_08042C9C)
    s16 +0x16 capture %  (sub_08042EDC; 100 + x)
    u32 +0x24 + 4*slot   modifier entry pointers. Slots: 0 foot, 1 vehicle, 2 air, 3 copter,
      4 sea (unit domain, unit +0x18), then 5 direct, 6 indirect, 7 transport (from the unit's
      min range, sub_080432E0). Foot units read slot 0 only. Other units add their domain
      slot and their role slot (sub_080430B0 and the getters after it).
    Modifier entry = s16 atk%, def%, move, range. The entries are shared, so new ones go
      to alloc'd space and only the changed CO's pointers are repointed.
  Unit table 0x5D5ABC, 25 x 0x5C. u16 +0x06 = cost / 10.
Out of scope (coded effects): HP-changing powers (Hawke, Sturm, Andy), Colin's Gold Rush
multiplier and Power of Money, Lash's terrain bonus, Sonja's hidden HP and Counter Break.
"""
import hashlib
import os
import struct
import sys
from dataclasses import dataclass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from aw2lib import (CO_ENTRY_SIZE, CO_NAMES, CO_SLOT_NAMES, CO_TABLE, ROM_BASE, ROM_SHA1,  # noqa: E402
                    UNIT_ENTRY_SIZE, UNIT_NAMES, UNIT_TABLE, u32)

BLOCK_NAMES = ("D2D", "COP", "SCOP")
BLOCK_OFF = (0x38, 0x7C, 0xC0)
BLOCK_FIELDS = {"good_luck": 0x0E, "bad_luck": 0x10, "counter": 0x12, "cost": 0x14, "capture": 0x16}
STAR_FIELDS = {"cop": 0x0C, "scop": 0x10}
DOMAIN_SLOTS = ("foot", "vehicle", "air", "copter", "sea")
MOD_ENTRY = struct.Struct("<hhhh")      # atk%, def%, move, range


@dataclass(frozen=True)
class CoField:
    """An s16 field in one or more power blocks of one CO (luck, counter, cost or capture)."""
    co: str
    blocks: tuple
    field: str
    before: int
    after: int
    why: str


@dataclass(frozen=True)
class CoStars:
    """A COP or SCOP star count (u32 in the CO header)."""
    co: str
    power: str          # "cop" | "scop"
    before: int
    after: int
    why: str


@dataclass(frozen=True)
class CoModifier:
    """Repoint (block, slot) modifier pointers of one CO to a new entry (atk%, def%, move, range)."""
    co: str
    blocks: tuple
    slots: tuple
    before: tuple
    after: tuple
    why: str


@dataclass(frozen=True)
class UnitCost:
    """Unit build cost in gold (stored in the ROM as cost / 10)."""
    unit: str
    before: int
    after: int
    why: str


CHANGES = (
    # ---- tone-downs -------------------------------------------------------------------------
    CoModifier("Sturm", (0, 1), DOMAIN_SLOTS, (20, 20, 0, 0), (15, 15, 0, 0),
               "Day-to-day 120/120 plus all-terrain movement is the most lopsided AW2 kit. "
               "115/115 keeps him the strongest CO and keeps his identity. Block 1 is his unused "
               "COP (0 stars) and only follows D2D."),
    CoModifier("Sturm", (2,), DOMAIN_SLOTS, (40, 40, 0, 0), (30, 30, 0, 0),
               "Meteor Strike's 8 HP hit is coded and stays. The stat spike drops from 140/140 to 130/130."),
    CoModifier("Grit", (1,), ("indirect",), (50, 0, 0, 2), (40, 0, 0, 2),
               "Snipe Attack: +2 range and 150% indirects for 3 stars is the standard complaint. "
               "Range is unchanged and firepower drops to 140%."),
    CoModifier("Grit", (2,), ("indirect",), (50, 0, 0, 3), (40, 0, 0, 3),
               "Super Snipe keeps +3 range. Firepower drops to 140% to match the COP."),
    CoField("Kanbei", (0, 1, 2), "cost", 20, 30,
            "130/130 day-to-day at only +20% cost is top tier. +30% keeps his units elite but "
            "fewer (Infantry 1300 G, Tank 9100 G)."),
    CoStars("Hawke", "cop", 5, 6,
            "Black Wave's coded 1 HP drain and heal across the map is the most repeatable swing "
            "power. It now needs one more star. Black Storm stays at 9."),
    CoStars("Colin", "cop", 2, 3,
            "Gold Rush (funds x1.5, coded) at 2 stars snowballs the economy. 3 stars slows the "
            "cycle without touching his cheap units."),
    # ---- small buffs ------------------------------------------------------------------------
    CoStars("Sami", "scop", 8, 7,
            "Victory March (one-turn captures, +2 infantry move) was rarely reachable at 8 stars."),
    CoField("Sonja", (0, 1, 2), "bad_luck", 10, 5,
            "Sonja's day-to-day bad luck roll (0..9%) halves to 0..4%. Her intel kit is coded and unchanged."),
    CoField("Flak", (0,), "good_luck", 15, 20,
            "Flak's day-to-day luck goes from -9..+14% to -9..+19%, so his variance pays off a "
            "little more often. Powers are unchanged."),
    UnitCost("Battleship", 28000, 25000,
             "Naval play is the weakest part of AW2. 25,000 G matches the Days of Ruin price and "
             "makes ports worth building from."),
)


# ---------------------------------------------------------------------------------------------
def _co_index(name):
    return CO_NAMES.index(name)


def _unit_index(name):
    for t, n in UNIT_NAMES.items():
        if n == name:
            return t
    raise KeyError(name)


def _s16(rom, o):
    return struct.unpack_from("<h", rom, o)[0]


def _block(co, b):
    return CO_TABLE + _co_index(co) * CO_ENTRY_SIZE + BLOCK_OFF[b]


def _slot_ptr_off(co, b, slot):
    return _block(co, b) + 0x24 + 4 * CO_SLOT_NAMES.index(slot)


def _read_mod(rom, ptr):
    return MOD_ENTRY.unpack_from(rom, ptr - ROM_BASE)


def _locations(c):
    """Yield (description, ROM offset) for every value a record touches."""
    if isinstance(c, CoField):
        for b in c.blocks:
            o = _block(c.co, b) + BLOCK_FIELDS[c.field]
            yield ("%s %s %s" % (c.co, BLOCK_NAMES[b], c.field), o)
    elif isinstance(c, CoStars):
        yield ("%s %s stars" % (c.co, c.power.upper()),
               CO_TABLE + _co_index(c.co) * CO_ENTRY_SIZE + STAR_FIELDS[c.power])
    elif isinstance(c, CoModifier):
        for b in c.blocks:
            for s in c.slots:
                yield ("%s %s %s modifier" % (c.co, BLOCK_NAMES[b], s), _slot_ptr_off(c.co, b, s))
    elif isinstance(c, UnitCost):
        yield ("%s cost" % c.unit, UNIT_TABLE + _unit_index(c.unit) * UNIT_ENTRY_SIZE + 0x06)
    else:
        raise TypeError(c)


def _current(rom, c, o):
    if isinstance(c, CoField):
        return _s16(rom, o)
    if isinstance(c, CoStars):
        return u32(rom, o)
    if isinstance(c, CoModifier):
        return _read_mod(rom, u32(rom, o))
    if isinstance(c, UnitCost):
        return struct.unpack_from("<H", rom, o)[0] * 10
    raise TypeError(c)


def _check(rom, want):
    """Return mismatch messages comparing every touched value with the record's before or after."""
    bad = []
    for c in CHANGES:
        exp = getattr(c, want)
        for desc, o in _locations(c):
            cur = _current(rom, c, o)
            if cur != exp:
                bad.append("%s: expected %s %r, found %r" % (desc, want, exp, cur))
    return bad


def apply(rom, alloc):
    """Apply CHANGES to rom (bytearray) in place. alloc(nbytes, align=4) -> ROM offset. Returns log lines."""
    bad = _check(rom, "before")
    if bad:
        raise ValueError("balance.apply: ROM does not hold the expected original values "
                         "(already applied, or not the stock AW2 USA ROM):\n  " + "\n  ".join(bad))
    for c in CHANGES:   # sanity on the designed values
        if isinstance(c, CoStars):
            assert 1 <= c.after <= 10 and c.after != c.before, c
        if isinstance(c, UnitCost):
            assert c.after % 10 == 0 and 0 < c.after // 10 <= 0xFFFF, c
        if isinstance(c, CoField):
            assert -0x8000 <= c.after <= 0x7FFF, c

    log = []
    mods = [c for c in CHANGES if isinstance(c, CoModifier)]
    if mods:
        size = MOD_ENTRY.size * len(mods)
        base = alloc(size, align=4)
        if base >= ROM_BASE:
            base -= ROM_BASE
        if base % 4 or base + size > len(rom) or any(v != 0xFF for v in rom[base:base + size]):
            raise ValueError("balance.apply: alloc returned 0x%X, which is not %d bytes of free 0xFF space"
                             % (base, size))
        for i, c in enumerate(mods):
            eo = base + i * MOD_ENTRY.size
            MOD_ENTRY.pack_into(rom, eo, *c.after)
            for desc, po in _locations(c):
                struct.pack_into("<I", rom, po, ROM_BASE + eo)
            log.append("balance: %s %s %s atk/def/move/range %s -> %s (entry 0x%06X)" % (
                c.co, "+".join(BLOCK_NAMES[b] for b in c.blocks), ",".join(c.slots), c.before, c.after, eo))
    for c in CHANGES:
        if isinstance(c, CoField):
            for desc, o in _locations(c):
                struct.pack_into("<h", rom, o, c.after)
            log.append("balance: %s %s %s %d -> %d" % (
                c.co, "+".join(BLOCK_NAMES[b] for b in c.blocks), c.field, c.before, c.after))
        elif isinstance(c, CoStars):
            for desc, o in _locations(c):
                struct.pack_into("<I", rom, o, c.after)
            log.append("balance: %s %s stars %d -> %d" % (c.co, c.power.upper(), c.before, c.after))
        elif isinstance(c, UnitCost):
            for desc, o in _locations(c):
                struct.pack_into("<H", rom, o, c.after // 10)
            log.append("balance: %s cost %d -> %d" % (c.unit, c.before, c.after))
    bad = _check(rom, "after")
    assert not bad, bad
    return log


def verify(rom):
    """Return a list of problems ([] = every change is present). Accepts bytes or bytearray."""
    return _check(rom, "after")


def markdown_table():
    rows = ["| # | target | before | after | rationale |", "|---|---|---|---|---|"]
    for i, c in enumerate(CHANGES, 1):
        if isinstance(c, CoModifier):
            tgt = "%s %s, slots %s (atk%%, def%%, move, range)" % (
                c.co, "/".join(BLOCK_NAMES[b] for b in c.blocks),
                "foot..sea" if c.slots == DOMAIN_SLOTS else ",".join(c.slots))
            fmt = lambda v: "%+d/%+d/%+d/%+d" % v  # noqa: E731
            bef, aft = fmt(c.before), fmt(c.after)
        elif isinstance(c, CoField):
            tgt = "%s %s %s" % (c.co, "/".join(BLOCK_NAMES[b] for b in c.blocks), c.field)
            bef, aft = "%+d" % c.before, "%+d" % c.after
        elif isinstance(c, CoStars):
            tgt = "%s %s stars" % (c.co, c.power.upper())
            bef, aft = str(c.before), str(c.after)
        else:
            tgt = "%s cost" % c.unit
            bef, aft = "%d G" % c.before, "%d G" % c.after
        rows.append("| %d | %s | %s | %s | %s |" % (i, tgt, bef, aft, c.why))
    return "\n".join(rows)


def main(argv):
    if argv[:1] == ["--table"]:
        print(markdown_table())
        return 0
    if len(argv) < 2:
        print(__doc__)
        return 2
    src, dst = argv[0], argv[1]
    at = int(argv[argv.index("--alloc-at") + 1], 0) if "--alloc-at" in argv else 0x7F0000
    repo = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    if os.path.realpath(dst).startswith(repo + os.sep):
        raise SystemExit("refusing to write a ROM inside the repository: " + dst)
    rom = bytearray(open(src, "rb").read())
    if hashlib.sha1(rom).hexdigest() != ROM_SHA1:
        raise SystemExit("base ROM sha1 mismatch (expected %s)" % ROM_SHA1)
    nxt = [at]

    def alloc(n, align=4):
        o = (nxt[0] + align - 1) // align * align
        nxt[0] = o + n
        return o
    for line in apply(rom, alloc):
        print(line)
    assert not verify(rom)
    open(dst, "wb").write(rom)
    print("wrote", dst)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
