"""Shared SVG drawing primitives for the RLC schematics (CRIG-node style).

Used by tools/base_schematic.py and tools/remote_schematic.py. Every drawing call
appends to the module-level `out` list; begin() starts a page, finish() writes
the SVG and, if cairosvg is installed, a 2x PNG on a white background.
"""
import html
import math

INK = '#1f2430'
GREY = '#6b7280'
FRAME = '#9aa3b2'
# functional wire colours (see legend)
C_VBAT = '#e03131'     # switched battery
C_KEY = '#f08c00'      # key-switched battery (KEY_NO)
C_FIRE = '#a61e4d'     # fire bus / igniter leads
C_3V3 = '#1971c2'      # +3V3
C_5V = '#e64980'       # +5V from the buck
C_SENSE = '#2f9e44'    # analogue / sense inputs
C_GATE = '#0c8599'     # GPIO -> MOSFET gate drive
C_DATA = '#7048e8'     # WS2812 data
C_LAMP_G = '#2b8a3e'
C_LAMP_R = '#c92a2a'
NF = '#9aa3b2'         # not fitted

out = []


def e(s):
    return html.escape(s, quote=True)


def text(x, y, s, size=11, anchor='start', bold=False, fill=INK, italic=False):
    out.append(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" '
               f'font-weight="{"bold" if bold else "normal"}"'
               f'{" font-style=\"italic\"" if italic else ""} fill="{fill}">{e(s)}</text>')


def lines(x, y, rows, size=11, anchor='start', fill=INK, dy=None, italic=False, bold=False):
    dy = dy or size + 3
    for i, r in enumerate(rows):
        ind = 18 if r.startswith('    ') else 0
        text(x + ind, y + i * dy, r.strip(), size, anchor, bold, fill, italic)


def line(x1, y1, x2, y2, c=INK, w=2, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ''
    out.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{c}" stroke-width="{w}" '
               f'stroke-linecap="round"{d}/>')


def wire(pts, c=INK, w=2, dash=None):
    for (a, b), (cc, d) in zip(pts, pts[1:]):
        line(a, b, cc, d, c, w, dash)


def dot(x, y, c=INK):
    out.append(f'<circle cx="{x}" cy="{y}" r="3.5" fill="{c}"/>')


def ocirc(x, y, c=INK, r=3.5):
    out.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="white" stroke="{c}" stroke-width="2"/>')


def rect(x, y, w, h, c=INK, sw=2, fill='none', rx=4, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ''
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" '
               f'stroke="{c}" stroke-width="{sw}"{d}/>')


def poly(pts, c=INK, fill='none', sw=2):
    p = ' '.join(f'{x},{y}' for x, y in pts)
    out.append(f'<polygon points="{p}" fill="{fill}" stroke="{c}" stroke-width="{sw}" stroke-linejoin="round"/>')


def frame(x, y, w, h, title):
    rect(x, y, w, h, FRAME, 1.5, rx=8, dash='6 4')
    text(x + 12, y + 20, title, 14, bold=True, fill=GREY)


def gnd(x, y, c=INK, dash=None):
    line(x, y, x, y + 8, c, 2, dash)
    line(x - 10, y + 8, x + 10, y + 8, c)
    line(x - 6, y + 12, x + 6, y + 12, c)
    line(x - 2, y + 16, x + 2, y + 16, c)


def _axis(x1, y1, x2, y2):
    L = math.hypot(x2 - x1, y2 - y1)
    ux, uy = (x2 - x1) / L, (y2 - y1) / L
    return L, ux, uy, -uy, ux


def res(x1, y1, x2, y2, c=INK, body=40, fusible=False, dash=None):
    """Axis-aligned resistor (IEC box) between two points, leads in colour c."""
    L, ux, uy, px, py = _axis(x1, y1, x2, y2)
    mx, my = (x1 + x2) / 2, (y1 + y2) / 2
    a = (mx - ux * body / 2, my - uy * body / 2)
    b = (mx + ux * body / 2, my + uy * body / 2)
    line(x1, y1, *a, c, 2, dash)
    line(*b, x2, y2, c, 2, dash)
    if abs(ux) > 0.5:
        rect(min(a[0], b[0]), my - 7, body, 14, INK if not dash else NF, 2, 'white', 0, dash)
    else:
        rect(mx - 7, min(a[1], b[1]), 14, body, INK if not dash else NF, 2, 'white', 0, dash)
    if fusible:
        line(*a, *b, INK, 1.5)


def cap(x1, y1, x2, y2, c=INK):
    L, ux, uy, px, py = _axis(x1, y1, x2, y2)
    mx, my = (x1 + x2) / 2, (y1 + y2) / 2
    g = 4
    line(x1, y1, mx - ux * g, my - uy * g, c)
    line(mx + ux * g, my + uy * g, x2, y2, c)
    for s in (-g, g):
        cx, cy = mx + ux * s, my + uy * s
        line(cx - px * 10, cy - py * 10, cx + px * 10, cy + py * 10, INK)


