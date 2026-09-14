#!/usr/bin/env python3
"""Build an RLCV splash video band for the RLC remote.

Turns any video the host can decode into the 480x160 JPEG frame sequence the
remote's boot splash plays from its `splash` partition.

    tools/mkvideoband.py boosters.mp4 -o splash.bin --track --zoom 1.2 --zoom-end 1.7
    ./build_remote.sh splash splash.bin

Two things here are not decoration:

GRADING. The band sits behind white title text on the boot screen of a launch
controller, and the text has to win. Every frame is desaturated, darkened and
then hard-capped so no channel exceeds --cap (default 0x9A), the same bound the
firmware documents. Footage that looks good on a monitor will scream here.

FRAMING. The band is 3:1 and keeps ~59% of a 16:9 frame's height at --zoom 1,
so a rocket landing — a tall subject moving a long way down the frame, shot by
a camera that pans and zooms to follow it — will not sit still in a fixed crop.
--track follows the subject automatically; see tracking notes below.

RE-TUNE AFTER THE 6:1 -> 3.75:1 CHANGE. Recipes written for the old 480x80
band do not transfer. A 3.75:1 window holds ~1.6x the frame height at the same
zoom, so the aggressive push-in the 6:1 strip needed (--zoom-end 2.6) now
overshoots, and --track-bias-y, which existed to buy headroom a 6:1 letterbox
could not spare, wants to be near zero. Start wider and check with --preview.

OVERLAY. Since 1.2.12 the title block is drawn ON this band, not above it. The
text is carried by ramped scrims baked in here — over the band's top rows for
the two title lines (--scrim-h/--scrim-keep) and over its bottom rows for the
club credit (--scrim-bot-h/--scrim-bot-keep), all four of which MUST match the
firmware's VBAND_SCRIM_* — plus glyph outlines drawn by the firmware — so the --cap grading below stays as
it is and SHOULD NOT be lowered further to "help". Grading the whole clip down
to suit its worst frame costs the picture everywhere to fix a problem confined
to the top third, which is the trade the scrim exists to avoid. What the
footage does owe the layout is quieter composition in its top third: the
MIDDLE of the band is clear of text and full strength (rows ~56-119),
and that is where the subject should land.

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

# 1.2.14: the band is the top half of the panel, 480x160 (3:1). It went
# 480x80 (6:1) -> 480x128 (3.75:1) -> here as the SPI clock and the frame
# budget allowed; 160 rows needs the 40 MHz clock (FSD 10.2.1). The firmware validates these
# against its own VBAND_W/VBAND_H and rejects a mismatched asset outright —
# cleanly, falling back to a plain dark band — so an asset built by an older
# copy of this tool will simply not play. Rebuild it, do not force it.
BAND_W = 480
BAND_H = 160
BAND_ASPECT = BAND_W / BAND_H      # 3:1

ANALYSIS_W, ANALYSIS_H = 960, 540  # tracking works on a downscale; plenty


# ── source decoding ────────────────────────────────────────────────

def frames_pyav(path, start=0.0):
    """Decode from `start`, seeking rather than grinding through the head.

    This is worth its few lines. Every run is two full passes (track, then
    crop), and without a seek both begin at t=0 — so cutting ten seconds from
    222 s into a 4K source meant decoding 7.4 minutes of video to use ten
    seconds of it, twice, at about six minutes a run. That is slow enough to
    stop you iterating on framing, which is the one thing this tool exists to
    let you do.

    Seek lands on a keyframe at or before the target, so back up two seconds
    and let the caller's own `t < start` test discard the run-in. The offset
    is in AV_TIME_BASE units (microseconds) because no stream is passed.
    """
    import av
    with av.open(path) as container:
        stream = container.streams.video[0]
        stream.thread_type = "AUTO"
        if start > 0:
            container.seek(int(max(0.0, start - 2.0) * 1_000_000))
        for frame in container.decode(stream):
            yield frame.to_image(), float(frame.time or 0.0)


def frames_cv2(path, start=0.0):
    import cv2
    cap = cv2.VideoCapture(path)
    if not cap.isOpened():
        sys.exit(f"cannot open {path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 25.0
    if start > 0:
        cap.set(cv2.CAP_PROP_POS_MSEC, max(0.0, start - 2.0) * 1000.0)
    i = int(max(0.0, start - 2.0) * fps)
    while True:
        ok, bgr = cap.read()
        if not ok:
            break
        yield Image.fromarray(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)), i / fps
        i += 1
    cap.release()


def source_frames(path, start=0.0):
    try:
        import av  # noqa: F401
        return frames_pyav(path, start)
    except ImportError:
        pass
    try:
        import cv2  # noqa: F401
        return frames_cv2(path, start)
    except ImportError:
        sys.exit("needs PyAV or OpenCV:  pip install av   (or opencv-python)")


def selected_frames(path, start, step, want):
    """Yield (index, PIL image) for the frames the band will be built from.

    Iterated twice — once to track, once to crop — rather than holding them:
    100 frames of 4K RGB is 2.5 GB resident, and decoding ten seconds again is
    far cheaper than that.
    """
    nxt, taken = start, 0
    for img, t in source_frames(path, start):
        if t < start or t + 1e-6 < nxt:
            continue
        yield taken, img
        taken += 1
        nxt += step
        if taken >= want:
            return


# ── subject tracking ───────────────────────────────────────────────

def find_blobs(img):
    """Return the bright blobs in one frame as (cx, cy, x0, y0, x1, y1, area),
    all in fractions of frame size. Selection between them is policy and lives
    in smooth_track().

    The plumes are the brightest thing in the shot and are blown out to near
    white, so brightness — not colour — is the discriminator. (Warmth, R-B,
    looks like the obvious choice and is a trap: clipped white flame has
    R ~= B, so an R-B test finds dark red vegetation instead.) The threshold
    floats with the frame's own mean so dusk footage and daylight both work.
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
    out = []
    for i in range(1, n):
        area = int(stats[i, cv2.CC_STAT_AREA])
        if area < 8:
            continue
        x0 = stats[i, cv2.CC_STAT_LEFT]
        y0 = stats[i, cv2.CC_STAT_TOP]
        out.append((cent[i][0] / ANALYSIS_W, cent[i][1] / ANALYSIS_H,
                    x0 / ANALYSIS_W, y0 / ANALYSIS_H,
                    (x0 + stats[i, cv2.CC_STAT_WIDTH]) / ANALYSIS_W,
                    (y0 + stats[i, cv2.CC_STAT_HEIGHT]) / ANALYSIS_H,
                    area))
    return out


