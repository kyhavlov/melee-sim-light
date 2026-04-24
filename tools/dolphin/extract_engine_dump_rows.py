from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from tools.dolphin.engine_dump_io import f32_from_bits, i8_from_u8, read_engine_dump


BUTTON_LR = 1 << 31


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


@dataclass(frozen=True)
class Window:
    start: int
    end: int

    def contains(self, frame_index: int) -> bool:
        return int(self.start) <= int(frame_index) <= int(self.end)


def _frame_hitboxes(d: object, frame_slot: int, port_count: int, port: int) -> list[dict[str, object]]:
    base = (frame_slot * port_count + (port - 1)) * 4
    rows: list[dict[str, object]] = []
    for hitbox_id in range(4):
        hb = d.hitboxes[base + hitbox_id]
        rows.append(
            {
                "hitbox_id": hitbox_id,
                "state": int(hb["state"]),
                "group": int(hb["group"]),
                "damage": int(hb["damage"]),
                "damage_stale": f32_from_bits(int(hb["damage_stale_bits"])),
                "offset": [
                    f32_from_bits(int(hb["offset_x_bits"])),
                    f32_from_bits(int(hb["offset_y_bits"])),
                    f32_from_bits(int(hb["offset_z_bits"])),
                ],
                "size": f32_from_bits(int(hb["size_bits"])),
                "angle": int(hb["angle"]),
                "kbg": int(hb["kbg"]),
                "wsk": int(hb["wsk"]),
                "bkb": int(hb["bkb"]),
                "element": int(hb["element"]),
                "shield_damage": int(hb["shield_damage"]),
                "sfx": int(hb["sfx"]),
                "sfx_kind": int(hb["sfx_kind"]),
                "flags": [int(v) for v in hb["flags"].tolist()],
                "bone_ptr": int(hb["bone_ptr"]),
                "pos": [
                    f32_from_bits(int(hb["pos_x_bits"])),
                    f32_from_bits(int(hb["pos_y_bits"])),
                    f32_from_bits(int(hb["pos_z_bits"])),
                ],
            }
        )
    return rows


def _frame_hurtboxes(d: object, frame_slot: int, port_count: int, port: int) -> list[dict[str, object]]:
    base = (frame_slot * port_count + (port - 1)) * 15
    rows: list[dict[str, object]] = []
    for hurtcap_id in range(15):
        hb = d.hurtboxes[base + hurtcap_id]
        rows.append(
            {
                "hurtcap_id": hurtcap_id,
                "state": int(hb["state"]),
                "a_offset": [
                    f32_from_bits(int(hb["a_offset_x_bits"])),
                    f32_from_bits(int(hb["a_offset_y_bits"])),
                    f32_from_bits(int(hb["a_offset_z_bits"])),
                ],
                "b_offset": [
                    f32_from_bits(int(hb["b_offset_x_bits"])),
                    f32_from_bits(int(hb["b_offset_y_bits"])),
                    f32_from_bits(int(hb["b_offset_z_bits"])),
                ],
                "scale": f32_from_bits(int(hb["scale_bits"])),
                "a_pos": [
                    f32_from_bits(int(hb["a_pos_x_bits"])),
                    f32_from_bits(int(hb["a_pos_y_bits"])),
                    f32_from_bits(int(hb["a_pos_z_bits"])),
                ],
                "b_pos": [
                    f32_from_bits(int(hb["b_pos_x_bits"])),
                    f32_from_bits(int(hb["b_pos_y_bits"])),
                    f32_from_bits(int(hb["b_pos_z_bits"])),
                ],
                "bone_idx": int(hb["bone_idx"]),
                "height": int(hb["height"]),
                "is_grabbable": int(hb["is_grabbable"]),
                "flags": int(hb["flags"]),
            }
        )
    return rows


