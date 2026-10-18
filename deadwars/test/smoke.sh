#!/usr/bin/env bash
# Dead Wars smoke test: build, boot headless, reach the mission 1 map, check the screens.
#
# Usage: deadwars/test/smoke.sh [options]          (works from any directory)
#   -n, --no-build   skip `make -C deadwars` and test the existing deadwars/deadwars.gba
#   -r, --rom FILE   test FILE instead (implies --no-build). The RAM state check runs only
#                    when FILE is deadwars/deadwars.gba, because it needs the matching ELF.
#   -o, --out DIR    where screenshots and logs go (default: deadwars/build/smoke, git-ignored)
#   -h, --help       show this help
# Environment: PREFIX (toolchain prefix, default arm-none-eabi-), HOSTCC (default gcc).
#
# Steps:
#   1. make -C deadwars. Errors fail the run. Compiler warnings are listed and counted but do
#      not fail it (make only recompiles what changed, so a no-op build shows none).
#   2. Builds the headless runner deadwars/build/gbarun from deadwars/test/gbarun.c (libmgba)
#      when it is missing or older than its source.
#   3. Plays a scripted run on fresh SRAM: boot -> title (shot "title") -> START -> Campaign ->
#      mission 1 -> START skips the intro dialogue -> A skips the mission banner -> map (shot "map").
#      Extra shots for bug reports: "menu" and "intro". Script format: header of test/gbarun.c.
#   4. Checks with python3 -I + Pillow that the title and map shots are 240x160, not blank or
#      uniform (enough colours, no single colour covering nearly the whole screen, some contrast),
#      and clearly differ from each other. Then, when the ELF is available, it reads `spr` and `gs`
#      from RAM and checks the game is in the survivors' turn of mission 1, Day 1, not over:
#        spr (video.h SpriteState): +0 cursor_show == 1, +1 map_visible == 1, +2 pointer_show == 0
#        gs  (game.h GameState):    +4 mission == 0, +7 day == 1, +8 side == 0, +11 over == 0
#      Every shot is also saved as PNG next to the PPM (OUT/shot-<name>.png) for bug reports.
#
# Exit codes:
#   0  pass
#   1  a check failed (blank, uniform or identical screens, wrong game state, runner crashed/hung)
#   2  usage error or missing prerequisite (arm-none-eabi-gcc, libmgba-dev, python3 + Pillow, ROM)
#   3  build failed (make -C deadwars, or compiling the runner)
#
# Prerequisites (Debian/Ubuntu): gcc-arm-none-eabi libnewlib-arm-none-eabi python3-pil libmgba-dev
# Owner: QA & Platform. If the boot flow changes (splash screen, title menu order, mission 1
# intro), or SpriteState/GameState fields move, update SCRIPT and the offsets above.
set -u

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
DW=$ROOT/deadwars
PREFIX=${PREFIX:-arm-none-eabi-}
HOSTCC=${HOSTCC:-gcc}
DEFAULT_ROM=$DW/deadwars.gba
ELF=$DW/build/deadwars.elf
RUNNER=$DW/build/gbarun
RUNNER_SRC=$DW/test/gbarun.c

BUILD=1
ROM=$DEFAULT_ROM
OUT=$DW/build/smoke

say()  { echo "smoke: $*"; }
die()  { local code=$1; shift; echo "smoke: FAIL: $*" >&2; exit "$code"; }
usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "${BASH_SOURCE[0]}"; }

while [ $# -gt 0 ]; do
	case "$1" in
	-n|--no-build) BUILD=0 ;;
	-r|--rom)
		[ $# -ge 2 ] || { usage >&2; die 2 "--rom needs a file"; }
		ROM=$2; BUILD=0; shift ;;
	-o|--out)
		[ $# -ge 2 ] || { usage >&2; die 2 "--out needs a directory"; }
		OUT=$2; shift ;;
	-h|--help) usage; exit 0 ;;
	*) usage >&2; die 2 "unknown option: $1" ;;
	esac
	shift
done

