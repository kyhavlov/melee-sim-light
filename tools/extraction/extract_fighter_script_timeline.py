from __future__ import annotations

import argparse

from tools.extraction.char_registry import CHARS
import json
import struct
from pathlib import Path

from tools.extraction.extract_fighter_moves import (
    _load_fighter_dat,
    _load_special_msids,
    _parse_ftco_submotion_enum,
    _parse_subaction_events,
    _read_s_temp4_subaction_ptr,
)
from tools.extraction.known_data_artifacts import SCRIPT_MAGIC, SCRIPT_VERSION


EVENT_IDS = {
    "create_hitbox": 1,
    "set_hitbox_damage": 2,
    "set_hitbox_size": 3,
    "set_hitbox_interaction": 4,
    "remove_hitbox": 5,
    "clear_hitboxes": 6,
    "set_cmd_var": 7,
    "set_throw_flags": 8,
    "allow_interrupt": 9,
    "set_throw_spawn_projectile": 10,
    "set_airborne_state": 11,
    "set_hit_status": 12,
    "set_all_hurt_state": 13,
    "set_hurt_state": 14,
    "set_jab_combo": 15,
    "set_jab_rapid": 16,
    "toggle_bone_physics": 17,
    "set_state_flags_221c_u16_y": 18,
    "start_smash_charge": 19,
    "pseudo_random_sfx": 20,
    "set_throw_hitbox": 21,
}

_CREATE_HITBOX_FLAGS = {
    "clank": 1 << 0,
    "rebound": 1 << 1,
    "hit_grounded": 1 << 2,
    "hit_aerial": 1 << 3,
    "use_common_bone_ids": 1 << 4,
    "ignore_fighter_scale": 1 << 5,
    "item_hit_interaction": 1 << 6,
    "ignore_thrown_fighters": 1 << 7,
    "only_hit_grabbed": 1 << 8,
    "item_match_start_x138": 1 << 9,
    "item_body_enabled": 1 << 10,
    "item_grabbable_only": 1 << 11,
}

RUNTIME_OWNER_EVENT_KINDS = {
    "set_cmd_var",
    "set_hit_status",
    "set_all_hurt_state",
    "set_hurt_state",
    "set_airborne_state",
    "set_state_flags_221c_u16_y",
}

UNSUPPORTED_EVENT_KINDS = {
    "set_hitbox_size",
    "set_hitbox_interaction",
    "remove_hitbox",
}


class UnsupportedScriptEventKind(ValueError):
    pass


def _as_u8(value: object) -> int:
    v = int(value)
    if not 0 <= v <= 0xFF:
        raise ValueError(f"u8 out of range: {v}")
    return v


def _as_i8(value: object) -> int:
    v = int(value)
    if not -128 <= v <= 127:
        raise ValueError(f"i8 out of range: {v}")
    return v


def _as_u16(value: object) -> int:
    v = int(value)
    if not 0 <= v <= 0xFFFF:
        raise ValueError(f"u16 out of range: {v}")
    return v


def _as_u32(value: object) -> int:
    v = int(value)
    if not 0 <= v <= 0xFFFFFFFF:
        raise ValueError(f"u32 out of range: {v}")
    return v


