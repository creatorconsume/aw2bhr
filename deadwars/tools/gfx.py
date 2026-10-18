#!/usr/bin/env python3
"""Dead Wars asset converter.

usage:  python3 -I tools/gfx.py <deadwars_dir>

Reads  <dir>/art/*.txt
Writes <dir>/build/gen/assets.h, <dir>/build/gen/assets.c   (contract: docs/ASSET_SPEC.md)
       <dir>/build/preview/*.png                           (3x scaled review sheets)

ART FILE FORMAT (art/*.txt)
---------------------------
Lines starting with '#' (outside of a sprite body) are comments; blank lines are ignored.
All names are global across files.  Blocks:

  palette NAME                  16 colour lines follow, then 'end'.
    C RRGGBB  [comment]         line i (0-based) defines palette index i.  C is the
    ...                         single character used for that index in pixel grids.
  end

  sprite NAME WxH PALETTE [key=value ...]
  <H rows of exactly W characters>
                                Pixel characters are looked up in PALETTE's char map.
                                If a char is not in the map: '.' = index 0, and the hex
                                digits 0-9 a-f (A-F) mean that index directly.
                                Options: bob=N  -> for units without an explicit second
                                frame, frame 1 is frame 0 with rows [0,N) shifted down
                                one pixel (idle breathing / engine bob).
                                A sprite called NAME.f1 is used as NAME's second frame.

  compose NAME WxH PALETTE      Layered image; ops until 'end':
    base SPRITE                 copy SPRITE (must be WxH)
    stamp SPRITE X Y [hflip]    paste SPRITE at X,Y; index 0 = transparent
  end

  derive NAME SRC OP            OP = transpose | hflip | vflip

  glyphs NAME WxH               Monochrome glyph set; lines until 'end':
    KEY row0 row1 ... row(H-1)  KEY is a single printable char, or a number
                                (decimal/0x..) giving the char code.  Each row is
                                up to W chars of '#' (on) / '.' (off).
  end

Category conventions (see docs/ASSET_SPEC.md):
  terrain  : sprites/composes named MT_<NAME> 16x16 on palette 'terrain'
             ('road_tex' = 16x16 cracked asphalt used to build the 16 road autotiles)
  units    : UG_<NAME> 16x16 on 'survivor' or 'horde'
  bosses   : BOSS_<NAME> 16x16 on 'horde' (own array boss_gfx, same frame rules as units)
  ui       : 'window' 24x24 (ui) sliced into the 9-slice set, 'cursor0'/'cursor1'
             16x16, 'pointer' 8x8 and 'explode0'..'explode3' 16x16 (cursor palette);
             glyphs 'font' and 'digits'
  portraits: PT_<NAME> 32x32 on palette 'pt_<name>'
  title    : glyphs 'logo', sprites 'moon', 'zombie_*' etc., composed procedurally.
"""
import os
import sys
import glob
import random
import math

from PIL import Image, ImageDraw

# --------------------------------------------------------------------------
# enums (must match docs/ASSET_SPEC.md order)
MT_BASE = ['PLAIN', 'FOREST', 'HILL', 'WATER', 'BRIDGE_H', 'BRIDGE_V', 'RUINS',
           'WALL', 'WRECK', 'HOUSE', 'HOUSE_LOOTED', 'WORKSHOP', 'SAFEHOUSE',
           'HOSPITAL', 'NEST', 'RADIO', 'HELIPAD']
MT_BUILDINGS = MT_BASE[MT_BASE.index('HOUSE'):]
UG = ['SURVIVOR', 'HEAVY', 'MEDIC', 'SNIPER', 'PICKUP', 'BUS', 'ARMOR',
      'WALKER', 'RUNNER', 'BRUTE', 'SPITTER', 'BLOATER']
UG_HORDE_FIRST = UG.index('WALKER')
OV = ['NONE', 'MOVE', 'ATTACK', 'FOG_UNUSED']
PT = ['MARA', 'DOC', 'JUNE', 'HOLT', 'ZERO', 'ZERO_BOSS']   # append only
BOSS = ['ZERO']   # boss_gfx, horde palette; append only
PAL_SINGLE = ['ui', 'terrain', 'overlay', 'survivor', 'horde', 'cursor', 'title']

N_BIT, E_BIT, S_BIT, W_BIT = 1, 2, 4, 8


class ArtError(Exception):
    pass


# --------------------------------------------------------------------------
# parsing

class Palette:
    def __init__(self, name):
        self.name = name
        self.colors = []
        self.chars = {}


class Art:
    def __init__(self):
        self.palettes = {}
        self.sprites = {}      # name -> dict(w,h,pal,pix,opts)
        self.composes = {}
        self.derives = {}
        self.glyphs = {}
        self._cache = {}

    def palette(self, name):
        if name not in self.palettes:
            raise ArtError('unknown palette %r' % name)
        return self.palettes[name]

    def has(self, name):
        return name in self.sprites or name in self.composes or name in self.derives

    def get(self, name):
        """-> (w, h, palname, pix[y][x])"""
        if name in self._cache:
            return self._cache[name]
        if name in self.sprites:
            s = self.sprites[name]
            r = (s['w'], s['h'], s['pal'], s['pix'])
        elif name in self.composes:
            c = self.composes[name]
            w, h = c['w'], c['h']
            pix = [[0] * w for _ in range(h)]
            for op in c['ops']:
                if op[0] == 'base':
                    bw, bh, _, bp = self.get(op[1])
                    if (bw, bh) != (w, h):
                        raise ArtError('%s: base %s size mismatch' % (name, op[1]))
                    pix = [row[:] for row in bp]
                elif op[0] == 'stamp':
                    sw, sh, _, sp = self.get(op[1])
                    ox, oy, hf = op[2], op[3], op[4]
                    for y in range(sh):
                        for x in range(sw):
                            v = sp[y][sw - 1 - x if hf else x]
                            tx, ty = ox + x, oy + y
                            if v and 0 <= tx < w and 0 <= ty < h:
                                pix[ty][tx] = v
            r = (w, h, c['pal'], pix)
        elif name in self.derives:
            src, op = self.derives[name]
            w, h, pal, sp = self.get(src)
            if op == 'transpose':
                pix = [[sp[x][y] for x in range(h)] for y in range(w)]
                w, h = h, w
            elif op == 'hflip':
                pix = [row[::-1] for row in sp]
            elif op == 'vflip':
                pix = [row[:] for row in sp[::-1]]
            else:
                raise ArtError('bad derive op %s' % op)
            r = (w, h, pal, pix)
        else:
            raise ArtError('missing sprite %r' % name)
        self._cache[name] = r
        return r


def _decode_char(ch, pal, where):
    if ch in pal.chars:
        return pal.chars[ch]
    if ch == '.':
        return 0
    if ch in '0123456789abcdefABCDEF':
        return int(ch, 16)
    raise ArtError('%s: char %r not in palette %s' % (where, ch, pal.name))


def _wh(tok, where):
    try:
        w, h = tok.lower().split('x')
        return int(w), int(h)
    except ValueError:
        raise ArtError('%s: bad size %r' % (where, tok))


def parse_art(art_dir):
    art = Art()
    files = sorted(glob.glob(os.path.join(art_dir, '*.txt')))
    if not files:
        raise ArtError('no art files in %s' % art_dir)
    pending = []   # sprites are decoded after all palettes are known
    for fn in files:
        lines = open(fn, encoding='utf-8').read().split('\n')
        i = 0
        base = os.path.basename(fn)
        while i < len(lines):
            line = lines[i].strip()
            where = '%s:%d' % (base, i + 1)
            i += 1
            if not line or line.startswith('#'):
                continue
            tok = line.split()
            kw = tok[0]
            if kw == 'palette':
                pal = Palette(tok[1])
                while True:
                    if i >= len(lines):
                        raise ArtError('%s: unterminated palette' % where)
                    l2 = lines[i].strip()
                    i += 1
                    if not l2 or l2.startswith('#'):
                        continue
                    if l2 == 'end':
                        break
                    t2 = l2.split()
                    ch, hexv = t2[0], t2[1]
                    if len(ch) != 1:
                        raise ArtError('%s: palette char must be 1 char' % where)
                    rgb = tuple(int(hexv[k:k + 2], 16) for k in (0, 2, 4))
                    pal.chars.setdefault(ch, len(pal.colors))
                    pal.colors.append(rgb)
                if len(pal.colors) > 16:
                    raise ArtError('%s: palette %s has >16 colours' % (where, pal.name))
                while len(pal.colors) < 16:
                    pal.colors.append((0, 0, 0))
                art.palettes[pal.name] = pal
            elif kw == 'sprite':
                name = tok[1]
                w, h = _wh(tok[2], where)
                palname = tok[3]
                opts = dict(t.split('=', 1) for t in tok[4:])
                rows = []
                for _ in range(h):
                    if i >= len(lines):
                        raise ArtError('%s: sprite %s truncated' % (where, name))
                    rows.append(lines[i].strip())
                    i += 1
                for k, r in enumerate(rows):
                    if len(r) != w:
                        raise ArtError('%s:%d: sprite %s row %d has width %d, expected %d'
                                       % (base, i - h + k + 1, name, k, len(r), w))
                if name in art.sprites:
                    raise ArtError('%s: duplicate sprite %s' % (where, name))
                pending.append((name, w, h, palname, rows, opts, where))
            elif kw == 'compose':
                name = tok[1]
                w, h = _wh(tok[2], where)
                ops = []
                while True:
                    if i >= len(lines):
                        raise ArtError('%s: unterminated compose' % where)
                    l2 = lines[i].strip()
                    i += 1
                    if not l2 or l2.startswith('#'):
                        continue
                    if l2 == 'end':
                        break
                    t2 = l2.split()
                    if t2[0] == 'base':
                        ops.append(('base', t2[1]))
                    elif t2[0] == 'stamp':
                        ops.append(('stamp', t2[1], int(t2[2]), int(t2[3]),
                                    len(t2) > 4 and t2[4] == 'hflip'))
                    else:
                        raise ArtError('%s: bad compose op %s' % (where, t2[0]))
                art.composes[name] = dict(w=w, h=h, pal=tok[3], ops=ops)
            elif kw == 'derive':
                art.derives[tok[1]] = (tok[2], tok[3])
            elif kw == 'glyphs':
                name = tok[1]
                w, h = _wh(tok[2], where)
                gl = {}
                while True:
                    if i >= len(lines):
                        raise ArtError('%s: unterminated glyphs' % where)
                    l2 = lines[i].rstrip()
                    i += 1
                    s2 = l2.strip()
                    if not s2 or (s2.startswith('#') and len(s2.split()) != h + 1):
                        continue
                    if s2 == 'end':
                        break
                    t2 = s2.split()
                    if len(t2) != h + 1:
                        raise ArtError('%s:%d: glyph needs %d rows' % (base, i, h))
                    key = t2[0]
                    code = ord(key) if len(key) == 1 else int(key, 0)
                    rows = t2[1:]
                    gw = max(len(r) for r in rows)
                    if gw > w:
                        raise ArtError('%s:%d: glyph too wide' % (base, i))
                    gl[code] = [[1 if c == '#' else 0 for c in r.ljust(gw, '.')] for r in rows]
                art.glyphs[name] = (w, h, gl)
            else:
                raise ArtError('%s: unknown keyword %r' % (where, kw))
    for name, w, h, palname, rows, opts, where in pending:
        pal = art.palette(palname)
        pix = [[_decode_char(c, pal, where) for c in r] for r in rows]
        art.sprites[name] = dict(w=w, h=h, pal=palname, pix=pix, opts=opts)
    return art


