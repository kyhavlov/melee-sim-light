"""Rasterize world-space triangles into slippilab's 1000 px side view."""
from __future__ import annotations

import ctypes
import os
import subprocess
import sys
from pathlib import Path

import numpy as np

SIZE = 1000
PX_PER_UNIT = 10.0  # 100-unit orthographic width over 1000 px
_lib = None


def _library():
    global _lib
    if _lib is not None:
        return _lib
    here = Path(__file__).resolve().parent
    src = here / 'raster.c'
    out = Path(os.environ.get('MSL_BAKE_BUILD', here.parents[2] / 'build' / 'bake')) / 'raster.so'
    if not out.exists() or out.stat().st_mtime < src.stat().st_mtime:
        out.parent.mkdir(parents=True, exist_ok=True)
        cc = os.environ.get('CC', 'cc')
        subprocess.run([cc, '-O2', '-shared', '-fPIC', '-o', str(out), str(src), '-lm'], check=True)
    _lib = ctypes.CDLL(str(out))
    _lib.msl_bake_fill.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                                   ctypes.POINTER(ctypes.c_uint8), ctypes.c_int, ctypes.c_int]
    return _lib


def project(tris_world: np.ndarray, horizontal_axis: int = 2, flip: float = 1.0) -> np.ndarray:
    """(n,3,3) world triangles -> (n,3,2) pixel coordinates.

    Slippilab's side camera: image x = 500 + 10 * (model horizontal axis),
    image y = 500 - 10 * model Y (y down in pixels).
    """
    hx = tris_world[:, :, horizontal_axis] * flip
    y = tris_world[:, :, 1]
    px = SIZE / 2 + PX_PER_UNIT * hx
    py = SIZE / 2 - PX_PER_UNIT * y
    return np.stack([px, py], axis=-1)


def rasterize(tris_px: np.ndarray) -> np.ndarray:
    image = np.zeros((SIZE, SIZE), dtype=np.uint8)
    if tris_px.shape[0] == 0:
        return image
    flat = np.ascontiguousarray(tris_px.reshape(-1, 6), dtype=np.float32)
    lib = _library()
    lib.msl_bake_fill(flat.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), int(flat.shape[0]),
                      image.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8)), SIZE, SIZE)
    return image


def to_pbm(image: np.ndarray) -> bytes:
    packed = np.packbits(image.astype(np.uint8), axis=1)
    return b'P4\n%d %d\n' % (image.shape[1], image.shape[0]) + packed.tobytes()
