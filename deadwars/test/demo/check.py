#!/usr/bin/env python3
"""Checks a run of test/demo/lb_demo.txt (see run.sh).

usage: python3 -I check.py OUTDIR [--cmp OTHERDIR]

  - every shot NAME has its text and window layers dumped next to it (NAME-txt.bin = SB_TEXT,
    NAME-win.bin = SB_WINDOW, 32x20 u16 entries each): no glyph may sit on a window frame tile
    (text running past its box);
  - the end state (end-run.bin = RunState, end-gs.bin = GameState) must be the farm mission at
    night with the four starting people alive and the run still going;
  - --cmp: the end state must match another core's run byte for byte (same inputs, same game).
Prints one line per problem and a summary; exits 1 on any problem.
"""
import glob
import os
import struct
import sys

TILE_WINDOW, CENTER = 96, 100        # video.h: 9-slice window tiles 96..104, the centre is 100
FONT_COUNT = 96                      # glyphs for ASCII 32..127


def frame_text(out):
    bad = 0
    shots = sorted(glob.glob(os.path.join(out, '*-txt.bin')))
    for t in shots:
        w = t[:-len('-txt.bin')] + '-win.bin'
        name = os.path.basename(t)[:-len('-txt.bin')]
        if not os.path.exists(w):
            print('%s: no window dump' % name)
            bad += 1
            continue
        tx = struct.unpack('<640H', open(t, 'rb').read(1280))
        wn = struct.unpack('<640H', open(w, 'rb').read(1280))
        for y in range(20):
            for x in range(30):
                g, wt = tx[y * 32 + x] & 0x3FF, wn[y * 32 + x] & 0x3FF
                if 0 < g < FONT_COUNT and TILE_WINDOW <= wt <= TILE_WINDOW + 8 and wt != CENTER:
                    print('%s: "%s" on a window frame at (%d,%d)' % (name, chr(32 + g), x, y))
                    bad += 1
    return len(shots), bad


def end_state(out):
    bad = 0
    try:
        rs = open(os.path.join(out, 'end-run.bin'), 'rb').read()
        g = open(os.path.join(out, 'end-gs.bin'), 'rb').read()
    except OSError as e:
        print('end state: %s' % e)
        return 1
    # RunState (lb.h): day +9, node +11, over +15, nroster +16; roster[] at +36, 46 bytes each (Person), alive +1
    # GameState (game.h): side +8, night +9, over +11
    want = [('run.node (Hensley Farm)', rs[11], 3), ('run.over', rs[15], 0), ('run.nroster', rs[16], 4),
            ('gs.night', g[9], 1), ('gs.over', g[11], 0)]
    for i in range(4):
        want.append(('roster[%d].alive' % i, rs[36 + 46 * i + 1], 1))
    for what, got, exp in want:
        if got != exp:
            print('end state: %s = %d, want %d' % (what, got, exp))
            bad += 1
    return bad


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    out = sys.argv[1]
    n, bad = frame_text(out)
    bad += end_state(out)
    if '--cmp' in sys.argv:
        other = sys.argv[sys.argv.index('--cmp') + 1]
        for f in ('end-run.bin', 'end-gs.bin'):
            a, b = os.path.join(out, f), os.path.join(other, f)
            if not os.path.exists(b) or open(a, 'rb').read() != open(b, 'rb').read():
                print('%s differs from %s' % (a, b))
                bad += 1
    print('demo check: %d shots, %d problems' % (n, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
