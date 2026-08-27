# ESP32 Wireless Rocket Launch Controller — Project Summary

Dear club members,

I'd like to introduce you to a project I've been working on: a custom **wireless rocket launch controller** built around the ESP32-S3 microcontroller. This document gives you an overview of what the system does, how it works, and — most importantly — the safety measures built into it.

I'm sharing this to keep everyone in the loop and to **ask for your feedback**. Whether you spot something we missed, have ideas for improvements, or just want to understand the system better, your input is welcome.

---

## What Is It?

The system consists of two separate units that communicate wirelessly:

- **Base Unit** — sits at the launch pad, directly connected to the igniters (up to 8 channels).
- **Remote Unit** — handheld controller that the launch operator carries at a safe distance (~200 m).

The operator selects which igniter channel to fire, goes through a deliberate arming procedure, and then presses and holds a fire button to ignite the motor. Releasing the button immediately cuts power to the igniter (dead-man switch).

Both units run on rechargeable LiPo batteries. The base unit is powered by a 3S 5000 mAh pack (enough current headroom for low-resistance igniters), and the remote by a 2S 2200 mAh pack.

---

## Arming & Launch Procedure

The launch sequence requires **multiple deliberate steps** — there is no single action that can fire an igniter:

1. Power on both units. They link up automatically over a wireless connection.
2. The operator **selects a channel** (1–8) using a rotary encoder on the remote.
3. Someone at the pad **turns a physical key switch** on the base unit to the ARM position.
4. The operator flips a **physical arm/disarm switch** on the remote.
5. The operator **long-presses the encoder button** (500 ms) to send the arm command.
6. The base unit checks all safety conditions (see below) and, if everything passes, energizes the arm relay. A siren at the pad starts sounding and stays on for the rest of the sequence.
7. The operator **presses and holds the fire button**. The base enters a 5-second pre-fire countdown, siren still sounding.
8. After the countdown, the channel relay closes and current flows to the igniter for a fixed 1-second fire pulse.
9. **Releasing the fire button at any time** during steps 6–8 immediately cuts power to the igniter.
10. After the fire pulse, all relays are de-energised, the system returns to idle, and the igniter is checked for a successful burn (open circuit = fired).

---

## Safety Features (Defense-in-Depth)

This is the part I'd most like your feedback on. The system was designed so that **no single hardware or software fault can cause unintended ignition**. Here are the layers:

### Hardware Interlocks

- **Three independent break points** in the fire path: the physical key switch, the arm relay contacts, and the per-channel relay. All three must be closed for current to reach the igniter.
- **Hardware AND gate on the arm relay**: the arm relay requires *both* the physical key switch AND a software-controlled MOSFET to energize. Either one alone does nothing.
- **Fail-safe defaults**: all relays are de-energised by default (NC position). 10 kΩ pulldown resistors on MOSFET gates ensure relays stay off until the firmware explicitly turns them on — even during boot, before software starts running.
- **Passive status LEDs**: three LEDs at the base unit run directly from battery power (no microcontroller involved), so you can always see whether the key is in SAFE or ARM position, and whether the arm relay is energized — even if the ESP32 is completely crashed or unpowered.
- **Current-limited continuity sensing**: igniter continuity is checked with a very small current (~1 mA, through two resistors in series), so checking continuity cannot accidentally fire an igniter.
- **All switches are active-low**: if a wire comes loose or breaks, the system interprets that as "safe" / "disarmed" / "button released."

### Software Interlocks

- **10 guard conditions** must all pass before the arm relay can energize:
  1. Physical key switch in ARM position (verified by a separate GPIO, not just assumed)
  2. Selected channel has continuity (open circuit = blocked)
  3. Selected channel number is in range (1–8)
  4. No other channel is already armed
  5. Message integrity verified (CRC32-C over the frame with a pre-shared key)
  6. Session token matches the one issued at link-up
  7. Sequence number is newer than the last one seen (anti-replay)
  8. Battery voltage above minimum (10.5 V on base)
  9. The channel has the bug-#18 hardware protection fitted (ADC clamp + snubber)
  10. Communication link quality is acceptable (≤30 % ping failures in the last 10)

  Followed by a confirmation, not a guard: **arm sense feedback** must report
  the arm relay actually closed within 200 ms, or the ARM is refused and the
  relay de-energised. The same signal detects welded contacts.
