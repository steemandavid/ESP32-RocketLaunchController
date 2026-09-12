#!/usr/bin/env python3
"""Build an RLCV splash video band for the RLC remote.

Turns any video the host can decode into the 480x80 JPEG frame sequence the
remote's boot splash plays from its `splash` partition.

    tools/mkvideoband.py boosters.mp4 -o splash.bin
    ./build_remote.sh splash splash.bin

The grading is not optional decoration. The band sits behind white title text
on the boot screen of a launch controller, and the text has to win: every
frame is desaturated, darkened and then hard-capped so no channel exceeds
--cap (default 0x9A), which is the same bound the firmware documents. Footage
that looks good on a monitor will scream on this screen without it.

Needs PyAV or OpenCV for decoding (either is enough) and Pillow for the rest.
"""

import argparse
import struct
import sys

try:
    from PIL import Image, ImageEnhance
except ImportError:
    sys.exit("needs Pillow:  pip install Pillow")

MAGIC = b"RLCV"
FORMAT = 1
HDR = 16

BAND_W = 480
BAND_H = 80


def frames_pyav(path):
    import av
    with av.open(path) as container:
        stream = container.streams.video[0]
        stream.thread_type = "AUTO"
        for frame in container.decode(stream):
            yield frame.to_image(), float(frame.time or 0.0)


