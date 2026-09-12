# Boot splash assets

## `splash_falconheavy.bin` — flashed asset, RLCV v1

The remote's boot splash video band (FSD §10.2.1): the two Falcon Heavy side
boosters descending onto LZ-1 and LZ-2 and touching down, 100 frames of
480×80 JPEG at 10 Hz, 10.0 s.

Flash it with:

    ./build_remote.sh splash assets/splash_falconheavy.bin

It lives in the remote's own `splash` partition, so this never requires
rebuilding or reflashing firmware.

### Source and licence

Falcon Heavy Demonstration Mission, 6 February 2018. Taken from
`Falcon_Heavy_test_flight.webm` on Wikimedia Commons — NASA imagery, credited
to Charles A. Babir, sourced from images.nasa.gov. **Public domain in the
United States:** NASA material is not protected by copyright unless otherwise
noted. No attribution is required; it is recorded here because knowing where a
redistributed asset came from is worth more than the two lines it costs.

    https://commons.wikimedia.org/wiki/File:Falcon_Heavy_test_flight.webm

The landing runs from 215.5 s to about 234 s of that source, with a cut to a
different camera at 235 s — any window must end before it.

### Exact recipe

    tools/mkvideoband.py fh.webm -o splash_falconheavy.bin \
        --start 222.0 --duration 10 \
        --anchor 0.50 --anchor-end 0.60 --anchor-settle 0.50 \
        --brightness 0.80 --contrast 1.05 --quality 82

`--anchor-end` is not cosmetic. The band keeps only ~30% of a 16:9 frame's
height and the boosters fall through roughly 45% of it, so a fixed crop cannot
hold both the descent and the touchdown.

**Getting the landing in frame is the fiddly part, and the first cut got it
wrong.** The source camera tracks and zooms during the landing: the ground line
sits at ~63-65% of frame height at t=226-227 s and rises to ~55% by t=228 s. A
band that is not low enough for the earlier, lower ground line puts the pads on
its bottom edge, and the boosters then descend *out of* the strip instead of
landing in it — which is exactly what the 1.2.9 cut (`--anchor-end 0.62
--anchor-settle 0.65`) did. Settling at 0.50 lands the pan on the touchdown
itself rather than after it, and the band spans roughly 42-72% of frame height
through the landing, with ground visible beneath the pads.

The grading (`--brightness`, `--contrast`, and the `--cap` default of `0x9A`)
is mandatory, not taste: this sits behind white title text on the boot screen
of a launch controller and the text has to win on contrast. Ungraded footage
screams over it.

Result: 199,110 B, 1,982 B/frame average, 2,620 B peak — 9.5% of the 2 MB
partition.
