#!/usr/bin/env python3
"""
Fit an RLC battery divider from a vbat-cal capture.

Reads a serial log produced by tools/vbat-cal, finds the stable plateaus where
a bench supply was held at a setpoint, pairs them in order with the reference
voltages measured at the board terminals, and fits the divider.

Log formats understood (TT-02, 2026-08-27):

  MEDIAN <raw> | mean <n> spread <n> (min <n> max <n>) pin <n> mV ...
      What tools/vbat-cal emits today. The MEDIAN column is the raw count used
      for the fit and "pin ... mV" is the calibrated pin voltage.

  CSV,<seq>,<raw>,<rmin>,<rmax>,<mv>,<held>
  PLATEAU,<idx>,<raw>,<mv>,<n>,<held_ms>
      An older vbat-cal that emitted machine-readable records directly. Still
      parsed so historic captures keep working; nothing produces these now.

Until this was fixed the parser only knew the CSV/PLATEAU forms and exited
"No CSV records found" on every real capture, so only the manual --pairs path
worked.

Two models are reported:

  gain-only    vbat = adc_mv * ratio          (what rlc_battery.c implements)
  gain+offset  vbat = adc_mv * ratio + offset (needs a code change to use)

The offset model exists to answer a question, not because it is preferred: if
the fitted offset is negligible, the gain-only model stands and only
*_VBAT_DIVIDER_RATIO needs correcting. A large offset means something
systematic is going on and should be understood before it is papered over.

Usage:
  tools/vbat_fit.py capture.log --refs 8000 9000 10000 11000 12000 12600
  tools/vbat_fit.py capture.log --list-plateaus      # check alignment first
"""

import argparse
import re
import sys

CSV_RE = re.compile(r'^CSV,(\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+)')
PLATEAU_RE = re.compile(r'^PLATEAU,(\d+),(-?\d+),(-?\d+),(\d+),(-?\d+)')
# TT-02: the live vbat-cal line. Anchored on the labels rather than on field
# positions so the trailing "[clipped]" / "** OVER RANGE **" annotations and
# the two vbat@ratio columns do not have to be described here.
MEDIAN_RE = re.compile(
    r'MEDIAN\s+(\d+)\s*\|\s*mean\s+(\d+)\s+spread\s+(\d+)\s*'
    r'\(min\s+(\d+)\s+max\s+(\d+)\)\s*pin\s+(\d+)\s*mV')
# A reading pinned at full scale is an over-range indication, not a
# measurement — vbat-cal says so in the line itself, and it must never reach
# a least-squares fit.
ADC_FULL_SCALE = 4095


def read_records(path):
    """Continuous trace records, and the firmware's own accepted plateaus.

    The firmware decides what counts as held (a dwell inside a tolerance band),
    so its PLATEAU records are authoritative when present. The continuous trace
    is kept for diagnosis and as a fallback.
    """
    recs, plats = [], []
    skipped_railed = 0
    with open(path, 'r', errors='replace') as fh:
        for line in fh:
            m = MEDIAN_RE.search(line)
            if m:
                raw = int(m.group(1))
                if raw >= ADC_FULL_SCALE - 5:
                    skipped_railed += 1
                    continue
                recs.append({
                    'seq': len(recs), 'raw': raw,
                    'rmin': int(m.group(4)), 'rmax': int(m.group(5)),
                    'mv': int(m.group(6)), 'held': 0,
                })
                continue
            m = CSV_RE.search(line)
            if m:
                recs.append({
                    'seq': int(m.group(1)), 'raw': int(m.group(2)),
                    'rmin': int(m.group(3)), 'rmax': int(m.group(4)),
                    'mv': int(m.group(5)), 'held': int(m.group(6)),
                })
                continue
            m = PLATEAU_RE.search(line)
            if m:
                plats.append({
                    'idx': int(m.group(1)), 'raw': float(m.group(2)),
                    'mv': float(m.group(3)), 'n': int(m.group(4)),
                    'held_ms': int(m.group(5)), 'spread': 0,
                })
    if skipped_railed:
        print(f'note: dropped {skipped_railed} over-range reading(s) at/near '
              f'ADC full scale ({ADC_FULL_SCALE})')
    return recs, plats