def _collect_rows(dump_path: str | Path, window: Window, ports: list[int]) -> dict[str, object]:
    d = read_engine_dump(dump_path)
    frame_count = int(d.header["frame_count"])
    port_count = int(d.header["port_count"])
    stage_id = int(d.header["stage_id"])
    is_teams = int(d.header["is_teams"])
    rows: list[dict[str, object]] = []

    if not ports:
        ports = list(range(1, port_count + 1))

    valid_ports = {p for p in ports if 1 <= p <= port_count}
    if len(valid_ports) != len(ports):
        raise ValueError(f"requested ports={ports} outside port_count={port_count}")
    ports = sorted(valid_ports)

    for frame_slot in range(frame_count):
        fr = d.frames[frame_slot]
        frame_index = int(fr["frame_index"])
        if not window.contains(frame_index):
            continue

        items_start = int(fr["item_offset"])
        items_end = items_start + int(fr["item_count"])
        frame_items = []
        for ii in range(items_start, items_end):
            it = d.items[ii]
            item_row = {
                "item_index": ii,
                "item_id": int(it["item_id"]),
                "kind": int(it["kind"]),
                "state": int(it["state"]),
                "owner_port": int(it["owner_port_i8"]),
                "anim_id": int(it["anim_id"]),
                "damage": int(it["damage"]),
                "pos_x": f32_from_bits(int(it["pos_x_bits"])),
                "pos_y": f32_from_bits(int(it["pos_y_bits"])),
                "vel_x": f32_from_bits(int(it["vel_x_bits"])),
                "vel_y": f32_from_bits(int(it["vel_y_bits"])),
            }
            if "xC34_damage_dealt" in it.dtype.names:
                item_row.update(
                    {
                        "xC34_damage_dealt": int(it["xC34_damage_dealt"]),
                        "xC48_clank_damage": int(it["xC48_clank_damage"]),
                        "xC4C_reflect_damage": int(it["xC4C_reflect_damage"]),
                        "xC50_shield_damage": int(it["xC50_shield_damage"]),
                        "xCA8_callback_damage": int(it["xCA8_callback_damage"]),
                        "xCBC_hitlag": f32_from_bits(int(it["xCBC_hitlag_bits"])),
                        "xCC0_hitlag_min": f32_from_bits(int(it["xCC0_hitlag_min_bits"])),
                        "xDA8_short": int(it["xDA8_short"]),
                        "xDC8_word": int(it["xDC8_word"]),
                        "xDCE_flags": int(it["xDCE_flags"]),
                        "laser_scale": f32_from_bits(int(it["xDD4_laser_scale_bits"])),
                        "laser_angle": f32_from_bits(int(it["xDD8_laser_angle_bits"])),
                        "laser_speed": f32_from_bits(int(it["xDDC_laser_speed_bits"])),
                        "laser_pos": [
                            f32_from_bits(int(it["xDE0_laser_pos_x_bits"])),
                            f32_from_bits(int(it["xDE4_laser_pos_y_bits"])),
                            f32_from_bits(int(it["xDE8_laser_pos_z_bits"])),
                        ],
                    }
                )
            if d.item_hitlists.size != 0:
                item_hitlists = []
                hitlist_base = ii * 4
                for hb_id in range(4):
                    hl = d.item_hitlists[hitlist_base + hb_id]
                    v1_ptr = [int(x) for x in hl["victims1_ptr"].tolist()]
                    v1_cd = [int(x) for x in hl["victims1_cooldown"].tolist()]
                    v2_ptr = [int(x) for x in hl["victims2_ptr"].tolist()]
                    v2_cd = [int(x) for x in hl["victims2_cooldown"].tolist()]
                    item_hitlists.append(
                        {
                            "hitbox_id": hb_id,
                            "group": int(hl["group"]),
                            "victims1_cursor": int(hl["victims1_cursor"]),
                            "victims2_cursor": int(hl["victims2_cursor"]),
                            "owner_gobj": int(hl["owner_gobj"]),
                            "victims1_ptr": v1_ptr,
                            "victims1_cooldown": v1_cd,
                            "victims2_ptr": v2_ptr,
                            "victims2_cooldown": v2_cd,
                            "victims1_active_slots": [i for i, ptr in enumerate(v1_ptr) if ptr != 0],
                            "victims2_active_slots": [i for i, ptr in enumerate(v2_ptr) if ptr != 0],
                        }
                    )
                item_row["hitlist_provenance"] = item_hitlists
            frame_items.append(item_row)

        for port in ports:
            idx = frame_slot * port_count + (port - 1)
            inp = d.inputs[idx]
            fighter = d.fighters[idx]
            buttons = int(inp["buttons"])
            hitlist_rows: list[dict[str, object]] = []
            if d.hitlists.size != 0:
                hitlist_base = idx * 4
                for hb_id in range(4):
                    hl = d.hitlists[hitlist_base + hb_id]
                    v1_ptr = [int(x) for x in hl["victims1_ptr"].tolist()]
                    v1_cd = [int(x) for x in hl["victims1_cooldown"].tolist()]
                    v2_ptr = [int(x) for x in hl["victims2_ptr"].tolist()]
                    v2_cd = [int(x) for x in hl["victims2_cooldown"].tolist()]
                    hitlist_rows.append(
                        {
                            "hitbox_id": hb_id,
                            "group": int(hl["group"]),
                            "victims1_cursor": int(hl["victims1_cursor"]),
                            "victims2_cursor": int(hl["victims2_cursor"]),
                            "owner_gobj": int(hl["owner_gobj"]),
                            "victims1_ptr": v1_ptr,
                            "victims1_cooldown": v1_cd,
                            "victims2_ptr": v2_ptr,
                            "victims2_cooldown": v2_cd,
                            "victims1_active_slots": [i for i, ptr in enumerate(v1_ptr) if ptr != 0],
                            "victims2_active_slots": [i for i, ptr in enumerate(v2_ptr) if ptr != 0],
                        }
                    )
            rows.append(
                {
                    "frame_index": frame_index,
                    "port": int(port),
                    "stage_id": stage_id,
                    "is_teams": is_teams,
                    "action_id": int(fighter["action_state"]),
                    "animation_id": int(fighter["anim_id"]),
                    "action_frame_f32": f32_from_bits(int(fighter["action_frame_bits"])),
                    "anim_frame_f32": f32_from_bits(int(fighter["anim_frame_bits"])),
                    "pos_x": f32_from_bits(int(fighter["pos_x_bits"])),
                    "pos_y": f32_from_bits(int(fighter["pos_y_bits"])),
                    "self_vel_x": f32_from_bits(int(fighter["self_vel_x_bits"])),
                    "self_vel_y": f32_from_bits(int(fighter["self_vel_y_bits"])),
                    "gr_vel": f32_from_bits(int(fighter["gr_vel_bits"])),
                    "shield_hp": f32_from_bits(int(fighter["shield_health_bits"])),
                    "ecb": {
                        "top": [
                            f32_from_bits(int(fighter["ecb_top_x_bits"])),
                            f32_from_bits(int(fighter["ecb_top_y_bits"])),
                        ],
                        "bottom": [
                            f32_from_bits(int(fighter["ecb_bottom_x_bits"])),
                            f32_from_bits(int(fighter["ecb_bottom_y_bits"])),
                        ],
                        "left": [
                            f32_from_bits(int(fighter["ecb_left_x_bits"])),
                            f32_from_bits(int(fighter["ecb_left_y_bits"])),
                        ],
                        "right": [
                            f32_from_bits(int(fighter["ecb_right_x_bits"])),
                            f32_from_bits(int(fighter["ecb_right_y_bits"])),
                        ],
                    },
                    "floor_normal": [
                        f32_from_bits(int(fighter["floor_normal_x_bits"])),
                        f32_from_bits(int(fighter["floor_normal_y_bits"])),
                    ],
                    "ground_accel": [
                        f32_from_bits(int(fighter["ground_accel_1_bits"])),
                        f32_from_bits(int(fighter["ground_accel_2_bits"])),
                    ],
                    "anim_vel": [
                        f32_from_bits(int(fighter["anim_vel_x_bits"])),
                        f32_from_bits(int(fighter["anim_vel_y_bits"])),
                    ],
                    "hitlag_left_f32": f32_from_bits(int(fighter["hitlag_left_bits"])),
                    "misc_as_bits": int(fighter["misc_as_bits"]),
                    "ground_or_air": int(fighter["ground_or_air"]),
                    "state_flags": [
                        int(fighter["state_flags_2218"]),
                        int(fighter["state_flags_221a"]),
                        int(fighter["state_flags_221b"]),
                        int(fighter["state_flags_221c"]),
                        int(fighter["state_flags_221f"]),
                    ],
                    "input": {
                        "buttons": buttons,
                        "held_lr_proxy": int((buttons & BUTTON_LR) != 0),
                        "stick_x": f32_from_bits(int(inp["stick_x_bits"])),
                        "stick_y": f32_from_bits(int(inp["stick_y_bits"])),
                        "cstick_x": f32_from_bits(int(inp["cstick_x_bits"])),
                        "cstick_y": f32_from_bits(int(inp["cstick_y_bits"])),
                        "l_shoulder": f32_from_bits(int(inp["l_shoulder_bits"])),
                        "r_shoulder": f32_from_bits(int(inp["r_shoulder_bits"])),
                        "raw_stick_x": i8_from_u8(int(inp["raw_stick_x_u8"])),
                        "raw_stick_y": i8_from_u8(int(inp["raw_stick_y_u8"])),
                        "raw_cstick_x": i8_from_u8(int(inp["raw_cstick_x_u8"])),
                        "raw_cstick_y": i8_from_u8(int(inp["raw_cstick_y_u8"])),
                    },
                    "hitlist_provenance": hitlist_rows,
                    "items": frame_items,
                }
            )
            row = rows[-1]
            row["hitboxes"] = _frame_hitboxes(d, frame_slot, port_count, int(port))
            row["hurtboxes"] = _frame_hurtboxes(d, frame_slot, port_count, int(port))
            if "ecb_lock_timer" in fighter.dtype.names:
                if "lightshield_amount_bits" in fighter.dtype.names:
                    row["lightshield_amount"] = f32_from_bits(int(fighter["lightshield_amount_bits"]))
                row["ecb_lock_timer"] = int(fighter["ecb_lock_timer"])
                row["coll_x130_flags"] = int(fighter["coll_x130_flags"])
                row["coll_x130_locked"] = 1 if (int(fighter["coll_x130_flags"]) & (1 << 4)) != 0 else 0
                row["shield_damage_taken"] = int(fighter["shield_damage_taken"])
                row["shield_int_damage"] = int(fighter["shield_int_damage"])
                row["shield_attacker_gobj"] = int(fighter["shield_attacker_gobj"])
                row["specialn_facing_dir"] = f32_from_bits(int(fighter["specialn_facing_dir_bits"]))
                row["shield_hit_element"] = int(fighter["shield_hit_element"])

    rows.sort(key=lambda r: (int(r["frame_index"]), int(r["port"])))
    return {
        "dump_path": str(Path(dump_path).resolve()),
        "frame_window": {"start": int(window.start), "end": int(window.end)},
        "frame_count": frame_count,
        "port_count": port_count,
        "rows": rows,
        "row_count": len(rows),
    }


