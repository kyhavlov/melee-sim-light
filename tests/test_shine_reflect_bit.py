from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Fox/Falco Shine action ids (GALE01):
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views
# refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
# (ftFx_MS_SpecialLwStart=360 .. ftFx_MS_SpecialAirLwTurn=369)
ACT_FX_SPECIAL_LW_START = 0x0168
ACT_FX_SPECIAL_LW_LOOP = 0x0169
ACT_FX_SPECIAL_LW_END = 0x016B
ACT_FX_SPECIAL_LW_TURN = 0x016C
ACT_FX_SPECIAL_AIR_LW_START = 0x016D
ACT_FX_SPECIAL_AIR_LW_LOOP = 0x016E
ACT_DAMAGE_HI_1 = 0x0050

ACT_WAIT = 0x000E

BUTTON_B = 0x0200

CHAR_FOX = 1
CHAR_FALCO = 22
STAGE_FD = 32

# Slippi state_flags byte0 (fp+0x2218) reflect-active bit.
STATE_FLAG_2218_REFLECT_ACTIVE = 0x10


def _char_attr(char_id: int, name: str) -> int:
    char_name = "fox" if int(char_id) == CHAR_FOX else "falco"
    data = json.loads(Path(f"data/characters/{char_name}.json").read_text())
    return int(data[name])


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


def _rollout_to_record(dataset_path: Path, *, start_record: int, target_record: int) -> np.void:
    import msl_binding

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > target_record

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes[0, :] = seed_u8[start_record, :seed_stride]
        msl_binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, target_record + 1):
            prev_input_bytes[0, :] = prev_input_u8[record, :input_stride]
            input_bytes[0, :] = input_u8[record, :input_stride]
            msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_compare_bytes)
    finally:
        msl_binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


def test_shine_reflect_active_bit_is_set_in_loop() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        prev_inp_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        prev_inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
        inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
        for cid in (CHAR_FOX, CHAR_FALCO):
            seed = np.zeros((1,), dtype=SEED_DTYPE)
            seed["frame_id"][0] = np.int32(0)
            seed["stage_id"][0] = np.uint32(STAGE_FD)
            seed["match_damage_ratio"][0] = np.float32(1.0)
            seed["num_players"][0] = np.uint8(2)
            seed["stocks"][0, :2] = np.uint8(4)
            seed["char_id"][0, 0] = np.uint8(cid)
            seed["char_id"][0, 1] = np.uint8(cid)
            seed["attack_ratio"][0, :2] = np.float32(1.0)
            seed["defense_ratio"][0, :2] = np.float32(1.0)
            seed["fighter_scale_y"][0, :2] = np.float32(1.0)
            seed["facing"][0, :2] = np.uint8(1)
            seed["on_ground"][0, :2] = np.uint8(1)
            seed["ground_id"][0, :2] = np.uint16(0)
            seed["pos_y"][0, :2] = np.float32(5.0)

            seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_LW_LOOP)
            seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
            seed["anim_frame_f32"][0, 0] = np.float32(0.0)
            seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
            seed["state_flags"][0, 0, 0] = np.uint8(0x00)

            seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
            seed["animation_index"][0, 1] = np.uint32(2)
            seed["anim_frame_f32"][0, 1] = np.float32(0.0)
            seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)

            seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
            msl_binding.reseed_seed(handle, seed_bytes)
            msl_binding.step_input(handle, prev_inp, inp)

            out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
            msl_binding.write_compare(handle, out_cmp)
            cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

            assert int(cmp0["action_id"][0]) == ACT_FX_SPECIAL_LW_LOOP
            got = int(cmp0["state_flags"][0, 0])
            assert (got & STATE_FLAG_2218_REFLECT_ACTIVE) != 0
    finally:
        msl_binding.destroy(handle)


def test_shine_reflect_active_bit_clears_on_release_to_end() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        # Do NOT hold B: should transition Loop -> End once release lag is satisfied.
        for cid in (CHAR_FOX, CHAR_FALCO):
            rl = _char_attr(cid, "reflector_release_lag_frames")

            seed = np.zeros((1,), dtype=SEED_DTYPE)
            seed["stage_id"][0] = np.uint32(STAGE_FD)
            seed["match_damage_ratio"][0] = np.float32(1.0)
            seed["num_players"][0] = np.uint8(2)
            seed["stocks"][0, :2] = np.uint8(4)
            seed["char_id"][0, :2] = np.uint8(cid)
            seed["attack_ratio"][0, :2] = np.float32(1.0)
            seed["defense_ratio"][0, :2] = np.float32(1.0)
            seed["fighter_scale_y"][0, :2] = np.float32(1.0)
            seed["facing"][0, :2] = np.uint8(1)
            seed["on_ground"][0, :2] = np.uint8(1)
            seed["ground_id"][0, :2] = np.uint16(0)
            seed["pos_y"][0, :2] = np.float32(5.0)

            seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_LW_LOOP)
            seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
            # Ensure action_frame >= (rl - 1) after anim-timebase advancement this step.
            seed["anim_frame_f32"][0, 0] = np.float32(float(max(0, rl)))
            seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
            # Start with reflect bit set so the test can assert it is cleared on exit.
            seed["state_flags"][0, 0, 0] = np.uint8(STATE_FLAG_2218_REFLECT_ACTIVE)

            seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
            seed["animation_index"][0, 1] = np.uint32(2)
            seed["anim_frame_f32"][0, 1] = np.float32(0.0)
            seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)

            seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
            msl_binding.reseed_seed(handle, seed_bytes)
            msl_binding.step_input(handle, prev_inp, inp)

            out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
            msl_binding.write_compare(handle, out_cmp)
            cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

            assert int(cmp0["action_id"][0]) == ACT_FX_SPECIAL_LW_END
            got = int(cmp0["state_flags"][0, 0])
            assert (got & STATE_FLAG_2218_REFLECT_ACTIVE) == 0
    finally:
        msl_binding.destroy(handle)


