#!/usr/bin/env python3
# Summarise PerfData dumps from test/perf_horde.txt: python3 test/perf_report.py OUTPREFIX
import struct, sys
p = sys.argv[1]
a = struct.unpack('<15I', open(p + '-after.bin', 'rb').read()[:60])
b = struct.unpack('<15I', open(p + '-before.bin', 'rb').read()[:60])
FRAME = 280896
for i, n in enumerate(['frame_work', 'find_paths', 'think', 'build_field']):
	cnt = a[1 + 3 * i] - b[1 + 3 * i]
	tot = a[2 + 3 * i] - b[2 + 3 * i]
	avg = tot // max(cnt, 1)
	print('%-12s n %5d  avg %8d (%.2f fr)  max %8d (%.2f fr)  total %.1f fr' %
	      (n, cnt, avg, avg / FRAME, a[3 + 3 * i], a[3 + 3 * i] / FRAME, tot / FRAME))
print('dropped frames during the horde turn:', a[13] - b[13])
print('worst frame during the horde turn: %d (%.2f fr)' % (a[14], a[14] / FRAME))
