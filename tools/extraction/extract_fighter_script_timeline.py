from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from tools.slippi.known_data_artifacts import SCRIPT_MAGIC, SCRIPT_VERSION


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


def _canonical_payload(data: dict) -> bytes:
    return json.dumps(data, sort_keys=True, separators=(",", ":")).encode("utf-8")


def _iter_entries(moves: dict) -> list[tuple[int, str, list[dict]]]:
    entries: list[tuple[int, str, list[dict]]] = []
    for name, rec in sorted(moves.get("moves", {}).items(), key=lambda kv: int(kv[1].get("submotion_id", 0))):
        entries.append((int(rec["submotion_id"]), str(name), list(rec.get("events", []))))
    for name, rec in sorted(moves.get("specials_by_msid", {}).items(), key=lambda kv: int(kv[0])):
        entries.append((int(rec["submotion_id"]), f"special_{name}", list(rec.get("events", []))))
    return entries


def main() -> None:
    ap = argparse.ArgumentParser(description="Pack decoded stable fighter script events as MSLFTSC1.")
    ap.add_argument("--moves", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--manifest", type=Path, default=None)
    args = ap.parse_args()

    moves = json.loads(args.moves.read_text(encoding="utf-8"))
    entries = _iter_entries(moves)

    index_rows: list[tuple[int, int, int]] = []
    event_payloads: list[bytes] = []
    event_count = 0
    unknown_counts: dict[str, int] = {}
    for msid, _name, events in entries:
        start = event_count
        for ev in events:
            kind = str(ev.get("kind", ""))
            kind_id = EVENT_IDS.get(kind)
            if kind_id is None:
                unknown_counts[kind] = unknown_counts.get(kind, 0) + 1
                continue
            payload = _canonical_payload(dict(ev.get("data", {})))
            event_payloads.append(struct.pack("<HHI", int(ev.get("frame", 0)) & 0xFFFF, kind_id, len(payload)) + payload)
            event_count += 1
        index_rows.append((msid & 0xFFFF, start, event_count - start))

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
            "character": moves.get("character"),
            "event_kinds": [{"id": v, "name": k} for k, v in sorted(EVENT_IDS.items(), key=lambda kv: kv[1])],
            "unknown_event_counts": unknown_counts,
            "entries": [{"msid": msid, "name": name} for msid, name, _events in entries],
        }
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
