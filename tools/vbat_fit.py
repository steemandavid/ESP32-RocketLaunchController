#!/usr/bin/env python3
"""
Fit an RLC battery divider from a vbat-cal capture.

Reads a serial log produced by tools/vbat-cal (lines beginning "CSV,"),
finds the stable plateaus where a bench supply was held at a setpoint, pairs
them in order with the reference voltages measured at the board terminals,
and fits the divider.

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

CSV_RE = re.compile(r'^CSV,(\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+),')


def read_records(path):
    recs = []
    with open(path, 'r', errors='replace') as fh:
        for line in fh:
            m = CSV_RE.search(line)
            if m:
                recs.append({
                    'seq': int(m.group(1)),
                    'raw': int(m.group(2)),
                    'rmin': int(m.group(3)),
                    'rmax': int(m.group(4)),
                    'mv': int(m.group(5)),
                })
    return recs


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
    ap.add_argument('logfile')
    ap.add_argument('--refs', nargs='*', type=float, default=[],
                    help='reference voltages in mV, in the order captured')
    ap.add_argument('--tol', type=int, default=10,
                    help='plateau tolerance in raw ADC counts (default 10)')
    ap.add_argument('--min-len', type=int, default=5,
                    help='minimum records per plateau (default 5)')
    ap.add_argument('--list-plateaus', action='store_true')
    args = ap.parse_args()

    recs = read_records(args.logfile)
    if not recs:
        sys.exit('No CSV records found. Is this a vbat-cal capture?')
    print(f'{len(recs)} records read from {args.logfile}')

    plats = find_plateaus(recs, args.tol, args.min_len)
    print(f'{len(plats)} plateaus detected '
          f'(tolerance {args.tol} counts, minimum {args.min_len} records)\n')

    print(f'{"#":>3} {"n":>4} {"raw":>8} {"adc_mv":>8} {"spread":>7}')
    for i, p in enumerate(plats):
        print(f'{i:>3} {p["n"]:>4} {p["raw"]:>8.1f} {p["mv"]:>8.1f} {p["spread"]:>7}')
    print()

    if args.list_plateaus or not args.refs:
        print('Re-run with --refs <mV> ... once the plateau count matches your '
              'setpoints.')
        return

    if len(args.refs) != len(plats):
        sys.exit(f'MISMATCH: {len(plats)} plateaus but {len(args.refs)} reference '
                 f'values.\nCheck alignment with --list-plateaus, or adjust --tol '
                 f'/ --min-len, before trusting any fit.')

    xs = [p['mv'] for p in plats]
    ys = list(args.refs)

    ratio_only = fit_through_origin(xs, ys)
    a, b = lstsq(xs, ys)

    print('=== Model 1: gain-only (what the firmware implements) ===')
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
    if abs(b) < 30 or worst2 > worst * 0.7:
        print(f'  Offset is small or buys little. Keep the gain-only model and set')
        print(f'  the divider ratio to {ratio_only:.4f}.')
    else:
        print(f'  The offset is material ({b:+.1f} mV) and cuts worst-case error from')
        print(f'  {worst:.1f} to {worst2:.1f} mV. Understand its source before adopting')
        print(f'  it; if genuine, rlc_battery.c needs an offset term.')


if __name__ == '__main__':
    main()
