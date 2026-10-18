#!/usr/bin/env python3
"""Contact sheet for matrix.sh: one row per case, one column per screenshot (needs Pillow).
usage: python3 -I sheet.py OUTDIR"""
import os
import sys

from PIL import Image, ImageDraw

out = sys.argv[1]
cases = sorted(d for d in os.listdir(out) if os.path.isfile(os.path.join(out, d, 'log.txt')))
shots = []  # in the order the scripts took them
for c in cases:
    for line in open(os.path.join(out, c, 'log.txt')):
        if line.startswith('shot ') and line.split()[1] not in shots:
            shots.append(line.split()[1])
LABEL = 130
sheet = Image.new('RGB', (LABEL + 244 * len(shots), 164 * len(cases)), (40, 40, 40))
draw = ImageDraw.Draw(sheet)
for r, c in enumerate(cases):
    draw.text((4, 164 * r + 70), c, fill=(255, 255, 255))
    for k, s in enumerate(shots):
        p = os.path.join(out, c, s + '.png')
        if os.path.exists(p):
            sheet.paste(Image.open(p), (LABEL + 244 * k, 164 * r + 2))
for k, s in enumerate(shots):
    draw.text((LABEL + 244 * k + 2, 2), s, fill=(255, 255, 0))
sheet.save(os.path.join(out, 'sheet.png'))
