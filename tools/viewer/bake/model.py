"""HSD model decode: JObj skeleton, DObj/PObj meshes, display lists, skinning.

Structure offsets follow src/sysdolphin/baselib/{jobj.h,dobj.h,pobj.h} and
the GX enums in src/extern/dolphin/include/dolphin/gx/GXEnum.h.
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field

import numpy as np

from .dat import Dat

JOBJ_CLASSICAL_SCALE = 1 << 3
JOBJ_HIDDEN = 1 << 4
JOBJ_INSTANCE = 1 << 12
JOBJ_USE_QUATERNION = 1 << 17

POBJ_TYPE_MASK = 0x3000
POBJ_SKIN = 0 << 12
POBJ_SHAPEANIM = 1 << 12
POBJ_ENVELOPE = 2 << 12

GX_VA_PNMTXIDX = 0
GX_VA_POS = 9
GX_VA_NULL = 0xFF
GX_NONE, GX_DIRECT, GX_INDEX8, GX_INDEX16 = 0, 1, 2, 3
GX_U8, GX_S8, GX_U16, GX_S16, GX_F32 = 0, 1, 2, 3, 4

GX_QUADS = 0x80
GX_TRIANGLES = 0x90
GX_TRIANGLESTRIP = 0x98
GX_TRIANGLEFAN = 0xA0
GX_LINES = 0xA8
GX_LINESTRIP = 0xB0
GX_POINTS = 0xB8


@dataclass
class VtxAttr:
    attr: int
    attr_type: int
    comp_cnt: int
    comp_type: int
    frac: int
    stride: int
    data: int  # offset of the vertex array


@dataclass
class PObj:
    offset: int
    flags: int
    attrs: list[VtxAttr]
    display: int
    display_size: int  # bytes
    joint_ref: int  # POBJ_SKIN: referenced joint offset (0 = own DObj's joint)
    envelopes: list[list[tuple[int, float]]]  # POBJ_ENVELOPE: per index, (joint offset, weight)
    # Decoded triangles: (n, 3) vertex-record indices into `verts`; verts are
    # (m, 3) model-space positions and `mtx_index` (m,) envelope indices.
    verts: np.ndarray = field(default_factory=lambda: np.zeros((0, 3), np.float32))
    mtx_index: np.ndarray = field(default_factory=lambda: np.zeros(0, np.int32))
    tris: np.ndarray = field(default_factory=lambda: np.zeros((0, 3), np.int32))


@dataclass
class DObj:
    offset: int
    index: int  # fighter dobj_list index (preorder over joints, chain order)
    joint: int  # owning joint index
    pobjs: list[PObj]


@dataclass
class Joint:
    offset: int
    index: int
    parent: int
    flags: int
    rotation: tuple[float, float, float]
    scale: tuple[float, float, float]
    translate: tuple[float, float, float]
    inv_bind: np.ndarray | None  # 4x4 inverse bind matrix or None
    dobjs: list[DObj]


@dataclass
class Model:
    joints: list[Joint]
    dobjs: list[DObj]  # by dobj_list index

    def joint_by_offset(self) -> dict[int, int]:
        return {j.offset: j.index for j in self.joints}


def _read_attrs(dat: Dat, off: int) -> list[VtxAttr]:
    attrs = []
    while True:
        attr = dat.u32(off)
        if attr == GX_VA_NULL:
            break
        attrs.append(VtxAttr(
            attr=attr,
            attr_type=dat.u32(off + 4),
            comp_cnt=dat.u32(off + 8),
            comp_type=dat.u32(off + 12),
            frac=dat.u8(off + 16),
            stride=dat.u16(off + 18),
            data=dat.ptr(off + 20),
        ))
        off += 24
    return attrs


def _comp_size(comp_type: int) -> int:
    return {GX_U8: 1, GX_S8: 1, GX_U16: 2, GX_S16: 2, GX_F32: 4}[comp_type]


def _read_component(dat: Dat, off: int, comp_type: int) -> float:
    if comp_type == GX_F32:
        return dat.f32(off)
    if comp_type == GX_U8:
        return float(dat.u8(off))
    if comp_type == GX_S8:
        return float(struct.unpack_from('>b', dat.data, off)[0])
    if comp_type == GX_U16:
        return float(dat.u16(off))
    if comp_type == GX_S16:
        return float(dat.s16(off))
    raise ValueError(f'unsupported component type {comp_type}')


def _direct_size(a: VtxAttr) -> int:
    """Byte width of a GX_DIRECT attribute inside the display list."""
    if a.attr <= 8:  # PNMTXIDX / TEXnMTXIDX
        return 1
    if a.attr in (11, 12):  # CLR0/CLR1 by GXCompType colour format
        return {0: 2, 1: 3, 2: 4, 3: 2, 4: 3, 5: 4}[a.comp_type]
    if a.attr == GX_VA_POS:
        count = 3 if a.comp_cnt == 1 else 2
    elif a.attr == 10:  # NRM: XYZ or NBT
        count = 9 if a.comp_cnt == 1 else 3
    else:  # TEXn: S or ST
        count = 2 if a.comp_cnt == 1 else 1
    return count * _comp_size(a.comp_type)


def _decode_pobj(dat: Dat, pobj: PObj) -> None:
    pos_attr = next((a for a in pobj.attrs if a.attr == GX_VA_POS), None)
    if pos_attr is None:
        return
    mtx_attr = next((a for a in pobj.attrs if a.attr == GX_VA_PNMTXIDX), None)
    ncomp = 3 if pos_attr.comp_cnt == 1 else 2
    csize = _comp_size(pos_attr.comp_type)
    scale = 1.0 / (1 << pos_attr.frac) if pos_attr.comp_type != GX_F32 else 1.0

    # Cache decoded position records by (index) to keep vertex count small.
    verts: list[tuple[float, float, float]] = []
    mtx_of: list[int] = []
    key_to_index: dict[tuple[int, int], int] = {}

    def vertex(pos_index: int, mtx_index: int) -> int:
        key = (pos_index, mtx_index)
        found = key_to_index.get(key)
        if found is not None:
            return found
        base = pos_attr.data + pos_index * pos_attr.stride
        comps = [_read_component(dat, base + c * csize, pos_attr.comp_type) * scale
                 for c in range(ncomp)]
        if ncomp == 2:
            comps.append(0.0)
        key_to_index[key] = len(verts)
        verts.append((comps[0], comps[1], comps[2]))
        mtx_of.append(mtx_index)
        return key_to_index[key]

    tris: list[tuple[int, int, int]] = []
    off = pobj.display
    end = pobj.display + pobj.display_size
    while off < end:
        opcode = dat.u8(off)
        if opcode == 0:
            break
        prim = opcode & 0xF8
        count = dat.u16(off + 1)
        off += 3
        indices: list[int] = []
        for _ in range(count):
            pos_index = -1
            mtx_index = 0
            for a in pobj.attrs:
                if a.attr_type == GX_DIRECT:
                    width = _direct_size(a)
                    if a.attr == GX_VA_PNMTXIDX:
                        mtx_index = dat.u8(off) // 3
                    off += width
                    continue
                if a.attr_type == GX_INDEX8:
                    value = dat.u8(off)
                    off += 1
                elif a.attr_type == GX_INDEX16:
                    value = dat.u16(off)
                    off += 2
                else:
                    continue
                if a.attr == GX_VA_POS:
                    pos_index = value
            if pos_index < 0:
                raise ValueError('display list vertex without a position')
            indices.append(vertex(pos_index, mtx_index))
        if prim == GX_TRIANGLES:
            for i in range(0, len(indices) - 2, 3):
                tris.append((indices[i], indices[i + 1], indices[i + 2]))
        elif prim == GX_TRIANGLESTRIP:
            for i in range(len(indices) - 2):
                if i % 2 == 0:
                    tris.append((indices[i], indices[i + 1], indices[i + 2]))
                else:
                    tris.append((indices[i + 1], indices[i], indices[i + 2]))
        elif prim == GX_TRIANGLEFAN:
            for i in range(1, len(indices) - 1):
                tris.append((indices[0], indices[i], indices[i + 1]))
        elif prim == GX_QUADS:
            for i in range(0, len(indices) - 3, 4):
                tris.append((indices[i], indices[i + 1], indices[i + 2]))
                tris.append((indices[i], indices[i + 2], indices[i + 3]))
        # lines/points contribute nothing to a filled silhouette
    pobj.verts = np.array(verts, dtype=np.float32).reshape(-1, 3)
    pobj.mtx_index = np.array(mtx_of, dtype=np.int32)
    pobj.tris = np.array(tris, dtype=np.int32).reshape(-1, 3)


def _read_pobj_chain(dat: Dat, off: int) -> list[PObj]:
    out = []
    while off:
        flags = dat.u16(off + 12)
        n_display = dat.u16(off + 14)
        pobj = PObj(
            offset=off,
            flags=flags,
            attrs=_read_attrs(dat, dat.ptr(off + 8)),
            display=dat.ptr(off + 16),
            display_size=n_display * 32,
            joint_ref=0,
            envelopes=[],
        )
        u = dat.ptr(off + 20)
        ptype = flags & POBJ_TYPE_MASK
        if ptype == POBJ_SKIN:
            pobj.joint_ref = u
        elif ptype == POBJ_ENVELOPE and u:
            # NULL-terminated array of pointers to NULL-terminated
            # (joint, weight) arrays.
            i = 0
            while True:
                entry = dat.ptr(u + i * 4)
                if not entry:
                    break
                weights = []
                j = 0
                while True:
                    joint = dat.ptr(entry + j * 8)
                    if not joint:
                        break
                    weights.append((joint, dat.f32(entry + j * 8 + 4)))
                    j += 1
                pobj.envelopes.append(weights)
                i += 1
        _decode_pobj(dat, pobj)
        out.append(pobj)
        off = dat.ptr(off + 4)
    return out


def load_model(dat: Dat, root_off: int) -> Model:
    joints: list[Joint] = []
    dobjs: list[DObj] = []

    def read_joint(off: int, parent: int) -> None:
        idx = len(joints)
        flags = dat.u32(off + 4)
        inv = None
        mtx_ptr = dat.ptr(off + 56)
        if mtx_ptr:
            rows = [struct.unpack_from('>ffff', dat.data, mtx_ptr + r * 16) for r in range(3)]
            inv = np.array(rows + [(0.0, 0.0, 0.0, 1.0)], dtype=np.float64)
        joint = Joint(
            offset=off, index=idx, parent=parent, flags=flags,
            rotation=dat.vec3(off + 20), scale=dat.vec3(off + 32),
            translate=dat.vec3(off + 44), inv_bind=inv, dobjs=[],
        )
        joints.append(joint)
        dobj_off = dat.ptr(off + 16) if not (flags & (1 << 14)) else 0
        while dobj_off:
            dobj = DObj(offset=dobj_off, index=len(dobjs), joint=idx,
                        pobjs=_read_pobj_chain(dat, dat.ptr(dobj_off + 12)))
            dobjs.append(dobj)
            joint.dobjs.append(dobj)
            dobj_off = dat.ptr(dobj_off + 4)
        child = dat.ptr(off + 8)
        if child and not (flags & JOBJ_INSTANCE):
            read_joint(child, idx)
        nxt = dat.ptr(off + 12)
        if nxt:
            read_joint(nxt, parent)

    read_joint(root_off, -1)
    return Model(joints, dobjs)


# ---------------------------------------------------------------------------
# Matrices (port of HSD_JObjMakeMatrix / HSD_MtxSRT with parent-scale
# compensation, src/sysdolphin/baselib/{jobj.c,mtx.c}).

def mtx_srt(scale, rot, trans, parent_scl):
    sx, sy, sz = scale
    sinX, cosX = math.sin(rot[0]), math.cos(rot[0])
    sinY, cosY = math.sin(rot[1]), math.cos(rot[1])
    sinZ, cosZ = math.sin(rot[2]), math.cos(rot[2])
    x2 = x1 = x0 = sx
    y2 = y1 = y0 = sy
    z2 = z1 = z0 = sz
    if parent_scl is not None:
        px, py, pz = parent_scl
        y2 *= py / px
        z2 *= pz / px
        x1 *= px / py
        z1 *= pz / py
        x0 *= px / pz
        y0 *= py / pz
    m = np.identity(4)
    m[0][0] = cosZ * (x2 * cosY)
    m[1][0] = sinZ * (x1 * cosY)
    m[2][0] = -x0 * sinY
    m[0][1] = y2 * (cosZ * sinX * sinY - cosX * sinZ)
    m[1][1] = y1 * (sinZ * sinX * sinY + cosX * cosZ)
    m[2][1] = cosY * (y0 * sinX)
    m[0][2] = z2 * (cosZ * cosX * sinY + sinX * sinZ)
    m[1][2] = z1 * (sinZ * cosX * sinY - sinX * cosZ)
    m[2][2] = cosY * (z0 * cosX)
    m[0][3], m[1][3], m[2][3] = trans
    return m


def world_matrices(model: Model, rotation, scale, translate) -> list[np.ndarray]:
    """World matrices for every joint given per-joint SRT (lists of tuples)."""
    world: list[np.ndarray] = []
    scl: list[tuple | None] = []
    for j in model.joints:
        parent = model.joints[j.parent] if j.parent >= 0 else None
        p_scl = scl[j.parent] if parent is not None else None
        if j.flags & JOBJ_CLASSICAL_SCALE:
            own = p_scl
        else:
            s = scale[j.index]
            own = (s[0] * p_scl[0], s[1] * p_scl[1], s[2] * p_scl[2]) if p_scl is not None else tuple(s)
        scl.append(own)
        local = mtx_srt(scale[j.index], rotation[j.index], translate[j.index], p_scl)
        world.append(world[j.parent] @ local if parent is not None else local)
    return world


def skinned_triangles(model: Model, world: list[np.ndarray], visible_dobjs) -> np.ndarray:
    """(n, 3, 3) world-space triangles of the visible DObjs."""
    by_offset = model.joint_by_offset()
    out = []
    for dobj in model.dobjs:
        if dobj.index not in visible_dobjs:
            continue
        owner = model.joints[dobj.joint]
        for pobj in dobj.pobjs:
            if pobj.tris.shape[0] == 0:
                continue
            ptype = pobj.flags & POBJ_TYPE_MASK
            verts_h = np.concatenate([pobj.verts.astype(np.float64), np.ones((pobj.verts.shape[0], 1))], axis=1)
            if ptype == POBJ_ENVELOPE and pobj.envelopes:
                mats = []
                for weights in pobj.envelopes:
                    # A single-joint envelope stores its vertices in that
                    # joint's local space and uses the joint matrix directly;
                    # multi-joint envelopes store bind-space vertices and
                    # blend world * inverse-bind (HSD_PObjSetupMtx).
                    if len(weights) == 1:
                        mats.append(world[by_offset[weights[0][0]]])
                        continue
                    m = np.zeros((4, 4))
                    for joint_off, w in weights:
                        jidx = by_offset[joint_off]
                        inv = model.joints[jidx].inv_bind
                        jm = world[jidx] @ inv if inv is not None else world[jidx]
                        m += w * jm
                    mats.append(m)
                mats = np.array(mats)
                idx = np.clip(pobj.mtx_index, 0, len(mats) - 1)
                positions = np.einsum('nij,nj->ni', mats[idx], verts_h)[:, :3]
            else:
                if ptype == POBJ_SKIN and pobj.joint_ref:
                    jidx = by_offset[pobj.joint_ref]
                    inv = model.joints[jidx].inv_bind
                    m = world[jidx] @ inv if inv is not None else world[jidx]
                else:
                    m = world[owner.index]
                positions = (verts_h @ m.T)[:, :3]
            out.append(positions[pobj.tris])
    if not out:
        return np.zeros((0, 3, 3))
    return np.concatenate(out, axis=0)