def smooth_track(per_frame, search, max_step, alpha):
    """Turn per-frame blob lists into a crop path that can be watched.

    Selection, then three guards.

    SELECTION is the **bounding-box centre** of the kept blobs, not their
    area-weighted centroid. Both agree while the subject is two symmetrical
    plumes. They diverge badly once it becomes a smoke cloud: the centroid is
    dragged down into the dense base of the cloud, so the crop sits low, fills
    its lower half with ground and cuts the top off the billow. The box centre
    tracks what the subject actually occupies.

    SEARCH WINDOW — after the first lock, only blobs whose centre lies within
    `search` of the current path position are considered. This is what makes
    tracking survivable after engine cut-off: the smoke is still the brightest
    thing nearby, but sunlit roads and buildings near the horizon are brighter
    than thinning smoke, and ungated the detector will happily jump to them and
    throw the framing to the bottom of the frame. Gating keeps it on the pad.

    max_step bounds travel per frame, so one bad detection cannot throw the
    framing. alpha is exponential smoothing, because a centre that jitters by a
    pixel becomes a band that visibly shakes.
    """
    out, cur = [], None
    for blobs in per_frame:
        if blobs:
            if cur is None:
                pool = blobs
            else:
                pool = [b for b in blobs
                        if abs(b[0] - cur[0]) <= search
                        and abs(b[1] - cur[1]) <= search]
            if pool:
                biggest = max(b[6] for b in pool)
                keep = [b for b in pool if b[6] >= 0.20 * biggest]
                x0 = min(b[2] for b in keep); x1 = max(b[4] for b in keep)
                y0 = min(b[3] for b in keep); y1 = max(b[5] for b in keep)
                tx, ty = (x0 + x1) / 2, (y0 + y1) / 2

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