def test_shine_reflect_active_bit_set_when_turn_transitions_to_loop() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        prev_inp_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        prev_inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
        inp_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_B)
        for cid in (CHAR_FOX, CHAR_FALCO):
            tf = _char_attr(cid, "reflector_turn_frames")
            assert tf > 0

            seed = np.zeros((1,), dtype=SEED_DTYPE)
            seed["stage_id"][0] = np.uint32(STAGE_FD)
            seed["match_damage_ratio"][0] = np.float32(1.0)
            seed["num_players"][0] = np.uint8(2)
            seed["stocks"][0, :2] = np.uint8(4)
            seed["char_id"][0, :2] = np.uint8(cid)
            seed["attack_ratio"][0, :2] = np.float32(1.0)
            seed["defense_ratio"][0, :2] = np.float32(1.0)
            seed["fighter_scale_y"][0, :2] = np.float32(1.0)
            seed["facing"][0, :2] = np.uint8(1)
            seed["on_ground"][0, :2] = np.uint8(1)
            seed["ground_id"][0, :2] = np.uint16(0)
            seed["pos_y"][0, :2] = np.float32(5.0)

            seed["action_id"][0, 0] = np.uint16(ACT_FX_SPECIAL_LW_TURN)
            seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
            # Ensure action_frame >= (tf - 1) after anim-timebase advancement this step.
            seed["anim_frame_f32"][0, 0] = np.float32(float(tf))
            seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
            seed["state_flags"][0, 0, 0] = np.uint8(0x00)

            seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
            seed["animation_index"][0, 1] = np.uint32(2)
            seed["anim_frame_f32"][0, 1] = np.float32(0.0)
            seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)

            seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
            msl_binding.reseed_seed(handle, seed_bytes)
            msl_binding.step_input(handle, prev_inp, inp)

            out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
            msl_binding.write_compare(handle, out_cmp)
            cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

            assert int(cmp0["action_id"][0]) == ACT_FX_SPECIAL_LW_LOOP
            got = int(cmp0["state_flags"][0, 0])
            assert (got & STATE_FLAG_2218_REFLECT_ACTIVE) != 0
    finally:
        msl_binding.destroy(handle)


@pytest.mark.integration
def test_shine_reflect_active_bit_clears_when_combat_changes_motion_state() -> None:
    # Replay-real rollout lock for a Shine -> Damage motion change:
    # - p0 enters SpecialLwLoop and owns fp->reflecting,
    # - p1's AttackDash hits p0 later in the rollout,
    # - Fighter_ChangeMotionState clears fp->reflecting on the DamageHi1 destination row.
    #
    # Regression target: HIS rollout from 4991 first differed at 5013 because the sim carried
    # state_flags[0] bit 0x10 from Shine into DamageHi1.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "replays/validation/aggregate_recent/HungryImportantSnake.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    p = 0
    start_record = 4991
    target_record = 5013
    assert int(ds.rows[start_record]["seed_t"]["action_id"][p]) == 65  # AttackAirN.
    assert int(ds.rows[5011]["ref_t1"]["action_id"][p]) == ACT_FX_SPECIAL_LW_LOOP
    assert (
        int(ds.rows[5011]["ref_t1"]["state_flags"][p, 0]) & STATE_FLAG_2218_REFLECT_ACTIVE
    ) != 0
    assert int(ds.rows[target_record]["ref_t1"]["action_id"][p]) == ACT_DAMAGE_HI_1
    assert (
        int(ds.rows[target_record]["ref_t1"]["state_flags"][p, 0])
        & STATE_FLAG_2218_REFLECT_ACTIVE
    ) == 0

    out = _rollout_to_record(dataset_path, start_record=start_record, target_record=target_record)
    assert int(out["action_id"][p]) == int(ds.rows[target_record]["ref_t1"]["action_id"][p])
    assert int(out["hitlag"][p]) == int(ds.rows[target_record]["ref_t1"]["hitlag"][p])
    assert int(out["hitstun"][p]) == int(ds.rows[target_record]["ref_t1"]["hitstun"][p])
    assert int(out["state_flags"][p, 0]) == int(ds.rows[target_record]["ref_t1"]["state_flags"][p, 0])


@pytest.mark.integration
def test_shine_reflect_active_bit_publishes_after_platform_pass_to_air_start() -> None:
    # Replay-real rollout lock for SpecialLwStart -> SpecialAirLwStart on a FoD side platform:
    # ftFx_SpecialLwStart_Pass changes motion state and then creates ReflectDesc before Slippi
    # serializes fp+0x2218. Ordinary ground->air Start transitions do not create this descriptor.
    #
    # Regression target: PTE rollout from match start first differed at 304/305 only in
    # state_flags[0] bit0x10.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwStart_Pass
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    p = 0
    target_record = 304
    assert int(ds.rows[303]["ref_t1"]["action_id"][p]) == ACT_FX_SPECIAL_LW_START
    assert int(ds.rows[target_record]["ref_t1"]["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START
    assert (
        int(ds.rows[target_record]["ref_t1"]["state_flags"][p, 0])
        & STATE_FLAG_2218_REFLECT_ACTIVE
    ) != 0

    out = _rollout_to_record(dataset_path, start_record=0, target_record=target_record)
    assert int(out["action_id"][p]) == int(ds.rows[target_record]["ref_t1"]["action_id"][p])
    assert int(out["state_flags"][p, 0]) == int(ds.rows[target_record]["ref_t1"]["state_flags"][p, 0])