def frames_cv2(path):
    import cv2
    cap = cv2.VideoCapture(path)
    if not cap.isOpened():
        sys.exit(f"cannot open {path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 25.0
    i = 0
    while True:
        ok, bgr = cap.read()
        if not ok:
            break
        rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
        yield Image.fromarray(rgb), i / fps
        i += 1
    cap.release()


def source_frames(path):
    try:
        import av  # noqa: F401
        return frames_pyav(path)
    except ImportError:
        pass
    try:
        import cv2  # noqa: F401
        return frames_cv2(path)
    except ImportError:
        sys.exit("needs PyAV or OpenCV:  pip install av   (or opencv-python)")


def fit_band(img, anchor):
    """Centre-crop to the band's aspect ratio, then resize to 480x80.

    `anchor` picks which slice of a tall source survives the crop: a landing
    shot usually wants the horizon, not the middle of the sky.

    The band keeps only ~30% of a 16:9 source's height, and boosters descending
    to a pad travel through far more than that, so a fixed anchor cannot hold
    both the descent and the touchdown. See anchor_at().
    """
    tw, th = BAND_W, BAND_H
    sw, sh = img.size
    target = tw / th
    if sw / sh > target:
        nw = int(sh * target)
        box = ((sw - nw) // 2, 0, (sw - nw) // 2 + nw, sh)
    else:
        nh = int(sw / target)
        top = int((sh - nh) * anchor)
        box = (0, top, sw, top + nh)
    return img.crop(box).resize((tw, th), Image.LANCZOS)


def anchor_at(progress, start, end, settle):
    """Crop anchor for a frame `progress` (0..1) through the clip.

    Pans from `start` to `end` on an ease-out curve that completes at
    `settle`, then holds. The pan follows the subject down the frame; settling
    before the end means the touchdown and everything after it sit still,
    which is what you want once there is nothing left to follow.
    """
    if end is None or settle <= 0:
        return start
    p = min(1.0, progress / settle)
    ease = 1.0 - (1.0 - p) ** 2
    return start + (end - start) * ease


def grade(img, saturation, brightness, contrast, cap):
    img = ImageEnhance.Color(img).enhance(saturation)
    img = ImageEnhance.Brightness(img).enhance(brightness)
    img = ImageEnhance.Contrast(img).enhance(contrast)
    # Hard ceiling, applied last so nothing above can reintroduce a hot pixel.
    lut = [min(cap, i * cap // 255) for i in range(256)] * 3
    return img.point(lut)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("video")
    ap.add_argument("-o", "--output", default="splash.bin")
    ap.add_argument("--fps", type=int, default=10,
                    help="playback rate; the panel runs at 10 Hz (default: 10)")
    ap.add_argument("--duration", type=float, default=10.0,
                    help="seconds to take (default: 10)")
    ap.add_argument("--start", type=float, default=0.0,
                    help="skip this many seconds of source first")
    ap.add_argument("--quality", type=int, default=62, help="JPEG quality")
    ap.add_argument("--saturation", type=float, default=0.45)
    ap.add_argument("--brightness", type=float, default=0.62)
    ap.add_argument("--contrast", type=float, default=0.92)
    ap.add_argument("--cap", type=int, default=0x9A,
                    help="max value per channel (default: 154 = 0x9A)")
    ap.add_argument("--anchor", type=float, default=0.5,
                    help="vertical crop position 0..1 (default: 0.5)")
    ap.add_argument("--anchor-end", type=float, default=None,
                    help="pan the crop to this position over the clip, to "
                         "follow a descending subject (default: no pan)")
    ap.add_argument("--anchor-settle", type=float, default=0.65,
                    help="fraction of the clip by which the pan completes "
                         "(default: 0.65)")
    ap.add_argument("--limit", type=int, default=2 * 1024 * 1024,
                    help="partition size to check against")
    ap.add_argument("--preview", metavar="PNG",
                    help="also write a contact sheet of the graded frames")
    args = ap.parse_args()

    interval = 1000 // args.fps
    want = int(args.duration * args.fps)
    step = 1.0 / args.fps

    raw, next_t, taken = [], args.start, 0
    for img, t in source_frames(args.video):
        if t < args.start:
            continue
        if t + 1e-6 < next_t:
            continue
        raw.append(img)
        taken += 1
        next_t += step
        if taken >= want:
            break

    if not raw:
        sys.exit("no frames taken — check --start against the clip length")

    # Anchoring needs the frame count, so crop and grade in a second pass.
    n_raw = len(raw)
    frames = []
    for i, img in enumerate(raw):
        prog = i / max(1, n_raw - 1)
        a = anchor_at(prog, args.anchor, args.anchor_end, args.anchor_settle)
        frames.append(grade(fit_band(img, a), args.saturation,
                            args.brightness, args.contrast, args.cap))
    raw = None
    if taken < want:
        print(f"note: source ran out at {taken} frames "
              f"({taken / args.fps:.1f} s of the {args.duration:.0f} s asked for)")

    import io
    blobs = []
    for img in frames:
        buf = io.BytesIO()
        img.save(buf, "JPEG", quality=args.quality, optimize=True,
                 subsampling="4:2:0")
        blobs.append(buf.getvalue())

    n = len(blobs)
    head = struct.pack("<4sHHHHHH", MAGIC, FORMAT, n, BAND_W, BAND_H, interval, 0)
    table_bytes = n * 8
    off = HDR + table_bytes
    table = b""
    for b in blobs:
        table += struct.pack("<II", off, len(b))
        off += len(b)

    out = head + table + b"".join(blobs)
    if len(out) > args.limit:
        sys.exit(f"blob is {len(out)} B, over the {args.limit} B partition — "
                 f"lower --quality or --duration")

    with open(args.output, "wb") as f:
        f.write(out)

    avg = sum(len(b) for b in blobs) // n
    print(f"{args.output}: {n} frames, {BAND_W}x{BAND_H}, {interval} ms/frame "
          f"({n * interval / 1000:.1f} s)")
    print(f"  {len(out):,} B total, {avg:,} B/frame avg, "
          f"{max(len(b) for b in blobs):,} B peak "
          f"({100 * len(out) / args.limit:.1f}% of the partition)")

    if args.preview:
        cols, rows = 2, (min(n, 12) + 1) // 2
        sheet = Image.new("RGB", (BAND_W * cols + 12, (BAND_H + 6) * rows + 6),
                          (18, 18, 20))
        pick = [frames[i * (n - 1) // max(1, min(n, 12) - 1)]
                for i in range(min(n, 12))]
        for i, im in enumerate(pick):
            sheet.paste(im, (4 + (i % cols) * (BAND_W + 4),
                             4 + (i // cols) * (BAND_H + 6)))
        sheet.save(args.preview)
        print(f"  preview: {args.preview}")


if __name__ == "__main__":
    main()
