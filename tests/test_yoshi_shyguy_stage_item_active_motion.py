from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset
from tools.slippi.make_dataset_from_slp import (
    _derive_yoshi_shyguy_dyn_y_phase,
    _derive_yoshi_shyguy_native_lanes,
    _derive_yoshi_shyguy_prev_vel_y,
    _derive_yoshi_shyguy_seed_lanes,
    _item_common_params,
)
from tools.slippi.known_data_artifacts import yoshi_shyguy_metadata


STAGE_YOSHIS_STORY = 8
ITEM_KIND_HEIHO = 0xD2


def _shyguy_params():
    return yoshi_shyguy_metadata(Path("data"))


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        ).copy()
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0], row["ref_t1"][0]
    finally:
        binding.destroy(handle)


def _step_seed(seed: np.ndarray) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert int(SEED_DTYPE.itemsize) == seed_stride
    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        prev_input = np.zeros((1,), dtype=INPUT_DTYPE)
        cur_input = np.zeros((1,), dtype=INPUT_DTYPE)
        prev_input_bytes = np.frombuffer(prev_input.tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        cur_input_bytes = np.frombuffer(cur_input.tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        binding.reseed_seed(handle, seed_bytes.copy())
        binding.step_input(handle, prev_input_bytes, cur_input_bytes)
        binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    finally:
        binding.destroy(handle)


def _empty_seed() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"] = np.uint32(STAGE_YOSHIS_STORY)
    seed["num_players"] = np.uint8(2)
    seed["match_damage_ratio"] = np.float32(1.0)
    seed["team_id"][0, :] = np.arange(4, dtype=np.uint8)
    seed["char_id"][0, 0] = np.uint8(1)
    seed["char_id"][0, 1] = np.uint8(22)
    seed["handicap"][0, :] = np.uint8(9)
    seed["attack_ratio"][0, :] = np.float32(1.0)
    seed["defense_ratio"][0, :] = np.float32(1.0)
    seed["facing"][0, :] = np.uint8(1)
    seed["facing_dir1"][0, :] = np.int8(1)
    seed["fighter_scale_y"][0, :] = np.float32(1.0)
    seed["item_reflect_damage_mul"][0, :] = np.float32(1.0)
    seed["item_reflect_transfer_port"][0, :] = np.uint8(0xFF)
    seed["item_hidden_body_hit_victim_port"][0, :] = np.uint8(0xFF)
    seed["items"][0, :]["owner"] = np.int8(-1)
    seed["stage_yoshi_shyguy_valid_u8"] = np.uint8(1)
    seed["stage_yoshi_shyguy_timer_u16"] = np.uint16(120)
    return seed


def _expected_shyguy_step_vel_y(seed: np.void, slot: int) -> float:
    cur = float(seed["items"][slot]["vel_y"])
    dyn_y_vel = _shyguy_params().dyn_y_vel
    if int(seed["item_shyguy_dyn_y_phase_valid_u8"][slot]) != 0:
        phase = int(seed["item_shyguy_dyn_y_phase_u8"][slot]) & 0xFF
        state = int(seed["items"][slot]["state"])
        if state == 4:
            if phase <= 29:
                expected_cur = 0.0
                next_vel = dyn_y_vel[0]
            else:
                q = (phase - 18) & 0xFF
                if q == 140:
                    expected_cur = -dyn_y_vel[0]
                elif q >= 141:
                    base = ((q - 141) & 0x3F) * 2
                    expected_cur = dyn_y_vel[base] + dyn_y_vel[(base + 1) & 0x7F]
                elif q == 12:
                    expected_cur = dyn_y_vel[0]
                else:
                    expected_cur = _state4_rate2_value(dyn_y_vel, q - 11)

                if q == 139:
                    next_vel = -dyn_y_vel[0]
                elif q >= 140:
                    base = ((q - 140) & 0x3F) * 2
                    next_vel = dyn_y_vel[base] + dyn_y_vel[(base + 1) & 0x7F]
                else:
                    next_vel = _state4_rate2_value(dyn_y_vel, q - 10)
            if abs(expected_cur - cur) <= 0.001:
                return next_vel
        else:
            expected_cur = _phase_value(dyn_y_vel, phase)
            if abs(expected_cur - cur) <= 0.001:
                return _phase_value(dyn_y_vel, phase + 1)
    if int(seed["item_shyguy_prev_vel_y_valid"][slot]) == 0:
        if int(seed["items"][slot]["state"]) == 1 and abs(cur) <= 0.001:
            return dyn_y_vel[0]
        return cur
    prev = float(seed["item_shyguy_prev_vel_y"][slot])
    for i, value in enumerate(dyn_y_vel):
        if abs(value - cur) <= 0.001 and abs(dyn_y_vel[(i - 1) % len(dyn_y_vel)] - prev) <= 0.001:
            return dyn_y_vel[(i + 1) % len(dyn_y_vel)]
    return cur


def _phase_value(dyn_y_vel: tuple[float, ...], phase: int) -> float:
    phase &= 0xFF
    if phase == 0:
        return 0.0
    if phase <= len(dyn_y_vel):
        return dyn_y_vel[phase - 1]
    return dyn_y_vel[256 - phase]


def _state4_rate2_value(dyn_y_vel: tuple[float, ...], phase: int) -> float:
    phase &= 0xFF
    if phase == 0:
        return 0.0
    if phase == 1:
        return _phase_value(dyn_y_vel, 1)
    base = (2 * phase) - 2
    return _phase_value(dyn_y_vel, base) + _phase_value(dyn_y_vel, base + 1)


def _expected_shyguy_step_vel_x(seed: np.void, slot: int) -> float:
    item = seed["items"][slot]
    if int(seed["item_shyguy_speed_index_valid_u8"][slot]) == 0:
        return float(item["vel_x"])
    speeds = _shyguy_params().speed
    speed = speeds[int(seed["item_shyguy_speed_index_u8"][slot]) % 3] * float(item["direction"])
    if int(item["state"]) == 4:
        speed *= _shyguy_params().state4_speed_mul
    return speed


def test_yoshi_shyguy_prev_vel_y_derivation_is_prefix_causal() -> None:
    items = np.zeros((3, 15), dtype=SEED_DTYPE["items"].base)
    items["owner"] = np.int8(-1)
    for frame, vel_y in enumerate((0.25, 0.5, -99.0)):
        item = items[frame, 0]
        item["exists"] = np.uint8(1)
        item["state"] = np.uint8(1)
        item["type"] = np.uint16(ITEM_KIND_HEIHO)
        item["owner"] = np.int8(-1)
        item["spawn_id"] = np.uint32(77)
        item["vel_y"] = np.float32(vel_y)

    prev_vel_y, valid = _derive_yoshi_shyguy_prev_vel_y(items)
    assert int(valid[1, 0]) == 1
    assert float(prev_vel_y[1, 0]) == pytest.approx(0.25)

    items[2, 0]["vel_y"] = np.float32(123.0)
    prev_vel_y_after, valid_after = _derive_yoshi_shyguy_prev_vel_y(items)
    assert int(valid_after[1, 0]) == 1
    assert float(prev_vel_y_after[1, 0]) == pytest.approx(0.25)


def test_yoshi_shyguy_dyn_y_phase_derivation_is_prefix_causal() -> None:
    items = np.zeros((4, 15), dtype=SEED_DTYPE["items"].base)
    items["owner"] = np.int8(-1)
    for frame, vel_y in enumerate((0.0, 0.7028961182, 0.7015228271, -99.0)):
        item = items[frame, 0]
        item["exists"] = np.uint8(1)
        item["state"] = np.uint8(1)
        item["type"] = np.uint16(ITEM_KIND_HEIHO)
        item["owner"] = np.int8(-1)
        item["spawn_id"] = np.uint32(77)
        item["vel_y"] = np.float32(vel_y)

    phase, valid = _derive_yoshi_shyguy_dyn_y_phase(items)
    assert int(valid[1, 0]) == 1
    assert int(phase[1, 0]) == 1
    assert int(valid[2, 0]) == 1
    assert int(phase[2, 0]) == 2

    items[3, 0]["vel_y"] = np.float32(123.0)
    phase_after, valid_after = _derive_yoshi_shyguy_dyn_y_phase(items)
    assert int(valid_after[2, 0]) == 1
    assert int(phase_after[2, 0]) == 2


def test_yoshi_shyguy_native_derivation_shapes_and_known_rows() -> None:
    pytest.importorskip("msl_binding")
    params = _shyguy_params()
    items = np.zeros((4, 15), dtype=SEED_DTYPE["items"].base)
    items["owner"] = np.int8(-1)
    for frame, vel_y in enumerate((0.0, params.dyn_y_vel[0], params.dyn_y_vel[1], -99.0)):
        item = items[frame, 0]
        item["exists"] = np.uint8(1)
        item["state"] = np.uint8(1)
        item["type"] = np.uint16(ITEM_KIND_HEIHO)
        item["owner"] = np.int8(-1)
        item["spawn_id"] = np.uint32(77)
        item["vel_x"] = np.float32(0.3)
        item["vel_y"] = np.float32(vel_y)
        item["pos_x"] = np.float32(-292.0)
        item["pos_y"] = np.float32(params.vpos[0])

    falling = items[1, 1]
    falling["exists"] = np.uint8(1)
    falling["state"] = np.uint8(3)
    falling["type"] = np.uint16(ITEM_KIND_HEIHO)
    falling["owner"] = np.int8(-1)
    falling["spawn_id"] = np.uint32(78)
    falling["damage"] = np.uint16(12)

    lanes = _derive_yoshi_shyguy_native_lanes(items, stage_id=STAGE_YOSHIS_STORY)
    assert len(lanes) == 13
    prev_vel_y, prev_valid, phase, phase_valid, timer, pattern, stage_valid, *_rest = lanes
    hitlag = lanes[11]
    hitlag_valid = lanes[12]

    assert prev_vel_y.shape == (4, 15)
    assert prev_vel_y.dtype == np.float32
    assert prev_valid.dtype == np.uint8
    assert phase.dtype == np.uint8
    assert phase_valid.dtype == np.uint8
    assert timer.dtype == np.uint16
    assert pattern.dtype == np.uint8
    assert stage_valid.dtype == np.uint8

    assert int(prev_valid[1, 0]) == 1
    assert float(prev_vel_y[1, 0]) == pytest.approx(0.0)
    assert int(phase_valid[1, 0]) == 1
    assert int(phase[1, 0]) == 1
    assert int(phase[2, 0]) == 2
    assert int(stage_valid[0]) == 1
    assert int(pattern[0]) == 0
    assert int(hitlag_valid[1, 1]) == 1
    assert int(hitlag[1, 1]) > 0


def test_yoshi_shyguy_native_derivation_rejects_mismatched_item_width() -> None:
    binding = pytest.importorskip("msl_binding")
    params = _shyguy_params()
    common = _item_common_params()
    items = np.zeros((2, 15), dtype=SEED_DTYPE["items"].base)
    items["owner"] = np.int8(-1)

    with pytest.raises(ValueError):
        binding.derive_yoshi_shyguy_seed_lanes(
            np.ascontiguousarray(items["exists"], dtype=np.uint8),
            np.ascontiguousarray(items["type"][:, :14], dtype=np.uint16),
            np.ascontiguousarray(items["owner"], dtype=np.int8),
            np.ascontiguousarray(items["state"], dtype=np.uint8),
            np.ascontiguousarray(items["spawn_id"], dtype=np.uint32),
            np.ascontiguousarray(items["instance_id"], dtype=np.uint16),
            np.ascontiguousarray(items["vel_x"], dtype=np.float32),
            np.ascontiguousarray(items["vel_y"], dtype=np.float32),
            np.ascontiguousarray(items["pos_x"], dtype=np.float32),
            np.ascontiguousarray(items["pos_y"], dtype=np.float32),
            np.ascontiguousarray(items["damage"], dtype=np.uint16),
            STAGE_YOSHIS_STORY,
            int(params.stage_id),
            int(params.item_kind),
            int(params.timer_reset),
            int(params.spawn_delay_step),
            float(params.state4_speed_mul),
            np.asarray(params.vpos, dtype=np.float32),
            np.asarray(params.speed, dtype=np.float32),
            np.asarray(params.dyn_y_vel, dtype=np.float32),
            float(common["item_hitlag_damage_mul"]),
            float(common["item_hitlag_base"]),
        )


def test_yoshi_shyguy_state3_damage_reset_restarts_return_delay_lane() -> None:
    pytest.importorskip("msl_binding")
    items = np.zeros((5, 15), dtype=SEED_DTYPE["items"].base)
    items["owner"] = np.int8(-1)
    for frame, (state, damage) in enumerate(((1, 0), (3, 2), (3, 2), (3, 4), (3, 4))):
        item = items[frame, 0]
        item["exists"] = np.uint8(1)
        item["state"] = np.uint8(state)
        item["type"] = np.uint16(ITEM_KIND_HEIHO)
        item["owner"] = np.int8(-1)
        item["spawn_id"] = np.uint32(123)
        item["vel_x"] = np.float32(0.5)
        item["vel_y"] = np.float32(-1.77)
        item["pos_x"] = np.float32(8.0)
        item["pos_y"] = np.float32(20.0)
        item["damage"] = np.uint16(damage)

    lanes = _derive_yoshi_shyguy_native_lanes(items, stage_id=STAGE_YOSHIS_STORY)
    delay, delay_valid = lanes[9], lanes[10]
    hitlag, hitlag_valid = lanes[11], lanes[12]

    assert int(delay_valid[1, 0]) == 1
    assert int(delay[1, 0]) == 12
    assert int(delay[2, 0]) == 12
    assert int(delay_valid[3, 0]) == 1
    assert int(delay[3, 0]) == 12
    assert int(hitlag_valid[3, 0]) == 1
    assert int(hitlag[3, 0]) > 0


def test_yoshi_shyguy_stage_timer_derivation_is_prefix_causal() -> None:
    items = np.zeros((3, 15), dtype=SEED_DTYPE["items"].base)
    items["owner"] = np.int8(-1)
    items[2, 0]["exists"] = np.uint8(1)
    items[2, 0]["state"] = np.uint8(1)
    items[2, 0]["type"] = np.uint16(ITEM_KIND_HEIHO)
    items[2, 0]["owner"] = np.int8(-1)
    items[2, 0]["spawn_id"] = np.uint32(50)
    items[2, 0]["pos_x"] = np.float32(-292.0)
    items[2, 0]["pos_y"] = np.float32(60.0)

    timer, pattern, valid, *_ = _derive_yoshi_shyguy_seed_lanes(items, stage_id=STAGE_YOSHIS_STORY)
    assert list(timer[:2]) == [119, 118]
    assert int(valid[1]) == 1

    items[2, 0]["pos_x"] = np.float32(304.0)
    items[2, 0]["pos_y"] = np.float32(0.0)
    timer_after, pattern_after, valid_after, *_ = _derive_yoshi_shyguy_seed_lanes(
        items, stage_id=STAGE_YOSHIS_STORY
    )
    assert int(timer_after[1]) == 118
    assert int(pattern_after[1]) == int(pattern[1])
    assert int(valid_after[1]) == 1


def test_yoshi_shyguy_dynamic_prev_vel_y_updates_active_motion() -> None:
    seed = _empty_seed()
    item = seed["items"][0, 0]
    item["exists"] = np.uint8(1)
    item["state"] = np.uint8(1)
    item["type"] = np.uint16(ITEM_KIND_HEIHO)
    item["owner"] = np.int8(-1)
    item["spawn_id"] = np.uint32(77)
    item["direction"] = np.float32(1.0)
    item["vel_x"] = np.float32(0.75)
    item["vel_y"] = np.float32(0.6644439697)
    item["pos_x"] = np.float32(10.0)
    item["pos_y"] = np.float32(20.0)
    seed["item_shyguy_prev_vel_y"][0, 0] = np.float32(0.6534576416)
    seed["item_shyguy_prev_vel_y_valid"][0, 0] = np.uint8(1)

    out = _step_seed(seed)
    expected_vel_y = 0.6740570068
    assert float(out["items"][0]["vel_y"]) == pytest.approx(expected_vel_y, abs=1e-6)
    assert float(out["items"][0]["pos_y"]) == pytest.approx(20.0 + expected_vel_y, abs=1e-6)
    assert float(out["items"][0]["pos_x"]) == pytest.approx(10.75, abs=1e-6)


def test_yoshi_shyguy_state0_delay_transitions_without_active_motion() -> None:
    seed = _empty_seed()
    item = seed["items"][0, 0]
    item["exists"] = np.uint8(1)
    item["state"] = np.uint8(0)
    item["type"] = np.uint16(ITEM_KIND_HEIHO)
    item["owner"] = np.int8(-1)
    item["spawn_id"] = np.uint32(77)
    item["direction"] = np.float32(-1.0)
    item["pos_x"] = np.float32(304.0)
    item["pos_y"] = np.float32(20.0)
    seed["item_shyguy_delay_valid_u8"][0, 0] = np.uint8(1)
    seed["item_shyguy_delay_u16"][0, 0] = np.uint16(0)
    seed["item_shyguy_speed_index_valid_u8"][0, 0] = np.uint8(1)
    seed["item_shyguy_speed_index_u8"][0, 0] = np.uint8(0)

    out = _step_seed(seed)
    assert int(out["items"][0]["state"]) == 1
    assert float(out["items"][0]["pos_x"]) == pytest.approx(304.0, abs=1e-6)
    assert float(out["items"][0]["vel_x"]) == pytest.approx(0.0, abs=1e-6)


def test_yoshi_shyguy_state1_entry_samples_first_dynamic_delta() -> None:
    seed = _empty_seed()
    item = seed["items"][0, 0]
    item["exists"] = np.uint8(1)
    item["state"] = np.uint8(1)
    item["type"] = np.uint16(ITEM_KIND_HEIHO)
    item["owner"] = np.int8(-1)
    item["spawn_id"] = np.uint32(77)
    item["direction"] = np.float32(1.0)
    item["vel_y"] = np.float32(0.0)
    item["pos_y"] = np.float32(20.0)

    out = _step_seed(seed)
    first_dyn_y = _shyguy_params().dyn_y_vel[0]
    assert float(out["items"][0]["vel_y"]) == pytest.approx(first_dyn_y, abs=1e-6)
    assert float(out["items"][0]["pos_y"]) == pytest.approx(
        20.0 + first_dyn_y, abs=1e-6
    )


def test_yoshi_shyguy_state3_zero_delay_enters_return_flight() -> None:
    seed = _empty_seed()
    item = seed["items"][0, 0]
    item["exists"] = np.uint8(1)
    item["type"] = np.uint16(ITEM_KIND_HEIHO)
    item["state"] = np.uint8(3)
    item["owner"] = np.int8(-1)
    item["pos_x"] = np.float32(12.0)
    item["pos_y"] = np.float32(40.0)
    item["vel_x"] = np.float32(0.5)
    item["vel_y"] = np.float32(-2.0)
    item["direction"] = np.float32(-1.0)
    item["damage"] = np.uint16(8)
    seed["item_shyguy_delay_u16"][0, 0] = np.uint16(0)
    seed["item_shyguy_delay_valid_u8"][0, 0] = np.uint8(1)
    seed["item_shyguy_hitlag_u8"][0, 0] = np.uint8(0)
    seed["item_shyguy_hitlag_valid_u8"][0, 0] = np.uint8(1)

    out = _step_seed(seed)
    out_item = out["items"][0]
    assert int(out_item["exists"]) == 1
    assert int(out_item["type"]) == ITEM_KIND_HEIHO
    assert int(out_item["state"]) == 4
    assert float(out_item["pos_x"]) == pytest.approx(12.0, abs=1e-6)
    assert float(out_item["pos_y"]) == pytest.approx(40.0, abs=1e-6)
    assert float(out_item["vel_x"]) == pytest.approx(0.0, abs=1e-6)
    assert float(out_item["vel_y"]) == pytest.approx(0.0, abs=1e-6)
    assert float(out_item["direction"]) == pytest.approx(1.0, abs=1e-6)


def test_yoshi_shyguy_state3_positive_delay_keeps_falling() -> None:
    seed = _empty_seed()
    item = seed["items"][0, 0]
    item["exists"] = np.uint8(1)
    item["type"] = np.uint16(ITEM_KIND_HEIHO)
    item["state"] = np.uint8(3)
    item["owner"] = np.int8(-1)
    item["pos_x"] = np.float32(12.0)
    item["pos_y"] = np.float32(40.0)
    item["vel_x"] = np.float32(0.5)
    item["vel_y"] = np.float32(-2.0)
    item["direction"] = np.float32(-1.0)
    item["damage"] = np.uint16(5)
    seed["item_shyguy_delay_u16"][0, 0] = np.uint16(1)
    seed["item_shyguy_delay_valid_u8"][0, 0] = np.uint8(1)
    seed["item_shyguy_hitlag_u8"][0, 0] = np.uint8(0)
    seed["item_shyguy_hitlag_valid_u8"][0, 0] = np.uint8(1)

    out = _step_seed(seed)
    out_item = out["items"][0]
    assert int(out_item["exists"]) == 1
    assert int(out_item["type"]) == ITEM_KIND_HEIHO
    assert int(out_item["state"]) == 3
    assert float(out_item["pos_x"]) == pytest.approx(12.5, abs=1e-6)
    assert float(out_item["vel_y"]) == pytest.approx(-2.12, abs=1e-5)


def test_yoshi_shyguy_falling_state_clears_on_generic_item_blast_bounds() -> None:
    seed = _empty_seed()
    item = seed["items"][0, 0]
    item["exists"] = np.uint8(1)
    item["type"] = np.uint16(ITEM_KIND_HEIHO)
    item["state"] = np.uint8(2)
    item["owner"] = np.int8(-1)
    item["pos_x"] = np.float32(171.808044)
    item["pos_y"] = np.float32(-1.16848)
    item["vel_x"] = np.float32(2.4214478)
    item["vel_y"] = np.float32(-2.2)
    item["direction"] = np.float32(1.0)
    seed["item_shyguy_hitlag_u8"][0, 0] = np.uint8(0)
    seed["item_shyguy_hitlag_valid_u8"][0, 0] = np.uint8(1)

    # State 2/3 Heiho uses the generic item destroy gate after Phys/integration:
    # it_802D8EC8 sets xDCC_flag.b3, then Item_802697D4 calls Item_802696CC. This is different
    # from active state 1/4's it_802D9714 20-unit return margin.
    # refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,itHeiho_UnkMotion2_Phys}
    # refs/melee/src/melee/it/item.c::{Item_802697D4,Item_802696CC}
    out = _step_seed(seed)
    assert int(out["items"][0]["exists"]) == 0
    assert int(out["items"][0]["type"]) == 0


@pytest.mark.integration
def test_yoshi_shyguy_falling_state_blast_clear_replay_real_cnm_5275() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    out, ref = _step_one_row(dataset_path, 5275)
    slot = 3
    assert int(ref["items"][slot]["exists"]) == 0
    assert int(out["items"][slot]["exists"]) == int(ref["items"][slot]["exists"])
    assert int(out["items"][slot]["type"]) == int(ref["items"][slot]["type"])


def test_yoshi_shyguy_stage_timer_spawns_without_future_items() -> None:
    seed = _empty_seed()
    seed["frame_pre_random_seed"] = np.uint32(0x77C154)
    seed["stage_yoshi_shyguy_timer_u16"] = np.uint16(0)
    seed["stage_yoshi_shyguy_pattern_u8"] = np.uint8(0)
    seed["stage_yoshi_shyguy_valid_u8"] = np.uint8(1)

    out = _step_seed(seed)
    assert int(out["items"][0]["exists"]) == 1
    assert int(out["items"][0]["type"]) == ITEM_KIND_HEIHO
    assert int(out["items"][0]["owner"]) == -1
    assert int(out["items"][0]["state"]) == 1
    pos_x = float(out["items"][0]["pos_x"])
    assert min(abs(pos_x + 292.0), abs(pos_x - 304.0)) <= 1e-6


def test_yoshi_shyguy_dynamic_prev_vel_y_does_not_touch_other_stages() -> None:
    seed = _empty_seed()
    seed["stage_id"] = np.uint32(32)
    item = seed["items"][0, 0]
    item["exists"] = np.uint8(1)
    item["state"] = np.uint8(1)
    item["type"] = np.uint16(ITEM_KIND_HEIHO)
    item["owner"] = np.int8(-1)
    item["spawn_id"] = np.uint32(88)
    item["vel_y"] = np.float32(0.25)
    item["pos_y"] = np.float32(20.0)
    seed["item_shyguy_prev_vel_y"][0, 0] = np.float32(0.20)
    seed["item_shyguy_prev_vel_y_valid"][0, 0] = np.uint8(1)

    out = _step_seed(seed)
    assert float(out["items"][0]["pos_y"]) == pytest.approx(20.0, abs=1e-6)
    assert float(out["items"][0]["vel_y"]) == pytest.approx(0.25, abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "slot"),
    [
        (1201, 0),
        (6354, 1),
    ],
)
def test_yoshi_shyguy_laser_item_hit_enters_damage_state(record: int, slot: int) -> None:
    # Replay-real locks for stage-object item-vs-item damage:
    # - Falco laser item HitCapsule intersects the Heiho item hurtbox.
    # - it_802706D0 records the item hit and Item_8026A294 dispatches Heiho dmg_received.
    # - Low cumulative damage enters state 3 through it_802D8EC8 / it_8027B798 and consumes the
    #   laser article.
    # refs/melee/src/melee/it/itcoll.c::{it_802706D0,it_80270E30}
    # refs/melee/src/melee/it/items/itheiho.c::it_802D8EC8
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    out, ref = _step_one_row(dataset_path, record)
    assert int(out["items"][slot]["exists"]) == 1
    assert int(out["items"][slot]["type"]) == ITEM_KIND_HEIHO
    assert int(out["items"][slot]["state"]) == 3
    assert int(out["items"][slot]["state"]) == int(ref["items"][slot]["state"])
    assert float(out["items"][slot]["vel_x"]) == pytest.approx(
        float(ref["items"][slot]["vel_x"]), abs=1e-5
    )
    assert float(out["items"][slot]["vel_y"]) == pytest.approx(
        float(ref["items"][slot]["vel_y"]), abs=1e-5
    )
    assert not any(int(item["exists"]) and int(item["type"]) == 55 for item in out["items"])


def test_yoshi_shyguy_laser_item_hit_does_not_admit_far_or_non_yoshi_rows() -> None:
    seed = _empty_seed()
    seed["items"][0, 0]["exists"] = np.uint8(1)
    seed["items"][0, 0]["type"] = np.uint16(ITEM_KIND_HEIHO)
    seed["items"][0, 0]["state"] = np.uint8(1)
    seed["items"][0, 0]["owner"] = np.int8(-1)
    seed["items"][0, 0]["pos_x"] = np.float32(0.0)
    seed["items"][0, 0]["pos_y"] = np.float32(0.0)
    seed["items"][0, 0]["direction"] = np.float32(1.0)
    seed["items"][0, 1]["exists"] = np.uint8(1)
    seed["items"][0, 1]["type"] = np.uint16(55)
    seed["items"][0, 1]["state"] = np.uint8(0)
    seed["items"][0, 1]["owner"] = np.int8(0)
    seed["items"][0, 1]["instance_id"] = np.uint16(77)
    seed["items"][0, 1]["spawn_id"] = np.uint32(77)
    seed["items"][0, 1]["pos_x"] = np.float32(100.0)
    seed["items"][0, 1]["pos_y"] = np.float32(0.0)
    seed["items"][0, 1]["vel_x"] = np.float32(5.0)
    seed["items"][0, 1]["timer"] = np.float32(80.0)
    seed["items"][0, 1]["direction"] = np.float32(1.0)
    seed["char_id"][0, 0] = np.uint8(22)

    out = _step_seed(seed)
    assert any(
        int(item["exists"]) and int(item["type"]) == ITEM_KIND_HEIHO and int(item["state"]) == 1
        for item in out["items"]
    )
    assert any(int(item["exists"]) and int(item["type"]) == 55 for item in out["items"])

    seed["stage_id"] = np.uint32(32)
    seed["items"][0, 1]["pos_x"] = np.float32(0.0)
    out_non_yoshi = _step_seed(seed)
    assert any(int(item["exists"]) and int(item["type"]) == 55 for item in out_non_yoshi["items"])


def test_yoshi_shyguy_reflected_laser_item_hit_uses_reflected_damage_lane() -> None:
    seed = _empty_seed()
    shy = seed["items"][0, 0]
    shy["exists"] = np.uint8(1)
    shy["type"] = np.uint16(ITEM_KIND_HEIHO)
    shy["state"] = np.uint8(1)
    shy["owner"] = np.int8(-1)
    shy["spawn_id"] = np.uint32(88)
    shy["pos_x"] = np.float32(0.0)
    shy["pos_y"] = np.float32(0.0)
    shy["direction"] = np.float32(1.0)

    laser = seed["items"][0, 1]
    laser["exists"] = np.uint8(1)
    laser["type"] = np.uint16(55)
    laser["state"] = np.uint8(0)
    laser["owner"] = np.int8(0)
    laser["instance_id"] = np.uint16(77)
    laser["spawn_id"] = np.uint32(77)
    laser["pos_x"] = np.float32(-1.0)
    laser["pos_y"] = np.float32(0.0)
    laser["vel_x"] = np.float32(1.0)
    laser["timer"] = np.float32(80.0)
    laser["direction"] = np.float32(1.0)
    seed["char_id"][0, 0] = np.uint8(22)

    raw_out = _step_seed(seed)
    assert int(raw_out["items"][0]["damage"]) == 3
    assert int(raw_out["items"][0]["damage"]) < int(_shyguy_params().damage_threshold * 0.8)
    assert int(raw_out["items"][0]["state"]) == 3

    reflected_seed = seed.copy()
    reflected_seed["item_reflect_damage_mul"][0, 1] = np.float32(5.0)
    reflected_out = _step_seed(reflected_seed)
    # Item_80269F14 / it_80272460 normalize reflected item damage before any item damage callback
    # consumes it. The Heiho item-vs-item path must feed that reflected lane into cumulative damage,
    # knockback/hitlag, and the it_802D8EC8 high-damage threshold branch.
    assert int(reflected_out["items"][0]["damage"]) == 15
    assert int(reflected_out["items"][0]["state"]) == 2
    assert not any(int(item["exists"]) and int(item["type"]) == 55 for item in reflected_out["items"])


@pytest.mark.integration
def test_active_yoshi_shyguy_integrates_visible_velocity() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    try:
        ds = read_dataset(str(dataset_path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            pytest.skip(f"stale local dataset cache: {exc}")
        raise
    record = 1235
    row = ds.samples[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]

    # Replay-real active Shy Guys: kind 0xD2, owner=-1, state 1. The admitted causal slice is the
    # generic item-position integration with current visible x40_vel; hidden dynamic-bone velocity
    # remains a separate open owner, so pos_y is asserted as the causal integration formula rather
    # than replay parity.
    # refs/melee/src/melee/it/items/itheiho.c::itHeiho_UnkMotion1_Phys
    # refs/melee/src/melee/it/item.c::Item_802697D4
    active_slots = [
        it
        for it, item in enumerate(seed["items"])
        if int(item["exists"])
        and int(item["type"]) == 0xD2
        and int(item["owner"]) == -1
        and int(item["state"]) == 1
    ]
    assert active_slots[:5] == [0, 1, 2, 3, 4]

    out, _ = _step_one_row(dataset_path, record)
    for it in active_slots[:5]:
        item = seed["items"][it]
        vel_x = _expected_shyguy_step_vel_x(seed, it)
        vel_y = _expected_shyguy_step_vel_y(seed, it)
        assert float(out["items"][it]["pos_x"]) == pytest.approx(
            float(item["pos_x"]) + vel_x, abs=1e-6
        )
        assert float(out["items"][it]["pos_y"]) == pytest.approx(
            float(item["pos_y"]) + vel_y, abs=1e-5
        )
        assert float(out["items"][it]["pos_x"]) == pytest.approx(
            float(ref["items"][it]["pos_x"]), abs=1e-6
        )


@pytest.mark.integration
def test_state4_yoshi_shyguy_integrates_visible_velocity() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    try:
        ds = read_dataset(str(dataset_path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            pytest.skip(f"stale local dataset cache: {exc}")
        raise
    record = 614
    row = ds.samples[record]
    seed = row["seed_t"]

    # refs/melee/src/melee/it/items/itheiho.c::itHeiho_UnkMotion4_Phys
    # refs/melee/src/melee/it/item.c::Item_802697D4
    active_slots = [
        it
        for it, item in enumerate(seed["items"])
        if int(item["exists"])
        and int(item["type"]) == 0xD2
        and int(item["owner"]) == -1
        and int(item["state"]) == 4
    ]
    assert active_slots == [3]

    out, _ = _step_one_row(dataset_path, record)
    for it in active_slots:
        item = seed["items"][it]
        vel_x = _expected_shyguy_step_vel_x(seed, it)
        vel_y = _expected_shyguy_step_vel_y(seed, it)
        assert float(out["items"][it]["pos_x"]) == pytest.approx(
            float(item["pos_x"]) + vel_x, abs=1e-6
        )
        assert float(out["items"][it]["pos_y"]) == pytest.approx(
            float(item["pos_y"]) + vel_y, abs=1e-6
        )


@pytest.mark.integration
def test_yoshi_shyguy_reconstructed_phase_handles_aobj_loop_rows() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    try:
        ds = read_dataset(str(dataset_path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            pytest.skip(f"stale local dataset cache: {exc}")
        raise

    # State 4 row 667 is after the x24 return-flight prefix. The raw active-frame modulo phase
    # selects the wrong side of the looping child AObj; preprocessing reconstructs the hidden phase
    # from replay-prefix prev/current x40_vel.y and the extracted GrSt.dat FObj deltas.
    # refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion4_Anim,it_802D98C4}
    out, ref = _step_one_row(dataset_path, 667)
    assert float(out["items"][3]["pos_y"]) == pytest.approx(
        float(ref["items"][3]["pos_y"]), abs=1e-6
    )


@pytest.mark.integration
def test_yoshi_shyguy_state3_zero_delay_rows_do_not_over_enter_state4() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    try:
        ds = read_dataset(str(dataset_path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            pytest.skip(f"stale local dataset cache: {exc}")
        raise

    items_for_derivation = np.empty(
        (int(ds.samples.shape[0]) + 1, 15), dtype=SEED_DTYPE["items"].base
    )
    items_for_derivation[:-1] = ds.samples["seed_t"]["items"]
    items_for_derivation[-1] = ds.samples["ref_t1"][-1]["items"]
    fresh_lanes = _derive_yoshi_shyguy_native_lanes(
        items_for_derivation, stage_id=STAGE_YOSHIS_STORY
    )
    fresh_delay, fresh_delay_valid = fresh_lanes[9], fresh_lanes[10]

    # These replay-real rows lock the rejected over-broad state-3 x24 reconstruction. Repeated
    # low-damage callbacks reset source x24 while staying in state 3, so zero alone is not a
    # sufficient causal transition predicate.
    # refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,itHeiho_UnkMotion3_Phys,it_802D9168}
    for record in (6124, 6133):
        row = ds.samples[record]
        seed = row["seed_t"]
        assert int(seed["items"][1]["type"]) == ITEM_KIND_HEIHO
        assert int(seed["items"][1]["state"]) == 3
        if (
            int(seed["item_shyguy_delay_u16"][1]) != int(fresh_delay[record, 1])
            or int(seed["item_shyguy_delay_valid_u8"][1]) != int(fresh_delay_valid[record, 1])
        ):
            pytest.skip("local dataset cache predates Shy Guy repeated-damage x24 derivation")
        out, ref = _step_one_row(dataset_path, record)
        assert int(out["items"][1]["state"]) == int(ref["items"][1]["state"]) == 3


@pytest.mark.integration
@pytest.mark.parametrize(("record", "slot"), [(613, 3), (2733, 4)])
def test_yoshi_shyguy_state3_zero_delay_enters_return_flight_replay_real(
    record: int, slot: int
) -> None:
    dataset_path = Path(
        "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")
    seed = read_dataset(str(dataset_path)).samples[record]["seed_t"]
    assert int(seed["items"][slot]["type"]) == ITEM_KIND_HEIHO
    assert int(seed["items"][slot]["state"]) == 3
    assert int(seed["item_shyguy_delay_valid_u8"][slot]) == 1
    assert int(seed["item_shyguy_delay_u16"][slot]) == 0

    out, ref = _step_one_row(dataset_path, record)
    assert int(out["items"][slot]["state"]) == int(ref["items"][slot]["state"]) == 4
    assert float(out["items"][slot]["pos_x"]) == pytest.approx(
        float(ref["items"][slot]["pos_x"]), abs=1e-6
    )
    assert float(out["items"][slot]["pos_y"]) == pytest.approx(
        float(ref["items"][slot]["pos_y"]), abs=1e-6
    )


def test_yoshi_shyguy_state2_uses_item_max_fall_speed() -> None:
    dataset_path = Path(
        "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    )
    record = 5264
    slot = 3
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    item = seed["items"][slot]
    assert int(item["type"]) == ITEM_KIND_HEIHO
    assert int(item["state"]) == 2
    assert float(item["vel_y"]) <= -float(_shyguy_params().fall_speed_max)

    out, ref = _step_one_row(dataset_path, record)
    assert float(out["items"][slot]["pos_y"]) == pytest.approx(float(ref["items"][slot]["pos_y"]))
    assert float(out["items"][slot]["vel_y"]) == pytest.approx(float(ref["items"][slot]["vel_y"]))
