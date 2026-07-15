from __future__ import annotations

import argparse
import struct
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from melee_sim.hsd_archive import HsdArchive, parse_hsd_archive
from melee_sim.raw_data import raw_data_dir, resolve_data_root
from tools.extraction.char_registry import CHARS

ISO_DIR = raw_data_dir()
DATA_DIR = resolve_data_root()
DATA_SCHEMA_VERSION = 2
SSANIM_VERSION = 5
CAPTURE_CAPTAIN_ANIM_DONOR_CHARACTER = "falcon"

F32 = np.float32


# ─────────────────────────────────────────────────────────────────────────────
# Optional wall-clock timings (extractor profiling)
# ─────────────────────────────────────────────────────────────────────────────


@dataclass
class _Timings:
    msid_total_s: float = 0.0
    msid_count: int = 0

    fobj_interpret_s: float = 0.0

    mtx_srt_s: float = 0.0
    mtx_srt_trig_s: float = 0.0
    mtx_concat_s: float = 0.0

    native_bake_s: float = 0.0

    file_write_s: float = 0.0

    def report(self, *, character: str) -> str:
        total = max(self.msid_total_s, 1.0e-9)
        lines = []
        lines.append(f"[timings] character={character} msids={self.msid_count} total={total:.3f}s")
        lines.append(f"[timings] fobj_interpret={self.fobj_interpret_s:.3f}s ({100.0*self.fobj_interpret_s/total:.1f}%)")
        lines.append(
            f"[timings] mtx_srt={self.mtx_srt_s:.3f}s ({100.0*self.mtx_srt_s/total:.1f}%) "
            f"(trig={self.mtx_srt_trig_s:.3f}s)"
        )
        lines.append(f"[timings] mtx_concat={self.mtx_concat_s:.3f}s ({100.0*self.mtx_concat_s/total:.1f}%)")
        lines.append(f"[timings] native_bake={self.native_bake_s:.3f}s ({100.0*self.native_bake_s/total:.1f}%)")
        lines.append(f"[timings] file_writes={self.file_write_s:.3f}s ({100.0*self.file_write_s/total:.1f}%)")
        return "\n".join(lines)


_TIMINGS: _Timings | None = None


# Fast-ish float32 rounding helper (keeps values as Python floats but with f32 rounding).
def _f32(x: float) -> float:
    return float(F32(x))


def _f32_mul(a: float, b: float) -> float:
    # Float32 multiply with rounding (like `ps_mul` per-lane).
    return _f32(a * b)


def _f32_madd(a: float, b: float, c: float) -> float:
    # Float32 fused multiply-add with rounding (like `ps_madd` per-lane).
    #
    # With float32 inputs, computing `a*b + c` in Python float (f64) is exact for these magnitudes,
    # then the cast rounds once to f32, matching the hardware fma result.
    return _f32(a * b + c)

def _f32_msub(a: float, b: float, c: float) -> float:
    # Float32 fused multiply-subtract with rounding (like `fmsubs`).
    return _f32(a * b - c)

def _f32_fnms(a: float, b: float, c: float) -> float:
    # Float32 fused negative multiply-add (like `fnmadds`: -(a*b) + c).
    return _f32(-(a * b) + c)

# Fused helpers for matrix math (avoid numpy scalar mul rounding).
def _f32_mul_fused(a: float, b: float) -> float:
    return _f32(float(a) * float(b))

def _f32_madd_fused(a: float, b: float, c: float) -> float:
    return _f32(float(a) * float(b) + float(c))

def _f32_msub_fused(a: float, b: float, c: float) -> float:
    return _f32(float(a) * float(b) - float(c))

def _f32_fnms_fused(a: float, b: float, c: float) -> float:
    return _f32(-(float(a) * float(b)) + float(c))


# ─────────────────────────────────────────────────────────────────────────────
# MSL `sinf`/`cosf` (decomp-first)
#
# Melee's sysdolphin `HSD_MtxSRT` uses `sinf`/`cosf` from MSL (`refs/melee/src/MSL/trigf.c`).
# When extracting bone matrices, we must match these float32 trig semantics or we will drift by
# a few ulps (visible as strict item-position mismatches).
# ─────────────────────────────────────────────────────────────────────────────

_MSL_EPSILON = F32(3.45266983e-4)
_MSL_SINCOS_ON_QUADRANT = np.asarray([0, 1, 1, 0, 0, -1, -1, 0], dtype=np.float32)
_MSL_SINCOS_POLY = np.asarray(
    [
        0.0000035287617,
        0.0000003089747,
        -0.0003259365,
        -0.00003657235,
        0.015854323,
        0.0024903931,
        -0.30842513,
        -0.08074551,
        1.0,
        0.7853982,
    ],
    dtype=np.float32,
)
_MSL_FOUR_OVER_PI_M1 = np.asarray(
    # Exact float32 literals from `refs/melee/build/GALE01/asm/MSL/trigf.s` `tmp_float`.
    [0.25, 0.023239374, 0.00000017055572, 0.00000000001867365],
    dtype=np.float32,
)


def _msl_hi_u32(x: np.float32) -> int:
    # Equivalent to `((s32*)&x)[0]` for an f32 value.
    return int(np.asarray(x, dtype=np.float32).view(np.uint32).item())


def _msl_sinf(x: float) -> np.float32:
    # Port of `refs/melee/src/MSL/trigf.c` `sinf(f32)`.
    xf = F32(x)
    # trigf.s: @38 = 0.63661975f (2/pi precomputed as float32).
    z = F32(0.63661975) * xf
    n = int(F32(z - F32(0.5)) if (_msl_hi_u32(xf) & 0x80000000) else F32(z + F32(0.5)))

    y = _f32(xf - F32(n) * F32(2.0))
    y = _f32_madd_fused(_MSL_FOUR_OVER_PI_M1[0], xf, y)
    y = _f32_madd_fused(_MSL_FOUR_OVER_PI_M1[1], xf, y)
    y = _f32_madd_fused(_MSL_FOUR_OVER_PI_M1[2], xf, y)
    y = _f32_madd_fused(_MSL_FOUR_OVER_PI_M1[3], xf, y)
    n &= 3

    if F32(abs(float(y))) < _MSL_EPSILON:
        n2 = n << 1
        t = _f32_mul_fused(y, _MSL_SINCOS_ON_QUADRANT[n2 + 1])
        return _f32_madd_fused(_MSL_SINCOS_POLY[9], t, _MSL_SINCOS_ON_QUADRANT[n2])

    ysq = y * y
    if n & 1:
        n2 = n << 1
        z2 = _f32_madd_fused(_MSL_SINCOS_POLY[0], ysq, _MSL_SINCOS_POLY[2])
        z2 = _f32_madd_fused(z2, ysq, _MSL_SINCOS_POLY[4])
        z2 = _f32_madd_fused(z2, ysq, _MSL_SINCOS_POLY[6])
        z2 = _f32_madd_fused(z2, ysq, _MSL_SINCOS_POLY[8])
        return z2 * _MSL_SINCOS_ON_QUADRANT[n2]

    n2 = n << 1
    z2 = _f32_madd_fused(_MSL_SINCOS_POLY[1], ysq, _MSL_SINCOS_POLY[3])
    z2 = _f32_madd_fused(z2, ysq, _MSL_SINCOS_POLY[5])
    z2 = _f32_madd_fused(z2, ysq, _MSL_SINCOS_POLY[7])
    z2 = _f32_madd_fused(z2, ysq, _MSL_SINCOS_POLY[9])
    z2 = _f32_mul_fused(z2, y)
    return z2 * _MSL_SINCOS_ON_QUADRANT[n2 + 1]


def _msl_cosf(x: float) -> np.float32:
    # Port of `refs/melee/src/MSL/trigf.c` `cosf(f32)`.
    xf = F32(x)
    z = F32(0.63661975) * xf
    n = int(F32(z - F32(0.5)) if (_msl_hi_u32(xf) & 0x80000000) else F32(z + F32(0.5)))

    y = _f32(xf - F32(n) * F32(2.0))
    y = _f32_madd_fused(_MSL_FOUR_OVER_PI_M1[0], xf, y)
    y = _f32_madd_fused(_MSL_FOUR_OVER_PI_M1[1], xf, y)
    y = _f32_madd_fused(_MSL_FOUR_OVER_PI_M1[2], xf, y)
    y = _f32_madd_fused(_MSL_FOUR_OVER_PI_M1[3], xf, y)
    n &= 3

    if F32(abs(float(y))) < _MSL_EPSILON:
        n2 = n << 1
        return _f32_fnms_fused(y, _MSL_SINCOS_ON_QUADRANT[n2], _MSL_SINCOS_ON_QUADRANT[n2 + 1])

    ysq = y * y
    if n & 1:
        n2 = n << 1
        z2 = _f32_madd_fused(_MSL_SINCOS_POLY[1], ysq, _MSL_SINCOS_POLY[3])
        z2 = _f32_madd_fused(z2, ysq, _MSL_SINCOS_POLY[5])
        z2 = _f32_madd_fused(z2, ysq, _MSL_SINCOS_POLY[7])
        z2 = _f32_madd_fused(z2, ysq, _MSL_SINCOS_POLY[9])
        z2 = _f32_mul_fused(z2, y)
        z2 = _f32(-z2)
        return z2 * _MSL_SINCOS_ON_QUADRANT[n2]

    n2 = n << 1
    z2 = _f32_madd_fused(_MSL_SINCOS_POLY[0], ysq, _MSL_SINCOS_POLY[2])
    z2 = _f32_madd_fused(z2, ysq, _MSL_SINCOS_POLY[4])
    z2 = _f32_madd_fused(z2, ysq, _MSL_SINCOS_POLY[6])
    z2 = _f32_madd_fused(z2, ysq, _MSL_SINCOS_POLY[8])
    return z2 * _MSL_SINCOS_ON_QUADRANT[n2 + 1]


def _spl_get_helmite(fterm: float, time: float, p0: float, p1: float, d0: float, d1: float) -> float:
    # Exact instruction order from `refs/melee/build/GALE01/asm/sysdolphin/baselib/spline.s`
    # `splGetHelmite`. Use f32-rounded ops + fmadds to match PPC precision.
    # PPC ABI: f1=fterm, f2=time, f3=p0, f4=p1, f5=d0, f6=d1.
    f1 = _f32(fterm)
    f2 = _f32(time)
    f3 = _f32(p0)
    f4 = _f32(p1)
    f5 = _f32(d0)
    f6 = _f32(d1)

    f11 = _f32_mul_fused(f2, f2)
    f10 = _f32_mul_fused(f1, f1)
    f9 = _f32_mul_fused(f11, f2)
    f0 = _f32_mul_fused(3.0, f11)
    f11 = _f32_mul_fused(f11, f1)
    f9 = _f32_mul_fused(f10, f9)
    f10 = _f32_mul_fused(f0, f10)
    f0 = _f32_mul_fused(2.0, f9)
    f9 = _f32(f9 - f11)
    f1 = _f32_mul_fused(f0, f1)
    f8 = _f32(f9 - f11)
    f0 = _f32(-f1)
    f1 = _f32(f1 - f10)
    f2 = _f32(f2 + f8)
    f0 = _f32(f0 + f10)
    f1 = _f32(1.0 + f1)
    f0 = _f32_mul_fused(f4, f0)
    f0 = _f32_madd_fused(f3, f1, f0)
    f0 = _f32_madd_fused(f5, f2, f0)
    f1 = _f32_madd_fused(f6, f9, f0)
    return _f32(f1)


def _u32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=False)


def _u16_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 2], "big", signed=False)


def _f32_be(buf: bytes, off: int) -> float:
    return struct.unpack(">f", buf[off : off + 4])[0]


def _read_cstr(buf: bytes, off: int) -> str:
    if off < 0 or off >= len(buf):
        return ""
    end = buf.find(b"\x00", off)
    if end < 0:
        end = len(buf)
    return buf[off:end].decode("ascii", errors="replace")


