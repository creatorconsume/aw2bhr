# Producer status

**Mode:** token-saving. No workflows. Roles finish their current task, push, then wait for the producer.

## Integration (`claude/dreamy-fermi-yhrz7u`)
- **HARDENING MERGED** (Oct 18). The integration branch now has: 15 verified engine fixes, simulator-tuned Part One, `claude/dw-engine` (perf + UI), `claude/onion-lab` (AW2 animations, emu lab), `claude/deadwars-dev` (audio), `claude/dw-art` (box art, Patient Zero art).
- Checks: zero build warnings, smoke.sh pass, playtest suite ALL PASS on mGBA and gpSP, sim builds.
- Everyone: merge integration before your next task. src/ ownership is now as in team/README.md.

## AW2 black/white screen: lab matrix on the owner's real ROM
`tools/emu-lab/matrix.sh` on AW2 (USA), no real BIOS available:
- OK: gpSP (Onion build 6250979), gpSP master, mGBA with Onion opts, zero-filled or short BIOS, force_gbp off, idle-loop off; foreign or truncated save states are rejected safely.
- WHITE screen: mGBA with a corrupt 16 KB BIOS. Segfault: gpSP with a corrupt BIOS.
- Conclusion: a bad `BIOS/gba_bios.bin` is the prime suspect. `docs/miyoo/ONION_GBA_FIX.md` updated.

## AW2 Remix
- Done: ROM research, balance module (`tools/remix/balance.py`, `docs/remix/BALANCE.md`), BPS encoder, map text format/export.
- In progress: `build_remix.py` (builder agent). Then the 5 Orange Star map remixes, done cheaply (single agent, not a workflow).
