from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class LaserShieldReflectEventDump:
    path: Path
    events: list[dict[str, Any]]


def _bits_to_f32(bits: Any) -> float | None:
    if not isinstance(bits, int):
        return None
    return struct.unpack(">f", struct.pack(">I", bits & 0xFFFFFFFF))[0]


def _flag_bit(value: Any, mask: int) -> int | None:
    if not isinstance(value, int):
        return None
    return 1 if (value & mask) != 0 else 0


def read_laser_shield_reflect_events(path: str | Path) -> LaserShieldReflectEventDump:
    """Read JSONL emitted by the laser shield/reflect Dolphin event probe."""

    p = Path(path)
    events: list[dict[str, Any]] = []
    with p.open("r", encoding="utf-8") as f:
        for lineno, line in enumerate(f, start=1):
            stripped = line.strip()
            if not stripped:
                continue
            try:
                row = json.loads(stripped)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{p}:{lineno}: invalid JSON event: {exc}") from exc
            if not isinstance(row, dict):
                raise ValueError(f"{p}:{lineno}: expected object event")
            if "frame" not in row or "event" not in row:
                raise ValueError(f"{p}:{lineno}: missing required frame/event fields")
            events.append(row)
    return LaserShieldReflectEventDump(path=p, events=events)


def _event_summary(ev: dict[str, Any]) -> dict[str, Any]:
    item = ev.get("item")
    fighter = ev.get("fighter")
    capsule = ev.get("capsule")
    out: dict[str, Any] = {
        "frame": ev.get("frame"),
        "phase": ev.get("phase"),
        "event": ev.get("event"),
        "fn": ev.get("fn"),
        "pc": ev.get("pc"),
        "lr": ev.get("lr"),
        "outcome": ev.get("outcome"),
        "return_value": ev.get("return_value"),
    }
    if isinstance(item, dict):
        xdce_flags = item.get("xDCE_flags")
        xdce_b5 = item.get("xDCE_b5")
        xdce_b4 = item.get("xDCE_b4")
        if xdce_b5 is None:
            xdce_b5 = _flag_bit(xdce_flags, 0x04)
        if xdce_b4 is None:
            xdce_b4 = _flag_bit(xdce_flags, 0x08)
        out.update(
            {
                "kind": item.get("kind"),
                "state": item.get("state"),
                "ground_or_air": item.get("ground_or_air"),
                "owner_port": item.get("owner_port"),
                "xDA8_short": item.get("xDA8_short"),
                "xC64_reflect_port": item.get("xC64_reflect_port"),
                "xC8C_reflect_xDA8_short": item.get("xC8C_reflect_xDA8_short"),
                "xC54": _bits_to_f32(item.get("xC54_bits")),
                "xDCC_flags": item.get("xDCC_flags"),
                "xDCE_flags": xdce_flags,
                "xDCE_b5": xdce_b5,
                "xDCE_b4": xdce_b4,
                "item_common_unk_degrees": item.get("item_common_unk_degrees"),
                "shield_bounce_threshold": item.get("shield_bounce_threshold"),
                "shield_bounce_predicate": item.get("shield_bounce_predicate"),
                "xC34": item.get("xC34_damage_dealt"),
                "xC48": item.get("xC48_clank_damage"),
                "xC4C": item.get("xC4C_reflect_damage"),
                "xC50": item.get("xC50_shield_damage"),
                "xCA8": item.get("xCA8_callback_damage"),
                "xCBC": item.get("xCBC_hitlag_bits"),
                "xCC0": _bits_to_f32(item.get("xCC0_hitlag_min_bits")),
            }
        )
        xC58_bits = item.get("xC58_bits")
        if isinstance(xC58_bits, list) and len(xC58_bits) == 3:
            out["xC58"] = [_bits_to_f32(v) for v in xC58_bits]
    if isinstance(fighter, dict):
        out.update(
            {
                "fighter_port": fighter.get("port"),
                "fighter_2218": fighter.get("state_flags_2218"),
                "fighter_221B": fighter.get("state_flags_221B"),
                "shield_unk1": _bits_to_f32(fighter.get("shield_unk1_bits")),
                "reflect_max_damage": fighter.get("reflect_max_damage"),
            }
        )
    if isinstance(capsule, dict):
        out.update(
            {
                "capsule_group": capsule.get("group"),
                "capsule_damage": _bits_to_f32(capsule.get("damage_bits")),
                "capsule_element": capsule.get("element"),
                "capsule_shield_damage": capsule.get("shield_damage"),
                "capsule_flags41": capsule.get("flags41"),
                "capsule_flags42": capsule.get("flags42"),
                "capsule_coll_distance": _bits_to_f32(capsule.get("coll_distance_bits")),
            }
        )
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Summarize laser shield/reflect event probe JSONL.")
    ap.add_argument("path", type=Path)
    ap.add_argument("--json", action="store_true", help="emit normalized JSON summary")
    args = ap.parse_args()

    dump = read_laser_shield_reflect_events(args.path)
    rows = [_event_summary(ev) for ev in dump.events]
    if args.json:
        print(json.dumps({"path": str(dump.path), "event_count": len(rows), "events": rows}, indent=2))
    else:
        for row in rows:
            print(
                "\t".join(
                    str(row.get(k, ""))
                    for k in (
                        "frame",
                        "phase",
                        "event",
                        "fn",
                        "kind",
                        "state",
                        "ground_or_air",
                        "owner_port",
                        "fighter_port",
                        "xDA8_short",
                        "xC64_reflect_port",
                        "xDCE_flags",
                        "shield_bounce_predicate",
                        "outcome",
                    )
                )
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