def _mtx_srt(
    scale: tuple[float, float, float],
    rot: tuple[float, float, float],
    trans: tuple[float, float, float],
    parent_scl: tuple[float, float, float] | None,
) -> tuple[float, ...]:
    # Decomp/ASM source: `refs/melee/build/GALE01/asm/sysdolphin/baselib/mtx.s` `HSD_MtxSRT`.
    #
    # Use f32-rounded mul + fused multiply-add/subtract to match the engine's instruction-level
    # rounding behavior.
    sx, sy, sz = (float(F32(scale[0])), float(F32(scale[1])), float(F32(scale[2])))
    rx, ry, rz = (float(F32(rot[0])), float(F32(rot[1])), float(F32(rot[2])))
    tx, ty, tz = (float(F32(trans[0])), float(F32(trans[1])), float(F32(trans[2])))

    t_trig0 = time.perf_counter() if _TIMINGS is not None else 0.0
    sin_x = float(_msl_sinf(rx))
    cos_x = float(_msl_cosf(rx))
    sin_y = float(_msl_sinf(ry))
    cos_y = float(_msl_cosf(ry))
    sin_z = float(_msl_sinf(rz))
    cos_z = float(_msl_cosf(rz))
    if _TIMINGS is not None:
        _TIMINGS.mtx_srt_trig_s += time.perf_counter() - t_trig0

    vec1x_2 = sx
    vec1y_2 = sy
    vec1z_2 = sz
    vec1x_1 = sx
    vec1y_1 = sy
    vec1z_1 = sz
    vec1x = sx
    vec1y = sy
    vec1z = sz

    if parent_scl is not None:
        psx, psy, psz = (
            float(F32(parent_scl[0])),
            float(F32(parent_scl[1])),
            float(F32(parent_scl[2])),
        )
        # ASM uses a double constant + fdiv + frsp; emulate by rounding the reciprocal to f32.
        inv_psx = _f32(1.0 / psx)
        inv_psy = _f32(1.0 / psy)
        inv_psz = _f32(1.0 / psz)

        vec1y_2 = _f32_mul_fused(vec1y_2, _f32_mul_fused(psy, inv_psx))
        vec1z_2 = _f32_mul_fused(vec1z_2, _f32_mul_fused(psz, inv_psx))
        vec1x_1 = _f32_mul_fused(vec1x_1, _f32_mul_fused(psx, inv_psy))
        vec1z_1 = _f32_mul_fused(vec1z_1, _f32_mul_fused(psz, inv_psy))
        vec1x = _f32_mul_fused(vec1x, _f32_mul_fused(psx, inv_psz))
        vec1y = _f32_mul_fused(vec1y, _f32_mul_fused(psy, inv_psz))

    # Mirrors the register-level ordering in `HSD_MtxSRT`.
    x2_cy = _f32_mul_fused(vec1x_2, cos_y)
    x1_cy = _f32_mul_fused(vec1x_1, cos_y)
    neg_x = _f32(-vec1x)

    m00 = _f32_mul_fused(cos_z, x2_cy)
    m10 = _f32_mul_fused(sin_z, x1_cy)
    m20 = _f32_mul_fused(neg_x, sin_y)

    sinx_siny = _f32_mul_fused(sin_x, sin_y)
    cosx_sinz = _f32_mul_fused(cos_x, sin_z)
    cosx_cosz = _f32_mul_fused(cos_x, cos_z)

    inner01 = _f32_msub_fused(cos_z, sinx_siny, cosx_sinz)
    inner11 = _f32_madd_fused(sin_z, sinx_siny, cosx_cosz)

    m01 = _f32_mul_fused(vec1y_2, inner01)
    m11 = _f32_mul_fused(vec1y_1, inner11)
    m21 = _f32_mul_fused(cos_y, _f32_mul_fused(vec1y, sin_x))

    cosx_siny = _f32_mul_fused(cos_x, sin_y)
    sinx_sinz = _f32_mul_fused(sin_x, sin_z)
    sinx_cosz = _f32_mul_fused(sin_x, cos_z)

    inner02 = _f32_madd_fused(cos_z, cosx_siny, sinx_sinz)
    inner12 = _f32_msub_fused(sin_z, cosx_siny, sinx_cosz)

    m02 = _f32_mul_fused(vec1z_2, inner02)
    m12 = _f32_mul_fused(vec1z_1, inner12)
    m22 = _f32_mul_fused(cos_y, _f32_mul_fused(vec1z, cos_x))

    return (m00, m01, m02, tx, m10, m11, m12, ty, m20, m21, m22, tz)


def _mtx_concat(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, ...]:
    # out = a * b for 3x4 matrices (treat as 4x4 with last row [0 0 0 1]).
    #
    # Decomp-first: emulate Dolphin SDK `PSMTXConcat` (paired-single) rounding/association:
    # see `refs/melee/build/GALE01/asm/dolphin/mtx/mtx.s`.
    a00, a01, a02, a03, a10, a11, a12, a13, a20, a21, a22, a23 = a
    b00, b01, b02, b03, b10, b11, b12, b13, b20, b21, b22, b23 = b

    # Row 0
    o00 = _f32_madd_fused(b20, a02, _f32_madd_fused(b10, a01, _f32_mul_fused(b00, a00)))
    o01 = _f32_madd_fused(b21, a02, _f32_madd_fused(b11, a01, _f32_mul_fused(b01, a00)))
    o02 = _f32_madd_fused(b22, a02, _f32_madd_fused(b12, a01, _f32_mul_fused(b02, a00)))
    o03 = _f32_madd_fused(b23, a02, _f32_madd_fused(b13, a01, _f32_mul_fused(b03, a00)))
    o03 = _f32_madd_fused(1.0, a03, o03)

    # Row 1
    o10 = _f32_madd_fused(b20, a12, _f32_madd_fused(b10, a11, _f32_mul_fused(b00, a10)))
    o11 = _f32_madd_fused(b21, a12, _f32_madd_fused(b11, a11, _f32_mul_fused(b01, a10)))
    o12 = _f32_madd_fused(b22, a12, _f32_madd_fused(b12, a11, _f32_mul_fused(b02, a10)))
    o13 = _f32_madd_fused(b23, a12, _f32_madd_fused(b13, a11, _f32_mul_fused(b03, a10)))
    o13 = _f32_madd_fused(1.0, a13, o13)

    # Row 2
    o20 = _f32_madd_fused(b20, a22, _f32_madd_fused(b10, a21, _f32_mul_fused(b00, a20)))
    o21 = _f32_madd_fused(b21, a22, _f32_madd_fused(b11, a21, _f32_mul_fused(b01, a20)))
    o22 = _f32_madd_fused(b22, a22, _f32_madd_fused(b12, a21, _f32_mul_fused(b02, a20)))
    o23 = _f32_madd_fused(b23, a22, _f32_madd_fused(b13, a21, _f32_mul_fused(b03, a20)))
    o23 = _f32_madd_fused(1.0, a23, o23)

    return (o00, o01, o02, o03, o10, o11, o12, o13, o20, o21, o22, o23)


# --- Minimal FObj interpreter (sysdolphin/baselib/fobj.c port, decomp-first) ---


HSD_A_OP_CON = 1
HSD_A_OP_LIN = 2
HSD_A_OP_SPL0 = 3
HSD_A_OP_SPL = 4
HSD_A_OP_SLP = 5
HSD_A_OP_KEY = 6

HSD_A_FRAC_FLOAT = 0 << 5
HSD_A_FRAC_S16 = 1 << 5
HSD_A_FRAC_U16 = 2 << 5
HSD_A_FRAC_S8 = 3 << 5
HSD_A_FRAC_U8 = 4 << 5

FOBJ_LOAD_DATA0 = 1
FOBJ_LOAD_DATA = 2
FOBJ_LOAD_WAIT = 3


def _parse_float(ad: memoryview, pos: int, frac: int) -> tuple[float, int]:
    # Mirrors fobj.c parseFloat: packed values are little-endian within the ad stream.
    if frac == HSD_A_FRAC_FLOAT:
        if pos + 4 > len(ad):
            return 0.0, pos
        d = ad[pos] | (ad[pos + 1] << 8) | (ad[pos + 2] << 16) | (ad[pos + 3] << 24)
        return float(struct.unpack("<f", struct.pack("<I", d & 0xFFFF_FFFF))[0]), pos + 4

    denom = 1 << (frac & 0x1F)
    kind = frac & 0xE0
    if kind == HSD_A_FRAC_S8:
        if pos + 1 > len(ad):
            return 0.0, pos
        numer = struct.unpack("<b", bytes([ad[pos]]))[0]
        return _f32(float(numer) / denom), pos + 1
    if kind == HSD_A_FRAC_U8:
        if pos + 1 > len(ad):
            return 0.0, pos
        numer = ad[pos]
        return _f32(float(numer) / denom), pos + 1
    if kind == HSD_A_FRAC_S16:
        if pos + 2 > len(ad):
            return 0.0, pos
        numer = struct.unpack("<h", bytes([ad[pos], ad[pos + 1]]))[0]
        return _f32(float(numer) / denom), pos + 2
    if kind == HSD_A_FRAC_U16:
        if pos + 2 > len(ad):
            return 0.0, pos
        numer = ad[pos] | (ad[pos + 1] << 8)
        return _f32(float(numer) / denom), pos + 2
    return 0.0, pos


def _parse_opcode(ad: memoryview, pos: int) -> int:
    return int(ad[pos] & 0xF)


def _parse_pack_info(ad: memoryview, pos: int) -> tuple[int, int]:
    d = int(ad[pos])
    pos += 1
    nb_pack = ((d >> 4) & 7) + 1
    shift = 3
    if (d & 0x80) == 0:
        return nb_pack, pos
    while True:
        d = int(ad[pos])
        pos += 1
        nb_pack += (d & 0x7F) << shift
        shift += 7
        if (d & 0x80) == 0:
            break
    return nb_pack, pos


def _parse_wait(ad: memoryview, pos: int) -> tuple[int, int]:
    wait = 0
    shift = 0
    while True:
        d = int(ad[pos])
        pos += 1
        wait |= (d & 0x7F) << shift
        shift += 7
        if (d & 0x80) == 0:
            break
    return wait, pos


