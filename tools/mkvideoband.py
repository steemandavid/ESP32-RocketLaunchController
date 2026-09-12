#!/usr/bin/env python3
"""Build an RLCV splash video band for the RLC remote.

Turns any video the host can decode into the 480x80 JPEG frame sequence the
remote's boot splash plays from its `splash` partition.

    tools/mkvideoband.py boosters.mp4 -o splash.bin --track --zoom 1.2 --zoom-end 1.7
    ./build_remote.sh splash splash.bin

Two things here are not decoration:

GRADING. The band sits behind white title text on the boot screen of a launch
controller, and the text has to win. Every frame is desaturated, darkened and
then hard-capped so no channel exceeds --cap (default 0x9A), the same bound the
firmware documents. Footage that looks good on a monitor will scream here.

FRAMING. The band is 6:1 and keeps only ~30% of a 16:9 frame's height at
--zoom 1, so a rocket landing — a tall subject moving a long way down the
frame, shot by a camera that pans and zooms to follow it — will not sit still
in a fixed crop. --track follows the subject automatically; see tracking notes
below.

Needs PyAV or OpenCV to decode, Pillow for the rest, and NumPy + OpenCV for
--track.
"""

import argparse
import io
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
BAND_ASPECT = BAND_W / BAND_H      # 6:1

ANALYSIS_W, ANALYSIS_H = 960, 540  # tracking works on a downscale; plenty


# ── source decoding ────────────────────────────────────────────────

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
        yield Image.fromarray(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)), i / fps
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


def selected_frames(path, start, step, want):
    """Yield (index, PIL image) for the frames the band will be built from.

    Iterated twice — once to track, once to crop — rather than holding them:
    100 frames of 4K RGB is 2.5 GB resident, and decoding ten seconds again is
    far cheaper than that.
    """
    nxt, taken = start, 0
    for img, t in source_frames(path):
        if t < start or t + 1e-6 < nxt:
            continue
        yield taken, img
        taken += 1
        nxt += step
        if taken >= want:
            return


# ── subject tracking ───────────────────────────────────────────────

def find_subject(img):
    """Locate the rocket plumes in one frame. Returns (x, y, area) in
    fractions of frame size, or None.

    The plumes are the brightest thing in the shot and are blown out to near
    white, so brightness — not colour — is the discriminator. (Warmth, R-B,
    looks like the obvious choice and is a trap: clipped white flame has
    R ~= B, so an R-B test finds dark red vegetation instead.) The threshold
    floats with the frame's own mean so dusk footage and daylight both work.

    Bright blobs within 20% of the largest are merged before taking the
    centroid, which is what keeps the point between two descending boosters
    rather than snapping to whichever is momentarily brighter.
    """
    import numpy as np
    import cv2

    a = np.asarray(img.resize((ANALYSIS_W, ANALYSIS_H))).astype(np.int32)
    # int32 deliberately: this overflows in int16 (255 * 587 > 32767) and the
    # whole image silently comes out dark.
    luma = (a[:, :, 0] * 299 + a[:, :, 1] * 587 + a[:, :, 2] * 114) // 1000

    thr = max(200, int(luma.mean()) + 45)
    mask = (luma > thr).astype(np.uint8)

    n, _lab, stats, cent = cv2.connectedComponentsWithStats(mask, 8)
    if n <= 1:
        return None
    areas = stats[1:, cv2.CC_STAT_AREA]
    keep = [i + 1 for i, ar in enumerate(areas) if ar >= max(8, 0.20 * areas.max())]
    if not keep:
        return None

    total = sum(int(stats[i, cv2.CC_STAT_AREA]) for i in keep)
    cx = sum(cent[i][0] * stats[i, cv2.CC_STAT_AREA] for i in keep) / total
    cy = sum(cent[i][1] * stats[i, cv2.CC_STAT_AREA] for i in keep) / total
    return cx / ANALYSIS_W, cy / ANALYSIS_H, total


def smooth_track(points, area_floor, max_step, alpha):
    """Turn raw per-frame detections into a crop path that can be watched.

    Three guards, each earning its place on real footage:

    area_floor — once the engines cut and the smoke thins, there is no subject
    left to track, and the detector will happily lock onto sunlit roads or
    buildings near the horizon instead. Below the floor the path HOLDS. That is
    not a fallback, it is correct: the pads do not move.

    max_step — bounds how far the crop may travel per frame, so a single bad
    detection cannot throw the framing.

    alpha — exponential smoothing, because a centroid that jitters by a pixel
    turns into a band that visibly shakes.
    """
    out, cur = [], None
    for p in points:
        if p is not None and p[2] >= area_floor:
            tx, ty = p[0], p[1]
            if cur is None:
                cur = [tx, ty]
            else:
                nx = cur[0] + (tx - cur[0]) * alpha
                ny = cur[1] + (ty - cur[1]) * alpha
                nx = min(cur[0] + max_step, max(cur[0] - max_step, nx))
                ny = min(cur[1] + max_step, max(cur[1] - max_step, ny))
                cur = [nx, ny]
        out.append(tuple(cur) if cur else None)

    # Frames before the first detection get the first known position, so the
    # clip does not open on a lurch.
    first = next((p for p in out if p), (0.5, 0.5))
    return [p if p else first for p in out]


# ── framing ────────────────────────────────────────────────────────

def ease_to(progress, start, end, settle):
    """Ease-out from start to end, completing at `settle`, then holding.

    Ease-out is right for following a subject: move with it early, settle as
    it stops. It is wrong for a zoom — see smoothstep_to.
    """
    if end is None or settle <= 0:
        return start
    p = min(1.0, progress / settle)
    return start + (end - start) * (1.0 - (1.0 - p) ** 2)