def find_plateaus(recs, tol_counts, min_len):
    """Group consecutive records whose raw average stays within tol_counts.

    Transitions between setpoints are discarded: a run has to be at least
    min_len records long to count, which is what separates 'held steady' from
    'passing through while the knob turned'.
    """
    plateaus, run = [], []
    for r in recs:
        if not run:
            run = [r]
            continue
        base = run[0]['raw']
        if abs(r['raw'] - base) <= tol_counts:
            run.append(r)
        else:
            if len(run) >= min_len:
                plateaus.append(run)
            run = [r]
    if len(run) >= min_len:
        plateaus.append(run)

    out = []
    for run in plateaus:
        # Drop the first record of each run: it can straddle the transition.
        body = run[1:] if len(run) > min_len else run
        n = len(body)
        raw = sum(x['raw'] for x in body) / n
        mv = sum(x['mv'] for x in body) / n
        spread = max(x['rmax'] for x in body) - min(x['rmin'] for x in body)
        out.append({'n': n, 'raw': raw, 'mv': mv, 'spread': spread})
    return out


def lstsq(xs, ys):
    """Least squares y = a*x + b."""
    n = len(xs)
    sx, sy = sum(xs), sum(ys)
    sxx = sum(x * x for x in xs)
    sxy = sum(x * y for x, y in zip(xs, ys))
    denom = n * sxx - sx * sx
    if denom == 0:
        return None, None
    a = (n * sxy - sx * sy) / denom
    b = (sy - a * sx) / n
    return a, b


def fit_through_origin(xs, ys):
    sxx = sum(x * x for x in xs)
    return sum(x * y for x, y in zip(xs, ys)) / sxx if sxx else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('logfile', nargs='?',
                    help='vbat-cal capture log (omit when using --pairs)')
    ap.add_argument('--pairs', nargs='*', default=[],
                    help='hand-noted RAW:REFERENCE_MV pairs, e.g. 636:8010 713:8990')
    ap.add_argument('--ratio-nominal', type=float, default=None,
                    help='nominal divider ratio, for comparison in the summary')
    ap.add_argument('--refs', nargs='*', type=float, default=[],
                    help='reference voltages in mV, in the order captured')
    ap.add_argument('--tol', type=int, default=10,
                    help='plateau tolerance in raw ADC counts (default 10)')
    ap.add_argument('--min-len', type=int, default=5,
                    help='minimum records per plateau (default 5)')
    ap.add_argument('--list-plateaus', action='store_true')
    ap.add_argument('--rederive', action='store_true',
                    help='ignore the firmware PLATEAU records and re-detect')
    args = ap.parse_args()

    # Hand-noted pairs: the operator swept the supply and read raw counts off
    # the terminal against a DVM. No log parsing involved.
    if args.pairs:
        plats, refs = [], []
        for spec in args.pairs:
            if ':' not in spec:
                sys.exit(f'Bad pair "{spec}" — expected RAW:REFERENCE_MV, e.g. 636:8010')
            raw_s, ref_s = spec.split(':', 1)
            try:
                raw_v, ref_v = float(raw_s), float(ref_s)
            except ValueError:
                sys.exit(f'Bad pair "{spec}" — both parts must be numbers')
            if raw_v >= 4090:
                sys.exit(f'Pair "{spec}" is at or near ADC full scale (4095). '
                         f'That is an over-range reading, not a measurement — drop it.')
            plats.append({'raw': raw_v, 'mv': 0.0, 'n': 1})
            refs.append(ref_v)
        print(f'{len(plats)} hand-noted pairs\n')
        print(f'{"#":>3} {"raw":>9} {"ref_mV":>9}')
        for i, (p, r) in enumerate(zip(plats, refs)):
            print(f'{i:>3} {p["raw"]:>9.1f} {r:>9.1f}')
        print()
        report(plats, refs, args, raw_only=True)
        return

    if not args.logfile:
        sys.exit('Give a logfile, or use --pairs RAW:REFERENCE_MV ...')

    recs, fw_plats = read_records(args.logfile)
    if not recs and not fw_plats:
        sys.exit('No usable records found. Expected MEDIAN lines (current '
                 'vbat-cal) or CSV,/PLATEAU, lines (historic captures). '
                 'Is this a vbat-cal log?')
    print(f'{len(recs)} trace records read from {args.logfile}')

    if fw_plats and not args.rederive:
        plats = fw_plats
        print(f'{len(plats)} plateaus accepted by the firmware '
              f'(use --rederive to re-detect from the trace instead)\n')
    else:
        plats = find_plateaus(recs, args.tol, args.min_len)
        print(f'{len(plats)} plateaus re-derived from the trace '
              f'(tolerance {args.tol} counts, minimum {args.min_len} records)\n')

    print(f'{"#":>3} {"n":>5} {"raw":>9} {"adc_mv":>8}')
    for i, p in enumerate(plats):
        print(f'{i:>3} {p["n"]:>5} {p["raw"]:>9.1f} {p["mv"]:>8.1f}')
    print()

    if args.list_plateaus or not args.refs:
        print('Re-run with --refs <mV> ... once the plateau count matches your '
              'setpoints.')
        return

    if len(args.refs) != len(plats):
        sys.exit(f'MISMATCH: {len(plats)} plateaus but {len(args.refs)} reference '
                 f'values.\nCheck alignment with --list-plateaus, or adjust --tol '
                 f'/ --min-len, before trusting any fit.')

    report(plats, list(args.refs), args, raw_only=False)


