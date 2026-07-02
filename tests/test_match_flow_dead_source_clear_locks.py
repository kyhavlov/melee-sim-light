from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


STAGE_FD = 32
CHAR_FOX = 1
ACT_DEAD_LEFT = 1
ACT_DEAD_RIGHT = 2
ACT_DEAD_UP_FALL = 6
ACT_WAIT = 14
SM_WAIT = 2


def _seed_base() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["anim_frame_f32"][0, :2] = np.float32(0.0)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT)
    return seed


def _step_once(seed: np.ndarray) -> np.void:
    msl_binding = pytest.importorskip("msl_binding")
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def test_dead_flow_preserves_terminal_source_clear_timer_until_rebirth_reset() -> None:
    # Dead* actions still need the death-source lane for dead-flow bookkeeping; Rebirth reset owns
    # the later clear. A terminal x18C8 seed must therefore not clear last_hit_by inside DeadLeft.
    # refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D331C,ftCo_800D34E0}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_UnkInitReset_80067C98}
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_DEAD_LEFT)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["action_frame"][0, 0] = np.int16(-1)
    seed["match_flow_timer"][0, 0] = np.uint8(50)
    seed["last_hit_by"][0, 0] = np.uint8(1)
    seed["source_clear_timer_x18c8"][0, 0] = np.uint8(1)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_DEAD_LEFT
    assert int(out["last_hit_by"][0]) == 1


def test_terminal_source_clear_timer_still_clears_outside_dead_flow() -> None:
    # Negative control: the ordinary Fighter_8006A360 x18C8 terminal clear still applies outside
    # match-flow Dead* actions.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    seed = _seed_base()
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(SM_WAIT)
    seed["last_hit_by"][0, 0] = np.uint8(1)
    seed["source_clear_timer_x18c8"][0, 0] = np.uint8(1)

    out = _step_once(seed)
    assert int(out["action_id"][0]) == ACT_WAIT
    assert int(out["last_hit_by"][0]) == 6


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "action_id"),
    [
        (
            "replays/validation/yoshis_story_recent/CheeryNumbMonkey.slpz",
            3745,
            0,
            ACT_DEAD_RIGHT,
        ),
        (
            "replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.slpz",
            6026,
            0,
            ACT_DEAD_LEFT,
        ),
        (
            "replays/validation/battlefield_recent/DelayedSuperbGuanaco.slpz",
            7037,
            1,
            ACT_DEAD_UP_FALL,
        ),
    ],
)
def test_replay_real_dead_flow_preserves_last_hit_by_on_terminal_x18c8(
    dataset_rel: str, record: int, p: int, action_id: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, record, p)
    assert int(seed["action_id"][p]) == action_id
    assert int(seed["source_clear_timer_x18c8"][p]) == 1
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["last_hit_by"][p]) == int(ref["last_hit_by"][p])
