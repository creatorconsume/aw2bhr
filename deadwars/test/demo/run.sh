#!/usr/bin/env bash
# The Last Boat demo: plays test/demo/lb_demo.txt headless and checks it.
#
# Usage: deadwars/test/demo/run.sh [options]          (works from any directory)
#   -n, --no-build   use the existing deadwars/deadwars.gba
#   -g, --gpsp       also play it on gpSP (tools/emu-lab lrun, the Onion core) and compare
#   -o, --out DIR    output directory (default deadwars/build/demo, git-ignored)
#
# Steps:
#   1. make -C deadwars (unless -n); build the headless runner (test/gbarun.c, libmgba) into
#      deadwars/build/gbarun when it is missing or older than its source.
#   2. Plays the script on fresh SRAM (mGBA). Every "shot NAME" becomes OUT/mgba/NAME.png (2x), and
#      the script dumps the text and window layers after each shot. At the end the RunState and
#      GameState are dumped too (addresses from build/deadwars.map).
#   3. check.py: no text on a window frame in any shot; the run ends in the Hensley Farm night
#      mission with all four alive.
#   4. -g: the same script on gpSP. gpSP's frame counter runs 3 frames ahead of mGBA's at the same
#      input, and the run's seed comes from it, so the first "wait 60" becomes "wait 57" there; then
#      both cores must end in byte-identical RunState and GameState.
# Exit: 0 pass, 1 a check failed, 2 missing prerequisite, 3 build failed.
set -u
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DW=$(cd "$HERE/../.." && pwd)
TOP=$(cd "$DW/.." && pwd)
OUT=$DW/build/demo
BUILD=1
GPSP=0
while [ $# -gt 0 ]; do
	case "$1" in
	-n|--no-build) BUILD=0 ;;
	-g|--gpsp) GPSP=1 ;;
	-o|--out) OUT=$2; shift ;;
	-h|--help) sed -n '2,20p' "$0"; exit 0 ;;
	*) echo "demo: unknown option $1" >&2; exit 2 ;;
	esac
	shift
done
python3 -c 'import PIL' 2>/dev/null || { echo "demo: needs python3 + Pillow" >&2; exit 2; }
if [ $BUILD = 1 ]; then
	make -s -C "$DW" >/dev/null || { echo "demo: build failed" >&2; exit 3; }
fi
ROM=$DW/deadwars.gba
MAP=$DW/build/deadwars.map
[ -f "$ROM" ] && [ -f "$MAP" ] || { echo "demo: no ROM or map; build first" >&2; exit 2; }
RUNNER=$DW/build/gbarun
if [ ! -x "$RUNNER" ] || [ "$DW/test/gbarun.c" -nt "$RUNNER" ]; then
	${HOSTCC:-gcc} -O2 -o "$RUNNER" "$DW/test/gbarun.c" -lmgba || { echo "demo: cannot build the runner (libmgba-dev?)" >&2; exit 3; }
fi

sym() { awk -v s="$1" '$2 == s && $1 ~ /^0x/ { sub(/^0x0*/, "", $1); print $1; exit }' "$MAP"; }
RUN=$(sym run); GS=$(sym gs)
[ -n "$RUN" ] && [ -n "$GS" ] || { echo "demo: run/gs not in $MAP" >&2; exit 2; }

rm -rf "$OUT"
mkdir -p "$OUT/mgba"
SCRIPT=$OUT/lb_demo.txt
{ cat "$HERE/lb_demo.txt"; echo "dump $RUN 2316 end-run"; echo "dump $GS 5300 end-gs"; } > "$SCRIPT"

echo "demo: playing on mGBA"
"$RUNNER" "$ROM" "$SCRIPT" "$OUT/mgba/s" 2>&1 | tail -n 1
# gbarun writes PREFIX-NAME.*: drop the prefix, PPM -> PNG at 2x
python3 -I - "$OUT/mgba" <<'EOF'
import glob, os, sys
from PIL import Image
d = sys.argv[1]
for f in glob.glob(os.path.join(d, 's-*')):
    base = os.path.join(d, os.path.basename(f)[2:])
    if f.endswith('.ppm'):
        im = Image.open(f)
        im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(base[:-4] + '.png')
        os.remove(f)
    else:
        os.replace(f, base)
EOF
python3 -I "$HERE/check.py" "$OUT/mgba"
rc=$?

if [ $GPSP = 1 ]; then
	LAB=$TOP/tools/emu-lab
	if [ ! -d "$LAB" ]; then
		echo "demo: no tools/emu-lab, skipping gpSP" >&2
	else
		make -s -C "$LAB" all >/dev/null || exit 3
		G=$OUT/gpsp
		mkdir -p "$G/save"
		sed '0,/^wait 60$/s//wait 57/' "$SCRIPT" > "$G/lb_demo_gpsp.txt"
		echo "demo: playing on gpSP"
		"$LAB/lrun" --core "$LAB/cores/gpsp-onion_libretro.so" --rom "$ROM" --opts "$LAB/profiles/onion-gpsp.opt" \
			--bios-dir "$G/nobios" --save-dir "$G/save" --out "$G" --map "$MAP" --script "$G/lb_demo_gpsp.txt" > "$G/log.txt" 2>&1
		grc=$?
		sed -n 's/^RESULT: /demo: gpSP /p' "$G/log.txt"
		[ $grc -ne 0 ] && rc=1
		# lrun can't read VRAM: compare the end state only
		python3 -I - "$G" "$OUT/mgba" <<'EOF' || rc=1
import os, sys
g, m = sys.argv[1], sys.argv[2]
bad = 0
for f in ('end-run.bin', 'end-gs.bin'):
    a, b = os.path.join(g, f), os.path.join(m, f)
    if not os.path.exists(a) or open(a, 'rb').read() != open(b, 'rb').read():
        print('demo: gpSP %s differs from mGBA' % f)
        bad += 1
print('demo: gpSP end state %s mGBA' % ('differs from' if bad else 'matches'))
sys.exit(1 if bad else 0)
EOF
	fi
fi
[ $rc -eq 0 ] && echo "demo: PASS (screenshots: $OUT/mgba/*.png)" || echo "demo: FAIL"
exit $rc
