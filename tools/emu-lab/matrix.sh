#!/bin/sh
# Onion OS GBA test matrix: run one ROM through mGBA and gpSP the way Onion configures them,
# plus the black/white-screen suspects (bad BIOS, stale auto state, core options).
#
# usage: tools/emu-lab/matrix.sh ROM [BIOS]
#   ROM   the .gba to test (never committed)
#   BIOS  optional real gba_bios.bin (md5 a860e8c0b6d573d191e4ec7db1b1e4f6)
# env:   SCRIPT=file   lrun script (default scripts/boot.txt)
#        MOD_ROM=file  a modified build of ROM: also load ROM's states into it (stale-state case)
#        OUT=dir       results (default tools/emu-lab/out/matrix/<rom name>)
# Prints a summary table and writes OUT/summary.txt, OUT/<case>/*.png and OUT/sheet.png.
set -u
LAB=$(cd "$(dirname "$0")" && pwd)
ROM=$(realpath "${1:?usage: matrix.sh ROM [BIOS]}")
BIOS=${2:+$(realpath "$2")}
SCRIPT=$(realpath "${SCRIPT:-$LAB/scripts/boot.txt}")
NAME=$(basename "${ROM%.*}")
OUT=${OUT:-$LAB/out/matrix/$NAME}
LRUN=$LAB/lrun
CORES=$LAB/cores
P=$LAB/profiles

[ -x "$LRUN" ] && [ -e "$CORES/gpsp-onion_libretro.so" ] || make -s -C "$LAB" all || exit 1
rm -rf "$OUT"
mkdir -p "$OUT/bios" "$OUT/opts"
: > "$OUT/summary.txt"

# --- BIOS variants (each in its own system dir as gba_bios.bin) ---
mkdir -p "$OUT/bios/none" "$OUT/bios/zeros" "$OUT/bios/garbage" "$OUT/bios/short"
head -c 16384 /dev/zero > "$OUT/bios/zeros/gba_bios.bin"
# junk behind a vector table of 7 ARM branches: passes mGBA's GBAIsBIOS() and gpSP's bios_rom[0] == 0x18,
# like a corrupt or wrong-version dump would
{ printf '\030\000\000\352\004\000\000\352\114\000\000\352\002\000\000\352\001\000\000\352\000\000\000\352\102\000\000\352'
  LC_ALL=C awk 'BEGIN { srand(1); for (i = 0; i < 16356; i++) printf "%c", int(rand() * 256) }'
} > "$OUT/bios/garbage/gba_bios.bin"
# wrong size, e.g. an HTML error page saved as gba_bios.bin
head -c 4096 /dev/urandom > "$OUT/bios/short/gba_bios.bin"
if [ -n "$BIOS" ]; then
	mkdir -p "$OUT/bios/real"
	cp "$BIOS" "$OUT/bios/real/gba_bios.bin"
	echo "BIOS md5: $(md5sum < "$BIOS" | cut -d' ' -f1) (official a860e8c0b6d573d191e4ec7db1b1e4f6)" | tee -a "$OUT/summary.txt"
fi

