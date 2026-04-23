from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class ThrowLaserEventDump:
    path: Path
    events: list[dict[str, Any]]


def read_throw_laser_events(path: str | Path) -> ThrowLaserEventDump:
    """Read JSONL emitted by the throw-laser event Dolphin probe.

    The probe is intentionally separate from the binary engine dump. It hooks transient
    item-spawn/body/damage/delete functions that can create and delete a laser before Slippi
    post-frame item serialization, so rows are line-delimited JSON events.
    """

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
    return ThrowLaserEventDump(path=p, events=events)


def _event_summary(ev: dict[str, Any]) -> dict[str, Any]:
    item = ev.get("item")
    hitbox = ev.get("hitbox")
    out: dict[str, Any] = {
        "frame": ev.get("frame"),
        "phase": ev.get("phase"),
        "event": ev.get("event"),
        "fn": ev.get("fn"),
    }
    if isinstance(item, dict):
        out.update(
            {
                "kind": item.get("kind"),
                "state": item.get("state"),
                "owner_port": item.get("owner_port"),
                "xDA8_short": item.get("xDA8_short"),
                "xC34": item.get("xC34_damage_dealt"),
                "xCA8": item.get("xCA8_callback_damage"),
                "xCBC": item.get("xCBC_hitlag_bits"),
                "xCC0": item.get("xCC0_hitlag_min_bits"),
                "xDC8": item.get("xDC8_word"),
                "lifetime": item.get("lifetime_bits"),
            }
        )
    if isinstance(hitbox, dict):
        out.update(
            {
                "hitbox_index": hitbox.get("index"),
                "hitbox_group": hitbox.get("group"),
                "hitbox_state": hitbox.get("state"),
                "victims1_active": hitbox.get("victims1_active_slots"),
                "victims1_cd": hitbox.get("victims1_cooldown"),
            }
        )
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Summarize throw-laser event probe JSONL.")
    ap.add_argument("path", type=Path)
    ap.add_argument("--json", action="store_true", help="emit normalized JSON summary")
    args = ap.parse_args()

    dump = read_throw_laser_events(args.path)
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
                        "owner_port",
                        "xDA8_short",
                        "hitbox_index",
                        "hitbox_group",
                        "victims1_active",
                    )
                )
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
