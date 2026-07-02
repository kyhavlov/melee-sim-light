from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    assert int(row.shape[0]) == 1

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = (
            np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, seed_stride)
            .copy()
        )
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = (
            np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, input_stride)
            .copy()
        )
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)

        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        seed = row["seed_t"][0]
        ref = row["ref_t1"][0]
        return seed, out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("label", "dataset_rel", "record", "p"),
    [
        (
            "tbk_target",
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz",
            330,
            1,
        ),
        (
            "tbk_control",
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz",
            329,
            1,
        ),
        (
            "gat_target",
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            1370,
            0,
        ),
        (
            "gat_control",
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            1369,
            0,
        ),
    ],
)
def test_damageflyroll_kb_decay_replay_real_locks(
    label: str, dataset_rel: str, record: int, p: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record)

    fields = (
        "action_id",
        "action_frame",
        "animation_index",
        "instance_id",
        "hitlag",
        "hitstun",
    )
    for field in fields:
        got = out[field][p].item()
        want = ref[field][p].item()
        assert got == want, (
            f"{label} replay-real lock drift: {dataset_rel} rec={record} p={p} "
            f"field={field} out={got} ref={want} seed={seed[field][p]}"
        )