def _smoothstep(p):
    p = min(1.0, max(0.0, p))
    return p * p * (3.0 - 2.0 * p)


def zoom_at(progress, start, peak, settle, final):
    """Three-point zoom: `start` -> `peak` by `settle`, then -> `final` by the
    end of the clip. Smoothstepped throughout.

    A smoothstep, not an ease-out: an ease-out push-in does almost all its
    travel in the first moment and then creeps, so the shot is already tight
    before the subject has done anything — the opposite of what a push-in is
    for. Smoothstep stays wide while the subject is still far away and large
    in frame, then moves, then settles on the moment that matters.

    The pull-back afterwards is not symmetry for its own sake. The subject
    keeps growing after touchdown — a smoke column quickly becomes far larger
    than the tight framing that suited two descending boosters — so holding the
    peak zoom crops the top off it. Pulling back also leaves the clip ending at
    roughly the width it started, which is what lets it loop without a visible
    jump.
    """
    if peak is None:
        return start
    if progress <= settle or settle >= 1.0:
        return start + (peak - start) * _smoothstep(progress / max(settle, 1e-6))
    if final is None:
        return peak
    return peak + (final - peak) * _smoothstep((progress - settle) / (1.0 - settle))


def crop_band(img, cx, cy, zoom, bias_y=0.0):
    """Cut a BAND_ASPECT (3:1) window of width (source width / zoom) centred on (cx, cy),
    clamped inside the frame, and resize it to the band.

    `bias_y` shifts the window as a fraction of its own height — negative is
    up. Subjects are rarely centred on what the detector finds: a rising smoke
    column wants headroom, not ground."""
    sw, sh = img.size
    cw = min(sw, sw / max(zoom, 1e-6))
    ch = cw / BAND_ASPECT
    if ch > sh:                      # never taller than the source
        ch = sh
        cw = ch * BAND_ASPECT

    x = min(sw - cw, max(0, cx * sw - cw / 2))
    y = min(sh - ch, max(0, cy * sh - ch / 2 + bias_y * ch))
    box = (int(round(x)), int(round(y)), int(round(x + cw)), int(round(y + ch)))
    return img.crop(box).resize((BAND_W, BAND_H), Image.LANCZOS)


def scrim_bottom(img, bot_h, keep_bot):
    """Mirror of scrim() at the foot of the band.

    Exists because the club credit moved from under the top scrim to the
    bottom of the band (fw 1.2.15), which is what clears the middle of the
    picture for the landing itself. Same ramp, inverted: full strength at the
    top of the region, down to keep_bot/256 on the last row.
    """
    if bot_h <= 1:
        return img
    px = img.load()
    w, h = img.size
    y0 = max(0, h - bot_h)
    span = (h - 1) - y0
    if span <= 0:
        return img
    for y in range(y0, h):
        keep = 256 - ((256 - keep_bot) * (y - y0)) // span
        for x in range(w):
            r, g, b = px[x, y]
            px[x, y] = ((r * keep) >> 8, (g * keep) >> 8, (b * keep) >> 8)
    return img