def smoothstep_to(progress, start, end, settle):
    """Smoothstep from start to end, completing at `settle`, then holding.

    Used for the zoom. An ease-out push-in does almost all its travel in the
    first moment and then creeps, so the shot is already tight before the
    subject has done anything — the opposite of what a push-in is for. A
    smoothstep stays wide while the subject is still far away and large in
    frame, then moves, then settles on the moment that matters.
    """
    if end is None or settle <= 0:
        return start
    p = min(1.0, progress / settle)
    return start + (end - start) * (p * p * (3.0 - 2.0 * p))


def crop_band(img, cx, cy, zoom):
    """Cut a 6:1 window of width (source width / zoom) centred on (cx, cy),
    clamped inside the frame, and resize it to the band."""
    sw, sh = img.size
    cw = min(sw, sw / max(zoom, 1e-6))
    ch = cw / BAND_ASPECT
    if ch > sh:                      # never taller than the source
        ch = sh
        cw = ch * BAND_ASPECT

    x = min(sw - cw, max(0, cx * sw - cw / 2))
    y = min(sh - ch, max(0, cy * sh - ch / 2))
    box = (int(round(x)), int(round(y)), int(round(x + cw)), int(round(y + ch)))
    return img.crop(box).resize((BAND_W, BAND_H), Image.LANCZOS)


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
    ap.add_argument("--duration", type=float, default=10.0)
    ap.add_argument("--start", type=float, default=0.0)
    ap.add_argument("--quality", type=int, default=82)

    ap.add_argument("--saturation", type=float, default=0.45)
    ap.add_argument("--brightness", type=float, default=0.80)
    ap.add_argument("--contrast", type=float, default=1.05)
    ap.add_argument("--cap", type=int, default=0x9A,
                    help="max value per channel (default: 154 = 0x9A)")

    ap.add_argument("--zoom", type=float, default=1.0,
                    help="1.0 = full source width (default: 1.0)")
    ap.add_argument("--zoom-end", type=float, default=None,
                    help="push in to this zoom over the clip")
    ap.add_argument("--zoom-settle", type=float, default=0.75,
                    help="fraction of the clip by which the push-in completes. "
                         "Aim this at the moment that matters — the touchdown "
                         "— not at the end of the clip.")

    ap.add_argument("--track", action="store_true",
                    help="follow the brightest subject (the plumes) instead "
                         "of using a fixed/panned anchor")
    ap.add_argument("--track-area", type=int, default=300,
                    help="hold the crop when the detected subject falls below "
                         "this many analysis pixels (default: 300)")
    ap.add_argument("--track-step", type=float, default=0.012,
                    help="max crop movement per frame, frame fractions")
    ap.add_argument("--track-alpha", type=float, default=0.35,
                    help="smoothing; lower is steadier (default: 0.35)")

    ap.add_argument("--anchor", type=float, default=0.5,
                    help="vertical crop position when not tracking")
    ap.add_argument("--anchor-end", type=float, default=None)
    ap.add_argument("--anchor-settle", type=float, default=0.65)

    ap.add_argument("--limit", type=int, default=2 * 1024 * 1024)
    ap.add_argument("--preview", metavar="PNG")
    args = ap.parse_args()

    interval = 1000 // args.fps
    want = int(args.duration * args.fps)
    step = 1.0 / args.fps

    # ── pass 1: track ──
    track = None
    if args.track:
        raw = []
        for _i, img in selected_frames(args.video, args.start, step, want):
            raw.append(find_subject(img))
        if not raw:
            sys.exit("no frames taken — check --start against the clip length")
        track = smooth_track(raw, args.track_area, args.track_step,
                             args.track_alpha)
        held = sum(1 for p, r in zip(track, raw)
                   if r is None or r[2] < args.track_area)
        print(f"tracked {len(raw)} frames, {len(raw) - held} locked, {held} held")

    # ── pass 2: crop, grade, encode ──
    frames = []
    for i, img in selected_frames(args.video, args.start, step, want):
        prog = i / max(1, want - 1)
        zoom = smoothstep_to(prog, args.zoom, args.zoom_end, args.zoom_settle)
        if track is not None:
            cx, cy = track[min(i, len(track) - 1)]
        else:
            cx = 0.5
            cy = ease_to(prog, args.anchor, args.anchor_end, args.anchor_settle)
        frames.append(grade(crop_band(img, cx, cy, zoom), args.saturation,
                            args.brightness, args.contrast, args.cap))

    if not frames:
        sys.exit("no frames taken — check --start against the clip length")
    if len(frames) < want:
        print(f"note: source ran out at {len(frames)} frames "
              f"({len(frames) / args.fps:.1f} s of {args.duration:.0f} s asked for)")

    blobs = []
    for img in frames:
        buf = io.BytesIO()
        img.save(buf, "JPEG", quality=args.quality, optimize=True,
                 subsampling="4:2:0")
        blobs.append(buf.getvalue())

    n = len(blobs)
    head = struct.pack("<4sHHHHHH", MAGIC, FORMAT, n, BAND_W, BAND_H, interval, 0)
    off, table = HDR + n * 8, b""
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
        k = min(n, 12)
        pick = [frames[i * (n - 1) // max(1, k - 1)] for i in range(k)]
        cols, rows = 2, (k + 1) // 2
        sheet = Image.new("RGB", (BAND_W * cols + 12, (BAND_H + 6) * rows + 6),
                          (18, 18, 20))
        for i, im in enumerate(pick):
            sheet.paste(im, (4 + (i % cols) * (BAND_W + 4),
                             4 + (i // cols) * (BAND_H + 6)))
        sheet.save(args.preview)
        print(f"  preview: {args.preview}")


if __name__ == "__main__":
    main()
