from __future__ import annotations

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tools.slippi.validation_buffer_fighter import _derive_match_flow_pending_rebirth_char_id
from tools.slippi.suite_io import load_suite, team_attack_on_from_start


STAGE_FD = 32
CHAR_FOX = 1

ACT_DEAD_DOWN = 0x0000
ACT_DEAD_LEFT = 0x0001
ACT_REBIRTH = 0x000C
ACT_WAIT = 0x000E


def _input_bytes(batch: int, stride: int) -> np.ndarray:
    return np.zeros((batch, stride), dtype=np.uint8)


def test_doubles_suite_requires_team_attack_on_contract(tmp_path) -> None:
    suite = tmp_path / "suite.json"
    suite.write_text(
        """{
  "name": "bad_doubles",
  "ucf_enabled": true,
  "ucf_cardinals_1_0_enabled": true,
  "replays": [{"replay": "r.slp", "ports": [1, 2, 3, 4]}]
}
""",
        encoding="utf-8",
    )

    with pytest.raises(ValueError, match="team_attack_on"):
        load_suite(suite)


def test_team_attack_on_from_slippi_start_bitfield() -> None:
    assert team_attack_on_from_start({"bitfield": [0x32, 0x01, 0x86, 0x4C]}) is True
    assert team_attack_on_from_start({"bitfield": [0x32, 0x00, 0x86, 0x4C]}) is False


def test_pending_rebirth_derivation_requires_teammate_stock_and_transition() -> None:
    post_action = np.array(
        [
            [ACT_DEAD_LEFT, ACT_WAIT, ACT_WAIT, ACT_WAIT],
            [ACT_DEAD_DOWN, ACT_WAIT, ACT_WAIT, ACT_WAIT],
            [ACT_DEAD_DOWN, ACT_WAIT, ACT_WAIT, ACT_WAIT],
        ],
        dtype=np.uint16,
    )
    post_char = np.array(
        [
            [CHAR_FOX, CHAR_FOX, CHAR_FOX, CHAR_FOX],
            [0, CHAR_FOX, CHAR_FOX, CHAR_FOX],
            [0, CHAR_FOX, CHAR_FOX, CHAR_FOX],
        ],
        dtype=np.uint8,
    )
    post_stocks = np.array(
        [
            [0, 2, 1, 1],
            [0, 2, 1, 1],
            [0, 2, 1, 1],
        ],
        dtype=np.uint8,
    )
    timer = np.array(
        [
            [2, 0, 0, 0],
            [1, 0, 0, 0],
            [1, 0, 0, 0],
        ],
        dtype=np.uint8,
    )

    got = _derive_match_flow_pending_rebirth_char_id(
        post_action_id_u16=post_action,
        post_char_id_u8=post_char,
        post_stocks_u8=post_stocks,
        match_flow_timer_u8=timer,
        static_char_id_u8=np.array([CHAR_FOX, CHAR_FOX, CHAR_FOX, CHAR_FOX], dtype=np.uint8),
        team_id_u8=np.array([0, 0, 1, 1], dtype=np.uint8),
        is_teams=True,
        num_players=4,
    )
    assert got[:, 0].tolist() == [0, CHAR_FOX, CHAR_FOX]


def test_pending_rebirth_derivation_keeps_terminal_elimination_unseeded() -> None:
    post_action = np.full((2, 4), ACT_DEAD_DOWN, dtype=np.uint16)
    post_char = np.zeros((2, 4), dtype=np.uint8)
    post_stocks = np.zeros((2, 4), dtype=np.uint8)
    timer = np.ones((2, 4), dtype=np.uint8)

    got = _derive_match_flow_pending_rebirth_char_id(
        post_action_id_u16=post_action,
        post_char_id_u8=post_char,
        post_stocks_u8=post_stocks,
        match_flow_timer_u8=timer,
        static_char_id_u8=np.array([CHAR_FOX, CHAR_FOX, CHAR_FOX, CHAR_FOX], dtype=np.uint8),
        team_id_u8=np.array([0, 0, 1, 1], dtype=np.uint8),
        is_teams=True,
        num_players=4,
    )
    assert np.all(got == 0)