@dataclass
class _FObj:
    ad: memoryview
    ad_head: int
    length: int
    startframe: int
    obj_type: int
    frac_value: int
    frac_slope: int

    # stateful fields
    state: int = FOBJ_LOAD_DATA0
    op: int = 0
    op_intrp: int = 0
    time: float = 0.0
    nb_pack: int = 0
    fterm: int = 0
    p0: float = 0.0
    p1: float = 0.0
    d0: float = 0.0
    d1: float = 0.0
    flags_20: bool = False
    flags_40: bool = False
    flags_80: bool = False
    pos: int = 0

    def req_anim(self, frame: float) -> None:
        self.pos = self.ad_head
        self.time = _f32(float(self.startframe) + float(frame))
        self.op = 0
        self.op_intrp = 0
        self.flags_40 = False
        self.flags_80 = False
        self.flags_20 = False
        self.nb_pack = 0
        self.fterm = 0
        self.p0 = 0.0
        self.p1 = 0.0
        self.d0 = 0.0
        self.d1 = 0.0
        self.state = FOBJ_LOAD_DATA0

    def _launch_key_data(self) -> None:
        if self.flags_40:
            self.op_intrp = self.op
            self.flags_40 = False
            self.flags_80 = True
            self.p0 = self.p1

    def _anim_con(self) -> int:
        self.p0 = self.p1
        self.p1, self.pos = _parse_float(self.ad, self.pos, self.frac_value)
        if self.op_intrp != HSD_A_OP_SLP:
            self.d0 = self.d1
            self.d1 = 0.0
        return FOBJ_LOAD_WAIT if self.state == FOBJ_LOAD_DATA0 else 4

    def _anim_lin(self) -> int:
        self.p0 = self.p1
        self.p1, self.pos = _parse_float(self.ad, self.pos, self.frac_value)
        if self.op_intrp != HSD_A_OP_SLP:
            self.d0 = self.d1
            self.d1 = 0.0
        return FOBJ_LOAD_WAIT if self.state == FOBJ_LOAD_DATA0 else 4

    def _anim_spl0(self) -> int:
        self.p0 = self.p1
        self.d0 = self.d1
        self.p1, self.pos = _parse_float(self.ad, self.pos, self.frac_value)
        self.d1 = 0.0
        return FOBJ_LOAD_WAIT if self.state == FOBJ_LOAD_DATA0 else 4

    def _anim_spl(self) -> int:
        self.p0 = self.p1
        self.p1, self.pos = _parse_float(self.ad, self.pos, self.frac_value)
        self.d0 = self.d1
        self.d1, self.pos = _parse_float(self.ad, self.pos, self.frac_slope)
        return FOBJ_LOAD_WAIT if self.state == FOBJ_LOAD_DATA0 else 4

    def _anim_slp(self) -> int:
        self.d0 = self.d1
        self.d1, self.pos = _parse_float(self.ad, self.pos, self.frac_slope)
        return self.state

    def _anim_key(self) -> int:
        self._launch_key_data()
        self.p1, self.pos = _parse_float(self.ad, self.pos, self.frac_value)
        self.flags_40 = True
        return FOBJ_LOAD_WAIT if self.state == FOBJ_LOAD_DATA0 else 4

    def _load_data(self) -> int:
        if self.pos - self.ad_head >= self.length:
            return 6
        self.op_intrp = self.op
        if self.nb_pack == 0:
            self.op = _parse_opcode(self.ad, self.pos)
            self.nb_pack, self.pos = _parse_pack_info(self.ad, self.pos)
        self.nb_pack -= 1

        if self.op == HSD_A_OP_CON:
            self.state = self._anim_con()
        elif self.op == HSD_A_OP_LIN:
            self.state = self._anim_lin()
        elif self.op == HSD_A_OP_SPL0:
            self.state = self._anim_spl0()
        elif self.op == HSD_A_OP_SPL:
            self.state = self._anim_spl()
        elif self.op == HSD_A_OP_SLP:
            self.state = self._anim_slp()
        elif self.op == HSD_A_OP_KEY:
            self.state = self._anim_key()
        else:
            self.state = 0
        return self.state

    def _load_wait(self) -> int:
        if self.pos - self.ad_head >= self.length:
            return 6
        self.fterm, self.pos = _parse_wait(self.ad, self.pos)
        self.flags_20 = True
        self.state = FOBJ_LOAD_DATA
        return self.state

    def _update_anim(self) -> float | None:
        if self.op_intrp == HSD_A_OP_KEY:
            if self.flags_80:
                self.flags_80 = False
                return self.p0
            return None

        if self.op_intrp == HSD_A_OP_CON:
            return self.p1 if self.time >= self.fterm else self.p0

        if self.op_intrp == HSD_A_OP_LIN:
            if self.flags_20:
                self.flags_20 = False
                if self.fterm != 0:
                    self.d0 = _f32((self.p1 - self.p0) / float(self.fterm))
                else:
                    self.d0 = 0.0
                    self.p0 = self.p1
            # Decomp (`FObjUpdateAnim`): `fobjdata.fv = fobj->d0 * fobj->time + fobj->p0;`
            # Use fused-style rounding to match PPC `fmadds` precision.
            return _f32_madd_fused(self.d0, self.time, self.p0)

        if self.op_intrp in (HSD_A_OP_SPL0, HSD_A_OP_SPL, HSD_A_OP_SLP):
            if self.fterm == 0:
                return self.p1
            # Decomp: splGetHelmite(1.0 / fobj->fterm, fobj->time, p0, p1, d0, d1)
            # Use the exact instruction ordering from `spline.s` for f32-accurate results.
            fterm = _f32(1.0 / float(self.fterm))
            return _spl_get_helmite(fterm, self.time, self.p0, self.p1, self.d0, self.d1)

        return None

    def interpret(self, rate: float) -> list[float]:
        # Returns zero or more update values produced during this interpret call.
        out: list[float] = []
        if self.state == 0:
            return out
        self.time = _f32(self.time + float(rate))
        if self.time < 0.0:
            return out

        fterm = 0.0
        # Mirror HSD_FObjInterpretAnim: EOF helper returns are local interpreter state for this call,
        # not persisted `self.state = 6`.
        # refs/melee/src/sysdolphin/baselib/fobj.c::{FObjLoadData,FObjLoadWait,HSD_FObjInterpretAnim}
        state = self.state
        iters = 0
        while True:
            iters += 1
            if iters > 100_000:
                # Guardrail against malformed/corrupted animation streams causing non-termination.
                return out
            st = state
            if st == 6:
                self.time = _f32(self.time + fterm)
                self._launch_key_data()
                v = self._update_anim()
                if v is not None:
                    out.append(v)
                return out
            if st in (FOBJ_LOAD_DATA0, FOBJ_LOAD_DATA):
                state = self._load_data()
                continue
            if st == FOBJ_LOAD_WAIT:
                if self.flags_80:
                    v = self._update_anim()
                    if v is not None:
                        out.append(v)
                state = self._load_wait()
                continue
            if st == 4:
                if self.fterm <= self.time:
                    fterm = float(self.fterm)
                    self.time = _f32(self.time - float(self.fterm))
                    self.state = FOBJ_LOAD_WAIT
                    state = FOBJ_LOAD_WAIT
                    continue
                v = self._update_anim()
                if v is not None:
                    out.append(v)
                self.state = 5
                state = 5
                return out
            if st == 5:
                self.state = 4
                state = 4
                continue
            return out


def validate_ssanimt1_file(path: Path) -> None:
    """Validate SSANIMT1 structural records and FObj payload readability.

    Runtime startup only validates SSANIMT1 offsets so sim/eval startup stays cheap. The extractor
    and data-contract tests own the deeper HSD FObj bytecode validation that catches stale or
    corrupt payloads before generated data is accepted.
    """

    buf = path.read_bytes()
    if len(buf) < 16 or buf[:8] != b"SSANIMT1":
        raise ValueError(f"{path}: bad SSANIMT1 header")
    version, local_count, anim_count = struct.unpack_from("<IHH", buf, 8)
    if int(version) != 3:
        raise ValueError(f"{path}: unsupported SSANIMT1 version {version}")
    off = 16
    local_bytes = int(local_count) + 2 * int(local_count) + 4 * int(local_count)
    if off + local_bytes > len(buf):
        raise ValueError(f"{path}: truncated SSANIMT1 local tables")
    off += local_bytes

    for _anim_i in range(int(anim_count)):
        if off + 8 > len(buf):
            raise ValueError(f"{path}: truncated SSANIMT1 anim header")
        msid, _end_frame = struct.unpack_from("<Hf", buf, off)
        off += 8
        for _part_i in range(int(local_count)):
            if off + 2 > len(buf):
                raise ValueError(f"{path}: truncated SSANIMT1 part track header msid={msid}")
            part = int(buf[off])
            n_tracks = int(buf[off + 1])
            off += 2
            for track_i in range(n_tracks):
                if off + 8 > len(buf):
                    raise ValueError(
                        f"{path}: truncated SSANIMT1 FObj header msid={msid} part={part} track={track_i}"
                    )
                obj_type, frac_value, frac_slope, _pad, startframe, length = struct.unpack_from(
                    "<BBBBHH", buf, off
                )
                off += 8
                if off + int(length) > len(buf):
                    raise ValueError(
                        f"{path}: truncated SSANIMT1 FObj payload msid={msid} part={part} track={track_i}"
                    )
                ad = memoryview(buf)[off : off + int(length)]
                off += int(length)
                if int(length) == 0:
                    continue
                fo = _FObj(
                    ad=ad,
                    ad_head=0,
                    length=int(length),
                    startframe=int(startframe),
                    obj_type=int(obj_type),
                    frac_value=int(frac_value),
                    frac_slope=int(frac_slope),
                )
                fo.req_anim(0.0)
                try:
                    fo.interpret(0.0)
                except IndexError as exc:
                    raise ValueError(
                        f"{path}: corrupt SSANIMT1 FObj payload msid={msid} part={part} "
                        f"track={track_i}"
                    ) from exc
    if off != len(buf):
        raise ValueError(f"{path}: trailing SSANIMT1 bytes")


@dataclass(frozen=True)
class _FigaTrack:
    length: int
    startframe: int
    obj_type: int
    frac_value: int
    frac_slope: int
    ad_abs: int


@dataclass(frozen=True)
class _FigaTree:
    type: int
    flags: int
    frames: float
    nodes: list[int]
    tracks: list[_FigaTrack]


def _read_figatree(arc: HsdArchive, abs_off: int) -> _FigaTree:
    buf = arc.buf
    typ = int.from_bytes(buf[abs_off : abs_off + 4], "big", signed=True)
    flags = _u32_be(buf, abs_off + 4)
    frames = _f32_be(buf, abs_off + 8)
    nodes_ptr = _u32_be(buf, abs_off + 0x0C)
    tracks_ptr = _u32_be(buf, abs_off + 0x10)
    nodes_abs = arc.data_base + nodes_ptr
    tracks_abs = arc.data_base + tracks_ptr

    nodes: list[int] = []
    p = nodes_abs
    while True:
        b = buf[p]
        p += 1
        v = struct.unpack("<b", bytes([b]))[0]
        if v == -1:
            break
        nodes.append(int(v))

    total_tracks = sum(nodes)
    tracks: list[_FigaTrack] = []
    off = tracks_abs
    for _ in range(total_tracks):
        length = _u16_be(buf, off + 0)
        startframe = _u16_be(buf, off + 2)
        obj_type = buf[off + 4]
        frac_value = buf[off + 5]
        frac_slope = buf[off + 6]
        ad_ptr = _u32_be(buf, off + 8)
        tracks.append(
            _FigaTrack(
                length=length,
                startframe=startframe,
                obj_type=obj_type,
                frac_value=frac_value,
                frac_slope=frac_slope,
                ad_abs=arc.data_base + ad_ptr,
            )
        )
        off += 12

    return _FigaTree(type=typ, flags=flags, frames=frames, nodes=nodes, tracks=tracks)


def _move_key_idx(name: str) -> int | None:
    mapping = {
        "ftCo_SM_Attack11": 0,
        "ftCo_SM_AttackDash": 1,
        "ftCo_SM_AttackS3": 2,
        "ftCo_SM_AttackHi3": 3,
        "ftCo_SM_AttackLw3": 4,
        "ftCo_SM_AttackS4": 5,
        "ftCo_SM_AttackHi4": 6,
        "ftCo_SM_AttackLw4": 7,
        "ftCo_SM_AttackAirN": 8,
        "ftCo_SM_AttackAirF": 9,
        "ftCo_SM_AttackAirB": 10,
        "ftCo_SM_AttackAirHi": 11,
        "ftCo_SM_AttackAirLw": 12,
    }
    return mapping.get(name)


def _extra_anim_msids() -> list[int]:
    # Common `ftCo_Submotion` IDs (from doldecomp `ftCommon/forward.h`) that we
    # bake so hurt capsules can follow the animated pose outside Attack states.
    curated = [
        238,  # ftCo_SM_EntryStart (used for entry pose even when anim_id is -1)
        2,  # ftCo_SM_Wait1_0
        7,  # ftCo_SM_WalkSlow
        8,  # ftCo_SM_WalkMiddle
        9,  # ftCo_SM_WalkFast
        10,  # ftCo_SM_Turn
        12,  # ftCo_SM_Dash
        13,  # ftCo_SM_Run
        14,  # ftCo_SM_RunBrake
        15,  # ftCo_SM_Kneebend
        16,  # ftCo_SM_JumpF
        17,  # ftCo_SM_JumpB
        # ECB coverage (spurious Landing fix): include JumpAerialF/B so ECB sampling does not
        # fall back to 0 for these msids.
        # Decomp source: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.
        18,  # ftCo_SM_JumpAerialF
        19,  # ftCo_SM_JumpAerialB
        20,  # ftCo_SM_Fall
        26,  # ftCo_SM_FallSpecial
        30,  # ftCo_SM_Squat
        31,  # ftCo_SM_SquatWait
        35,  # ftCo_SM_Landing
        36,  # ftCo_SM_LandingFallSpecial
        73,  # ftCo_SM_LandingAirN
        74,  # ftCo_SM_LandingAirF
        75,  # ftCo_SM_LandingAirB
        76,  # ftCo_SM_LandingAirHi
        77,  # ftCo_SM_LandingAirLw
        # Damage / hitstun (needed so hurtcaps/hitboxes follow pose immediately after being hit).
        165,  # ftCo_SM_DamageHi1
        168,  # ftCo_SM_DamageN1
        171,  # ftCo_SM_DamageLw1
        174,  # ftCo_SM_DamageAir1
        177,  # ftCo_SM_DamageFlyHi
        178,  # ftCo_SM_DamageFlyN
        179,  # ftCo_SM_DamageFlyLw
        180,  # ftCo_SM_DamageFlyTop (needed for ECB grounding parity during tumble landings; refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`)
        181,  # ftCo_SM_DamageFlyRoll (ECB coverage; action selection is gated elsewhere; refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`)
        # Downed / knockdown (needed for DownAttack hitbox placement + hurtcaps during knockdown).
        183,  # ftCo_SM_DownBoundU
        184,  # ftCo_SM_DownWaitU
        187,  # ftCo_SM_DownAttackU
        188,  # ftCo_SM_DownFowardU (downed roll forward)
        189,  # ftCo_SM_DownBackU (downed roll back)
        191,  # ftCo_SM_DownBoundD
        192,  # ftCo_SM_DownWaitD
        195,  # ftCo_SM_DownAttackD
        196,  # ftCo_SM_DownFowardD (downed roll forward)
        197,  # ftCo_SM_DownBackD (downed roll back)
        37,  # ftCo_SM_GuardOn (needed for GuardOn blend timeline length / x2E8)
        38,  # ftCo_SM_Guard
        39,  # ftCo_SM_GuardOff
        40,  # ftCo_SM_GuardDamage (shieldstun / GuardSetOff)
        41,  # ftCo_SM_EscapeN
        42,  # ftCo_SM_EscapeF
        43,  # ftCo_SM_EscapeB
        44,  # ftCo_SM_EscapeAir
        # Grabs / throws (keep ECB/hurtcaps aligned during capture / throw states).
        242,  # ftCo_SM_Catch
        243,  # ftCo_SM_CatchDash
        244,  # ftCo_SM_CatchWait
        245,  # ftCo_SM_CatchAttack
        247,  # ftCo_SM_ThrowF
        248,  # ftCo_SM_ThrowB
        249,  # ftCo_SM_ThrowHi
        250,  # ftCo_SM_ThrowLw
        251,  # ftCo_SM_CapturePulledHi
        252,  # ftCo_SM_CaptureWaitHi
        253,  # ftCo_SM_CaptureDamageHi
        254,  # ftCo_SM_CapturePulledLw
        255,  # ftCo_SM_CaptureWaitLw
        256,  # ftCo_SM_CaptureDamageLw
        262,  # ftCo_SM_ThrownF
        263,  # ftCo_SM_ThrownB
        264,  # ftCo_SM_ThrownHi
        265,  # ftCo_SM_ThrownLw
        # Ledge/cliff states (root-motion + collision fidelity for replay parity).
        216,  # ftCo_SM_CliffCatch
        217,  # ftCo_SM_CliffWait
        219,  # ftCo_SM_CliffClimbSlow
        220,  # ftCo_SM_CliffClimbQuick
        221,  # ftCo_SM_CliffAttackSlow
        222,  # ftCo_SM_CliffAttackQuick
        223,  # ftCo_SM_CliffEscapeSlow
        224,  # ftCo_SM_CliffEscapeQuick
        225,  # ftCo_SM_CliffJumpSlow1
        226,  # ftCo_SM_CliffJumpSlow2
        227,  # ftCo_SM_CliffJumpQuick1
        228,  # ftCo_SM_CliffJumpQuick2
    ]
    # Foundation coverage pass:
    # include the full GALE01 common submotion domain so replay-used common states
    # (Damage*/Passive*/Guard*/Capture*/etc.) always have pose tracks when present.
    #
    # Decomp source:
    # - refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_Submotion
    #   (`ftCo_SM_Count = 295`, so valid common submotions are [0, 295)).
    common_all = list(range(0, 295))
    return sorted(set(curated) | set(common_all))


