# Remote unit VBAT sense — 2026-08-19 — CALIBRATION ABORTED (bug #21)

Reference: DVM at the board terminals, bench supply swept 5.33–8.57 V.
Firmware: `tools/vbat-cal` (UART console build). Raw counts mapped through this
chip's own `adc_cali` curve, captured in `remote_adc_curve_2026-08-19.log`.

**No calibration was applied.** The sense circuit is non-linear, and a
non-linear fault cannot be corrected with a gain constant.

## Measurements

| DVM (V) | raw | pin mV (via adc_cali) | implied ratio | ideal 2.8:1 pin | shortfall |
|---|---|---|---|---|---|
| 5.33 | 2034 | 1729.5 | 3.082 | 1904 | −174 |
| 5.79 | 2136 | 1815.0 | 3.190 | 2068 | −253 |
| 6.27 | 2227 | 1891.2 | 3.315 | 2239 | −348 |
| 6.77 | 2309 | 1960.0 | 3.454 | 2418 | −458 |
| 7.56 | 2412 | 2045.0 | 3.697 | 2700 | −655 |
| 8.00 | 2463 | 2086.5 | 3.834 | 2857 | −771 |
| 8.57 | 2521 | 2135.5 | 4.013 | 3061 | −925 |

The implied ratio drifts 30 % across the sweep. A resistive divider is linear by
definition, so this is not a resistor-value error — see bug #21 in
Development_Progress.md.

## Contrast with the base

The base unit calibrated cleanly on the same day with the same method: implied
ratio 4.285–4.442 (a 3.7 % spread attributable to ADC compression near full
scale), fitting to 0.70 % worst-case in its operating band. The remote's 30 %
drift is an entirely different magnitude and direction, and is not explained by
ADC behaviour — the remote's pin never rises above 2136 mV, well inside the
linear region where the base's ADC behaved well.

That contrast is itself evidence: the same measurement technique, the same
chip family and the same ADC configuration produced a clean result on one unit
and a badly non-linear one on the other.

## Not applied

`REMOTE_VBAT_DIVIDER_RATIO` remains at its nominal `2.8f`. Restoring the FSD
§5.6.2 production thresholds (7000 / 6600 / 6400) is **blocked** until the sense
circuit is fixed: with the current fault, every voltage in the 2S range reads
below CRITICAL and the remote would boot straight into STATE_ERROR.


---

# Re-run after removing the zener — CALIBRATION SUCCEEDED

The 3.3 V zener was removed from the ADC node (no replacement clamp fitted
yet). DVM at the pin then read **2.86 V with 8.05 V in** against an ideal
2875 mV — the divider behaves exactly as specified, confirming the zener was
the entire fault.

Re-swept 4.94–8.56 V. The bench supply was noisy (600–1500 counts of sample
spread, with individual samples clipping at full scale), so `tools/vbat-cal`
was changed to report a **median** of 129 samples rather than a mean: the
median is immune to spikes and to clipping, and proved 2× more stable
(14 counts of line-to-line variation versus 31 for the mean).

| DVM (V) | raw (median) | pin mV | implied ratio | % of ADC ceiling |
|---|---|---|---|---|
| 4.94 | 2041 | 1735.2 | 2.8469 | 55 % |
| 5.46 | 2267 | 1924.2 | 2.8375 | 61 % |
| 5.93 | 2462 | 2086.0 | 2.8428 | 66 % |
| 6.40 | 2666 | 2253.0 | 2.8407 | 71 % |
| 6.96 | 2921 | 2456.5 | 2.8333 | 78 % |
| 7.56 | 3213 | 2672.5 | 2.8288 | 85 % |
| 8.00 | 3465 | 2842.5 | 2.8144 | 90 % |
| 8.56 | 3838 | 3054.5 | 2.8024 | 97 % |

Implied ratio now spans **1.6 %** (was 30 % with the zener), declining with
voltage — the ADC-compression signature, same direction and magnitude as the
base.

| Model | Ratio | Offset | Worst case |
|---|---|---|---|
| gain-only, all points | 2.8261 | — | 72 mV |
| gain+offset | 2.7533 | +179 mV | 29 mV |
| **gain-only, operating band (≥6.3 V)** | **2.8211** | — | **57 mV** |

**Adopted: 2.8211**, gain-only, same methodology and same reasoning as the
base — the offset model fits better numerically but is a straight line
absorbing ADC curvature, physically unexplained, and would extrapolate badly.

## Threshold verification

With ratio 2.8211 and FSD §5.6.2 production thresholds restored:

| True | Firmware reports | Error | Behaviour |
|---|---|---|---|
| 6400 (CRITICAL) | 6356 | −44 | trips correctly |
| 6600 (MIN_OPERATE) | 6561 | −39 | correct |
| 7000 (MIN_ARM) | 6971 | −29 | blocks arming |
| 8400 (full pack) | 8446 | +46 | arming allowed |

Every error near a threshold is negative, so protection trips early rather
than late.

## Still open

- **No clamp fitted on GPIO 1.** The zener was removed and not replaced. A
  BAT54 (or BAT85 / BAT43 / 1N5711) to the 3.3 V rail is still required — see
  bug #21. A 1N5819 is *not* suitable: as a 1 A power Schottky its reverse
  leakage would reintroduce the same class of error, pulling readings up
  rather than down.
- **ADC headroom.** A full 2S pack sits at 97 % of the ADC ceiling. Rescaling
  to 3.0 kΩ / 1.2 kΩ (ratio 3.5) would put it near 76 % and require re-running
  this calibration.
