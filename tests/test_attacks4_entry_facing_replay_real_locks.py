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
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _step_one_row(*, binding, row: np.ndarray, num_players: int) -> np.void:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        prev_input_bytes = (
            np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
        )
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        (
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz",
            4927,
            0,
            14,  # Wait
            60,  # AttackS4S
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            2996,
            1,
            42,  # Landing
            60,  # AttackS4S
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            10806,
            0,
            14,  # Wait
            60,  # AttackS4S
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz",
            4475,
            1,
            14,  # Wait
            60,  # AttackS4S
        ),
    ],
)
def test_attacks4_entry_copies_stick_sign_into_facing(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int
) -> None:
    # Replay-real lock: ftCo_AttackS4_CheckInput / ftCo_AttackS4_8008C114 route through
    # decideFighter, which sets `fp->facing_dir = stick_x_sign` before entering AttackS4*.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::{
    #   ftCo_AttackS4_CheckInput,ftCo_AttackS4_8008C114,decideFighter
    # }
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == seed_action
    assert int(row["ref_t1"]["action_id"][0, p]) == ref_action
    assert int(row["seed_t"]["facing"][0, p]) != int(row["ref_t1"]["facing"][0, p])

    binding = pytest.importorskip("msl_binding")
    out = _step_one_row(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["facing"][p]) == int(row["ref_t1"]["facing"][0, p])
