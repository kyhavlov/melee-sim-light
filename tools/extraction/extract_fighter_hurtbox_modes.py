from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

from melee_sim.hsd_archive import HsdArchive
from tools.extraction.extract_fighter_moves import (
    _load_fighter_dat,
    _load_special_msids,
    _parse_ftco_submotion_enum,
    _parse_subaction_events,
    _read_s_temp4_subaction_ptr,
)

_MAGIC = b"MSLHURM1"
_VERSION = 1

# Decomp: refs/melee/src/melee/lb/forward.h
_HURTCAPSULE_ENABLED = 0
_HURTCAPSULE_DISABLED = 1
_HURTCAPSULE_INTANGIBLE = 2


@dataclass(frozen=True)
class _HurtCapsBin:
    capsule_count: int
    bone_part_ids: list[int]  # index -> Fighter_Part id (u16)


def _read_hurtcaps_bin(path: Path) -> _HurtCapsBin:
    # See tools/extraction/extract_fighter_hurtcapsules.py (_write_hurtcaps_bin).
    buf = path.read_bytes()
    if len(buf) < 16:
        raise ValueError(f"hurtcaps bin too small: {path}")
    if buf[:8] != b"MSLHURT1":
        raise ValueError(f"hurtcaps bin bad magic: {buf[:8]!r} path={path}")
    ver, cap_count_u16, _reserved = struct.unpack_from("<IHH", buf, 8)
    if int(ver) != 1:
        raise ValueError(f"hurtcaps bin bad version: {ver} path={path}")
    cap_count = int(cap_count_u16)
    if not (0 <= cap_count <= 32):
        raise ValueError(f"hurtcaps bin capsule_count out of range: {cap_count} path={path}")
    rec_bytes = 34
    need = 16 + cap_count * rec_bytes
    if len(buf) < need:
        raise ValueError(f"hurtcaps bin truncated: need={need} got={len(buf)} path={path}")
    bone_part_ids: list[int] = []
    off = 16
    for _i in range(cap_count):
        (bone_part_id,) = struct.unpack_from("<H", buf, off)
        bone_part_ids.append(int(bone_part_id))
        off += rec_bytes
    return _HurtCapsBin(capsule_count=cap_count, bone_part_ids=bone_part_ids)


def _pack_states_u64(states: list[int]) -> int:
    # 2-bit lanes, 32 capsules max.
    out = 0
    for i, st in enumerate(states):
        out |= (int(st) & 0x3) << (2 * i)
    return out


def _build_states_by_frame(
    events: list[object],
    *,
    bone_part_to_cap_index: dict[int, int],
    capsule_count: int,
    max_frames: int,
) -> list[int]:
    # Default: enabled (decomp init: refs/melee/src/melee/ft/ftcoll.c::ftColl_HurtboxInit).
    cur = [_HURTCAPSULE_ENABLED for _ in range(capsule_count)]
    out_u64: list[int] = []

    # events are already in deterministic movescript execution order within a frame.
    events_by_frame: list[list[object]] = [[] for _ in range(max_frames)]
    for ev in events:
        try:
            frame = int(getattr(ev, "frame"))
        except Exception:
            continue
        if 0 <= frame < max_frames:
            events_by_frame[frame].append(ev)

    for frame in range(max_frames):
        for ev in events_by_frame[frame]:
            kind = getattr(ev, "kind", None)
            data = getattr(ev, "data", None)
            if kind == "set_all_hurt_state":
                try:
                    st = int((data or {}).get("state", _HURTCAPSULE_ENABLED))
                except Exception:
                    st = _HURTCAPSULE_ENABLED
                # Decomp domain: HurtCapsuleState is {Enabled=0, Disabled=1, Intangible=2}.
                if st not in (_HURTCAPSULE_ENABLED, _HURTCAPSULE_DISABLED, _HURTCAPSULE_INTANGIBLE):
                    continue
                for i in range(capsule_count):
                    cur[i] = st
            elif kind == "set_hurt_state":
                try:
                    bone_idx = int((data or {}).get("bone_idx", -1))
                    st = int((data or {}).get("state", _HURTCAPSULE_ENABLED))
                except Exception:
                    continue
                if st not in (_HURTCAPSULE_ENABLED, _HURTCAPSULE_DISABLED, _HURTCAPSULE_INTANGIBLE):
                    continue
                cap_i = bone_part_to_cap_index.get(bone_idx)
                if cap_i is None:
                    continue
                if 0 <= cap_i < capsule_count:
                    cur[cap_i] = st

        out_u64.append(_pack_states_u64(cur))
    return out_u64