def _summary_text(payload: dict[str, object]) -> str:
    rows = payload["rows"]
    lines = []
    lines.append("# engine_dump_rows")
    lines.append(
        f"dump={payload['dump_path']} frame_window={payload['frame_window']['start']}..{payload['frame_window']['end']} rows={payload['row_count']}"
    )
    lines.append("")
    for r in rows:
        hitlist_summary = "hitlist=v6"
        if r["hitlist_provenance"]:
            compact = []
            for hb in r["hitlist_provenance"]:
                v1n = len(hb["victims1_active_slots"])
                v2n = len(hb["victims2_active_slots"])
                compact.append(
                    f"hb{hb['hitbox_id']}[g={hb['group']} c={hb['victims1_cursor']}/{hb['victims2_cursor']} "
                    f"a={v1n}/{v2n}]"
                )
            hitlist_summary = " ".join(compact)
        lines.append(
            f"frame={r['frame_index']} p={r['port']} action={r['action_id']} anim={r['animation_id']} "
            f"action_f32={r['action_frame_f32']:.3f} hitlag={r['hitlag_left_f32']:.3f} "
            f"shield={r['shield_hp']:.3f} gr_vel={r['gr_vel']:.3f} flags={r['state_flags']} "
            f"buttons=0x{int(r['input']['buttons']):08x} "
            f"{hitlist_summary}"
        )
        if "ecb_lock_timer" in r:
            ecb = r["ecb"]
            lines[-1] += (
                f" ecb_b=({ecb['bottom'][0]:.3f},{ecb['bottom'][1]:.3f})"
                f" ecb_t=({ecb['top'][0]:.3f},{ecb['top'][1]:.3f})"
                f" floor_n=({r['floor_normal'][0]:.3f},{r['floor_normal'][1]:.3f})"
                f" light={r.get('lightshield_amount', 0.0):.3f}"
                f" ecb_lock={r['ecb_lock_timer']} x130=0x{int(r['coll_x130_flags']):08x}"
                f" locked={r['coll_x130_locked']} x19a0={r['shield_damage_taken']}"
                f" x19a4={r['shield_int_damage']} x19a8=0x{int(r['shield_attacker_gobj']):08x}"
                f" x19ac={r['specialn_facing_dir']:.3f} x19b0={r['shield_hit_element']}"
            )
        if r["items"]:
            item_bits = []
            for it in r["items"]:
                desc = (
                    f"it{it['item_index']} kind={it['kind']} st={it['state']} own={it['owner_port']} "
                    f"xDA8={it.get('xDA8_short', 0)} dmg={it.get('xC34_damage_dealt', 0)} "
                    f"xCA8={it.get('xCA8_callback_damage', 0)}"
                )
                if "hitlist_provenance" in it:
                    compact = []
                    for hb in it["hitlist_provenance"]:
                        v1n = len(hb["victims1_active_slots"])
                        v2n = len(hb["victims2_active_slots"])
                        if v1n or v2n or hb["victims1_cursor"] or hb["victims2_cursor"]:
                            compact.append(
                                f"hb{hb['hitbox_id']}[g={hb['group']} c={hb['victims1_cursor']}/"
                                f"{hb['victims2_cursor']} a={v1n}/{v2n}]"
                            )
                    if compact:
                        desc += " " + " ".join(compact)
                item_bits.append(desc)
            lines.append("  items: " + " | ".join(item_bits))
    return "\n".join(lines).rstrip() + "\n"


