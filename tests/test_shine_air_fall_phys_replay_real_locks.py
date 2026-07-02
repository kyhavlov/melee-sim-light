from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


ACT_FX_SPECIAL_AIR_LW_START = 0x016D
ACT_FX_SPECIAL_AIR_LW_LOOP = 0x016E
ACT_FX_SPECIAL_AIR_LW_TURN = 0x0171
ACT_FX_SPECIAL_LW_LOOP = 0x0169
ACT_JUMP_AERIAL_F = 27
ACT_DAMAGE_AIR = 84


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _run_one_step(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    assert int(row.shape[0]) == 1

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

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    return row["seed_t"].reshape(-1)[0].copy(), row["ref_t1"].reshape(-1)[0].copy(), out


def _run_rollout_window(dataset_path: Path, *, start_record: int, window_records: set[int]):
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    seed_bytes = (
        np.frombuffer(samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, seed_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    out: dict[int, np.void] = {}
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for rec in range(start_record, max(window_records) + 1):
            row = samples[rec : rec + 1]
            step_seed_bytes = (
                np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, seed_stride)
            )
            prev_input_bytes = (
                np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            input_bytes = (
                np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            binding.step_input_replay_frame_rng(handle, step_seed_bytes, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            if rec in window_records:
                out[rec] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)
    refs = {rec: samples[rec]["ref_t1"].copy() for rec in window_records}
    seeds = {rec: samples[rec]["seed_t"].copy() for rec in window_records}
    return seeds, refs, out


def _live_laser_keys(items) -> set[tuple[int, int, int, int]]:
    keys: set[tuple[int, int, int, int]] = set()
    for it in items:
        if int(it["exists"]) == 1 and int(it["type"]) == 55:
            keys.add((int(it["owner"]), int(it["instance_id"]), int(it["spawn_id"]), int(it["state"])))
    return keys


def test_aerial_shine_loop_applies_reflector_fall_phys() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    )
    if not dataset_path.exists():
        pytest.skip("missing local aggregate dataset")

    seed, ref, out = _run_one_step(dataset_path, 717)
    p = 0
    assert int(seed["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP
    assert int(ref["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP

    assert int(out["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-7)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


def test_platform_pass_aerial_shine_loop_carries_gravity_delay_pte() -> None:
    # FoD platform-pass from grounded Shine Start enters SpecialAirLwStart through
    # ftFx_SpecialLwStart_Pass / ftCo_8009A184. The hidden reflector gravityDelay is still live
    # when the action reaches Loop, so the first Loop frame with pass_vel_y=-0.5 must not apply the
    # reflector fall accel yet.
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLwStart_Pass,ftFx_SpecialAirLwStart_Phys,ftFx_SpecialAirLwLoop_Phys}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A184
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.slpz"
    )
    if not dataset_path.exists():
        pytest.skip("missing local FoD dataset")

    seed, ref, out = _run_one_step(dataset_path, 307)
    p = 0
    assert int(seed["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP
    assert int(seed["action_frame"][p]) == 0
    assert int(seed["seed_prev_action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START
    assert float(seed["speed_y_self"][p]) == pytest.approx(-0.5, abs=1e-7)
    assert int(ref["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP

    assert int(out["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-7)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


def test_aerial_shine_loop_landing_carries_air_x_to_ground_speed_ewt() -> None:
    # SpecialAirLwLoop_Coll -> ftFx_SpecialAirLwLoop_AirToGround calls ftCommon_8007D7FC
    # before entering grounded SpecialLwLoop. That helper publishes `fp->gr_vel = fp->self_vel.x`;
    # the post-change ftCommon_ClampAirDrift does not clear the landing row's ground speed.
    #
    # EWT rec 8656 is the direct FoD replay-real lock: Falco lands out of aerial Shine Loop on a
    # platform and must carry the current horizontal self velocity into the grounded speed lane.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLwLoop_AirToGround
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.slpz"
    )
    if not dataset_path.exists():
        pytest.skip("missing local FoD dataset")

    seed, ref, out = _run_one_step(dataset_path, 8656)
    p = 1
    assert int(seed["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP
    assert int(ref["action_id"][p]) == ACT_FX_SPECIAL_LW_LOOP
    assert int(ref["on_ground"][p]) == 1

    assert int(out["action_id"][p]) == ACT_FX_SPECIAL_LW_LOOP
    assert int(out["on_ground"][p]) == 1
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=1e-6
    )
    assert float(out["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=1e-6
    )


def test_grounded_shine_loop_followup_keeps_seeded_ground_speed_ewt() -> None:
    # Negative/control for the landing handoff above: once the replay seed is already grounded
    # SpecialLwLoop, the air->ground helper is no longer active. The grounded loop follow-up keeps
    # the seed/source ground-speed lane rather than reapplying a landing carry.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.slpz"
    )
    if not dataset_path.exists():
        pytest.skip("missing local FoD dataset")

    seed, ref, out = _run_one_step(dataset_path, 8657)
    p = 1
    assert int(seed["action_id"][p]) == ACT_FX_SPECIAL_LW_LOOP
    assert int(seed["on_ground"][p]) == 1
    assert int(ref["action_id"][p]) == ACT_FX_SPECIAL_LW_LOOP

    assert int(out["action_id"][p]) == ACT_FX_SPECIAL_LW_LOOP
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=1e-6
    )


def test_aerial_shine_start_delay_does_not_apply_reflector_fall_early() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    )
    if not dataset_path.exists():
        pytest.skip("missing local aggregate dataset")

    seed, ref, out = _run_one_step(dataset_path, 715)
    p = 0
    assert int(seed["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START
    assert int(ref["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START

    assert int(out["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-7)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


def test_aerial_shine_start_to_loop_handoff_does_not_double_tick_fall() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    )
    if not dataset_path.exists():
        pytest.skip("missing local aggregate dataset")

    seed, ref, out = _run_one_step(dataset_path, 716)
    p = 0
    assert int(seed["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START
    assert int(ref["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP

    assert int(out["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-7)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


def test_aerial_shine_start_to_turn_handoff_does_not_double_tick_fall() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/DistinctCaringCobra.slpz"
    )
    if not dataset_path.exists():
        pytest.skip("missing local aggregate dataset")

    seed, ref, out = _run_one_step(dataset_path, 4738)
    p = 1
    assert int(seed["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START
    assert int(ref["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_TURN

    assert int(out["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_TURN
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-7)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_shine_jumpaerial_entry_clears_hidden_depth_before_laser_body_tch_8969() -> None:
    # SpecialAirLwLoop IASA -> JumpAerialF calls ftCo_JumpAerial_Enter_Basic, whose common entry
    # owner `ftCommon_8007D5D4` clears `fp->cur_pos.z`. TCH rolls out from a grounded-overlap prefix
    # that legitimately carries hidden depth during Shine, but that lane must be cleared at the
    # JumpAerial entry. Otherwise the later Falco laser BODY probe sees a false full hit at 8969.
    # The adjacent 8970 row proves the real BODY hit is still admitted one frame later.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialAirLwLoop_IASA,ftFx_SpecialAirLwTurn_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    )
    if not dataset_path.exists():
        pytest.skip("missing local aggregate dataset")

    _seeds, refs, outs = _run_rollout_window(
        dataset_path, start_record=8846, window_records={8940, 8969, 8970}
    )
    p = 1

    assert int(refs[8940]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(outs[8940]["action_id"][p]) == ACT_JUMP_AERIAL_F

    assert int(refs[8969]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(outs[8969]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(outs[8969]["hitlag"][p]) == int(refs[8969]["hitlag"][p]) == 3
    assert int(outs[8969]["hitstun"][p]) == int(refs[8969]["hitstun"][p]) == 0
    assert float(outs[8969]["percent"][p]) == pytest.approx(float(refs[8969]["percent"][p]), abs=1e-6)
    assert _live_laser_keys(outs[8969]["items"]) == _live_laser_keys(refs[8969]["items"])

    assert int(refs[8970]["action_id"][p]) == ACT_DAMAGE_AIR
    assert int(outs[8970]["action_id"][p]) == ACT_DAMAGE_AIR
    assert int(outs[8970]["hitstun"][p]) == int(refs[8970]["hitstun"][p]) == 9
    assert float(outs[8970]["percent"][p]) == pytest.approx(float(refs[8970]["percent"][p]), abs=1e-6)
    assert _live_laser_keys(outs[8970]["items"]) == _live_laser_keys(refs[8970]["items"])