def _encode_payload(kind: str, data: dict) -> bytes:
    if kind in {
        "allow_interrupt",
        "clear_hitboxes",
        "set_throw_spawn_projectile",
        "toggle_bone_physics",
    }:
        return b""
    if kind == "set_cmd_var":
        return struct.pack("<BHx", _as_u8(data["idx"]), _as_u16(data["value"]))
    if kind == "set_throw_flags":
        return struct.pack("<Bxxx", _as_u8(data["hit_idx"]))
    if kind == "set_hitbox_damage":
        return struct.pack("<Bxxxf", _as_u8(data["idx"]), float(data["damage"]))
    if kind in {"set_airborne_state", "set_hit_status", "set_all_hurt_state", "set_jab_rapid"}:
        return struct.pack("<Bxxx", _as_u8(data["state"]))
    if kind == "set_hurt_state":
        return struct.pack("<BBxx", _as_u8(data["bone_idx"]), _as_u8(data["state"]))
    if kind == "set_jab_combo":
        return struct.pack("<Bxxx", _as_u8(data["disabled"]))
    if kind == "set_state_flags_221c_u16_y":
        return struct.pack("<Hxx", _as_u16(data["flags"]))
    if kind == "start_smash_charge":
        return struct.pack(
            "<BBHf",
            _as_u8(data["hold_frames"]),
            _as_u8(data["color_anim"]),
            0,
            float(data["damage_mul"]),
        )
    if kind == "pseudo_random_sfx":
        return struct.pack(
            "<BBBB",
            _as_u8(data["random_range"]),
            _as_u8(data["volume"]),
            _as_u8(data["panning"]),
            _as_u8(data["behavior"]),
        )
    if kind == "set_throw_hitbox":
        return struct.pack(
            "<BBBBHHHHf",
            _as_u8(data["idx"]),
            _as_u8(data["element"]),
            _as_u8(data["sfx_kind"]),
            _as_u8(data["sfx_severity"]),
            _as_u16(data["angle"]),
            _as_u16(data["kbg"]),
            _as_u16(data["wsk"]),
            _as_u16(data["bkb"]),
            float(data["damage"]),
        )
    if kind == "create_hitbox":
        hb = dict(data["hitbox"])
        flags = 0
        for key, bit in _CREATE_HITBOX_FLAGS.items():
            if bool(hb.get(key, False)):
                flags |= bit
        return struct.pack(
            "<BBBBBBbBHHHHfffffI",
            _as_u8(hb["hitbox_id"]),
            _as_u8(hb["bone"]),
            _as_u8(hb["hit_group"]),
            _as_u8(hb["element"]),
            _as_u8(hb["sfx_kind"]),
            _as_u8(hb["sfx_severity"]),
            _as_i8(hb["shield_damage"]),
            _as_u8(hb["rehit_frames"]),
            _as_u16(hb["angle"]),
            _as_u16(hb["kbg"]),
            _as_u16(hb["wsk"]),
            _as_u16(hb["bkb"]),
            float(hb["damage"]),
            float(hb["size"]),
            float(hb["x_offset"]),
            float(hb["y_offset"]),
            float(hb["z_offset"]),
            _as_u32(flags),
        )
    if kind in UNSUPPORTED_EVENT_KINDS:
        # These are currently unknown/unused for Fox/Falco generated data. Keep the event id
        # reserved but require an explicit encoder before writing runtime-consumed payloads.
        raise UnsupportedScriptEventKind(f"unsupported payload for event kind {kind!r}")
    raise ValueError(f"unsupported event kind {kind!r}")


def _iter_entries_from_moves(moves: dict) -> list[tuple[int, str, list[dict]]]:
    entries: list[tuple[int, str, list[dict]]] = []
    for name, rec in sorted(moves.get("moves", {}).items(), key=lambda kv: int(kv[1].get("submotion_id", 0))):
        entries.append((int(rec["submotion_id"]), str(name), list(rec.get("events", []))))
    for name, rec in sorted(moves.get("specials_by_msid", {}).items(), key=lambda kv: int(kv[0])):
        entries.append((int(rec["submotion_id"]), f"special_{name}", list(rec.get("events", []))))
    return entries


def _event_record(ev: object) -> dict:
    if isinstance(ev, dict):
        return ev
    return {
        "frame": int(getattr(ev, "frame")),
        "kind": str(getattr(ev, "kind")),
        "data": dict(getattr(ev, "data", {})),
    }


