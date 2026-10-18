#!/usr/bin/env python3
"""Dead Wars: The Last Boat art generator (contract: src/lbart.h).

usage:  python3 -I tools/lbart.py art/lb.txt <outdir> [--preview DIR]

Writes <outdir>/lbart.c, and review sheets build/preview/lb_*.png (default: <outdir>/../preview;
needs Pillow, skipped without it). Deterministic; the C output needs the standard library only.

Nothing here is drawn by hand. Every sprite comes from a table row in art/lb.txt, the way
godot-office builds its robots (art/gen/robots: one CAST row per character -> shapes -> tone
slots -> outline -> clean-up):

  people   one `cast` row per LBG_* (body, skin, hair, hat, outfit, colours, prop, extras)
           -> parts (pixel rects, ellipses, lines) -> tone slots light/base/shade/dark from a
           fixed light (upper left) -> creases where a part lies over another, dark lines
           around props -> 1 px outline -> clean-up of lone tone pixels.
           Frame 1 is the same model with the upper body bobbed down 1 px.
  roofs    a pitched roof function of the edge distances, cut into the 9-slice, plus one
           emblem per building kind stamped on the NW piece (the sign tile).
  icons    small shape recipes on the cursor palette.
  journey  a night road map: ground noise, fields, woods, the river, the coast, roads drawn
           as straight lines between the stops (run.c moves the caravan marker on them).

SOURCE FORMAT (art/lb.txt)
--------------------------
gfx.py reads every art/*.txt too, so the file only uses syntax gfx.py accepts:
  palette NAME ... end          16 colour lines, as in art/palettes.txt (gfx.py ignores the
                                names it does not know).
  #: KEYWORD ARGS [key=value]   a data line for lbart.py (a comment for gfx.py).
  # anything else               a comment.
Palette characters are resolved through art/palettes.txt (same directory as lb.txt) and the
palettes in lb.txt itself. Keywords (art/lb.txt documents each): ramp body cast | roof sign |
icon | node ground field wood river sea town road gas farm wreck ship. Unknown keys are errors.
"""
import math
import os
import re
import sys

# --------------------------------------------------------------------------
# errors / parsing


class ArtError(Exception):
    pass


class Palette:
    def __init__(self, name):
        self.name = name
        self.colors = []
        self.chars = {}

    def idx(self, ch, where=''):
        if ch in self.chars:
            return self.chars[ch]
        if ch == '.':
            return 0
        raise ArtError('%s: char %r not in palette %s' % (where, ch, self.name))


def parse_palette_block(lines, i, name, where):
    pal = Palette(name)
    while True:
        if i >= len(lines):
            raise ArtError('%s: unterminated palette %s' % (where, name))
        l2 = lines[i].strip()
        i += 1
        if not l2 or l2.startswith('#'):
            continue
        if l2 == 'end':
            break
        t = l2.split()
        if len(t[0]) != 1 or len(t) < 2 or not re.fullmatch(r'[0-9a-fA-F]{6}', t[1]):
            raise ArtError('%s: bad palette line %r' % (where, l2))
        pal.chars.setdefault(t[0], len(pal.colors))
        pal.colors.append(tuple(int(t[1][k:k + 2], 16) for k in (0, 2, 4)))
    if len(pal.colors) > 16:
        raise ArtError('%s: palette %s has %d colours (max 16)' % (where, name, len(pal.colors)))
    while len(pal.colors) < 16:
        pal.colors.append((0, 0, 0))
    return pal, i


def read_art_file(path, want_sprites=()):
    """palettes, '#:' data lines and (only the named) sprite grids of one art file."""
    pals, data, sprites = {}, [], {}
    lines = open(path, encoding='utf-8').read().split('\n')
    base = os.path.basename(path)
    i = 0
    while i < len(lines):
        raw = lines[i].strip()
        where = '%s:%d' % (base, i + 1)
        i += 1
        if raw.startswith('#:'):
            toks = raw[2:].split()
            if toks:
                data.append((toks, where))
            continue
        if not raw or raw.startswith('#'):
            continue
        t = raw.split()
        if t[0] == 'palette':
            pal, i = parse_palette_block(lines, i, t[1], where)
            pals[t[1]] = pal
        elif t[0] == 'sprite' and t[1] in want_sprites:
            h = int(t[2].lower().split('x')[1])
            sprites[t[1]] = (t[3], [lines[i + k].strip() for k in range(h)])
            i += h
    return pals, data, sprites


KEYS = {   # allowed key=value keys per keyword
    'ramp': (), 'body': ('head', 'torso', 'legs', 'hand', 'back_hand'),
    'cast': ('body', 'skin', 'hair', 'hair_color', 'hat', 'badge', 'outfit', 'top', 'legs', 'boots',
             'trim', 'shirt', 'prop', 'acc', 'pack', 'headset', 'mic', 'beard'),
    'roof': ('ridge', 'slope', 'course_line', 'verge_light', 'verge_dark', 'eave_shadow', 'wall',
             'wall_shade', 'glass', 'glass_dark', 'glint', 'course', 'wall_rows'),
    'sign': ('brick', 'mortar', 'cap', 'shadow', 'stripe_a', 'stripe_b', 'board', 'text', 'plate',
             'steel', 'light', 'cross', 'cross_light', 'cross_dark'),
    'icon': ('axis', 'wood', 'step', 'knob', 'plank', 'nail', 'lid', 'side', 'slat', 'latch', 'pole',
             'cloth', 'lash', 'fill', 'edge', 'glint', 'ring'),
    'node': (), 'ground': ('base', 'dark', 'light', 'darkest'),
    'field': ('dir', 'step', 'crop', 'furrow', 'edge'), 'wood': ('seed', 'step', 'gaps', 'fill', 'shade', 'light'),
    'river': ('half', 'deep', 'water', 'ripple', 'bank'), 'sea': ('amp', 'period', 'deep', 'water', 'foam', 'sand'),
    'town': ('seed', 'burning', 'lit', 'pave', 'roofs'), 'road': ('kind', 'bridge'),
    'gas': (), 'farm': (), 'wreck': ('color', 'dir'), 'ship': (),
}


def need(kv, k, where):
    if k not in kv:
        raise ArtError('%s: %s= missing' % (where, k))
    return kv[k]


def num(kv, k, default, where, lo=None, hi=None):
    try:
        v = int(kv.get(k, default))
    except ValueError:
        raise ArtError('%s: %s=%r is not a number' % (where, k, kv.get(k)))
    if (lo is not None and v < lo) or (hi is not None and v > hi):
        raise ArtError('%s: %s=%d out of range %s..%s' % (where, k, v, lo, hi))
    return v


def pair(v, where):
    """'kind:ramp' -> (kind, ramp)"""
    t = v.split(':')
    if len(t) != 2:
        raise ArtError('%s: %r needs KIND:RAMP' % (where, v))
    return t[0], t[1]


def kv_split(toks, where):
    pos, kv = [], {}
    for t in toks:
        if '=' in t:
            k, v = t.split('=', 1)
            if k in kv:
                raise ArtError('%s: duplicate key %s' % (where, k))
            kv[k] = v
        else:
            if kv:
                raise ArtError('%s: positional %r after key=value' % (where, t))
            pos.append(t)
    return pos, kv


def read_enums(header):
    """enum member lists of lbart.h, keyed by prefix (LBG, LBR, LBI)."""
    txt = open(header, encoding='utf-8').read()
    txt = re.sub(r'//[^\n]*', '', txt)
    out = {}
    for body in re.findall(r'enum\s*\{([^}]*)\}', txt):
        names = [n.strip() for n in body.split(',') if n.strip()]
        pre = names[0].split('_')[0]
        out[pre] = names
    return out


class Source:
    def __init__(self, path):
        self.dir = os.path.dirname(os.path.abspath(path))
        pp = os.path.join(self.dir, 'palettes.txt')
        if not os.path.exists(pp):
            raise ArtError('%s not found (palette characters come from it)' % pp)
        self.pals, _, _ = read_art_file(pp)
        mine, self.data, _ = read_art_file(path)
        for n in mine:
            if n in self.pals:
                raise ArtError('lb.txt: palette %s already defined in palettes.txt' % n)
        self.pals.update(mine)
        for n in ('survivor', 'terrain', 'cursor', 'journey'):
            if n not in self.pals:
                raise ArtError('palette %s missing' % n)

        for toks, where in self.data:
            if toks[0] not in KEYS:
                raise ArtError('%s: unknown keyword %r' % (where, toks[0]))
            _, kv = kv_split(toks[1:], where)
            bad = sorted(set(kv) - set(KEYS[toks[0]]))
            if bad:
                raise ArtError('%s: unknown key(s) for %s: %s' % (where, toks[0], ', '.join(bad)))

    def rows(self, kw):
        for toks, where in self.data:
            if toks[0] == kw:
                pos, kv = kv_split(toks[1:], where)
                yield pos, kv, where


# --------------------------------------------------------------------------
# small pixel helpers

def blank(w, h, v=0):
    return [[v] * w for _ in range(h)]


def rect(x0, y0, x1, y1):
    """inclusive pixel rectangle (empty if x1 < x0 or y1 < y0)"""
    return {(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)}


def ellipse(cx, cy, rx, ry):
    """pixels whose centres lie inside the ellipse (centre and radii in pixel units)"""
    out = set()
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            if dx * dx + dy * dy <= 1.0:
                out.add((x, y))
    return out


def line(x0, y0, x1, y1):
    """Bresenham, inclusive"""
    out = set()
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        out.add((x0, y0))
        if x0 == x1 and y0 == y1:
            return out
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def ints(s, n, where):
    try:
        v = [int(t) for t in s.split(',')]
    except ValueError:
        raise ArtError('%s: %r is not %d numbers' % (where, s, n))
    if len(v) != n:
        raise ArtError('%s: %r needs %d numbers' % (where, s, n))
    return v


N4 = ((1, 0), (-1, 0), (0, 1), (0, -1))
N8 = N4 + ((1, 1), (-1, 1), (1, -1), (-1, -1))

# --------------------------------------------------------------------------
# people: CAST row -> parts -> tones -> lines -> outline -> clean-up

