from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_items_spawn_joint_replay_real_locks import _skip_if_required_artifacts_missing
from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


BUTTON_L = 0x0040
BUTTON_R = 0x0020
BUTTON_Y = 0x0800

ACT_WAIT = 0x000E
ACT_KNEE_BEND = 0x0018
ACT_PASS = 0x00F4
ACT_GUARD_REFLECT = 0x00B6

SM_WAIT = 2

CHAR_FOX = 1
STAGE_FD = 32

STATE_FLAG_221C_B1 = 0x40
STATE_FLAG_221C_B2 = 0x20
STATE_FLAG_221C_B3 = 0x10


def _mk_input_bytes(input_stride: int) -> np.ndarray:
    return np.zeros((1, input_stride), dtype=np.uint8)


def _run_seed_step(seed: np.ndarray, prev_input: np.ndarray, input_t: np.ndarray) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    out = np.zeros((1, compare_stride), dtype=np.uint8)
    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_input, input_t)
        msl_binding.write_compare(handle, out)
    finally:
        msl_binding.destroy(handle)
    return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()


def _guard_reflect_exit_seed(*, x14: int, x18: int, state_flags_221c: int) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)

    seed["action_id"][0, 0] = np.uint16(ACT_GUARD_REFLECT)
    seed["action_frame"][0, 0] = np.int16(-1)
    seed["anim_frame_f32"][0, 0] = np.float32(-1.0)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["frame_speed_mul_f32"][0, 0] = np.float32(0.0)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_GUARD_REFLECT)
    seed["seed_prev_action_frame"][0, 0] = np.int16(-2)
    seed["guard_reflect_timer_x14"][0, 0] = np.uint8(x14)
    seed["guard_reflect_timer_x18"][0, 0] = np.uint8(x18)
    seed["state_flags"][0, 0, 3] = np.uint8(state_flags_221c)

    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT)
    seed["on_ground"][0, 1] = np.uint8(1)
    return seed


@pytest.mark.parametrize(
    ("x14", "x18", "seed_flags", "expected_flags"),
    [
        (2, 4, STATE_FLAG_221C_B1 | STATE_FLAG_221C_B2 | STATE_FLAG_221C_B3,
         STATE_FLAG_221C_B1 | STATE_FLAG_221C_B2),
        (1, 3, STATE_FLAG_221C_B1 | STATE_FLAG_221C_B2, STATE_FLAG_221C_B2),
    ],
)
def test_guard_reflect_exit_uses_post_anim_timer_bits(
    x14: int, x18: int, seed_flags: int, expected_flags: int
) -> None:
    # GuardReflect_Anim runs before GuardReflect_IASA. ftCo_80093BC0 ticks mv.co.guard.x14/x18
    # and clears x221C_b3 before same-frame IASA exits such as KneeBend are reported.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_GuardReflect_IASA}
    import msl_binding

    sizes = msl_binding.sizes()
    input_stride = int(sizes["input"])
    prev_input = _mk_input_bytes(input_stride)
    input_t = _mk_input_bytes(input_stride)
    prev_view = prev_input.view(INPUT_DTYPE).reshape((1,))
    input_view = input_t.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    input_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_L | BUTTON_Y)

    out = _run_seed_step(
        _guard_reflect_exit_seed(x14=x14, x18=x18, state_flags_221c=seed_flags),
        prev_input,
        input_t,
    )

    assert int(out["action_id"][0]) == ACT_KNEE_BEND
    assert int(out["state_flags"][0, 3]) == expected_flags


@dataclass(frozen=True)
class _ReplayCase:
    dataset_rel: str
    record: int
    p: int
    expected_action: int
    expected_flags_221c: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ReplayCase(
            dataset_rel="replays/validation/pokemon_stadium_recent/CornyDelayedOkapi.slpz",
            record=9403,
            p=1,
            expected_action=ACT_PASS,
            expected_flags_221c=STATE_FLAG_221C_B2,
            note="soft-platform GuardReflect->Pass expired x14 live x18",
        ),
        _ReplayCase(
            dataset_rel="replays/validation/yoshis_story_recent/CheeryNumbMonkey.slpz",
            record=4735,
            p=1,
            expected_action=ACT_KNEE_BEND,
            expected_flags_221c=STATE_FLAG_221C_B1 | STATE_FLAG_221C_B2,
            note="GuardReflect->KneeBend live x14/x18",
        ),
        _ReplayCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
            record=734,
            p=0,
            expected_action=ACT_KNEE_BEND,
            expected_flags_221c=STATE_FLAG_221C_B2,
            note="cardinal GuardReflect->KneeBend expired x14 live x18",
        ),
    ],
)
def test_guard_reflect_exit_timer_bits_replay_real_locks(case: _ReplayCase) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[case.record]
    seed = row["seed_t"]
    ref = row["ref_t1"]
    assert int(seed["action_id"][case.p]) == ACT_GUARD_REFLECT, case.note
    assert int(ref["action_id"][case.p]) == case.expected_action, case.note
    assert int(ref["state_flags"][case.p, 3]) == case.expected_flags_221c, case.note

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][case.p]) == int(ref["action_id"][case.p]), case.note
    assert int(out["state_flags"][case.p, 3]) == int(ref["state_flags"][case.p, 3]), case.note
