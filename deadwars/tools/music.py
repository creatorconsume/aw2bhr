#!/usr/bin/env python3
"""Dead Wars music compiler: audio/music.txt (MML-style text) -> build/gen/music.{c,h}

File format (one statement per line, '#' starts a comment):

  wave NAME HEX32                 32 4-bit samples, one period (first sample = first digit)
  inst NAME sq   duty=0..3 env=VOL,DIR,STEP [gate=F] [vib=DEPTH,SPEED] [sweep=0xNN]
  inst NAME wave wave=NAME vol=0|25|50|75|100 [gate=F] [vib=DEPTH,SPEED]
  drum C   env=VOL,DIR,STEP noise=0xNN [gate=F]   (C = one letter used on the noise channel)
  song NAME speed=F               F = frames per 16th note
  sq1: / sq2: / wave: / noise:    channel text, may span several lines (they append)

Envelope: VOL 0-15, DIR 0 = fade out / 1 = fade in, STEP 0 = constant, 1-7 = 1/64s per step.
gate = frames before the note is cut (staccato), 0 = hold for the full length.

Channel text (case sensitive):
  c d e f g a b   notes, followed by + or # (sharp) or - (flat), then an optional length
  r               rest
  LENGTH          1 2 4 8 16 32 (whole..32nd), '.' dots it; ^LEN ties more length on
  oN > <          set octave / up / down (squares: o2..o7, wave: o1..o6)
  lN              default length
  @NAME           instrument (not on the noise channel, where each letter is a drum)
  vN              volume override 0-15 for following notes (v- clears it)
  [ ... ]N        repeat N times (nests)
  L               loop point: playback jumps here at the end. All channels of a song
                  must reach L at the same time and have the same length (seamless loops).
                  A song without L plays once and stops (jingles).
  |               bar line, ignored
"""
import os
import re
import sys

EV_REST, EV_TIE, EV_INST, EV_VOL, EV_LOOP, EV_END = 0xF0, 0xF1, 0xF2, 0xF3, 0xFE, 0xFF
CHANNELS = ["sq1", "sq2", "wave", "noise"]
NOTE_BASE = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
LOWEST_OCT = 1          # note index 0 = C1
NOTE_COUNT = 7 * 12     # C1..B7
WAVE_VOL = {0: 0x00, 25: 0x60, 50: 0x40, 75: 0x80, 100: 0x20}  # SOUND3CNT_H >> 8


def fail(msg, line=None):
	sys.exit("music.py: %s%s" % ("line %d: " % line if line else "", msg))


def parse_env(s):
	v, d, st = (int(x, 0) for x in s.split(","))
	assert 0 <= v <= 15 and d in (0, 1) and 0 <= st <= 7
	return (v << 4) | (d << 3) | st


def kv(parts):
	out = {}
	for p in parts:
		if "=" not in p:
			fail("expected key=value, got %r" % p)
		k, v = p.split("=", 1)
		out[k] = v
	return out


class Song:
	def __init__(self, name, speed):
		self.name, self.speed = name, speed
		self.text = {c: "" for c in CHANNELS}


TOKEN = r"[a-km-nps-uw-z][+#-]?\d*\.*(?:\^\d+\.*)*|r\d*\.*(?:\^\d+\.*)*|o\d|l\d+\.*|v-|v\d+|@\w+|\[|\]\d*|[<>L|]"


def tokenize(text):
	"""-> (tokens, leftover characters that are not tokens)"""
	return re.findall(TOKEN, text), re.sub(TOKEN + r"|\s", "", text)


def expand_repeats(toks):
	out, stack = [], [[]]
	for t in toks:
		if t == "[":
			stack.append([])
		elif t.startswith("]"):
			body = stack.pop()
			stack[-1].extend(body * int(t[1:] or 2))
		else:
			stack[-1].append(t)
	if len(stack) != 1:
		fail("unbalanced [ ]")
	return stack[0]


def length16(spec, default):
	"""'4.' -> sixteenths (may be fractional for 32nds)."""
	if not spec:
		return default
	m = re.fullmatch(r"(\d+)(\.*)", spec)
	n = int(m.group(1))
	if n not in (1, 2, 4, 8, 16, 32):
		fail("bad length %s" % spec)
	base = 16 / n
	total, add = base, base
	for _ in m.group(2):
		add /= 2
		total += add
	return total


