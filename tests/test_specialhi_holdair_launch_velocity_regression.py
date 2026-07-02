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
    ("dataset_rel", "record", "p", "baseline_abs_pos_y"),
    [
        (
            "replays/validation/aggregate_recent/"
            "HilariousVillainousGiraffe.slpz",
            3833,
            1,
            1.006180,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.slpz",
            9368,
            0,
            4.308922,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            3083,
            0,
            3.359558,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            2905,
            0,
            3.799999,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            7904,
            0,
            3.799999,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.slpz",
            8473,
            1,
            3.799999,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz",
            7079,
            0,
            2.826900,
        ),
    ],
)
def test_specialhi_holdair_launch_rows_clear_hold_velocity_and_improve_pos_y_error(
    dataset_rel: str, record: int, p: int, baseline_abs_pos_y: float
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(dataset_path, record, p)

    # Decomp ownership:
    # - HoldAir anim end enters launch (`ftFx_SpecialAirHi_Enter`) in-air before the current-frame
    #   input proc installs new pad state.
    # - Launch enter derives launch angle from the pre-input stick and ftFox_DatAttrs.{x64,x88},
    #   then overwrites `fp->self_vel.{x,y}` from x74 launch speed.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiHoldAir_Anim,ftFx_SpecialAirHi_Enter
    # }
    assert int(seed["action_id"][p]) == 354  # ftFx_MS_SpecialHiHoldAir
    assert int(ref["action_id"][p]) == 356  # ftFx_MS_SpecialAirHi
    assert int(seed["on_ground"][p]) == 0
    assert int(seed["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) == int(ref["hitstun"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 356
    assert abs(float(out["speed_air_x_self"][p]) - float(ref["speed_air_x_self"][p])) <= 1e-3
    assert abs(float(out["speed_y_self"][p]) - float(ref["speed_y_self"][p])) <= 1e-3

    # Runtime-dominant locks: one-step@t and rollout@t agree for these rows.
    assert int(out_roll["action_id"][p]) == int(out["action_id"][p])
    assert abs(float(out_roll["pos_x"][p]) - float(out["pos_x"][p])) <= 1e-4
    assert abs(float(out_roll["pos_y"][p]) - float(out["pos_y"][p])) <= 1e-4

    abs_pos_y_err = abs(float(out["pos_y"][p]) - float(ref["pos_y"][p]))
    assert abs_pos_y_err <= (float(baseline_abs_pos_y) - 0.25)


@pytest.mark.integration
def test_specialhi_holdair_launch_uses_pre_input_stick_tch_3849() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/"
        "TubbyCurlyHerring.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    record = 3849
    p = 1

    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    seed = samples["seed_t"][record]
    ref = samples["ref_t1"][record]
    assert int(seed["action_id"][p]) == 354  # ftFx_MS_SpecialHiHoldAir
    assert int(ref["action_id"][p]) == 356  # ftFx_MS_SpecialAirHi
    assert int(samples["prev_input_t"]["p"][record]["main_x"][p]) == 48
    assert int(samples["prev_input_t"]["p"][record]["main_y"][p]) == 88
    assert int(samples["input_t"]["p"][record]["main_x"][p]) == 57
    assert int(samples["input_t"]["p"][record]["main_y"][p]) == 86

    def run_with_current_input(main_x: int, main_y: int) -> np.void:
        handle = binding.init(batch_size=1, num_players=int(ds.num_players))
        try:
            seed_bytes[0, :] = seed_u8[record, :seed_stride]
            prev_input_bytes[0, :] = prev_input_u8[record, :input_stride]
            input_bytes[0, :] = input_u8[record, :input_stride]
            input_view = input_bytes.view(samples["input_t"].dtype).reshape((1,))
            input_view["p"][0]["main_x"][p] = np.int8(main_x)
            input_view["p"][0]["main_y"][p] = np.int8(main_y)
            binding.reseed_seed(handle, seed_bytes)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            return out_view[0].copy()
        finally:
            binding.destroy(handle)

    got = run_with_current_input(57, 86)
    got_mutated_current = run_with_current_input(80, -80)

    # Decomp ordering: this is a prio-1 Anim callback path, so the launch vector must be unchanged
    # when only the later current-frame input is mutated.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_Spaghetti_8006AD10}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiHoldAir_Anim,ftFx_SpecialAirHi_Enter}
    for field in ("pos_x", "pos_y", "speed_air_x_self", "speed_y_self"):
        assert float(got[field][p]) == pytest.approx(float(ref[field][p]), abs=1e-5), field
        assert float(got_mutated_current[field][p]) == pytest.approx(
            float(got[field][p]), abs=1e-6
        ), field


@pytest.mark.integration
def test_specialhi_holdair_launch_hit_uses_current_instance_stale_queue_his_1764() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/"
        "HungryImportantSnake.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    record = 1764
    attacker = 0
    defender = 1
    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(
        dataset_path, record, attacker, window_before=0
    )

    assert int(seed["action_id"][attacker]) == 354  # ftFx_MS_SpecialHiHoldAir
    assert int(ref["action_id"][attacker]) == 356  # ftFx_MS_SpecialAirHi
    assert int(seed["action_id"][defender]) == 86  # DamageAir3
    assert int(ref["action_id"][defender]) == 90  # DamageFlyTop

    # The launch hit is created by the SpecialAirHi transition, so the same attack instance already
    # present in the seeded stale queue must apply. Treating it as a residual frame-start capsule
    # excluded this entry and produced an unstaled 14% hit.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiHoldAir_Anim,ftFx_SpecialAirHi_Enter}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_80076ED8}
    attack_id = int(seed["attack_id"][attacker])
    attack_instance = int(seed["attack_instance"][attacker])
    assert attack_id == 20
    assert any(
        int(mid) == attack_id and int(inst) == attack_instance
        for mid, inst in zip(
            seed["stale_move_id"][attacker], seed["stale_attack_instance"][attacker]
        )
    )

    for got in (out, out_roll):
        assert int(got["action_id"][defender]) == int(ref["action_id"][defender])
        assert int(got["hitlag"][defender]) == int(ref["hitlag"][defender])
        assert int(got["hitstun"][defender]) == int(ref["hitstun"][defender])
        assert float(got["percent"][defender]) == pytest.approx(
            float(ref["percent"][defender]), abs=1e-5
        )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        (
            "replays/validation/aggregate_recent/"
            "HilariousVillainousGiraffe.slpz",
            3832,
            1,
            354,  # HoldAir one frame before launch with clamped/deadzoned horizontal stick
            354,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.slpz",
            9367,
            0,
            354,  # HoldAir one frame before launch
            354,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            7903,
            0,
            354,  # HoldAir one frame before launch
            354,
        ),
    ],
)
def test_specialhi_holdair_launch_context_controls_remain_replay_real(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(dataset_path, record, p)

    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out_roll["action_id"][p]) == int(out["action_id"][p])
    assert np.isfinite(float(out["pos_x"][p]))
    assert np.isfinite(float(out["pos_y"][p]))
    assert np.isfinite(float(ref["pos_x"][p]))
    assert np.isfinite(float(ref["pos_y"][p]))


@pytest.mark.integration
def test_specialairhi_bound_entry_applies_extracted_horizontal_velocity_scalar_his_559() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/"
        "HungryImportantSnake.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    p = 1
    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(dataset_path, 559, p)

    # Decomp ownership: SpecialAirHi_Coll can call ftFx_SpecialHiBound_Enter, which changes motion,
    # immediately ticks anim, then scales `fp->self_vel.x *= da->x84_FOX_FIREFOX_BOUND_VEL_X`.
    # Source key: data/characters/{fox,falco}.json `firefox_bound_vel_x`.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiBound_Enter
    assert int(seed["action_id"][p]) == 356  # ftFx_MS_SpecialAirHi
    assert int(ref["action_id"][p]) == 359  # ftFx_MS_SpecialHiBound
    assert int(ref["on_ground"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 359
    assert float(out["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=1e-5
    )
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-5)

    # Rollout lock for the motivating disruptive row: the same bound-entry velocity must survive
    # runtime history, not just teacher-forced one-step state.
    assert int(out_roll["action_id"][p]) == int(ref["action_id"][p])
    assert float(out_roll["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=1e-5
    )


@pytest.mark.integration
def test_specialairhi_bound_velocity_scalar_does_not_apply_before_bound_entry_his_558() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/"
        "HungryImportantSnake.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    p = 1
    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(dataset_path, 558, p)

    assert int(seed["action_id"][p]) == 356
    assert int(ref["action_id"][p]) == 356
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out_roll["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p]) == 0
    assert int(out_roll["jumps_left"][p]) == int(ref["jumps_left"][p]) == 0

    unscaled = float(ref["speed_air_x_self"][p])
    assert abs(unscaled) > 0.1
    assert float(out["speed_air_x_self"][p]) == pytest.approx(unscaled, abs=1e-5)
    assert float(out["speed_air_x_self"][p]) != pytest.approx(unscaled * 0.800000011920929, abs=1e-4)


@pytest.mark.integration
@pytest.mark.parametrize("record", [560, 562])
def test_specialhi_bound_airborne_phys_uses_root_y_and_air_friction_his(record: int) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/"
        "HungryImportantSnake.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    p = 1
    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(
        dataset_path, record, p, window_before=26
    )

    # Decomp ownership:
    # - airborne SpecialHiBound_Phys calls ft_800851C0, replacing self_vel.y from TransN offset.
    # - it then calls ftCommon_8007CF58 for horizontal friction/clamp.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiBound_Phys
    # refs/melee/src/melee/ft/ft_081B.c::ft_800851C0
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CF58
    assert int(seed["action_id"][p]) == 359
    assert int(ref["action_id"][p]) == 359
    assert int(ref["on_ground"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p])
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-4)
    assert float(out["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=1e-4
    )

    assert int(out_roll["action_id"][p]) == int(ref["action_id"][p])
    assert int(out_roll["on_ground"][p]) == int(ref["on_ground"][p])
    assert int(out_roll["jumps_left"][p]) == int(ref["jumps_left"][p])
    assert float(out_roll["speed_y_self"][p]) == pytest.approx(
        float(ref["speed_y_self"][p]), abs=1e-4
    )
