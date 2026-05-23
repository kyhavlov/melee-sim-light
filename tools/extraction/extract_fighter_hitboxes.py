from __future__ import annotations

"""
`data/moves/<char>.json` → `data/hitboxes/<char>.bin` (MSLHITB1 v1).

This module is intentionally *not* ISO-direct: it compiles the already ISO-derived movescript event
stream into a compact binary table for init-time C loading.
"""

import argparse
import json
import struct
from dataclasses import dataclass, replace
from pathlib import Path


_REC_BYTES = 44


@dataclass(frozen=True)
class _Rec:
    frame: int
    kind: int  # 0 = set/enable, 1 = clear, 2 = active-slot damage mutation
    hitbox_id: int
    bone_part_id: int
    x: float
    y: float
    z: float
    radius: float
    damage: float
    u16_tail: tuple[int, int, int, int, int, int, int, int]

    def pack(self) -> bytes:
        return struct.pack(
            "<HBBI5f8H",
            int(self.frame) & 0xFFFF,
            int(self.kind) & 0xFF,
            int(self.hitbox_id) & 0xFF,
            int(self.bone_part_id) & 0xFFFF_FFFF,
            float(self.x),
            float(self.y),
            float(self.z),
            float(self.radius),
            float(self.damage),
            *[int(x) & 0xFFFF for x in self.u16_tail],
        )


def _hit_flags_u16(hb: dict) -> int:
    # src/hitboxes_tables.h: MSLHITB1 v1 u16_6 bit assignments
    # Decomp trail for the underlying bools:
    # - refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C (create_hitbox_3/create_hitbox_4)
    flags = 0
    if hb.get("hit_grounded"):
        flags |= 1 << 9
    if hb.get("hit_aerial"):
        flags |= 1 << 10
    if hb.get("item_hit_interaction"):
        flags |= 1 << 11
    if hb.get("ignore_thrown_fighters"):
        flags |= 1 << 12
    if hb.get("ignore_fighter_scale"):
        flags |= 1 << 13
    if hb.get("clank"):
        flags |= 1 << 14
    if hb.get("rebound"):
        flags |= 1 << 15
    return flags & 0xFFFF


def _u16_7_hitlist_meta(hb: dict) -> int:
    # Hit group is spawn_hitbox_0.hit_group / HitCapsule.x4:
    # - refs/melee/src/melee/lb/types.h::spawn_hitbox_0
    # - refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    #
    # Rehit rate uses HitCapsule.x40_b4 as the per-victim timer:
    # - refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 and ::lbColl_80008A5C
    #
    # Current extraction policy: rehit_frames is always 0 until we find an asm-backed source for
    # HitCapsule.x40_b4 in fighter movescripts (see tools/extraction/extract_fighter_moves.py).
    hit_group = int(hb.get("hit_group", 0)) & 0x7
    rehit_frames = int(hb.get("rehit_frames", 0)) & 0xFF
    return int(rehit_frames | (hit_group << 8)) & 0xFFFF


