# Producer status

**Mode:** token-saving. No workflows. Roles finish their current task, push, then wait for the producer.

## Integration (`claude/dreamy-fermi-yhrz7u`)
- Waiting on: Dead Wars hardening (verify + fix stages), team charter finalization.
- Merge queue, in order: hardening → `claude/dw-engine` (gpSP perf) → `claude/onion-lab` (AW2-style animation, emu lab) → `claude/deadwars-dev` (audio). Producer resolves conflicts, then posts **"hardening merged"** here and messages all roles.

## AW2 black/white screen: lab matrix on the owner's real ROM
`tools/emu-lab/matrix.sh` on AW2 (USA), no real BIOS available:
- OK: gpSP (Onion build 6250979), gpSP master, mGBA with Onion opts, zero-filled or short BIOS, force_gbp off, idle-loop off; foreign or truncated save states are rejected safely.
- WHITE screen: mGBA with a corrupt 16 KB BIOS. Segfault: gpSP with a corrupt BIOS.
- Conclusion: a bad `BIOS/gba_bios.bin` is the prime suspect. `docs/miyoo/ONION_GBA_FIX.md` updated.

## AW2 Remix
- Done: ROM research, balance module (`tools/remix/balance.py`, `docs/remix/BALANCE.md`), BPS encoder, map text format/export.
- In progress: `build_remix.py` (builder agent). Then the 5 Orange Star map remixes, done cheaply (single agent, not a workflow).
