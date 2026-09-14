#!/usr/bin/env python3
"""Re-time an RLCV band without re-encoding it.

    tools/rlcv_repack.py in.bin out.bin 2 200     # 10 Hz -> 5 Hz, same length

Takes every Nth frame and rewrites the frame interval. JPEG payloads are
copied byte for byte, so this is lossless and instant.

WHY THIS EXISTS RATHER THAN JUST PASSING --fps 5 TO mkvideoband.py. The two do
NOT produce the same file. Tracking runs over the frames the tool selects, and
its guards are per-frame: --track-step bounds travel per frame and
--track-alpha smooths per frame. Sample at 5 Hz instead of 10 and the subject
moves twice as far between frames, so the step clamp bites harder and the
crop path comes out different — a re-tune, not a re-time.

The shipped band is built at 10 Hz, where the tracking was tuned and checked,
and then re-timed here. That keeps the motion path identical to the one that
was reviewed, and means the frame rate can be changed without touching the
framing.
"""
import struct, sys

src, dst, step, interval = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
b = open(src, "rb").read()
magic, fmt, n, W, H, old_iv, _ = struct.unpack("<4sHHHHHH", b[:16])
assert magic == b"RLCV" and fmt == 1

keep = list(range(0, n, step))
blobs = []
for i in keep:
    off, ln = struct.unpack("<II", b[16 + i*8 : 16 + i*8 + 8])
    blobs.append(b[off:off+ln])

m = len(blobs)
head = struct.pack("<4sHHHHHH", b"RLCV", 1, m, W, H, interval, 0)
off, table = 16 + m*8, b""
for x in blobs:
    table += struct.pack("<II", off, len(x))
    off += len(x)
out = head + table + b"".join(blobs)
open(dst, "wb").write(out)
print(f"{dst}: {n}@{old_iv}ms -> {m}@{interval}ms "
      f"({m*interval/1000:.1f} s), {len(out):,} B "
      f"({100*len(out)/(2*1024*1024):.1f}% of partition)")
