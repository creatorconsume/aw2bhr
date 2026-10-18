#!/bin/sh
# Dead Wars playtest regression suite on Onion-like cores (QA).
# usage: deadwars/test/playtest/run.sh [TEST ...]     (default: every *.txt here)
# env:   ROM=path (default deadwars/deadwars.gba, built first)  CORES="mgba gpsp-onion"
#        OUT=dir (default deadwars/build/playtest)
# Each test is an lrun script (tools/emu-lab/lrun.c). Every test runs on each core with a fresh
# save dir; "# needs-sram: OTHER" starts from OTHER's .srm on the same core. A test fails on an
# "expect" failure, a crash, or a hang/black-screen report. Same-named screenshots of the first two
# cores are compared and differences are listed as notes. They are not failures, because gpSP starts
# the game 2 frames later than mGBA and Dead Wars seeds its RNG from frame_count (src/map.c), so
# battles can diverge. Set STRICT_CMP=1 to fail on them (useful for screens before any RNG use).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$HERE/../../.." && pwd)
LAB=$TOP/tools/emu-lab
DW=$TOP/deadwars
OUT=${OUT:-$DW/build/playtest}
CORES=${CORES:-"mgba gpsp-onion"}
if [ -z "${ROM:-}" ]; then
	make -s -C "$DW" >/dev/null || exit 1
	ROM=$DW/deadwars.gba
fi
make -s -C "$LAB" all >/dev/null || exit 1

[ $# -gt 0 ] || set -- "$HERE"/*.txt
fails=0
printf '%-36s %-11s %s\n' test core result
for t in "$@"; do
	name=$(basename "$t" .txt)
	need=$(sed -n 's/^# needs-sram: *//p' "$t")
	for core in $CORES; do
		opts=onion-gpsp
		[ "$core" = mgba ] && opts=onion-mgba
		d=$OUT/$name/$core
		rm -rf "$d"
		mkdir -p "$d/save"
		[ -n "$need" ] && cp "$OUT/$need/$core/save/"*.srm "$d/save/" 2>/dev/null
		"$LAB/lrun" --core "$LAB/cores/${core}_libretro.so" --rom "$ROM" --opts "$LAB/profiles/$opts.opt" \
			--bios-dir "$d/nobios" --save-dir "$d/save" --out "$d" --map "$DW/build/deadwars.map" \
			--script "$t" > "$d/log.txt" 2>&1
		rc=$?
		res=$(sed -n 's/^RESULT: //p' "$d/log.txt")
		[ $rc -gt 4 ] && res="CRASH (exit $rc)"
		[ -n "$res" ] || res="ERROR: $(tail -n 1 "$d/log.txt")"
		[ $rc -ne 0 ] && fails=$((fails + 1))
		printf '%-36s %-11s %s\n' "$name" "$core" "$res"
		grep -E '^(expect|until) .*(FAIL|is )|HANG|NO-RESPONSE' "$d/log.txt" | sed 's/^/    /'
	done
	# cross-core screenshot comparison
	set -- $CORES
	if [ $# -ge 2 ]; then
		diffs=$(python3 -I "$HERE/cmpshots.py" "$OUT/$name/$1" "$OUT/$name/$2")
		if [ -n "$diffs" ]; then
			[ "${STRICT_CMP:-0}" = 1 ] && fails=$((fails + 1))
			echo "$diffs" | sed "s/^/    note: $1 vs $2: /"
		fi
	fi
done
echo "outputs: $OUT"
[ $fails -eq 0 ] && echo "ALL PASS" || echo "$fails FAILED"
[ $fails -eq 0 ]