def _special_anim_msids(character: str) -> list[int]:
    # Include all character-specific special submotions (B moves) so ECB/hurtcaps
    # can follow the correct pose during specials even before we fully simulate them.
    out: list[int] = []
    for msid in range(0, 512):
        entry = _msid_anim_entry(character, msid)
        if entry is None:
            continue
        name, _x4, _x8, _flags_u8 = entry
        # Names are AJ symbols (e.g. PlyFox5K_Share_ACTION_SpecialNStart_figatree).
        if "Special" in name:
            out.append(int(msid))
    return sorted(set(out))


def _figatree_source_character(character: str, msid: int) -> str:
    """Return the fighter file whose FigaTree is applied to ``character``.

    CaptureCaptain enters with Falcon's gobj as Fighter_ChangeMotionState's ``arg3`` animation
    source. Vanilla therefore applies Falcon's TCaptainSpecialHi FigaTree to the captured fighter's
    own skeleton. Cross-bake that donor tree into each target-character pose artifact so runtime
    attachment and hurtbox queries consume the same source relation.

    refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCaptain.c::ftCo_8009CA0C
    refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    refs/melee/src/melee/ft/ftdata.c::ftData_80085CD8
    """
    if msid == 276:  # ftCo_SM_CaptureCaptain
        return CAPTURE_CAPTAIN_ANIM_DONOR_CHARACTER
    return character


def _ftkind(character: str) -> int:
    # From doldecomp `enum FighterKind` ids (see prior ModelDb code).
    if character in CHARS:
        return CHARS[character].internal_id
    return {
        "puff": 0x0F,
        "peach": 0x09,
    }[character]


def _fighter_prefix(character: str) -> str:
    if character in CHARS:
        return Path(CHARS[character].pl_dat).stem
    return {
        "peach": "PlPe",
        "puff": "PlPr",
    }[character]


def _ftdata_symbol(character: str) -> str:
    if character in CHARS:
        return CHARS[character].ftdata_symbol
    return {
        "peach": "ftDataPeach",
        "puff": "ftDataPurin",
    }[character]


def _ftdata_xc_count(character: str) -> int:
    """Return `ftData_Table_Unk0[kind].count` for this character.

    Decomp: `refs/melee/src/melee/ft/ftdata.c` `ftData_Table_Unk0`.
    This count is the length of the per-anim-id tables (`ftData.xC` and `ftData.x10`),
    where `ftData.x10` is a packed `[blend_frames, dynamics_tree_idx]` pair per anim id.
    """
    # Values are ftData_Table_Unk0[internal_id].count from refs/melee/src/melee/ft/ftdata.c
    # (FTKIND_MAX rows indexed by FighterKind): fox=row 1, falcon=row 2, sheik=row 7,
    # marth=row 18, zelda=row 19, falco=row 22.
    return CHARS[character].anim_table_count


def _write_anim_blend_data(character: str, out_dir: Path) -> Path:
    """Write per-anim-id blend/dynamics bytes to `data/anims/<character>.blend.bin`.

    Layout (v1):
    - magic: 8 bytes "SSANIMB1"
    - version: u32 LE (1)
    - anim_count: u16 LE (`ftData_Table_Unk0[kind].count`)
    - bytes: [u8; anim_count*2] (pairs: [blend_frames, dynamics_tree_idx])
    """
    prefix = _fighter_prefix(character)
    base = ISO_DIR / f"{prefix}.dat"
    arc = parse_hsd_archive(base.read_bytes())
    ftdata_abs = arc.get_public_offset(_ftdata_symbol(character))
    if ftdata_abs is None:
        raise RuntimeError(f"{base.name} missing ftData public symbol")

    x10_ptr = _u32_be(arc.buf, ftdata_abs + 0x10)
    if x10_ptr == 0:
        raise RuntimeError(f"{base.name} ftData.x10 null (blend data)")
    x10_abs = arc.data_base + x10_ptr

    anim_count = _ftdata_xc_count(character)
    want = anim_count * 2
    if x10_abs + want > len(arc.buf):
        raise RuntimeError(f"{base.name} ftData.x10 truncated (want {want} bytes)")
    raw = arc.buf[x10_abs : x10_abs + want]

    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"{character}.blend.bin"
    with out_path.open("wb") as f:
        f.write(b"SSANIMB1")
        f.write(struct.pack("<I", 1))
        f.write(struct.pack("<H", anim_count))
        f.write(raw)
    return out_path


def _read_fighter_dynamics(character: str) -> list[dict[str, object]]:
    """Return ftData.x2C dynamic-bone descriptors from the fighter DAT.

    Decomp owner:
    - refs/melee/src/melee/ft/ftdynamics.c::ftCo_8009CF84
    - refs/melee/src/melee/ft/ftdynamics.c::ftCo_8009DD94
    - refs/melee/src/melee/lb/lb_00F9.c::{lb_8000FD48,lb_80011710}

    The raw `BoneDynamicsDesc` carries a root Fighter_Part, a chain count, and per-node
    lb_00F9 dynamics constants. Unsupported/missing dynamics return an empty list.
    """
    prefix = _fighter_prefix(character)
    base = ISO_DIR / f"{prefix}.dat"
    arc = parse_hsd_archive(base.read_bytes())
    ftdata_abs = arc.get_public_offset(_ftdata_symbol(character))
    if ftdata_abs is None:
        raise RuntimeError(f"{base.name} missing ftData public symbol")

    dyn_ptr = _u32_be(arc.buf, ftdata_abs + 0x2C)
    if dyn_ptr == 0:
        return []
    dyn_abs = arc.data_base + dyn_ptr
    if dyn_abs + 0x14 > len(arc.buf):
        raise RuntimeError(f"{base.name} ftData.x2C out of bounds")

    dyn_count = int(_u32_be(arc.buf, dyn_abs + 0x00))
    bones_ptr = _u32_be(arc.buf, dyn_abs + 0x04)
    collider_count = int(_u32_be(arc.buf, dyn_abs + 0x08))
    colliders_ptr = _u32_be(arc.buf, dyn_abs + 0x0C)
    if dyn_count <= 0 or bones_ptr == 0:
        return []
    bones_abs = arc.data_base + bones_ptr
    colliders: list[tuple[int, tuple[float, float, float], float]] = []
    if collider_count > 0 and colliders_ptr != 0:
        colliders_abs = arc.data_base + colliders_ptr
        for i in range(collider_count):
            ent_abs = colliders_abs + i * 0x14
            if ent_abs + 0x14 > len(arc.buf):
                raise RuntimeError(f"{base.name} dynamic collider truncated: index={i}")
            part = int(_u32_be(arc.buf, ent_abs + 0x00))
            offset = tuple(float(_f32_be(arc.buf, ent_abs + 0x04 + j * 4)) for j in range(3))
            radius = float(_f32_be(arc.buf, ent_abs + 0x10))
            colliders.append((part, (offset[0], offset[1], offset[2]), radius))

    out: list[dict[str, object]] = []
    for i in range(dyn_count):
        # Decomp: BoneDynamicsDesc is `bone_id + DynamicsDesc`, i.e. 0x18 bytes.
        # refs/melee/src/melee/lb/types.h::{DynamicsDesc,BoneDynamicsDesc}
        # refs/melee/build/GALE01/asm/melee/ft/ftdynamics.s::ftCo_8009CB40 (mulli index, 0x18)
        bone_abs = bones_abs + i * 0x18
        if bone_abs + 0x18 > len(arc.buf):
            raise RuntimeError(f"{base.name} dynamic bone descriptor truncated: index={i}")
        root_part = int(_u32_be(arc.buf, bone_abs + 0x00))
        data_ptr = _u32_be(arc.buf, bone_abs + 0x04)
        chain_count = int(_u32_be(arc.buf, bone_abs + 0x08))
        pos = tuple(float(_f32_be(arc.buf, bone_abs + 0x0C + j * 4)) for j in range(3))
        entries: list[tuple[float, ...]] = []
        if data_ptr != 0:
            data_abs = arc.data_base + data_ptr
            for j in range(max(0, chain_count)):
                ent_abs = data_abs + j * 0x3C
                if ent_abs + 0x3C > len(arc.buf):
                    raise RuntimeError(
                        f"{base.name} dynamic constants truncated: set={i} entry={j}"
                    )
                entries.append(tuple(float(_f32_be(arc.buf, ent_abs + k * 4)) for k in range(15)))
        out.append(
            {
                "root_part": root_part,
                "chain_count": chain_count,
                "pos": pos,
                "entries": entries,
                "colliders": colliders,
            }
        )
    return out


def _default_costume_dat_and_joint(character: str) -> tuple[str, str]:
    """Return (dat_filename, joint_name) for costume 0 (Nr).

    Decomp: ftdata.c `CostumeListsForeachCharacter` + per-character
    `ft*Init_CostumeStrings` tables (e.g. ftFox/ftFx_Init.c).
    Runtime uses `CostumeListsForeachCharacter[fp->kind].costume_list[costume_id].joint`.
    """
    info = CHARS.get(character)
    if info is not None:
        return info.costume_dat, info.costume_joint
    return {
        "peach": ("PlPeNr.dat", "PlyPeach5K_Share_joint"),
        "puff": ("PlPrNr.dat", "PlyPurin5K_Share_joint"),
    }[character]


def _load_parts_table_for_ftkind(ftkind: int) -> tuple[list[int], list[int], list[int]]:
    """Return (part_to_joint, inserted_parts, joint_to_part).

    - `part_to_joint`: maps Fighter_Part ids to indices into fp->parts.
    - `inserted_parts`: indices into fp->parts that correspond to inserted joints
      (see `Fighter_804D6540` + `ftParts_8007506C`).
    - `joint_to_part`: maps indices into fp->parts back to semantic Fighter_Part ids.
    """
    plco = parse_hsd_archive((ISO_DIR / "PlCo.dat").read_bytes())
    ft_load_common = plco.get_public_offset("ftLoadCommonData")
    if ft_load_common is None:
        raise RuntimeError("PlCo.dat missing ftLoadCommonData")
    p_data = [_u32_be(plco.buf, ft_load_common + i * 4) for i in range(23)]

    ft_parts_table_abs = plco.data_base + p_data[4]
    ft_parts_tbl_ptr = _u32_be(plco.buf, ft_parts_table_abs + ftkind * 4)
    ft_parts_tbl_abs = plco.data_base + ft_parts_tbl_ptr
    joint_to_part_ptr = _u32_be(plco.buf, ft_parts_tbl_abs + 0)
    part_to_joint_ptr = _u32_be(plco.buf, ft_parts_tbl_abs + 4)
    parts_num = _u32_be(plco.buf, ft_parts_tbl_abs + 8)
    joint_to_part_abs = plco.data_base + joint_to_part_ptr
    part_to_joint_abs = plco.data_base + part_to_joint_ptr
    joint_to_part = list(plco.buf[joint_to_part_abs : joint_to_part_abs + parts_num])
    part_to_joint = list(plco.buf[part_to_joint_abs : part_to_joint_abs + parts_num])

    # Fighter_804D6540 inserted-joint table (optional).
    inserted_parts: list[int] = []
    f6540_ptr = p_data[5]
    if f6540_ptr:
        f6540_tbl_abs = plco.data_base + f6540_ptr
        entry_ptr = _u32_be(plco.buf, f6540_tbl_abs + ftkind * 4)
        if entry_ptr:
            entry_abs = plco.data_base + entry_ptr
            x0_ptr = _u32_be(plco.buf, entry_abs + 0)
            count = _u32_be(plco.buf, entry_abs + 4)
            if x0_ptr and count:
                x0_abs = plco.data_base + x0_ptr
                for i in range(min(count, 256)):
                    # Fighter_804D6540_x0_t is 4 bytes; `x0` is the first byte and is the part index.
                    inserted_parts.append(int(plco.buf[x0_abs + i * 4 + 0]))
    inserted_parts = sorted(set(inserted_parts))

    return part_to_joint, inserted_parts, joint_to_part


