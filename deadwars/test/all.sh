#!/usr/bin/env bash
# Dead Wars: every check in one go (CI). Works from any directory.
#   1. clean build with zero compiler warnings (and the PERF build)
#   2. host unit tests of the tactical rules, the horde turn, the mission flow, the run layer and the
#      battery save (test/unit, ASan + UBSan)
#   3. storyc selftest + the storylet runtime fuzzers (test/story)
#   4. the classic balance simulator builds (test/sim)
#   5. smoke test (boot, classic mission 1)        test/smoke.sh
#   6. playtest regressions on mGBA and gpSP       test/playtest/run.sh (needs tools/emu-lab)
#   7. The Last Boat demo on mGBA and gpSP         test/demo/run.sh -g
# Exit 0 when everything passed; the failing steps are listed otherwise.
set -u -o pipefail
DW=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TOP=$(cd "$DW/.." && pwd)
fails=()
step() { echo "== $1"; }

step "build"
make -s -C "$DW" clean
warn=$(make -C "$DW" 2>&1 | grep -E "warning|error")
if [ -n "$warn" ] || [ ! -f "$DW/deadwars.gba" ]; then echo "$warn"; fails+=(build); fi
make -s -C "$DW" PERF=1 >/dev/null 2>&1 || fails+=(perf-build)

step "unit tests"
make -s -C "$DW/test/unit" run | tail -n 4 || fails+=(unit)

step "storylets"
log=$(make -s -C "$DW/test/story" run 2>&1) || fails+=(story)
echo "$log" | grep -E "selftest|story_fuzz:.*checks|FAIL|rror"

step "sim"
make -s -C "$DW/test/sim" >/dev/null || fails+=(sim)

step "smoke"
"$DW/test/smoke.sh" -n | tail -n 1 || fails+=(smoke)

step "playtest"
if [ -d "$TOP/tools/emu-lab" ]; then
	ROM=$DW/deadwars.gba "$DW/test/playtest/run.sh" | tail -n 1 || fails+=(playtest)
else
	echo "no tools/emu-lab: skipped"
fi

step "demo"
"$DW/test/demo/run.sh" -n -g | tail -n 4 || fails+=(demo)

if [ ${#fails[@]} -eq 0 ]; then
	echo "ALL PASS"
else
	echo "FAILED: ${fails[*]}"
	exit 1
fi