def _records_from_events(events: list[dict]) -> list[_Rec]:
    out: list[_Rec] = []
    active: dict[int, _Rec] = {}
    for ev in events:
        kind = ev.get("kind")
        frame = int(ev.get("frame", 0))
        if kind == "create_hitbox":
            hb = ev.get("data", {}).get("hitbox", {}) or {}

            hb_id = int(hb.get("hitbox_id", 0xFF))
            if not (0 <= hb_id < 4):
                continue

            use_common = bool(hb.get("use_common_bone_ids", False))
            if use_common:
                # Decomp shape would require ftParts_GetBoneIndex(fp, bone_id).
                # Bounded scope (Fox/Falco only) currently never sets use_common_bone_ids.
                raise ValueError("create_hitbox: use_common_bone_ids=1 is not supported in this extractor yet")

            bone_part_id = int(hb.get("bone", 0)) & 0xFF

            # ftAction_8007121C sets b_offset as:
            # - x := create_hitbox_1.z_offset
            # - y := create_hitbox_2.y_offset
            # - z := create_hitbox_2.x_offset
            # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
            x = float(hb.get("z_offset", 0.0))
            y = float(hb.get("y_offset", 0.0))
            z = float(hb.get("x_offset", 0.0))

            radius = float(hb.get("size", 0.0))
            damage = float(hb.get("damage", 0.0))

            angle = int(hb.get("angle", 0)) & 0xFFFF
            kbg = int(hb.get("kbg", 0)) & 0xFFFF
            wsk = int(hb.get("wsk", 0)) & 0xFFFF
            bkb = int(hb.get("bkb", 0)) & 0xFFFF

            element = int(hb.get("element", 0)) & 0xFF
            shield_damage = int(hb.get("shield_damage", 0)) & 0xFF
            u16_4 = (element & 0xFF) | ((shield_damage & 0xFF) << 8)

            sfx_sev = int(hb.get("sfx_severity", 0)) & 0xFF
            sfx_kind = int(hb.get("sfx_kind", 0)) & 0xFF
            u16_5 = (sfx_sev & 0xFF) | ((sfx_kind & 0xFF) << 8)

            u16_6 = _hit_flags_u16(hb)
            u16_7 = _u16_7_hitlist_meta(hb)

            rec = _Rec(
                frame=frame,
                kind=0,
                hitbox_id=hb_id,
                bone_part_id=bone_part_id,
                x=x,
                y=y,
                z=z,
                radius=radius,
                damage=damage,
                u16_tail=(angle, kbg, wsk, bkb, u16_4, u16_5, u16_6, u16_7),
            )
            active[hb_id] = rec
            out.append(rec)
        elif kind == "set_hitbox_damage":
            # Decomp opcode 12 (`ftAction_8007162C`) mutates an already-live HitCapsule's damage.
            # Keep this distinct from create/enable records so runtime does not replay
            # ftColl_800768A0 enable-edge clear/copy or pose-create publication.
            idx = int(ev.get("data", {}).get("idx", -1))
            if not (0 <= idx < 4) or idx not in active:
                continue
            rec = replace(active[idx], frame=frame, kind=2, damage=float(ev["data"]["damage"]))
            active[idx] = rec
            out.append(rec)
        elif kind == "clear_hitboxes":
            active.clear()
            out.append(
                _Rec(
                    frame=frame,
                    kind=1,
                    hitbox_id=0xFF,
                    bone_part_id=0,
                    x=0.0,
                    y=0.0,
                    z=0.0,
                    radius=0.0,
                    damage=0.0,
                    u16_tail=(0, 0, 0, 0, 0, 0, 0, 0),
                )
            )
        elif kind == "remove_hitbox":
            idx = int(ev.get("data", {}).get("idx", -1))
            if not (0 <= idx < 4):
                continue
            active.pop(idx, None)
            out.append(
                _Rec(
                    frame=frame,
                    kind=1,
                    hitbox_id=idx,
                    bone_part_id=0,
                    x=0.0,
                    y=0.0,
                    z=0.0,
                    radius=0.0,
                    damage=0.0,
                    u16_tail=(0, 0, 0, 0, 0, 0, 0, 0),
                )
            )

    for r in out:
        b = r.pack()
        if len(b) != _REC_BYTES:
            raise AssertionError(f"bad record size: got {len(b)} bytes, expected {_REC_BYTES}")
    return out


def _msid_events_from_moves_json(moves_json: dict) -> dict[int, list[dict]]:
    out: dict[int, list[dict]] = {}

    for _name, entry in (moves_json.get("moves") or {}).items():
        msid = int(entry.get("submotion_id", -1))
        if msid < 0:
            continue
        out[msid] = list(entry.get("events") or [])

    for msid_str, entry in (moves_json.get("specials_by_msid") or {}).items():
        try:
            msid = int(msid_str)
        except ValueError:
            continue
        out[msid] = list(entry.get("events") or [])

    return out


def write_mslhitb1(*, moves_json_path: Path, out_path: Path) -> None:
    """
    Build `data/hitboxes/<char>.bin` (MSLHITB1 v1) from ISO-derived `data/moves/<char>.json`.

    Source pointers:
    - Binary layout: agent_docs/DATA_CONTRACT.md (MSLHITB1 v1)
    - Move event extraction + create_hitbox decode:
      tools/extraction/extract_fighter_moves.py::_parse_subaction_events and ::_decode_create_hitbox
    - Decomp shape for create_hitbox (field meanings):
      refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    """
    moves = json.loads(moves_json_path.read_text(encoding="utf-8"))
    msid_to_events = _msid_events_from_moves_json(moves)

    msids = sorted(msid_to_events.keys())
    entry_count = len(msids)

    index_base = 16
    index_bytes = entry_count * 12
    payload_base = index_base + index_bytes

    index_entries: list[tuple[int, int, int, int]] = []  # (msid, rec_count, rec_bytes, payload_off)
    payload_chunks: list[bytes] = []

    cur_off = payload_base
    for msid in msids:
        recs = _records_from_events(msid_to_events[msid])
        payload = b"".join(r.pack() for r in recs)
        rec_count = len(recs)
        index_entries.append((int(msid) & 0xFFFF, int(rec_count) & 0xFFFF, len(payload), cur_off))
        payload_chunks.append(payload)
        cur_off += len(payload)

    header = b"".join(
        [
            b"MSLHITB1",
            struct.pack("<I", 1),
            struct.pack("<I", entry_count),
        ]
    )

    idx = b"".join(struct.pack("<HHII", msid, rec_count, rec_bytes, payload_off) for msid, rec_count, rec_bytes, payload_off in index_entries)
    out_path.write_bytes(header + idx + b"".join(payload_chunks))


def main() -> None:
    ap = argparse.ArgumentParser(description="Build MSLHITB1 hitbox event tables from extracted moves JSON.")
    ap.add_argument("--moves", type=Path, required=True, help="path to data/moves/<char>.json")
    ap.add_argument("--out", type=Path, required=True, help="output path (typically data/hitboxes/<char>.bin)")
    args = ap.parse_args()
    write_mslhitb1(moves_json_path=args.moves, out_path=args.out)


if __name__ == "__main__":
    main()
