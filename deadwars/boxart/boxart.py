#!/usr/bin/env python3
"""Onion OS game-list box art for Dead Wars and Advance Wars 2 - Remix.

usage:  python3 -I boxart/boxart.py <deadwars_dir>

Writes <dir>/boxart/deadwars.png and <dir>/boxart/aw2remix.png (250x360, Onion's
recommended maximum).  Everything is original pixel art: Dead Wars reuses the game's
own sprites from art/*.txt, the Remix cover uses the sprites in boxart/remix.txt.
Both are integer-scaled pixel art over a 2x low-res background, so they stay crisp.

Install on the SD card as  Roms/GBA/Imgs/<rom file name without .gba>.png
"""
import os
import sys
import random
import importlib.util

from PIL import Image, ImageChops

W, H = 250, 360


def load_gfx(root):
    spec = importlib.util.spec_from_file_location('gfx', os.path.join(root, 'tools', 'gfx.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def bayer(x, y):
    return [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]][y & 3][x & 3] / 16.0 + 1 / 32.0


def mix(a, b, t):
    return tuple(int(a[k] + (b[k] - a[k]) * t) for k in range(3))


def hexrgb(s):
    return tuple(int(s[k:k + 2], 16) for k in (0, 2, 4))


class Canvas:
    """RGB image plus helpers for stamping palette sprites at integer scales."""

    def __init__(self, gfx, art, w, h, bg=(0, 0, 0)):
        self.gfx, self.art = gfx, art
        self.img = Image.new('RGB', (w, h), bg)
        self.px = self.img.load()
        self.w, self.h = w, h

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[x, y] = c

    def rect(self, x0, y0, x1, y1, c):
        for y in range(max(y0, 0), min(y1, self.h)):
            for x in range(max(x0, 0), min(x1, self.w)):
                self.px[x, y] = c

    def stamp(self, name, x0, y0, scale=1, flip=False, pal=None, tint=None, remap=None, fade=None):
        """Draw art sprite NAME; tint=(rgb, amount) darkens toward a colour,
        remap={index: rgb} overrides palette entries (e.g. glowing eyes),
        fade=(rgb, rows) dissolves the bottom rows into a colour."""
        w, h, palname, pix = self.art.get(name)
        cols = [self.gfx.gba_rgb(c) for c in self.art.palette(pal or palname).colors]
        if remap:
            for k, v in remap.items():
                cols[k] = v
        for y in range(h):
            for x in range(w):
                v = pix[y][w - 1 - x if flip else x]
                if not v:
                    continue
                c = cols[v]
                if tint and not (remap and v in remap):
                    c = mix(c, tint[0], tint[1])
                for sy in range(scale):
                    for sx in range(scale):
                        X, Y = x0 + x * scale + sx, y0 + y * scale + sy
                        cc = c
                        if fade and y >= h - fade[1]:
                            t = (y - (h - fade[1]) + 1) / float(fade[1] + 1)
                            cc = mix(c, fade[0], 0.5) if t < bayer(X // scale, Y // scale) else fade[0]
                        self.put(X, Y, cc)

    def stamp_unit(self, gfx_name, x0, y0, scale, flip=False, frame=0):
        name = gfx_name + ('.f1' if frame and self.art.has(gfx_name + '.f1') else '')
        self.stamp(name, x0, y0, scale, flip)

    def text(self, s, x0, y0, scale, col, shadow=(0, 0, 0), font='font'):
        gw, gh, gl = self.art.glyphs[font]
        x = x0
        for ch in s:
            g = gl[ord(ch)]
            for gy, row in enumerate(g):
                for gx, v in enumerate(row):
                    if v:
                        for sy in range(scale):
                            for sx in range(scale):
                                if shadow:
                                    self.put(x + (gx + 1) * scale + sx - scale + scale, y0 + (gy + 1) * scale + sy, shadow)
            for gy, row in enumerate(g):
                for gx, v in enumerate(row):
                    if v:
                        for sy in range(scale):
                            for sx in range(scale):
                                self.put(x + gx * scale + sx, y0 + gy * scale + sy, col)
            x += (len(g[0]) + 1) * scale if ch != ' ' else 4 * scale

    def text_width(self, s, scale, font='font'):
        gw, gh, gl = self.art.glyphs[font]
        return sum((len(gl[ord(c)][0]) + 1) * scale if c != ' ' else 4 * scale for c in s) - scale


def logo(cv, lines, glyphs, scale, y0, colors, drips=0.0, depth=3, outline=2, seed=5, gap=None,
         xoff=0):
    """Chunky extruded logo (same recipe as the title screen).
    colors: dict hi, mid, lo, drip, ext, out.  Returns bottom y."""
    gw, gh, gl = glyphs
    w, h = cv.w, cv.h
    mask = [[0] * w for _ in range(h)]
    lh = gh * scale
    gap = scale if gap is None else gap
    tops = []
    y = y0
    for line in lines:
        adv = [4 * scale if c == ' ' else (len(gl[ord(c)][0]) + 1) * scale for c in line]
        total = sum(adv) - scale
        x = (w - total) // 2 + xoff
        for c, a in zip(line, adv):
            if c != ' ':
                for gy, row in enumerate(gl[ord(c)]):
                    for gx, v in enumerate(row):
                        if v:
                            for sy in range(scale):
                                for sx in range(scale):
                                    yy, xx = y + gy * scale + sy, x + gx * scale + sx
                                    if 0 <= yy < h and 0 <= xx < w:
                                        mask[yy][xx] = 1
            x += a
        tops.append(y)
        y += lh + gap
    bottom = y - gap
    if drips:
        dr = random.Random(seed)
        yl = bottom - 1
        x = 0
        while x < w - 3:
            if mask[yl][x] and mask[yl][x + 1] and mask[yl][x + 2] and dr.random() < drips:
                n = dr.randrange(4, 22)
                for k in range(n):
                    for dx in range(3 if k < n - 6 else 2):
                        if yl + 1 + k < h:
                            mask[yl + 1 + k][x + dx] = 2
                for k in range(2):
                    for dx in range(2):
                        if yl + n + 1 + k < h:
                            mask[yl + n + 1 + k][x + dx] = 2
                x += 9
            x += 1
    ext = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if mask[y][x] == 1:
                for k in range(1, depth + 1):
                    if y + k < h and x + k < w and not mask[y + k][x + k]:
                        ext[y + k][x + k] = 1
    for y in range(h):
        for x in range(w):
            if mask[y][x] or ext[y][x]:
                continue
            near = False
            for dy in range(-outline, outline + 1):
                for dx in range(-outline, outline + 1):
                    if abs(dx) + abs(dy) <= outline and 0 <= y + dy < h and 0 <= x + dx < w \
                            and (mask[y + dy][x + dx] or ext[y + dy][x + dx]):
                        near = True
                        break
                if near:
                    break
            if near:
                cv.px[x, y] = colors['out']
    for y in range(h):
        for x in range(w):
            if ext[y][x]:
                cv.px[x, y] = colors['ext']
    for y in range(h):
        for x in range(w):
            m = mask[y][x]
            if not m:
                continue
            if m == 2:
                cv.px[x, y] = colors['drip']
                continue
            top = max(t for t in tops if t <= y)
            rel = (y - top) / float(lh)
            if rel < 0.1 and (not mask[y - 1][x] or y - top < scale):
                c = colors['hi']
            elif not mask[y][x - 1] and rel < 0.55:
                c = colors['hi']
            elif rel < 0.45:
                c = colors['mid']
            elif rel < 0.7:
                c = colors['mid'] if (0.7 - rel) / 0.25 > bayer(x // 2, y // 2) else colors['lo']
            else:
                c = colors['lo']
            cv.px[x, y] = c
    return bottom


# ---------------------------------------------------------------------------- Dead Wars

def dead_wars(gfx, root):
    art = gfx.parse_art(os.path.join(root, 'art'))
    T = {k: gfx.gba_rgb(art.palette('title').colors[v]) for k, v in art.palette('title').chars.items()}
    # low-res (2x) backdrop: sky, stars, moon, ruined skyline, street fog
    lw, lh = W // 2, H // 2
    lo = Canvas(gfx, art, lw, lh)
    sky = [T['A'], T['B'], T['C'], T['D']]
    bands = [(0, 44), (44, 74), (74, 98), (98, lh)]
    for k, (a, b) in enumerate(bands):
        for y in range(a, b):
            for x in range(lw):
                c = sky[k]
                if k and y < a + 8 and (y - a + 0.5) / 8.0 <= bayer(x, y):
                    c = sky[k - 1]
                lo.px[x, y] = c
    rnd = random.Random(21)
    for _ in range(40):
        lo.put(rnd.randrange(lw), rnd.randrange(1, 90), T['M'] if rnd.random() < 0.6 else T['m'])
    lo.stamp('moon', 3, 50)

    def skyline(base, col, seed, minw, maxw, minh, maxh, lit, p):
        r = random.Random(seed)
        x = -r.randrange(0, 6)
        while x < lw:
            bw, bh = r.randrange(minw, maxw + 1), r.randrange(minh, maxh + 1)
            top = base - bh
            tops = [top] * bw
            if r.random() < 0.65:
                start = r.randrange(bw // 4, max(bw // 4 + 1, 3 * bw // 4))
                t = top
                cols = range(start, bw) if r.random() < 0.5 else range(bw - 1 - start, -1, -1)
                for c in cols:
                    t += r.choice((0, 1, 1, 2, 3))
                    tops[c] = min(t, base - 4)
            for c in range(bw):
                for yy in range(max(tops[c], 0), base):
                    lo.put(x + c, yy, col)
            if r.random() < 0.4:
                ax = r.randrange(1, max(2, bw - 1))
                for yy in range(tops[ax] - r.randrange(3, 10), tops[ax]):
                    lo.put(x + ax, yy, col)
            for wy in range(top + 3, base - 4, 5):
                for wx in range(x + 2, x + bw - 2, 3):
                    if 0 <= wx - x < bw and wy - 1 >= tops[wx - x] + 1 and r.random() < p:
                        lo.put(wx, wy, lit)
                        lo.put(wx, wy + 1, lit)
            x += bw + r.randrange(0, 3)

    skyline(140, T['E'], 11, 9, 20, 40, 74, T['C'], 0.10)
    back = lo.img.copy()
    skyline(150, T['F'], 4, 7, 16, 22, 54, T['y'], 0.18)
    for y in range(146, lh):
        for x in range(lw):
            lo.px[x, y] = T['E']
    for y in range(136, 156):
        for x in range(lw):
            lv = (1 - abs(y - 146) / 8.0) * 0.75
            if lv > bayer(x, y):
                lo.px[x, y] = T['C'] if lv > 0.5 else T['E']
    for x in range(lw):   # kerb + faded lane paint
        lo.px[x, 158] = T['F']
        if (x // 5) % 3 == 0:
            lo.px[x, 170] = T['C']
    cv = Canvas(gfx, art, W, H)
    cv.img.paste(back.resize((W, H), Image.NEAREST))
    cv.px = cv.img.load()

    # Patient Zero looms over the city: 3x, sunk into the dark, eyes left burning,
    # rising from behind the near skyline
    zp = art.palette('pt_zero').chars
    glow = {zp['e']: (232, 255, 96), zp['E']: (128, 200, 40)}
    cv.stamp('PT_ZERO', 96, 138, 3, tint=(T['B'], 0.5), remap=glow, fade=(T['F'], 8))
    front = lo.img.resize((W, H), Image.NEAREST)
    diff = ImageChops.difference(front, back.resize((W, H), Image.NEAREST)).convert('L').point(
        lambda v: 255 if v else 0)
    cv.img.paste(front, (0, 0), diff)
    # the horde shambles in from the right (silhouettes, then sprites)
    for name, x, base, flip, s in (('zombie_a', 150, 300, True, 2), ('zombie_c', 196, 300, True, 2),
                                   ('zombie_b', 226, 302, True, 2), ('zombie_a', 120, 296, True, 1),
                                   ('zombie_b', 136, 298, True, 1), ('zombie_c', 178, 294, True, 1)):
        zw, zh = art.get(name)[:2]
        cv.stamp(name, x, base - zh * s, s, flip, remap={art.palette('title').chars['R']: (255, 48, 40)})
    # Mara holds the line, front left (her bust's cut-off base sinks behind the street)
    cv.stamp('PT_MARA', -6, 196, 3)
    # the street in front, drawn over the busts' bottom edges
    for y in range(296, H):
        for x in range(W):
            c = T['E'] if y < 300 else T['F']
            if y >= 300 and (x // 2 + y // 2) % 9 == 0 and (y // 2) % 3 == 0:
                c = T['E']
            cv.px[x, y] = c
    for x in range(0, W, 2):
        if (x // 10) % 3 == 0:
            cv.rect(x, 328, x + 2, 330, T['C'])
    # map units square off on the street
    sv = [('UG_SURVIVOR', 4), ('UG_HEAVY', 38), ('UG_ARMOR', 72)]
    for k, (n, x) in enumerate(sv):
        cv.stamp_unit(n, x, 302 + (k & 1) * 4, 2, frame=k & 1)
    hd = [('UG_WALKER', 132), ('UG_RUNNER', 164), ('UG_BRUTE', 196), ('UG_BLOATER', 220)]
    for k, (n, x) in enumerate(hd):
        cv.stamp_unit(n, x, 302 + ((k + 1) & 1) * 4, 2, flip=True, frame=k & 1)
    # tagline
    s = 'SURVIVORS vs THE HORDE'
    tw = cv.text_width(s, 1)
    cv.rect(0, 340, W, 356, (8, 6, 12))
    cv.text(s, (W - tw) // 2, 344, 1, (236, 228, 192), shadow=(90, 18, 26))
    # logo last so it sits on top of everything
    logo(cv, ['DEAD', 'WARS'], art.glyphs['logo'], 4, 12,
         dict(hi=T['h'], mid=T['R'], lo=T['r'], drip=T['r'], ext=T['k'], out=T['K']),
         drips=0.22, depth=5, outline=3, gap=8)
    return cv.img


# ---------------------------------------------------------------------------- AW2 Remix

def remix(gfx, root):
    art = gfx.parse_art(os.path.join(root, 'boxart'))
    font = gfx.parse_art(os.path.join(root, 'art'))
    art.glyphs['font'] = font.glyphs['font']
    P = {k: gfx.gba_rgb(art.palette('remix').colors[v]) for k, v in art.palette('remix').chars.items()}
    lw, lh = W // 2, H // 2
    lo = Canvas(gfx, art, lw, lh)
    # sunburst behind the logo, blue (left) vs red (right) split down a diagonal
    cx, cy = lw // 2, 58
    import math
    for y in range(lh):
        for x in range(lw):
            a = math.atan2(y - cy, x - cx)
            ray = int((a + math.pi) / (2 * math.pi) * 24) & 1
            left = x - cx < (y - cy) * -0.35
            if left:
                c = P['b'] if ray else P['B']
            else:
                c = P['r'] if ray else P['R']
            d = math.hypot(x - cx, y - cy)
            if d < 26:
                c = P['Y'] if ray else P['y']
            elif d < 34 and (d - 26) / 8.0 < bayer(x, y):
                c = P['Y'] if ray else P['y']
            lo.px[x, y] = c
    # ground: grass field, then a road, with a dither horizon
    for y in range(112, lh):
        for x in range(lw):
            c = P['G'] if y < 150 else P['g']
            if 112 <= y < 118 and (y - 112 + 0.5) / 6.0 < bayer(x, y):
                c = lo.px[x, y]
            lo.px[x, y] = c
    for y in range(150, 168):
        for x in range(lw):
            c = P['a']
            if y in (150, 167):
                c = P['K']
            elif y == 158 and (x // 4) % 2 == 0:
                c = P['c']
            lo.px[x, y] = c
    rnd = random.Random(3)
    for _ in range(70):   # grass tufts
        x, y = rnd.randrange(lw), rnd.randrange(120, 149)
        lo.put(x, y, P['l'])
        lo.put(x + 1, y - 1, P['l'])
    for _ in range(40):
        x, y = rnd.randrange(lw), rnd.randrange(168, lh)
        lo.put(x, y, P['G'])
    cv = Canvas(gfx, art, W, H)
    cv.img.paste(lo.img.resize((W, H), Image.NEAREST))
    cv.px = cv.img.load()
    # the armies (original sprites): blue left facing right, red right facing left
    cv.stamp('fighter', 8, 186, 2, pal='remix_blue')
    cv.stamp('fighter', 194, 176, 2, flip=True, pal='remix_red')
    cv.stamp('tank_big', 6, 222, 4, pal='remix_blue')
    cv.stamp('tank_big', 148, 222, 4, flip=True, pal='remix_red')
    for k, x in enumerate((8, 40, 72)):
        cv.stamp('trooper', x, 308 + (k & 1) * 4, 2, pal='remix_blue')
    for k, x in enumerate((146, 178, 210)):
        cv.stamp('trooper', x, 308 + ((k + 1) & 1) * 4, 2, flip=True, pal='remix_red')
    # VS spark between the tanks
    cv.stamp('spark', 109, 248, 2, pal='remix_blue')
    # title block
    logo(cv, ['ADVANCE'], art.glyphs['block'], 3, 18,
         dict(hi=P['W'], mid=P['c'], lo=P['C'], drip=P['C'], ext=P['n'], out=P['K']),
         depth=3, outline=2)
    logo(cv, ['WARS 2'], art.glyphs['block'], 5, 52,
         dict(hi=P['W'], mid=P['c'], lo=P['C'], drip=P['C'], ext=P['n'], out=P['K']),
         depth=4, outline=3)
    # REMIX: a slanted stencil banner across the logo foot
    ban = Canvas(gfx, art, W, 60, (255, 0, 255))
    ban.rect(0, 8, W, 52, P['K'])
    ban.rect(0, 11, W, 49, P['Y'])
    ban.rect(0, 13, W, 47, P['O'])
    for x in range(0, W, 12):
        for y in range(13, 47):
            if (x + y) % 12 < 2:
                pass
    logo(ban, ['REMIX'], art.glyphs['block'], 4, 12,
         dict(hi=P['W'], mid=P['W'], lo=P['c'], drip=P['c'], ext=P['K'], out=P['K']),
         depth=3, outline=2)
    bp = ban.img.load()
    for y in range(60):
        for x in range(W):
            c = bp[x, y]
            if c == (255, 0, 255):
                continue
            yy = 116 + y - (x - W // 2) // 8   # rises to the right
            cv.put(x, yy, c)
    s = 'ORIGINAL ART  -  FAN REMIX'
    cv.rect(0, 344, W, 360, P['K'])
    cv.text(s, (W - cv.text_width(s, 1)) // 2, 348, 1, P['W'], shadow=P['n'])
    return cv.img


def main(argv):
    if len(argv) != 2:
        sys.stderr.write('usage: python3 -I boxart/boxart.py <deadwars_dir>\n')
        return 2
    root = argv[1]
    gfx = load_gfx(root)
    out = os.path.join(root, 'boxart')
    for name, fn in (('deadwars.png', dead_wars), ('aw2remix.png', remix)):
        img = fn(gfx, root)
        assert img.size == (W, H)
        img.save(os.path.join(out, name), optimize=True)
        print('boxart: wrote', os.path.join(out, name))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