def test_pending_rebirth_derivation_requires_teammate_more_than_one_stock() -> None:
    post_action = np.array(
        [
            [ACT_DEAD_LEFT, ACT_WAIT, ACT_WAIT, ACT_WAIT],
            [ACT_DEAD_DOWN, ACT_WAIT, ACT_WAIT, ACT_WAIT],
            [ACT_DEAD_DOWN, ACT_WAIT, ACT_WAIT, ACT_WAIT],
        ],
        dtype=np.uint16,
    )
    post_char = np.array(
        [
            [CHAR_FOX, CHAR_FOX, CHAR_FOX, CHAR_FOX],
            [0, CHAR_FOX, CHAR_FOX, CHAR_FOX],
            [0, CHAR_FOX, CHAR_FOX, CHAR_FOX],
        ],
        dtype=np.uint8,
    )
    post_stocks = np.array(
        [
            [0, 1, 1, 1],
            [0, 1, 1, 1],
            [0, 1, 1, 1],
        ],
        dtype=np.uint8,
    )
    timer = np.array(
        [
            [2, 0, 0, 0],
            [1, 0, 0, 0],
            [1, 0, 0, 0],
        ],
        dtype=np.uint8,
    )

    got = _derive_match_flow_pending_rebirth_char_id(
        post_action_id_u16=post_action,
        post_char_id_u8=post_char,
        post_stocks_u8=post_stocks,
        match_flow_timer_u8=timer,
        static_char_id_u8=np.array([CHAR_FOX, CHAR_FOX, CHAR_FOX, CHAR_FOX], dtype=np.uint8),
        team_id_u8=np.array([0, 0, 1, 1], dtype=np.uint8),
        is_teams=True,
        num_players=4,
    )
    assert np.all(got[:, 0] == 0)


def test_zero_stock_terminal_dead_slot_remains_inactive() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(4)
    seed["is_teams"][0] = np.uint8(1)
    seed["team_id"][0, :4] = np.array([0, 1, 1, 0], dtype=np.uint8)
    seed["char_id"][0, 0] = np.uint8(0)
    seed["stocks"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_DEAD_DOWN)
    seed["action_frame"][0, 0] = np.int16(-1)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["pos_x"][0, 0] = np.float32(-250.0)
    seed["pos_y"][0, 0] = np.float32(120.0)
    seed["shield_hp"][0, 0] = np.float32(60.0)
    seed["state_flags"][0, 0, :] = np.array([0x80, 0x20, 0x10, 0x08, 0x40], dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=4)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        inp = _input_bytes(1, input_stride)
        msl_binding.step_input(handle, inp, inp)
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out)
        cmp0 = out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)

    assert int(cmp0["char_id"][0]) == 0
    assert int(cmp0["stocks"][0]) == 0
    assert int(cmp0["action_id"][0]) == ACT_DEAD_DOWN
    assert int(cmp0["action_frame"][0]) == 0
    assert int(cmp0["animation_index"][0]) == 0
    assert int(cmp0["on_ground"][0]) == 1
    assert int(cmp0["ground_id"][0]) == 0
    assert float(cmp0["pos_x"][0]) == 0.0
    assert float(cmp0["pos_y"][0]) == 0.0
    assert float(cmp0["shield_hp"][0]) == 0.0
    assert int(cmp0["last_hit_by"][0]) == 0
    assert np.all(cmp0["state_flags"][0] == 0)


def test_zero_stock_live_death_animation_is_not_terminalized_before_timer_expiry() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(4)
    seed["is_teams"][0] = np.uint8(1)
    seed["team_id"][0, :4] = np.array([0, 1, 1, 0], dtype=np.uint8)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["stocks"][0, 0] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_DEAD_LEFT)
    seed["match_flow_timer"][0, 0] = np.uint8(2)
    seed["pos_x"][0, 0] = np.float32(-250.0)
    seed["pos_y"][0, 0] = np.float32(20.0)

    handle = msl_binding.init(batch_size=1, num_players=4)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        inp = _input_bytes(1, input_stride)
        msl_binding.step_input(handle, inp, inp)
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out)
        cmp0 = out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)

    assert int(cmp0["char_id"][0]) == CHAR_FOX
    assert int(cmp0["stocks"][0]) == 0
    assert int(cmp0["action_id"][0]) == ACT_DEAD_LEFT
    assert float(cmp0["pos_x"][0]) == -250.0


def test_zeroed_team_stock_pending_rebirth_restores_fighter_kind_at_timer_expiry() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(4)
    seed["is_teams"][0] = np.uint8(1)
    seed["team_id"][0, :4] = np.array([0, 1, 1, 0], dtype=np.uint8)
    seed["char_id"][0, 1] = np.uint8(0)
    seed["stocks"][0, 1] = np.uint8(0)
    seed["action_id"][0, 1] = np.uint16(ACT_DEAD_DOWN)
    seed["action_frame"][0, 1] = np.int16(0)
    seed["match_flow_timer"][0, 1] = np.uint8(1)
    seed["match_flow_pending_rebirth_char_id"][0, 1] = np.uint8(CHAR_FOX)
    seed["source_port0"][0, 1] = np.uint8(1)

    handle = msl_binding.init(batch_size=1, num_players=4)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        inp = _input_bytes(1, input_stride)
        msl_binding.step_input(handle, inp, inp)
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out)
        cmp0 = out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)

    assert int(cmp0["char_id"][1]) == CHAR_FOX
    assert int(cmp0["stocks"][1]) == 1
    assert int(cmp0["action_id"][1]) == ACT_REBIRTH
