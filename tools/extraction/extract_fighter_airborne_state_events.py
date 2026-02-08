from __future__ import annotations

import argparse
import struct
from pathlib import Path

from tools.extraction.extract_fighter_moves import (
    _load_fighter_dat,
    _load_special_msids,
    _parse_ftco_submotion_enum,
    _parse_subaction_events,
    _read_s_temp4_subaction_ptr,
)

_MAGIC = b"MSLAIRS1"
_VERSION = 1
_NO_EVENT = 0xFF


def _build_events_by_frame(events: list[object], *, max_frames: int) -> list[int]:
    out = [_NO_EVENT for _ in range(max_frames)]
    for ev in events:
        if getattr(ev, "kind", None) != "set_airborne_state":
            continue
        try:
            frame = int(getattr(ev, "frame"))
        except Exception:
            continue
        if frame < 0 or frame >= max_frames:
            continue
        data = getattr(ev, "data", None)
        try:
            st = int((data or {}).get("state", _NO_EVENT))
        except Exception:
            st = _NO_EVENT
        if st < 0 or st > 2:
            continue
        out[frame] = int(st)
    return out


def _write_bin(out_path: Path, *, msids: list[int], payloads_by_msid: dict[int, list[int]], max_frames: int) -> None:
    # Layout (little-endian):
    # - magic[8] = "MSLAIRS1"
    # - version: u32 = 1
    # - frame_count: u16
    # - reserved: u16 = 0
    # - entry_count: u32
    # - index[entry_count] entries, each:
    #     - msid: u16
    #     - reserved: u16 = 0
    #     - payload_bytes: u32 = frame_count * 1
    #     - payload_off: u32 (absolute)
    # - payload: concatenated u8[frame_count] for each msid
    if not (0 <= max_frames <= 0xFFFF):
        raise ValueError(f"max_frames out of range for u16: {max_frames}")
    if len(msids) > 0xFFFF_FFFF:
        raise ValueError("too many entries")

    entry_count = len(msids)
    index_rec_bytes = 12
    hdr_bytes = 20
    payload_bytes = int(max_frames)
    index_bytes = entry_count * index_rec_bytes
    payload_base = hdr_bytes + index_bytes

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(_MAGIC)
        f.write(struct.pack("<IHHI", int(_VERSION), int(max_frames), 0, int(entry_count)))
        off = payload_base
        for msid in msids:
            f.write(struct.pack("<HHII", int(msid) & 0xFFFF, 0, int(payload_bytes), int(off)))
            off += payload_bytes
        for msid in msids:
            frames = payloads_by_msid.get(msid)
            if frames is None:
                frames = [_NO_EVENT for _ in range(max_frames)]
            if len(frames) != max_frames:
                raise ValueError(f"bad frame count for msid={msid}: {len(frames)} want={max_frames}")
            for st in frames:
                f.write(struct.pack("<B", int(st) & 0xFF))


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Extract fighter movescript set_airborne_state (opcode 25) event timelines into compact .bin tables."
    )
    ap.add_argument("--iso_dir", type=Path, default=Path("_iso"))
    ap.add_argument("--melee_decomp", type=Path, default=Path("refs/melee"))
    ap.add_argument("--special_msids_dir", type=Path, default=Path("data/special_msids"))
    ap.add_argument("--out_dir", type=Path, default=Path("data/airborne_state_events"))
    ap.add_argument("--chars", type=str, default="fox,falco", help="comma-separated characters (fox,falco,...)")
    ap.add_argument("--max_frames", type=int, default=240)
    ap.add_argument("--max_steps_per_frame", type=int, default=10000)
    args = ap.parse_args()

    char_to_dat = {
        "fox": ("PlFx.dat", "ftDataFox"),
        "falco": ("PlFc.dat", "ftDataFalco"),
    }

    enum_map = _parse_ftco_submotion_enum(args.melee_decomp)
    msid_candidates: set[int] = set()
    ftco_sm_count = int(enum_map.get("ftCo_SM_Count", 0))
    if ftco_sm_count > 0:
        for msid in range(ftco_sm_count):
            if 0 <= msid <= 0xFFFF:
                msid_candidates.add(msid)

    out_dir: Path = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    for ch in [c.strip() for c in args.chars.split(",") if c.strip()]:
        if ch not in char_to_dat:
            raise RuntimeError(f"unknown character {ch!r}")
        dat_name, sym = char_to_dat[ch]

        arc = _load_fighter_dat(args.iso_dir, dat_name)
        ft_off = arc.get_public_offset(sym)
        if ft_off is None:
            raise RuntimeError(f"{dat_name}: missing public symbol {sym!r}")
        s_temp4_list = arc.ptr32(ft_off + 0x0C)

        payloads_by_msid: dict[int, list[int]] = {}
        msids_present: set[int] = set()

        for msid in sorted(set(msid_candidates) | set(_load_special_msids(args.special_msids_dir, ch))):
            sub_ptr = _read_s_temp4_subaction_ptr(arc, s_temp4_list, int(msid))
            if sub_ptr is None:
                continue
            try:
                events = _parse_subaction_events(
                    arc,
                    sub_ptr,
                    max_frames=int(args.max_frames),
                    max_steps_per_frame=int(args.max_steps_per_frame),
                )
            except RuntimeError:
                # Deterministic best-effort: skip scripts that exceed the interpreter step budget.
                continue
            timeline = _build_events_by_frame(events, max_frames=int(args.max_frames))
            if any(int(x) != _NO_EVENT for x in timeline):
                payloads_by_msid[int(msid)] = timeline
                msids_present.add(int(msid))

        _write_bin(
            out_dir / f"{ch}.bin",
            msids=sorted(msids_present),
            payloads_by_msid=payloads_by_msid,
            max_frames=int(args.max_frames),
        )


if __name__ == "__main__":
    main()