def _write_bin(
    out_path: Path,
    *,
    msids: list[int],
    payloads_by_msid: dict[int, list[int]],
    max_frames: int,
    capsule_count: int,
) -> None:
    # Layout (little-endian):
    # - magic[8] = "MSLHURM1"
    # - version: u32 = 1
    # - frame_count: u16
    # - capsule_count: u16
    # - entry_count: u32
    # - index[entry_count] entries, each:
    #     - msid: u16
    #     - reserved: u16 = 0
    #     - payload_bytes: u32 = frame_count * 8
    #     - payload_off: u32 (absolute)
    # - payload: concatenated u64[frame_count] for each msid (states packed into 2-bit lanes)
    if not (0 <= max_frames <= 0xFFFF):
        raise ValueError(f"max_frames out of range for u16: {max_frames}")
    if len(msids) > 0xFFFF_FFFF:
        raise ValueError("too many entries")

    if not (0 <= capsule_count <= 32):
        raise ValueError(f"capsule_count out of range: {capsule_count}")

    entry_count = len(msids)
    index_rec_bytes = 12
    hdr_bytes = 20
    payload_bytes = int(max_frames) * 8
    index_bytes = entry_count * index_rec_bytes
    payload_base = hdr_bytes + index_bytes

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(_MAGIC)
        f.write(struct.pack("<IHHI", int(_VERSION), int(max_frames), int(capsule_count), int(entry_count)))
        # Index.
        off = payload_base
        for msid in msids:
            f.write(struct.pack("<HHII", int(msid) & 0xFFFF, 0, int(payload_bytes), int(off)))
            off += payload_bytes
        # Payload.
        for msid in msids:
            frames = payloads_by_msid.get(msid)
            if frames is None:
                frames = [0 for _ in range(max_frames)]
            if len(frames) != max_frames:
                raise ValueError(f"bad frame count for msid={msid}: {len(frames)} want={max_frames}")
            for st_u64 in frames:
                f.write(struct.pack("<Q", int(st_u64) & 0xFFFF_FFFF_FFFF_FFFF))


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Extract fighter hurt capsule state timelines (movescript-derived) into compact .bin tables."
    )
    ap.add_argument("--iso_dir", type=Path, default=Path("_iso"))
    ap.add_argument("--melee_decomp", type=Path, default=Path("refs/melee"))
    ap.add_argument("--hurtcaps_dir", type=Path, default=Path("data/hurtcaps"))
    ap.add_argument("--special_msids_dir", type=Path, default=Path("data/special_msids"))
    ap.add_argument("--out_dir", type=Path, default=Path("data/hurtbox_states"))
    ap.add_argument("--chars", type=str, default="fox,falco", help="comma-separated characters (fox,falco,...)")
    ap.add_argument("--max_frames", type=int, default=240)
    ap.add_argument("--max_steps_per_frame", type=int, default=10000)
    args = ap.parse_args()

    char_to_dat = {
        "fox": ("PlFx.dat", "ftDataFox"),
        "falco": ("PlFc.dat", "ftDataFalco"),
    }

    enum_map = _parse_ftco_submotion_enum(args.melee_decomp)
    want = [
        "ftCo_SM_Attack11",
        "ftCo_SM_AttackDash",
        "ftCo_SM_AttackS3",
        "ftCo_SM_AttackHi3",
        "ftCo_SM_AttackLw3",
        "ftCo_SM_AttackS4",
        "ftCo_SM_AttackHi4",
        "ftCo_SM_AttackLw4",
        "ftCo_SM_AttackAirN",
        "ftCo_SM_AttackAirF",
        "ftCo_SM_AttackAirB",
        "ftCo_SM_AttackAirHi",
        "ftCo_SM_AttackAirLw",
        "ftCo_SM_DownAttackU",
        "ftCo_SM_DownAttackD",
        "ftCo_SM_Catch",
        "ftCo_SM_CatchDash",
        "ftCo_SM_CatchWait",
        "ftCo_SM_ThrowF",
        "ftCo_SM_ThrowB",
        "ftCo_SM_ThrowHi",
        "ftCo_SM_ThrowLw",
        "ftCo_SM_ThrownF",
        "ftCo_SM_ThrownB",
        "ftCo_SM_ThrownHi",
        "ftCo_SM_ThrownLw",
    ]
    want_ids = {name: enum_map[name] for name in want if name in enum_map}
    if len(want_ids) != len(want):
        missing = sorted(set(want) - set(want_ids))
        raise RuntimeError(f"missing expected ftCo_SM_* entries: {missing}")

    out_dir: Path = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    for ch in [c.strip() for c in args.chars.split(",") if c.strip()]:
        if ch not in char_to_dat:
            raise RuntimeError(f"unknown character {ch!r}")
        dat_name, sym = char_to_dat[ch]

        hurtcaps = _read_hurtcaps_bin(args.hurtcaps_dir / f"{ch}.bin")
        # Decomp semantics: ftColl_8007B128 returns on first matching bone index, so preserve the
        # first occurrence when multiple capsules share the same bone_part_id.
        bone_part_to_cap_index: dict[int, int] = {}
        for i, b in enumerate(hurtcaps.bone_part_ids):
            bb = int(b)
            if bb not in bone_part_to_cap_index:
                bone_part_to_cap_index[bb] = int(i)

        arc = _load_fighter_dat(args.iso_dir, dat_name)
        ft_off = arc.get_public_offset(sym)
        if ft_off is None:
            raise RuntimeError(f"{dat_name}: missing public symbol {sym!r}")
        s_temp4_list = arc.ptr32(ft_off + 0x0C)

        payloads_by_msid: dict[int, list[int]] = {}
        msids: set[int] = set()

        for _name, sm_id in want_ids.items():
            sub_ptr = _read_s_temp4_subaction_ptr(arc, s_temp4_list, sm_id)
            if sub_ptr is None:
                continue
            # Note: the move-script commands that toggle hurt capsule state are parsed by
            # tools/extraction/extract_fighter_moves.py::_parse_subaction_events:
            # - opcode 27 => kind="set_all_hurt_state"
            # - opcode 28 => kind="set_hurt_state"
            events = _parse_subaction_events(
                arc,
                sub_ptr,
                max_frames=int(args.max_frames),
                max_steps_per_frame=int(args.max_steps_per_frame),
            )
            states = _build_states_by_frame(
                events,
                bone_part_to_cap_index=bone_part_to_cap_index,
                capsule_count=hurtcaps.capsule_count,
                max_frames=int(args.max_frames),
            )
            payloads_by_msid[int(sm_id)] = states
            msids.add(int(sm_id))

        for msid in _load_special_msids(args.special_msids_dir, ch):
            sub_ptr = _read_s_temp4_subaction_ptr(arc, s_temp4_list, int(msid))
            if sub_ptr is None:
                continue
            events = _parse_subaction_events(
                arc,
                sub_ptr,
                max_frames=int(args.max_frames),
                max_steps_per_frame=int(args.max_steps_per_frame),
            )
            states = _build_states_by_frame(
                events,
                bone_part_to_cap_index=bone_part_to_cap_index,
                capsule_count=hurtcaps.capsule_count,
                max_frames=int(args.max_frames),
            )
            payloads_by_msid[int(msid)] = states
            msids.add(int(msid))

        msids_sorted = sorted(msids)
        _write_bin(
            out_dir / f"{ch}.bin",
            msids=msids_sorted,
            payloads_by_msid=payloads_by_msid,
            max_frames=int(args.max_frames),
            capsule_count=hurtcaps.capsule_count,
        )


if __name__ == "__main__":
    main()
