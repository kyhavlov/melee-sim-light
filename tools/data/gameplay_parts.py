from __future__ import annotations

import json
import os
import struct
from pathlib import Path


SPEC = Path(__file__).with_name("gameplay_parts.json")
MAGIC = b"MSLPART1"


def emit_gameplay_parts(data_root: Path) -> None:
    """Emit the audited headless pose closure consumed by the native runtime."""

    payload = json.loads(SPEC.read_text(encoding="utf-8"))
    if payload.get("format") != MAGIC.decode("ascii") or payload.get("version") != 1:
        raise ValueError(f"unsupported gameplay-part specification: {SPEC}")
    fighters = payload.get("fighters")
    if not isinstance(fighters, dict) or not fighters:
        raise ValueError(f"empty gameplay-part specification: {SPEC}")

    destination = data_root / "model_parts"
    destination.mkdir(parents=True, exist_ok=True)
    wanted: set[str] = set()
    for name, fighter in sorted(fighters.items()):
        if not isinstance(name, str) or not isinstance(fighter, dict):
            raise ValueError(f"malformed gameplay-part fighter: {name!r}")
        external_id = fighter.get("external_id")
        parts = fighter.get("live_parts")
        if (
            not isinstance(external_id, int)
            or not isinstance(parts, list)
            or not parts
            or any(not isinstance(part, int) or not 0 <= part < 256 for part in parts)
            or parts != sorted(set(parts))
        ):
            raise ValueError(f"malformed gameplay-part closure: {name}")

        output = bytearray(MAGIC)
        output += struct.pack("<IHHHH", 1, external_id, len(parts), 0, 0)
        for part in parts:
            # Native runtime consumes the part id. Ancestor closure is already
            # represented by the audited list; the remaining row fields are
            # reserved for offline inspection.
            output += struct.pack("<HhII", part, -1, 0, 0)
        path = destination / f"{name}.bin"
        temporary = path.with_name(f".{path.name}.tmp")
        temporary.write_bytes(output)
        os.replace(temporary, path)
        wanted.add(path.name)

    for path in destination.glob("*.bin"):
        if path.name not in wanted:
            path.unlink()