def _load_parts_table(character: str) -> tuple[list[int], list[int], list[int]]:
    return _load_parts_table_for_ftkind(_ftkind(character))


def _read_rest_srt_and_parents(character: str) -> tuple[
    list[tuple[float, float, float]],
    list[tuple[float, float, float]],
    list[tuple[float, float, float]],
    list[int],
    list[int],
]:
    """Return (rest_rot, rest_scl, rest_pos, parent_part, part_flags) aligned to fp->parts indices."""
    # Use the costume joint (not ftData.x5C) as the runtime skeleton root.
    # Decomp: ftData_80085820 + Fighter_UnkUpdateCostumeJoint_800686E4.
    costume_dat, costume_joint = _default_costume_dat_and_joint(character)
    base = ISO_DIR / costume_dat
    arc = parse_hsd_archive(base.read_bytes())
    root_abs = arc.get_public_offset(costume_joint)
    if root_abs is None:
        raise RuntimeError(f"{base.name} missing costume joint {costume_joint!r}")

    # Load part_to_joint + inserted_parts for alignment and array length.
    part_to_joint, inserted_parts, _ = _load_parts_table(character)
    parts_num = len(part_to_joint)
    is_skip = [False] * parts_num
    for s in inserted_parts:
        if 0 <= s < parts_num:
            is_skip[s] = True

    # Traverse HSD_Joint tree (pre-order), recording per-node parent indices.
    node_rot: list[tuple[float, float, float]] = []
    node_scl: list[tuple[float, float, float]] = []
    node_pos: list[tuple[float, float, float]] = []
    node_parent: list[int] = []
    node_flags: list[int] = []

    stack: list[tuple[int, int]] = [(root_abs, -1)]
    while stack:
        node_abs, parent_idx = stack.pop()
        idx = len(node_rot)
        node_parent.append(parent_idx)
        node_flags.append(_u32_be(arc.buf, node_abs + 0x04))
        node_rot.append(
            (
                _f32_be(arc.buf, node_abs + 0x14),
                _f32_be(arc.buf, node_abs + 0x18),
                _f32_be(arc.buf, node_abs + 0x1C),
            )
        )
        node_scl.append(
            (
                _f32_be(arc.buf, node_abs + 0x20),
                _f32_be(arc.buf, node_abs + 0x24),
                _f32_be(arc.buf, node_abs + 0x28),
            )
        )
        node_pos.append(
            (
                _f32_be(arc.buf, node_abs + 0x2C),
                _f32_be(arc.buf, node_abs + 0x30),
                _f32_be(arc.buf, node_abs + 0x34),
            )
        )

        child_ptr = _u32_be(arc.buf, node_abs + 0x08)
        next_ptr = _u32_be(arc.buf, node_abs + 0x0C)
        if next_ptr:
            stack.append((arc.data_base + next_ptr, parent_idx))
        if child_ptr:
            stack.append((arc.data_base + child_ptr, idx))

    # Map node indices onto parts indices by inserting skip placeholders.
    node_to_part: list[int] = []
    node_i = 0
    for part_i in range(parts_num):
        if is_skip[part_i]:
            continue
        if node_i >= len(node_rot):
            break
        node_to_part.append(part_i)
        node_i += 1
    if node_i != len(node_rot):
        # Not fatal, but suggests our skip list isn't perfect.
        print(
            f"warning: {character} node count mismatch: nodes={len(node_rot)} mapped={node_i} parts_num={parts_num} skips={sum(is_skip)}"
        )

    part_rot = [(0.0, 0.0, 0.0)] * parts_num
    part_scl = [(1.0, 1.0, 1.0)] * parts_num
    part_pos = [(0.0, 0.0, 0.0)] * parts_num
    parent_part = [-1] * parts_num
    part_flags = [0] * parts_num
    for node_idx, part_idx in enumerate(node_to_part):
        part_rot[part_idx] = node_rot[node_idx]
        part_scl[part_idx] = node_scl[node_idx]
        part_pos[part_idx] = node_pos[node_idx]
        part_flags[part_idx] = int(node_flags[node_idx])
        p = node_parent[node_idx]
        parent_part[part_idx] = node_to_part[p] if p >= 0 and p < len(node_to_part) else -1

    return part_rot, part_scl, part_pos, parent_part, part_flags


def _read_model_scale_and_inv_part(character: str) -> tuple[float, int]:
    """Return (model_scaling, inv_scale_part_index) from ftData/co_attrs."""
    prefix = _fighter_prefix(character)
    base = ISO_DIR / f"{prefix}.dat"
    arc = parse_hsd_archive(base.read_bytes())
    ftdata_abs = arc.get_public_offset(_ftdata_symbol(character))
    if ftdata_abs is None:
        raise RuntimeError(f"{base.name} missing ftData public symbol")

    co_attrs_ptr = _u32_be(arc.buf, ftdata_abs + 0x00)
    model_scaling = 1.0
    if co_attrs_ptr != 0:
        model_scaling = _f32_be(arc.buf, arc.data_base + co_attrs_ptr + 0x8C)

    x8_ptr = _u32_be(arc.buf, ftdata_abs + 0x08)
    x8_abs = arc.data_base + x8_ptr
    inv_part = int(arc.buf[x8_abs + 0x10])
    return float(model_scaling), inv_part


def extract_static_x1a70(character: str) -> tuple[float, float]:
    """Return Fighter_Create's unscaled ``x1A70.{y,z}`` rest-pose vector.

    ``Fighter_UnkUpdateVecFromBones_8006876C`` samples the costume JObj tree after parts setup and
    stores ``TransN.world - XRotN.world``.  This is model data, not a Wait-animation value: Marth,
    Falcon, and Sheik have a nonzero rest-pose Z component that their Wait tracks overwrite.

    Sources:
    - refs/melee/src/melee/ft/fighter.c::Fighter_UnkUpdateVecFromBones_8006876C
    - refs/melee/src/melee/ft/ftparts.c::ftParts_SetupParts
    - refs/melee/src/sysdolphin/baselib/jobj.c::HSD_JObjSetupMatrixSub
    """
    part_rot, part_scl, part_pos, parent_part, part_flags = _read_rest_srt_and_parents(character)
    if len(parent_part) <= 2:
        raise RuntimeError(f"{character}: fighter part table does not contain TransN/XRotN")

    identity = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    world: list[tuple[float, ...] | None] = [None] * len(parent_part)
    world_scl: list[tuple[float, float, float] | None] = [None] * len(parent_part)

    def build(part: int) -> None:
        if world[part] is not None:
            return
        parent = parent_part[part]
        if parent >= 0:
            build(parent)
        parent_world = world[parent] if parent >= 0 else identity
        parent_scale = world_scl[parent] if parent >= 0 else None
        if parent_world is None:  # pragma: no cover - guarded by recursive build
            raise RuntimeError(f"{character}: missing parent matrix for fighter part {part}")
        local = _mtx_srt(part_scl[part], part_rot[part], part_pos[part], parent_scale)
        world[part] = _mtx_concat(parent_world, local)

        if (part_flags[part] & 8) != 0:
            world_scl[part] = parent_scale
        elif parent_scale is None:
            world_scl[part] = part_scl[part]
        else:
            world_scl[part] = (
                _f32_mul_fused(part_scl[part][0], parent_scale[0]),
                _f32_mul_fused(part_scl[part][1], parent_scale[1]),
                _f32_mul_fused(part_scl[part][2], parent_scale[2]),
            )

    build(1)  # FtPart_TransN
    build(2)  # FtPart_XRotN
    transn = world[1]
    xrotn = world[2]
    if transn is None or xrotn is None:  # pragma: no cover - guarded by build
        raise RuntimeError(f"{character}: failed to materialize TransN/XRotN rest pose")
    return (_f32(transn[7] - xrotn[7]), _f32(transn[11] - xrotn[11]))


def _read_ftdata_x8_u8(character: str, rel_off: int) -> int:
    """Read a u8 from `ftData.x8` (refs/melee/src/melee/ft/types.h::ftData.x8) for this character."""
    prefix = _fighter_prefix(character)
    base = ISO_DIR / f"{prefix}.dat"
    arc = parse_hsd_archive(base.read_bytes())
    ftdata_abs = arc.get_public_offset(_ftdata_symbol(character))
    if ftdata_abs is None:
        raise RuntimeError(f"{base.name} missing ftData public symbol")
    x8_ptr = _u32_be(arc.buf, ftdata_abs + 0x08)
    x8_abs = arc.data_base + x8_ptr
    off = int(x8_abs) + int(rel_off)
    if off < 0 or off >= len(arc.buf):
        raise RuntimeError(f"{base.name} ftData.x8 u8 out of bounds: off={off} rel={rel_off}")
    return int(arc.buf[off])


def _msid_anim_entry_full(
    character: str, msid: int
) -> tuple[str, int, int, int, int, int] | None:
    prefix = _fighter_prefix(character)
    base = ISO_DIR / f"{prefix}.dat"
    arc = parse_hsd_archive(base.read_bytes())
    ftdata_abs = arc.get_public_offset(_ftdata_symbol(character))
    if ftdata_abs is None:
        raise RuntimeError(f"{base.name} missing ftData public symbol")

    msid_tbl_ptr = _u32_be(arc.buf, ftdata_abs + 0x0C)
    if msid_tbl_ptr == 0:
        raise RuntimeError(f"{base.name} ftData.xC null (msid table)")
    tbl_abs = arc.data_base + msid_tbl_ptr
    off = tbl_abs + msid * 0x18
    if off + 0x18 > len(arc.buf):
        return None
    name_ptr = _u32_be(arc.buf, off + 0x00)
    if name_ptr == 0:
        return None
    name = _read_cstr(arc.buf, arc.data_base + name_ptr)
    # `ftData_80085A14` patches x14 by loading `<prefix>AJ.dat` and doing:
    #   x14 = a_head + x4
    # where x4 is an offset into the AJ file and x8 is the archive size.
    x4 = _u32_be(arc.buf, off + 0x04)
    x8 = _u32_be(arc.buf, off + 0x08)
    flags_u8 = int(arc.buf[off + 0x10])
    # Fighter_ChangeMotionState copies the full big-endian x10_animCurrFlags word to fp->x594.
    # Its final byte's low six bits are fp->x597_bits, the FighterKind namespace used to map this
    # FigaTree's nodes through ftPartsRemap. CaptureCaptain is the important cross-source case:
    # Falcon supplies the animation archive, but x597 is FTKIND_NONE's canonical common skeleton.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/ftanim.c::{ftAnim_8006FE08,ftAnim_8006FCE4}
    flags_word = _u32_be(arc.buf, off + 0x10)
    source_part_bits = (flags_word >> 9) & 0x1FFF
    source_ftkind = flags_word & 0x3F
    return name, int(x4), int(x8), flags_u8, source_part_bits, source_ftkind


def _msid_anim_entry(character: str, msid: int) -> tuple[str, int, int, int] | None:
    """Return the legacy public animation-entry tuple used by sibling extractors."""
    entry = _msid_anim_entry_full(character, msid)
    return None if entry is None else entry[:4]