def extract_to_dir(
    *,
    dump_path: str | Path,
    start_frame: int,
    end_frame: int,
    ports: list[int],
    out_dir: str | Path,
) -> tuple[Path, Path]:
    payload = _collect_rows(dump_path, Window(start_frame, end_frame), ports)
    dst = Path(out_dir).resolve()
    dst.mkdir(parents=True, exist_ok=True)
    json_path = dst / "engine_dump_rows.json"
    txt_path = dst / "engine_dump_rows.txt"
    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    txt_path.write_text(_summary_text(payload), encoding="utf-8")
    return json_path, txt_path


def main() -> int:
    ap = argparse.ArgumentParser(description="Extract deterministic frame rows from an engine-dump binary.")
    ap.add_argument("--dump", required=True, type=Path, help="path to engine dump .bin")
    ap.add_argument("--start-frame", required=True, type=int)
    ap.add_argument("--end-frame", required=True, type=int)
    ap.add_argument(
        "--ports",
        type=str,
        default="1,2",
        help="comma-separated port list (1-based), e.g. '1' or '1,2'",
    )
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path("reports/triage") / f"{_timestamp()}_engine_dump_rows",
    )
    args = ap.parse_args()

    if args.end_frame < args.start_frame:
        raise SystemExit(f"--end-frame ({args.end_frame}) must be >= --start-frame ({args.start_frame})")
    ports = [int(tok.strip()) for tok in str(args.ports).split(",") if tok.strip()]
    json_path, txt_path = extract_to_dir(
        dump_path=args.dump,
        start_frame=int(args.start_frame),
        end_frame=int(args.end_frame),
        ports=ports,
        out_dir=args.out_dir,
    )
    print(f"wrote {json_path}")
    print(f"wrote {txt_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
