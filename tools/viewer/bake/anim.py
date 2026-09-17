"""FigaTree animation evaluation: a port of the HSD FObj keyframe interpreter.

Follows src/sysdolphin/baselib/fobj.c (HSD_FObjReqAnim + HSD_FObjInterpretAnim
with rate 0, which is how ftAnim shows an exact frame) and
src/melee/lb/lbanim.c (FigaTree -> per-joint track binding).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

from .dat import Dat, load_dat

HSD_A_FRAC_FLOAT = 0 << 5
HSD_A_FRAC_S16 = 1 << 5
HSD_A_FRAC_U16 = 2 << 5
HSD_A_FRAC_S8 = 3 << 5
HSD_A_FRAC_U8 = 4 << 5

OP_NONE, OP_CON, OP_LIN, OP_SPL0, OP_SPL, OP_SLP, OP_KEY = range(7)

A_J_ROTX, A_J_ROTY, A_J_ROTZ, A_J_PATH = 1, 2, 3, 4
A_J_TRAX, A_J_TRAY, A_J_TRAZ = 5, 6, 7
A_J_SCAX, A_J_SCAY, A_J_SCAZ = 8, 9, 10
A_J_NODE, A_J_BRANCH = 11, 12


@dataclass
class Track:
    length: int
    startframe: int
    obj_type: int
    frac_value: int
    frac_slope: int
    data: bytes  # ad_head .. ad_head+length


@dataclass
class FigaTree:
    name: str
    frames: float
    classical_scale: bool
    joint_tracks: list[list[Track]]  # per joint (preorder)


def _parse_float(buf: bytes, pos: int, frac: int) -> tuple[float, int]:
    if frac == HSD_A_FRAC_FLOAT:
        return struct.unpack_from('<f', buf, pos)[0], pos + 4
    denom = 1 << (frac & 0x1F)
    kind = frac & 0xE0
    if kind == HSD_A_FRAC_S8:
        return struct.unpack_from('<b', buf, pos)[0] / denom, pos + 1
    if kind == HSD_A_FRAC_U8:
        return buf[pos] / denom, pos + 1
    if kind == HSD_A_FRAC_S16:
        return struct.unpack_from('<h', buf, pos)[0] / denom, pos + 2
    if kind == HSD_A_FRAC_U16:
        return struct.unpack_from('<H', buf, pos)[0] / denom, pos + 2
    return 0.0, pos


def _parse_varint(buf: bytes, pos: int) -> tuple[int, int]:
    value = 0
    shift = 0
    while True:
        d = buf[pos]
        pos += 1
        value |= (d & 0x7F) << shift
        shift += 7
        if not (d & 0x80):
            return value, pos


def _hermite(fterm_inv: float, time: float, p0: float, p1: float, d0: float, d1: float) -> float:
    t2 = time * time
    f2 = fterm_inv * fterm_inv
    t2_f = t2 * fterm_inv
    t3_f2 = f2 * (t2 * time)
    two_t3_f3 = 2.0 * t3_f2 * fterm_inv
    three_t2_f2 = 3.0 * t2 * f2
    return (p0 * (two_t3_f3 - three_t2_f2 + 1.0) + p1 * (three_t2_f2 - two_t3_f3)
            + d0 * (t3_f2 - 2.0 * t2_f + time) + d1 * (t3_f2 - t2_f))


class FObjState:
    """Streaming keyframe cursor (fields mirror HSD_FObj)."""

    def __init__(self, track: Track):
        self.track = track
        self.buf = track.data

    def req(self, frame: float) -> None:
        self.ad = 0
        self.time = float(self.track.startframe) + frame
        self.op = 0
        self.op_intrp = 0
        self.flags = 0
        self.nb_pack = 0
        self.fterm = 0.0
        self.p0 = self.p1 = self.d0 = self.d1 = 0.0
        self.state = 1

    # -- loaders ---------------------------------------------------------
    def _load_data(self) -> int:
        if self.ad >= self.track.length:
            return 6
        self.op_intrp = self.op
        if self.nb_pack == 0:
            self.op = self.buf[self.ad] & 0xF
            d = self.buf[self.ad]
            self.ad += 1
            nb = ((d >> 4) & 7) + 1
            shift = 3
            if d & 0x80:
                while True:
                    d = self.buf[self.ad]
                    self.ad += 1
                    nb += (d & 0x7F) << shift
                    shift += 7
                    if not (d & 0x80):
                        break
            self.nb_pack = nb
        self.nb_pack -= 1
        st = self.state
        nxt = 3 if st == 1 else 4
        fv, fs = self.track.frac_value, self.track.frac_slope
        if self.op in (OP_CON, OP_LIN):
            self.p0 = self.p1
            self.p1, self.ad = _parse_float(self.buf, self.ad, fv)
            if self.op_intrp != OP_SLP:
                self.d0 = self.d1
                self.d1 = 0.0
            self.state = nxt
        elif self.op == OP_SPL0:
            self.p0 = self.p1
            self.d0 = self.d1
            self.p1, self.ad = _parse_float(self.buf, self.ad, fv)
            self.d1 = 0.0
            self.state = nxt
        elif self.op == OP_SPL:
            self.p0 = self.p1
            self.p1, self.ad = _parse_float(self.buf, self.ad, fv)
            self.d0 = self.d1
            self.d1, self.ad = _parse_float(self.buf, self.ad, fs)
            self.state = nxt
        elif self.op == OP_SLP:
            self.d0 = self.d1
            self.d1, self.ad = _parse_float(self.buf, self.ad, fs)
            # state unchanged
        elif self.op == OP_KEY:
            self._launch_key()
            self.p1, self.ad = _parse_float(self.buf, self.ad, fv)
            self.flags |= 0x40
            self.state = nxt
        else:
            return 0
        return self.state

    def _load_wait(self) -> int:
        if self.ad >= self.track.length:
            return 6
        wait, self.ad = _parse_varint(self.buf, self.ad)
        self.fterm = float(wait)
        self.flags |= 0x20
        self.state = 2
        return 2

    def _launch_key(self) -> None:
        if self.flags & 0x40:
            self.op_intrp = self.op
            self.flags &= ~0x40
            self.flags |= 0x80
            self.p0 = self.p1

    def _value(self) -> float | None:
        op = self.op_intrp
        if op == OP_KEY:
            if self.flags & 0x80:
                self.flags &= ~0x80
                return self.p0
            return None
        if op == OP_CON:
            return self.p1 if self.time >= self.fterm else self.p0
        if op == OP_LIN:
            if self.flags & 0x20:
                self.flags &= ~0x20
                if self.fterm != 0:
                    self.d0 = (self.p1 - self.p0) / self.fterm
                else:
                    self.d0 = 0.0
                    self.p0 = self.p1
            return self.d0 * self.time + self.p0
        if op in (OP_SPL0, OP_SPL, OP_SLP):
            if self.fterm != 0:
                return _hermite(1.0 / self.fterm, self.time, self.p0, self.p1, self.d0, self.d1)
            return self.p1
        return None

    def evaluate(self, frame: float) -> float | None:
        """HSD_FObjReqAnim(frame) then HSD_FObjInterpretAnim(rate 0)."""
        self.req(frame)
        if self.time < 0.0:
            return None
        fterm = 0.0
        state = self.state
        value = None
        while True:
            if state == 6:
                self.time += fterm
                self._launch_key()
                return self._value()
            if state in (1, 2):
                state = self._load_data()
            elif state == 3:
                if self.flags & 0x80:
                    value = self._value()
                state = self._load_wait()
            elif state == 4:
                if self.fterm <= self.time:
                    fterm = self.fterm
                    self.time -= self.fterm
                    state = 3
                    self.state = 3
                    continue
                v = self._value()
                self.state = 5
                return v if v is not None else value
            elif state == 5:
                state = 4
                self.state = 4
            elif state == 0:
                return value


def load_figatree(blob: bytes, name: str) -> FigaTree:
    dat = load_dat(blob)
    root = next(iter(dat.roots.values()))
    tree_type = dat.u32(root)
    frames = dat.f32(root + 8)
    nodes = dat.ptr(root + 12)
    tracks = dat.ptr(root + 16)
    counts: list[int] = []
    k = 0
    while True:
        b = dat.u8(nodes + k)
        if b == 0xFF:
            break
        counts.append(b)
        k += 1
    joint_tracks: list[list[Track]] = []
    t = 0
    for count in counts:
        group = []
        for _ in range(count):
            off = tracks + t * 12
            length = dat.u16(off)
            start = dat.u16(off + 2)
            ad = dat.ptr(off + 8)
            group.append(Track(
                length=length, startframe=start, obj_type=dat.u8(off + 4),
                frac_value=dat.u8(off + 5), frac_slope=dat.u8(off + 6),
                data=bytes(dat.data[ad:ad + length]),
            ))
            t += 1
        joint_tracks.append(group)
    return FigaTree(name=name, frames=frames, classical_scale=bool(tree_type & 1),
                    joint_tracks=joint_tracks)


@dataclass
class Subaction:
    index: int
    name: str
    anim_offset: int
    anim_size: int
    script: int
    flags: int


def read_subactions(ftdata: Dat, root_name: str, count: int) -> list[Subaction]:
    ft = ftdata.roots[root_name]
    table = ftdata.ptr(ft + 0xC)
    out = []
    for i in range(count):
        e = table + i * 0x18
        name_off = ftdata.ptr(e)
        if not name_off:
            continue
        end = ftdata.data.index(b'\0', name_off)
        out.append(Subaction(
            index=i, name=ftdata.data[name_off:end].decode(),
            anim_offset=ftdata.u32(e + 4), anim_size=ftdata.u32(e + 8),
            script=ftdata.ptr(e + 0xC), flags=ftdata.u32(e + 0x10),
        ))
    return out


class Pose:
    """Evaluates a FigaTree into per-joint SRT lists for a frame."""

    def __init__(self, tree: FigaTree, base_rotation, base_scale, base_translate):
        self.tree = tree
        self.base_rotation = [list(r) for r in base_rotation]
        self.base_scale = [list(s) for s in base_scale]
        self.base_translate = [list(t) for t in base_translate]
        self.cursors = [[FObjState(tr) for tr in group] for group in tree.joint_tracks]

    def evaluate(self, frame: float):
        rotation = [list(r) for r in self.base_rotation]
        scale = [list(s) for s in self.base_scale]
        translate = [list(t) for t in self.base_translate]
        hidden_nodes: set[int] = set()
        hidden_branches: set[int] = set()
        for joint, cursors in enumerate(self.cursors):
            if joint >= len(rotation):
                break
            for cur in cursors:
                v = cur.evaluate(frame)
                if v is None:
                    continue
                t = cur.track.obj_type
                if t == A_J_ROTX:
                    rotation[joint][0] = v
                elif t == A_J_ROTY:
                    rotation[joint][1] = v
                elif t == A_J_ROTZ:
                    rotation[joint][2] = v
                elif t == A_J_TRAX:
                    translate[joint][0] = v
                elif t == A_J_TRAY:
                    translate[joint][1] = v
                elif t == A_J_TRAZ:
                    translate[joint][2] = v
                elif t in (A_J_SCAX, A_J_SCAY, A_J_SCAZ):
                    if abs(v) < 1e-3:
                        v = 1e-3
                    scale[joint][t - A_J_SCAX] = v
                elif t == A_J_NODE:
                    if v <= 0.5:
                        hidden_nodes.add(joint)
                elif t == A_J_BRANCH:
                    if v <= 0.5:
                        hidden_branches.add(joint)
        return rotation, scale, translate, hidden_nodes, hidden_branches