def _collect_needed_parts_from_moves(character: str, moves_path: Path) -> tuple[list[int], list[int]]:
    import json

    moves = json.loads(moves_path.read_text())
    part_to_joint, _skip_parts, _ = _load_parts_table(character)
    parts_num = len(part_to_joint)

    needed: set[int] = set()
    max_frame_by_move: dict[int, int] = {}
    extra_msids = set(_extra_anim_msids())
    for mv_name, entry in moves.get("moves", {}).items():
        mk = _move_key_idx(mv_name)
        msid = int(entry.get("submotion_id", -1))
        include_parts = (mk is not None) or (msid in extra_msids)
        max_frame = 0
        for ev in entry.get("events", []):
            max_frame = max(max_frame, int(ev.get("frame", 0)))
            if not include_parts:
                continue
            if ev.get("kind") != "create_hitbox":
                continue
            hb = ev.get("data", {}).get("hitbox", {})
            bone = int(hb.get("bone", 0))
            use_common = bool(hb.get("use_common_bone_ids", False))
            if use_common and 0 <= bone < len(part_to_joint):
                part = int(part_to_joint[bone])
            else:
                part = bone
            if 0 <= part < parts_num:
                needed.add(part)
        if mk is not None:
            max_frame_by_move[mk] = max_frame

    # Special scripts can also create hitboxes (e.g. shines, lasers, up-B hits).
    for _msid_str, entry in moves.get("specials_by_msid", {}).items():
        for ev in entry.get("events", []):
            if ev.get("kind") != "create_hitbox":
                continue
            hb = ev.get("data", {}).get("hitbox", {})
            bone = int(hb.get("bone", 0))
            use_common = bool(hb.get("use_common_bone_ids", False))
            if use_common and 0 <= bone < len(part_to_joint):
                part = int(part_to_joint[bone])
            else:
                part = bone
            if 0 <= part < parts_num:
                needed.add(part)

    # Also include hip/bust/head anchors via FtPart ids (4, 16, 34) -> part indices.
    for ftpart in (4, 16, 34):
        if 0 <= ftpart < len(part_to_joint):
            part = int(part_to_joint[ftpart])
            if 0 <= part < parts_num:
                needed.add(part)

    # Include common "hold/attach" joints even if they are not referenced by hitboxes.
    # Decomp: many articles/props (including Fox/Falco blaster laser) attach relative to FtPart_RThumbNb.
    for ftpart in (49,):  # FtPart_RThumbNb
        if 0 <= ftpart < len(part_to_joint):
            part = int(part_to_joint[ftpart])
            if 0 <= part < parts_num:
                needed.add(part)

    # Include character special intercept descriptor bones so the runtime can pose them
    # (e.g. Marth Counter AbsorbDesc bone; consumed by combat.c
    # marth_counter_desc_overlaps_hitbox via anim_pose_get_matrix).
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8 (fp->parts[shield->bone].joint)
    char_path = DATA_DIR / "characters" / f"{character}.json"
    if char_path.exists():
        try:
            ch_json = json.loads(char_path.read_text())
            desc_part = int(ch_json.get("speciallw_counter_desc_bone", -1))
            if 0 <= desc_part < parts_num:
                needed.add(desc_part)
        except Exception as e:
            print(f"warning: failed to read {char_path}: {e}")

    # Include hurt capsule bones if extracted (needed to animate accurate defender hurt capsules).
    hurt_path = DATA_DIR / "hurtcaps" / f"{character}.json"
    if hurt_path.exists():
        try:
            hurt = json.loads(hurt_path.read_text())
            for cap in hurt.get("capsules", []):
                part = int(cap.get("bone_idx", -1))
                if 0 <= part < parts_num:
                    needed.add(part)
        except Exception as e:
            print(f"warning: failed to read {hurt_path}: {e}")

    return sorted(needed), [max_frame_by_move.get(i, 0) for i in range(13)]


def _closure_with_ancestors(needed: list[int], parent_part: list[int]) -> list[int]:
    out: set[int] = set(needed)
    for p in needed:
        cur = p
        while cur >= 0:
            out.add(cur)
            cur = parent_part[cur]
    return sorted(out)


def _dynamic_first_child_chain(root: int, count: int, parent_part: list[int]) -> list[int]:
    """Return the runtime child chain used by lb_8000FD48 for fighter dynamics.

    `lb_8000FD48` starts at the configured root JObj and walks `jobj->child` once per
    dynamic node. Fighter skeleton data has at most one first-child continuation along
    Fox's dynamic tail chain, so resolve the same chain in Fighter_Part space.
    """
    if root < 0 or count <= 0:
        return []
    children: dict[int, list[int]] = {}
    for part, parent in enumerate(parent_part):
        if parent >= 0:
            children.setdefault(int(parent), []).append(int(part))
    chain: list[int] = []
    cur = int(root)
    for _ in range(int(count)):
        if cur < 0 or cur >= len(parent_part):
            break
        chain.append(cur)
        kids = children.get(cur, [])
        if not kids:
            break
        cur = min(kids)
    return chain


