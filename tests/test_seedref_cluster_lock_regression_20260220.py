from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


_REQUIRED_ARTIFACTS = (
    "data/stages/final_destination.json",
    "data/common/ft_common_data.json",
    "data/characters/fox.json",
    "data/characters/falco.json",
    "data/anims/fox.tracks.bin",
    "data/anims/falco.tracks.bin",
    "data/moves/fox.json",
    "data/moves/falco.json",
)


def _skip_if_required_artifacts_missing(root: Path) -> None:
    missing = [rel for rel in _REQUIRED_ARTIFACTS if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local extracted artifacts: {', '.join(missing)}")


def _run_record(dataset_path: Path, record: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).copy().reshape(1, input_stride)
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        seed = row["seed_t"].reshape(-1)[0]
        ref = row["ref_t1"].reshape(-1)[0]
        return out, seed, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_seedref_cluster_rows_match_ref_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    rows = (
        # GuardReflect entry ordering cluster (combat shield envelope gate).
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_80092450}
        (
            "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            1370,
            0,
            "action_id",
            84,
        ),
        (
            "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            2272,
            0,
            "hitlag",
            0,
        ),
        # Ledge/collision ownership cluster (mpColl ledge-grab mask + cliff scheduling).
        # refs/melee/src/melee/mp/mpcoll.c::mpColl_80047E14
        # refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298
        (
            "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            4388,
            1,
            "instance_id",
            914,
        ),
        (
            "replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
            330,
            1,
            "action_id",
            79,
        ),
    )

    for dataset_rel, record, p, field, expect in rows:
        dataset_path = root / dataset_rel
        if not dataset_path.exists():
            pytest.skip(f"missing local replay: {dataset_rel}")

        out, seed, ref = _run_record(dataset_path, record)

        assert int(seed[field][p]) == expect
        assert int(ref[field][p]) == expect
        assert int(out[field][0, p]) == expect