# --------------------------------------------------------------------------
# tile helpers

def blank(w, h, v=0):
    return [[v] * w for _ in range(h)]


def tile_at(pix, x0, y0):
    return tuple(pix[y0 + y][x0 + x] for y in range(8) for x in range(8))


def tile_bytes(t):
    out = bytearray()
    for y in range(8):
        for x in range(0, 8, 2):
            out.append((t[y * 8 + x] & 15) | ((t[y * 8 + x + 1] & 15) << 4))
    return bytes(out)


def tile_hflip(t):
    return tuple(t[y * 8 + 7 - x] for y in range(8) for x in range(8))


def tile_vflip(t):
    return tuple(t[(7 - y) * 8 + x] for y in range(8) for x in range(8))


def sprite_tiles_1d(pix, w, h):
    """row-major 8x8 tiles -> concatenated bytes"""
    out = b''
    for ty in range(h // 8):
        for tx in range(w // 8):
            out += tile_bytes(tile_at(pix, tx * 8, ty * 8))
    return out


class TileSet:
    """Deduplicating tile bank (with h/v flip matching)."""

    def __init__(self, flips=True):
        self.tiles = []
        self.lookup = {}
        self.flips = flips

    def add(self, t):
        if t in self.lookup:
            idx, fl = self.lookup[t]
            return idx | (fl << 10)
        idx = len(self.tiles)
        self.tiles.append(t)
        self.lookup[t] = (idx, 0)
        if self.flips:
            h = tile_hflip(t)
            v = tile_vflip(t)
            hv = tile_vflip(h)
            for var, fl in ((h, 1), (v, 2), (hv, 3)):
                if var not in self.lookup:
                    self.lookup[var] = (idx, fl)
        return idx

    def data(self):
        return b''.join(tile_bytes(t) for t in self.tiles)


def decode_se(tiles, se):
    t = tiles[se & 0x3FF]
    if se & 0x400:
        t = tile_hflip(t)
    if se & 0x800:
        t = tile_vflip(t)
    return t


def bytes_to_tile(b):
    t = []
    for byte in b:
        t.append(byte & 15)
        t.append(byte >> 4)
    return tuple(t)


def bgr555(rgb):
    r, g, b = rgb
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def gba_rgb(rgb):
    """colour as the GBA will actually show it (5 bits/channel)"""
    return tuple(((c >> 3) << 3) | (c >> 5) for c in rgb)


# --------------------------------------------------------------------------
# generators

def make_road(art, mask):
    _, _, _, grass = art.get('MT_PLAIN')
    _, _, _, tex = art.get('road_tex')
    pal = art.palette('terrain').chars
    CURB, EDGE, LINE = pal['c'], pal['a'], pal['D']
    SHADE = pal['g']
    lo, hi = 3, 12
    R = [[False] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            inside = lo <= x <= hi and lo <= y <= hi
            if (mask & N_BIT) and y < lo and lo <= x <= hi:
                inside = True
            if (mask & S_BIT) and y > hi and lo <= x <= hi:
                inside = True
            if (mask & W_BIT) and x < lo and lo <= y <= hi:
                inside = True
            if (mask & E_BIT) and x > hi and lo <= y <= hi:
                inside = True
            R[y][x] = inside

    def isroad(x, y):
        if 0 <= x < 16 and 0 <= y < 16:
            return R[y][x]
        # outside the tile: continue the road only where it connects
        cx, cy = min(max(x, 0), 15), min(max(y, 0), 15)
        return R[cy][cx] and ((y < 0 and mask & N_BIT) or (y > 15 and mask & S_BIT) or
                              (x < 0 and mask & W_BIT) or (x > 15 and mask & E_BIT))

    pix = [row[:] for row in grass]
    # round convex outer corners
    cut = []
    for y in range(16):
        for x in range(16):
            if R[y][x]:
                ox = [d for d in (-1, 1) if not isroad(x + d, y)]
                oy = [d for d in (-1, 1) if not isroad(x, y + d)]
                if ox and oy and not isroad(x + ox[0], y + oy[0]):
                    cut.append((x, y))
    for x, y in cut:
        R[y][x] = False
    for y in range(16):
        for x in range(16):
            if R[y][x]:
                edge = any(not isroad(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                pix[y][x] = CURB if edge else tex[y][x]
            else:
                near = any(isroad(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                diag = any(isroad(x + dx, y + dy) for dx, dy in ((1, 1), (-1, 1), (1, -1), (-1, -1)))
                if near:
                    pix[y][x] = EDGE
                elif diag:
                    pix[y][x] = SHADE
    # faded lane dashes on straight runs / turns
    bits = bin(mask).count('1')
    if 1 <= bits <= 2:
        for y in range(16):
            for x in range(16):
                if not R[y][x] or pix[y][x] == CURB:
                    continue
                on = False
                if x in (7, 8):
                    if (mask & N_BIT and y <= 8) or (mask & S_BIT and y >= 7):
                        on = on or (y % 8) in (1, 2, 3, 4)
                if y in (7, 8):
                    if (mask & W_BIT and x <= 8) or (mask & E_BIT and x >= 7):
                        on = on or (x % 8) in (1, 2, 3, 4)
                if on and tex[y][x] == tex[0][0]:   # cracks break the paint
                    pix[y][x] = LINE
    return pix


def build_terrain(art):
    metas = []
    for n in MT_BASE:
        w, h, pal, pix = art.get('MT_' + n)
        if (w, h) != (16, 16) or pal != 'terrain':
            raise ArtError('MT_%s must be 16x16 on terrain palette' % n)
        metas.append(pix)
    for m in range(16):
        metas.append(make_road(art, m))
    for k, pix in enumerate(metas):
        for row in pix:
            if 0 in row:
                raise ArtError('terrain metatile %d uses index 0' % k)
    ts = TileSet()
    meta = []
    for pix in metas:
        meta.append([ts.add(tile_at(pix, x, y)) for (x, y) in ((0, 0), (8, 0), (0, 8), (8, 8))])
    return ts, meta, metas


def build_overlay():
    ts = TileSet()
    meta = []
    for k in range(len(OV)):
        pix = blank(16, 16)
        if k in (1, 2):
            fill, edge = (1, 2) if k == 1 else (3, 4)
            for y in range(16):
                for x in range(16):
                    e = x in (0, 15) or y in (0, 15)
                    pix[y][x] = edge if e else fill
        elif k == 3:
            for y in range(16):
                for x in range(16):
                    pix[y][x] = 5 if (x + y) & 1 else 6
        meta.append([ts.add(tile_at(pix, x, y)) for (x, y) in ((0, 0), (8, 0), (0, 8), (8, 8))])
    return ts, meta


def bob_frame(pix, n):
    out = [row[:] for row in pix]
    for y in range(n - 1, 0, -1):
        out[y] = pix[y - 1][:]
    out[0] = [0] * len(pix[0])
    return out


def build_units(art, prefix='UG_', names=UG, pal_for=None):
    frames = []
    for k, n in enumerate(names):
        name = prefix + n
        w, h, pal, pix = art.get(name)
        want = pal_for(k) if pal_for else ('survivor' if k < UG_HORDE_FIRST else 'horde')
        if (w, h) != (16, 16) or pal != want:
            raise ArtError('%s must be 16x16 on %s palette' % (name, want))
        if art.has(name + '.f1'):
            p1 = art.get(name + '.f1')[3]
        else:
            opts = art.sprites[name]['opts'] if name in art.sprites else {}
            p1 = bob_frame(pix, int(opts.get('bob', '0'))) if 'bob' in opts else pix
        frames.append((pix, p1))
    return frames


def glyph_tile(g, ox, oy):
    pix = blank(8, 8)
    for y, row in enumerate(g):
        for x, v in enumerate(row):
            if v and 0 <= ox + x < 8 and 0 <= oy + y < 8:
                pix[oy + y][ox + x] = 1
    return pix


def build_font(art):
    gw, gh, gl = art.glyphs['font']
    tiles = []
    for code in range(32, 128):
        g = gl.get(code)
        if g is None:
            raise ArtError('font: missing glyph %d (%r)' % (code, chr(code)))
        w = len(g[0])
        ox = 1 if w <= 5 else 0
        pix = glyph_tile(g, ox, 0)
        sh = blank(8, 8)
        for y in range(8):
            for x in range(8):
                if pix[y][x] == 1:
                    for dx, dy in ((1, 0), (0, 1), (1, 1)):
                        if x + dx < 8 and y + dy < 8 and pix[y + dy][x + dx] == 0:
                            sh[y + dy][x + dx] = 2
        for y in range(8):
            for x in range(8):
                if sh[y][x] and not pix[y][x]:
                    pix[y][x] = 2
        tiles.append(pix)
    return tiles


def build_digits(art):
    gw, gh, gl = art.glyphs['digits']
    tiles = []
    for d in range(10):
        g = gl[ord(str(d))]
        w = len(g[0])
        ox, oy = 7 - w, 7 - len(g)   # glyph hugs the bottom-right, 1px outline
        core = glyph_tile(g, ox, oy)
        pix = blank(8, 8)
        for y in range(8):
            for x in range(8):
                if core[y][x]:
                    pix[y][x] = 2
                    for dx in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            xx, yy = x + dx, y + dy
                            if 0 <= xx < 8 and 0 <= yy < 8 and not core[yy][xx]:
                                pix[yy][xx] = 1
        tiles.append(pix)
    return tiles


def build_windows(art):
    w, h, pal, pix = art.get('window')
    if (w, h, pal) != (24, 24, 'ui'):
        raise ArtError('window must be 24x24 on ui palette')
    return [[row[x * 8:x * 8 + 8] for row in pix[y * 8:y * 8 + 8]] for y in range(3) for x in range(3)]


def build_portraits(art):
    out = []
    for n in PT:
        w, h, pal, pix = art.get('PT_' + n)
        if (w, h) != (32, 32) or pal != 'pt_' + n.lower():
            raise ArtError('PT_%s must be 32x32 on palette pt_%s' % (n, n.lower()))
        out.append(pix)
    return out


# ---- battle scene -----------------------------------------------------------

BB = ['PLAIN', 'WOODS', 'RUINS', 'CITY', 'NEST']          # battle backdrops; append only
BFX = ['MUZZLE', 'SLASH', 'ACID', 'SPARK', 'RISE']         # battle effects; append only
BFX_PAL = {'MUZZLE': 0, 'SLASH': 0, 'ACID': 1, 'SPARK': 0, 'RISE': 1}   # 0 cursor, 1 horde
BG_W, BG_H, BG_GROUND = 120, 104, 88
BATTLE_BG_MAX_TILES = 448
VEHICLES = ('PICKUP', 'BUS', 'ARMOR')


def scale2x(pix):
    """Scale2x/EPX: doubles a sprite while keeping diagonals chunky instead of jagged."""
    h, w = len(pix), len(pix[0])
    g = lambda x, y: pix[min(max(y, 0), h - 1)][min(max(x, 0), w - 1)]
    out = blank(w * 2, h * 2)
    for y in range(h):
        for x in range(w):
            P, A, B, C, D = pix[y][x], g(x, y - 1), g(x + 1, y), g(x - 1, y), g(x, y + 1)
            e0, e1, e2, e3 = P, P, P, P
            if C == A and C != D and A != B:
                e0 = A
            if A == B and A != C and B != D:
                e1 = B
            if D == C and D != B and C != A:
                e2 = C
            if B == D and B != A and D != C:
                e3 = D
            out[2 * y][2 * x], out[2 * y][2 * x + 1] = e0, e1
            out[2 * y + 1][2 * x], out[2 * y + 1][2 * x + 1] = e2, e3
    return out


def shift_rows(pix, y0, y1, dx):
    out = [row[:] for row in pix]
    w = len(pix[0])
    for y in range(y0, y1):
        out[y] = [pix[y][x - dx] if 0 <= x - dx < w else 0 for x in range(w)]
    return out


def battle_frames(art, base, f0, f1, kind):
    """4 battle frames for one unit: explicit BF_ sprites win, else derived poses.
    kind: 'gun' (survivor foot), 'veh' (vehicle), 'claw' (zombie)."""
    def pick(suffix, make):
        nm = base + suffix
        if art.has(nm):
            w, h, _, pix = art.get(nm)
            if (w, h) != (32, 32):
                raise ArtError('%s must be 32x32' % nm)
            return pix
        return make()
    i0 = pick('', lambda: scale2x(f0))
    i1 = pick('.f1', lambda: scale2x(f1))
    if kind == 'veh':
        a0 = pick('.a0', lambda: shift_rows(i0, 0, 32, 1))
        a1 = pick('.a1', lambda: shift_rows(i0, 0, 32, -1))
    elif kind == 'gun':
        a0 = pick('.a0', lambda: shift_rows(i0, 0, 22, 1))       # lean into the aim
        a1 = pick('.a1', lambda: shift_rows(i0, 0, 22, -1))      # recoil
    else:
        a0 = pick('.a0', lambda: shift_rows(i0, 0, 24, -1))      # rear back
        a1 = pick('.a1', lambda: shift_rows(i0, 0, 24, 2))       # lunge
    return [i0, i1, a0, a1]


def muzzle_point(pix, kind):
    best = None
    for y in range(4, 24):
        for x in range(31, -1, -1):
            if pix[y][x]:
                if best is None or x > best[0]:
                    best = (x, y)
                break
    if best is None:
        return (-1, -1)
    return (min(best[0] + 2, 31), best[1])


def build_battle_bg(art):
    P = art.palette('terrain').chars
    _, _, _, grass = art.get('MT_PLAIN')
    _, _, _, tex = art.get('road_tex')

    def sky():
        pix = blank(BG_W, BG_H, P['u'])
        for y in range(BG_H):
            for x in range(BG_W):
                if y < 16:
                    pix[y][x] = P['n']
                elif y < 32:
                    lvl = (y - 16 + 0.5) / 16.0
                    pix[y][x] = P['u'] if lvl > [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9],
                                                  [15, 7, 13, 5]][y & 3][x & 3] / 16.0 else P['n']
        for cx, cy, w in ((24, 22, 26), (88, 14, 20)):        # tile-aligned puffy clouds
            for y in range(cy - 6, cy + 4):
                for x in range(cx - w // 2, cx + w // 2):
                    dx = (x - cx) / (w / 2.0)
                    top = cy - 2 - int(4 * (1 - dx * dx)) - (2 if abs(dx) < 0.3 else 0)
                    if top <= y < cy + 3 and 0 <= x < BG_W:
                        pix[y][x] = P['c'] if y >= cy + 1 else P['w']
                if 0 <= y < BG_H:
                    pass
        return pix

    def stamp(pix, name, x0, y0, scale=1, skip='GlgX', keep_ground=False):
        w, h, _, sp = art.get(name)
        skipv = {P[c] for c in skip if c in P}
        for y in range(h):
            for x in range(w):
                v = sp[y][x]
                if v == 0 or (v in skipv and not keep_ground):
                    continue
                for sy in range(scale):
                    for sx in range(scale):
                        tx, ty = x0 + x * scale + sx, y0 + y * scale + sy
                        if 0 <= tx < BG_W and 0 <= ty < BG_H:
                            pix[ty][tx] = v

    def ground(pix, y0, kind):
        for y in range(y0, BG_H):
            for x in range(BG_W):
                if kind == 'grass':
                    pix[y][x] = grass[y % 16][x % 16]
                elif kind == 'dirt':
                    v = grass[y % 16][x % 16]
                    pix[y][x] = {P['G']: P['d'], P['l']: P['D'], P['g']: P['K']}.get(v, P['d'])
                elif kind == 'rubble':
                    v = tex[y % 16][x % 16]
                    pix[y][x] = P['c'] if v == P['a'] else P['a']
                elif kind == 'asphalt':
                    pix[y][x] = tex[y % 16][x % 16]
        for x in range(BG_W):                                  # horizon edge
            pix[y0][x] = P['K'] if kind != 'grass' else P['g']

    def road(pix, y0, y1):
        for y in range(y0, y1):
            for x in range(BG_W):
                pix[y][x] = tex[y % 16][x % 16]
        for x in range(BG_W):
            pix[y0][x] = pix[y1 - 1][x] = P['c']
            pix[y0 - 1][x] = pix[y1][x] = P['a']
            if (x // 8) % 2 == 0:
                pix[(y0 + y1) // 2][x] = P['D']

    def treeline(pix, y, col):
        for x in range(BG_W):
            hgt = 5 + int(3 * math.sin(x * 0.35) + 2 * math.sin(x * 0.11 + 1))
            for yy in range(y - hgt, y):
                pix[yy][x] = col

    out = {}
    # PLAIN / road: open field, far treeline, a road across the front
    p = sky()
    treeline(p, 72, P['g'])
    ground(p, 72, 'grass')
    road(p, 84, 98)
    out['PLAIN'] = p
    # WOODS: big trees behind, bushes at the sides
    p = sky()
    treeline(p, 72, P['g'])
    for x, y in ((-6, 40), (20, 44), (52, 38), (84, 42), (108, 40)):
        stamp(p, 'tree', x, y, 3, skip='')
    ground(p, 72, 'grass')
    for x in (-8, 104):
        stamp(p, 'tree', x, 76, 2, skip='')
    out['WOODS'] = p
    # RUINS: broken buildings behind, rubble underfoot
    p = sky()
    for x, y in ((-10, 24), (36, 30), (80, 22)):
        stamp(p, 'MT_RUINS', x, y, 3)
    ground(p, 72, 'rubble')
    stamp(p, 'MT_HILL', 96, 64, 2)
    stamp(p, 'MT_HILL', -12, 66, 2)
    out['RUINS'] = p
    # CITY: houses and the hospital behind a sidewalk and street
    p = sky()
    stamp(p, 'MT_HOUSE', -8, 24, 3)
    stamp(p, 'MT_HOSPITAL', 38, 24, 3)
    stamp(p, 'MT_HOUSE_LOOTED', 84, 24, 3)
    for y in range(72, 80):
        for x in range(BG_W):
            p[y][x] = P['c'] if (x % 16) and y != 79 else P['a']
    for y in range(80, BG_H):
        for x in range(BG_W):
            p[y][x] = tex[y % 16][x % 16]
    for x in range(BG_W):
        p[72][x] = P['w']
        if (x // 10) % 2 == 0:
            p[92][x] = P['D']
    out['CITY'] = p
    # NEST: dead trees, a gore mound pulsing behind, raw dirt with red veins
    p = sky()
    treeline(p, 72, P['K'])
    for x0 in (10, 46, 98):
        for yy in range(40, 72):
            p[yy][x0] = p[yy][x0 + 1] = P['K']
        for k in range(1, 8):
            for b in (-1, 1):
                xx, yy = x0 + b * k, 52 - k + (k // 3) * 3 + (8 if b > 0 else 0)
                if 0 <= xx < BG_W:
                    p[yy][xx] = P['K']
    stamp(p, 'MT_NEST', 36, 40, 3, skip='GlgX')
    ground(p, 72, 'dirt')
    for x in range(BG_W):
        y = 82 + int(4 * math.sin(x * 0.2)) + int(2 * math.sin(x * 0.07))
        p[y][x] = P['r']
        y2 = 94 + int(3 * math.sin(x * 0.15 + 2))
        p[y2][x] = P['r']
    out['NEST'] = p
    ts = TileSet()
    maps = {}
    for n in BB:
        pix = out[n]
        for row in pix:
            if 0 in row:
                raise ArtError('battle backdrop %s uses index 0' % n)
        maps[n] = [[ts.add(tile_at(pix, tx * 8, ty * 8)) for tx in range(BG_W // 8)] for ty in range(BG_H // 8)]
    if len(ts.tiles) > BATTLE_BG_MAX_TILES:
        raise ArtError('battle backdrops use %d tiles (max %d)' % (len(ts.tiles), BATTLE_BG_MAX_TILES))
    return ts, maps, out


def build_battle(art, R):
    figs, muz = [], []
    for k, n in enumerate(UG):
        f0, f1 = R['units'][k]
        kind = 'veh' if n in VEHICLES else ('gun' if k < UG_HORDE_FIRST else 'claw')
        fr = battle_frames(art, 'BF_' + n, f0, f1, kind)
        figs.append(fr)
        muz.append((-1, -1) if n == 'BUS' else muzzle_point(fr[3], kind))
    bosses = []
    for k, n in enumerate(BOSS):
        f0, f1 = R['bosses'][k]
        bosses.append(battle_frames(art, 'BF_BOSS_' + n, f0, f1, 'claw'))
    fx = []
    for n in BFX:
        frames = []
        for f in range(4):
            nm = 'bfx_%s%d' % (n.lower(), f)
            if art.has(nm):
                frames.append(art.get(nm)[3])
            elif n == 'RISE':
                frames.append(scale2x(art.get('fx_rise%d' % f)[3]))
            else:
                raise ArtError('missing battle effect %s' % nm)
        fx.append(frames)
    ts, maps, pix = build_battle_bg(art)
    return dict(figs=figs, muzzle=muz, bosses=bosses, fx=fx, bg_ts=ts, bg_maps=maps, bg_pix=pix)


# ---- title -----------------------------------------------------------------

def build_title(art):
    P = art.palette('title').chars
    W, H = 240, 160
    rnd = random.Random(1337)
    pix = blank(W, H, P['A'])
    sky = [P['A'], P['B'], P['C'], P['D']]
    # sky gradient: 4 bands with 2x2 ordered-dither transitions, all tile-aligned
    bands = [(0, 48, 0), (48, 80, 1), (80, 104, 2), (104, 160, 3)]
    for y0, y1, k in bands:
        for y in range(y0, y1):
            for x in range(W):
                pix[y][x] = sky[k]

    def bayer(x, y):
        return [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]][y & 3][x & 3] / 16.0 + 1 / 32.0

    for y0, y1, k in bands[1:]:
        for y in range(y0 - 8, y0):
            for x in range(W):
                lvl = (y - (y0 - 8) + 0.5) / 8.0
                pix[y][x] = sky[k] if lvl > bayer(x, y) else sky[k - 1]
    # stars (sparse, so they cost few tiles)
    for _ in range(34):
        x, y = rnd.randrange(W), rnd.randrange(2, 76)
        pix[y][x] = P['M'] if rnd.random() < 0.6 else P['m']
    # moon (top right, clear of the logo)
    mw, mh, _, moon = art.get('moon')
    mx, my = 200, 4
    for y in range(mh):
        for x in range(mw):
            if moon[y][x]:
                pix[my + y][mx + x] = moon[y][x]

    def skyline(base_y, colour, seed, minw, maxw, minh, maxh, lit, win_col, win_p):
        r = random.Random(seed)
        x = -r.randrange(0, 6)
        while x < W:
            bw = r.randrange(minw, maxw + 1)
            bh = r.randrange(minh, maxh + 1)
            top = base_y - bh
            tops = [top] * bw
            if r.random() < 0.65:
                # collapsed: the roofline crumbles away down a jagged slope
                start = r.randrange(bw // 4, max(bw // 4 + 1, 3 * bw // 4))
                t = top
                left = r.random() < 0.5
                cols = range(start, bw) if not left else range(bw - 1 - start, -1, -1)
                for c in cols:
                    t += r.choice((0, 1, 1, 2, 3))
                    tops[c] = min(t, base_y - 4)
            for c in range(bw):
                xx = x + c
                if 0 <= xx < W:
                    for yy in range(max(tops[c], 0), base_y):
                        pix[yy][xx] = colour
            # antenna / water tower / rebar
            if r.random() < 0.4:
                ax = x + r.randrange(1, max(2, bw - 1))
                if 0 <= ax < W:
                    for yy in range(tops[ax - x] - r.randrange(3, 10), tops[ax - x]):
                        if yy >= 0:
                            pix[yy][ax] = colour
            # windows: a regular grid, a few of them lit
            for wy in range(top + 3, base_y - 4, 5):
                for wx in range(x + 2, x + bw - 2, 3):
                    if 0 <= wx < W and wy - 1 >= tops[wx - x] + 1 and r.random() < win_p:
                        pix[wy][wx] = lit if r.random() < 0.8 else win_col
                        pix[wy + 1][wx] = win_col if lit != win_col else lit
            x += bw + r.randrange(0, 3)

    skyline(130, P['E'], 7, 10, 22, 34, 66, P['C'], P['C'], 0.10)
    skyline(138, P['F'], 3, 8, 18, 18, 50, P['y'], P['k'], 0.20)
    # street: dark tarmac below a band of ground fog
    for y in range(134, H):
        for x in range(W):
            pix[y][x] = P['E'] if y >= 140 else pix[y][x]
    for y in range(126, 142):
        for x in range(W):
            lvl = (1 - abs(y - 136) / 6.0) * 0.7
            if lvl > bayer(x, y):
                pix[y][x] = P['C'] if lvl > 0.5 else P['E']
    for x in range(W):
        if (x // 6) % 3 == 0:
            pix[150][x] = P['F']

    def stamp_zombie(name, zx, base, flip, scale=1):
        zw, zh, _, zp = art.get(name)
        zy = base - zh * scale
        on = set()
        for y in range(zh * scale):
            for x in range(zw * scale):
                v = zp[y // scale][(zw - 1 - x // scale) if flip else x // scale]
                if v and 0 <= zx + x < W and 0 <= zy + y < H:
                    pix[zy + y][zx + x] = v
                    on.add((zx + x, zy + y))
        # moonlight rim on the up/right edges
        for (x, y) in on:
            if pix[y][x] == P['K'] and ((x + 1, y) not in on or (x, y - 1) not in on) \
                    and (x + 1, y - 1) not in on:
                pix[y][x] = P['D']

    for name, zx, flip in (('zombie_b', 40, False), ('zombie_c', 92, False),
                           ('zombie_a', 120, True), ('zombie_c', 150, True),
                           ('zombie_b', 178, True)):
        stamp_zombie(name, zx, 152, flip)
    stamp_zombie('zombie_a', -2, 162, False, 2)
    stamp_zombie('zombie_b', 214, 164, True, 2)

    # logo
    gw, gh, gl = art.glyphs['logo']
    text = 'DEAD WARS'
    scale = 2
    adv = []
    for ch in text:
        adv.append(4 * scale if ch == ' ' else (len(gl[ord(ch)][0]) + 1) * scale)
    total = sum(adv) - scale
    lx, ly = (W - total) // 2 - 1, 38
    mask = blank(W, H, 0)
    x = lx
    for ch, a in zip(text, adv):
        if ch != ' ':
            g = gl[ord(ch)]
            for gy, row in enumerate(g):
                for gx, v in enumerate(row):
                    if v:
                        for sy in range(scale):
                            for sx in range(scale):
                                mask[ly + gy * scale + sy][x + gx * scale + sx] = 1
        x += a
    lh = gh * scale
    # blood drips under some columns
    dr = random.Random(5)
    x = lx
    while x < W - 1:
        if mask[ly + lh - 1][x] and mask[ly + lh - 1][x + 1] and dr.random() < 0.16:
            n = dr.randrange(3, 14)
            for k in range(n):
                mask[ly + lh + k][x] = 2
                if k < n - 3:
                    mask[ly + lh + k][x + 1] = 2
            mask[ly + lh + n][x] = 2
            x += 5
        x += 1
    # 3D extrusion down-right
    ext = blank(W, H, 0)
    for y in range(H):
        for x in range(W):
            if mask[y][x] == 1:
                for k in (1, 2, 3):
                    if y + k < H and x + k < W and not mask[y + k][x + k]:
                        ext[y + k][x + k] = 1
    # outline around letters + extrusion + drips
    for y in range(H):
        for x in range(W):
            if mask[y][x] or ext[y][x]:
                continue
            near = False
            for dy in (-2, -1, 0, 1, 2):
                for dx in (-2, -1, 0, 1, 2):
                    if abs(dx) + abs(dy) <= 2 and 0 <= y + dy < H and 0 <= x + dx < W \
                            and (mask[y + dy][x + dx] or ext[y + dy][x + dx]):
                        near = True
            if near:
                pix[y][x] = P['K']
    for y in range(H):
        for x in range(W):
            if ext[y][x]:
                pix[y][x] = P['k']
    for y in range(H):
        for x in range(W):
            m = mask[y][x]
            if not m:
                continue
            if m == 2:
                pix[y][x] = P['r']
                continue
            rel = (y - ly) / float(lh)
            if rel < 0.08 and (not mask[y - 1][x] or y - ly < 2):
                c = P['h']
            elif not mask[y][x - 1] and rel < 0.5:
                c = P['h']
            elif rel < 0.45:
                c = P['R']
            elif rel < 0.7:
                c = P['R'] if (0.7 - rel) / 0.25 > bayer(x, y) else P['r']
            else:
                c = P['r']
            pix[y][x] = c
    # subtitle bar letters are left to the engine (font uses title palette 1/2)

    ts = TileSet()
    tmap = []
    for ty in range(20):
        row = []
        for tx in range(30):
            row.append(ts.add(tile_at(pix, tx * 8, ty * 8)))
        tmap.append(row)
    return ts, tmap, pix


# --------------------------------------------------------------------------
# output

def c_bytes(data, indent='    '):
    lines = []
    for k in range(0, len(data), 16):
        lines.append(indent + ', '.join('0x%02X' % b for b in data[k:k + 16]) + ',')
    return '\n'.join(lines)


def c_u16(vals, indent='    '):
    lines = []
    for k in range(0, len(vals), 8):
        lines.append(indent + ', '.join('0x%04X' % v for v in vals[k:k + 8]) + ',')
    return '\n'.join(lines)


def pal_vals(pal):
    return [bgr555(c) for c in pal.colors]


def write_c(gen_dir, art, R):
    ALIGN = '__attribute__((aligned(4)))'
    h = []
    h.append('/* Generated by tools/gfx.py from art/ (text pixel grids) -- DO NOT EDIT. See docs/ASSET_SPEC.md */')
    h.append('#ifndef DEADWARS_ASSETS_H\n#define DEADWARS_ASSETS_H\n\n#include <stdint.h>\n')
    h.append('/* ---- palettes (BGR555, index 0 = transparent) ---- */')
    for n in PAL_SINGLE:
        h.append('extern const uint16_t pal_%s[16];' % n)
    h.append('\n/* ---- terrain: 16x16 metatiles ---- */')
    h.append('enum { ' + ', '.join('MT_' + n for n in MT_BASE) + ',')
    h.append('       MT_ROAD0, /* + 16 autotile variants: MT_ROAD0 + mask, N=1 E=2 S=4 W=8 */')
    h.append('       MT_COUNT = MT_ROAD0 + 16 };')
    h.append('#define TERRAIN_TILE_COUNT %d' % len(R['terrain_ts'].tiles))
    h.append('extern const uint8_t  terrain_tiles[TERRAIN_TILE_COUNT * 32];')
    h.append('extern const uint16_t terrain_meta[MT_COUNT][4];  /* TL,TR,BL,BR: tile | hflip<<10 | vflip<<11 */')
    h.append('#define TERRAIN_TEAM_LIGHT 13\n#define TERRAIN_TEAM_MID   14\n#define TERRAIN_TEAM_DARK  15')
    h.append('\n/* ---- move / attack overlay ---- */')
    h.append('enum { ' + ', '.join('OV_' + n for n in OV) + ', OV_COUNT };')
    h.append('#define OVERLAY_TILE_COUNT %d' % len(R['overlay_ts'].tiles))
    h.append('extern const uint8_t  overlay_tiles[OVERLAY_TILE_COUNT * 32];')
    h.append('extern const uint16_t overlay_meta[OV_COUNT][4];')
    h.append('\n/* ---- units: 16x16 OBJ, 2 frames, 1D tile order TL,TR,BL,BR, facing right ---- */')
    h.append('enum { ' + ', '.join('UG_' + n for n in UG) + ', UG_COUNT };')
    h.append('#define UG_FIRST_HORDE UG_%s' % UG[UG_HORDE_FIRST])
    h.append('extern const uint8_t unit_gfx[UG_COUNT][2][128];')
    h.append('\n/* ---- boss units: 16x16 OBJ, 2 frames, same layout as unit_gfx, pal_horde ---- */')
    h.append('enum { ' + ', '.join('BOSS_' + n for n in BOSS) + ', BOSS_COUNT };')
    h.append('extern const uint8_t boss_gfx[BOSS_COUNT][2][128];')
    h.append('\n/* ---- battle scene (full-screen split view; see docs/ASSET_SPEC.md) ---- */')
    h.append('enum { BF_IDLE0, BF_IDLE1, BF_ATK0, BF_ATK1, BF_FRAMES };')
    h.append('extern const uint8_t battle_fig_gfx[UG_COUNT][BF_FRAMES][512];    /* 32x32, facing right, map sprite palette */')
    h.append('extern const uint8_t battle_boss_gfx[BOSS_COUNT][BF_FRAMES][512]; /* pal_horde */')
    h.append('extern const int8_t  battle_fig_muzzle[UG_COUNT][2];  /* x,y of the impact point in BF_ATK1; -1 = none */')
    h.append('enum { ' + ', '.join('BB_' + n for n in BB) + ', BB_COUNT };')
    h.append('#define BATTLE_BG_W 15  /* tiles; panel is 120x104, drawn for the right side */')
    h.append('#define BATTLE_BG_H 13')
    h.append('#define BATTLE_BG_GROUND %d  /* y of the ground line in the panel */' % BG_GROUND)
    h.append('#define BATTLE_BG_TILE_COUNT %d' % len(R['battle']['bg_ts'].tiles))
    h.append('extern const uint8_t  battle_bg_tiles[BATTLE_BG_TILE_COUNT * 32];  /* pal_terrain */')
    h.append('extern const uint16_t battle_bg_map[BB_COUNT][BATTLE_BG_H][BATTLE_BG_W];')
    h.append('enum { ' + ', '.join('BFX_' + n for n in BFX) + ', BFX_COUNT };')
    h.append('extern const uint8_t battle_fx_gfx[BFX_COUNT][4][512];  /* 32x32, centre = impact point */')
    h.append('extern const uint8_t battle_fx_pal[BFX_COUNT];          /* 0 = pal_cursor, 1 = pal_horde */')
    h.append('\n/* ---- cursor / small UI sprites (pal_cursor) ---- */')
    h.append('extern const uint8_t cursor_gfx[2][128];')
    h.append('extern const uint8_t digit_gfx[10][32];')
    h.append('extern const uint8_t pointer_gfx[32];')
    h.append('extern const uint8_t explode_gfx[4][128];')
    h.append('\n/* ---- font + windows (pal_ui; font: 1 = text, 2 = shadow) ---- */')
    h.append('extern const uint8_t font_tiles[96][32];   /* ASCII 32..127 */')
    h.append('extern const uint8_t window_tiles[9][32];  /* TL,T,TR,L,C,R,BL,B,BR */')
    h.append('\n/* ---- portraits: 32x32 OBJ, 16 tiles row-major ---- */')
    h.append('enum { ' + ', '.join('PT_' + n for n in PT) + ', PORTRAIT_COUNT };')
    h.append('extern const uint16_t pal_portrait[PORTRAIT_COUNT][16];')
    h.append('extern const uint8_t  portrait_gfx[PORTRAIT_COUNT][512];')
    h.append('\n/* ---- title screen 240x160 (pal_title; idx 1/2 = white/black so font_tiles render on it) ---- */')
    h.append('#define TITLE_TILE_COUNT %d' % len(R['title_ts'].tiles))
    h.append('extern const uint8_t  title_tiles[TITLE_TILE_COUNT * 32];')
    h.append('extern const uint16_t title_map[20][30];')
    h.append('\n#endif')

    c = []
    c.append('/* Generated by tools/gfx.py from art/ (text pixel grids) -- DO NOT EDIT. */')
    c.append('#include "assets.h"\n')
    for n in PAL_SINGLE:
        c.append('const uint16_t pal_%s[16] %s = {\n%s\n};' % (n, ALIGN, c_u16(pal_vals(art.palette(n)))))
    c.append('const uint16_t pal_portrait[PORTRAIT_COUNT][16] %s = {' % ALIGN)
    for n in PT:
        c.append('    { /* %s */\n%s\n    },' % (n, c_u16(pal_vals(art.palette('pt_' + n.lower())), '        ')))
    c.append('};\n')

    def tiles_arr(name, ts, count_macro):
        c.append('const uint8_t %s[%s * 32] %s = {\n%s\n};' % (name, count_macro, ALIGN, c_bytes(ts.data())))

    def meta_arr(name, meta, dim, names):
        c.append('const uint16_t %s[%s][4] = {' % (name, dim))
        for nm, m in zip(names, meta):
            c.append('    { %s }, /* %s */' % (', '.join('0x%04X' % v for v in m), nm))
        c.append('};\n')

    tiles_arr('terrain_tiles', R['terrain_ts'], 'TERRAIN_TILE_COUNT')
    meta_arr('terrain_meta', R['terrain_meta'], 'MT_COUNT',
             ['MT_' + n for n in MT_BASE] + ['MT_ROAD0+%d' % m for m in range(16)])
    tiles_arr('overlay_tiles', R['overlay_ts'], 'OVERLAY_TILE_COUNT')
    meta_arr('overlay_meta', R['overlay_meta'], 'OV_COUNT', ['OV_' + n for n in OV])

    c.append('const uint8_t unit_gfx[UG_COUNT][2][128] %s = {' % ALIGN)
    for n, (f0, f1) in zip(UG, R['units']):
        c.append('    { /* UG_%s */' % n)
        for f in (f0, f1):
            c.append('        {\n%s\n        },' % c_bytes(sprite_tiles_1d(f, 16, 16), '            '))
        c.append('    },')
    c.append('};\n')

    c.append('const uint8_t boss_gfx[BOSS_COUNT][2][128] %s = {' % ALIGN)
    for n, (f0, f1) in zip(BOSS, R['bosses']):
        c.append('    { /* BOSS_%s */' % n)
        for f in (f0, f1):
            c.append('        {\n%s\n        },' % c_bytes(sprite_tiles_1d(f, 16, 16), '            '))
        c.append('    },')
    c.append('};\n')

    B = R['battle']

    def fig_arr(name, dim, names, sets):
        c.append('const uint8_t %s[%s][BF_FRAMES][512] %s = {' % (name, dim, ALIGN))
        for nm, frames in zip(names, sets):
            c.append('    { /* %s */' % nm)
            for f in frames:
                c.append('        {\n%s\n        },' % c_bytes(sprite_tiles_1d(f, 32, 32), '            '))
            c.append('    },')
        c.append('};\n')

    fig_arr('battle_fig_gfx', 'UG_COUNT', ['UG_' + n for n in UG], B['figs'])
    fig_arr('battle_boss_gfx', 'BOSS_COUNT', ['BOSS_' + n for n in BOSS], B['bosses'])
    c.append('const int8_t battle_fig_muzzle[UG_COUNT][2] = {')
    for n, (x, y) in zip(UG, B['muzzle']):
        c.append('    { %d, %d }, /* UG_%s */' % (x, y, n))
    c.append('};\n')
    c.append('const uint8_t battle_bg_tiles[BATTLE_BG_TILE_COUNT * 32] %s = {\n%s\n};' % (ALIGN, c_bytes(B['bg_ts'].data())))
    c.append('const uint16_t battle_bg_map[BB_COUNT][BATTLE_BG_H][BATTLE_BG_W] = {')
    for n in BB:
        c.append('    { /* BB_%s */' % n)
        for row in B['bg_maps'][n]:
            c.append('        { %s },' % ', '.join('0x%04X' % v for v in row))
        c.append('    },')
    c.append('};\n')
    c.append('const uint8_t battle_fx_gfx[BFX_COUNT][4][512] %s = {' % ALIGN)
    for n, frames in zip(BFX, B['fx']):
        c.append('    { /* BFX_%s */' % n)
        for f in frames:
            c.append('        {\n%s\n        },' % c_bytes(sprite_tiles_1d(f, 32, 32), '            '))
        c.append('    },')
    c.append('};\n')
    c.append('const uint8_t battle_fx_pal[BFX_COUNT] = { %s };\n' % ', '.join(str(BFX_PAL[n]) for n in BFX))

    c.append('const uint8_t cursor_gfx[2][128] %s = {' % ALIGN)
    for f in R['cursor']:
        c.append('    {\n%s\n    },' % c_bytes(sprite_tiles_1d(f, 16, 16), '        '))
    c.append('};\n')
    c.append('const uint8_t digit_gfx[10][32] %s = {' % ALIGN)
    for d, t in enumerate(R['digits']):
        c.append('    { /* %d */\n%s\n    },' % (d, c_bytes(sprite_tiles_1d(t, 8, 8), '        ')))
    c.append('};\n')
    c.append('const uint8_t pointer_gfx[32] %s = {\n%s\n};\n' % (ALIGN, c_bytes(sprite_tiles_1d(R['pointer'], 8, 8))))
    c.append('const uint8_t explode_gfx[4][128] %s = {' % ALIGN)
    for f in R['explode']:
        c.append('    {\n%s\n    },' % c_bytes(sprite_tiles_1d(f, 16, 16), '        '))
    c.append('};\n')

    c.append('const uint8_t font_tiles[96][32] %s = {' % ALIGN)
    for k, t in enumerate(R['font']):
        ch = chr(32 + k)
        label = repr(ch) if ch not in '*/\\' else 'code %d' % (32 + k)
        c.append('    { /* %s */\n%s\n    },' % (label.replace('*/', ''), c_bytes(sprite_tiles_1d(t, 8, 8), '        ')))
    c.append('};\n')
    c.append('const uint8_t window_tiles[9][32] %s = {' % ALIGN)
    for nm, t in zip(['TL', 'T', 'TR', 'L', 'C', 'R', 'BL', 'B', 'BR'], R['window']):
        c.append('    { /* %s */\n%s\n    },' % (nm, c_bytes(sprite_tiles_1d(t, 8, 8), '        ')))
    c.append('};\n')

    c.append('const uint8_t portrait_gfx[PORTRAIT_COUNT][512] %s = {' % ALIGN)
    for n, p in zip(PT, R['portraits']):
        c.append('    { /* PT_%s */\n%s\n    },' % (n, c_bytes(sprite_tiles_1d(p, 32, 32), '        ')))
    c.append('};\n')

    tiles_arr('title_tiles', R['title_ts'], 'TITLE_TILE_COUNT')
    c.append('const uint16_t title_map[20][30] = {')
    for row in R['title_map']:
        c.append('    { %s },' % ', '.join('0x%04X' % v for v in row))
    c.append('};')

    os.makedirs(gen_dir, exist_ok=True)
    _write_if_changed(os.path.join(gen_dir, 'assets.h'), '\n'.join(h) + '\n')
    _write_if_changed(os.path.join(gen_dir, 'assets.c'), '\n'.join(c) + '\n')


def _write_if_changed(path, text):
    if os.path.exists(path) and open(path).read() == text:
        os.utime(path, None)
        return
    with open(path, 'w') as f:
        f.write(text)


# --------------------------------------------------------------------------
# previews (decoded back from the generated tile data, so they test the pipeline)

TEAM_COLORS = {
    'neutral': None,
    'blue': [(144, 192, 255), (64, 112, 232), (24, 48, 144)],
    'red': [(248, 112, 96), (200, 40, 40), (112, 16, 24)],
}
SCALE = 3


def pal_rgb(colors, team=None):
    cols = [gba_rgb(c) for c in colors]
    if team and TEAM_COLORS.get(team):
        cols[13:16] = [gba_rgb(c) for c in TEAM_COLORS[team]]
    return cols


def blit_tile(img, t, x0, y0, cols, transparent=True):
    px = img.load()
    for y in range(8):
        for x in range(8):
            v = t[y * 8 + x]
            if v or not transparent:
                if 0 <= x0 + x < img.width and 0 <= y0 + y < img.height:
                    px[x0 + x, y0 + y] = cols[v]


def blit_meta(img, tiles, meta, x0, y0, cols, transparent=True):
    for k, (dx, dy) in enumerate(((0, 0), (8, 0), (0, 8), (8, 8))):
        blit_tile(img, decode_se(tiles, meta[k]), x0 + dx, y0 + dy, cols, transparent)


def blit_obj(img, data, w, h, x0, y0, cols):
    k = 0
    for ty in range(h // 8):
        for tx in range(w // 8):
            blit_tile(img, bytes_to_tile(data[k * 32:(k + 1) * 32]), x0 + tx * 8, y0 + ty * 8, cols)
            k += 1


def up(img, s=SCALE):
    return img.resize((img.width * s, img.height * s), Image.NEAREST)


def label(img, x, y, text, fill=(255, 255, 255)):
    d = ImageDraw.Draw(img)
    d.text((x + 1, y + 1), text, fill=(0, 0, 0))
    d.text((x, y), text, fill=fill)


def write_previews(prev_dir, art, R):
    os.makedirs(prev_dir, exist_ok=True)
    tpal = art.palette('terrain').colors
    tt = R['terrain_ts'].tiles
    meta = R['terrain_meta']
    names = MT_BASE + ['ROAD%d' % m for m in range(16)]
    BG = (40, 40, 48)

    # --- terrain sheet: every metatile (neutral), buildings in 3 team colours
    cols_n = 9
    cell = 16 * SCALE + 16
    rows = (len(names) + cols_n - 1) // cols_n
    sheet = Image.new('RGB', (cols_n * cell + 8, rows * (cell + 12) + 3 * (cell + 12) + 30), BG)
    for k, nm in enumerate(names):
        im = Image.new('RGB', (16, 16))
        blit_meta(im, tt, meta[k], 0, 0, pal_rgb(tpal), False)
        x, y = 8 + (k % cols_n) * cell, 4 + (k // cols_n) * (cell + 12)
        sheet.paste(up(im), (x, y + 12))
        label(sheet, x, y, nm)
    y0 = 4 + rows * (cell + 12) + 10
    for r, team in enumerate(TEAM_COLORS):
        for k, nm in enumerate(MT_BUILDINGS):
            im = Image.new('RGB', (16, 16))
            blit_meta(im, tt, meta[MT_BASE.index(nm)], 0, 0, pal_rgb(tpal, team), False)
            x, y = 8 + k * cell, y0 + r * (cell + 12)
            sheet.paste(up(im), (x, y + 12))
            label(sheet, x, y, team if k == 0 else '')
    sheet.save(os.path.join(prev_dir, 'terrain.png'))

    # --- units sheet
    upal = {'survivor': pal_rgb(art.palette('survivor').colors),
            'horde': pal_rgb(art.palette('horde').colors)}
    plain_meta = meta[0]
    allu = [(n, R['units'][k], upal['survivor' if k < UG_HORDE_FIRST else 'horde']) for k, n in enumerate(UG)]
    allu += [('*' + n, R['bosses'][k], upal['horde']) for k, n in enumerate(BOSS)]
    sheet = Image.new('RGB', (len(allu) * 44 + 8, 2 * 60 + 20), BG)
    for k, (n, fr, cols) in enumerate(allu):
        for f in range(2):
            im = Image.new('RGB', (16, 16), (96, 96, 112) if f == 0 else (0, 0, 0))
            blit_meta(im, tt, plain_meta, 0, 0, pal_rgb(tpal), False) if f == 1 else None
            blit_obj(im, sprite_tiles_1d(fr[f], 16, 16), 16, 16, 0, 0, cols)
            x, y = 8 + k * 44, 16 + f * 60
            sheet.paste(up(im, 2), (x, y))
        label(sheet, 8 + k * 44, 2, n[:7])
    # frame 0 on grass at 3x on a second row band
    big = Image.new('RGB', (len(allu) * 52 + 8, 64), BG)
    for k, (n, fr, cols) in enumerate(allu):
        im = Image.new('RGB', (16, 16))
        blit_meta(im, tt, plain_meta, 0, 0, pal_rgb(tpal), False)
        blit_obj(im, sprite_tiles_1d(fr[0], 16, 16), 16, 16, 0, 0, cols)
        big.paste(up(im), (8 + k * 52, 8))
    out = Image.new('RGB', (max(sheet.width, big.width), sheet.height + big.height), BG)
    out.paste(sheet, (0, 0))
    out.paste(big, (0, sheet.height))
    out.save(os.path.join(prev_dir, 'units.png'))

    # --- UI sheet: font, window w/ text, cursor, digits, pointer, overlay
    ui = pal_rgb(art.palette('ui').colors)
    cur = pal_rgb(art.palette('cursor').colors)
    ov = pal_rgb(art.palette('overlay').colors)
    im = Image.new('RGB', (240, 160), (0, 0, 0))
    fill = ui[3]
    # font chart on window
    def draw_window(x0, y0, wt, ht):
        for ty in range(ht):
            for tx in range(wt):
                r = 0 if ty == 0 else (2 if ty == ht - 1 else 1)
                c_ = 0 if tx == 0 else (2 if tx == wt - 1 else 1)
                blit_tile(im, tuple(v for row in R['window'][r * 3 + c_] for v in row),
                          x0 + tx * 8, y0 + ty * 8, ui)

    def draw_text(x0, y0, s):
        for k, ch in enumerate(s):
            code = ord(ch)
            t = R['font'][code - 32]
            blit_tile(im, tuple(v for row in t for v in row), x0 + k * 8, y0, ui)

    draw_window(0, 0, 30, 9)
    for r in range(6):
        draw_text(8, 8 + r * 8, ''.join(chr(32 + r * 16 + k) for k in range(16)))
    draw_window(0, 72, 18, 6)
    draw_text(8, 80, 'Mara: Hold the')
    draw_text(8, 88, 'line! Zombies at')
    draw_text(8, 96, 'the gate, 12 HP.')
    draw_text(8, 104, 'Fire  Wait  End?')
    blit_tile(im, tuple(v for row in R['pointer'] for v in row), 150, 136, cur)
    # overlay demo over plain/forest blended 50%
    demo = Image.new('RGB', (64, 32))
    for k in range(4):
        blit_meta(demo, tt, meta[k % 3], k * 16, 0, pal_rgb(tpal), False)
        blit_meta(demo, tt, meta[(k + 1) % 3], k * 16, 16, pal_rgb(tpal), False)
    ovl = demo.copy()
    for k in range(4):
        blit_meta(ovl, R['overlay_ts'].tiles, R['overlay_meta'][1 if k < 2 else 2], k * 16, 0, ov)
        blit_meta(ovl, R['overlay_ts'].tiles, R['overlay_meta'][1 if k < 2 else 2], k * 16, 16, ov)
    demo = Image.blend(demo, ovl, 0.5)
    im.paste(demo, (160, 80))
    # cursor frames + digits on a plain tile
    for f in range(2):
        blit_meta(im, tt, meta[0], 160 + f * 24, 120, pal_rgb(tpal), False)
        blit_obj(im, sprite_tiles_1d(R['cursor'][f], 16, 16), 16, 16, 160 + f * 24, 120, cur)
    for d in range(10):
        blit_meta(im, tt, meta[0], 4 + d * 16, 124, pal_rgb(tpal), False) if d < 9 else None
        blit_tile(im, tuple(v for row in R['digits'][d] for v in row), 4 + d * 16 + 8, 124 + 8, cur)
    up(im).save(os.path.join(prev_dir, 'ui.png'))

    # --- portraits
    sheet = Image.new('RGB', (len(PT) * 40 + 8, 48), BG)
    for k, n in enumerate(PT):
        cols = pal_rgb(art.palette('pt_' + n.lower()).colors)
        im = Image.new('RGB', (32, 32), (24, 24, 32))
        blit_obj(im, sprite_tiles_1d(R['portraits'][k], 32, 32), 32, 32, 0, 0, cols)
        sheet.paste(im, (8 + k * 40, 8))
    up(sheet).save(os.path.join(prev_dir, 'portraits.png'))

    # --- title
    tcols = pal_rgb(art.palette('title').colors)
    im = Image.new('RGB', (240, 160))
    for ty in range(20):
        for tx in range(30):
            blit_tile(im, decode_se(R['title_ts'].tiles, R['title_map'][ty][tx]), tx * 8, ty * 8, tcols, False)
    for k, ch in enumerate('PRESS START'):
        blit_tile(im, tuple(v for row in R['font'][ord(ch) - 32] for v in row), 76 + k * 8, 112, tcols)
    up(im).save(os.path.join(prev_dir, 'title.png'))

    # --- scene mock-up (1x and 3x): readability check
    write_scene(prev_dir, art, R)
    write_battle_previews(prev_dir, art, R)


def night_cols(cols, terrain):
    """mirror video.c palettes_apply(): terrain 7/16 toward (2,3,10), units 3/16 toward (4,6,14)"""
    t, n = ((2, 3, 10), 7) if terrain else ((4, 6, 14), 3)
    out = []
    for c in cols:
        c5 = [v >> 3 for v in c]
        c5 = [c5[k] + (t[k] - c5[k]) * n // 16 for k in range(3)]
        out.append(tuple((v << 3) | (v >> 2) for v in c5))
    return out


# squad slots in a 120x104 panel for the side facing right (x, y = top-left of a 32x32),
# drawn in list order (back to front); the first N are used for N displayed HP
BATTLE_SLOTS = [(10, 50), (40, 48), (70, 50), (24, 58), (54, 57), (84, 58),
                (6, 66), (36, 66), (66, 67), (20, 72)]


def battle_mock(art, R, left, right, night=False):
    """240x160 battle screen: (unit, hp, backdrop) for each side; left faces right"""
    B = R['battle']
    tcol = pal_rgb(art.palette('terrain').colors)
    pals = {'survivor': pal_rgb(art.palette('survivor').colors), 'horde': pal_rgb(art.palette('horde').colors),
            'cursor': pal_rgb(art.palette('cursor').colors)}
    if night:
        tcol = night_cols(tcol, True)
        pals = {k: (night_cols(v, False) if k != 'cursor' else v) for k, v in pals.items()}
    im = Image.new('RGB', (240, 160), (16, 16, 24))
    ui = pal_rgb(art.palette('ui').colors)
    for side, (unit, hp, bb) in enumerate((left, right)):
        panel = Image.new('RGB', (BG_W, BG_H))
        for ty, row in enumerate(B['bg_maps'][bb]):
            for tx, se in enumerate(row):
                blit_tile(panel, decode_se(B['bg_ts'].tiles, se), tx * 8, ty * 8, tcol, False)
        if side == 0:
            panel = panel.transpose(Image.FLIP_LEFT_RIGHT)
        if unit.startswith('BOSS_'):
            frames, palname = B['bosses'][BOSS.index(unit[5:])], 'horde'
        else:
            k = UG.index(unit)
            frames, palname = B['figs'][k], 'survivor' if k < UG_HORDE_FIRST else 'horde'
        for i in range(hp):
            x, y = BATTLE_SLOTS[i]
            f = frames[(i & 1) if side == 1 else (3 if i % 3 == 0 else 0)]
            if side == 1:
                f = [row[::-1] for row in f]
                x = BG_W - 32 - x
            blit_obj(panel, sprite_tiles_1d(f, 32, 32), 32, 32, x, y, pals[palname])
        im.paste(panel, (side * 120, 0))
    # effects on the defender and at the attacker's muzzle
    B_fx = dict(zip(BFX, B['fx']))
    lu = left[0]
    if not lu.startswith('BOSS_') and UG.index(lu) < UG_HORDE_FIRST and B['muzzle'][UG.index(lu)][0] >= 0:
        mx, my = B['muzzle'][UG.index(lu)]
        x, y = BATTLE_SLOTS[0]
        blit_obj(im, sprite_tiles_1d(B_fx['MUZZLE'][1], 32, 32), 32, 32, x + mx - 16, y + my - 16, pals['cursor'])
        blit_obj(im, sprite_tiles_1d(B_fx['SPARK'][1], 32, 32), 32, 32, 160, 60, pals['cursor'])
    else:
        blit_obj(im, sprite_tiles_1d(B_fx['SLASH'][1], 32, 32), 32, 32, 160, 58, pals['cursor'])
    # divider + bottom UI windows (stand-ins for the engine's)
    for y in range(BG_H):
        im.putpixel((119, y), (8, 8, 16))
        im.putpixel((120, y), (8, 8, 16))
    d = ImageDraw.Draw(im)
    for side in range(2):
        x0 = side * 120
        d.rectangle((x0 + 2, BG_H + 2, x0 + 117, 157), fill=ui[3], outline=ui[4])
        name = (left if side == 0 else right)[0].replace('BOSS_', '')
        d.text((x0 + 8, BG_H + 8), name.title(), fill=ui[1])
        d.text((x0 + 8, BG_H + 24), '%d HP' % (left if side == 0 else right)[1], fill=ui[6])
    return im


def write_battle_previews(prev_dir, art, R):
    B = R['battle']
    BGc = (40, 40, 48)
    day = battle_mock(art, R, ('HEAVY', 7, 'PLAIN'), ('WALKER', 10, 'WOODS'))
    night = battle_mock(art, R, ('BRUTE', 8, 'NEST'), ('SURVIVOR', 6, 'CITY'), night=True)
    boss = battle_mock(art, R, ('SNIPER', 4, 'RUINS'), ('BOSS_ZERO', 9, 'NEST'))
    for nm, im in (('battle_day', day), ('battle_night', night), ('battle_boss', boss)):
        im.save(os.path.join(prev_dir, nm + '_1x.png'))
        up(im).save(os.path.join(prev_dir, nm + '.png'))
    # sheet: every figure x 4 frames on its usual backdrop colour, backdrops, effects
    upal = {'survivor': pal_rgb(art.palette('survivor').colors), 'horde': pal_rgb(art.palette('horde').colors),
            'cursor': pal_rgb(art.palette('cursor').colors)}
    names = UG + ['BOSS_' + n for n in BOSS]
    sets = B['figs'] + B['bosses']
    cell = 36
    sheet = Image.new('RGB', (max(len(names) * cell, len(BB) * (BG_W + 4), len(BFX) * 4 * 34) + 8,
                              4 * cell + 20 + 112 + 2 * cell), BGc)
    for k, (n, frames) in enumerate(zip(names, sets)):
        cols = upal['survivor' if k < UG_HORDE_FIRST else 'horde']
        for f, fr in enumerate(frames):
            im = Image.new('RGB', (32, 32), (92, 148, 224) if f % 2 == 0 else (80, 160, 60))
            blit_obj(im, sprite_tiles_1d(fr, 32, 32), 32, 32, 0, 0, cols)
            if f == 3 and k < len(UG) and B['muzzle'][k][0] >= 0:
                im.putpixel(B['muzzle'][k], (255, 0, 255))
            sheet.paste(im, (8 + k * cell, 16 + f * cell))
        label(sheet, 8 + k * cell, 2, n.replace('BOSS_', '*')[:6])
    y0 = 20 + 4 * cell
    tcol = pal_rgb(art.palette('terrain').colors)
    for k, n in enumerate(BB):
        im = Image.new('RGB', (BG_W, BG_H))
        for ty, row in enumerate(B['bg_maps'][n]):
            for tx, se in enumerate(row):
                blit_tile(im, decode_se(B['bg_ts'].tiles, se), tx * 8, ty * 8, tcol, False)
        sheet.paste(im, (8 + k * (BG_W + 4), y0))
    y0 += 112
    for k, (n, frames) in enumerate(zip(BFX, B['fx'])):
        cols = upal['cursor' if BFX_PAL[n] == 0 else 'horde']
        for f, fr in enumerate(frames):
            im = Image.new('RGB', (32, 32), (24, 24, 32))
            blit_obj(im, sprite_tiles_1d(fr, 32, 32), 32, 32, 0, 0, cols)
            sheet.paste(im, (8 + (k * 4 + f) * 34, y0 + 4))
    sheet.save(os.path.join(prev_dir, 'battle_sheet_1x.png'))
    up(sheet, 2).save(os.path.join(prev_dir, 'battle_sheet.png'))


SCENE = [
    # 15 x 10 map;  legend below
    ".F..FF....~~.FF",
    ".H..B.....~~.F.",
    ".rrrrrrrrr==rrS",
    ".r.W..r...~~.r.",
    ".r..k.r..R~~.r.",
    "Gr....r...~~.r.",
    ".r.F..r..w~~~b~",
    ".r..FF.^^...Lr.",
    ".r..F.^^h..T.rN",
    ".rrrrrrrrrrrrrP",
]
LEGEND = {'.': 'PLAIN', 'F': 'FOREST', '^': 'HILL', '~': 'WATER', '=': 'BRIDGE_H',
          'b': 'BRIDGE_V', 'R': 'RUINS', 'W': 'WALL', 'k': 'WRECK', 'H': 'HOUSE',
          'L': 'HOUSE_LOOTED', 'G': 'WORKSHOP', 'S': 'SAFEHOUSE', 'B': 'HOSPITAL',
          'N': 'NEST', 'T': 'RADIO', 'P': 'HELIPAD', 'r': 'ROAD', 'w': 'WRECK', 'h': 'HILL'}
SCENE_OWNER = {(1, 1): 'blue', (4, 1): 'blue', (14, 2): 'blue', (0, 5): 'blue',
               (11, 8): 'red', (14, 8): 'red'}
SCENE_UNITS = [  # (x, y, unit, hp)
    (2, 3, 'SURVIVOR', 10), (3, 4, 'HEAVY', 7), (5, 1, 'MEDIC', 10), (6, 4, 'SNIPER', 4),
    (1, 6, 'PICKUP', 10), (5, 2, 'BUS', 10), (2, 8, 'ARMOR', 9),
    (8, 4, 'WALKER', 10), (9, 5, 'RUNNER', 6), (12, 4, 'BRUTE', 10), (8, 7, 'SPITTER', 3),
    (12, 8, 'BLOATER', 10), (13, 5, 'WALKER', 2), (10, 8, 'BOSS_ZERO', 8),
]
MOVE_DEMO = [(2, 3), (1, 3), (2, 2), (1, 2), (3, 2), (1, 4), (2, 4), (1, 5), (4, 2), (2, 5), (3, 3)]
ATTACK_DEMO = (8, 4)


def write_scene(prev_dir, art, R):
    tt = R['terrain_ts'].tiles
    meta = R['terrain_meta']
    tpal = art.palette('terrain').colors
    im = Image.new('RGB', (240, 160))
    grid = SCENE
    roadish = lambda x, y: 0 <= y < len(grid) and 0 <= x < 15 and grid[y][x] in 'rb='
    for y, row in enumerate(grid):
        for x, ch in enumerate(row):
            nm = LEGEND[ch]
            if nm == 'ROAD':
                m = (N_BIT if roadish(x, y - 1) else 0) | (S_BIT if roadish(x, y + 1) else 0) | \
                    (W_BIT if roadish(x - 1, y) else 0) | (E_BIT if roadish(x + 1, y) else 0)
                k = len(MT_BASE) + m
            else:
                k = MT_BASE.index(nm)
            team = SCENE_OWNER.get((x, y))
            blit_meta(im, tt, meta[k], x * 16, y * 16, pal_rgb(tpal, team), False)
    # move overlay around the survivor at (2,5), attack overlay on (9,6)
    ovl = im.copy()
    ov = pal_rgb(art.palette('overlay').colors)
    for (x, y) in MOVE_DEMO:
        blit_meta(ovl, R['overlay_ts'].tiles, R['overlay_meta'][1], x * 16, y * 16, ov)
    ax, ay = ATTACK_DEMO
    blit_meta(ovl, R['overlay_ts'].tiles, R['overlay_meta'][2], ax * 16, ay * 16, ov)
    im = Image.blend(im, ovl, 0.45)
    upal = {'survivor': pal_rgb(art.palette('survivor').colors),
            'horde': pal_rgb(art.palette('horde').colors)}
    cur = pal_rgb(art.palette('cursor').colors)
    for x, y, n, hp in SCENE_UNITS:
        if n.startswith('BOSS_'):
            k, f = UG_HORDE_FIRST, R['bosses'][BOSS.index(n[5:])][(x + y) & 1]
        else:
            k = UG.index(n)
            f = R['units'][k][(x + y) & 1]
        cols = upal['survivor' if k < UG_HORDE_FIRST else 'horde']
        if k >= UG_HORDE_FIRST:   # horde faces left on the map
            f = [row[::-1] for row in f]
        blit_obj(im, sprite_tiles_1d(f, 16, 16), 16, 16, x * 16, y * 16, cols)
        if hp < 10:
            blit_tile(im, tuple(v for row in R['digits'][hp] for v in row), x * 16 + 8, y * 16 + 8, cur)
    blit_obj(im, sprite_tiles_1d(R['cursor'][0], 16, 16), 16, 16, ax * 16, ay * 16, cur)
    im.save(os.path.join(prev_dir, 'scene_1x.png'))
    up(im).save(os.path.join(prev_dir, 'scene.png'))


# --------------------------------------------------------------------------

def main(argv):
    if len(argv) != 2:
        sys.stderr.write('usage: python3 -I tools/gfx.py <deadwars_dir>\n')
        return 2
    root = argv[1]
    try:
        art = parse_art(os.path.join(root, 'art'))
        for n in PAL_SINGLE + ['pt_' + p.lower() for p in PT]:
            art.palette(n)
        R = {}
        R['terrain_ts'], R['terrain_meta'], R['terrain_pix'] = build_terrain(art)
        R['overlay_ts'], R['overlay_meta'] = build_overlay()
        R['units'] = build_units(art)
        R['bosses'] = build_units(art, 'BOSS_', BOSS, lambda k: 'horde')
        R['cursor'] = [art.get('cursor0')[3], art.get('cursor1')[3]]
        R['pointer'] = art.get('pointer')[3]
        R['explode'] = [art.get('explode%d' % f)[3] for f in range(4)]
        R['battle'] = build_battle(art, R)
        R['digits'] = build_digits(art)
        R['font'] = build_font(art)
        R['window'] = build_windows(art)
        R['portraits'] = build_portraits(art)
        R['title_ts'], R['title_map'], R['title_pix'] = build_title(art)
        if len(R['title_ts'].tiles) > 512:
            raise ArtError('title has %d unique tiles (max 512)' % len(R['title_ts'].tiles))
        if not all(v == 0 for row in R['font'][0] for v in row):
            raise ArtError('font: space must be blank')
    except ArtError as e:
        sys.stderr.write('gfx.py: error: %s\n' % e)
        return 1
    write_c(os.path.join(root, 'build', 'gen'), art, R)
    write_previews(os.path.join(root, 'build', 'preview'), art, R)
    print('gfx.py: terrain %d tiles, title %d tiles, overlay %d tiles -> build/gen/assets.[ch]'
          % (len(R['terrain_ts'].tiles), len(R['title_ts'].tiles), len(R['overlay_ts'].tiles)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
