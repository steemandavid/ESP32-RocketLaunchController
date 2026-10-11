#!/usr/bin/env python3
"""RLC base unit schematic -> SVG (+ PNG via cairosvg), in the style of the CRIG node schematic.

Source: FSD RLC-FSPEC-001 v1.75 §5.4 / §5.6 / App. C.1, the as-built tables in
Development_Progress.md, and the hand-drawn base schematic
(netwerkdrive Rocketry/ESP32 Rocket Launch Controller/IMG_20260827_143820_001.jpg)
as confirmed by the owner on 2026-10-11 (zeners fitted, XT30 igniter sockets). The
drawing's '1k3' continuity resistor is a 1k8: the 2026-08-23 known-load measurements
only fit 3.3 k total. Drawing primitives live in tools/schematic_svg.py.
Regenerate after any hardware change:

    pip install cairosvg
    python tools/base_schematic.py docs/reference/RLC_base_schematic
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schematic_svg import *  # noqa: E402,F401,F403

OUT = sys.argv[1] if len(sys.argv) > 1 else 'RLC_base_schematic'
W, H = 1910, 1270

# =================================================================== canvas
begin(W, H, 'RLC base unit — schematic',
      'ESP32 Rocket Launch Controller · as built 2026-10-11 · fw 1.2.21 · '
      'FSD v1.75 §5.4/§5.6/App. C.1 + as-built tables + hand-drawn base schematic (2026-08-27)')

# =================================================================== A · POWER
frame(20, 60, 600, 430, 'A · POWER, 5 V BUCK, BATTERY SENSE')
rect(40, 100, 140, 80)
text(110, 124, 'BT1  3S LiPo', 13, 'middle', True)
text(110, 140, '5000 mAh · 100C', 11, 'middle')
text(110, 154, '11.1 V nom · 12.6 V full', 11, 'middle')
text(110, 168, 'XT60', 10, 'middle', fill=GREY)
line(110, 180, 110, 200)
gnd(110, 200)
text(110, 236, 'no hardware UV cut-off (bug #25)', 10, 'middle', fill=GREY, italic=True)
# SW1
wire([(180, 140), (232, 140)], C_VBAT)
line(232, 140, 268, 126)
ocirc(232, 140)
ocirc(268, 140)
text(250, 162, 'SW1 on/off', 11, 'middle')
text(250, 176, 'red plate above key', 10, 'middle', fill=GREY)
wire([(268, 140), (470, 140)], C_VBAT, 2.5)
dot(310, 140, C_VBAT)
dot(470, 140, C_VBAT)
wire([(470, 140), (560, 140)], C_VBAT, 2.5)
tag(565, 136, 'VBAT+', C_VBAT, size=13)
text(565, 152, '9.0–12.6 V', 10, fill=GREY)
# U2 LM2596 buck module
wire([(310, 140), (310, 250)], C_VBAT)
rect(230, 250, 170, 90)
text(315, 276, 'U2  LM2596S buck', 12, 'middle', True)
text(315, 292, 'DC-DC module', 10, 'middle')
text(315, 308, 'VBAT → 5.0 V', 11, 'middle', True, C_5V)
text(238, 264, 'IN', 9, fill=GREY)
text(392, 316, 'OUT', 9, 'end', fill=GREY)
text(315, 334, 'GND', 9, 'middle', fill=GREY)
line(315, 340, 315, 356)
gnd(315, 356)
wire([(400, 312), (430, 312), (430, 392), (500, 392)], C_5V, 2.5)
tag(506, 388, '+5V', C_5V)
text(506, 404, '→ U1 5V0', 10, fill=GREY)
text(506, 417, '→ WS2812B VDD', 10, fill=GREY)
# battery divider
res(470, 140, 470, 236, C_VBAT)
text(482, 192, 'R 33 k', 11)
dot(470, 236, C_SENSE)
res(470, 236, 470, 330)
text(482, 288, 'R 10 k', 11)
gnd(470, 330)
wire([(470, 236), (580, 236)], C_SENSE)
tag(526, 230, 'VBAT_ADC', C_SENSE, size=11)
text(526, 250, '→ IO1', 10, fill=C_SENSE)
lines(40, 440, ['+3V3 is not a separate supply: it is the dev board’s own LDO (U1 3V3 pin), fed from +5V.',
                'Divider 4.3 : 1 → 0–3.3 V ADC for 0–14.2 V. No TL431 clamp on the 3.3 V rail (bug #24).'],
      10, fill=GREY, italic=True)

# =================================================================== B · MCU
frame(640, 60, 380, 780, 'B · MCU (ESP32-S3 dev board)')
BX, BY, BW, BH = 780, 110, 160, 690
rect(BX, BY, BW, BH)
text(BX + BW / 2, 420, 'U1', 18, 'middle', True)
lines(BX + BW / 2, 442, ['ESP32-S3-', 'DevKitC-1', 'WROOM-1', 'N16R8'], 11, 'middle', dy=14)
lines(BX + BW / 2, 514, ['2.4 GHz ext. ant.', '(U.FL)', 'USB, USB-JTAG', '(case open)'], 10, 'middle', fill=GREY, dy=13)
lines(BX + BW / 2, 586, ['spare:', 'IO38 IO39 IO41'], 10, 'middle', fill=GREY, dy=13)

left = [('IO1', 'VBAT_ADC', C_SENSE), ('IO2', 'ADC_CH1', C_SENSE), ('IO10', 'ADC_CH2', C_SENSE),
        ('IO4', 'ADC_CH3', C_SENSE), ('IO5', 'ADC_CH4', C_SENSE), ('IO6', 'ADC_CH5', C_SENSE),
        ('IO7', 'ADC_CH6', C_SENSE), ('IO8', 'ADC_CH7', C_SENSE), ('IO9', 'ADC_CH8', C_SENSE),
        ('IO21', 'ARM_SENSE', C_SENSE), ('IO42', 'KEY_SENSE', C_SENSE), ('3V3', '+3V3  (out)', C_3V3),
        ('5V0', '+5V', C_5V)]
right = [('IO11', 'RLY_CH1', C_GATE), ('IO12', 'RLY_CH2', C_GATE), ('IO13', 'RLY_CH3', C_GATE),
         ('IO14', 'RLY_CH4', C_GATE), ('IO15', 'RLY_CH5', C_GATE), ('IO16', 'RLY_CH6', C_GATE),
         ('IO17', 'RLY_CH7', C_GATE), ('IO18', 'RLY_CH8', C_GATE), ('IO47', 'ARM_RLY', C_GATE),
         ('IO40', 'SIREN', C_GATE), ('IO48', 'WS2812_DIN', C_DATA), ('GND', 'GND', INK)]
step = 50
for i, (pin, netname, c) in enumerate(left):
    y = BY + 40 + i * step
    wire([(660, y), (BX, y)], c, 2)
    text(BX + 6, y + 4, pin, 10, fill=GREY)
    text(664, y - 6, netname, 11, bold=True)
for i, (pin, netname, c) in enumerate(right):
    y = BY + 40 + i * step
    text(BX + BW - 6, y + 4, pin, 10, 'end', fill=GREY)
    if netname == 'GND':
        wire([(BX + BW, y), (BX + BW + 40, y)])
        gnd(BX + BW + 40, y)
        continue
    wire([(BX + BW, y), (1014, y)], c, 2)
    text(1014, y - 6, netname, 11, 'end', True)
text(BX + 6, BY + 40 + 9 * step + 18, 'arm relay contact', 9, fill=GREY)
text(BX + 6, BY + 40 + 10 * step + 18, 'key position', 9, fill=GREY)
text(BX + BW - 6, BY + 40 + 10 * step + 18, '+ on-board LED', 9, 'end', fill=GREY)
lines(660, 812, ['All analogue inputs on ADC1 (ADC2 is unusable while ESP-NOW runs).'], 10, fill=GREY, italic=True)

# =================================================================== C · ARM INTERLOCK
frame(1040, 60, 850, 500, 'C · ARM INTERLOCK (key switch AND IO47 → K_ARM) + FIRE BUS')
# key switch S2 (SPDT, shown at SAFE)
KX = 1150
tag(KX, 100, 'VBAT+', C_VBAT, 'middle')
wire([(KX, 106), (KX, 136)], C_VBAT)
ocirc(KX, 140)
ocirc(KX - 26, 182)
ocirc(KX + 26, 182)
line(KX, 144, KX - 23, 178)
text(KX + 34, 130, 'S2 key switch', 12, bold=True)
text(KX + 34, 144, 'SPDT, shown SAFE', 10, fill=GREY)
text(KX - 32, 172, 'NC', 10, 'end', fill=GREY)
text(KX + 32, 176, 'NO', 10, fill=GREY)
# NC -> SAFE lamp
wire([(KX - 26, 186), (KX - 26, 230)], C_VBAT)
diode(KX - 26, 230, KX - 26, 270, 'led', C_VBAT, body=C_LAMP_G)
res(KX - 26, 270, KX - 26, 316)
gnd(KX - 26, 316)
text(KX - 58, 248, 'SAFE', 11, 'end', True, C_LAMP_G)
text(KX - 58, 262, 'lamp', 10, 'end', fill=GREY)
text(KX - 40, 298, '220 Ω', 10, 'end')
# NO -> KEY_NO
wire([(KX + 26, 186), (KX + 26, 220)], C_KEY, 2.5)
dot(KX + 26, 210, C_KEY)
diode(KX + 26, 220, KX + 26, 266, 'led', C_KEY, body=C_LAMP_R, side=-1)
res(KX + 26, 266, KX + 26, 316)
gnd(KX + 26, 316)
text(KX + 62, 248, 'ARM', 11, bold=True, fill=C_LAMP_R)
text(KX + 62, 262, 'lamp', 10, fill=GREY)
text(KX + 40, 298, '470 Ω', 10)
CX = 1380   # K_ARM coil x
wire([(KX + 26, 210), (KX + 110, 210)], C_KEY, 2.5)
diode(KX + 110, 210, KX + 160, 210, 'std', C_KEY)
wire([(KX + 160, 210), (1550, 210)], C_KEY, 2.5)
tag(KX + 40, 204, 'KEY_NO', C_KEY, size=11)
text(KX + 135, 194, 'D series', 9, 'middle', fill=GREY)
text(KX + 172, 204, 'COIL+', 10, bold=True, fill=C_KEY)
dot(CX, 210, C_KEY)
coil(CX, 210, 330, 'K_ARM', C_KEY)
dot(CX, 330)
flyback(CX, 210, 330, 40)
dot(CX + 40, 210, C_KEY)
# HOT lamp across coil
HX = CX + 100
dot(HX, 210, C_KEY)
diode(HX, 210, HX, 262, 'led', C_KEY, body=C_LAMP_R, side=-1)
res(HX, 262, HX, 330)
wire([(HX, 330), (CX + 40, 330)])
text(HX + 36, 236, 'HOT', 11, bold=True, fill=C_LAMP_R)
text(HX + 36, 250, 'lamp', 10, fill=GREY)
text(HX - 14, 304, '470 Ω', 10, 'end')
nmos(CX, 330, 'Q_ARM')
gate_drive(CX - 34, 360, 'ARM_RLY', 'IO47', 170)
# key-sense divider
SX = 1550
res(SX, 210, SX, 290, C_KEY)
text(SX + 12, 254, '27 k', 10)
dot(SX, 300, C_SENSE)
wire([(SX, 290), (SX, 300)])
res(SX, 300, SX, 380)
text(SX + 12, 344, '10 k', 10)
gnd(SX, 380)
wire([(SX, 300), (SX + 46, 300)], C_SENSE)
tag(SX + 8, 294, 'KEY_SENSE', C_SENSE, size=11)
text(SX + 50, 304, '→ IO42', 10, fill=C_SENSE)
wire([(SX, 300), (SX - 30, 300)])
dot(SX, 300, C_SENSE)
diode(SX - 30, 380, SX - 30, 300, 'zener')
gnd(SX - 30, 380)
text(SX - 30, 410, '3V3 Z', 9, 'middle')

# K_ARM contact (SPDT, shown at rest: COM on NC)
AX = 1700
tag(AX + 30, 92, 'VBAT+', C_VBAT, 'middle')
wire([(AX + 30, 98), (AX + 30, 146)], C_VBAT, 2.5)
dot(AX + 30, 120, C_VBAT)
ocirc(AX + 30, 150)          # NO
ocirc(AX - 30, 150)          # NC (unused)
ocirc(AX, 196)               # COM
line(AX, 192, AX - 27, 154)
text(AX + 40, 160, 'NO', 10, fill=GREY)
text(AX - 40, 146, 'NC', 10, 'end', fill=GREY)
text(AX - 40, 160, '(unused)', 9, 'end', fill=GREY)
text(AX - 10, 206, 'COM', 10, 'end', fill=GREY)
text(AX - 52, 116, 'K_ARM contact', 12, 'end', True)
text(AX - 52, 130, '12 V auto SPDT, 20 A', 10, 'end', fill=GREY)
wire([(AX, 200), (AX, 240)], C_FIRE, 3.5)
dot(AX, 240, C_FIRE)
# snubber across NO-COM
wire([(AX + 30, 120), (AX + 90, 120)], C_VBAT)
res(AX + 90, 120, AX + 90, 180)
cap(AX + 90, 180, AX + 90, 240, INK)
wire([(AX + 90, 240), (AX, 240)], C_FIRE, 3.5)
dot(AX + 90, 240, C_FIRE)
text(AX + 104, 154, 'R', 10)
text(AX + 104, 210, 'C', 10)
text(AX + 104, 224, 'RC snubber: fitted,', 9, fill=GREY)
text(AX + 104, 235, 'value not recorded', 9, fill=GREY)
# fire bus out
wire([(AX + 90, 240), (AX + 175, 240)], C_FIRE, 3.5)
dot(AX + 175, 240, C_FIRE)
tag(AX + 100, 262, 'FIRE_BUS', C_FIRE, size=13)
text(AX + 100, 277, '→ D (all 8 NO)', 10, fill=GREY)
# arm-sense divider
res(AX, 240, AX, 340, C_FIRE)
text(AX - 12, 294, '27 k', 10, 'end')
dot(AX, 350, C_SENSE)
wire([(AX, 340), (AX, 350)])
res(AX, 350, AX, 420)
text(AX - 12, 390, '10 k', 10, 'end')
gnd(AX, 420)
wire([(AX, 350), (AX - 110, 350)], C_SENSE)
tag(AX - 110, 344, 'ARM_SENSE', C_SENSE, size=11)
text(AX - 110, 366, '→ IO21', 10, fill=C_SENSE)
wire([(AX, 350), (AX + 36, 350)])
diode(AX + 36, 420, AX + 36, 350, 'zener')
gnd(AX + 36, 420)
text(AX + 50, 388, '3V3 Z', 9)
# TVS not fitted on fire bus
wire([(AX + 175, 240), (AX + 175, 350)], NF, 1.5, '4 3')
diode(AX + 175, 420, AX + 175, 350, 'zener', NF, '4 3')
gnd(AX + 175, 420, NF)
text(AX + 163, 382, 'TVS SMBJ18A', 9, 'end', fill=GREY)
text(AX + 163, 394, 'not fitted', 9, 'end', fill=GREY)

lines(1056, 470, ['Fire path has TWO breaks in series: the K_ARM contact and the channel relay contact (D).',
                  'S2 is NOT in the fire path: it breaks K_ARM’s coil current, in series with Q_ARM — a hardware AND gate.'],
      12, fill=C_LAMP_R, bold=False)
lines(1056, 512, ['SAFE / ARM / HOT lamps are passive and work with U1 unpowered.  '
                  'HOT = K_ARM coil energised = FIRE_BUS live.',
                  '27 k / 10 k + 3V3 zener on IO21 (contact) and IO42 (key). D series sits between the key and the K_ARM coil.'],
      10, fill=GREY, italic=True)

# =================================================================== D · IGNITER CHANNEL
frame(1040, 580, 850, 670, 'D · IGNITER CHANNEL — typical, one of 8 (n = 1 … 8)')
# coil + driver
QX = 1230
tag(QX, 620, 'VBAT+', C_VBAT, 'middle')
wire([(QX, 626), (QX, 640)], C_VBAT)
dot(QX, 640, C_VBAT)
coil(QX, 640, 750, 'K_CHn', C_VBAT)
dot(QX, 750)
flyback(QX, 640, 750, 40)
FX = QX + 90
wire([(QX + 40, 640), (FX, 640)], C_VBAT)
dot(QX + 40, 640, C_VBAT)
res(FX, 640, FX, 690)
diode(FX, 690, FX, 750, 'led', INK, body=C_LAMP_R, side=-1)
wire([(FX, 750), (QX + 40, 750)])
dot(QX + 40, 750)
text(FX + 12, 668, '470 Ω', 10)
text(FX + 28, 716, 'FIRE', 11, bold=True, fill=C_LAMP_R)
text(FX + 28, 730, 'lamp', 10, fill=GREY)
nmos(QX, 750, 'Q_CHn')
gate_drive(QX - 34, 780, 'RLY_CHn', 'IO11…IO18', 150)
text(1060, 880, 'FIRE lamp (lower lens) lights while K_CHn', 10, fill=GREY, italic=True)
text(1060, 894, 'is energised; IGN lens = WS2812 pixel.', 10, fill=GREY, italic=True)

# continuity sense
RX = 1450
tag(RX, 616, '+3V3', C_3V3, 'middle')
wire([(RX, 622), (RX, 630)], C_3V3)
res(RX, 630, RX, 690, C_3V3, fusible=True)
text(RX - 14, 656, '1.8 k', 10, 'end')
text(RX - 14, 668, 'R_ref2', 9, 'end', fill=GREY)
res(RX, 690, RX, 760, INK, fusible=True)
text(RX - 14, 722, '1.5 k', 10, 'end')
text(RX - 14, 734, 'R_ref1', 9, 'end', fill=GREY)
dot(RX, 780, C_SENSE)
wire([(RX, 760), (RX, 780)])
res(RX, 780, RX, 860)
text(RX + 14, 816, 'R_pull', 10)
text(RX + 14, 828, '100 k', 10)
gnd(RX, 860)
wire([(RX, 780), (1340, 780)], C_SENSE)
# ADC net label below the gate area
tag(1344, 800, 'ADC_CHn', C_SENSE, size=11)
text(1344, 814, '→ IO2,10,4–9', 10, fill=C_SENSE)
# clamps
KCX = 1520
wire([(RX, 780), (KCX, 780)], C_SENSE)
dot(KCX, 780, C_SENSE)
diode(KCX, 780, KCX, 700, 'schottky')
wire([(KCX, 700), (KCX, 680)], C_3V3)
tag(KCX, 674, '+3V3', C_3V3, 'middle', 11)
diode(KCX, 860, KCX, 780, 'schottky')
gnd(KCX, 860)
text(KCX + 14, 744, '1N5819', 10)
text(KCX + 14, 826, '1N5819', 10)
# 217R to NC
NCX = 1640
res(KCX, 780, NCX - 4, 780, INK)
text((KCX + NCX) / 2, 768, '220 Ω', 10, 'middle')
# channel relay contact: NC (1640,780), NO (1640,720), COM (1700,750)
ocirc(NCX, 780)
ocirc(NCX, 720)
ocirc(1700, 750)
line(1696, 752, NCX + 4, 777)
text(NCX - 4, 800, 'NC', 10, 'middle', fill=GREY)
text(NCX - 10, 716, 'NO', 10, 'end', fill=GREY)
text(1700, 772, 'COM', 10, 'middle', fill=GREY)
text(1630, 836, 'K_CHn contact', 12, bold=True)
text(1630, 850, 'at rest: NC (sense)', 10, fill=GREY)
text(1630, 864, 'energised: NO (fire)', 10, fill=GREY)
# NO <- FIRE_BUS
wire([(NCX, 716), (NCX, 640)], C_FIRE, 3.5)
dot(NCX, 660, C_FIRE)
tag(NCX + 6, 634, 'FIRE_BUS', C_FIRE, size=12)
text(NCX + 76, 634, '(from C)', 10, fill=GREY)
# COM -> XT60 -> igniter
wire([(1704, 750), (1790, 750)], C_FIRE, 3.5)
dot(1750, 750, C_FIRE)
# snubber NO-COM
wire([(NCX, 660), (1750, 660)], C_FIRE, 2)
res(1750, 660, 1750, 700)
cap(1750, 700, 1750, 750)
text(1762, 684, '100 Ω', 10)
text(1762, 724, '100 nF', 10)
text(1762, 737, '63 V', 9, fill=GREY)
ocirc(1790, 750, C_FIRE, 4.5)
text(1800, 770, 'XT30 +', 10, fill=GREY)
wire([(1790, 755), (1790, 780)], C_FIRE, 3.5)
rect(1780, 780, 20, 60, C_FIRE, 2, 'white', 0)
wire([(1780, 790), (1800, 800), (1780, 810), (1800, 820), (1780, 830)], C_FIRE, 1.5)
text(1808, 806, 'IGNITER', 11, bold=True, fill=C_FIRE)
text(1808, 820, 'e-match', 10, fill=GREY)
text(1808, 834, '≈ 1–2 Ω', 10, fill=GREY)
wire([(1790, 840), (1790, 865)], INK, 2)
ocirc(1790, 869, INK, 4.5)
text(1800, 884, 'XT30 −', 10, fill=GREY)
wire([(1790, 874), (1790, 890)])
gnd(1790, 890)
lines(1340, 900, ['Test current ≤ 1 mA (3.3 V / 3.3 kΩ, both fusible); one R_ref shorted ≤ 2.2 mA (no-fire ≈ 50 mA).',
                  'Clamps + 220 Ω (217 Ω measured) on all 8 channels. A fired igniter reads OPEN back on NC.'],
      10, fill=GREY, italic=True)

# channel map table
TY = 950
text(1060, TY, 'Channel map', 13, bold=True)
cols = ['', 'CH1', 'CH2', 'CH3', 'CH4', 'CH5', 'CH6', 'CH7', 'CH8']
rows = [('RLY_CHn  (Q_CHn gate)', ['IO11', 'IO12', 'IO13', 'IO14', 'IO15', 'IO16', 'IO17', 'IO18'], C_GATE),
        ('ADC_CHn  (continuity)', ['IO2', 'IO10', 'IO4', 'IO5', 'IO6', 'IO7', 'IO8', 'IO9'], C_SENSE),
        ('WS2812 pixel (IGN lens)', ['0', '1', '2', '3', '4', '5', '6', '7'], C_DATA)]
x0, cw0, cw, rh = 1060, 200, 70, 24
tw = cw0 + 8 * cw
rect(x0, TY + 10, tw, rh * 4, FRAME, 1, rx=2)
for i, c in enumerate(cols[1:]):
    text(x0 + cw0 + cw * i + cw / 2, TY + 10 + 17, c, 11, 'middle', True)
for r, (name, vals, col) in enumerate(rows):
    yy = TY + 10 + rh * (r + 1)
    line(x0, yy, x0 + tw, yy, FRAME, 1)
    text(x0 + 8, yy + 17, name, 11, fill=col, bold=True)
    for i, v in enumerate(vals):
        text(x0 + cw0 + cw * i + cw / 2, yy + 17, v, 11, 'middle')
for i in range(9):
    line(x0 + cw0 + cw * i, TY + 10, x0 + cw0 + cw * i, TY + 10 + rh * 4, FRAME, 1)
text(x0, TY + 130, 'CH1 is the leftmost channel module on the panel; ADC_CH2 is IO10 because IO3 is a strapping pin.',
     10, fill=GREY, italic=True)

# legend
LY = 1110
text(1060, LY, 'Wire colours (by function — not the physical cable colours, which are not documented)', 13, bold=True)
legend = [(C_VBAT, 2.5, None, 'VBAT+ — battery after SW1'),
          (C_KEY, 2.5, None, 'KEY_NO — battery via key switch S2'),
          (C_FIRE, 3.5, None, 'FIRE_BUS / igniter leads'),
          (C_3V3, 2.5, None, '+3V3 — U1 on-board LDO output'),
          (C_5V, 2.5, None, '+5V — from U2 buck'),
          (C_SENSE, 2.5, None, 'analogue / sense input to U1'),
          (C_GATE, 2.5, None, 'U1 output → MOSFET gate'),
          (C_DATA, 2.5, None, 'WS2812 data'),
          (NF, 1.5, '4 3', 'specified, NOT fitted')]
for i, (c, w, d, s) in enumerate(legend):
    lx = 1060 + (i % 2) * 400
    ly = LY + 26 + (i // 2) * 24
    line(lx, ly - 4, lx + 40, ly - 4, c, w + 0.5, d)
    text(lx + 52, ly, s, 11)

# =================================================================== E · SIREN + PIXELS
frame(20, 510, 600, 330, 'E · SIREN, STATUS PIXELS')
SXX = 230
tag(SXX, 552, 'VBAT+', C_VBAT, 'middle')
wire([(SXX, 558), (SXX, 570)], C_VBAT)
dot(SXX, 570, C_VBAT)
wire([(SXX, 570), (SXX, 586)], C_VBAT)
rect(SXX - 30, 586, 60, 40, INK, 2, 'white')
text(SXX, 611, 'SIREN', 11, 'middle', True)
wire([(SXX, 626), (SXX, 650)])
dot(SXX, 650)
wire([(SXX, 570), (SXX + 50, 570)], C_VBAT)
diode(SXX + 50, 650, SXX + 50, 570, 'schottky')
wire([(SXX, 650), (SXX + 50, 650)])
text(SXX + 64, 606, '1N5819', 10)
text(SXX + 64, 620, 'flyback', 10, fill=GREY)
text(SXX - 40, 600, '< 200 mA', 10, 'end', fill=GREY)
text(SXX - 40, 614, 'measured', 10, 'end', fill=GREY)
nmos(SXX, 650, 'Q_SIREN')
gate_drive(SXX - 34, 680, 'SIREN', 'IO40', 170)
lines(40, 790, ['Continuous in ARMED / PRE_FIRE / FIRING; 4 × 500/500 ms on LINK_LOST; 3 blasts on ERROR;',
                'one 200 ms boot chirp; igniter connect = 2 blips, disconnect = 1 burst (fw 1.2.21).'],
      10, fill=GREY, italic=True)
# WS2812
rect(410, 570, 190, 110)
text(505, 594, '8 × WS2812B', 13, 'middle', True)
text(505, 612, 'IGN lens of CH1 … CH8', 11, 'middle')
text(505, 628, 'DIN at the CH1 end', 10, 'middle', fill=GREY)
text(505, 642, 'pixel n−1 = CHn', 10, 'middle', fill=GREY)
text(418, 664, 'DIN', 9, fill=GREY)
text(505, 672, 'GND', 9, 'middle', fill=GREY)
wire([(320, 655), (410, 655)], C_DATA, 2.5)
text(322, 649, 'WS2812_DIN', 10, 'start', True)
text(322, 670, '← IO48', 10, fill=C_DATA)
line(505, 680, 505, 700)
gnd(505, 700)
wire([(560, 570), (560, 548)], C_5V, 2.5)
tag(560, 542, '+5V', C_5V, 'middle', 11)
text(560, 584, 'VDD', 9, 'middle', fill=GREY)
wire([(600, 655), (612, 655)], C_DATA, 2)
text(598, 672, 'DOUT', 9, 'end', fill=GREY)
text(410, 740, 'On-board LED (IO48) mirrors pixel 0 = CH1.', 10, fill=GREY, italic=True)

# =================================================================== NOTES
rect(20, 860, 1000, 390, FRAME, 1.5, rx=8)
text(32, 884, 'NOTES', 15, bold=True, fill=GREY)
notes = [
    '1. All GPIO are 3.3 V logic. Not used: IO0/IO3/IO45/IO46 (strapping), IO19/20 (USB), IO33–37 (octal PSRAM),',
    '    IO43/44 (UART0). Spare: IO38, IO39, IO41.',
    '2. Every IRLZ44N (8 × channel, K_ARM, siren): 150 Ω gate series + 10 kΩ gate pull-down, so all outputs stay off while',
    '    U1 boots or resets; flyback diode cathode to VBAT+ (1N4007 or 1N5819 per FSD; siren: 1N5819 fitted).',
    '3. Fire path: VBAT+ → K_ARM NO/COM → FIRE_BUS → K_CHn NO/COM → XT30 → igniter → GND. Two breaks.',
    '    K_ARM pulls in only with S2 at ARM AND IO47 high; IO21 verifies the contact, IO42 reads the key directly.',
    '4. Relays: 12 V automotive SPDT, 20 A contacts, coil 80–150 mA. Relay at rest (NC) = igniter on the continuity',
    '    circuit; the sense voltage and the fire voltage can never reach the igniter at the same time.',
    '5. Continuity bands (firmware): CONNECTED < ~67 Ω, MARGINAL ~67–500 Ω, SUSPECT ~0.5–1.09 kΩ, OPEN above.',
    '6. NOT fitted (grey dashed): TVS on FIRE_BUS; TL431 3.3 V rail clamp (bug #24); hardware battery undervoltage',
    '    cut-off (bug #25). Fitted: 3V3 zeners on IO21/IO42, RC snubbers on K_ARM and all 8 K_CHn.',
    '7. Power: XT60 → SW1 → VBAT+ → U2 LM2596S → +5V → U1 5V0 and the WS2812B VDD. +3V3 = U1\u2019s own LDO.',
    '8. Panel: CH1 … CH8 modules left to right (IGN lens, FIRE lens, XT30); key plate at the right end with SAFE / ARM',
    '    / HOT lamps; SW1 on the small plate above it; SMA antenna far left. No USB on the panel — service via open case.',
    '9. Base 3S pack: arming blocked below 10.5 V, ERROR below 9.0 V (firmware only).',
]
lines(32, 910, notes, 11, dy=21)

finish(OUT, W)
