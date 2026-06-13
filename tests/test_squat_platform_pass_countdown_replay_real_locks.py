from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_SQUAT = 39
ACT_PASS = 244


def _size_key(sizes: dict[str, int], key: str) -> int:
    if key in sizes:
        return int(sizes[key])
    return int(sizes[f"{key}_v0"])


def _run_one_step(dataset_path: Path, record: int, *, row_mutator=None) -> np.void:
    msl_binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    sizes = msl_binding.sizes()
    seed_stride = _size_key(sizes, "seed")
    input_stride = _size_key(sizes, "input")
    compare_stride = _size_key(sizes, "compare")
    assert compare_stride == COMPARE_DTYPE.itemsize

    row = ds.samples[record : record + 1].copy()
    if row_mutator is not None:
        row_mutator(row)

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = msl_binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        msl_binding.reseed_seed(handle, row["seed_t"].view(np.uint8).reshape(1, seed_stride).copy())
        msl_binding.step_input(
            handle,
            row["prev_input_t"].view(np.uint8).reshape(1, input_stride).copy(),
            row["input_t"].view(np.uint8).reshape(1, input_stride).copy(),
        )
        msl_binding.write_compare(handle, out_bytes)
    finally:
        msl_binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()


def _row(dataset_path: Path, record: int) -> np.void:
    ds = read_dataset(str(dataset_path))
    return ds.samples[record]


def _run_rollout_window(
    dataset_path: Path,
    *,
    start_record: int,
    target_record: int,
    row_mutator=None,
) -> np.void:
    msl_binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    sizes = msl_binding.sizes()
    seed_stride = _size_key(sizes, "seed")
    input_stride = _size_key(sizes, "input")
    compare_stride = _size_key(sizes, "compare")
    assert compare_stride == COMPARE_DTYPE.itemsize

    rows = ds.samples[start_record : target_record + 1].copy()
    if row_mutator is not None:
        row_mutator(rows)

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = msl_binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        msl_binding.reseed_seed_rollout(
            handle, rows["seed_t"][0:1].view(np.uint8).reshape(1, seed_stride).copy()
        )
        for i in range(rows.shape[0]):
            msl_binding.step_input(
                handle,
                rows["prev_input_t"][i : i + 1].view(np.uint8).reshape(1, input_stride).copy(),
                rows["input_t"][i : i + 1].view(np.uint8).reshape(1, input_stride).copy(),
            )
            msl_binding.write_compare(handle, out_bytes)
    finally:
        msl_binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()


@pytest.mark.integration
def test_squat_platform_pass_countdown_consumes_after_stick_release_marth_lock() -> None:
    # Squat platform-pass hidden countdown:
    # ftCo_80099F9C arms mv.co.squat.x0/x4 while down is held, then ftCo_Squat_IASA_inline
    # decrements x4 and enters Pass without rechecking lstick.y. This one-step row exercises the
    # source arming path itself; longer released-stick consume windows are covered by rollout locks
    # where runtime has carried the hidden latch from earlier Squat frames.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_80099F9C
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA_inline
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/marth/replays/validation/marth/VictoriousSpitefulAlpaca.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 3269
    p = 1
    row = _row(dataset_path, record)
    assert int(row["seed_t"]["action_id"][p]) == ACT_SQUAT
    assert int(row["seed_t"]["action_frame"][p]) == 3
    assert int(row["prev_input_t"]["p"]["main_y"][p]) < -60
    assert int(row["input_t"]["p"]["main_y"][p]) > -60
    assert int(row["ref_t1"]["action_id"][p]) == ACT_PASS

    out = _run_one_step(dataset_path, record)
    assert int(out["action_id"][p]) == ACT_PASS
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][p])
    assert int(out["on_ground"][p]) == int(row["ref_t1"]["on_ground"][p]) == 0


@pytest.mark.integration
def test_squat_platform_pass_countdown_does_not_consume_one_frame_early_marth_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/marth/replays/validation/marth/VictoriousSpitefulAlpaca.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 3268
    p = 1
    row = _row(dataset_path, record)
    assert int(row["seed_t"]["action_id"][p]) == ACT_SQUAT
    assert int(row["seed_t"]["action_frame"][p]) == 2
    assert int(row["ref_t1"]["action_id"][p]) == ACT_SQUAT

    out = _run_one_step(dataset_path, record)
    assert int(out["action_id"][p]) == ACT_SQUAT
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][p])


@pytest.mark.integration
def test_squat_platform_pass_countdown_preserves_held_down_spacie_positive_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        / "PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 2257
    p = 0
    row = _row(dataset_path, record)
    assert int(row["seed_t"]["action_id"][p]) == ACT_SQUAT
    assert int(row["seed_t"]["action_frame"][p]) == 3
    assert int(row["prev_input_t"]["p"]["main_y"][p]) < -60
    assert int(row["input_t"]["p"]["main_y"][p]) < -60
    assert int(row["ref_t1"]["action_id"][p]) == ACT_PASS

    out = _run_one_step(dataset_path, record)
    assert int(out["action_id"][p]) == ACT_PASS
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][p])


