# Boot splash assets

The remote's boot splash plays a video band across the top half of its panel,
with the title and club credit drawn over it (FSD §10.2.1). The asset lives in
the remote's own `splash` flash partition, so footage can be re-cut and
reflashed without rebuilding or touching the image that runs the fire path.

| File | Geometry | For firmware |
|---|---|---|
| `splash_falconheavy_160.bin` | 480×160, 50 frames @ 200 ms (5 Hz), 10.0 s | **1.2.14 and later** |
| `splash_falconheavy.bin` | 480×80, 100 frames @ 100 ms, 10.0 s | 1.2.11 and earlier |

Flash the current one with:

    ./build_remote.sh splash assets/splash_falconheavy_160.bin

The 480×80 file is kept only as the asset that goes with 1.2.11. It will not
play on current firmware — `splash_video_validate()` rejects the dimensions and
the splash falls back to a plain dark band, which is the designed behaviour for
every asset failure.

## Source and licence

Falcon Heavy Demonstration Mission, 6 February 2018. Taken from
`Falcon_Heavy_test_flight.webm` on Wikimedia Commons — NASA imagery, credited
to Charles A. Babir, sourced from images.nasa.gov. **Public domain in the
United States:** NASA material is not protected by copyright unless otherwise
noted. No attribution is required; it is recorded here because knowing where a
redistributed asset came from is worth more than the two lines it costs.

    https://commons.wikimedia.org/wiki/File:Falcon_Heavy_test_flight.webm

The landing runs from 215.5 s to about 234 s of that source, with a cut to a
different camera at 235 s — any window must end before it.

## Exact recipe

Two steps. Build at 10 Hz, then re-time to 5 Hz:

    tools/mkvideoband.py fh.webm -o fh160.bin \
        --start 222.0 --duration 10 --track \
        --zoom 1.0 --zoom-end 1.9 --zoom-settle 0.55 --zoom-final 1.10 \
        --track-bias-y -0.08

    tools/rlcv_repack.py fh160.bin splash_falconheavy_160.bin 2 200

**Why two steps and not `--fps 5`.** Tracking runs over the frames the tool
selects and its guards are per-frame: `--track-step` bounds travel per frame,
`--track-alpha` smooths per frame. Sampling at 5 Hz makes the subject move
twice as far between frames, so the clamp bites harder and the crop path comes
out different. Building at 10 Hz keeps the motion path that was actually
reviewed; re-timing is lossless and instant.

Result: 159,464 B, 7.6 % of the 2 MB partition.

## Why these numbers

**5 Hz, not 10.** The constraint is the panel, and it is tighter than it looks.
The ILI9488 is 18-bit-only over SPI at 3 bytes/pixel, and `flush()` was
measured at 99 ms for a 480×128 band at 20 MHz — 74 ms of that the transfer
itself, the rest per-row `memcmp`, bounce copy and shadow update. Decode
(~12 ms) and the blit (~9 ms) were never the problem. At 480×160 and 40 MHz a
frame where the clip advances costs ~120 ms against a 100 ms period, so 10 Hz
does not fit; at 5 Hz the frames between cost almost nothing and the average is
~73 ms. Pushing for 10 Hz here does not gain smoothness, it just makes the
display task hog its core.

**The layout leaves the middle clear, and the cut must respect it.** Rows
0–55 carry the title under a ramped scrim; rows 120–159 carry the club credit
under another. **Rows ~56–119 are text-free and full strength, and that is
where the touchdown belongs.** `--track-bias-y -0.08` is what puts it there —
it also drops surplus foreground, and what remains is dimmed by the bottom
scrim rather than competing with the smoke.

**The scrims are baked in here, not applied by the firmware.** They ran on the
remote for exactly one revision and cost ~14 ms/frame of byte-wise
read-modify-write in PSRAM, inside a display task with a 100 ms budget. The
tool applies the identical integer maths once, on a host. `--scrim-h`,
`--scrim-keep`, `--scrim-bot-h` and `--scrim-bot-keep` **must match the
firmware's `VBAND_SCRIM_*`** — the defaults do.

> **A stale asset will not be caught.** The validator checks dimensions, not
> scrim geometry. An asset built for a different layout plays happily with its
> dark stripes in the wrong place. Every other asset failure fails safe; this
> one does not. Rebuild the asset whenever the scrim constants or the text
> layout move.

**Do not lower `--cap` to help the text.** The scrims plus the firmware's glyph
outlines carry it. Grading the whole clip down to suit its worst frame costs
the picture everywhere to fix a problem confined to the text rows.

## Framing notes worth keeping

**Tracking keys on brightness, not colour.** Warmth (R−B) is the obvious choice
and is a trap: the flames are blown out to near-white, so R ≈ B, and an R−B
test locks onto dark red vegetation instead. A threshold that floats with the
frame mean finds the plumes in both dusk and daylight.

**Selection is the bounding-box centre, not the centroid.** The two agree while
the subject is two symmetrical plumes and diverge badly once it is a smoke
column: the area-weighted centroid is dragged into the dense base of the cloud,
sitting the crop low and cutting the top off the billow.

**The search window is what survives engine cut-off.** Once the smoke thins,
sunlit roads and buildings near the horizon are brighter than it; ungated, the
detector jumps to them and throws the framing to the bottom of the frame.

**The push-in is a smoothstep, not an ease-out**, and `--zoom-settle 0.55` aims
the end of the move at the touchdown itself. **`--zoom-final` pulls back out**
afterwards, because the smoke column grows past any tight framing and because
the firmware loops the clip — a cut that ends tighter than it opens will pop at
the wrap.

**Re-cutting is cheap now.** `mkvideoband.py` seeks to `--start` instead of
decoding from t=0, so a run over this source takes ~30 s rather than ~6 min.
Use `--preview` and iterate.