def diode(x1, y1, x2, y2, kind='std', c=INK, dash=None, body=INK, side=1):
    """Anode at (x1,y1), cathode at (x2,y2)."""
    L, ux, uy, px, py = _axis(x1, y1, x2, y2)
    mx, my = (x1 + x2) / 2, (y1 + y2) / 2
    s, hw = 7, 8
    base = (mx - ux * s, my - uy * s)
    apex = (mx + ux * s, my + uy * s)
    line(x1, y1, *base, c, 2, dash)
    line(*apex, x2, y2, c, 2, dash)
    col = NF if dash else body
    fill = 'white'
    poly([(base[0] + px * hw, base[1] + py * hw), (base[0] - px * hw, base[1] - py * hw), apex], col, fill)
    b1 = (apex[0] + px * hw, apex[1] + py * hw)
    b2 = (apex[0] - px * hw, apex[1] - py * hw)
    line(*b1, *b2, col, 2)
    if kind == 'schottky':
        line(*b1, b1[0] + ux * 4, b1[1] + uy * 4, col, 2)
        line(b1[0] + ux * 4, b1[1] + uy * 4, b1[0] + ux * 4 - px * 3, b1[1] + uy * 4 - py * 3, col, 2)
        line(*b2, b2[0] - ux * 4, b2[1] - uy * 4, col, 2)
        line(b2[0] - ux * 4, b2[1] - uy * 4, b2[0] - ux * 4 + px * 3, b2[1] - uy * 4 + py * 3, col, 2)
    elif kind == 'zener':
        line(*b1, b1[0] - ux * 4, b1[1] - uy * 4, col, 2)
        line(*b2, b2[0] + ux * 4, b2[1] + uy * 4, col, 2)
    elif kind == 'led':
        px, py = px * side, py * side
        for k in (-4, 4):
            sx, sy = mx + ux * k + px * 11, my + uy * k + py * 11
            ex, ey = sx + px * 10 + ux * 5, sy + py * 10 + uy * 5
            line(sx, sy, ex, ey, body, 1.5)
            poly([(ex, ey), (ex - px * 4 - ux * 1, ey - py * 4 - uy * 1),
                  (ex - px * 1 - ux * 4, ey - py * 1 - uy * 4)], body, body, 1)


def coil(x, y1, y2, name, c_top=INK, c_bot=INK):
    """Relay coil (IEC box with diagonal) vertical from y1 to y2."""
    mid = (y1 + y2) / 2
    line(x, y1, x, mid - 22, c_top)
    line(x, mid + 22, x, y2, c_bot)
    rect(x - 12, mid - 22, 24, 44, INK, 2, 'white', 0)
    line(x - 12, mid + 22, x + 12, mid - 22, INK, 1.5)
    text(x - 18, mid - 2, name, 12, 'end', True)
    text(x - 18, mid + 12, 'coil 12 V', 10, 'end', fill=GREY)


def nmos(x, y, name, c_gate=C_GATE):
    """IRLZ44N: drain at (x,y), source at (x,y+60) -> GND, gate at (x-34,y+30)."""
    wire([(x, y), (x, y + 15), (x - 12, y + 15)])
    wire([(x, y + 60), (x, y + 45), (x - 12, y + 45)])
    for a, b in ((10, 20), (25, 35), (40, 50)):
        line(x - 12, y + a, x - 12, y + b, INK, 2.5)
    wire([(x - 12, y + 30), (x, y + 30), (x, y + 45)])
    poly([(x - 11, y + 30), (x - 5, y + 27), (x - 5, y + 33)], INK, INK, 1)
    line(x - 18, y + 15, x - 18, y + 45, INK, 2)
    line(x - 34, y + 30, x - 18, y + 30, c_gate)
    gnd(x, y + 60)
    text(x + 10, y + 28, name, 12, bold=True)
    text(x + 10, y + 42, 'IRLZ44N', 10, fill=GREY)


def gate_drive(x, y, net, gpio, length=150):
    """150R series + 10k pull-down from gate point (x,y) leftwards; net label at the far end."""
    pd = x - 26
    wire([(x, y), (pd, y)], C_GATE)
    dot(pd, y)
    res(pd, y, pd, y + 64)
    gnd(pd, y + 64)
    text(pd + 12, y + 40, '10 k', 10)
    rx = pd - 62
    res(pd, y, rx, y, C_GATE)
    text((pd + rx) / 2, y - 12, '150 Ω', 10, 'middle')
    end = x - length
    wire([(rx, y), (end, y)], C_GATE)
    text(end, y - 6, net, 11, bold=True, fill=INK)
    text(end, y + 14, f'← {gpio}', 10, fill=C_GATE)


def flyback(x_coil, y_top, y_bot, dx=40, label='flyback'):
    wire([(x_coil, y_bot), (x_coil + dx, y_bot)])
    diode(x_coil + dx, y_bot, x_coil + dx, y_top)
    wire([(x_coil + dx, y_top), (x_coil, y_top)])
    text(x_coil + dx + 12, (y_top + y_bot) / 2 + 4, label, 10, fill=GREY)


def tag(x, y, s, c, anchor='start', size=12):
    text(x, y, s, size, anchor, True, c)



def begin(w, h, title, subtitle):
    out.clear()
    out.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" '
               f'font-family="Helvetica, Arial, sans-serif">')
    out.append(f'<rect width="{w}" height="{h}" fill="white"/>')
    text(20, 30, title, 22, bold=True)
    text(20, 48, subtitle, 12, fill=GREY)


def finish(path, w):
    out.append('</svg>')
    svg = '\n'.join(out)
    with open(path + '.svg', 'w') as f:
        f.write(svg)
    try:
        import cairosvg
        cairosvg.svg2png(bytestring=svg.encode(), write_to=path + '.png', output_width=w * 2,
                         background_color='white')
        print('wrote', path + '.svg', path + '.png')
    except ImportError:
        print('wrote', path + '.svg (install cairosvg for the PNG)')