def scrim(img, scrim_h, keep_top):
    """Darken the band's top rows on a ramp, for the title block drawn over
    them by the firmware.

    BAKED IN HERE, NOT DONE ON THE REMOTE. Firmware 1.2.12 first applied this
    per frame in the framebuffer and it cost far too much: ~150 KB of
    byte-wise read-modify-write in PSRAM, every frame, inside a display task
    that had 100 ms for everything. Here it is free — it happens once, on a
    host, before the JPEG is even encoded.

    The maths is the firmware's, integer for integer, so the look does not
    shift between the two: `keep` rises linearly from keep_top/256 at row 0 to
    256/256 at row scrim_h-1, and each channel becomes (c * keep) >> 8.
    """
    if scrim_h <= 1:
        return img
    px = img.load()
    w, h = img.size
    for y in range(min(scrim_h, h)):
        keep = keep_top + ((256 - keep_top) * y) // (scrim_h - 1)
        for x in range(w):
            r, g, b = px[x, y]
            px[x, y] = ((r * keep) >> 8, (g * keep) >> 8, (b * keep) >> 8)
    return img


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

    ap.add_argument("--zoom-final", type=float, default=None,
                    help="pull back out to this zoom by the end of the clip, "
                         "for a subject that keeps growing after the moment "
                         "(and to let the clip loop cleanly)")
    ap.add_argument("--track", action="store_true",
                    help="follow the brightest subject (the plumes) instead "
                         "of using a fixed/panned anchor")
    ap.add_argument("--track-search", type=float, default=0.12,
                    help="only consider blobs within this fraction of the "
                         "frame of the current position (default: 0.12)")
    ap.add_argument("--track-bias-y", type=float, default=0.0,
                    help="shift the crop vertically by this fraction of its "
                         "own height; negative is up (default: 0)")
    ap.add_argument("--track-step", type=float, default=0.012,
                    help="max crop movement per frame, frame fractions")
    ap.add_argument("--track-alpha", type=float, default=0.35,
                    help="smoothing; lower is steadier (default: 0.35)")

    ap.add_argument("--anchor", type=float, default=0.5,
                    help="vertical crop position when not tracking")
    ap.add_argument("--anchor-end", type=float, default=None)
    ap.add_argument("--anchor-settle", type=float, default=0.65)

    ap.add_argument("--scrim-h", type=int, default=56,
                    help="rows of ramped darkening at the top of the band, "
                         "under the firmware's title line. MUST match the "
                         "firmware's VBAND_SCRIM_H (default: 56); 0 disables")
    ap.add_argument("--scrim-keep", type=int, default=56,
                    help="fraction of 256 of the original pixel surviving at "
                         "row 0, ramping to 256 by --scrim-h. MUST match the "
                         "firmware's VBAND_SCRIM_KEEP (default: 56 = 22%%)")

    ap.add_argument("--scrim-bot-h", type=int, default=40,
                    help="rows of ramped darkening at the BOTTOM of the band, "
                         "under the club credit. MUST match the firmware's "
                         "VBAND_SCRIM_BOT_H (default: 40); 0 disables")
    ap.add_argument("--scrim-bot-keep", type=int, default=72,
                    help="fraction of 256 surviving on the last row, ramping "
                         "up to 256 at the top of that region. MUST match the "
                         "firmware's VBAND_SCRIM_BOT_KEEP (default: 72)")

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
            raw.append(find_blobs(img))
        if not raw:
            sys.exit("no frames taken — check --start against the clip length")
        track = smooth_track(raw, args.track_search, args.track_step,
                             args.track_alpha)
        blind = sum(1 for r in raw if not r)
        print(f"tracked {len(raw)} frames, {len(raw) - blind} with blobs, "
              f"{blind} blind")

    # ── pass 2: crop, grade, encode ──
    frames = []
    for i, img in selected_frames(args.video, args.start, step, want):
        prog = i / max(1, want - 1)
        zoom = zoom_at(prog, args.zoom, args.zoom_end, args.zoom_settle,
                       args.zoom_final)
        if track is not None:
            cx, cy = track[min(i, len(track) - 1)]
        else:
            cx = 0.5
            cy = ease_to(prog, args.anchor, args.anchor_end, args.anchor_settle)
        frames.append(scrim_bottom(
            scrim(grade(crop_band(img, cx, cy, zoom, args.track_bias_y),
                        args.saturation, args.brightness, args.contrast,
                        args.cap),
                  args.scrim_h, args.scrim_keep),
            args.scrim_bot_h, args.scrim_bot_keep))

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
