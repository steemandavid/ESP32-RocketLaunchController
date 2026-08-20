# Gotron shopping list — RLC hardware

Supplier: **Gotron** (www.gotron.be), Belgian, 3 stores (Aalst / Gent / Hasselt).
Daily dispatch 14:30, free delivery from €100.
Compiled 2026-08-19, header strips added 2026-08-20. Prices are the discounted
web prices at those dates.

## Selected items

| Qty | Part code | Description | Unit | Line | Purpose |
|---|---|---|---|---|---|
| 5 | `BAT85` | Schottky diode 30 V / 0.2 A, DO-35 | €0.21 | €1.05 | ADC/GPIO overvoltage clamp — bug #22 (remote GPIO 1) + spares for the base GPIO 21/42 clamps (bug #18) |
| 2 | `R10W/10` | 10 Ω 10 W ±5 % wirewound cement, Ø10×49 mm | €0.62 | €1.24 | Igniter substitute — **GOOD** band |
| 4 | `R50W/8E2` | 8.2 Ω 50 W ±5 % power resistor | €5.37 | €21.48 | Igniter substitute — **GOOD** band, arc-realistic (4 in parallel = 2.05 Ω, 6.1 A) |
| 2 | `R11W220` | 220 Ω 11 W wirewound ceramic, SETA RB58, Ø9×46 mm | €1.75 | €3.50 | Igniter substitute — **MARGINAL** band |
| 4 | `30PF1` | Rechte connectorrij 1×20 pin, **vrouwelijk**, P 2,54 mm (0.1" female header strip) | €0.79 | €3.16 | Sockets for the test resistors / harness, dev-board mounting |

**Total: €30.43** (5 line items, 17 pieces)

Product URLs:

- BAT85 — https://www.gotron.be/signal-diode-schotky-diode-30v-0-2a-do35.html ([datasheet](https://www.gotron.be/media/files/Downloads/BAT85.pdf))
- R10W/10 — https://www.gotron.be/10watt-10-ohm-draadgewikkelde-weerstand.html
- R50W/8E2 — https://www.gotron.be/componenten/passief/weerstanden/25w-50w/50watt-vermogenweerstand-5-8-2-ohm.html
- R11W220 — https://www.gotron.be/componenten/passief/weerstanden/draad/draadgewikkelde-ceramische-weerstand-220ohm-11w-seta-rb58.html
- 30PF1 — https://www.gotron.be/rechte-connectorrij-1x20-pin-vrouwelijk-p2-54.html

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
| 4× R50W/8E2 in parallel (2.05 Ω) | 2.0 mV | GOOD | realistic e-match value | 6.1 A total, ~19 W each — well inside 50 W. Reproduces the ~6 A that destroyed two base ESP32s (bug #18), for snubber/clamp validation |
| 1× R50W/8E2 (8.2 Ω) | 8.2 mV | GOOD | — | 1.54 A, 19.4 W |
| R11W220 (220 Ω) | 206 mV | MARGINAL | 3× above the GOOD edge, 7× below OPEN | 57 mA, 0.72 W |

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

**Availability caveat.** The 11 W SETA RB58 line is marked OBSOLETE / "while
stock lasts"; 220 Ω showed >20 at Aalst only (Gent/Hasselt 1–2 days). Order
these when ordering the rest.

## Optional additions (not ordered — codes recorded for later)

| Part code | Description | Price | Reads as | Use |
|---|---|---|---|---|
| `R5W/22` | 22 Ω 5 W wirewound | €0.37 | 21.9 mV, GOOD | gentler GOOD substitute (0.57 A, 7.2 W) |
| `R10W/100` | 100 Ω 10 W wirewound | €0.50 | 97 mV, MARGINAL | sits only 1.5× over the GOOD/MARGINAL edge — hysteresis / threshold test |
| `R11W1K` | 1 kΩ 11 W SETA RB58 | €1.75 | 761 mV, MARGINAL | near-OPEN edge test |
| `R25W/3K3` | 3.3 kΩ 25 W | €3.10 | 1.62 V, OPEN | OPEN simulator (only 1 in stock at Aalst) |
| `1N5711` | Schottky 70 V / 15 mA DO-35, 200 nA leakage | €0.54 | — | lowest-leakage clamp option; only 1 piece in stock |
| `1N4148` | Si signal diode 75 V / 200 mA DO-35 | €0.12 | — | nA-leakage interim clamp, clamps at ~4.0 V instead of ~3.6 V |
| `30PF2` | Rechte connectorrij 2×20 pin, vrouwelijk, P 2,54 | €0.80 | — | dual-row female header, if a 2×n socket is ever needed |
