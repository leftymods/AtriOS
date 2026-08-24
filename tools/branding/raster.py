#!/usr/bin/env python3
"""
Rasterize the canonical AtriOS mark (user-supplied SVG design) to PNG.

Canonical geometry (viewbox 400x400, scale = S/400):
  outer arc : r=160, stroke 14, from -35° covering 320° cw (gap at -55°)
  mid ring  : r=112, stroke 12
  core disc : r=58
  accent dot: c=(291.77, 68.94) r=11, #F59E0B — ALWAYS drawn last,
              never recolored by overlapping layers
Gradient: #2563EB -> #7C3AED along the main diagonal.

Usage: raster.py [out.png] [--size N]     (default: mark.png, 1600)
"""
import math, struct, zlib, sys

S = 1600
out = 'mark.png'
args = sys.argv[1:]
while args:
    a = args.pop(0)
    if a == '--size':
        S = int(args.pop(0))
    else:
        out = a

CX = CY = S / 2
SC = S / 400.0
FE = 2.0 * (S / 1600.0)

def hx(c):
    return (int(c[1:3], 16), int(c[3:5], 16), int(c[5:7], 16))

BLUE, PURPLE = hx('#2563EB'), hx('#7C3AED')
ORANGE = hx('#F59E0B')

R_OUT, W_OUT = 160 * SC, 14 * SC / 2
R_MID, W_MID = 112 * SC, 12 * SC / 2
R_CORE = 58 * SC
DOT_C = (291.77 * SC, 68.94 * SC)
DOT_R = 11 * SC
A_START, A_END = -35.0, 285.0

def grad(x, y):
    t = (x / S + y / S) / 2
    return tuple(BLUE[i] + (PURPLE[i] - BLUE[i]) * t for i in range(3))

def fill_cov(signed):
    v = 0.5 - signed / (2 * FE)
    return 0.0 if v < 0 else (1.0 if v > 1 else v)

def band_cov(d, r, hw):
    v = 0.5 - (abs(d - r) - hw) / (2 * FE)
    return 0.0 if v < 0 else (1.0 if v > 1 else v)

def in_arc(ang):
    a = ang % 360.0
    s = A_START % 360.0
    e = A_END % 360.0
    return (a >= s) or (a <= e)

rows = []
for py in range(S):
    row = bytearray()
    dy = py - CY
    for px in range(S):
        dx = px - CX
        d = math.hypot(dx, dy)

        # painter's model, bottom -> top; STRICT '<' so the dot (last)
        # always wins over a segment passing underneath
        alpha, col = 0.0, (0, 0, 0)

        a = band_cov(d, R_OUT, W_OUT) if in_arc(
            math.degrees(math.atan2(dy, dx))) else 0.0
        if a > alpha:
            alpha, col = a, grad(px, py)

        a = band_cov(d, R_MID, W_MID)
        if a > alpha:
            alpha, col = a, grad(px, py)

        a = fill_cov(d - R_CORE)
        if a > alpha:
            alpha, col = a, grad(px, py)

        a = fill_cov(math.hypot(px - DOT_C[0], py - DOT_C[1]) - DOT_R)
        if a > alpha:
            alpha, col = a, ORANGE

        if alpha <= 0:
            row += b'\0\0\0\0'
            continue
        row += bytes((int(col[0]), int(col[1]), int(col[2]), int(alpha * 255)))
    rows.append(bytes(row))

raw = b''.join(b'\x00' + r for r in rows)

def ch(t, d):
    c = t + d
    return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)

png = (b'\x89PNG\r\n\x1a\n'
       + ch(b'IHDR', struct.pack('>IIBBBBB', S, S, 8, 6, 0, 0, 0))
       + ch(b'IDAT', zlib.compress(raw, 9))
       + ch(b'IEND', b''))
open(out, 'wb').write(png)
print(out, len(png), 'bytes')
