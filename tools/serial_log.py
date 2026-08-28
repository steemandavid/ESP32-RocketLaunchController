#!/usr/bin/env python3
"""Timestamped serial logger for RLC bench sessions.

Handles the failure that cost a capture in the 2026-08-28 session: a unit
reset re-enumerates its USB adapter, the old fd goes stale, and the logger
keeps "capturing" nothing. This reopens the port after SILENCE_S without
data, and stamps every line with the host wall-clock so dual-console
captures can be correlated offline.

Usage:
  ./serial_log.py -p /dev/serial/by-id/... -o base.log [--send-on PATTERN:BYTES]
  --send-on watches the SAME port for PATTERN (regex) and writes BYTES to it
  once, then re-arms — for fault-injection keys that must land inside a
  window too tight to hand-time (T-S09/T-S16 style).
"""
import argparse
import datetime
import re
import serial  # pyserial

SILENCE_S = 20  # reopen after this long with no data


def stamp():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port", required=True)
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--send-on", action="append", default=[],
                    help="PATTERN:BYTES — on regex match, write BYTES once, re-arm")
    args = ap.parse_args()

    rules = []
    for spec in args.send_on:
        pat, data = spec.split(":", 1)
        rules.append([re.compile(pat), data.encode(), False])

    with open(args.out, "a", buffering=1) as log:
        while True:
            try:
                ser = serial.Serial(args.port, args.baud, timeout=SILENCE_S)
            except serial.SerialException as e:
                log.write(f"{stamp()} [logger] open failed: {e}; retry in 2 s\n")
                import time; time.sleep(2)
                continue
            log.write(f"{stamp()} [logger] opened {args.port}\n")
            try:
                while True:
                    line = ser.readline()
                    if not line:
                        log.write(f"{stamp()} [logger] {SILENCE_S}s silence — reopening\n")
                        break
                    text = line.decode(errors="replace").rstrip("\r\n")
                    log.write(f"{stamp()} {text}\n")
                    for r in rules:
                        if r[0] is not None and r[0].search(text):
                            ser.write(r[1])
                            log.write(f"{stamp()} [logger] sent {r[1]!r} on /{r[0].pattern}/\n")
                            r[0], r[2] = None, True  # one-shot per rule
            except serial.SerialException as e:
                log.write(f"{stamp()} [logger] port died: {e}; reopening\n")
            finally:
                try: ser.close()
                except Exception: pass


if __name__ == "__main__":
    main()