def _write_fighter_dynamics_data(
    character: str,
    out_dir: Path,
    dynamic_sets: list[dict[str, object]],
    parent_part: list[int],
    part_rot: list[tuple[float, float, float]],
    part_pos: list[tuple[float, float, float]],
    part_scl: list[tuple[float, float, float]],
    disabled_msids: list[int] | None = None,
) -> Path:
    """Write extracted ftData.x2C dynamic-chain descriptors for runtime pose ownership.

    Layout `SSDYNN01` v9:
    - set_count:u16, total_node_count:u16
    - per set: root_part:u16, node_count:u16, pos:vec3
    - per node: part:u16, pad:u16, constants[15]:f32, rest_rot/pos/scl:vec3 each
    - disabled_msid_count:u16, reserved:u16, disabled_msids:u16[]
    - collider_count:u16, reserved:u16
    - per collider: part:u16, pad:u16, offset:vec3, radius:f32

    The constants are the raw 0x3C-byte `lb_00F9_UnkDesc1Inner` entries copied by
    `lb_80011710`; runtime maps them onto the `lb_8001044C` dynamic-node fields. The
    disabled msids are the extracted ftData animation-entry x594_b3 owner. The collider records
    are ftData.x2C->x8 (`fp->x1670`) entries consumed by `lb_8001044C`'s segment/sphere avoidance.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"{character}.dyn.bin"
    encoded_sets: list[
        tuple[
            int,
            tuple[float, float, float],
            list[
                tuple[
                    int,
                    tuple[float, ...],
                    tuple[float, float, float],
                    tuple[float, float, float],
                    tuple[float, float, float],
                ]
            ],
        ]
    ] = []
    total_nodes = 0
    for dyn in dynamic_sets:
        root = int(dyn.get("root_part", -1))
        count = int(dyn.get("chain_count", 0))
        chain = _dynamic_first_child_chain(root, count, parent_part)
        entries = list(dyn.get("entries", []))
        nodes: list[tuple[int, tuple[float, ...]]] = []
        for i, part in enumerate(chain):
            if i >= len(entries):
                break
            nodes.append(
                (
                    int(part),
                    tuple(float(x) for x in entries[i]),
                    tuple(float(x) for x in part_rot[part]),
                    tuple(float(x) for x in part_pos[part]),
                    tuple(float(x) for x in part_scl[part]),
                )
            )
        if nodes:
            pos_raw = dyn.get("pos", (0.0, 0.0, 0.0))
            pos = tuple(float(x) for x in pos_raw)  # type: ignore[arg-type]
            encoded_sets.append((root, (pos[0], pos[1], pos[2]), nodes))
            total_nodes += len(nodes)

    with out_path.open("wb") as f:
        f.write(b"SSDYNN01")
        f.write(struct.pack("<I", 9))
        f.write(struct.pack("<H", len(encoded_sets)))
        f.write(struct.pack("<H", total_nodes))
        for root, pos, nodes in encoded_sets:
            f.write(struct.pack("<HH3f", int(root) & 0xFFFF, len(nodes), *pos))
            for part, entry, rest_rot, rest_pos, rest_scl in nodes:
                if len(entry) != 15:
                    raise RuntimeError(f"{character}: dynamic node for part {part} has {len(entry)} constants")
                f.write(
                    struct.pack(
                        "<HH24f",
                        int(part) & 0xFFFF,
                        0,
                        *entry,
                        *rest_rot,
                        *rest_pos,
                        *rest_scl,
                    )
                )
        disabled_owner_msids = sorted({int(msid) & 0xFFFF for msid in (disabled_msids or [])})
        # v9 leaves the four v8 probe-owner slots empty so old artifacts cannot silently retain
        # semantic motion allowlists during the source-flag cutover. The runtime loader removes
        # these reserved slots once the v9 transition is complete.
        for _reserved_index in range(4):
            f.write(struct.pack("<HH", 0, 0))
        f.write(struct.pack("<HH", len(disabled_owner_msids), 0))
        for msid in disabled_owner_msids:
            f.write(struct.pack("<H", msid))
        colliders_raw = []
        for dyn in dynamic_sets:
            for part, offset, radius in dyn.get("colliders", []):  # type: ignore[assignment]
                colliders_raw.append((int(part), tuple(float(x) for x in offset), float(radius)))
            if colliders_raw:
                break
        f.write(struct.pack("<HH", len(colliders_raw), 0))
        for part, offset, radius in colliders_raw:
            f.write(struct.pack("<HH4f", int(part) & 0xFFFF, 0, offset[0], offset[1], offset[2], radius))
    return out_path


def _dynamic_disabled_msids(character: str, msids: list[int]) -> list[int]:
    """Return motions whose source x594_b3 disables ordinary fighter dynamics.

    Fighter_ChangeMotionState copies the ftData animation-entry flags into ``fp->x594`` and
    ftCo_8009E7B4 routes x594_b3 through ``ftCo_8009CB40(..., false, NULL)``.  The raw first byte
    is big-endian bitfield storage, so x594_b3 is mask 0x10.  x594_b4 selects a separate dynamic
    FigaTree owner that is not present in the extracted supported-character motion set; fail if it
    appears instead of silently collapsing the two modes.

    refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009CB40,ftCo_8009E7B4}
    """
    if not msids:
        return []
    out: list[int] = []
    for msid in msids:
        source_character = _figatree_source_character(character, int(msid))
        entry = _msid_anim_entry_full(source_character, int(msid))
        if entry is None:
            continue
        flags_u8 = int(entry[3])
        if (flags_u8 & 0x08) != 0:
            raise RuntimeError(
                f"{character}: submotion {msid} uses unsupported x594_b4 dynamic FigaTree"
            )
        if (flags_u8 & 0x10) != 0:
            out.append(int(msid))
    return sorted(set(out))


def _node_mapping_for_parts(
    parts_num: int,
    inserted_parts: list[int],
    fig: _FigaTree,
    *,
    enabled_inserted_bits: int = 0,
) -> tuple[list[int], list[int], list[int]]:
    # Map FigaTree node indices onto part indices by skipping the same parts
    # that are absent from the JObj traversal. ftParts_8007506C returns one bit per inserted part;
    # the part consumes a FigaTree node only when that bit is enabled in fp->x594_bits.
    # refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FCE4
    # refs/melee/src/melee/ft/ftparts.c::ftParts_8007506C
    is_skip = [False] * parts_num
    for bit_i, part in enumerate(inserted_parts):
        if 0 <= part < parts_num and (enabled_inserted_bits & (1 << bit_i)) == 0:
            is_skip[part] = True

    node_to_part: list[int] = []
    p = 0
    for _node_idx in range(len(fig.nodes)):
        while p < parts_num and is_skip[p]:
            p += 1
        if p >= parts_num:
            break
        node_to_part.append(p)
        p += 1

    part_to_node = [-1] * parts_num
    for ni, pi in enumerate(node_to_part):
        part_to_node[pi] = ni

    track_base_by_node: list[int] = []
    acc = 0
    for nframes in fig.nodes:
        track_base_by_node.append(acc)
        acc += int(nframes)

    return node_to_part, part_to_node, track_base_by_node


def _remap_figatree_nodes_to_target_parts(
    *, target_character: str, source_ftkind: int, source_part_bits: int, fig: _FigaTree
) -> tuple[list[int], list[int]]:
    """Return target raw-part -> source node and source node track offsets.

    Vanilla applies a FigaTree through ``ftPartsRemap(target_kind, fp->x597_bits, source_part)``.
    The source kind comes from the animation entry's full ``x10_animCurrFlags`` word, not
    necessarily from the fighter file that supplied the archive. CaptureCaptain demonstrates the
    distinction: Falcon supplies the FigaTree, while x597 selects FTKIND_NONE's canonical common
    skeleton. Raw part indices are character-specific, so the source raw part must map to a
    semantic ``Fighter_Part`` and then to the target raw part.

    refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FCE4
    refs/melee/src/melee/ft/ftparts.c::ftPartsRemap
    """
    target_part_to_joint, target_skip_parts, _target_joint_to_part = _load_parts_table(
        target_character
    )
    if source_ftkind == _ftkind(target_character):
        _node_to_part, part_to_node, track_base_by_node = _node_mapping_for_parts(
            len(target_part_to_joint),
            target_skip_parts,
            fig,
            enabled_inserted_bits=source_part_bits,
        )
        return part_to_node, track_base_by_node

    source_part_to_joint, source_skip_parts, source_joint_to_part = _load_parts_table_for_ftkind(
        source_ftkind
    )
    _source_node_to_part, source_part_to_node, track_base_by_node = _node_mapping_for_parts(
        len(source_part_to_joint),
        source_skip_parts,
        fig,
        enabled_inserted_bits=source_part_bits,
    )
    target_part_to_node = [-1] * len(target_part_to_joint)
    for source_raw_part, node_i in enumerate(source_part_to_node):
        if node_i < 0 or source_raw_part >= len(source_joint_to_part):
            continue
        semantic_part = int(source_joint_to_part[source_raw_part])
        if semantic_part == 0xFF or semantic_part >= len(target_part_to_joint):
            continue
        target_raw_part = int(target_part_to_joint[semantic_part])
        if target_raw_part == 0xFF or target_raw_part >= len(target_part_to_node):
            continue
        target_part_to_node[target_raw_part] = int(node_i)
    return target_part_to_node, track_base_by_node


def extract_one_character(
    *,
    character: str,
    moves_path: Path,
    out_dir: Path,
    timings: bool = False,
    msids: list[int] | None = None,
    add_msids: list[int] | None = None,
) -> Path:
    import json

    global _TIMINGS
    prev_timings = _TIMINGS
    _TIMINGS = _Timings() if timings else None
    if character not in {"fox", "falco", "sheik", "peach", "marth", "puff", "falcon", "zelda"}:
        _TIMINGS = prev_timings
        raise SystemExit(f"unsupported character {character!r}")

    needed_parts, max_frame_by_move = _collect_needed_parts_from_moves(character, moves_path)
    part_to_joint, skip_parts, joint_to_part = _load_parts_table(character)
    parts_num = len(part_to_joint)
    # Always include TopN and TransN so we can:
    # - reference a stable root joint for ECB/hurtcaps
    # - recover decomp-style root-motion offsets (x68C_transNPos) from TransN translation.
    for part in (0, 1):  # FtPart_TopN=0, FtPart_TransN=1
        if part not in needed_parts:
            needed_parts = [part] + needed_parts

    # Grab/capture victim attachment needs additional common Fighter_Part joints even if they are
    # not referenced by moves/hurtcaps. SSANIM part ids are raw fp->parts indices, so map the
    # semantic Fighter_Part ids through this character's extracted ftPartsTable first:
    # - Victim attachment uses FtPart_XRotN (2) (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB464).
    # - Grab/capture setup constrains the victim XRotN to the grab owner's FtPart_TransN2 (52)
    #   (refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_800DB368).
    # - Some grab/throw flows also consult FtPart_ThrowN (51) (refs/melee/src/melee/ft/forward.h::Fighter_Part).
    for ftpart in (2, 52, 51):  # FtPart_XRotN=2, FtPart_TransN2=52, FtPart_ThrowN=51
        if 0 <= ftpart < len(part_to_joint):
            part = int(part_to_joint[ftpart])
            if part != 0xFF and 0 <= part < parts_num and part not in needed_parts:
                needed_parts.append(part)

    # Include additional parts needed for post-frame fidelity beyond move hitboxes.
    #
    # Decomp: hurt capsule endpoints come from `lb_8000B1CC(hurt->bone, ...)`, where `hurt->bone`
    # is `fp->parts[hurt->capsule.bone_idx].joint`. If we don't emit animation matrices for those
    # parts, runtime collision will fall back to bind pose and can choose the wrong hurt height
    # (DamageHi/N/Lw) on hit.
    #
    # Sources:
    # - Hurtcaps: `data/hurtcaps/<character>.json` (extracted from ftData hurtbox init tables)
    # - ECB joints: `data/characters/<character>.json` `ecb_joints` (used by our ECB probe/debug)
    hc_path = DATA_DIR / "hurtcaps" / f"{character}.json"
    if hc_path.exists():
        try:
            hc = json.loads(hc_path.read_text())
            for cap in hc.get("capsules", []) or []:
                bi = cap.get("bone_idx")
                if isinstance(bi, int) and 0 <= bi < 256 and bi not in needed_parts:
                    needed_parts.append(bi)
        except Exception:
            pass

    ch_path = DATA_DIR / "characters" / f"{character}.json"
    if ch_path.exists():
        try:
            ch = json.loads(ch_path.read_text())
            for bi in ch.get("ecb_joints", []) or []:
                if isinstance(bi, int) and 0 <= bi < 256 and bi not in needed_parts:
                    needed_parts.append(bi)
        except Exception:
            pass
    part_rot, part_scl, part_pos, parent_part, part_flags = _read_rest_srt_and_parents(character)
    model_scaling, inv_scale_part = _read_model_scale_and_inv_part(character)
    inv_model_scale = 1.0 / model_scaling if abs(model_scaling) > 1.0e-6 else 1.0
    dynamic_sets = _read_fighter_dynamics(character)
    moves = json.loads(moves_path.read_text())
    # The current fixed runtime state carries one descriptor chain. Keep every source set that fits
    # that representation (Fox's tail today) and reject multi-set characters here until the state
    # surface can represent all sets without collisions.
    if len(dynamic_sets) != 1 or int(dynamic_sets[0].get("chain_count", 0)) > 16:
        dynamic_sets = []
    for dyn in dynamic_sets:
        for part in _dynamic_first_child_chain(
            int(dyn.get("root_part", -1)), int(dyn.get("chain_count", 0)), parent_part
        ):
            if 0 <= part < 256 and part not in needed_parts:
                needed_parts.append(part)
    # Capture victim alignment anchor (`mv.co.capturedamage.x18`) is set from `ftData.x8->x11`.
    # Decomp: refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800D9CE8
    #
    # Store the raw u8 (index into `fp->parts[]`) as a pose part id so the simulator can query
    # that joint in SSANIM01.
    try:
        grab_anchor_part = _read_ftdata_x8_u8(character, 0x11)
        if 0 <= grab_anchor_part < 256 and grab_anchor_part not in needed_parts:
            needed_parts.append(int(grab_anchor_part))
    except Exception:
        pass

    closure_parts = _closure_with_ancestors(needed_parts, parent_part)

    # Parse moves file for per-move msid (submotion_id).
    msid_by_move = [None] * 13
    for mv_name, entry in moves.get("moves", {}).items():
        mk = _move_key_idx(mv_name)
        if mk is None:
            continue
        msid_by_move[mk] = int(entry.get("submotion_id", -1))

    msid_to_mk: dict[int, int] = {}
    for mk, msid in enumerate(msid_by_move):
        if msid is not None and msid >= 0:
            msid_to_mk[int(msid)] = int(mk)

    if msids is not None:
        wanted_msids = [int(m) for m in msids if int(m) >= 0]
        wanted_msids = sorted(set(wanted_msids))
    else:
        wanted_msids = sorted(
            {msid for msid in msid_to_mk.keys()}
            | set(_extra_anim_msids())
            | set(_special_anim_msids(character))
        )
        if add_msids:
            base = set(int(m) for m in wanted_msids)
            extra = set(int(m) for m in add_msids if int(m) >= 0)
            wanted_msids = sorted(base | extra)

    _write_fighter_dynamics_data(
        character,
        out_dir,
        dynamic_sets,
        parent_part,
        part_rot,
        part_pos,
        part_scl,
        disabled_msids=_dynamic_disabled_msids(character, wanted_msids) if dynamic_sets else [],
    )

    prefix = _fighter_prefix(character)
    aj_path = ISO_DIR / f"{prefix}AJ.dat"
    if not aj_path.exists():
        raise RuntimeError(f"missing animation DAT {aj_path} (extract from ISO first)")
    aj_buf_by_character = {character: aj_path.read_bytes()}
    aj_cache: dict[tuple[str, int], HsdArchive] = {}

    # Build anim data.
    joint_parts = needed_parts[:]  # store only the joints we directly need at runtime
    local_parts = closure_parts[:]  # store locals for needed parts + ancestors (exact root-chain replay)

    # Precompute order: parents before children within closure_parts.
    depth_cache: dict[int, int] = {}

    def depth(p: int) -> int:
        d = depth_cache.get(p)
        if d is not None:
            return d
        pp = parent_part[p]
        if pp < 0:
            depth_cache[p] = 0
            return 0
        dd = 1 + depth(int(pp))
        depth_cache[p] = dd
        return dd

    order = sorted(closure_parts, key=lambda p: (depth(p), p))

    try:
        from melee_sim import _native as msl
    except Exception as exc:
        raise RuntimeError(
            "melee_sim native bindings are required for fighter animation extraction; "
            "install/build melee_sim and rerun extraction."
        ) from exc

    rest_rot_np = np.asarray(part_rot, dtype=np.float32)
    rest_pos_np = np.asarray(part_pos, dtype=np.float32)
    rest_scl_np = np.asarray(part_scl, dtype=np.float32)
    parent_np = np.asarray(parent_part, dtype=np.int16)
    flags_np = np.asarray(part_flags, dtype=np.uint32)
    order_np = np.asarray(order, dtype=np.int32)
    local_parts_np = np.asarray(local_parts, dtype=np.int32)
    joint_parts_np = np.asarray(joint_parts, dtype=np.int32)

    # Header: magic + version + joint_count + anim_count + joint_parts bytes.
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f"{character}.bin"
    locals_path = out_dir / f"{character}.locals.bin"
    tracks_path = out_dir / f"{character}.tracks.bin"
    with out_path.open("wb") as f, locals_path.open("wb") as f_loc, tracks_path.open("wb") as f_tr:
        f.write(b"SSANIM01")
        # v5 preserves the v4 byte layout and requires source-donor cross-bakes such as
        # CaptureCaptain in addition to stopped non-loop AObj terminal values and the TransN tail.
        f.write(struct.pack("<I", SSANIM_VERSION))
        f.write(struct.pack("<H", len(joint_parts)))
        f.write(struct.pack("<H", len(wanted_msids)))
        f.write(bytes([p & 0xFF for p in joint_parts]))
        # Local SRT file: locals for closure parts, per-frame (no TransN baked in).
        f_loc.write(b"SSANIML1")
        f_loc.write(struct.pack("<I", 1))
        f_loc.write(struct.pack("<H", len(local_parts)))
        f_loc.write(struct.pack("<H", len(wanted_msids)))
        f_loc.write(bytes([p & 0xFF for p in local_parts]))
        for p in local_parts:
            f_loc.write(struct.pack("<h", int(parent_part[p])))
        for p in local_parts:
            f_loc.write(struct.pack("<I", int(part_flags[p]) & 0xFFFFFFFF))

        # Track file: decomp-first HSD FObj track descriptors + raw AD streams for float-frame
        # evaluation at runtime (needed for fractional `cur_anim_frame` during blends like Dash→Run).
        #
        # Layout:
        # - magic: 8 bytes "SSANIMT1"
        # - version: u32 little-endian (3)
        # - local_count: u16
        # - anim_count: u16
        # - local_parts: [u8; local_count] (common FtPart ids)
        # - local_parent: [i16; local_count] (parent FtPart id, -1 if none)
        # - local_flags: [u32; local_count] (HSD_JObj flags per part)
        # - For each anim (anim_count):
        #   - msid: u16
        #   - end_frame: f32 (FigaTree.frames)
        #   - aobj_loop: u8 (1 if ftData_80085FD4_ret.x10_b1 (loop) is set for this anim id, else 0)
        #   - uses_root_motion: u8 (1 if ftData_80085FD4_ret.x10_b0 is set for this anim id, else 0)
        #     Decomp:
        #     - refs/melee/src/melee/ft/types.h::ftData_80085FD4_ret (+0x10 bit0/bit1)
        #     - refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState (loads fp->x594_s32)
        #     - refs/melee/src/melee/ft/ft_081B.c::{ft_80085030,ft_800850E0}
        #     - refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBE8 (sets AOBJ_LOOP when fp->x594_b1_loop)
        #   - For each local part (local_count):
        #     - part: u8 (must match local_parts[i])
        #     - n_tracks: u8
        #     - For each track:
        #       - obj_type: u8 (HSD_A_J_*, see sysdolphin/baselib/jobj.h)
        #       - frac_value: u8 (HSD_A_FRAC_*, see sysdolphin/baselib/fobj.h)
        #       - frac_slope: u8
        #       - pad: u8 (0)
        #       - startframe: u16
        #       - length: u16 (bytes)
        #       - ad_bytes: [u8; length]
        f_tr.write(b"SSANIMT1")
        f_tr.write(struct.pack("<I", 3))
        f_tr.write(struct.pack("<H", len(local_parts)))
        f_tr.write(struct.pack("<H", len(wanted_msids)))
        f_tr.write(bytes([p & 0xFF for p in local_parts]))
        for p in local_parts:
            f_tr.write(struct.pack("<h", int(parent_part[p])))
        for p in local_parts:
            f_tr.write(struct.pack("<I", int(part_flags[p]) & 0xFFFFFFFF))

        for msid in wanted_msids:
            t_msid0 = time.perf_counter() if _TIMINGS is not None else 0.0
            source_character = _figatree_source_character(character, int(msid))
            entry = _msid_anim_entry_full(source_character, int(msid))
            if entry is None:
                t_w0 = time.perf_counter() if _TIMINGS is not None else 0.0
                f.write(struct.pack("<H", int(msid) & 0xFFFF))
                f.write(struct.pack("<H", 0))
                f_loc.write(struct.pack("<H", int(msid) & 0xFFFF))
                f_loc.write(struct.pack("<H", 0))
                f_tr.write(struct.pack("<H", int(msid) & 0xFFFF))
                f_tr.write(struct.pack("<f", 0.0))
                f_tr.write(struct.pack("<B", 0))
                f_tr.write(struct.pack("<B", 0))
                for part in local_parts:
                    f_tr.write(struct.pack("<BB", int(part) & 0xFF, 0))
                if _TIMINGS is not None:
                    _TIMINGS.file_write_s += time.perf_counter() - t_w0
                    _TIMINGS.msid_total_s += time.perf_counter() - t_msid0
                    _TIMINGS.msid_count += 1
                continue

            source_aj_buf = aj_buf_by_character.get(source_character)
            if source_aj_buf is None:
                source_aj_path = ISO_DIR / f"{_fighter_prefix(source_character)}AJ.dat"
                if not source_aj_path.exists():
                    raise RuntimeError(
                        f"missing donor animation DAT {source_aj_path} (extract from ISO first)"
                    )
                source_aj_buf = source_aj_path.read_bytes()
                aj_buf_by_character[source_character] = source_aj_buf

            sym, base_off, _size, msid_flags_u8, source_part_bits, source_ftkind = entry
            if base_off < 0 or base_off + 0x20 > len(source_aj_buf):
                t_w0 = time.perf_counter() if _TIMINGS is not None else 0.0
                f.write(struct.pack("<H", int(msid) & 0xFFFF))
                f.write(struct.pack("<H", 0))
                f_loc.write(struct.pack("<H", int(msid) & 0xFFFF))
                f_loc.write(struct.pack("<H", 0))
                f_tr.write(struct.pack("<H", int(msid) & 0xFFFF))
                f_tr.write(struct.pack("<f", 0.0))
                f_tr.write(struct.pack("<B", 0))
                f_tr.write(struct.pack("<B", 0))
                for part in local_parts:
                    f_tr.write(struct.pack("<BB", int(part) & 0xFF, 0))
                if _TIMINGS is not None:
                    _TIMINGS.file_write_s += time.perf_counter() - t_w0
                    _TIMINGS.msid_total_s += time.perf_counter() - t_msid0
                    _TIMINGS.msid_count += 1
                continue

            cache_key = (source_character, base_off)
            arc = aj_cache.get(cache_key)
            if arc is None:
                arc = parse_hsd_archive(source_aj_buf, base=base_off)
                aj_cache[cache_key] = arc

            fig_off = arc.get_public_offset(sym)
            if fig_off is None:
                t_w0 = time.perf_counter() if _TIMINGS is not None else 0.0
                f.write(struct.pack("<H", int(msid) & 0xFFFF))
                f.write(struct.pack("<H", 0))
                f_loc.write(struct.pack("<H", int(msid) & 0xFFFF))
                f_loc.write(struct.pack("<H", 0))
                f_tr.write(struct.pack("<H", int(msid) & 0xFFFF))
                f_tr.write(struct.pack("<f", 0.0))
                f_tr.write(struct.pack("<B", 0))
                f_tr.write(struct.pack("<B", 0))
                for part in local_parts:
                    f_tr.write(struct.pack("<BB", int(part) & 0xFF, 0))
                if _TIMINGS is not None:
                    _TIMINGS.file_write_s += time.perf_counter() - t_w0
                    _TIMINGS.msid_total_s += time.perf_counter() - t_msid0
                    _TIMINGS.msid_count += 1
                continue

            fig = _read_figatree(arc, fig_off)
            part_to_node, track_base_by_node = _remap_figatree_nodes_to_target_parts(
                target_character=character,
                source_ftkind=source_ftkind,
                source_part_bits=source_part_bits,
                fig=fig,
            )

            # Emit raw fobj track data for float-frame evaluation.
            tracks_by_part: dict[int, list[_FigaTrack]] = {}
            for part in local_parts:
                ni = part_to_node[part]
                if ni < 0 or ni >= len(fig.nodes):
                    continue
                nframes = int(fig.nodes[ni])
                base = track_base_by_node[ni]
                tracks = fig.tracks[base : base + nframes]
                # Keep only JObj SRT / branch-related track types (ignore material/shape for now).
                part_tracks = [t for t in tracks if 1 <= t.obj_type <= 12]
                if part_tracks:
                    tracks_by_part[int(part)] = part_tracks

            t_w_tr0 = time.perf_counter() if _TIMINGS is not None else 0.0
            f_tr.write(struct.pack("<H", int(msid) & 0xFFFF))
            f_tr.write(struct.pack("<f", float(fig.frames)))
            # Fighter animation loop flag: fp->x594_b1_loop is sourced from ftData_80085FD4_ret.x10_b1
            # for the anim id (which matches the submotion/msid domain for common states).
            # refs/melee/src/melee/ft/types.h::ftData_80085FD4_ret and refs/melee/src/melee/ft/types.h::Fighter (x594_b1_loop)
            # refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBE8 (applies AOBJ_LOOP)
            # NOTE: The decomp bitfield `u8 x10_b1 : 1` corresponds to 0x40 in the raw byte on
            # big-endian (PPC) builds.
            f_tr.write(struct.pack("<B", 1 if (int(msid_flags_u8) & 0x40) != 0 else 0))
            # The adjacent bitfield `u8 x10_b0 : 1` corresponds to 0x80 in the raw big-endian byte
            # and owns `fp->x594_b0` TransN root motion in ft_80085030/ft_800850E0.
            f_tr.write(struct.pack("<B", 1 if (int(msid_flags_u8) & 0x80) != 0 else 0))
            for part in local_parts:
                part_tracks = tracks_by_part.get(int(part), [])
                f_tr.write(struct.pack("<BB", int(part) & 0xFF, len(part_tracks) & 0xFF))
                for t in part_tracks:
                    ad_abs = int(t.ad_abs)
                    length = int(t.length)
                    ad_bytes = arc.buf[ad_abs : ad_abs + length]
                    f_tr.write(
                        struct.pack(
                            "<BBBBHH",
                            int(t.obj_type) & 0xFF,
                            int(t.frac_value) & 0xFF,
                            int(t.frac_slope) & 0xFF,
                            0,
                            int(t.startframe) & 0xFFFF,
                            length & 0xFFFF,
                        )
                    )
                    f_tr.write(ad_bytes)
            if _TIMINGS is not None:
                _TIMINGS.file_write_s += time.perf_counter() - t_w_tr0

            # Build fobj lists for active parts (closure), grouped by part.
            # Simulate frames.
            if msid in msid_to_mk:
                mk = msid_to_mk[int(msid)]
                frame_count = int(max_frame_by_move[mk]) + 8
                frame_count = max(1, min(frame_count, 90))
            else:
                frame_count = max(1, min(int(fig.frames) + 1, 120))

            t_w0 = time.perf_counter() if _TIMINGS is not None else 0.0
            f.write(struct.pack("<H", int(msid) & 0xFFFF))
            f.write(struct.pack("<H", frame_count))
            f_loc.write(struct.pack("<H", int(msid) & 0xFFFF))
            f_loc.write(struct.pack("<H", frame_count))
            if _TIMINGS is not None:
                _TIMINGS.file_write_s += time.perf_counter() - t_w0

            update_parts_list: list[int] = []
            fobj_starts_list: list[int] = [0]
            fobj_desc_list: list[tuple[int, int, int]] = []
            for part in closure_parts:
                ni = part_to_node[part]
                if ni < 0 or ni >= len(fig.nodes):
                    continue
                nframes = int(fig.nodes[ni])
                base = track_base_by_node[ni]
                tracks = fig.tracks[base : base + nframes]
                start_len = len(fobj_desc_list)
                for t in tracks:
                    if t.obj_type < 1 or t.obj_type > 10:
                        continue
                    d0 = (int(t.obj_type) & 0xFF) | ((int(t.frac_value) & 0xFF) << 8) | ((int(t.frac_slope) & 0xFF) << 16)
                    d1 = (int(t.startframe) & 0xFFFF) | ((int(t.length) & 0xFFFF) << 16)
                    d2 = int(t.ad_abs) & 0xFFFF_FFFF
                    fobj_desc_list.append((d0, d1, d2))
                if len(fobj_desc_list) != start_len:
                    update_parts_list.append(int(part))
                    fobj_starts_list.append(len(fobj_desc_list))

            update_parts_np = np.asarray(update_parts_list, dtype=np.int32)
            fobj_starts_np = np.asarray(fobj_starts_list, dtype=np.int32)
            if fobj_desc_list:
                fobj_desc_np = np.asarray(fobj_desc_list, dtype=np.uint32)
                fobj_desc_np = fobj_desc_np.reshape((-1, 3))
            else:
                fobj_desc_np = np.zeros((0, 3), dtype=np.uint32)

            t_native0 = time.perf_counter() if _TIMINGS is not None else 0.0
            mats_bytes, locals_bytes, transn_bytes = msl.anim_bake_ssanim01(
                rest_rot_np,
                rest_pos_np,
                rest_scl_np,
                parent_np,
                flags_np,
                order_np,
                local_parts_np,
                joint_parts_np,
                update_parts_np,
                fobj_starts_np,
                fobj_desc_np,
                arc.buf,
                int(frame_count),
                int(inv_scale_part),
                float(inv_model_scale),
                float(fig.frames),
                1 if (int(msid_flags_u8) & 0x40) != 0 else 0,
            )
            if _TIMINGS is not None:
                _TIMINGS.native_bake_s += time.perf_counter() - t_native0
            t_w0 = time.perf_counter() if _TIMINGS is not None else 0.0
            f_loc.write(locals_bytes)
            f.write(mats_bytes)
            f.write(transn_bytes)
            if _TIMINGS is not None:
                _TIMINGS.file_write_s += time.perf_counter() - t_w0

            if _TIMINGS is not None:
                _TIMINGS.msid_total_s += time.perf_counter() - t_msid0
                _TIMINGS.msid_count += 1

    if _TIMINGS is not None:
        print(_TIMINGS.report(character=character))
    _TIMINGS = prev_timings
    validate_ssanimt1_file(tracks_path)
    return out_path


def main() -> None:
    global DATA_DIR, ISO_DIR
    ap = argparse.ArgumentParser(description="Extract per-move fighter bone matrices from FigaTree animations (decomp-first).")
    ap.add_argument("--character", type=str, required=True, help="one of: fox,falco,sheik,peach,marth,puff,falcon,zelda")
    ap.add_argument("--moves", type=Path, default=None, help="path to data/moves/<character>.json (default inferred)")
    ap.add_argument(
        "--data-dir",
        type=Path,
        default=DATA_DIR,
        help="directory containing generated character metadata",
    )
    ap.add_argument(
        "--iso-dir",
        type=Path,
        default=ISO_DIR,
        help="directory containing extracted Pl*.dat files",
    )
    ap.add_argument("--out-dir", type=Path, default=DATA_DIR / "anims", help="output directory")
    ap.add_argument(
        "--blend-only",
        action="store_true",
        help="only write <character>.blend.bin (no matrix/local extraction)",
    )
    ap.add_argument("--timings", action="store_true", help="print wall-clock breakdown of extractor hot paths")
    ap.add_argument("--msid", type=int, action="append", default=None, help="only extract these submotion ids (repeatable)")
    ap.add_argument(
        "--add-msid",
        type=int,
        action="append",
        default=None,
        help="add these submotion ids to the default extracted set (repeatable; ignored if --msid is set)",
    )
    args = ap.parse_args()
    DATA_DIR = args.data_dir
    ISO_DIR = args.iso_dir

    moves = args.moves
    if moves is None:
        moves = DATA_DIR / "moves" / f"{args.character}.json"
    if not moves.exists():
        raise SystemExit(f"moves file not found: {moves}")

    if args.blend_only:
        out = _write_anim_blend_data(args.character, args.out_dir)
        print(f"wrote {out}")
        return

    try:
        out = extract_one_character(
            character=args.character,
            moves_path=moves,
            out_dir=args.out_dir,
            timings=bool(args.timings),
            msids=args.msid,
            add_msids=args.add_msid,
        )
    except RuntimeError as exc:
        raise SystemExit(str(exc)) from None
    _write_anim_blend_data(args.character, args.out_dir)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
