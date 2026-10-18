#!/usr/bin/env python3
"""Compare same-named screenshots of two playtest runs; print one line per differing shot.
usage: python3 -I cmpshots.py DIR_A DIR_B"""
import os
import sys

from PIL import Image, ImageChops

a, b = sys.argv[1], sys.argv[2]
for f in sorted(os.listdir(a)):
    if not f.endswith('.png') or not os.path.exists(os.path.join(b, f)):
        continue
    ia = Image.open(os.path.join(a, f)).convert('RGB')
    ib = Image.open(os.path.join(b, f)).convert('RGB')
    if ia.size != ib.size:
        print('%s: size %s vs %s' % (f, ia.size, ib.size))
        continue
    box = ImageChops.difference(ia, ib).getbbox()
    if box:
        n = ImageChops.difference(ia, ib).convert('L').point(lambda v: 255 if v else 0).histogram()[255]
        print('%s: %d pixels differ in %s' % (f, n, box))
