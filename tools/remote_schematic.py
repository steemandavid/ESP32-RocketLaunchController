#!/usr/bin/env python3
"""RLC remote unit schematic -> SVG (+ PNG via cairosvg), same style as the base schematic.

Source: FSD RLC-FSPEC-001 v1.76 §5.5, §5.6.2, App. C.2, pin_config.h, rlc_config.h and
Development_Progress.md (bugs #21/#22/#25), plus owner answers of 2026-10-11: a buck
module makes 5 V for the DevKit 5V0 pin and the WS2812 strip; the ILI9488 runs from
3.3 V. Not verified: the buck module type, the buzzer driver topology and the battery
connector. FSD §5.5 warns the remote panel has never been reconciled against the
built unit, so treat this drawing as design-of-record, not as-traced.

    pip install cairosvg
    python tools/remote_schematic.py docs/reference/RLC_remote_schematic
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schematic_svg import *  # noqa: E402,F401,F403

OUT = sys.argv[1] if len(sys.argv) > 1 else 'RLC_remote_schematic'
W, H = 1910, 1270

C_IN = C_SENSE      # switch / analogue inputs to U1
C_OUT = C_GATE      # U1 outputs (LEDs, buzzer, backlight)
C_SPI = C_DATA      # SPI bus + WS2812 data

begin(W, H, 'RLC remote unit — schematic',
      'ESP32 Rocket Launch Controller · 2026-10-11 · fw 1.2.21 · FSD v1.76 §5.5, §5.6.2, App. C.2 + '
      'pin_config.h · design of record — remote not yet traced against the built unit (FSD §5.5)')

# =================================================================== A · POWER
frame(20, 60, 600, 430, 'A · POWER, 5 V BUCK, BATTERY SENSE')
rect(40, 100, 140, 80)
text(110, 124, 'BT1  2S LiPo', 13, 'middle', True)
text(110, 140, '2200 mAh · 30C', 11, 'middle')
text(110, 154, '7.4 V nom · 8.4 V full', 11, 'middle')
text(110, 168, 'T-plug per FSD (unverified)', 9, 'middle', fill=GREY)
line(110, 180, 110, 200)
gnd(110, 200)
text(110, 236, 'no hardware UV cut-off (bug #25)', 10, 'middle', fill=GREY, italic=True)
wire([(180, 140), (232, 140)], C_VBAT)
line(232, 140, 268, 126)
ocirc(232, 140)
ocirc(268, 140)
text(250, 162, 'SW1 on/off', 11, 'middle')
text(250, 176, 'panel switch', 10, 'middle', fill=GREY)
wire([(268, 140), (470, 140)], C_VBAT, 2.5)
dot(310, 140, C_VBAT)
dot(470, 140, C_VBAT)
wire([(470, 140), (560, 140)], C_VBAT, 2.5)
tag(565, 136, 'VBAT+', C_VBAT, size=13)
text(565, 152, '6.4–8.4 V', 10, fill=GREY)
# U2 buck
wire([(310, 140), (310, 250)], C_VBAT)
rect(230, 250, 170, 90)
text(315, 276, 'U2  5 V buck', 12, 'middle', True)
text(315, 292, 'module type not recorded', 10, 'middle', fill=GREY)
text(315, 308, 'VBAT → 5.0 V', 11, 'middle', True, C_5V)
text(238, 264, 'IN', 9, fill=GREY)
text(392, 316, 'OUT', 9, 'end', fill=GREY)
text(315, 334, 'GND', 9, 'middle', fill=GREY)
line(315, 340, 315, 356)
gnd(315, 356)
wire([(400, 312), (430, 312), (430, 392), (500, 392)], C_5V, 2.5)
tag(506, 388, '+5V', C_5V)
text(506, 404, '→ U1 5V0', 10, fill=GREY)
text(506, 417, '→ WS2812 VDD', 10, fill=GREY)
# battery divider 18k / 10k
res(470, 140, 470, 236, C_VBAT)
text(482, 192, 'R 18 k', 11)
dot(470, 236, C_IN)
res(470, 236, 470, 330)
text(482, 288, 'R 10 k', 11)
gnd(470, 330)
wire([(470, 236), (580, 236)], C_IN)
tag(498, 230, 'VBAT_ADC → IO1', C_IN, size=11)
# recommended clamp, not fitted
wire([(545, 236), (545, 290)], NF, 1.5, '4 3')
diode(545, 290, 545, 336, 'schottky', NF, '4 3')
wire([(545, 336), (545, 350)], NF, 1.5, '4 3')
text(545, 364, '+3V3', 9, 'middle', fill=GREY)
lines(556, 302, ['BAT54', 'not fitted', '(bug #22)'], 9, fill=GREY, dy=11)
lines(40, 440, ['Divider 2.8 : 1, calibrated ratio 2.8211 (2026-08-19). The 3.3 V zener that made it non-linear',
                'was removed (bug #21); GPIO 1 now has no clamp. +3V3 = the dev board’s own LDO (3V3 pin).'],
      10, fill=GREY, italic=True)

# =================================================================== B · MCU
frame(640, 60, 380, 780, 'B · MCU (ESP32-S3 dev board)')
BX, BY, BW, BH = 780, 110, 160, 690
rect(BX, BY, BW, BH)
text(BX + BW / 2, 420, 'U1', 18, 'middle', True)
lines(BX + BW / 2, 442, ['ESP32-S3-', 'DevKitC-1', 'WROOM-1', 'N16R8'], 11, 'middle', dy=14)
lines(BX + BW / 2, 514, ['SMA ext. antenna', 'USB, USB-JTAG'], 10, 'middle', fill=GREY, dy=13)
lines(BX + BW / 2, 556, ['spare:', 'IO38 IO39 IO40', 'IO41 IO42 IO47'], 10, 'middle', fill=GREY, dy=13)

left = [('IO1', 'VBAT_ADC', C_IN), ('IO4', 'ENC_A', C_IN), ('IO5', 'ENC_B', C_IN),
        ('IO6', 'ENC_SW', C_IN), ('IO7', 'KEY_NO', C_IN), ('IO2', 'KEY_NC', C_IN),
        ('IO15', 'FIRE_BTN', C_IN), ('IO9', 'SPI_MISO', C_SPI), ('3V3', '+3V3  (out)', C_3V3),
        ('5V0', '+5V', C_5V)]
right = [('IO8', 'ARM_LED', C_OUT), ('IO17', 'FIRE_LED_R', C_OUT), ('IO18', 'FIRE_LED_G', C_OUT),
         ('IO16', 'BUZZER', C_OUT), ('IO11', 'SPI_MOSI', C_SPI), ('IO12', 'SPI_SCLK', C_SPI),
         ('IO10', 'DISP_CS', C_SPI), ('IO13', 'DISP_DC', C_SPI), ('IO14', 'DISP_RST', C_SPI),
         ('IO21', 'DISP_BL', C_OUT), ('IO48', 'WS2812_DIN', C_SPI), ('GND', 'GND', INK)]
step = 54
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
for i in range(1, 7):
    text(BX + 6, BY + 40 + i * step + 16, 'pull-up', 9, fill=GREY)
text(BX + BW - 6, BY + 40 + 10 * step + 16, '+ on-board LED', 9, 'end', fill=GREY)
lines(660, 818, ['All switch inputs use U1’s internal pull-ups; contacts switch to GND.',
                 'SPI on SPI2_HOST. GPIO 1 is ADC1 (safe with ESP-NOW).'], 10, fill=GREY, italic=True)

# =================================================================== C · OPERATOR CONTROLS
frame(1040, 60, 850, 430, 'C · OPERATOR CONTROLS (encoder, arm key, fire button)')
# encoder
rect(1070, 110, 150, 130)
text(1145, 134, 'ENC1', 13, 'middle', True)
text(1145, 150, 'rotary encoder', 11, 'middle')
text(1145, 164, 'KY-040 type', 10, 'middle', fill=GREY)
for y, pin, netname, gpio in ((180, 'CLK', 'ENC_A', 'IO4'), (204, 'DT', 'ENC_B', 'IO5'),
                              (228, 'SW', 'ENC_SW', 'IO6')):
    text(1212, y + 4, pin, 9, 'end', fill=GREY)
    wire([(1220, y), (1290, y)], C_IN)
    text(1296, y + 4, f'{netname} → {gpio}', 11, bold=True, fill=C_IN)
line(1145, 240, 1145, 256)
gnd(1145, 256)
lines(1078, 196, ['+ pin supply:', 'not documented'], 9, fill=GREY, dy=11)
lines(1060, 300, ['A/B: both edges, cycle-position decoder,', 'ENC_DIVIDER 4, ENC_REVERSED 1.',
                  'SW: 500 ms long-press = ARM confirm.'], 10, fill=GREY, italic=True)

# arm key switch S2 (SPDT, COM to GND, shown SAFE)
KX = 1520
ocirc(KX, 230)                  # COM
ocirc(KX - 30, 180)             # NC
ocirc(KX + 30, 180)             # NO
line(KX, 226, KX - 27, 184)
wire([(KX, 234), (KX, 252)])
gnd(KX, 252)
text(KX - 40, 186, 'NC', 10, 'end', fill=GREY)
text(KX + 40, 196, 'NO', 10, fill=GREY)
text(KX + 14, 244, 'COM', 9, fill=GREY)
wire([(KX + 34, 180), (1650, 180)], C_IN)
text(1656, 176, 'KEY_NO → IO7', 11, bold=True, fill=C_IN)
text(1656, 190, 'LOW = ARM (authoritative)', 9, fill=GREY)
wire([(KX - 30, 176), (KX - 30, 130), (1650, 130)], C_IN)
text(1656, 126, 'KEY_NC → IO2', 11, bold=True, fill=C_IN)
text(1656, 140, 'LOW = SAFE (cross-check)', 9, fill=GREY)
text(KX - 40, 100, 'S2 arm key switch', 12, 'end', True)
text(KX - 40, 114, 'SPDT, COM to GND, shown SAFE', 10, 'end', fill=GREY)

# arm LED: +3V3 -> R(built-in) -> LED -> IO8 (active LOW)
LX = 1800
tag(LX, 238, '+3V3', C_3V3, 'middle', 11)
wire([(LX, 244), (LX, 256)], C_3V3)
res(LX, 256, LX, 300, INK, body=30)
diode(LX, 300, LX, 350, 'led', INK, body=C_LAMP_R, side=-1)
wire([(LX, 350), (LX, 372)], C_OUT)
text(LX + 8, 384, 'ARM_LED', 11, 'middle', True)
text(LX + 8, 398, '← IO8 (active LOW)', 10, 'middle', fill=C_OUT)
text(LX - 14, 282, 'R built-in', 9, 'end', fill=GREY)
text(LX - 24, 330, 'ARM', 11, 'end', True, C_LAMP_R)
text(LX - 24, 344, 'lamp', 10, 'end', fill=GREY)

# fire button module S3
rect(1290, 290, 440, 186, FRAME, 1.5, rx=6, dash='5 4')
text(1300, 308, 'S3 illuminated fire button (button + red/green ring, resistors built in)', 10, bold=True, fill=GREY)
# button contact to GND
text(1310, 372, 'FIRE_BTN → IO15', 11, bold=True, fill=C_IN)
text(1310, 386, 'LOW = pressed', 9, fill=GREY)
wire([(1310, 420), (1352, 420)], C_IN)
ocirc(1356, 420)
ocirc(1396, 420)
line(1346, 406, 1406, 406)
line(1376, 406, 1376, 394)
line(1368, 394, 1384, 394)
wire([(1400, 420), (1440, 420), (1440, 432)])
gnd(1440, 432)
text(1310, 468, 'dead-man: release = CEASE FIRE', 9, fill=GREY)
# ring LEDs: IO -> R -> LED -> GND (active HIGH)
for x, netname, gpio, col, name in ((1560, 'FIRE_LED_R', 'IO17', C_LAMP_R, 'red'),
                                    (1660, 'FIRE_LED_G', 'IO18', C_LAMP_G, 'green')):
    text(x, 336, netname, 10, 'middle', True)
    text(x, 350, f'← {gpio}', 10, 'middle', fill=C_OUT)
    wire([(x, 356), (x, 366)], C_OUT)
    res(x, 366, x, 396, INK, body=24)
    diode(x, 396, x, 436, 'led', INK, body=col, side=-1)
    line(x, 436, x, 446)
    gnd(x, 446)
    text(x - 12, 422, name, 9, 'end', fill=col)

# =================================================================== D · DISPLAY
frame(1040, 510, 850, 330, 'D · DISPLAY (ILI9488 3.5" SPI TFT, 480 × 320)')
DX, DY = 1500, 548
rect(DX, DY, 200, 262)
text(DX + 100, DY + 108, 'U3  ILI9488', 13, 'middle', True)
lines(DX + 100, DY + 126, ['480 × 320, RGB666', 'clone, ID 0x2A403300', '14-pin module'], 10, 'middle', fill=GREY, dy=13)
text(DX + 100, DY + 250, 'pins 10–14 (touch): N/C', 9, 'middle', fill=GREY)
dpins = [('1 VCC', '+3V3', C_3V3, None), ('2 GND', 'GND', INK, None),
         ('3 CS', 'DISP_CS', C_SPI, 'IO10'), ('4 RESET', 'DISP_RST', C_SPI, 'IO14'),
         ('5 DC', 'DISP_DC', C_SPI, 'IO13'), ('6 SDI', 'SPI_MOSI', C_SPI, 'IO11'),
         ('7 SCK', 'SPI_SCLK', C_SPI, 'IO12'), ('8 LED', 'DISP_BL', C_OUT, 'IO21'),
         ('9 SDO', 'SPI_MISO', C_SPI, 'IO9')]
for i, (pin, netname, c, gpio) in enumerate(dpins):
    y = DY + 18 + i * 26
    text(DX + 6, y + 4, pin, 9, fill=GREY)
    if netname == 'GND':
        wire([(DX, y), (DX - 50, y)])
        gnd(DX - 50, y)
        continue
    wire([(DX, y), (1250, y)], c, 2)
    arrow = '→' if netname == 'SPI_MISO' else '←'
    label = netname if gpio is None else f'{netname}  {arrow} {gpio}'
    text(1254, y - 4, label, 10, bold=True, fill=c if gpio is None else INK)
lines(1060, 570, ['SPI2_HOST, 20 MHz write,', '6.67 MHz register read.', '',
                  'Backlight: IO21 driven HIGH', '(always on, no PWM).', '',
                  'A MISO/MOSI swap gives a', 'blank screen and ID 0.'], 10, fill=GREY, italic=True, dy=13)
lines(1720, 570, ['Health check:', 'ID read at boot and', 'every 5 s; 0x00000000', 'or 0xFFFFFFFF = fault,',
                  'two bad reads → ERROR', '(+ CMD_DISARM if armed).'], 10, fill=GREY, italic=True, dy=13)

# =================================================================== E · BUZZER + PIXELS
frame(20, 510, 600, 330, 'E · BUZZER, STATUS PIXELS')
rect(170, 570, 170, 100, INK, 2, 'white')
text(255, 594, 'BZ1 buzzer + driver', 12, 'middle', True)
text(255, 610, 'FSD: active buzzer,', 10, 'middle')
text(255, 623, 'BC547 NPN low-side', 10, 'middle')
text(255, 640, 'topology not verified', 9, 'middle', fill=GREY)
text(178, 660, 'IN', 9, fill=GREY)
wire([(60, 655), (170, 655)], C_OUT, 2.5)
text(62, 649, 'BUZZER', 10, bold=True)
text(62, 670, '← IO16', 10, fill=C_OUT)
line(255, 670, 255, 690)
gnd(255, 690)
wire([(310, 570), (310, 548)], NF, 1.5, '4 3')
text(316, 548, 'supply: not documented', 9, fill=GREY)
lines(40, 740, ['Firmware drives it active LOW (PIN_BUZZER_ACTIVE 0, "BC547 inverts").',
                'A plain NPN low-side stage is active HIGH, so the as-built driver is not',
                'what the FSD describes — it works on the bench; trace it before relying on it.'],
      10, fill=GREY, italic=True)
# WS2812
rect(410, 570, 190, 110)
text(505, 594, '8 × WS2812', 13, 'middle', True)
text(505, 612, 'channel map pixels', 11, 'middle')
text(505, 628, 'DIN at the CH8 end', 10, 'middle', fill=GREY)
text(505, 642, 'pixel 7−(n−1) = CHn', 10, 'middle', fill=GREY)
text(418, 664, 'DIN', 9, fill=GREY)
text(505, 672, 'GND', 9, 'middle', fill=GREY)
wire([(370, 655), (410, 655)], C_SPI, 2.5)
text(372, 700, 'WS2812_DIN', 10, 'start', True)
text(372, 714, '← IO48', 10, fill=C_SPI)
wire([(370, 655), (370, 688)], C_SPI, 2.5)
line(505, 680, 505, 700)
gnd(505, 700)
wire([(560, 570), (560, 548)], C_5V, 2.5)
tag(560, 542, '+5V', C_5V, 'middle', 11)
text(560, 584, 'VDD', 9, 'middle', fill=GREY)
text(40, 806, 'Pixels: on-board LED (IO48) mirrors pixel 0 = CH8. Colours come from the base’s STATUS_UPDATE.', 10, fill=GREY, italic=True)

# =================================================================== NOTES
rect(20, 860, 1000, 390, FRAME, 1.5, rx=8)
text(32, 884, 'NOTES', 15, bold=True, fill=GREY)
notes = [
    '1. All GPIO are 3.3 V logic. Not used: IO0/IO3/IO45/IO46 (strapping), IO19/20 (USB), IO33–37 (octal PSRAM),',
    '    IO43/44 (UART0). Spare: IO38, IO39, IO40, IO41, IO42, IO47. IO2 is NOT spare — the key’s NC contact grounds it.',
    '2. Every switch input is a contact to GND with U1’s internal pull-up, so a broken wire reads released / SAFE.',
    '    Debounce: fire button 8-bit (80 ms); encoder SW and both key contacts 16-bit (160 ms).',
    '3. The remote key is not in the fire path. It gates arming in firmware; the two fire-path breaks are on the base.',
    '    KEY_NO alone decides the arm state; KEY_NC disagreeing for longer than ARM_SWITCH_DISAGREE_MS → ARM KEY SWITCH FAULT.',
    '4. LED polarity: ARM lamp active LOW (+3V3 → R → LED → IO8); fire ring LEDs active HIGH (IO → R → LED → GND).',
    '    Red ring = button live (armed AND base confirms); green = not live. Resistors are inside the modules.',
    '5. Power: 2S pack → SW1 → VBAT+ → U2 5 V buck → U1 5V0 and the WS2812 strip; +3V3 = U1 LDO, also feeds',
    '    the display and the arm lamp. Arming blocked below 7.0 V, ERROR below 6.4 V (firmware only).',
    '6. NOT fitted (grey dashed): BAT54 clamp on IO1 (bug #22); hardware battery undervoltage cut-off (bug #25).',
    '7. Not documented / not verified: U2 module type, buzzer driver topology and supply, battery connector (FSD: T-plug),',
    '    encoder + pin supply. The remote has never been traced against the built unit (FSD §5.5 warning).',
]
lines(32, 910, notes, 11, dy=21)

# =================================================================== legend
frame(1040, 860, 850, 390, 'WIRE COLOURS (by function — real cable colours not documented)')
legend = [(C_VBAT, 2.5, None, 'VBAT+ — battery after SW1'),
          (C_5V, 2.5, None, '+5V — from U2 buck'),
          (C_3V3, 2.5, None, '+3V3 — U1 on-board LDO output'),
          (C_IN, 2.5, None, 'switch / analogue input to U1'),
          (C_OUT, 2.5, None, 'U1 output — LEDs, buzzer, backlight'),
          (C_SPI, 2.5, None, 'SPI bus / WS2812 data'),
          (NF, 1.5, '4 3', 'specified / recommended, NOT fitted')]
for i, (c, w, d, s) in enumerate(legend):
    lx = 1060 + (i % 2) * 400
    ly = 910 + (i // 2) * 26
    line(lx, ly - 4, lx + 40, ly - 4, c, w + 0.5, d)
    text(lx + 52, ly, s, 11)
text(1060, 1040, 'Remote pin map (App. C.2)', 13, bold=True)
pmap = [('Encoder A / B / SW', 'IO4 / IO5 / IO6'), ('Arm key NO / NC', 'IO7 / IO2'),
        ('Fire button', 'IO15'), ('Arm lamp / fire ring R / G', 'IO8 / IO17 / IO18'),
        ('Buzzer', 'IO16'), ('Battery ADC', 'IO1'),
        ('SPI MOSI / SCLK / MISO', 'IO11 / IO12 / IO9'), ('Display CS / DC / RST / BL', 'IO10 / IO13 / IO14 / IO21'),
        ('WS2812 strip', 'IO48')]
for i, (k, v) in enumerate(pmap):
    lx = 1060 + (i % 2) * 400
    ly = 1066 + (i // 2) * 22
    text(lx, ly, k, 11, fill=GREY)
    text(lx + 210, ly, v, 11, bold=True)

finish(OUT, W)