def _iter_entries_from_iso(
    *,
    character: str,
    iso_dir: Path,
    melee_decomp: Path,
    special_msids_dir: Path,
    max_frames: int,
    max_steps_per_frame: int,
) -> list[tuple[int, str, list[dict]]]:
    char_to_dat = {name: (info.pl_dat, info.ftdata_symbol) for name, info in CHARS.items()}
    if character not in char_to_dat:
        raise RuntimeError(f"unknown character {character!r}")

    enum_map = _parse_ftco_submotion_enum(melee_decomp)
    ftco_sm_count = int(enum_map.get("ftCo_SM_Count", 0))
    if ftco_sm_count <= 0:
        raise RuntimeError("missing ftCo_SM_Count from decomp ftCo_Submotion enum")
    name_by_msid = {int(v): k for k, v in enum_map.items() if k.startswith("ftCo_SM_")}

    dat_name, sym = char_to_dat[character]
    arc = _load_fighter_dat(iso_dir, dat_name)
    ft_off = arc.get_public_offset(sym)
    if ft_off is None:
        raise RuntimeError(f"{dat_name}: missing public symbol {sym!r}")
    s_temp4_list = arc.ptr32(ft_off + 0x0C)

    domain = sorted(set(range(ftco_sm_count)) | set(_load_special_msids(special_msids_dir, character)))
    entries: list[tuple[int, str, list[dict]]] = []
    for msid in domain:
        if not (0 <= int(msid) <= 0xFFFF):
            continue
        sub_ptr = _read_s_temp4_subaction_ptr(arc, s_temp4_list, int(msid))
        if sub_ptr is None:
            continue
        try:
            parsed = _parse_subaction_events(
                arc,
                sub_ptr,
                max_frames=int(max_frames),
                max_steps_per_frame=int(max_steps_per_frame),
            )
        except RuntimeError:
            continue
        name = name_by_msid.get(int(msid), f"special_{int(msid)}")
        entries.append((int(msid), name, [_event_record(ev) for ev in parsed]))
    return entries


def main() -> None:
    ap = argparse.ArgumentParser(description="Pack decoded stable fighter script events as MSLFTSC1.")
    ap.add_argument("--moves", type=Path, default=None)
    ap.add_argument("--character", type=str, default=None)
    ap.add_argument("--iso_dir", type=Path, default=Path("_iso"))
    ap.add_argument("--melee_decomp", type=Path, default=Path("refs/melee"))
    ap.add_argument("--special_msids_dir", type=Path, default=Path("data/special_msids"))
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--manifest", type=Path, default=None)
    ap.add_argument("--max_frames", type=int, default=240)
    ap.add_argument("--max_steps_per_frame", type=int, default=10000)
    args = ap.parse_args()

    moves = json.loads(args.moves.read_text(encoding="utf-8")) if args.moves is not None else {}
    if args.character is not None:
        entries = _iter_entries_from_iso(
            character=str(args.character),
            iso_dir=args.iso_dir,
            melee_decomp=args.melee_decomp,
            special_msids_dir=args.special_msids_dir,
            max_frames=int(args.max_frames),
            max_steps_per_frame=int(args.max_steps_per_frame),
        )
    elif args.moves is not None:
        entries = _iter_entries_from_moves(moves)
    else:
        raise SystemExit("either --character or --moves is required")

    index_rows: list[tuple[int, int, int]] = []
    event_payloads: list[bytes] = []
    event_count = 0
    unknown_counts: dict[str, int] = {}
    unsupported_counts: dict[str, int] = {}
    for msid, _name, events in entries:
        start = event_count
        for ev in events:
            kind = str(ev.get("kind", ""))
            kind_id = EVENT_IDS.get(kind)
            if kind_id is None:
                unknown_counts[kind] = unknown_counts.get(kind, 0) + 1
                continue
            try:
                payload = _encode_payload(kind, dict(ev.get("data", {})))
            except UnsupportedScriptEventKind:
                unsupported_counts[kind] = unsupported_counts.get(kind, 0) + 1
                continue
            event_payloads.append(
                struct.pack("<HHI", _as_u16(ev.get("frame", 0)), kind_id, len(payload)) + payload
            )
            event_count += 1
        index_rows.append((_as_u16(msid), start, event_count - start))

    index_off = 28
    event_off = index_off + len(index_rows) * 12
    buf = bytearray()
    buf += SCRIPT_MAGIC
    buf += struct.pack("<IIIII", SCRIPT_VERSION, len(index_rows), event_count, index_off, event_off)
    for msid, start, count in index_rows:
        buf += struct.pack("<HHiI", msid, 0, start, count)
    for payload in event_payloads:
        buf += payload
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(buf))

    if args.manifest is not None:
        payload = {
            "magic": SCRIPT_MAGIC.decode("ascii"),
            "version": SCRIPT_VERSION,
            "character": str(args.character) if args.character is not None else moves.get("character"),
            "event_kinds": [{"id": v, "name": k} for k, v in sorted(EVENT_IDS.items(), key=lambda kv: kv[1])],
            "unknown_event_counts": unknown_counts,
            "unsupported_event_counts": unsupported_counts,
            "entries": [{"msid": msid, "name": name} for msid, name, _events in entries],
        }
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
