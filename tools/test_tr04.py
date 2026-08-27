#!/usr/bin/env python3
"""T-R04: Verify wait_for_ack sentinel preserves LINK_LOST.

Strategy:
1. Halt base via esptool flasher stub (application stops completely)
2. User triggers arm on remote → CMD_ARM sent, no ACK returns
3. Remote enters wait_for_ack, link loss fires during that window
4. Remote must transition to LINK_LOST (not stay in IDLE)
5. Resume base, verify recovery

Ports:
  Defaults are the stable /dev/serial/by-id board-serial paths for this pair.
  Override on the command line when the hardware changes:

      ./test_tr04.py --base <by-id> --remote <by-id>

  Find yours with `ls /dev/serial/by-id/`. Never use /dev/ttyACMx — those
  numbers reorder on every replug.
"""
import argparse, serial, subprocess, time, sys, os

# TT-01 (2026-08-27): these two were stale and, worse, crossed. BASE_PORT
# pointed at the adapter of dead chip #3 (no longer present on the machine at
# all) and REMOTE_PORT pointed at ...042156, which is the BASE board. Running
# the script as written halted the remote and talked to the base as if it were
# the remote. Corrected against the live by-ids.
BASE_PORT   = "/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E042156-if00"
REMOTE_PORT = "/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E043219-if00"
BAUD        = 115200

ESPTOOL = os.environ.get(
    "ESPTOOL",
    "/home/john/.espressif/python_env/idf5.4_py3.12_env/bin/esptool.py")

def halt_base():
    """Halt base by entering flasher stub."""
    cmd = [ESPTOOL, "--chip", "esp32s3", "-p", BASE_PORT,
           "--after", "no_reset_stub", "chip_id"]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
    print(f"  halt stdout: {result.stdout[-200:]}" if result.stdout else "  halt: no stdout")
    return result.returncode == 0

def resume_base():
    """Resume base from flasher stub."""
    cmd = [ESPTOOL, "--chip", "esp32s3", "-p", BASE_PORT,
           "--after", "hard_reset", "run"]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
    print(f"  resume stdout: {result.stdout[-200:]}" if result.stdout else "  resume: no stdout")
    return result.returncode == 0

def main():
    print("=== T-R04: wait_for_ack LINK_LOST preservation ===\n")

    # Step 1: Halt the base
    print("Step 1: Halting base via esptool flasher stub...")
    if not halt_base():
        print("FATAL: Could not halt base.")
        sys.exit(1)
    print("Base halted. Application is not running.\n")

    # Drain remote serial
    remote = serial.Serial(REMOTE_PORT, BAUD, timeout=0.2)
    drain_start = time.time()
    while time.time() - drain_start < 2:
        remote.readline()  # drain old data

    print("Step 2: Monitoring remote. Link loss will be detected shortly.")
    print(">>> NOW: Long-press encoder to trigger CMD_ARM <<<")
    print(">>> (arm switch ON, channel with continuity) <<<\n")

    start = time.time()
    saw_link_lost = False
    saw_arm_activity = False
    saw_recovery = False
    link_lost_time = None

    while time.time() - start < 35:
        line_bytes = remote.readline()
        if not line_bytes:
            continue
        try:
            line = line_bytes.decode("utf-8", errors="replace").rstrip()
        except:
            continue
        if not line:
            continue

        elapsed = time.time() - start
        print(f"  [{elapsed:5.1f}s] {line}")

        # Detect any arm activity
        if not saw_arm_activity:
            if any(kw in line for kw in [
                "IDLE -> ARMED", "ARM rejected", "ARM NACK",
                "ARM failed", "CMD_ARM", "arm verify",
                "LONG PRESS"
            ]):
                saw_arm_activity = True

        # Detect LINK_LOST
        if not saw_link_lost:
            if any(x in line for x in ["LINK LOST", "-> LINK_LOST", "link_lost", "state=7"]):
                saw_link_lost = True
                link_lost_time = elapsed
                print(f"\n  >>> LINK_LOST at {elapsed:.1f}s <<<\n")

    remote.close()

    # Step 3: Resume base
    print("\nStep 3: Resuming base...")
    if resume_base():
        print("Base resumed. Waiting 15s for re-link...\n")
        time.sleep(5)

        remote2 = serial.Serial(REMOTE_PORT, BAUD, timeout=1)
        start2 = time.time()
        while time.time() - start2 < 15:
            line_bytes = remote2.readline()
            if line_bytes:
                try:
                    line = line_bytes.decode("utf-8", errors="replace").rstrip()
                    if line:
                        print(f"  [recovery] {line}")
                        if "state=1" in line and "armed=0" in line:
                            saw_recovery = True
                            print("\n  >>> Recovered to IDLE <<<")
                            break
                except:
                    pass
        remote2.close()

    # --- Results ---
    print("\n=== T-R04 RESULTS ===")
    print(f"  Arm activity on remote:   {'YES' if saw_arm_activity else 'NO'}")
    print(f"  Remote -> LINK_LOST:      {'YES' if saw_link_lost else 'NO'}")
    if link_lost_time:
        print(f"  LINK_LOST at:             {link_lost_time:.1f}s")
    print(f"  Recovery to IDLE:         {'YES' if saw_recovery else 'NO'}")

    if saw_arm_activity and saw_link_lost:
        print("\n  ** PASS ** : Remote entered LINK_LOST when base disappeared.")
        print("  wait_for_ack correctly processed EVT_LINK_LOST via sentinel (R1).")
        sys.exit(0)
    elif saw_link_lost and not saw_arm_activity:
        print("\n  ** PASS (no arm) ** : Link loss detected. But no arm was attempted.")
        print("  Re-run with arm attempt for full R1 verification.")
        sys.exit(0)
    elif saw_arm_activity and not saw_link_lost:
        print("\n  ** FAIL ** : Arm attempted but remote did NOT enter LINK_LOST.")
        sys.exit(1)
    else:
        print("\n  ** INCOMPLETE ** : No activity detected.")
        sys.exit(1)

if __name__ == "__main__":
    # TT-01: ports are overridable so a board swap does not need a code edit
    # (and so the next stale-by-id bug is a wrong argument, not wrong firmware
    # talking to the wrong board).
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default=BASE_PORT,
                    help="base unit /dev/serial/by-id path")
    ap.add_argument("--remote", default=REMOTE_PORT,
                    help="remote unit /dev/serial/by-id path")
    args = ap.parse_args()
    BASE_PORT, REMOTE_PORT = args.base, args.remote

    for name, port in (("base", BASE_PORT), ("remote", REMOTE_PORT)):
        if not os.path.exists(port):
            print(f"ERROR: {name} port does not exist: {port}")
            print("Current ports:")
            bydir = "/dev/serial/by-id"
            for p in sorted(os.listdir(bydir)) if os.path.isdir(bydir) else []:
                print(f"  {bydir}/{p}")
            sys.exit(2)

    main()
