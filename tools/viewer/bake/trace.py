"""Bitmap -> SVG path via potrace, expressed in slippilab's pixel space."""
from __future__ import annotations

import re
import subprocess

from .raster import SIZE, to_pbm

_NUM = re.compile(r'-?\d+(?:\.\d+)?(?:e-?\d+)?')


def trace(image) -> str:
    """Return an SVG path (absolute commands, y down, 1000 px frame)."""
    if not image.any():
        return ''
    proc = subprocess.run(['potrace', '--svg', '-o', '-', '-'], input=to_pbm(image),
                          capture_output=True, check=True)
    svg = proc.stdout.decode()
    m = re.search(r'transform="translate\(([-\d.]+),([-\d.]+)\) scale\(([-\d.]+),([-\d.]+)\)"', svg)
    tx, ty, sx, sy = (float(v) for v in m.groups()) if m else (0.0, 0.0, 1.0, 1.0)
    paths = re.findall(r'<path d="([^"]*)"', svg)
    out = []
    for d in paths:
        out.append(_absolutize(d, tx, ty, sx, sy))
    return ' '.join(out)


def _fmt(v: float) -> str:
    s = f'{v:.1f}'
    if s.endswith('.0'):
        s = s[:-2]
    return s


def _absolutize(d: str, tx: float, ty: float, sx: float, sy: float) -> str:
    tokens = re.findall(r'[MmCcLlZz]|' + _NUM.pattern, d)
    i = 0
    cx = cy = 0.0
    start = (0.0, 0.0)
    out = []

    def T(x, y):
        return tx + sx * x, ty + sy * y

    cmd = None
    while i < len(tokens):
        tok = tokens[i]
        if tok in 'MmCcLlZz':
            cmd = tok
            i += 1
            if cmd in 'Zz':
                out.append('Z')
                cx, cy = start
            continue
        if cmd in 'Mm':
            x, y = float(tokens[i]), float(tokens[i + 1])
            i += 2
            if cmd == 'm':
                x, y = cx + x, cy + y
            cx, cy = x, y
            start = (cx, cy)
            ax, ay = T(cx, cy)
            out.append(f'M{_fmt(ax)} {_fmt(ay)}')
            cmd = 'l' if cmd == 'm' else 'L'
        elif cmd in 'Ll':
            x, y = float(tokens[i]), float(tokens[i + 1])
            i += 2
            if cmd == 'l':
                x, y = cx + x, cy + y
            cx, cy = x, y
            ax, ay = T(cx, cy)
            out.append(f'L{_fmt(ax)} {_fmt(ay)}')
        elif cmd in 'Cc':
            vals = [float(t) for t in tokens[i:i + 6]]
            i += 6
            if cmd == 'c':
                vals = [cx + vals[0], cy + vals[1], cx + vals[2], cy + vals[3], cx + vals[4], cy + vals[5]]
            cx, cy = vals[4], vals[5]
            pts = [T(vals[0], vals[1]), T(vals[2], vals[3]), T(vals[4], vals[5])]
            out.append('C' + ' '.join(f'{_fmt(x)} {_fmt(y)}' for x, y in pts))
        else:
            i += 1
    return ''.join(out)
