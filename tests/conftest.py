from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))


_COMMON_DATA_FILES = (
    "manifest.json",
    "common/ft_common_data.json",
    "items/lasers.bin",
    "items/item_common.json",
    "items/articles/fox_falco.bin",
    "items/articles/manifest.json",
    "stage_items/yoshi_shyguy.bin",
    "stage_items/dream_whispy.bin",
    "staling/weights.bin",
    "stages/bin/grnla.bin",
    "stages/bin/grnba.bin",
    "stages/bin/griz.bin",
    "stages/bin/grps.bin",
    "stages/bin/grst.bin",
    "stages/bin/grop.bin",
)

_CHAR_DATA_FILES = (
    "characters/{char}.json",
    "special_msids/{char}.json",
    "moves/{char}.json",
    "attack_id/move_id/{char}.bin",
    "staling/move_id/{char}.bin",
    "motion_state/owners/{char}.bin",
    "anims/{char}.bin",
    "anims/{char}.locals.bin",
    "anims/{char}.tracks.bin",
    "anims/{char}.dyn.bin",
    "model_parts/{char}.bin",
    "hurtcaps/{char}.bin",
    "hurtcaps/{char}.json",
    "scripts/{char}.bin",
    "scripts/{char}_manifest.json",
    "hitboxes/{char}.bin",
    "ecb/{char}_bottom.bin",
    "ecb/{char}_extents.bin",
    "shields/{char}.bin",
)


def _required_data_paths() -> list[Path]:
    data = ROOT / "data"
    manifest_path = data / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        chars = manifest["chars"]
        if not isinstance(chars, list) or not chars or not all(isinstance(c, str) for c in chars):
            raise ValueError("manifest chars must be a non-empty string list")
    except (OSError, json.JSONDecodeError, KeyError, ValueError, TypeError) as exc:
        pytest.exit(
            f"generated simulator data is missing or invalid: {manifest_path}: {exc}\n"
            "Run: make bootstrap ISO=/path/to/SSBM.iso",
            returncode=2,
        )

    relative = list(_COMMON_DATA_FILES)
    for char in chars:
        relative.extend(template.format(char=char) for template in _CHAR_DATA_FILES)
    return [data / rel for rel in relative]


def pytest_sessionstart(session) -> None:  # type: ignore[no-untyped-def]
    missing = [path for path in _required_data_paths() if not path.is_file()]
    if not missing:
        return

    preview = "\n".join(f"  - {path.relative_to(ROOT)}" for path in missing[:12])
    extra = "" if len(missing) <= 12 else f"\n  ... and {len(missing) - 12} more"
    pytest.exit(
        f"generated simulator data is incomplete:\n{preview}{extra}\n"
        "Run: make bootstrap ISO=/path/to/SSBM.iso",
        returncode=2,
    )
