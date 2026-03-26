from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


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
def test_item_hit_damage_entry_faces_away_when_owner_left_of_victim() -> None:
    # Replay-real lock: item-hit damage entry should copy the BODY-style facing_dir_1 sign so the
    # victim turns away from the owner when the owner is to the victim's left.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[923:924]
    p = 0

    assert int(row["seed_t"]["action_id"][0, p]) == 14  # Dash
    assert int(row["ref_t1"]["action_id"][0, p]) == 75  # DamageHi1
    assert float(row["seed_t"]["pos_x"][0, p]) > float(row["seed_t"]["pos_x"][0, 1])
    assert int(row["seed_t"]["facing"][0, p]) == 1
    assert int(row["ref_t1"]["facing"][0, p]) == 0

    binding = pytest.importorskip("msl_binding")
    out = _step_one_row(binding=binding, row=row, num_players=int(ds.header["num_players"]))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["facing"][p]) == int(row["ref_t1"]["facing"][0, p])


@pytest.mark.integration
def test_item_hit_damage_entry_faces_away_when_owner_right_of_victim() -> None:
    # Replay-real lock: the same item-hit damage entry should face right when the owner is to the
    # victim's right at collision time.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[3386:3387]
    p = 0

    assert int(row["seed_t"]["action_id"][0, p]) == 67  # AttackAirB
    assert int(row["ref_t1"]["action_id"][0, p]) == 84  # DamageAir1
    assert float(row["seed_t"]["pos_x"][0, p]) < float(row["seed_t"]["pos_x"][0, 1])
    assert int(row["seed_t"]["facing"][0, p]) == 0
    assert int(row["ref_t1"]["facing"][0, p]) == 1

    binding = pytest.importorskip("msl_binding")
    out = _step_one_row(binding=binding, row=row, num_players=int(ds.header["num_players"]))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["facing"][p]) == int(row["ref_t1"]["facing"][0, p])