def compile_channel(song, ch, insts, drums):
	toks, junk = tokenize(song.text[ch])
	if junk:
		fail("song %s %s: can't parse %r" % (song.name, ch, junk[:20]))
	toks = expand_repeats(toks)
	out = []
	octave, deflen = 4, 4.0
	t16 = 0.0           # position in sixteenths
	loop_at = None
	cur_inst = None

	def emit_len(code, sixteenths, extra=()):
		frames = sixteenths * song.speed
		if abs(frames - round(frames)) > 1e-6:
			fail("song %s %s: length not a whole number of frames" % (song.name, ch))
		frames = int(round(frames))
		first = True
		while frames > 0:
			f = min(frames, 255)
			if first:
				out.extend(extra)
				out.extend([code, f])
			else:
				out.extend([EV_TIE, f])
			first = False
			frames -= f

	for t in toks:
		if t == "|":
			continue
		if t == "L":
			loop_at = t16
			out.append(EV_LOOP)
		elif t == ">":
			octave += 1
		elif t == "<":
			octave -= 1
		elif t[0] == "o":
			octave = int(t[1:])
		elif t[0] == "l":
			deflen = length16(t[1:], deflen)
		elif t == "v-":
			out.extend([EV_VOL, 0xFF])
		elif t[0] == "v":
			out.extend([EV_VOL, int(t[1:])])
		elif t[0] == "@":
			if ch == "noise":
				fail("song %s: no instruments on the noise channel" % song.name)
			if t[1:] not in insts:
				fail("unknown instrument %s" % t)
			idx, kind = insts[t[1:]]
			if (kind == "wave") != (ch == "wave"):
				fail("song %s %s: instrument %s is the wrong kind" % (song.name, ch, t))
			cur_inst = idx
			out.extend([EV_INST, idx])
		else:
			m = re.fullmatch(r"([a-z])([+#-]?)(\d*\.*)((?:\^\d+\.*)*)", t)
			name, acc, ln, ties = m.groups()
			n16 = length16(ln, deflen)
			for tie in re.findall(r"\^(\d+\.*)", ties):
				n16 += length16(tie, deflen)
			if name == "r":
				emit_len(EV_REST, n16)
			elif ch == "noise":
				if name not in drums:
					fail("song %s: unknown drum %s" % (song.name, name))
				emit_len(drums[name], n16)
			else:
				if name not in NOTE_BASE:
					fail("song %s %s: %s is not a note" % (song.name, ch, t))
				if cur_inst is None:
					fail("song %s %s: note before any @instrument" % (song.name, ch))
				semi = NOTE_BASE[name] + {"": 0, "+": 1, "#": 1, "-": -1}[acc]
				idx = (octave - LOWEST_OCT) * 12 + semi
				lo = 0 if ch == "wave" else 12
				hi = NOTE_COUNT - (12 if ch == "wave" else 0)
				if not lo <= idx < hi:
					fail("song %s %s: note %s out of range (octave %d)" % (song.name, ch, t, octave))
				emit_len(idx, n16)
			t16 += n16
	out.append(EV_END)
	return out, t16, loop_at