LIGHT = (-0.55, -0.62, 0.56)       # from the upper left, toward the viewer
T_LIGHT, T_SHADE = 0.80, 0.30      # dot(normal, light) thresholds for tones 0 / 2


class Part:
    """pixels of one material. z: draw order (higher = nearer). group: parts of another group
    get a dark line where a nearer one lies on them. shade: flat | cyl | round | bottom.
    crease: darkens the part under it. keep: exempt from lines and clean-up.
    post: stamped after the outline pass (thin dark details such as lashes)"""

    def __init__(self, name, ramp, pix, z, group='body', shade='round', crease=False, keep=False, post=False):
        self.name, self.ramp, self.pix, self.z = name, ramp, set(pix), z
        self.group, self.shade, self.crease, self.keep, self.post = group, shade, crease, keep, post


class Ramps:
    """material -> four palette indices (light, base, shade, dark)"""

    def __init__(self, src, palname):
        self.pal = src.pals[palname]
        self.r = {}
        for pos, kv, where in src.rows('ramp'):
            if len(pos) != 6 or pos[1] != palname:
                if len(pos) == 6:
                    continue
                raise ArtError('%s: ramp NAME PALETTE light base shade dark' % where)
            self.r[pos[0]] = tuple(self.pal.idx(c, where) for c in pos[2:])

    def __call__(self, name, where=''):
        if name not in self.r:
            raise ArtError('%s: unknown ramp %r' % (where, name))
        return self.r[name]


