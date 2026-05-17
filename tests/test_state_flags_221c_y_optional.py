from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def _skip_if_required_artifacts_missing(root: Path) -> None:
    # Integration policy: skip if required local artifacts are missing.
    required = [
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/special_msids/fox.json",
        "data/special_msids/falco.json",
        "data/moves/fox.json",
        "data/moves/falco.json",
        "data/scripts/fox.bin",
        "data/scripts/falco.bin",
        "data/attack_id/move_id/fox.bin",
        "data/attack_id/move_id/falco.bin",
        "data/anims/fox.bin",
        "data/anims/falco.bin",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/hurtcaps/fox.bin",
        "data/hurtcaps/falco.bin",
        "data/hitboxes/fox.bin",
        "data/hitboxes/falco.bin",
        "data/ecb/fox_bottom.bin",
        "data/ecb/falco_bottom.bin",
        "data/ecb/fox_extents.bin",
        "data/ecb/falco_extents.bin",
        "data/items/lasers.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _populate_data_overlay_without_legacy_script_owner_splits(dst_data_dir: Path) -> None:
    src_data_dir = ROOT / "data"
    dst_data_dir.mkdir(parents=True, exist_ok=True)
    exclude_roots = {"airborne_state_events", "hit_status", "hurtbox_states", "state_flags_221c_y"}

    for src in src_data_dir.rglob("*"):
        rel = src.relative_to(src_data_dir)
        if rel.parts and rel.parts[0] in exclude_roots:
            continue
        dst = dst_data_dir / rel
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.link(src, dst)
        except OSError:
            dst.write_bytes(src.read_bytes())


@pytest.mark.integration
def test_init_succeeds_without_legacy_script_owner_split_tables() -> None:
    pytest.importorskip("msl_binding")
    _skip_if_required_artifacts_missing(ROOT)

    build_dir = ROOT / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=build_dir) as td:
        data_dir = Path(td) / "data"
        # Runtime script-owner products are cached from MSLFTSC1; stale split artifacts must not
        # remain implicit init requirements.
        _populate_data_overlay_without_legacy_script_owner_splits(data_dir)

        env = os.environ.copy()
        env["MSL_DATA_DIR"] = str(data_dir)
        code = (
            "import msl_binding\n"
            "h=msl_binding.init(batch_size=1,num_players=2)\n"
            "assert h is not None\n"
            "msl_binding.destroy(h)\n"
        )
        proc = subprocess.run(
            [sys.executable, "-c", code],
            cwd=str(ROOT),
            env=env,
            capture_output=True,
            text=True,
        )
        assert proc.returncode == 0, f"stdout={proc.stdout}\nstderr={proc.stderr}"
