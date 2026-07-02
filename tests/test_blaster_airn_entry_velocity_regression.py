from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views


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


def _step_one_row_with_rollout_at_record(
    dataset_path: Path, record: int, p: int, *, window_before: int = 24
) -> tuple[np.void, np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > record, f"replay too short for record={record}"

    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    one_step_handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes[0, :] = seed_u8[record, :seed_stride]
        prev_input_bytes[0, :] = prev_input_u8[record, :input_stride]
        input_bytes[0, :] = input_u8[record, :input_stride]
        binding.reseed_seed(one_step_handle, seed_bytes)
        binding.step_input(one_step_handle, prev_input_bytes, input_bytes)
        binding.write_compare(one_step_handle, out_compare_bytes)
        out_one = out_view[0].copy()
    finally:
        binding.destroy(one_step_handle)

    start = max(0, int(record) - int(window_before))
    rollout_handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes[0, :] = seed_u8[start, :seed_stride]
        binding.reseed_seed_rollout(rollout_handle, seed_bytes)
        for j in range(start, int(record) + 1):
            prev_input_bytes[0, :] = prev_input_u8[j, :input_stride]
            input_bytes[0, :] = input_u8[j, :input_stride]
            binding.step_input(rollout_handle, prev_input_bytes, input_bytes)
            if j == int(record):
                binding.write_compare(rollout_handle, out_compare_bytes)
        out_roll = out_view[0].copy()
    finally:
        binding.destroy(rollout_handle)

    seed = samples["seed_t"][record]
    ref = samples["ref_t1"][record]
    return seed, out_one, ref, out_roll


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        (
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz",
            6444,
            1,
            25,  # JumpF
            344,  # SpecialAirNStart
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            104,
            1,
            25,  # JumpF
            344,  # SpecialAirNStart
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            6158,
            1,
            25,  # JumpF
            344,  # SpecialAirNStart
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            2715,
            1,
            25,  # JumpF
            344,  # SpecialAirNStart
        ),
    ],
)
def test_blaster_airn_start_rows_preserve_aerial_velocity_runtime_family(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(dataset_path, record, p)

    # Decomp ownership:
    # - Aerial blaster entry does not clear self velocity.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirN_Enter
    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(seed["on_ground"][p]) == 0
    assert int(ref["on_ground"][p]) == 0
    assert int(seed["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) == int(ref["hitstun"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 344
    assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 2e-4
    assert abs(float(out["speed_air_x_self"][p]) - float(ref["speed_air_x_self"][p])) <= 1e-5
    assert abs(float(out["speed_y_self"][p]) - float(ref["speed_y_self"][p])) <= 1e-5

    # Runtime-dominant lock: one-step@t and rollout@t agree for this family.
    assert int(out_roll["action_id"][p]) == int(out["action_id"][p]) == 344
    assert abs(float(out_roll["pos_y"][p]) - float(out["pos_y"][p])) <= 1e-4


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action", "rollout_action"),
    [
        (
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz",
            6443,
            1,
            25,  # JumpF (pre-entry control)
            25,
            25,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            103,
            1,
            25,  # JumpF (pre-entry control)
            25,
            25,
        ),
    ],
)
def test_blaster_airn_start_runtime_context_controls_stay_replay_real(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int, rollout_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(dataset_path, record, p)

    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == int(rollout_action)
    assert int(out_roll["action_id"][p]) == int(rollout_action)
    assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 2e-4
    assert abs(float(out_roll["pos_y"][p]) - float(out["pos_y"][p])) <= 1e-4


@pytest.mark.integration
def test_blaster_airn_start_preserves_damagefall_kb_velocity_on_entry() -> None:
    # Decomp ownership:
    # - ftFx_SpecialAirN_Enter calls Fighter_ChangeMotionState(..., flags=0), then spawns blaster.
    # - Fighter_ChangeMotionState does not clear fp->x8c_kb_vel.
    # Regression target: PPA rec1123 p0 DamageFall -> SpecialAirNStart after DamageFlyRoll carry.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "replays/validation/aggregate_recent/PriceyPartialAlbatross.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    record = 1123
    p = 0
    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(
        dataset_path, record, p, window_before=20
    )

    assert int(seed["action_id"][p]) == 38  # DamageFall
    assert int(ref["action_id"][p]) == 344  # SpecialAirNStart
    assert int(seed["on_ground"][p]) == 0
    assert float(seed["speed_x_attack"][p]) != 0.0
    assert float(seed["speed_y_attack"][p]) != 0.0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 344
    assert float(out["speed_x_attack"][p]) == pytest.approx(float(ref["speed_x_attack"][p]), abs=1e-6)
    assert float(out["speed_y_attack"][p]) == pytest.approx(float(ref["speed_y_attack"][p]), abs=1e-6)
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-5)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-5)

    assert int(out_roll["action_id"][p]) == int(ref["action_id"][p]) == 344
    assert float(out_roll["speed_x_attack"][p]) == pytest.approx(
        float(ref["speed_x_attack"][p]), abs=1e-6
    )
    assert float(out_roll["speed_y_attack"][p]) == pytest.approx(
        float(ref["speed_y_attack"][p]), abs=1e-6
    )
    assert float(out_roll["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-5)
    assert float(out_roll["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-5)


@pytest.mark.integration
def test_blaster_airn_start_zero_kb_velocity_control_does_not_fabricate_carry() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    record = 95
    p = 0
    seed, out, ref, _out_roll = _step_one_row_with_rollout_at_record(
        dataset_path, record, p, window_before=4
    )

    assert int(seed["action_id"][p]) == 25  # JumpF
    assert int(ref["action_id"][p]) == 344  # SpecialAirNStart
    assert float(seed["speed_x_attack"][p]) == 0.0
    assert float(seed["speed_y_attack"][p]) == 0.0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 344
    assert float(out["speed_x_attack"][p]) == pytest.approx(0.0, abs=1e-7)
    assert float(out["speed_y_attack"][p]) == pytest.approx(0.0, abs=1e-7)
    assert float(out["speed_x_attack"][p]) == pytest.approx(float(ref["speed_x_attack"][p]), abs=1e-7)
    assert float(out["speed_y_attack"][p]) == pytest.approx(float(ref["speed_y_attack"][p]), abs=1e-7)