# opts variant: NAME BASE KEY VALUE [KEY VALUE ...]
variant() {
	name=$1 base=$2
	shift 2
	cp "$P/$base.opt" "$OUT/opts/$name.opt"
	while [ $# -ge 2 ]; do
		sed -i "s/^$1 = .*/$1 = \"$2\"/" "$OUT/opts/$name.opt"
		grep -q "^$1 = " "$OUT/opts/$name.opt" || echo "$1 = \"$2\"" >> "$OUT/opts/$name.opt"
		shift 2
	done
}
cp "$P/onion-mgba.opt" "$P/onion-gpsp.opt" "$OUT/opts/"
variant mgba-nogbp onion-mgba mgba_force_gbp OFF
variant mgba-noidle onion-mgba mgba_idle_optimization "Don't Remove"
variant mgba-nobios onion-mgba mgba_use_bios OFF
variant mgba-skipbios onion-mgba mgba_skip_bios ON

# run CASE CORE OPTS BIOSDIR [ROM] [lrun args...]
run() {
	case=$1 core=$2 opts=$3 bios=$4
	shift 4
	rom=$ROM
	if [ $# -gt 0 ] && [ -f "$1" ]; then rom=$1; shift; fi
	d=$OUT/$case
	mkdir -p "$d"
	"$LRUN" --core "$CORES/${core}_libretro.so" --rom "$rom" --opts "$OUT/opts/$opts.opt" \
		--bios-dir "$OUT/bios/$bios" --save-dir "$d" --out "$d" --script "$SCRIPT" "$@" > "$d/log.txt" 2>&1
	shots=$(awk '/^shot/ {printf "%s=%s ", $2, $5}' "$d/log.txt")
	res=$(sed -n 's/^RESULT: //p' "$d/log.txt")
	[ -n "$res" ] || res="CRASH/ERROR: $(tail -n 1 "$d/log.txt")"
	notes=$(grep -E '^\[(core|osd)\]|^autostate|^loadstate.*FAILED|HANG|NO-RESPONSE' "$d/log.txt" | awk '!seen[$0]++' | head -n 3 |
		sed 's|/[^ ]*/||g' | tr '\n' ';')
	printf '%-18s %-11s %-13s %-8s %s| %s %s\n' "$case" "$core" "$opts" "$bios" "$shots" "$res" "$notes" |
		tee -a "$OUT/summary.txt"
}

echo "ROM: $ROM ($(md5sum < "$ROM" | cut -d' ' -f1))  script: $SCRIPT" | tee -a "$OUT/summary.txt"
echo "case               core        opts          bios     shots | result notes" | tee -a "$OUT/summary.txt"
# Onion 4.4 default (gpSP) and 4.3 default (mGBA), exactly as shipped
run gpsp-onion      gpsp-onion onion-gpsp   none
run mgba-onion      mgba       onion-mgba   none
run gpsp-master     gpsp       onion-gpsp   none
# BIOS suspects
if [ -n "$BIOS" ]; then
	run mgba-realbios   mgba       onion-mgba   real
	run mgba-realbios-skip mgba    mgba-skipbios real
	run gpsp-realbios   gpsp-onion onion-gpsp   real
fi
run mgba-zerobios   mgba       onion-mgba   zeros
run mgba-junkbios   mgba       onion-mgba   garbage
run mgba-shortbios  mgba       onion-mgba   short
run mgba-junk-off   mgba       mgba-nobios  garbage
run gpsp-junkbios   gpsp-onion onion-gpsp   garbage
run gpsp-zerobios   gpsp-onion onion-gpsp   zeros
# core options
run mgba-nogbp      mgba       mgba-nogbp   none
run mgba-noidle     mgba       mgba-noidle  none
# stale / foreign auto save states (Onion loads <rom>.state.auto before the first frame)
run state-gpsp2mgba mgba       onion-mgba   none --autostate "$OUT/gpsp-onion/run.state"
run state-mgba2gpsp gpsp-onion onion-gpsp   none --autostate "$OUT/mgba-onion/run.state"
head -c "$(($(stat -c %s "$OUT/mgba-onion/run.state") / 2))" "$OUT/mgba-onion/run.state" > "$OUT/trunc.state"
run state-trunc-mgba mgba      onion-mgba   none --autostate "$OUT/trunc.state"
run state-self-mgba mgba       onion-mgba   none --autostate "$OUT/mgba-onion/run.state"
run state-self-gpsp gpsp-onion onion-gpsp   none --autostate "$OUT/gpsp-onion/run.state"
if [ -n "${MOD_ROM:-}" ]; then
	MOD=$(realpath "$MOD_ROM")
	run mod-mgba        mgba       onion-mgba   none "$MOD"
	run mod-gpsp        gpsp-onion onion-gpsp   none "$MOD"
	run mod-mgba-noidle mgba       mgba-noidle  none "$MOD"
	run mod-oldstate-mgba mgba     onion-mgba   none "$MOD" --autostate "$OUT/mgba-onion/run.state"
	run mod-oldstate-gpsp gpsp-onion onion-gpsp none "$MOD" --autostate "$OUT/gpsp-onion/run.state"
fi

python3 -I "$LAB/sheet.py" "$OUT" 2>/dev/null && echo "contact sheet: $OUT/sheet.png"
echo "summary: $OUT/summary.txt"