def main():
	root = sys.argv[1] if len(sys.argv) > 1 else "."
	src = os.path.join(root, "audio", "music.txt")
	gen = os.path.join(root, "build", "gen")
	os.makedirs(gen, exist_ok=True)

	waves, insts, inst_rows, drums, songs = {}, {}, [], {}, []
	song = None
	cur_ch = None
	for ln, raw in enumerate(open(src), 1):
		line = raw.split("#", 1)[0].strip()
		if not line:
			continue
		m = re.match(r"(sq1|sq2|wave|noise):(.*)", line)
		if m:
			if not song:
				fail("channel text outside a song", ln)
			cur_ch = m.group(1)
			song.text[cur_ch] += " " + m.group(2)
			continue
		parts = line.split()
		if parts[0] == "wave" and len(parts) == 3:
			h = parts[2]
			if not re.fullmatch(r"[0-9A-Fa-f]{32}", h):
				fail("wave needs 32 hex digits", ln)
			waves[parts[1]] = (len(waves), bytes.fromhex(h))
		elif parts[0] == "inst":
			name, kind, opts = parts[1], parts[2], kv(parts[3:])
			gate = int(opts.get("gate", "0"))
			vd, vs = (int(x) for x in opts.get("vib", "0,0").split(","))
			if kind == "sq":
				row = (0, int(opts["duty"]) << 6, parse_env(opts["env"]), gate, vd, vs, int(opts.get("sweep", "0"), 0))
			elif kind == "wave":
				row = (1, waves[opts["wave"]][0], WAVE_VOL[int(opts["vol"])], gate, vd, vs, 0)
			else:
				fail("inst kind must be sq or wave", ln)
			insts[name] = (len(inst_rows), kind)
			inst_rows.append((name, row))
		elif parts[0] == "drum":
			name, opts = parts[1], kv(parts[2:])
			if len(name) != 1 or not name.islower() or name in "rlov":
				fail("drum names are single letters (not r l o v)", ln)
			drums[name] = len(inst_rows)
			insts["drum_" + name] = (len(inst_rows), "noise")
			inst_rows.append(("drum " + name, (2, int(opts["noise"], 0), parse_env(opts["env"]), int(opts.get("gate", "0")), 0, 0, 0)))
		elif parts[0] == "song":
			song = Song(parts[1], int(kv(parts[2:])["speed"]))
			songs.append(song)
		else:
			fail("unknown statement %r" % parts[0], ln)
	if len(inst_rows) > 0xF0:
		fail("too many instruments")

	c_lines = ['// generated by tools/music.py from audio/music.txt - do not edit', '#include "music.h"', ""]
	c_lines.append("const MusInst mus_inst[%d] = {" % len(inst_rows))
	for name, r in inst_rows:
		c_lines.append("\t{ %d, 0x%02X, 0x%02X, %d, %d, %d, 0x%02X }, // %s" % (r + (name,)))
	c_lines.append("};\n")
	c_lines.append("const u8 mus_waves[%d][16] = {" % max(1, len(waves)))
	for name, (i, data) in sorted(waves.items(), key=lambda x: x[1][0]):
		c_lines.append("\t{ %s }, // %s" % (", ".join("0x%02X" % b for b in data), name))
	c_lines.append("};\n")
	# square channel frequency register per note (C1..B7); wave channel plays an octave lower
	freqs = []
	for i in range(NOTE_COUNT):
		hz = 440.0 * 2 ** ((i + 12 * LOWEST_OCT - 57) / 12)  # A4 = index 57 from C0
		freqs.append(max(0, min(2047, round(2048 - 131072 / hz))))
	c_lines.append("const u16 mus_freq[%d] = {" % NOTE_COUNT)
	for i in range(0, NOTE_COUNT, 12):
		c_lines.append("\t" + ", ".join("%4d" % f for f in freqs[i:i + 12]) + ",")
	c_lines.append("};\n")

	report = []
	for s in songs:
		lens, loops = [], []
		for ch in CHANNELS:
			data, t16, loop_at = compile_channel(s, ch, insts, drums)
			lens.append(t16)
			loops.append(loop_at)
			c_lines.append("static const u8 song_%s_%s[] = { %s };" % (s.name, ch, ", ".join(str(b) for b in data)))
		if len(set(lens)) != 1:
			fail("song %s: channel lengths differ (sixteenths): %s" % (s.name, dict(zip(CHANNELS, lens))))
		if len(set(loops)) != 1:
			fail("song %s: loop points differ: %s" % (s.name, dict(zip(CHANNELS, loops))))
		report.append("%s: %g bars%s, %.1fs" % (s.name, lens[0] / 16, " (loops at bar %g)" % (loops[0] / 16 + 1) if loops[0] is not None else "", lens[0] * s.speed / 60))
	c_lines.append("")
	c_lines.append("const u8 *const mus_songs[MUS_COUNT][4] = {")
	for s in songs:
		c_lines.append("\t{ %s }," % ", ".join("song_%s_%s" % (s.name, ch) for ch in CHANNELS))
	c_lines.append("};")
	c_lines.append("const char *const mus_names[MUS_COUNT] = { %s };" % ", ".join('"%s"' % s.name.capitalize() for s in songs))

	h = ["// generated by tools/music.py from audio/music.txt - do not edit",
	     "#ifndef MUSIC_H", "#define MUSIC_H", '#include "gba.h"', "",
	     "// kind: 0 square, 1 wave, 2 noise drum. param: square duty bits (NRx1), wave index, noise NR43",
	     "// env: NRx2 envelope byte (wave: SOUND3CNT_H volume bits >> 8)",
	     "typedef struct { u8 kind, param, env, gate, vib_depth, vib_speed, sweep; } MusInst;",
	     "enum { MEV_REST = 0x%02X, MEV_TIE, MEV_INST, MEV_VOL, MEV_LOOP = 0x%02X, MEV_END };" % (EV_REST, EV_LOOP),
	     "enum { %s, MUS_COUNT };" % ", ".join("MUS_" + s.name.upper() for s in songs),
	     "#define MUS_NOTE_COUNT %d" % NOTE_COUNT,
	     "extern const MusInst mus_inst[];", "extern const u8 mus_waves[][16];",
	     "extern const u16 mus_freq[MUS_NOTE_COUNT];", "extern const u8 *const mus_songs[MUS_COUNT][4];",
	     "extern const char *const mus_names[MUS_COUNT];",
	     "", "#endif"]
	for path, lines in ((os.path.join(gen, "music.c"), c_lines), (os.path.join(gen, "music.h"), h)):
		new = "\n".join(lines) + "\n"
		if not os.path.exists(path) or open(path).read() != new:
			open(path, "w").write(new)
	print("music.py: " + "; ".join(report))


if __name__ == "__main__":
	main()