# ---- prerequisites
command -v python3 >/dev/null 2>&1 || die 2 "python3 not found"
python3 -I -c 'import PIL.Image, PIL.ImageChops, PIL.ImageStat' 2>/dev/null ||
	die 2 "Pillow not found for python3 (sudo apt install python3-pil)"
command -v "$HOSTCC" >/dev/null 2>&1 || die 2 "host compiler '$HOSTCC' not found"
if [ "$BUILD" = 1 ]; then
	command -v "${PREFIX}gcc" >/dev/null 2>&1 ||
		die 2 "${PREFIX}gcc not found (sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi)"
fi
mkdir -p "$OUT" || die 2 "cannot create $OUT"
OUT=$(cd "$OUT" && pwd)

# ---- 1. build the ROM
if [ "$BUILD" = 1 ]; then
	say "make -C deadwars"
	if ! make -C "$DW" >"$OUT/make.log" 2>&1; then
		tail -n 40 "$OUT/make.log" >&2
		die 3 "make -C deadwars failed (full log: $OUT/make.log)"
	fi
	nwarn=$(grep -c 'warning:' "$OUT/make.log")
	if [ "$nwarn" -gt 0 ]; then
		grep 'warning:' "$OUT/make.log" >&2
		say "note: $nwarn compiler warning(s) in this build (definition of done: zero in your files)"
	fi
fi
[ -f "$ROM" ] || die 2 "ROM not found: $ROM"

# ---- 2. build the headless runner
if [ ! -x "$RUNNER" ] || [ "$RUNNER_SRC" -nt "$RUNNER" ]; then
	say "building $RUNNER"
	mkdir -p "$(dirname "$RUNNER")"
	if ! "$HOSTCC" -O2 -o "$RUNNER" "$RUNNER_SRC" -lmgba >"$OUT/gbarun-build.log" 2>&1; then
		cat "$OUT/gbarun-build.log" >&2
		if grep -qE 'mgba/core/core\.h|cannot find -lmgba' "$OUT/gbarun-build.log"; then
			die 2 "libmgba not found (sudo apt install libmgba-dev)"
		fi
		die 3 "compiling $RUNNER_SRC failed"
	fi
fi

# ---- 3. scripted run
STATE=0
SPR_ADDR=""
GS_ADDR=""
if [ "$ROM" -ef "$DEFAULT_ROM" ] && [ -f "$ELF" ] && command -v "${PREFIX}nm" >/dev/null 2>&1; then
	SPR_ADDR=$("${PREFIX}nm" "$ELF" | awk '$3 == "spr" { print $1; exit }')
	GS_ADDR=$("${PREFIX}nm" "$ELF" | awk '$3 == "gs" { print $1; exit }')
	if [ -n "$SPR_ADDR" ] && [ -n "$GS_ADDR" ]; then
		STATE=1
	else
		say "note: symbols spr/gs not found in $ELF; skipping the RAM state check"
	fi
else
	say "note: RAM state check skipped (it needs the built deadwars.gba, its ELF and ${PREFIX}nm)"
fi

SCRIPT=$OUT/smoke-script.txt
cat >"$SCRIPT" <<'EOF'
# generated by deadwars/test/smoke.sh
# boot: the title screen appears right after power-on
wait 30
shot title
# START -> title menu (Campaign is preselected on fresh SRAM)
press T 3
wait 10
shot menu
# Campaign -> mission select (only mission 1 is unlocked)
press A 3
wait 10
# mission 1 -> intro dialogue; wait until the first line has finished typing
press A 3
wait 200
shot intro
# START skips the whole intro scene, then A skips the mission banner
press T 3
wait 20
press A 3
wait 40
shot map
EOF
if [ "$STATE" = 1 ]; then
	echo "dump $SPR_ADDR 4 spr" >>"$SCRIPT"
	echo "dump $GS_ADDR 12 gs" >>"$SCRIPT"
fi

rm -f "$OUT"/shot-*.ppm "$OUT"/shot-*.png "$OUT"/shot-*.bin
say "running $(basename "$ROM") headless"
TIMEOUT=()
command -v timeout >/dev/null 2>&1 && TIMEOUT=(timeout 120)
${TIMEOUT[@]+"${TIMEOUT[@]}"} "$RUNNER" "$ROM" "$SCRIPT" "$OUT/shot" >"$OUT/gbarun.log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
	cat "$OUT/gbarun.log" >&2
	[ "$rc" -eq 124 ] && die 1 "the runner hung (killed after 120 s)"
	die 1 "the runner exited with status $rc"