@pytest.mark.integration
def test_squat_platform_pass_countdown_requires_hidden_latch_negative_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/marth/replays/validation/marth/VictoriousSpitefulAlpaca.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 3269
    p = 1

    def clear_hidden_latch_and_inputs(row: np.ndarray) -> None:
        row["prev_input_t"]["p"]["main_y"][0, p] = np.int8(0)
        row["input_t"]["p"]["main_y"][0, p] = np.int8(0)

    out = _run_one_step(dataset_path, record, row_mutator=clear_hidden_latch_and_inputs)
    assert int(out["action_id"][p]) == ACT_SQUAT
    assert int(out["action_id"][p]) != ACT_PASS


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "start_record", "target_record", "p"),
    [
        ("datasets/marth/replays/validation/marth/LoudDullGoat.msl", 88, 90, 0),
        ("datasets/marth/replays/validation/marth/VictoriousSpitefulAlpaca.msl", 89, 91, 1),
    ],
)
def test_squat_platform_pass_countdown_carries_after_stick_release_marth_rollout(
    dataset_rel: str, start_record: int, target_record: int, p: int
) -> None:
    # Free-running source owner:
    # ftCo_80099F9C arms mv.co.squat.x0/x4 while down is held on the platform, then later
    # ftCo_Squat_IASA_inline decrements x4 and enters Pass even after the stick is released.
    # These rows have no visible down input on the consume frame, so row-local one-step inference is
    # intentionally insufficient; the hidden countdown must be carried from the earlier Squat rows.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_80099F9C
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA_inline
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    start = ds.samples[start_record]
    target = ds.samples[target_record]
    assert int(start["seed_t"]["action_id"][p]) == ACT_SQUAT
    assert int(start["seed_t"]["action_frame"][p]) == 1
    assert int(start["input_t"]["p"]["main_y"][p]) < -60
    assert int(target["seed_t"]["action_id"][p]) == ACT_SQUAT
    assert int(target["seed_t"]["action_frame"][p]) == 3
    assert int(target["input_t"]["p"]["main_y"][p]) > -60
    assert int(target["ref_t1"]["action_id"][p]) == ACT_PASS

    out = _run_rollout_window(dataset_path, start_record=start_record, target_record=target_record)
    assert int(out["action_id"][p]) == ACT_PASS
    assert int(out["action_id"][p]) == int(target["ref_t1"]["action_id"][p])
    assert int(out["on_ground"][p]) == int(target["ref_t1"]["on_ground"][p]) == 0


@pytest.mark.integration
def test_squat_platform_pass_countdown_rollout_requires_armed_down_latch() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/marth/replays/validation/marth/LoudDullGoat.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 0

    def clear_down_latch(rows: np.ndarray) -> None:
        rows["prev_input_t"]["p"]["main_y"][:, p] = np.int8(0)
        rows["input_t"]["p"]["main_y"][:, p] = np.int8(0)

    out = _run_rollout_window(dataset_path, start_record=88, target_record=90, row_mutator=clear_down_latch)
    assert int(out["action_id"][p]) == ACT_SQUAT
    assert int(out["action_id"][p]) != ACT_PASS


@pytest.mark.integration
def test_squat_platform_pass_countdown_does_not_arm_on_same_frame_squat_entry_spacie_rollout() -> None:
    # Callback-order negative for the free-running hidden countdown:
    # PassiveStand_Anim enters Wait, whose IASA can enter Squat, but that same source callback pass
    # does not also run ftCo_Squat_IASA/ftCo_80099F9C. The pass latch first arms on the next real
    # Squat IASA frame, so this held-down spacie control passes at rec 2594, not rec 2593.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/battlefield_recent/"
        / "MediumVirtualPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 0
    entry = ds.samples[2591]
    early = ds.samples[2593]
    consume = ds.samples[2594]
    assert int(entry["seed_t"]["action_id"][p]) == 200
    assert int(entry["ref_t1"]["action_id"][p]) == ACT_SQUAT
    assert int(early["seed_t"]["action_id"][p]) == ACT_SQUAT
    assert int(early["seed_t"]["action_frame"][p]) == 2
    assert int(early["ref_t1"]["action_id"][p]) == ACT_SQUAT
    assert int(consume["seed_t"]["action_id"][p]) == ACT_SQUAT
    assert int(consume["seed_t"]["action_frame"][p]) == 3
    assert int(consume["ref_t1"]["action_id"][p]) == ACT_PASS

    early_out = _run_rollout_window(dataset_path, start_record=2591, target_record=2593)
    assert int(early_out["action_id"][p]) == ACT_SQUAT
    assert int(early_out["action_id"][p]) == int(early["ref_t1"]["action_id"][p])

    consume_out = _run_rollout_window(dataset_path, start_record=2591, target_record=2594)
    assert int(consume_out["action_id"][p]) == ACT_PASS
    assert int(consume_out["action_id"][p]) == int(consume["ref_t1"]["action_id"][p])


@pytest.mark.integration
def test_squat_platform_pass_countdown_no_hidden_latch_spacie_negative_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        / "PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 2257
    p = 0

    def clear_hidden_latch_and_inputs(row: np.ndarray) -> None:
        row["prev_input_t"]["p"]["main_y"][0, p] = np.int8(0)
        row["input_t"]["p"]["main_y"][0, p] = np.int8(0)

    out = _run_one_step(dataset_path, record, row_mutator=clear_hidden_latch_and_inputs)
    assert int(out["action_id"][p]) == ACT_SQUAT
    assert int(out["action_id"][p]) != ACT_PASS