def report(plats, ys, args, raw_only):
    # ---- Primary: reference volts against RAW counts -------------------
    # This bypasses esp_adc_cal entirely and folds the whole chain (ADC gain,
    # offset and divider) into one fit. If its residuals are as flat as the
    # calibrated-mV fit below, it is the simpler and more robust model.
    xr = [p['raw'] for p in plats]
    ar, br = lstsq(xr, ys)
    print('=== Model 0: reference vs RAW counts (bypasses esp_adc_cal) ===')
    print(f'  mv_per_count = {ar:.6f}, offset = {br:+.1f} mV')
    worst_raw = 0.0
    for x, y in zip(xr, ys):
        pred = ar * x + br
        err = pred - y
        worst_raw = max(worst_raw, abs(err))
        print(f'   ref {y:8.1f} mV   raw {x:8.1f}   predicted {pred:8.1f}   '
              f'error {err:+7.1f} mV ({100*err/y:+.2f} %)')
    print(f'  worst-case error: {worst_raw:.1f} mV\n')

    if raw_only:
        print('=== Recommendation ===')
        print(f'  Fit against raw counts: vbat_mV = {ar:.6f} * raw {br:+.1f}')
        print(f'  Worst-case error across the sweep: {worst_raw:.1f} mV')
        if abs(br) < 30:
            print(f'  Offset is negligible, so a pure scale works: '
                  f'vbat_mV = raw * {(sum(ys)/sum(p["raw"] for p in plats)):.6f}')
        else:
            print(f'  Offset {br:+.1f} mV is material — rlc_battery.c currently has no')
            print(f'  offset term, so adopting this needs a code change.')
        if args.ratio_nominal:
            print(f'\n  For comparison against the nominal {args.ratio_nominal} divider:')
            print(f'  the implied pin voltage per count and the resulting effective')
            print(f'  ratio depend on the ADC reference, so compare end-to-end error')
            print(f'  rather than the ratio alone.')
        return

    xs = [p['mv'] for p in plats]

    ratio_only = fit_through_origin(xs, ys)
    a, b = lstsq(xs, ys)

    print('=== Model 1: gain-only on calibrated mV (what the firmware implements) ===')
    print(f'  ratio = {ratio_only:.4f}')
    worst = 0.0
    for x, y in zip(xs, ys):
        pred = x * ratio_only
        err = pred - y
        worst = max(worst, abs(err))
        print(f'   ref {y:8.1f} mV   predicted {pred:8.1f}   error {err:+7.1f} mV '
              f'({100*err/y:+.2f} %)')
    print(f'  worst-case error: {worst:.1f} mV\n')

    print('=== Model 2: gain + offset ===')
    print(f'  ratio = {a:.4f}, offset = {b:+.1f} mV')
    worst2 = 0.0
    for x, y in zip(xs, ys):
        pred = a * x + b
        err = pred - y
        worst2 = max(worst2, abs(err))
        print(f'   ref {y:8.1f} mV   predicted {pred:8.1f}   error {err:+7.1f} mV '
              f'({100*err/y:+.2f} %)')
    print(f'  worst-case error: {worst2:.1f} mV\n')

    print('=== Recommendation ===')
    print(f'  worst-case error — raw fit {worst_raw:.1f} mV, '
          f'gain-only {worst:.1f} mV, gain+offset {worst2:.1f} mV')
    if worst_raw < worst * 0.5:
        print('  The raw-count fit is markedly better, which means esp_adc_cal is')
        print('  contributing error on this chip. Consider fitting raw directly in')
        print('  rlc_battery.c rather than going through adc_cali_raw_to_voltage().')
    if abs(b) < 30 or worst2 > worst * 0.7:
        print(f'  Offset is small or buys little. Keep the gain-only model and set')
        print(f'  the divider ratio to {ratio_only:.4f}.')
    else:
        print(f'  The offset is material ({b:+.1f} mV) and cuts worst-case error from')
        print(f'  {worst:.1f} to {worst2:.1f} mV. Understand its source before adopting')
        print(f'  it; if genuine, rlc_battery.c needs an offset term.')


if __name__ == '__main__':
    main()
