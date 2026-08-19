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
