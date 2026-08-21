# Gotron shopping list — RLC hardware

Supplier: **Gotron** (www.gotron.be), Belgian, 3 stores (Aalst / Gent / Hasselt).
Daily dispatch 14:30, free delivery from €100.
Compiled 2026-08-19. Revised 2026-08-20: female header strips added; R5W/22 and
R10W/100 promoted to the order; R50W/8E2 dropped to the optional list. Prices
are the discounted web prices at those dates.

## Selected items

| Qty | Part code | Description | Unit | Line | Purpose |
|---|---|---|---|---|---|
| 5 | `BAT85` | Schottky diode 30 V / 0.2 A, DO-35 | €0.21 | €1.05 | ADC/GPIO overvoltage clamp — bug #22 (remote GPIO 1) + spares for the base GPIO 21/42 clamps (bug #18) |
| 2 | `R10W/10` | 10 Ω 10 W ±5 % wirewound cement, Ø10×49 mm | €0.62 | €1.24 | Igniter substitute — **GOOD** band |
| 2 | `R5W/22` | 22 Ω 5 W ±5 % wirewound | €0.37 | €0.74 | Igniter substitute — **GOOD** band, low-current |
| 2 | `R11W220` | 220 Ω 11 W wirewound ceramic, SETA RB58, Ø9×46 mm | €1.75 | €3.50 | Igniter substitute — **MARGINAL** band |
| 2 | `R10W/100` | 100 Ω 10 W ±5 % wirewound cement | €0.50 | €1.00 | Igniter substitute — **MARGINAL**, sits just over the GOOD/MARGINAL edge (threshold + hysteresis test) |
| 4 | `30PF1` | Rechte connectorrij 1×20 pin, **vrouwelijk**, P 2,54 mm (0.1" female header strip) | €0.79 | €3.16 | Sockets for the test resistors / harness, dev-board mounting |
| 10 | `RC220E` | 220 Ω 1 % metal film, 1/4 W | €0.12 | €1.20 | Sense-branch series resistor, one per channel + spares — limits the fault current the 3V3 clamp injects into the rail (bug #18) |
| 2 | `TL431` | Shunt regulator 2,495–36 V ±2 %, TO-92 | €1.07 | €2.14 | 3.3 V rail clamp at ~3.57 V (bug #24) |
| 2 | `RC4K3` | 4,3 kΩ 1 % metal film, 1/4 W | €0.12 | €0.24 | TL431 upper divider |
| 2 | `RC10K` | 10 kΩ 1 % metal film, 1/4 W | €0.12 | €0.24 | TL431 lower divider |

**Total: €14.51** (10 line items, 33 pieces)

Product URLs:

- BAT85 — https://www.gotron.be/signal-diode-schotky-diode-30v-0-2a-do35.html ([datasheet](https://www.gotron.be/media/files/Downloads/BAT85.pdf))
- R10W/10 — https://www.gotron.be/10watt-10-ohm-draadgewikkelde-weerstand.html
- R5W/22 — https://www.gotron.be/5watt-22-ohm-draadgewikkelde-weerstand.html
- R10W/100 — https://www.gotron.be/10watt-100-ohm-draadgewikkelde-weerstand.html
- R11W220 — https://www.gotron.be/componenten/passief/weerstanden/draad/draadgewikkelde-ceramische-weerstand-220ohm-11w-seta-rb58.html
- 30PF1 — https://www.gotron.be/rechte-connectorrij-1x20-pin-vrouwelijk-p2-54.html
- RC220E — https://www.gotron.be/rc220e-r1-220-ohm-metaalfilmweerstand-1-4-watt.html
- TL431 — https://www.gotron.be/u-shunt-reg-2-495-36v-2-to92.html
- RC4K3 — https://www.gotron.be/rc4k3-r1-4-3-kohm-metaalfilmweerstand-1-4-watt.html
- RC10K — https://www.gotron.be/rc10k-r1-10-kohm-metaalfilmweerstand-1-4-watt.html

## Notes on the selection

**BAT85 as the bug #22 clamp.** Anode to the divider centre / ADC pin, cathode to
3V3. Gotron does **not** stock BAT54, BAT54S or BAT43. The 1N5711 (lower leakage,
200 nA max) is stocked but only **1 piece** was available, so BAT85 is the
practical choice. Its datasheet I_R of 2 µA is specified at 25–30 V reverse; in
this position the diode sits at only ~0.3–1 V reverse, where actual leakage is
roughly an order of magnitude lower. Against the present 6429 Ω Thevenin source
the budget is <3.1 µA for <20 mV of error, so BAT85 should pass — but it is the
candidate that could drift at elevated temperature (leakage ≈ doubles per 10 °C).

> Fit the bug #23 divider rescale (3.0 kΩ / 1.2 kΩ, 857 Ω) at the same time if
> convenient — it relaxes the leakage requirement 7.5× and removes the question
> entirely. Either way, re-run `tools/vbat-cal` after fitting: a flat implied
> ratio means the part is fine, drift means it is leaking.

Do **not** substitute a 1N5819 (stocked, €0.21) — as a 1 A power Schottky its
leakage biases readings *upward*, masking a flat pack.

**Continuity band boundaries** (computed from `CONT_*_UV` in
`components/rlc_common/include/rlc_config.h` through the real sense network:
3.3 V, R_ref 3.3 kΩ, R_pull 100 kΩ). Note these are **not** the 20 Ω / 500 Ω the
FSD glossary states:

| Boundary | Threshold | Actual resistance |
|---|---|---|
| SHORT / GOOD | 500 µV | 0.50 Ω |
| GOOD / MARGINAL | 66 000 µV | 67.4 Ω |
| MARGINAL / OPEN | 1 500 000 µV | 2.83 kΩ |

Where the selected parts land, and what they dissipate on a 1 s fire pulse at
12.6 V (3S full charge):

| Part | Sense reading | Band | Margin | Fire pulse |
|---|---|---|---|---|
| R10W/10 (10 Ω) | 9.97 mV | GOOD | 6.6× below the MARGINAL edge, 20× above SHORT | 1.26 A, 15.9 W → 16 J, safe transiently on a 10 W cement body |
| R5W/22 (22 Ω) | 21.9 mV | GOOD | 3× below the MARGINAL edge, 44× above SHORT | 0.57 A, 7.2 W → 7 J, comfortable on a 5 W body for 1 s |
| R11W220 (220 Ω) | 206 mV | MARGINAL | 3× above the GOOD edge, 7× below OPEN | 57 mA, 0.72 W |
| R10W/100 (100 Ω) | 97 mV | MARGINAL | only 1.5× over the GOOD edge — deliberate boundary case | 126 mA, 1.6 W |

**No arc-realistic load in this order.** The 8.2 Ω 50 W `R50W/8E2` (4 in parallel
= 2.05 Ω, 6.1 A) was considered and dropped — it is the only combination here
that reproduces the ~6 A a real e-match draws, i.e. the current that destroyed
two base ESP32s under bug #18. Everything ordered draws 1.26 A or less, so it
exercises the continuity bands and the fire path but **does not validate the
channel-1 snubber and clamps at realistic arc energy**. Add it (code retained
below) before treating a fire test as representative.

**Safety.** Never leave a sub-ohm resistor connected through a fire pulse — 0.1 Ω
across 12.6 V is 126 A, far beyond the relay contacts and any sane pack
discharge. Low-value parts are for exercising the SHORT band on the *continuity*
path only, with the channel relay de-energised.

**Female 0.1" header strips.** `30PF1` is a 1×20 socket strip on 2.54 mm pitch,
>20 in stock at all three stores. It is **not** pre-scored for breaking
(*afbreekbaar: nee*), so cut to length with a fine saw or knife — the usual
trick of sacrificing one position applies. Four strips give 80 sockets, enough
to build a plug-in igniter-substitute harness for all eight channels plus
spares. Gotron's female range is single-row `30PF1` (1×20) and dual-row `30PF2`
(2×20); the 1×40 `40PF1` is a **male** strip despite the similar code, so do not
order it by mistake.

**Why 1 % on the 220 Ω.** The sense-branch resistor sets a fixed ~206 mV floor on
every channel, and the band thresholds are derived from it. A ±5 % part spreads
that floor by ±10 mV, which is ±11 Ω on a 67 Ω boundary — 17 %. `RC220E` at 1 %
holds it to ±2 Ω. The cheaper 1 W 5 % metal oxide (`R1W/220`, €0.12) is usable
only if you hand-sort a matched set of eight against a DVM. Fault dissipation is
~0.37 W for the duration of a pulse, which a 1/4 W part survives transiently;
`R1W/220` is the safer choice on that count alone, so sorting a matched set of 1 W
parts is a legitimate alternative.

**TL431 rail clamp, not a zener.** V_clamp = 2.495 × (1 + R1/R2) = 2.495 ×
(1 + 4k3/10k) = **3.57 V**. Cathode to 3V3, anode to GND, REF to the R1/R2
mid-point, 10 nF from REF to anode, mounted at the DevKit's 3V3 pin. Standing
draw ~235 µA; sinks up to 100 mA with a knee a few mV wide. The `BZX83C3V6`
zener (€0.17) is **not** an adequate substitute in a 3.3 V / 3.6 V window: ±5 %
tolerance spans 3.42–3.78 V and it needs ~3.9 V to sink 40 mA.

**Availability caveat.** The 11 W SETA RB58 line is marked OBSOLETE / "while
stock lasts"; 220 Ω showed >20 at Aalst only (Gent/Hasselt 1–2 days). Order
these when ordering the rest.

## Bug #27 — base siren driver (needed, part codes NOT yet looked up)

Added 2026-08-21. The base siren is not connected: GPIO 40 drives nothing and
the IRLZ44N driver has not been fitted, so the pad currently has **no audible
warning during ARMED / PRE_FIRE / FIRING**. See Development_Progress bug #27
and FSD §5.4.8.

The design already calls for 10 IRLZ44N (8 channels + arm relay + siren), so
check the parts bin before ordering — the siren MOSFET may already be on hand
even though it is not installed.

| Qty | Part | Purpose |
|---|---|---|
| 1 | IRLZ44N (or equivalent logic-level N-channel MOSFET) | Low-side siren switch, GPIO 40 |
| 1 | 150 Ω 1/4 W | Gate series resistor (GPIO → gate) |
| 1 | 10 kΩ 1/4 W | **Gate pull-down (gate → GND) — boot safety, not optional** |
| 1 | 1N4007, or 1N5819 / SS14 | Flyback diode across the siren coil (cathode VBAT+, anode drain) |
| 1 | 12 V siren / sounder | The device itself, if not already sourced |

> **Part codes and prices are deliberately blank** — they have not been checked
> against Gotron stock, and every other row in this document carries a verified
> code. Look them up before ordering rather than trusting a guess here. Note
> `RC10K` (10 kΩ, €0.12) is already on the order above for the TL431 divider,
> so that one line may just need its quantity raised.

## Optional additions (not ordered — codes recorded for later)

| Part code | Description | Price | Reads as | Use |
|---|---|---|---|---|
| `R50W/8E2` | 8.2 Ω 50 W power resistor | €5.37 | 8.2 mV, GOOD | 4 in parallel = 2.05 Ω / 6.1 A — arc-realistic fire load, ~19 W each |
| `R11W1K` | 1 kΩ 11 W SETA RB58 | €1.75 | 761 mV, MARGINAL | near-OPEN edge test |
| `R25W/3K3` | 3.3 kΩ 25 W | €3.10 | 1.62 V, OPEN | OPEN simulator (only 1 in stock at Aalst) |
| `1N5711` | Schottky 70 V / 15 mA DO-35, 200 nA leakage | €0.54 | — | lowest-leakage clamp option; only 1 piece in stock |
| `1N4148` | Si signal diode 75 V / 200 mA DO-35 | €0.12 | — | nA-leakage interim clamp, clamps at ~4.0 V instead of ~3.6 V |
| `30PF2` | Rechte connectorrij 2×20 pin, vrouwelijk, P 2,54 | €0.80 | — | dual-row female header, if a 2×n socket is ever needed |