- **Continuity is watched for the whole time the pad is armed**, not just at the moment of arming. If the igniter on the armed channel goes open-circuit — someone pulls a lead, a clip falls off — the base disarms itself and silences the siren within about a second. This was added in August 2026 after bench testing showed the pad staying armed with a disconnected igniter.
- **Dead-man switch**: during firing, the remote sends repeated "fire alive" messages. If the base stops receiving them (operator released the button, or link was lost), power is cut.
- **Auto-disarm**: if the system is armed but no fire command is received within 10 seconds, it disarms automatically.
- **Contact welding detection**: if the arm relay is supposed to be off but the feedback GPIO reads that it's still conducting, the system enters a permanent error state that requires a power cycle to clear.
- **Unrecoverable error state**: if anything truly unexpected happens (welded relay, multiple channels armed simultaneously, etc.), the system locks up in an error state. The only way out is to physically walk to the base unit and cycle power. We consider this safer than trying to "self-heal."

### Communication Security

The wireless link between the two units is protected by three independent layers:

1. **AES-128-CCM encryption** (built into ESP-NOW) — prevents external devices from injecting commands.
2. **Application-layer CRC32-C integrity check** with a compile-time key — detects corrupted or forged messages.
3. **Replay protection** using monotonically increasing sequence numbers and a random session token generated at each link-up — prevents replaying a previously captured "fire" command.

**One honest caveat on the above.** The project is developed in the open, and
the keys for layers 1 and 2 are currently in the public source repository. Until
they are rotated and kept out of the repo, those two layers protect against
accidental interference and casual radio traffic, but not against someone who
has read the source. Layer 3 is unaffected — its session token is generated
randomly at each link-up, so a captured command cannot be replayed later. This
is tracked and will be fixed before the system is used in the field; I mention
it because overstating the security would be worse than fixing it quietly.

Firmware version mismatch between the two units also prevents operation — both must run the same version.

---

## Current Status

The firmware covers all core functionality:

- Wireless link with encryption, integrity, and replay protection
- 8-channel continuity sensing with real-time feedback
- Full arming and firing state machines with all safety interlocks
- Battery monitoring on both units
- Siren, buzzer, and RGB LED status indicators
- 12 boot-time self-test suites
- LCD display on the remote (480×320 colour screen): status, continuity grid,
  arming/firing screens, and error/NACK messages

**What's not done yet:**

- On-target verification of the display screens (the driver runs and the panel
  reports healthy; the layouts have not been checked by eye yet)
- Field hardening and environmental testing
- Enclosure / physical build

Earlier in development the base unit's ESP32 was destroyed during a fire test, when a relay arc coupled back into the microcontroller's sensing inputs. That is fixed: the software now de-energizes the relays in the safe order, and every one of the eight channels has the protection hardware fitted (snubbers across the relay contacts, clamp diodes and a limiting resistor on each sense input). The pad siren, the arm indicator LEDs and the channel status lights are all working as of August 2026.

The controller is now in its final test campaign — the full arming and firing test suite, plus range and endurance testing. Channels 2–8 have never been fired in anger, so the first shot on each will be treated as a test in its own right.

---

## Questions for the Club

I'd appreciate your thoughts on:

1. **Safety**: Did we miss any failure modes? Are there additional interlocks you'd want to see?
2. **Features**: Is there anything you'd like the controller to do that it doesn't do today? (e.g., support for cluster ignition, longer fire pulses for composite motors, etc.)
3. **Operational workflow**: Does the arming procedure feel right — too many steps, too few, or about right?
4. **Physical design**: Any preferences for enclosure layout, connector types, or ergonomics?
5. **Compliance**: Are there NAR/TRA safety code requirements we should explicitly address?

Feel free to reply with questions, concerns, or ideas. Thanks for taking the time to review this!

— John