fi

# ---- 4. checks
python3 -I -u - "$OUT/shot" "$STATE" <<'PY'
import sys
from PIL import Image, ImageChops, ImageStat

prefix, state = sys.argv[1], sys.argv[2] == "1"
W, H = 240, 160
MIN_COLOURS = 6      # distinct RGB colours on screen
MAX_DOMINANT = 0.90  # largest share of the screen a single colour may cover
MIN_CONTRAST = 6.0   # standard deviation of luminance (0..255)
MIN_DIFF = 0.25      # share of pixels that must differ between the title and the map (title vs title menu ~8%)
fails = []

def load(name):
    path = f"{prefix}-{name}.ppm"
    try:
        im = Image.open(path).convert("RGB")
    except Exception as e:
        fails.append(f"{name}: no screenshot ({e})")
        return None
    im.save(f"{prefix}-{name}.png")
    return im

def screen_ok(name, im):
    if im is None:
        return
    if im.size != (W, H):
        fails.append(f"{name}: size {im.size[0]}x{im.size[1]}, expected {W}x{H}")
        return
    cols = im.getcolors(W * H)
    top = max(c for c, _ in cols) / (W * H)
    contrast = ImageStat.Stat(im.convert("L")).stddev[0]
    print(f"smoke: {name}: {len(cols)} colours, top colour {top:.0%}, contrast {contrast:.1f}")
    if len(cols) < MIN_COLOURS:
        fails.append(f"{name}: only {len(cols)} colours (blank screen?)")
    if top > MAX_DOMINANT:
        fails.append(f"{name}: one colour covers {top:.0%} of the screen (blank screen?)")
    if contrast < MIN_CONTRAST:
        fails.append(f"{name}: almost no contrast ({contrast:.1f})")

shots = {n: load(n) for n in ("title", "menu", "intro", "map")}
for n in ("title", "map"):
    screen_ok(n, shots[n])

t, m = shots["title"], shots["map"]
if t is not None and m is not None and t.size == m.size == (W, H):
    hist = ImageChops.difference(t, m).convert("L").histogram()
    diff = sum(hist[25:]) / (W * H)
    print(f"smoke: title vs map: {diff:.0%} of pixels differ")
    if diff < MIN_DIFF:
        fails.append(f"title and map screens are nearly identical ({diff:.1%} differ): stuck on the title?")

if state:
    try:
        spr = open(f"{prefix}-spr.bin", "rb").read()
        gs = open(f"{prefix}-gs.bin", "rb").read()
        checks = [
            ("spr.cursor_show", spr[0], 1, "map cursor hidden: not in the survivors' turn (dialogue/banner/menu?)"),
            ("spr.map_visible", spr[1], 1, "the map is not shown"),
            ("spr.pointer_show", spr[2], 0, "a menu is open"),
            ("gs.mission", gs[4], 0, "wrong mission"),
            ("gs.day", gs[7], 1, "wrong day"),
            ("gs.side", gs[8], 0, "not the survivors' turn"),
            ("gs.over", gs[11], 0, "the mission already ended"),
        ]
        for field, got, want, why in checks:
            if got != want:
                fails.append(f"{field} = {got}, expected {want}: {why}")
        if not any(f.startswith(("spr.", "gs.")) for f in fails):
            print("smoke: game state: mission 1, Day 1, survivors' turn, cursor on the map")
    except Exception as e:
        fails.append(f"RAM state check: {e}")

for f in fails:
    print(f"smoke: FAIL: {f}", file=sys.stderr)
sys.exit(1 if fails else 0)
PY
rc=$?
if [ "$rc" -ne 0 ]; then
	echo "smoke: screenshots: $OUT/shot-*.png" >&2
	exit 1
fi
say "PASS (screenshots: $OUT/shot-*.png)"
exit 0
