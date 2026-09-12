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
        --start 222.0 --duration 10 --track \
        --zoom 1.0 --zoom-end 2.6 --zoom-settle 0.55 --zoom-final 1.15 \
        --track-bias-y -0.10

`--track` and the push-in are what make this shot work, and both took a
couple of wrong turns worth recording.

**Tracking.** The band is 6:1 and keeps only ~30% of a 16:9 frame's height at
zoom 1, while the boosters fall through far more than that and the camera pans
*and* zooms to follow them. No fixed or hand-panned crop holds the subject for
ten seconds; `--track` follows it per frame.

The detector keys on **brightness, not colour**. Warmth (R−B) is the obvious
choice and is a trap: the flames are blown out to near-white, so R ≈ B, and an
R−B test locks onto dark red vegetation instead. The plumes are simply the
brightest things in the shot, and a threshold that floats with the frame mean
finds them in both dusk and daylight.

Once the engines cut and the smoke thins there is **nothing left to track**,
and the detector will happily lock onto sunlit roads and buildings near the
horizon — which it did, throwing the crop to the bottom of the frame for the
last two seconds. `--track-area` holds the crop when the detected subject
falls below a floor. Holding is not a fallback here, it is correct: the pads
do not move.

**Selection is the bounding-box centre, not the centroid.** The two agree
while the subject is two symmetrical plumes and diverge badly once it is a
smoke column: the area-weighted centroid is dragged down into the dense base of
the cloud, so the crop sits low, fills its lower half with ground and cuts the
top off the billow.

**The push-in uses a smoothstep, not an ease-out.** An ease-out zoom does
almost all its travel in the first moment and then creeps, so the shot is
already tight before anything has happened — the opposite of what a push-in is
for. `--zoom-settle 0.55` aims the end of the move at the touchdown itself.

**`--zoom-final` pulls back out afterwards**, for two reasons. The subject
keeps growing — the smoke column quickly becomes far larger than the tight
framing that suited two descending boosters, so holding the peak zoom crops its
top off. And the firmware loops the clip (fw 1.2.11), so ending near the
opening width is what stops the wrap being a visible jump.

The zoom can be this aggressive (1.0 → 2.6) only because the subject *shrinks*:
the two boosters span ~37% of frame height while still high and far apart, and
under 8% at touchdown once the camera has widened. Early frames clip the upper
booster's nose, which is unavoidable — a 6:1 letterbox cannot hold two rockets
separated diagonally without zooming out past the point of the exercise. The
4K source means even zoom 2.6 still oversamples the 480-pixel band about 3×.

The grading (`--saturation`, `--brightness`, `--contrast`, and the `--cap`
default of `0x9A`) is mandatory, not taste: this sits behind white title text
on the boot screen of a launch controller and the text has to win on contrast.
Ungraded footage screams over it.

Result: 175,281 B, 1,744 B/frame average, 2,996 B peak — 8.4% of the 2 MB
partition.