def render_parts(parts, w=16, h=16, outline=1):
    """painter's order by z; returns index grid (palette indices, 0 = empty)"""
    owner = [[None] * w for _ in range(h)]
    post = [p for p in parts if p.post]
    parts = [p for p in parts if not p.post]
    for p in sorted(parts, key=lambda q: q.z):
        for x, y in p.pix:
            if 0 <= x < w and 0 <= y < h:
                owner[y][x] = p
    at = lambda x, y: owner[y][x] if 0 <= x < w and 0 <= y < h else None
    out = blank(w, h)
    tone = blank(w, h, -1)
    # tone slots from a fixed light over each part's visible box
    boxes = {}
    for y in range(h):
        for x in range(w):
            p = owner[y][x]
            if p:
                b = boxes.setdefault(id(p), [x, y, x, y])
                b[0], b[1], b[2], b[3] = min(b[0], x), min(b[1], y), max(b[2], x), max(b[3], y)
    lx, ly, lz = LIGHT
    ln = (lx * lx + ly * ly + lz * lz) ** 0.5
    for y in range(h):
        for x in range(w):
            p = owner[y][x]
            if not p:
                continue
            x0, y0, x1, y1 = boxes[id(p)]
            t = 1
            if p.shade == 'bottom':
                t = 2 if y == y1 and x < x1 - 1 else 1
            elif p.shade != 'flat' and x1 - x0 >= 2:
                rx, ry = (x1 - x0 + 1) / 2.0, (y1 - y0 + 1) / 2.0
                u = (x + 0.5 - (x0 + rx)) / rx
                v = (y + 0.5 - (y0 + ry)) / ry if p.shade == 'round' and y1 - y0 >= 2 else 0.0
                n2 = u * u + v * v
                nz = (1.0 - n2) ** 0.5 if n2 < 1.0 else 0.0
                nl = (u * u + v * v + nz * nz) ** 0.5 or 1.0
                b = (u * lx + v * ly + nz * lz) / (nl * ln)
                t = 0 if b > T_LIGHT else 2 if b < T_SHADE else 1
            tone[y][x] = t
    # lines: next to a nearer part of another group -> outline; under a creasing part -> dark
    lines = [[None] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            p = owner[y][x]
            if not p or p.keep:
                continue
            for dx, dy in N4:
                q = at(x + dx, y + dy)
                if not q or q is p or q.z <= p.z:
                    continue
                if q.group != p.group:
                    lines[y][x] = 'K'
                    break
                if q.crease:
                    lines[y][x] = 'D'
    for y in range(h):
        for x in range(w):
            p = owner[y][x]
            if p:
                if lines[y][x] == 'K':
                    out[y][x] = outline
                    tone[y][x] = -1
                elif lines[y][x] == 'D':
                    out[y][x] = p.ramp[3]
                    tone[y][x] = 3
                else:
                    out[y][x] = p.ramp[tone[y][x]]
    # clean-up: a lone light/shade pixel joins its part's base tone
    for y in range(h):
        for x in range(w):
            p = owner[y][x]
            if not p or tone[y][x] not in (0, 2) or p.keep:
                continue
            if not any(0 <= x + dx < w and 0 <= y + dy < h and out[y + dy][x + dx] == out[y][x]
                       for dx, dy in N8):
                out[y][x] = p.ramp[1]
    # silhouette: 1 px outline, 4-connected
    sil = [row[:] for row in out]
    for y in range(h):
        for x in range(w):
            if out[y][x]:
                continue
            if any(0 <= x + dx < w and 0 <= y + dy < h and out[y + dy][x + dx] for dx, dy in N4):
                sil[y][x] = outline
    for p in sorted(post, key=lambda q: q.z):
        for x, y in p.pix:
            if 0 <= x < w and 0 <= y < h:
                sil[y][x] = p.ramp[1]
    return sil


class Body:
    """body type geometry (pixel boxes, inclusive), see `body` rows in lb.txt"""

    def __init__(self, kv, where):
        self.head = ints(need(kv, 'head', where), 4, where)     # x0,y0,x1,y1
        self.torso = ints(need(kv, 'torso', where), 4, where)   # x0,y0,x1,y1
        self.legs = ints(need(kv, 'legs', where), 4, where)     # back x0,x1, front x0,x1 (rows torso.y1+1..13)
        self.hand = ints(need(kv, 'hand', where), 2, where)     # front hand
        self.back_hand = ints(kv.get('back_hand', '%d,%d' % (self.torso[0] - 1, self.torso[3] - 1)), 2, where)
        if self.torso[3] > 12 or self.head[3] > self.torso[1] or self.head[1] < 1:
            raise ArtError('%s: the torso ends by row 12 (legs on 13, boots on 14); the head sits on it, from row 1' % where)


def person_parts(c, body, R, bob, where):
    """one CAST row -> parts (bob: the upper body moved down by this many pixels)"""
    P = []
    hx0, hy0, hx1, hy1 = body.head
    tx0, ty0, tx1, ty1 = body.torso
    hy0, hy1, ty0, ty1 = hy0 + bob, hy1 + bob, ty0 + bob, ty1 + bob
    hand = (body.hand[0], body.hand[1] + bob)
    bhand = (body.back_hand[0], body.back_hand[1] + bob)
    skin = R(need(c, 'skin', where), where)
    top = R(need(c, 'top', where), where)
    legs = R(c.get('legs', c['top']), where)
    boots = R(c.get('boots', 'boot'), where)
    outfit = c.get('outfit', 'jacket')
    if outfit not in ('jacket', 'coat', 'labcoat', 'overalls'):
        raise ArtError('%s: unknown outfit %s' % (where, outfit))
    long_coat = outfit in ('coat', 'labcoat')
    # legs from under the torso to row 13, boots on row 14 (the front boot a toe longer)
    lb0, lb1, lf0, lf1 = body.legs
    P.append(Part('leg_b', legs, rect(lb0, ty1 + 1, lb1, 13), 1, shade='flat'))
    P.append(Part('leg_f', legs, rect(lf0, ty1 + 1, lf1, 13), 2, shade='cyl'))
    P.append(Part('boot_b', boots, rect(lb0, 14, lb1, 14), 1, shade='flat'))
    P.append(Part('boot_f', boots, rect(lf0, 14, lf1 + 1, 14), 2, shade='flat'))
    # torso; a long coat hangs over the thighs
    tpix = rect(tx0, ty0, tx1, ty1) - {(tx0, ty0), (tx1, ty0)}
    hem = ty1
    if long_coat:
        hem = max(ty1, min(body.torso[3] + 1, 13))   # the hem does not bob
        tpix |= rect(tx0, ty1, tx1, hem) - {(tx1, hem)}
    P.append(Part('torso', top, tpix, 3, shade='cyl'))
    if outfit == 'labcoat' and c.get('shirt'):     # open front: the shirt shows
        P.append(Part('shirt', R(c['shirt'], where), rect(tx1, ty0 + 1, tx1, ty1 - 1), 4, shade='flat', keep=True))
    if outfit == 'overalls':                       # bib and straps over the shirt
        bib = rect(tx0, ty0 + 2, tx1, ty1) | {(tx0 + 1, ty0), (tx0 + 1, ty0 + 1), (tx1 - 1, ty0 + 1)}
        P.append(Part('bib', legs, bib, 4, shade='cyl'))
    trim = c.get('trim')
    if trim:
        tk, tc = pair(trim, where)
        tr = R(tc, where)
        if tk == 'stripe':                         # reflective bands: chest and hem
            ys = {ty1 - 1}
            if long_coat:
                ys.add(hem)
            P.append(Part('stripe', tr, {(x, y) for y in ys for x in range(tx0, tx1 + 1)} & tpix, 4, shade='flat', keep=True))
        elif tk == 'belt':
            P.append(Part('belt', tr, {(x, ty1) for x in range(tx0, tx1 + 1)}, 4, shade='flat', keep=True))
        else:
            raise ArtError('%s: unknown trim %s' % (where, tk))
    # back arm behind the torso; front arm over it (its sleeve creases the torso)
    sx, sy = tx0 + 1, ty0 + 1
    P.append(Part('arm_b', top, line(sx, sy, bhand[0] + 1, bhand[1] - 1) | {(bhand[0] + 1, bhand[1])}, 0, shade='flat'))
    fsx, fsy = (tx0 + tx1) // 2 + 1, ty0 + 1
    arm = line(fsx, fsy, hand[0] - 1, hand[1]) | line(fsx - 1, fsy, hand[0] - 2, hand[1])
    P.append(Part('arm_f', top, arm, 6, shade='flat', crease=True))
    P.append(Part('hand_f', skin, {hand}, 9, group='hand', shade='flat', keep=True))
    P.append(Part('hand_b', skin, {bhand}, 0.5, shade='flat', keep=True))
    # head: rounded skull, jaw forward; shade only along the jaw line
    hpix = rect(hx0, hy0, hx1, hy1) - {(hx0, hy0), (hx1, hy0), (hx0, hy1)}
    P.append(Part('head', skin, hpix, 7, shade='bottom'))
    eye = (hx1 - 1, hy0 + 3 if hy1 - hy0 >= 5 else hy0 + 2)
    style = c.get('hair', 'short')
    hair = R(need(c, 'hair_color', where), where) if style != 'bald' else None
    hp = set()
    if style not in ('short', 'long', 'ponytail', 'messy', 'bald'):
        raise ArtError('%s: unknown hair %s' % (where, style))
    if style != 'bald':                            # crown + back of the head
        hp |= rect(hx0, hy0, hx1 - 1, hy0) | rect(hx0, hy0, hx0 + 1, hy0 + 2) | {(hx0 + 2, hy0 + 1), (hx0, hy0 + 3)}
        if style == 'messy':
            hp |= {(hx0 + 1, hy0 - 1), (hx0 + 3, hy0 - 1), (hx1 - 1, hy0 + 1)}
        if style == 'long':                        # down the back to the shoulders
            hp |= rect(hx0 - 1, hy0 + 1, hx0 + 1, hy1 + 2) - {(hx0 - 1, hy0 + 1)}
        if style == 'ponytail':
            hp |= {(hx0 - 1, hy0 + 1), (hx0 - 2, hy0 + 2), (hx0 - 2, hy0 + 3), (hx0 - 2, hy0 + 4), (hx0 - 1, hy0 + 2)}
    if c.get('beard'):                             # jaw and chin, the eye row stays clear
        hp |= rect(hx0 + 2, hy1, hx1, hy1) | {(hx0 + 2, hy1 - 1), (hx1, hy1 - 1)}
    if hair and hp:
        P.append(Part('hair', hair, hp - hpix, 5, shade='round'))
        P.append(Part('hair_top', hair, hp & hpix, 8, shade='round'))
    P.append(Part('eye', R('eye', where), {eye}, 8.5, shade='flat', keep=True))
    # hats (over the hair)
    hat = c.get('hat', 'none')
    if hat != 'none':
        kind, col = pair(hat, where)
        hr = R(col, where)
        if kind == 'helmet':      # firefighter: dome, long back brim, front badge
            dome = rect(hx0 + 1, hy0 - 1, hx1 - 1, hy0 - 1) | rect(hx0, hy0, hx1, hy0)
            brim = rect(hx0 - 2, hy0 + 1, hx1 + 1, hy0 + 1)
            P.append(Part('hat', hr, dome | brim, 10, shade='round'))
            if c.get('badge'):
                P.append(Part('badge', R(c['badge'], where), {(hx1, hy0)}, 10.5, shade='flat', keep=True))
        elif kind == 'cap':       # military cap: flat crown, peak forward
            crown = rect(hx0, hy0 - 1, hx1 - 1, hy0) - {(hx0, hy0 - 1)}
            peak = rect(hx1 - 1, hy0 + 1, hx1 + 1, hy0 + 1)
            P.append(Part('hat', hr, crown | peak, 10, shade='round'))
        elif kind == 'bandana':   # band over the crown, knot tails at the back
            band = rect(hx0, hy0 - 1, hx1 - 1, hy0) - {(hx0, hy0 - 1), (hx1 - 1, hy0 - 1)}
            tails = {(hx0 - 1, hy0 + 1), (hx0 - 2, hy0 + 2), (hx0 - 1, hy0)}
            P.append(Part('hat', hr, band | tails, 10, shade='flat'))
        elif kind == 'hood':      # hood up: crown and back, framing the face
            hood = rect(hx0, hy0 - 1, hx1 - 1, hy0) | rect(hx0 - 1, hy0, hx0 + 1, hy1 + 1) | {(hx1, hy0)}
            hood -= {(hx0, hy0 - 1), (hx0 - 1, hy0)}
            P.append(Part('hat', hr, hood, 10, shade='round'))
        elif kind == 'beanie':
            P.append(Part('hat', hr, rect(hx0, hy0 - 1, hx1 - 1, hy0 + 1) - {(hx0, hy0 - 1)}, 10, shade='round'))
        else:
            raise ArtError('%s: unknown hat %s' % (where, kind))
    if c.get('headset'):                           # band over the crown, ear cup, mic at the mouth
        hs = R(c['headset'], where)
        P.append(Part('headset', hs, rect(hx0 + 2, hy0, hx0 + 2, hy0 + 3), 10.5, shade='flat', keep=True))
        P.append(Part('mic', R(c.get('mic', 'steel'), where), {(hx0 + 3, hy0 + 4)}, 10.6, shade='flat', keep=True))
    # accessory on the back / hip
    acc = c.get('acc', 'none')
    if acc == 'medkit':
        cx, cy = tx0 - 1, ty1 - 1                  # a white box on the hip, red cross
        mk = rect(cx - 2, cy - 1, cx + 2, cy + 1)
        P.append(Part('medkit', R('white', where), mk, 4.5, group='bag', shade='flat'))
        P.append(Part('cross', R('red', where), {(cx - 1, cy), (cx, cy), (cx + 1, cy), (cx, cy - 1), (cx, cy + 1)},
                      4.6, group='bag', shade='flat', keep=True))
    elif acc == 'backpack':
        P.append(Part('pack', R(c.get('pack', 'brown'), where), rect(tx0 - 2, ty0 + 1, tx0, ty1 - 1), 2.5, group='bag', shade='cyl'))
    elif acc != 'none':
        raise ArtError('%s: unknown acc %s' % (where, acc))
    P += prop_parts(c.get('prop', 'none'), hand, R, where)
    for p in P:
        bad = [(x, y) for x, y in p.pix if not (1 <= x <= 14 and 1 <= y <= 14)]
        if bad:
            raise ArtError('%s: %s leaves the 14x14 interior at %s (no room for the outline)' % (where, p.name, bad[0]))
    return P


def prop_parts(kind, hand, R, where):
    hx, hy = hand
    steel, gun, wood = R('steel', where), R('gun', where), R('wood', where)
    prop = lambda name, ramp, pix, z=8.8, keep=False: Part(name, ramp, pix, z, group='prop', shade='flat', keep=keep)
    if kind == 'none':
        return []
    if kind == 'rifle':       # stock under the arm, long barrel to the frame edge
        return [prop('stock', wood, line(hx - 3, hy + 1, hx - 1, hy)),
                prop('barrel', gun, line(hx + 1, hy, 14, hy) | {(hx + 1, hy + 1), (hx + 2, hy + 1)})]
    if kind == 'pistol':      # short, held out
        return [prop('pistol', gun, line(hx + 1, hy - 1, hx + 3, hy - 1) | {(hx + 1, hy)})]
    if kind == 'crossbow':    # stock forward, bow limbs across, bolt tip
        return [prop('stock', wood, line(hx - 2, hy, hx + 3, hy)),
                prop('bow', steel, line(hx + 3, hy - 3, hx + 3, hy + 3) | {(hx + 2, hy - 3), (hx + 2, hy + 3)}, 8.9),
                prop('bolt', R('white', where), {(hx + 4, hy)}, 8.9, True)]
    if kind == 'machete':     # raised blade, wide at the tip
        return [prop('grip', wood, {(hx, hy - 1)}),
                prop('blade', steel, line(hx + 1, hy - 2, hx + 3, hy - 4) | line(hx + 2, hy - 2, hx + 4, hy - 4) | {(hx + 4, hy - 5)})]
    if kind == 'bat':         # over the shoulder, thick end up
        return [prop('bat', wood, line(hx, hy - 1, hx + 2, hy - 4) | line(hx + 2, hy - 4, hx + 3, hy - 7) | {(hx + 3, hy - 5), (hx + 4, hy - 6), (hx + 4, hy - 7)})]
    if kind == 'knife':       # short blade, point up
        return [prop('knife', steel, line(hx + 1, hy - 1, hx + 2, hy - 2), keep=True)]
    raise ArtError('%s: unknown prop %s' % (where, kind))


def build_people(src, enums):
    R = Ramps(src, 'survivor')
    K = R.pal.idx('K')
    bodies = {pos[0]: Body(kv, where) for pos, kv, where in src.rows('body')}
    rows = {}
    for pos, kv, where in src.rows('cast'):
        if len(pos) != 1:
            raise ArtError('%s: cast LBG_NAME key=value ...' % where)
        if pos[0] in rows:
            raise ArtError('%s: duplicate cast %s' % (where, pos[0]))
        rows[pos[0]] = (kv, where)
    frames, names = [], enums['LBG'][:-1]
    for n in names:
        if n not in rows:
            raise ArtError('lb.txt: no cast row for %s' % n)
        kv, where = rows[n]
        if kv.get('body') not in bodies:
            raise ArtError('%s: unknown body %r' % (where, kv.get('body')))
        b = bodies[kv['body']]
        f = [render_parts(person_parts(kv, b, R, bob, where), outline=K) for bob in (0, 1)]
        for k, pix in enumerate(f):
            if not any(pix[15]):
                raise ArtError('%s.f%d: feet must stand on the bottom row' % (n, k))
        frames.append(f)
    extra = set(rows) - set(names)
    if extra:
        raise ArtError('lb.txt: cast rows not in lbart.h: %s' % ', '.join(sorted(extra)))
    return frames


# --------------------------------------------------------------------------
# roofs: one roof function of the distances to the footprint's edges, cut into a 9-slice

SLICE = {'LBR_NW': ('L', 'T'), 'LBR_N': ('M', 'T'), 'LBR_NE': ('R', 'T'),
         'LBR_W': ('L', 'M'), 'LBR_C': ('M', 'M'), 'LBR_E': ('R', 'M'),
         'LBR_SW': ('L', 'B'), 'LBR_S': ('M', 'B'), 'LBR_SE': ('R', 'B')}


class RoofStyle:
    def __init__(self, src):
        rows = list(src.rows('roof'))
        if len(rows) != 1:
            raise ArtError('lb.txt: exactly one roof row expected')
        _, kv, where = rows[0]
        P = src.pals['terrain']
        col = lambda k: P.idx(need(kv, k, where), where)
        self.K = P.idx('K')
        for k in ('ridge', 'slope', 'course_line', 'verge_light', 'verge_dark', 'eave_shadow',
                  'wall', 'wall_shade', 'glass', 'glass_dark', 'glint'):
            setattr(self, k, col(k))
        self.course = num(kv, 'course', 4, where, 2, 8)        # shingle course height (divides 16)
        self.wall_rows = num(kv, 'wall_rows', 9, where, 6, 12)  # S row: eave line + wall above the bottom outline
        if 16 % self.course:
            raise ArtError('%s: course must divide 16' % where)


def roof_piece(st, cc, rc):
    """16x16 terrain indices for one 9-slice piece (cc: L/M/R column class, rc: T/M/B row class),
    a 3/4 front view like MT_HOUSE: the ridge along the top edge, the front slope in shingle
    courses, the eave, then the front wall with one window per tile. Every pixel depends on
    the distance to an outer edge or on (x mod 8, y mod course), so the pieces join seamlessly
    in any footprint (checked against direct renders in build_roofs)."""
    out = blank(16, 16)
    c = st.course
    eave = 15 - st.wall_rows              # B piece: the roof's lower edge line
    for y in range(16):
        for x in range(16):
            wall = rc == 'B' and y > eave
            if rc == 'T' and y == 0:
                v = st.K
            elif rc == 'T' and y == 1:
                v = st.ridge
            elif rc == 'B' and y == eave or rc == 'B' and y == 15:
                v = st.K
            elif wall:
                wy = y - eave - 1          # 0 = right under the eave
                v = st.eave_shadow if wy == 0 else st.wall_shade if y == 14 else st.wall
                # window: frame 6x5 centred, glass with a glint, dark lower half
                if 5 <= x <= 10 and 2 <= wy <= 6 and wy <= st.wall_rows - 3:
                    if x in (5, 10) or wy in (2, 6):
                        v = st.K
                    else:
                        v = st.glint if (x, wy) == (6, 3) else st.glass if wy <= 4 else st.glass_dark
            else:                          # slope: courses with staggered joints
                v = st.slope
                if y % c == c - 1:
                    v = st.course_line
                elif y % c == 1 and (x + (4 if (y // c) & 1 else 0)) % 8 == 2:
                    v = st.course_line
                if rc == 'B' and y == eave - 1:
                    v = st.course_line
            # gable ends: outline, a lit west verge and a shaded east verge
            if cc == 'L' and x <= 1 and not (rc == 'T' and y == 0):
                v = st.K if x == 0 else (st.verge_light if not wall and v != st.K else v)
            if cc == 'R' and x >= 14 and not (rc == 'T' and y == 0):
                v = st.K if x == 15 else (st.verge_dark if not wall and v != st.K else v)
            out[y][x] = v
    return out


def emblem(kind, kv, P, where):
    """sign emblem as {(x, y): index} on the NW piece (inside its outline)"""
    K = P.idx('K')
    col = lambda k, d: P.idx(kv.get(k, d), where)
    E = {}

    def box(x0, y0, x1, y1, fill, edge=K):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                E[(x, y)] = edge if x in (x0, x1) or y in (y0, y1) else fill

    if kind == 'chimney':            # brick stack on the slope with a concrete cap
        brick, mortar, cap = col('brick', 'r'), col('mortar', 'd'), col('cap', 'c')
        box(7, 4, 12, 11, brick)
        for y in range(5, 11):
            for x in range(8, 12):
                if y % 3 == 1 or (x == (9 if (y // 3) & 1 else 10) and y % 3 != 1):
                    E[(x, y)] = mortar
        box(6, 2, 13, 4, cap)
        E[(13, 10)] = E[(13, 11)] = E[(13, 9)] = col('shadow', 'Z')
        return E
    if kind == 'awning':             # shop: striped awning over a sign board
        a, b, board = col('stripe_a', 'r'), col('stripe_b', 'w'), col('board', 'a')
        box(3, 3, 13, 8, board)
        for x in range(4, 13):
            E[(x, 4)] = col('text', 'w') if x % 2 == 0 and 5 <= x <= 11 else board
        for y in range(9, 12):
            for x in range(3, 14):
                if y == 11 and x % 2 == 0:
                    continue
                E[(x, y)] = a if (x // 2) % 2 == 0 else b
        for x in range(3, 14):
            if (x, 12) not in E and x % 2 == 1:
                E[(x, 12)] = K
        return E
    if kind == 'wrench':             # garage: open-ended wrench on a dark plate
        plate, steel, light = col('plate', 'a'), col('steel', 'c'), col('light', 'w')
        box(3, 3, 13, 13, plate)
        for x, y in line(5, 11, 9, 7):    # handle, 2 px thick
            E[(x, y)] = steel
            E[(x + 1, y)] = light if (x, y) == (5, 11) else steel
        for x, y in ((9, 5), (8, 5), (10, 6), (11, 6), (11, 7), (11, 8), (10, 8), (9, 7), (10, 7)):
            E[(x, y)] = steel              # jaw: a C open to the upper right
        E[(10, 5)] = E[(12, 5)] = plate
        E[(9, 6)] = steel
        E[(10, 6)] = plate
        E[(8, 5)] = light
        return E
    if kind == 'cross':              # pharmacy: green cross on a white plate
        plate, cr, crl, crd = col('plate', 'w'), col('cross', 'G'), col('cross_light', 'l'), col('cross_dark', 'g')
        box(3, 3, 13, 13, plate)
        for y in range(5, 12):
            for x in range(5, 12):
                if 7 <= x <= 9 or 7 <= y <= 9:
                    E[(x, y)] = cr
        for x, y in ((7, 5), (8, 5), (9, 5), (5, 7), (6, 7), (10, 7), (11, 7)):
            E[(x, y)] = crl
        for x, y in ((7, 11), (8, 11), (9, 11), (5, 9), (6, 9), (10, 9), (11, 9)):
            E[(x, y)] = crd
        return E
    raise ArtError('%s: unknown emblem %s' % (where, kind))


def build_roofs(src, enums):
    st = RoofStyle(src)
    P = src.pals['terrain']
    names = enums['LBR'][:-1]
    metas = {}
    for n, (cc, rc) in SLICE.items():
        metas[n] = roof_piece(st, cc, rc)
    signs = {}
    for pos, kv, where in src.rows('sign'):
        if len(pos) != 2:
            raise ArtError('%s: sign LBR_SIGN_KIND EMBLEM key=value ...' % where)
        if pos[0] in signs:
            raise ArtError('%s: duplicate sign %s' % (where, pos[0]))
        pix = [row[:] for row in metas['LBR_NW']]
        for (x, y), v in emblem(pos[1], kv, P, where).items():
            if not (1 <= x <= 14 and 1 <= y <= 14):
                raise ArtError('%s: emblem leaves the tile interior' % where)
            pix[y][x] = v
        signs[pos[0]] = pix
    metas.update(signs)
    for n in names:
        if n not in metas:
            raise ArtError('lb.txt: no art for %s' % n)
    extra = set(metas) - set(names)
    if extra:
        raise ArtError('lb.txt: signs not in lbart.h: %s' % ', '.join(sorted(extra)))
    ts = TileSet()
    meta = []
    for n in names:
        pix = metas[n]
        for row in pix:
            if 0 in row:
                raise ArtError('%s uses index 0 (would show through to the backdrop)' % n)
        meta.append([ts.add(tile_at(pix, x, y)) for (x, y) in ((0, 0), (8, 0), (0, 8), (8, 8))])
    if len(ts.tiles) > 288:
        raise ArtError('roofs need %d BG tiles (max 288)' % len(ts.tiles))
    out = [metas[n] for n in names]
    # every footprint rebuilt from the deduplicated tiles and flips must match its pieces
    for sign in [n for n in names if n.startswith('LBR_SIGN_')]:
        for w in (2, 3, 4):
            for h in (2, 3):
                pix = compose_building(out, names, w, h, sign)
                for ty in range(h):
                    for tx in range(w):
                        k = names.index(sign) if tx == ty == 0 else \
                            (0 if ty == 0 else 6 if ty == h - 1 else 3) + (0 if tx == 0 else 2 if tx == w - 1 else 1)
                        for q in range(4):
                            t = decode_se(ts.tiles, meta[k][q])
                            for yy in range(8):
                                row = pix[ty * 16 + (q >> 1) * 8 + yy]
                                if list(t[yy * 8:yy * 8 + 8]) != row[tx * 16 + (q & 1) * 8:tx * 16 + (q & 1) * 8 + 8]:
                                    raise ArtError('roof tiles do not rebuild %s %dx%d' % (sign, w, h))
    return ts, meta, out


def compose_building(metas, names, w, h, sign=None):
    """footprint w x h (tiles) from the 9-slice, the way tac_gfx.c picks pieces"""
    idx = {n: k for k, n in enumerate(names)}
    pix = blank(w * 16, h * 16)
    for ty in range(h):
        for tx in range(w):
            if tx == 0 and ty == 0 and sign:
                k = idx[sign]
            else:   # LBR_NW + row * 3 + column, as tac_redraw_tile() picks it
                k = (0 if ty == 0 else 6 if ty == h - 1 else 3) + (0 if tx == 0 else 2 if tx == w - 1 else 1)
            for y in range(16):
                pix[ty * 16 + y][tx * 16:tx * 16 + 16] = metas[k][y]
    return pix


# --------------------------------------------------------------------------
# icons: shape recipes on the cursor palette, outlined like the people

def transpose(pix):
    return [[pix[x][y] for x in range(16)] for y in range(16)]


def icon_parts(kind, kv, R, where):
    """parts of one icon (S/E variants share the S recipe; E is its transpose)"""
    ramp = lambda k, d: R(kv.get(k, d), where)
    flat = lambda name, r, pix, z, group='icon', keep=False: Part(name, r, pix, z, group=group, shade='flat', keep=keep)
    if kind in ('door', 'barricade'):
        # the wall between the door tile and the outside runs between rows 7 and 8: the door
        # stands on it (rows 0..7, its base on the building's bottom outline), the step outside
        wood, step = ramp('wood', 'c_wood'), ramp('step', 'c_stone')
        P = [Part('door', wood, rect(5, 1, 10, 6), 1, group='icon', shade='cyl'),
             flat('knob', ramp('knob', 'c_white'), {(9, 4)}, 2, keep=True),
             flat('step', step, rect(3, 9, 12, 9), 1, group='step')]
        if kind == 'barricade':   # two planks nailed across, wider than the frame
            plank, nail = ramp('plank', 'c_plank'), ramp('nail', 'c_smoke')
            p1 = rect(2, 1, 13, 2)
            p2 = line(2, 6, 13, 4) | line(2, 5, 13, 3)
            P += [Part('plank1', plank, p1, 3, group='plank1', shade='flat'),
                  Part('plank2', plank, p2, 4, group='plank2', shade='flat'),
                  flat('nail1', nail, {(3, 2), (12, 2)}, 3.5, group='plank1', keep=True),
                  flat('nail2', nail, {(3, 5), (12, 4)}, 5, group='plank2', keep=True)]
        return P
    if kind == 'crate':           # supply crate in 3/4 view: lid, slats, a corner strap
        lid, side = ramp('lid', 'c_lid'), ramp('side', 'c_wood')
        return [flat('lid', lid, rect(4, 5, 11, 6), 1),
                Part('side', side, rect(4, 7, 11, 11), 1, group='side', shade='cyl'),
                flat('slat', ramp('slat', 'c_dark'), {(x, 9) for x in range(5, 11)}, 2, group='side'),
                flat('latch', ramp('latch', 'c_white'), {(7, 7), (8, 7)}, 2, group='side', keep=True)]
    if kind == 'flag':            # exit: a flag on a pole (direction-free: exits are on any edge)
        pole, cloth = ramp('pole', 'c_pole'), ramp('cloth', 'c_flag')
        fl = rect(6, 2, 11, 6) - {(11, 4)} | {(10, 4)}
        return [flat('pole', pole, rect(5, 1, 5, 13), 1),
                Part('cloth', cloth, fl, 2, group='cloth', shade='round'),
                flat('base', pole, rect(4, 13, 6, 13), 1)]
    if kind == 'eye_closed':      # hidden: a closed eye (lid arc, lashes), top-right corner
        lid = ramp('lid', 'c_white')
        arc = {(9, 1), (9, 2), (10, 2), (10, 3), (11, 3), (12, 3), (13, 2), (13, 3), (14, 1), (14, 2)}
        lash = {(9, 4), (11, 5), (13, 5), (15, 4)}
        return [flat('lid', lid, arc, 1), Part('lash', ramp('lash', 'c_dark'), lash, 2, shade='flat', post=True)]
    if kind == 'drop':            # infected: a drop of fever-yellow, top-right corner
        fill, edge = ramp('fill', 'c_hazard'), ramp('edge', 'c_amber')
        drop = {(12, 1), (12, 2), (11, 3), (12, 3), (13, 3)} | rect(10, 4, 14, 5) | rect(11, 6, 13, 6)
        rim = {(13, 3), (14, 4), (14, 5), (13, 6), (12, 6)}
        return [flat('drop', fill, drop - rim, 1), flat('rim', edge, rim, 1),
                flat('glint', ramp('glint', 'c_white'), {(11, 4)}, 2, keep=True)]
    if kind == 'cross':           # downed: red cross, top-right corner
        red = ramp('fill', 'c_red')
        cr = rect(11, 1, 12, 6) | rect(9, 3, 14, 4)
        return [Part('cross', red, cr, 1, group='icon', shade='round')]
    if kind == 'ring':            # noise: a ring round the tile centre
        ring = ramp('ring', 'c_white')
        outer, inner = ellipse(8.0, 8.0, 6.6, 6.6), ellipse(8.0, 8.0, 5.2, 5.2)
        return [flat('ring', ring, outer - inner, 1)]
    raise ArtError('%s: unknown icon %s' % (where, kind))


def build_icons(src, enums):
    R = Ramps(src, 'cursor')
    K = R.pal.idx('K')
    names = enums['LBI'][:-1]
    rows = {}
    for pos, kv, where in src.rows('icon'):
        if len(pos) != 2:
            raise ArtError('%s: icon LBI_NAME KIND key=value ...' % where)
        if pos[0] in rows:
            raise ArtError('%s: duplicate icon %s' % (where, pos[0]))
        rows[pos[0]] = (pos[1], kv, where)
    out = []
    for n in names:
        if n not in rows:
            raise ArtError('lb.txt: no icon row for %s' % n)
        kind, kv, where = rows[n]
        parts = icon_parts(kind, kv, R, where)
        for p in parts:
            if any(not (0 <= x <= 15 and 0 <= y <= 15) for x, y in p.pix):
                raise ArtError('%s: %s leaves the 16x16 frame' % (where, p.name))
        pix = render_parts(parts, outline=K)
        if kv.get('axis') == 'e':
            pix = transpose(pix)
        used = {v for row in pix for v in row}
        if max(used) > 7:
            raise ArtError('%s: %s uses cursor colour %d (only 1..7 are defined)' % (where, n, max(used)))
        out.append(pix)
    extra = set(rows) - set(names)
    if extra:
        raise ArtError('lb.txt: icon rows not in lbart.h: %s' % ', '.join(sorted(extra)))
    return out


# --------------------------------------------------------------------------
# journey map: a night road map of the slice, 240x160 on its own palette

JW, JH = 240, 160


class Rng:
    """small deterministic generator (xorshift32)"""

    def __init__(self, seed):
        self.s = (seed * 2654435761 + 0x9E3779B9) & 0xFFFFFFFF or 1

    def next(self):
        s = self.s
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        self.s = s
        return s

    def int(self, a, b):
        """a..b inclusive"""
        return a + self.next() % (b - a + 1)


def hash2(x, y, k=0):
    h = (x * 374761393 + y * 668265263 + k * 1442695041) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return h ^ (h >> 16)


def seg_dist(px, py, ax, ay, bx, by):
    """distance from (px, py) to segment a-b and the position t (pixels from a) along it"""
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L2))
    qx, qy = ax + t * dx, ay + t * dy
    return ((px - qx) ** 2 + (py - qy) ** 2) ** 0.5, t * L2 ** 0.5


def poly_dist(px, py, pts):
    return min(seg_dist(px, py, *pts[k], *pts[k + 1])[0] for k in range(len(pts) - 1))


def xy(s, where):
    v = ints(s, 2, where)
    return v[0], v[1]


def box4(s, where):
    return ints(s, 4, where)


class Journey:
    def __init__(self, src):
        self.src = src
        self.J = src.pals['journey']
        self.c = lambda ch, where='journey palette': self.J.idx(ch, where)
        self.pix = blank(JW, JH)
        self.water = [[False] * JW for _ in range(JH)]
        self.road = [[False] * JW for _ in range(JH)]
        self.nodes = {}
        for pos, kv, where in src.rows('node'):
            if len(pos) != 3:
                raise ArtError('%s: node NAME x y (8x8 tile of the marker)' % where)
            x, y = ints(pos[1] + ',' + pos[2], 2, where)
            if not (0 <= x < 30 and 0 <= y < 20):
                raise ArtError('%s: node off screen' % where)
            if pos[0] in self.nodes:
                raise ArtError('%s: duplicate node %s' % (where, pos[0]))
            self.nodes[pos[0]] = (x, y)

    def put(self, x, y, v):
        if 0 <= x < JW and 0 <= y < JH:
            self.pix[y][x] = v

    def clear(self, x0, y0, x1, y1, where):
        """a site's box (inclusive) must not cover a road or water"""
        for y in range(max(y0, 0), min(y1, JH - 1) + 1):
            for x in range(max(x0, 0), min(x1, JW - 1) + 1):
                if self.road[y][x] or self.water[y][x]:
                    raise ArtError('%s: site box %d,%d..%d,%d covers a road or water at %d,%d' % (where, x0, y0, x1, y1, x, y))

    def point(self, s, where):
        """NODE name -> centre of its 8x8 marker tile; @x,y -> pixel"""
        if s.startswith('@'):
            return xy(s[1:], where)
        if s not in self.nodes:
            raise ArtError('%s: unknown node %s' % (where, s))
        x, y = self.nodes[s]
        return x * 8 + 4, y * 8 + 4

    # ---- layers
    def ground(self, kv, where):
        base, dark, light, darkest = (self.c(need(kv, k, where), where) for k in ('base', 'dark', 'light', 'darkest'))
        for y in range(JH):
            for x in range(JW):
                h = hash2(x & 31, y & 31, 1)   # period 32: plain ground dedupes into 16 tiles
                v = base
                if h % 11 == 0:
                    v = dark
                elif h % 37 == 1:
                    v = light
                elif h % 53 == 2:
                    v = darkest
                self.pix[y][x] = v

    def field(self, pos, kv, where):
        x0, y0, x1, y1 = box4(pos[0], where)
        a, b, edge = self.c(kv.get('crop', 'd'), where), self.c(kv.get('furrow', 'c'), where), self.c(kv.get('edge', 'b'), where)
        step = num(kv, 'step', 3, where, 2, 16)
        d = kv.get('dir', 'h')
        if d not in ('h', 'v', 'd'):
            raise ArtError('%s: dir=h|v|d' % where)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if x in (x0, x1) or y in (y0, y1):
                    v = edge
                else:
                    k = (y - y0) if d == 'h' else (x - x0) if d == 'v' else (x + y)
                    v = b if k % step == 0 else a
                self.put(x, y, v)

    def wood(self, pos, kv, where):
        x0, y0, x1, y1 = box4(pos[0], where)
        rng = Rng(num(kv, 'seed', 1, where))
        fill, shade, hi = self.c(kv.get('fill', 'b'), where), self.c(kv.get('shade', 'a'), where), self.c(kv.get('light', 'd'), where)
        step = num(kv, 'step', 6, where, 3, 32)
        gaps = num(kv, 'gaps', 15, where, 0, 100)
        trees = []
        for gy in range(y0, y1 + 1, step):
            for gx in range(x0, x1 + 1, step):
                if rng.int(0, 99) < gaps:
                    continue
                trees.append((gx + rng.int(-2, 2) + (step // 2 if (gy // step) & 1 else 0), gy + rng.int(-1, 1), rng.int(2, 3)))
        for tx, ty, r in sorted(trees, key=lambda t: t[1]):
            for (x, y) in ellipse(tx + 0.5, ty + 0.5, r + 0.4, r):
                dx, dy = x - tx, y - ty
                v = shade if dx + dy >= r - 1 else fill
                if (dx, dy) == (-1, -1):
                    v = hi
                self.put(x, y, v)
            for (x, y) in ellipse(tx + 0.5, ty + 0.5, r + 1.4, r + 1.0) - ellipse(tx + 0.5, ty + 0.5, r + 0.4, r):
                if 0 <= x < JW and 0 <= y < JH and self.pix[y][x] not in (fill, shade, hi):
                    self.pix[y][x] = shade if y >= ty else self.pix[y][x]

    def river(self, pos, kv, where):
        pts = [xy(p, where) for p in pos]
        if len(pts) < 2:
            raise ArtError('%s: river needs 2+ points' % where)
        hw = num(kv, 'half', 8, where, 2, 30)
        deep, mid, light, bank = (self.c(need(kv, k, where), where) for k in ('deep', 'water', 'ripple', 'bank'))
        for y in range(JH):
            for x in range(JW):
                d = poly_dist(x + 0.5, y + 0.5, pts)
                if d <= hw:
                    v = deep if d < hw - 3 else mid
                    if hash2(x // 3, y, 7) % 9 == 0 and d < hw - 1:
                        v = light
                    self.pix[y][x] = v
                    self.water[y][x] = True
                elif d <= hw + 1.2:
                    self.pix[y][x] = bank

    def sea(self, pos, kv, where):
        if len(pos) != 1:
            raise ArtError('%s: sea SHORE_X key=value ...' % where)
        base = ints(pos[0], 1, where)[0]
        amp, per = num(kv, 'amp', 3, where, 0, 20), num(kv, 'period', 14, where, 2, 200)
        deep, mid, foam, sand = (self.c(need(kv, k, where), where) for k in ('deep', 'water', 'foam', 'sand'))
        for y in range(JH):
            sx = base + amp * math.sin(y / per) + 1.5 * math.sin(y / 5.0 + 1.0)
            for x in range(JW):
                d = x - sx
                if d >= 0:
                    v = deep if d > 6 else mid
                    if d < 1.2 or (d > 3 and hash2(x // 4, y // 2, 9) % 13 == 0):
                        v = foam if d < 1.2 else mid
                    self.pix[y][x] = v
                    self.water[y][x] = True
                elif d > -2.5:
                    self.pix[y][x] = sand

    def roads(self, rows):
        """all verges, then all asphalt, then the paint, so crossings stay clean"""
        segs = []
        for pos, kv, where in rows:
            if len(pos) != 2:
                raise ArtError('%s: road FROM TO [kind=road|highway] [bridge=1]' % where)
            a, b = self.point(pos[0], where), self.point(pos[1], where)
            if kv.get('kind', 'road') not in ('road', 'highway'):
                raise ArtError('%s: kind=road|highway' % where)
            hw = 2.6 if kv.get('kind') == 'highway' else 1.6
            segs.append((a, b, hw, kv, where))
        asphalt, verge, paint = self.c('g'), self.c('e'), self.c('l')
        deck, rail = self.c('f'), self.c('k')
        for layer in range(3):
            for a, b, hw, kv, where in segs:
                m = hw + 3
                L = ((b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2) ** 0.5
                for y in range(max(0, int(min(a[1], b[1]) - m)), min(JH, int(max(a[1], b[1]) + m) + 1)):
                    for x in range(max(0, int(min(a[0], b[0]) - m)), min(JW, int(max(a[0], b[0]) + m) + 1)):
                        d, t = seg_dist(x + 0.5, y + 0.5, a[0] + 0.5, a[1] + 0.5, b[0] + 0.5, b[1] + 0.5)
                        wet = self.water[y][x]
                        if wet and d <= hw + 1.0 and kv.get('bridge') != '1':
                            raise ArtError('%s: road crosses water (mark it bridge=1)' % where)
                        if layer == 0 and hw < d <= hw + 1.0:
                            self.pix[y][x] = rail if wet else verge
                        elif layer == 1 and d <= hw:
                            self.pix[y][x] = deck if wet else asphalt
                            self.road[y][x] = True
                        elif layer == 2 and d <= 0.55 and not wet and 6 <= t <= L - 6 and int(t) % 8 < 3:
                            self.pix[y][x] = paint      # faded centre line, clear of junctions
                        elif layer == 2 and wet and d <= hw and int(t) % 3 == 0:
                            self.pix[y][x] = verge      # deck planks

    # ---- sites
    def building(self, x0, y0, w, h, roof, ridge, lit=None, fire=None):
        k = self.c('k')
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                edge = x in (x0, x0 + w - 1) or y in (y0, y0 + h - 1)
                self.put(x, y, k if edge else (ridge if y == y0 + h // 2 else roof))
        if lit:
            self.put(lit[0], lit[1], self.c('l'))
        if fire:                                    # flame clusters, smoke drifting up-right
            r, o, smoke = self.c('r'), self.c('o'), self.c('e')
            for (x, y) in sorted(fire):
                for dx, dy in N4:
                    if x0 < x + dx < x0 + w - 1 and y0 < y + dy < y0 + h - 1:
                        self.put(x + dx, y + dy, r)
                self.put(x, y, o)
                for k in range(1, 6):
                    if k & 1:
                        self.put(x + k // 2 + 1, y0 - k, smoke)

    def town(self, pos, kv, where):
        x0, y0, x1, y1 = box4(pos[0], where)
        rng = Rng(num(kv, 'seed', 1, where))
        pave = self.c(kv.get('pave', 'b'), where)
        roofs = [self.c(ch, where) for ch in kv.get('roofs', 'ef')]
        burning = num(kv, 'burning', 0, where, 0, 100)
        lit = num(kv, 'lit', 40, where, 0, 100)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if not self.water[y][x]:
                    self.put(x, y, pave)
        y = y0 + 1
        while y + 3 <= y1:
            h = rng.int(4, 6)
            if y + h - 1 > y1:
                h = y1 - y + 1
            x = x0 + rng.int(0, 2)
            while x + 3 <= x1:
                w = rng.int(4, 8)
                if x + w - 1 > x1:
                    w = x1 - x + 1
                if w >= 4 and h >= 4 and rng.int(0, 99) < 85:
                    roof = roofs[rng.int(0, len(roofs) - 1)]
                    win = (x + rng.int(1, w - 2), y + rng.int(1, h - 2)) if rng.int(0, 99) < lit else None
                    fire = None
                    if rng.int(0, 99) < burning:
                        fire = {(x + rng.int(1, w - 2), y + rng.int(1, h - 2)) for _ in range(3)}
                        roof = self.c('a')
                    self.building(x, y, w, h, roof, self.c('e') if roof != self.c('e') else self.c('f'), win, fire)
                x += w + rng.int(1, 3)
            y += h + 2

    def gas(self, pos, kv, where):
        """canopy lit underneath, pumps, the shop behind, a glowing pylon sign"""
        x, y = xy(pos[0], where)
        self.clear(x - 5, y - 9, x + 13, y + 15, where)
        k, f, l, o, r, e = (self.c(ch) for ch in 'kflore')
        for yy in range(y, y + 6):
            for xx in range(x, x + 13):
                edge = xx in (x, x + 12) or yy in (y, y + 5)
                self.put(xx, yy, k if edge else (l if yy == y + 2 and xx % 3 == 0 else f))
        for px in (x + 3, x + 6, x + 9):
            self.put(px, y + 7, r)
            self.put(px, y + 8, k)
        self.building(x + 2, y + 10, 9, 6, e, f, (x + 4, y + 12))
        for yy in range(y - 6, y + 1):
            self.put(x - 3, yy, e)
        for yy in range(y - 9, y - 5):
            for xx in range(x - 5, x):
                self.put(xx, yy, k if yy in (y - 9, y - 6) or xx in (x - 5, x - 1) else o)

    def farm(self, pos, kv, where):
        x, y = xy(pos[0], where)
        self.clear(x, y - 1, x + 18, y + 14, where)
        k, r, e, f, a = (self.c(ch) for ch in 'krefa')
        for yy in range(y, y + 8):                  # barn: red roof, dark ridge
            for xx in range(x, x + 11):
                edge = xx in (x, x + 10) or yy in (y, y + 7)
                self.put(xx, yy, k if edge else (a if yy == y + 3 else r))
        sx, sy = x + 14, y + 3                      # silo
        for (xx, yy) in ellipse(sx + 0.5, sy + 0.5, 3.2, 3.2):
            self.put(xx, yy, f)
        for (xx, yy) in ellipse(sx + 0.5, sy + 0.5, 4.2, 4.2) - ellipse(sx + 0.5, sy + 0.5, 3.2, 3.2):
            self.put(xx, yy, k)
        self.put(sx - 1, sy - 1, self.c('w'))
        self.building(x + 1, y + 10, 7, 5, e, f, (x + 3, y + 12))   # farmhouse

    def wreck(self, pos, kv, where):
        x, y = xy(pos[0], where)
        body = self.c(kv.get('color', 'r'), where)
        if not any(self.road[yy][xx] for yy in range(max(y, 0), min(y + 5, JH)) for xx in range(max(x, 0), min(x + 5, JW))):
            raise ArtError('%s: a wreck belongs on a road' % where)
        k = self.c('k')
        horiz = kv.get('dir', 'h') == 'h'
        w, h = (5, 3) if horiz else (3, 5)
        for yy in range(y - 1, y + h + 1):
            for xx in range(x - 1, x + w + 1):
                inside = x <= xx < x + w and y <= yy < y + h
                self.put(xx, yy, body if inside else k)
        self.put(x + (1 if horiz else 1), y + 1, self.c('f'))

    def ship(self, pos, kv, where):
        x, y = xy(pos[0], where)
        if not all(self.water[yy][xx] for yy in (y + 3, y + 5) for xx in (x, x + 12) if 0 <= xx < JW and 0 <= yy < JH):
            raise ArtError('%s: the ship must float' % where)
        k, f, e, l, w = (self.c(ch) for ch in 'kfelw')
        for yy in range(y + 3, y + 6):              # hull, narrowing to the keel
            inset = yy - (y + 3)
            for xx in range(x + inset, x + 13 - inset):
                self.put(xx, yy, k if yy == y + 5 or xx in (x + inset, x + 12 - inset) else e)
        for xx in range(x - 1, x + 14):
            self.put(xx, y + 3, k)
        for yy in range(y, y + 3):                  # cabin with lit windows
            for xx in range(x + 3, x + 9):
                self.put(xx, yy, k if yy == y or xx in (x + 3, x + 8) else (l if xx % 2 == 0 else f))
        for yy in range(y - 4, y):                  # mast and its light
            self.put(x + 10, yy, f)
        self.put(x + 10, y - 5, w)

    def build(self):
        S = self.src
        g = list(S.rows('ground'))
        if len(g) != 1:
            raise ArtError('lb.txt: exactly one ground row expected')
        self.ground(g[0][1], g[0][2])
        for kw in ('field', 'wood', 'river', 'sea', 'town'):
            for pos, kv, where in S.rows(kw):
                getattr(self, kw)(pos, kv, where)
        self.roads(list(S.rows('road')))
        for kw in ('gas', 'farm', 'wreck', 'ship'):
            for pos, kv, where in S.rows(kw):
                getattr(self, kw)(pos, kv, where)
        for row in self.pix:
            if 0 in row:
                raise ArtError('journey map leaves pixels at index 0')
        ts = TileSet()
        tmap = [[ts.add(tile_at(self.pix, tx * 8, ty * 8)) for tx in range(30)] for ty in range(20)]
        if len(ts.tiles) > 512:
            raise ArtError('journey map needs %d tiles (max 512)' % len(ts.tiles))
        for ty in range(20):
            for tx in range(30):
                if decode_se(ts.tiles, tmap[ty][tx]) != tile_at(self.pix, tx * 8, ty * 8):
                    raise ArtError('journey tiles do not rebuild the map at tile %d,%d' % (tx, ty))
        pal = [bgr555(c) for c in self.J.colors]
        return ts, tmap, pal, self.pix


def build_journey(src):
    return Journey(src).build()


# --------------------------------------------------------------------------
# GBA tile helpers

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


def obj16(pix):
    """16x16 -> 128 bytes, 1D order TL, TR, BL, BR"""
    return b''.join(tile_bytes(tile_at(pix, x, y)) for (x, y) in ((0, 0), (8, 0), (0, 8), (8, 8)))


class TileSet:
    """deduplicating tile bank with h/v flip matching; add() -> index | hflip<<10 | vflip<<11"""

    def __init__(self):
        self.tiles = []
        self.lookup = {}

    def add(self, t):
        if t in self.lookup:
            idx, fl = self.lookup[t]
            return idx | (fl << 10)
        idx = len(self.tiles)
        self.tiles.append(t)
        self.lookup[t] = (idx, 0)
        h = tile_hflip(t)
        v = tile_vflip(t)
        for var, fl in ((h, 1), (v, 2), (tile_vflip(h), 3)):
            self.lookup.setdefault(var, (idx, fl))
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


def bgr555(rgb):
    r, g, b = rgb
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def gba_rgb(rgb):
    return tuple(((c >> 3) << 3) | (c >> 5) for c in rgb)


# --------------------------------------------------------------------------
# C output

def c_bytes(data, indent, per=16):
    out = []
    for i in range(0, len(data), per):
        out.append(indent + ', '.join('0x%02X' % b for b in data[i:i + per]) + ',')
    return '\n'.join(out)


def c_u16(vals, indent, per=10):
    out = []
    for i in range(0, len(vals), per):
        out.append(indent + ', '.join('0x%04X' % v for v in vals[i:i + per]) + ',')
    return '\n'.join(out)


def write_c(path, enums, people, roofs, icons, journey):
    rts, rmeta, _ = roofs
    jts, jmap, jpal, _ = journey
    L = ['// GENERATED by tools/lbart.py from art/lb.txt (contract: src/lbart.h). Do not edit:',
         '// change the rows in art/lb.txt and rebuild.',
         '#include "lbart.h"',
         '',
         '#define LBART_ALIGN __attribute__((aligned(4)))',
         '',
         '// people: 16x16 OBJ, frames idle / bob, 1D tiles TL TR BL BR, pal_survivor',
         'const uint8_t lbart_person_gfx[LBG_COUNT][2][128] LBART_ALIGN = {']
    for n, frames in zip(enums['LBG'][:-1], people):
        L.append('    [%s] = {' % n)
        for f in frames:
            L.append('        {')
            L.append(c_bytes(obj16(f), '            '))
            L.append('        },')
        L.append('    },')
    L.append('};')
    L.append('')
    L.append('// roofs: BG tiles for charblock 0 from LBART_BG_TILE_BASE, pal_terrain')
    L.append('const int lbart_bg_tile_count = %d;' % len(rts.tiles))
    L.append('const uint8_t lbart_bg_tiles[%d * 32] LBART_ALIGN = {' % len(rts.tiles))
    L.append(c_bytes(rts.data(), '    '))
    L.append('};')
    L.append('_Static_assert(sizeof lbart_bg_tiles / 32 <= LBART_BG_TILE_COUNT_MAX, "roof tiles overflow charblock 0");')
    L.append('const uint16_t lbart_roof_meta[LBR_COUNT][4] = {')
    for n, m in zip(enums['LBR'][:-1], rmeta):
        L.append('    [%s] = { %s },' % (n, ', '.join('0x%04X' % v for v in m)))
    L.append('};')
    L.append('')
    L.append('// icons: 16x16 OBJ, 1D tiles TL TR BL BR, pal_cursor (indices 1..7)')
    L.append('const uint8_t lbart_icon_gfx[LBI_COUNT][128] LBART_ALIGN = {')
    for n, pix in zip(enums['LBI'][:-1], icons):
        L.append('    [%s] = {' % n)
        L.append(c_bytes(obj16(pix), '        '))
        L.append('    },')
    L.append('};')
    L.append('')
    L.append('// journey map: 240x160 backdrop, charblock 1, own palette')
    L.append('const int lbart_journey_tile_count = %d;' % len(jts.tiles))
    L.append('const uint8_t lbart_journey_tiles[%d * 32] LBART_ALIGN = {' % len(jts.tiles))
    L.append(c_bytes(jts.data(), '    '))
    L.append('};')
    L.append('_Static_assert(sizeof lbart_journey_tiles / 32 <= 512, "journey tiles overflow charblock 1");')
    L.append('const uint16_t lbart_journey_map[20][30] = {')
    for row in jmap:
        L.append('    {')
        L.append(c_u16(row, '        '))
        L.append('    },')
    L.append('};')
    L.append('const uint16_t lbart_journey_pal[16] = {')
    L.append(c_u16(jpal, '    ', 8))
    L.append('};')
    data = '\n'.join(L) + '\n'
    tmp = path + '.tmp'
    with open(tmp, 'w', encoding='utf-8', newline='\n') as f:
        f.write(data)
    os.replace(tmp, path)


# --------------------------------------------------------------------------
# previews (Pillow)

def ref_sprites(src):
    """MT_PLAIN (grass) and the zombies from gfx.py's art, for scale checks"""
    out = {}
    for fn, names in (('terrain.txt', ('MT_PLAIN', 'road_tex')), ('units.txt', ('UG_WALKER', 'UG_RUNNER', 'UG_SURVIVOR'))):
        p = os.path.join(src.dir, fn)
        if os.path.exists(p):
            _, _, sp = read_art_file(p, names)
            for n, (pal, rows) in sp.items():
                P = src.pals[pal]
                out[n] = (pal, [[P.idx(ch, n) for ch in r] for r in rows])
    return out


def blit(im, pix, ox, oy, pal, scale=1, hflip=False):
    w = len(pix[0])
    for y, row in enumerate(pix):
        for x, v in enumerate(row):
            if v:
                c = gba_rgb(pal.colors[v])
                xx = (w - 1 - x) if hflip else x
                for dy in range(scale):
                    for dx in range(scale):
                        im.putpixel((ox + xx * scale + dx, oy + y * scale + dy), c)


def grass(im, ox, oy, w, h, refs, tpal, scale):
    g = refs.get('MT_PLAIN', ('terrain', blank(16, 16, 3)))[1]
    for ty in range(0, h, 16):
        for tx in range(0, w, 16):
            blit(im, g, ox + tx * scale, oy + ty * scale, tpal, scale)


def preview_people(path, src, people, refs, Image):
    S = 4
    names = read_enums_cached(src)['LBG'][:-1]
    n = len(names) + 1
    W, H = n * 20 * S, 2 * 20 * S
    im = Image.new('RGB', (W, H), (40, 40, 48))
    spal, hpal, tpal = src.pals['survivor'], src.pals['horde'], src.pals['terrain']
    for f in range(2):
        for k in range(n):
            ox, oy = (k * 20 + 2) * S, (f * 20 + 2) * S
            grass(im, ox, oy, 16, 16, refs, tpal, S)
            if k < len(names):
                blit(im, people[k][f], ox, oy, spal, S)
            elif 'UG_WALKER' in refs:
                pix = refs['UG_WALKER'][1]
                if f:
                    pix = [[0] * 16] + [r[:] for r in pix[:12]] + [r[:] for r in pix[13:]]
                blit(im, pix, ox, oy, hpal, S, hflip=True)
    im.save(path)


def preview_roofs(path, src, roofs, refs, Image):
    ts, meta, metas = roofs
    names = read_enums_cached(src)['LBR'][:-1]
    tpal = src.pals['terrain']
    S = 2
    sizes = ((2, 2), (3, 2), (4, 2), (3, 3))
    kinds = [n for n in names if n.startswith('LBR_SIGN_')]
    W = (sum(w for w, h in sizes) + len(sizes) + 1) * 16
    H = (len(kinds) * 4 + 1) * 16
    im = Image.new('RGB', (W * S, H * S))
    grass(im, 0, 0, W, H, refs, tpal, S)
    for r, k in enumerate(kinds):
        x = 16
        for w, h in sizes:
            blit(im, compose_building(metas, names, w, h, k), x * S, (16 + r * 64) * S, tpal, S)
            x += (w + 1) * 16
    im.save(path)


def preview_icons(path, src, icons, refs, Image):
    S = 4
    names = read_enums_cached(src)['LBI'][:-1]
    cpal, tpal = src.pals['cursor'], src.pals['terrain']
    road = refs.get('road_tex', ('terrain', blank(16, 16, 7)))[1]
    W, H = (len(names) * 20 + 4) * S, 44 * S
    im = Image.new('RGB', (W, H), (40, 40, 48))
    for k, pix in enumerate(icons):
        for r in range(2):
            ox, oy = (k * 20 + 4) * S, (r * 20 + 4) * S
            if r == 0:
                grass(im, ox, oy, 16, 16, refs, tpal, S)
            else:
                blit(im, road, ox, oy, tpal, S)
            blit(im, pix, ox, oy, cpal, S)
    im.save(path)


def preview_scene(path1, path3, src, people, roofs, icons, refs, Image):
    """a mock tactical screen (15x10 tiles) placed the way tac_gfx.c draws it: roofs over the
    footprint (sign on the top-left tile), doors 8 px outside the door tile (N = vflip of S,
    W = hflip of E), status icons over people, zombies facing left"""
    ts, meta, metas = roofs
    names = read_enums_cached(src)
    LBR, LBI, LBG = names['LBR'], names['LBI'], names['LBG']
    tpal, spal, hpal, cpal = (src.pals[n] for n in ('terrain', 'survivor', 'horde', 'cursor'))
    im = Image.new('RGB', (240, 160))
    grass(im, 0, 0, 240, 160, refs, tpal, 1)
    road = refs.get('road_tex', ('terrain', blank(16, 16, 7)))[1]
    for tx in range(15):
        for ty in (4, 5):
            blit(im, road, tx * 16, ty * 16, tpal, 1)
    bldgs = [(1, 1, 2, 2, 'LBR_SIGN_HOUSE', (1, 2, 2), 0), (5, 1, 3, 2, 'LBR_SIGN_SHOP', (6, 2, 2), 1),
             (10, 7, 2, 2, 'LBR_SIGN_PHARMACY', (11, 7, 1), 0), (2, 7, 4, 2, 'LBR_SIGN_GARAGE', (3, 7, 0), 1),
             (10, 0, 3, 3, 'LBR_SIGN_HOUSE', (10, 1, 3), 0)]
    for bx, by, w, h, sign, door, barred in bldgs:
        blit(im, compose_building(metas, LBR[:-1], w, h, sign), bx * 16, by * 16, tpal, 1)
    def obj(pix, x, y, pal, hflip=False, vflip=False):
        if vflip:
            pix = pix[::-1]
        blit(im, pix, x, y, pal, 1, hflip)
    for bx, by, w, h, sign, (dx, dy, d), barred in bldgs:
        ic = LBI.index('LBI_DOOR_E' if d & 1 else 'LBI_DOOR_S') + (2 if barred else 0)
        ox, oy = (1 if d == 1 else -1 if d == 3 else 0), (1 if d == 2 else -1 if d == 0 else 0)
        obj(icons[ic], dx * 16 + ox * 8, dy * 16 + oy * 8, cpal, hflip=d == 3, vflip=d == 0)
    obj(icons[LBI.index('LBI_CACHE')], 8 * 16, 3 * 16, cpal)
    for y in (4, 5):
        obj(icons[LBI.index('LBI_EXIT')], 0, y * 16, cpal)
    obj(icons[LBI.index('LBI_NOISE')], 9 * 16, 5 * 16, cpal)
    folk = [(0, 3, 'LBG_MARA', None), (2, 4, 'LBG_DOC', 'LBI_INFECTED'), (4, 5, 'LBG_JUNE', None),
            (6, 4, 'LBG_HOLT', None), (8, 6, 'LBG_ROSA', 'LBI_HIDDEN'), (5, 6, 'LBG_TESS', 'LBI_DOWNED'),
            (13, 4, 'LBG_MAN', None), (7, 9, 'LBG_WOMAN', None)]
    for x, y, n, st in folk:
        obj(people[LBG.index(n)][(x + y) & 1], x * 16, y * 16, spal)
        if st:
            obj(icons[LBI.index(st)], x * 16, y * 16, cpal)
    for x, y, n in ((11, 4, 'UG_WALKER'), (12, 5, 'UG_RUNNER'), (13, 6, 'UG_WALKER'), (14, 3, 'UG_WALKER')):
        if n in refs:
            obj(refs[n][1], x * 16, y * 16, hpal, hflip=True)
    im.save(path1)
    im.resize((720, 480), Image.NEAREST).save(path3)


def load_font(src):
    """glyphs of art/font.txt (for the run.c overlay in the journey preview)"""
    p = os.path.join(src.dir, 'font.txt')
    gl = {}
    if not os.path.exists(p):
        return gl
    inside = False
    for raw in open(p, encoding='utf-8'):
        t = raw.split()
        if not t:
            continue
        if t[0] == 'glyphs':
            inside = t[1] == 'font'
            continue
        if t[0] == 'end':
            inside = False
            continue
        if inside and len(t) == 9:
            code = ord(t[0]) if len(t[0]) == 1 else int(t[0], 0)
            gl[code] = [[1 if ch == '#' else 0 for ch in r] for r in t[1:]]
    return gl


def preview_journey(path, src, journey, Image, overlay=False):
    ts, tmap, pal, pix = journey
    J = src.pals['journey']
    S = 3
    im = Image.new('RGB', (JW * S, JH * S))
    # drawn from the tiles and the map, as the GBA shows it
    for ty in range(20):
        for tx in range(30):
            t = decode_se(ts.tiles, tmap[ty][tx])
            for y in range(8):
                for x in range(8):
                    c = gba_rgb(J.colors[t[y * 8 + x]])
                    for dy in range(S):
                        for dx in range(S):
                            im.putpixel(((tx * 8 + x) * S + dx, (ty * 8 + y) * S + dy), c)
    if overlay:   # what run.c's journey_screen() puts on top (status, labels, window)
        fill, white, black = (0x28, 0x30, 0x58), (0xf8, 0xf8, 0xf0), (0x10, 0x10, 0x18)
        font = load_font(src)

        def cell(cx, cy, col=fill):
            for y in range(8 * S):
                for x in range(8 * S):
                    im.putpixel((cx * 8 * S + x, cy * 8 * S + y), col)

        def text(cx, cy, s, col=white):
            for k, ch in enumerate(s):
                g = font.get(ord(ch))
                if not g:
                    continue
                ox = 1 if len(g[0]) <= 5 else 0
                for y, row in enumerate(g):
                    for x, v in enumerate(row):
                        if v:
                            for sx, sy, c in ((1, 1, black), (0, 0, col)):
                                px, py = (cx + k) * 8 + ox + x + sx, cy * 8 + y + sy
                                if px < 240 and py < 160:
                                    for dy in range(S):
                                        for dx in range(S):
                                            im.putpixel((px * S + dx, py * S + dy), c)
        for cy in list(range(0, 4)) + list(range(16, 20)):
            for cx in range(30):
                cell(cx, cy)
        text(0, 0, 'Day 1')
        text(8, 0, 'The boat leaves Day 40')
        text(0, 1, 'Food 12   Fuel 10   Ammo 6')
        text(1, 17, 'Cedar Falls', (0xf8, 0xd8, 0x48))
        labels = {'CEDAR': ('Cedar', 'Falls'), 'MAIN': ('Main', 'Street'), 'GAS': ('Gas', 'Station'),
                  'FARM': ('Hensley', 'Farm'), 'HIGHWAY': ('County', 'Highway'), 'BRIDGE': ("Miller's", 'Bridge')}
        jr = Journey(src)
        for n, (x, y) in jr.nodes.items():
            l1, l2 = labels.get(n, (n, ''))
            w = 2 + max(len(l1), len(l2))
            for cy in (y, y + 1):
                for cx in range(max(x, 0), min(x + w, 30)):
                    cell(cx, cy)
            text(x, y, '*' if n == 'CEDAR' else 'o')
            text(x + 2, y, l1)
            text(x + 2, y + 1, l2)
    im.save(path)


_ENUMS = {}


def read_enums_cached(src):
    if 'v' not in _ENUMS:
        _ENUMS['v'] = read_enums(os.path.join(src.dir, '..', 'src', 'lbart.h'))
    return _ENUMS['v']


def main(argv):
    args = argv[1:]
    prev = None
    if '--preview' in args:
        k = args.index('--preview')
        prev = args[k + 1]
        del args[k:k + 2]
    if len(args) != 2:
        sys.stderr.write('usage: python3 -I tools/lbart.py art/lb.txt <outdir> [--preview DIR]\n')
        return 2
    src_path, outdir = args
    if prev is None:
        prev = os.path.join(outdir, '..', 'preview')
    try:
        src = Source(src_path)
        enums = read_enums_cached(src)
        people = build_people(src, enums)
        roofs = build_roofs(src, enums)
        icons = build_icons(src, enums)
        journey = build_journey(src)
    except ArtError as e:
        sys.stderr.write('lbart.py: error: %s\n' % e)
        return 1
    os.makedirs(outdir, exist_ok=True)
    write_c(os.path.join(outdir, 'lbart.c'), enums, people, roofs, icons, journey)
    print('lbart.py: %d people, roofs %d BG tiles, %d icons, journey %d tiles -> %s'
          % (len(people), len(roofs[0].tiles), len(icons), len(journey[0].tiles), os.path.join(outdir, 'lbart.c')))
    try:
        from PIL import Image
    except ImportError:
        Image = None
        sys.stderr.write('lbart.py: Pillow not installed, previews skipped\n')
    if Image:
        os.makedirs(prev, exist_ok=True)
        refs = ref_sprites(src)
        preview_people(os.path.join(prev, 'lb_people.png'), src, people, refs, Image)
        preview_roofs(os.path.join(prev, 'lb_roofs.png'), src, roofs, refs, Image)
        preview_icons(os.path.join(prev, 'lb_icons.png'), src, icons, refs, Image)
        preview_scene(os.path.join(prev, 'lb_scene_1x.png'), os.path.join(prev, 'lb_scene.png'),
                      src, people, roofs, icons, refs, Image)
        preview_journey(os.path.join(prev, 'lb_journey.png'), src, journey, Image)
        preview_journey(os.path.join(prev, 'lb_journey_ui.png'), src, journey, Image, overlay=True)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
