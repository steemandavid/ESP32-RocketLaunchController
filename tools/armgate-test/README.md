# armgate-test — arm-relay AND-gate verifier

Standalone bring-up firmware for the **base** unit. Proves the hardware AND
gate of FSD §5.4.4 **electrically, at the ARM SENSE node**, rather than from
the indicator LEDs.

## Why

Bug #28 and its sibling — the ARM RELAY LED lighting with the key in SAFE, and
the arm-key red/green LEDs lighting together — were both sneak paths in the
*indicator* wiring, fixed by rework on 2026-08-26. The LEDs are normally how an
operator judges arm relay state, so immediately after touching that wiring the
LEDs are exactly what must not be used as the instrument. Run this after any
rework of the arm relay, key switch or indicator wiring.

## What it checks

| Step | Key | GPIO 47 | Expect |
|---|---|---|---|
| 0 | SAFE→ARM→SAFE | — | KEY SENSE (GPIO 42) tracks the key |
| 1 | SAFE | driven | relay out, ARM SENSE (GPIO 21) = 0 |
| 2 | ARM | low | relay out, ARM SENSE = 0 |
| 3 | ARM | driven | relay in, ARM SENSE = 1, coil LED lit |
| 4 | ARM | low again | relay releases, ARM SENSE back to 0 |
| 5 | ARM→SAFE | driven | key alone breaks the coil, ARM SENSE = 0 |

Steps 1–3 are the three rows that matter. The other three cover failures those
rows cannot see:

- **Step 0** validates the instrument. KEY SENSE is what every later step uses
  to know where the key is; a stuck input would otherwise turn the whole run
  into a silent no-op that reports PASS.
- **Step 4** catches a relay that pulls in and never lets go.
- **Step 5** reaches the key-SAFE case from a relay that was just energised,
  with the software leg still asserted. The key switch leg is the one that has
  to hold with the ESP32 crashed or unpowered, so it is worth proving from both
  directions.

Each step samples ARM SENSE every 10 ms for 2 s after a 150 ms settle. A line
that cannot hold a steady level fails whichever level was expected — a
marginal sneak path is not an interlock you can reason about.

The operator moves the key; the firmware moves GPIO 47. Key position is read
from KEY SENSE, so the program waits for the right position instead of asking
anyone to type. The only keyboard input is ENTER at the safety prompt and y/n
for "is the coil LED lit".

## Safety

**Disconnect all igniters before running.** This firmware energises the arm
relay, which puts VBAT on the fire bus. All eight channel relays are driven
de-energised at boot and held there, so no current can reach an igniter — but
the interlock is what is under test, so do not rely on it.

Have a meter on the ARM SENSE node for step 3, at least once.

## Run

```
cd tools/armgate-test
./run.sh
```

`run.sh` sources the ESP-IDF environment itself, so it works from a fresh
shell — the same convention as `./build_base.sh` in the project root. It
defaults to the base unit's by-id port and checks the port exists before
building; override with `./run.sh -p <by-id>`.

Exit the monitor with **Ctrl-]**.

The default by-id is the base board as of 2026-08-26 — confirm with
`ls /dev/serial/by-id/`, and see the by-id caveat in `Development_Progress.md`:
a COM by-id identifies the CH340 adapter on that board, so it survives a chip
swap but not a board swap.

<details><summary>Without the wrapper</summary>

```
source ~/esp/esp-idf/export.sh
idf.py -p /dev/serial/by-id/usb-1a86_USB_Single_Serial_5B5E042156-if00 flash monitor
```
</details>

Reset the board to run the sequence again.

### Console

Output goes to **UART0 — the CH340 bridge**, not the ESP32-S3's native
USB-Serial/JTAG port. That matches the RLC firmware itself (UART0 primary
console, USB-Serial/JTAG secondary) and the `usb-1a86_USB_Single_Serial_*`
by-id both boards are reached on.

A tool built the other way round *looks like a boot loop*: the ROM banner
still reaches UART0 so you see repeated `rst:0x1 (POWERON)` blocks, but
nothing the application prints ever arrives. If you see that symptom in any
tool in this repo, check `CONFIG_ESP_CONSOLE_*` before suspecting the
hardware.

**Reflash the real firmware when finished:** `./build_base.sh flash`
