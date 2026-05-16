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
ACT_DAMAGE_FLY_TOP = 90
ROLLOUT_CLOCK_REPLAY_FRAME_SEED = 2


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


def _field_bytes(samples: np.ndarray, record: int, field: str, stride: int) -> np.ndarray:
    return np.frombuffer(samples[record : record + 1][field].tobytes(order="C"), dtype=np.uint8).reshape(
        1, stride
    ).copy()


def _run_rollout_to_record(
    dataset_path: Path, *, start_record: int, target_record: int, return_clock_mode: bool = False
) -> tuple[np.void, np.void] | tuple[np.void, np.void, int]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(handle, _field_bytes(samples, start_record, "seed_t", seed_stride))
        for record in range(start_record, target_record + 1):
            binding.step_input(
                handle,
                _field_bytes(samples, record, "prev_input_t", input_stride),
                _field_bytes(samples, record, "input_t", input_stride),
            )
            binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        ref = samples[target_record]["ref_t1"]
        if return_clock_mode:
            return out, ref, int(binding.debug_get_rollout_clock_mode(handle, 0))
        return out, ref
    finally:
        binding.destroy(handle)


def _run_rollout_samples_to_record(
    samples: np.ndarray, *, num_players: int, start_record: int, target_record: int
) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=num_players,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(
            handle, _field_bytes(samples, start_record, "seed_t", seed_stride)
        )
        for record in range(start_record, target_record + 1):
            binding.step_input(
                handle,
                _field_bytes(samples, record, "prev_input_t", input_stride),
                _field_bytes(samples, record, "input_t", input_stride),
            )
            binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        ref = samples[target_record]["ref_t1"].copy()
        return out, ref
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


def _rollout_clock_mode_for_seed(seed: np.ndarray) -> int:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])

    assert int(SEED_DTYPE.itemsize) == seed_stride
    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        binding.reseed_seed_rollout(handle, seed_bytes.copy())
        return int(binding.debug_get_rollout_clock_mode(handle, 0))
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
    (
        prev_vel_y,
        prev_valid,
        phase,
        phase_valid,
        timer,
        pattern,
        stage_valid,
        *_rest,
    ) = lanes
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


def test_yoshi_shyguy_stage_pattern_latches_new_spawn_group_only() -> None:
    items = np.zeros((4, 15), dtype=SEED_DTYPE["items"].base)
    items["owner"] = np.int8(-1)

    first = items[1, 0]
    first["exists"] = np.uint8(1)
    first["state"] = np.uint8(1)
    first["type"] = np.uint16(ITEM_KIND_HEIHO)
    first["owner"] = np.int8(-1)
    first["spawn_id"] = np.uint32(50)
    first["pos_x"] = np.float32(-292.0)
    first["pos_y"] = np.float32(60.0)

    moved = items[2, 0]
    moved["exists"] = np.uint8(1)
    moved["state"] = np.uint8(1)
    moved["type"] = np.uint16(ITEM_KIND_HEIHO)
    moved["owner"] = np.int8(-1)
    moved["spawn_id"] = np.uint32(50)
    moved["pos_x"] = np.float32(304.0)
    moved["pos_y"] = np.float32(75.0)

    timer, pattern, valid, *_ = _derive_yoshi_shyguy_seed_lanes(items, stage_id=STAGE_YOSHIS_STORY)

    assert int(valid[3]) == 1
    assert int(pattern[1]) == 2
    assert int(pattern[2]) == 2
    assert int(pattern[3]) == 2
    assert int(timer[3]) == 120


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


def test_yoshi_shyguy_post_reset_export_restarts_child_delta() -> None:
    seed = _empty_seed()
    item = seed["items"][0, 0]
    item["exists"] = np.uint8(1)
    item["state"] = np.uint8(1)
    item["type"] = np.uint16(ITEM_KIND_HEIHO)
    item["owner"] = np.int8(-1)
    item["spawn_id"] = np.uint32(52)
    item["direction"] = np.float32(1.0)
    item["vel_x"] = np.float32(0.0)
    item["vel_y"] = np.float32(10.828628540039062)
    item["pos_x"] = np.float32(1.1003704071044922)
    item["pos_y"] = np.float32(47.06268310546875)
    seed["item_shyguy_speed_index_u8"][0, 0] = np.uint8(0)
    seed["item_shyguy_speed_index_valid_u8"][0, 0] = np.uint8(1)
    seed["item_shyguy_prev_vel_y"][0, 0] = np.float32(-0.5381011962890625)
    seed["item_shyguy_prev_vel_y_valid"][0, 0] = np.uint8(1)
    seed["item_shyguy_dyn_y_phase_u8"][0, 0] = np.uint8(209)
    seed["item_shyguy_dyn_y_phase_valid_u8"][0, 0] = np.uint8(1)

    out = _step_seed(seed)
    first_dyn_y = _shyguy_params().dyn_y_vel[0]
    assert float(out["items"][0]["pos_y"]) == pytest.approx(47.06268310546875 + first_dyn_y)
    assert float(out["items"][0]["vel_y"]) == pytest.approx(first_dyn_y, abs=1e-6)
    assert float(out["items"][0]["vel_x"]) == pytest.approx(0.30000001192092896, abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "slot"),
    [
        (7581, 0),
        (7606, 1),
        (7628, 2),
        (7661, 3),
    ],
)
def test_yoshi_shyguy_state1_floor_contact_exports_reset_child_delta_pec(
    record: int, slot: int
) -> None:
    # Active Heiho floor contact:
    # - itHeiho_UnkMotion1_Coll sees temp_r31 == 1 and calls itHeiho_UnkMotion1_Anim_inline.
    # - That path resets itemVar.heiho.x3C, restarts state 1, then immediately calls it_802D98C4;
    #   the post-frame x40_vel.y export is the reset-to-current child-JObj delta, not zero.
    # refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Coll,
    #   itHeiho_UnkMotion1_Anim_inline,it_802D98AC,it_802D98C4}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local replay dataset: PhysicalElectricCapybara.msl")

    out, ref = _step_one_row(dataset_path, record)
    assert int(ref["items"][slot]["exists"]) == 1
    assert int(ref["items"][slot]["type"]) == ITEM_KIND_HEIHO
    assert int(ref["items"][slot]["state"]) == 1
    assert float(ref["items"][slot]["vel_x"]) == 0.0
    assert abs(float(ref["items"][slot]["vel_y"])) > 1.0
    for field in ("exists", "type", "state", "spawn_id"):
        assert int(out["items"][slot][field]) == int(ref["items"][slot][field])
    assert float(out["items"][slot]["pos_x"]) == pytest.approx(
        float(ref["items"][slot]["pos_x"]), abs=1e-5
    )
    assert float(out["items"][slot]["pos_y"]) == pytest.approx(
        float(ref["items"][slot]["pos_y"]), abs=1e-5
    )
    assert float(out["items"][slot]["vel_x"]) == pytest.approx(
        float(ref["items"][slot]["vel_x"]), abs=1e-6
    )
    assert float(out["items"][slot]["vel_y"]) == pytest.approx(
        float(ref["items"][slot]["vel_y"]), abs=1e-5
    )


@pytest.mark.integration
def test_yoshi_shyguy_state1_non_floor_contact_keeps_dynamic_delta_pec() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local replay dataset: PhysicalElectricCapybara.msl")

    out, ref = _step_one_row(dataset_path, 7580)
    slot = 0
    assert int(ref["items"][slot]["exists"]) == 1
    assert int(ref["items"][slot]["type"]) == ITEM_KIND_HEIHO
    assert int(ref["items"][slot]["state"]) == 1
    assert float(ref["items"][slot]["vel_x"]) != 0.0
    assert abs(float(ref["items"][slot]["vel_y"])) < 1.0
    assert float(out["items"][slot]["vel_x"]) == pytest.approx(
        float(ref["items"][slot]["vel_x"]), abs=1e-6
    )
    assert float(out["items"][slot]["vel_y"]) == pytest.approx(
        float(ref["items"][slot]["vel_y"]), abs=1e-6
    )


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


@pytest.mark.integration
def test_yoshi_shyguy_one_step_timer_zero_uses_next_frame_rng_pec_119() -> None:
    # Replay-real one-step clock lock for grStory_801E3418:
    # PEC:119 seeds with the visible Shy Guy timer already at zero and no live Heiho items. Vanilla
    # consumes the next Slippi frame-start HSD stream for pattern/count/jitter and spawns five
    # right-side Heiho. Starting from the seed row's previous frame-start stream spawns the wrong
    # count and vertical pattern.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    dataset_path = Path(
        "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[119]["seed_t"]
    ref = ds.samples[119]["ref_t1"]
    assert int(seed["frame_pre_random_seed"]) == int(ref["frame_pre_random_seed"]), (
        "local dataset cache predates one-step frame RNG seed phase correction; rerun forced "
        "preprocess so no-live Shy Guy timer-zero rows carry the spawn-frame RNG seed"
    )
    assert int(seed["stage_yoshi_shyguy_timer_u16"]) == 0
    assert not any(
        int(item["exists"]) and int(item["type"]) == ITEM_KIND_HEIHO for item in seed["items"]
    )

    out, ref = _step_one_row(dataset_path, 119)
    live_slots = [i for i, item in enumerate(ref["items"]) if int(item["exists"]) != 0]
    assert live_slots == [0, 1, 2, 3, 4]
    assert int(out["frame_pre_random_seed"]) == int(ref["frame_pre_random_seed"])
    for slot in live_slots:
        assert int(out["items"][slot]["exists"]) == int(ref["items"][slot]["exists"])
        assert int(out["items"][slot]["type"]) == int(ref["items"][slot]["type"])
        assert float(out["items"][slot]["pos_x"]) == pytest.approx(
            float(ref["items"][slot]["pos_x"]), abs=1e-6
        )
        assert float(out["items"][slot]["pos_y"]) == pytest.approx(
            float(ref["items"][slot]["pos_y"]), abs=1e-6
        )


@pytest.mark.integration
def test_yoshi_shyguy_one_step_timer_countdown_keeps_rng_seed_owned_pec_118() -> None:
    # Negative owner boundary: when the stage timer is still counting down, grStory_801E3418 returns
    # before any Shy Guy RNG consumers. The replay one-step pre-step owner is only for the timer-zero
    # spawn callback, not for all timer-visible Yoshi rows.
    # refs/melee/src/melee/gr/grstory.c::grStory_801E3418
    dataset_path = Path(
        "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[118]["seed_t"]
    ref = ds.samples[118]["ref_t1"]
    assert int(seed["frame_pre_random_seed"]) == int(ref["frame_pre_random_seed"]), (
        "local dataset cache predates one-step frame RNG seed phase correction; rerun forced "
        "preprocess so Yoshi no-live Shy Guy rows carry the corrected seed phase"
    )
    assert int(seed["stage_yoshi_shyguy_timer_u16"]) == 1

    out, _ = _step_one_row(dataset_path, 118)
    assert int(out["frame_pre_random_seed"]) == int(seed["frame_pre_random_seed"])
    assert not any(int(item["exists"]) and int(item["type"]) == ITEM_KIND_HEIHO for item in out["items"])


@pytest.mark.integration
def test_yoshi_shyguy_rollout_advances_replay_rng_clock_until_spawn_cnm_2153() -> None:
    # Runtime rollout lock for the stage scheduler RNG owner:
    # - CNM:2153 seeds eight frames before the Yoshi's Story Shy Guy timer reaches zero.
    # - Vanilla consumes the frame-2038 Slippi/HSD RNG seed and spawns five left-side Heiho items.
    # - A frozen reseed RNG clock consumes the stale frame-2030 seed instead, spawning one right-side
    #   Heiho at x=304. The rollout seed lane carries the source spawn-frame stream so the zero-timer
    #   callback samples the same pattern/count/jitter without a synthetic frame-clock bridge.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[2153]["seed_t"]
    assert int(seed["stage_yoshi_shyguy_spawn_rng_seed_valid_u8"]) == 1
    assert int(seed["stage_yoshi_shyguy_spawn_rng_seed_u32"]) == int(
        ds.samples[2161]["seed_t"]["frame_pre_random_seed"]
    )

    out, ref = _run_rollout_to_record(dataset_path, start_record=2153, target_record=2161)

    live_slots = [i for i, item in enumerate(ref["items"]) if int(item["exists"]) != 0]
    assert len(live_slots) == 5
    for slot in live_slots:
        assert int(out["items"][slot]["exists"]) == 1
        assert int(out["items"][slot]["type"]) == ITEM_KIND_HEIHO
        assert int(out["items"][slot]["state"]) == int(ref["items"][slot]["state"])
        assert float(out["items"][slot]["pos_x"]) == pytest.approx(
            float(ref["items"][slot]["pos_x"]), abs=1e-6
        )
        assert float(out["items"][slot]["pos_y"]) == pytest.approx(
            float(ref["items"][slot]["pos_y"]), abs=1e-6
        )
        assert int(out["items"][slot]["spawn_id"]) == int(ref["items"][slot]["spawn_id"])


@pytest.mark.integration
def test_yoshi_shyguy_rollout_rng_clock_clears_after_spawn_cnm_2153() -> None:
    # Lifetime boundary for the replay-frame Shy Guy RNG clock:
    # starting before the zero-timer callback must advance to the spawn-frame RNG seed, but once
    # `grStory_801E3418` has spawned live Heiho items it returns before any later RNG consumers.
    # Later rollout frames must therefore keep frame_pre_random_seed seed-owned again.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    out, _ref, clock_mode = _run_rollout_to_record(
        dataset_path, start_record=2153, target_record=2162, return_clock_mode=True
    )

    spawn_post_rng = int(ds.samples[2161]["ref_t1"]["frame_pre_random_seed"])
    next_frame_rng = int(ds.samples[2162]["ref_t1"]["frame_pre_random_seed"])
    assert clock_mode == 0
    assert int(out["frame_pre_random_seed"]) == spawn_post_rng
    assert int(out["frame_pre_random_seed"]) != next_frame_rng
    assert any(int(item["exists"]) and int(item["type"]) == ITEM_KIND_HEIHO for item in out["items"])


@pytest.mark.integration
def test_yoshi_shyguy_rollout_clock_stays_seed_owned_when_heiho_live_cnm_2162() -> None:
    # Negative owner boundary: once the spawn exists, `grStory_801E3418` returns before decrementing
    # the stage timer or consuming pattern/count RNG. Reseeding on the first active Heiho frame must
    # preserve the seed-owned frame RNG lane instead of entering the stage-spawn rollout clock owner.
    # refs/melee/src/melee/gr/grstory.c::grStory_801E3418
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed_rng = int(ds.samples[2162]["seed_t"]["frame_pre_random_seed"])
    out, ref = _run_rollout_to_record(dataset_path, start_record=2162, target_record=2162)

    assert int(out["frame_pre_random_seed"]) == seed_rng
    assert int(out["frame_pre_random_seed"]) != int(ref["frame_pre_random_seed"])
    assert int(out["items"][0]["exists"]) == 1
    assert int(out["items"][0]["type"]) == ITEM_KIND_HEIHO


def test_yoshi_shyguy_rollout_clock_does_not_preempt_top_blast_rng_owner() -> None:
    # Arbitration boundary: an isolated no-live Shy Guy countdown can install its explicit
    # spawn-frame RNG seed at the future zero-timer callback, but it must not freeze the Slippi
    # frame-start stream when another source owner needs that stream earlier. Top-blast
    # DeadUpFall selection consumes `HSD_Randi(100)+1` from the normal replay frame clock.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    seed = _empty_seed()
    seed["stage_yoshi_shyguy_valid_u8"] = np.uint8(1)
    seed["stage_yoshi_shyguy_timer_u16"] = np.uint16(7)
    seed["stage_yoshi_shyguy_spawn_rng_seed_valid_u8"] = np.uint8(1)
    seed["stage_yoshi_shyguy_spawn_rng_seed_u32"] = np.uint32(0x12340000)
    seed["action_id"][0, 0] = np.uint16(ACT_DAMAGE_FLY_TOP)
    seed["animation_index"][0, 0] = np.uint32(180)
    seed["pos_y"][0, 0] = np.float32(300.0)
    seed["speed_y_attack"][0, 0] = np.float32(5.0)

    assert _rollout_clock_mode_for_seed(seed) == ROLLOUT_CLOCK_REPLAY_FRAME_SEED


@pytest.mark.integration
def test_yoshi_shyguy_rollout_uses_spawn_frame_rng_seed_lawful_meerkat_112() -> None:
    # LIM:112 starts seven frames before the Yoshi scheduler reaches timer zero. The source global
    # HSD stream advances by unrelated consumers during that countdown; using the old synthetic
    # `+0x10000` replay clock spawns the left-side group, while grStory_801E3418 samples the
    # spawn-frame stream and creates the right-side group.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/LawfulInsistentMeerkat.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[112]["seed_t"]
    assert int(seed["stage_yoshi_shyguy_timer_u16"]) == 7
    assert int(seed["stage_yoshi_shyguy_spawn_rng_seed_valid_u8"]) == 1
    assert int(seed["stage_yoshi_shyguy_spawn_rng_seed_u32"]) == int(
        ds.samples[119]["seed_t"]["frame_pre_random_seed"]
    )

    out, ref = _run_rollout_to_record(dataset_path, start_record=112, target_record=119)
    live_slots = [i for i, item in enumerate(ref["items"]) if int(item["exists"]) != 0]
    assert live_slots == [0]
    assert int(out["items"][0]["exists"]) == 1
    assert int(out["items"][0]["type"]) == ITEM_KIND_HEIHO
    assert float(out["items"][0]["pos_x"]) == pytest.approx(float(ref["items"][0]["pos_x"]), abs=1e-6)
    assert float(out["items"][0]["pos_y"]) == pytest.approx(float(ref["items"][0]["pos_y"]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "target_record", "expected_slots"),
    [
        (2719, 2724, [0, 1, 2, 3]),
        (3679, 3681, [0, 1, 2]),
    ],
)
def test_yoshi_shyguy_spawn_seed_coexists_with_replay_frame_rng_owner_lawful_meerkat(
    record: int, target_record: int, expected_slots: list[int]
) -> None:
    # These LIM countdown rollouts carry another replay-frame RNG-clock owner from fighter hidden
    # state, but `grStory_801E3418` still owns the zero-timer Shy Guy spawn stream. The explicit
    # spawn seed must install at the callback instead of letting the broader +0x10000 replay clock
    # choose side/count.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/LawfulInsistentMeerkat.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[record]["seed_t"]
    assert int(seed["stage_yoshi_shyguy_spawn_rng_seed_valid_u8"]) == 1
    assert int(seed["stage_yoshi_shyguy_spawn_rng_seed_u32"]) == int(
        ds.samples[target_record]["seed_t"]["frame_pre_random_seed"]
    )

    out, ref, clock_mode = _run_rollout_to_record(
        dataset_path, start_record=record, target_record=target_record, return_clock_mode=True
    )
    assert clock_mode == ROLLOUT_CLOCK_REPLAY_FRAME_SEED
    assert int(out["frame_pre_random_seed"]) == int(ref["frame_pre_random_seed"])
    live_slots = [i for i, item in enumerate(ref["items"]) if int(item["exists"]) != 0]
    assert live_slots == expected_slots
    for slot in expected_slots:
        assert int(out["items"][slot]["exists"]) == 1
        assert int(out["items"][slot]["type"]) == ITEM_KIND_HEIHO
        assert int(out["items"][slot]["state"]) == int(ref["items"][slot]["state"])
        assert int(out["items"][slot]["spawn_id"]) == int(ref["items"][slot]["spawn_id"])
        assert float(out["items"][slot]["pos_x"]) == pytest.approx(
            float(ref["items"][slot]["pos_x"]), abs=1e-6
        )
        assert float(out["items"][slot]["pos_y"]) == pytest.approx(
            float(ref["items"][slot]["pos_y"]), abs=1e-6
        )


@pytest.mark.integration
def test_yoshi_shyguy_spawn_seed_coexists_with_opening_countdown_clock_dsg_74() -> None:
    # Opening-countdown rows keep the broad replay-frame clock so input lock clears on the source
    # frame, but a later no-live Shy Guy zero-timer callback must still install its explicit
    # spawn-frame RNG seed. This guards against treating the rollout clock mode as exclusive owner
    # state for `grStory_801E3418`.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/DependentSteelGrouse.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[74]["seed_t"]
    assert int(seed["opening_input_lock_timer"][0]) > 0
    assert int(seed["stage_yoshi_shyguy_spawn_rng_seed_valid_u8"]) == 1
    assert int(seed["stage_yoshi_shyguy_spawn_rng_seed_u32"]) == int(
        ds.samples[119]["seed_t"]["frame_pre_random_seed"]
    )

    out, ref, clock_mode = _run_rollout_to_record(
        dataset_path, start_record=74, target_record=119, return_clock_mode=True
    )
    assert clock_mode == ROLLOUT_CLOCK_REPLAY_FRAME_SEED
    assert int(out["frame_pre_random_seed"]) == int(ref["frame_pre_random_seed"])
    assert int(out["items"][0]["exists"]) == 1
    assert int(out["items"][0]["type"]) == ITEM_KIND_HEIHO
    assert int(out["items"][0]["spawn_id"]) == int(ref["items"][0]["spawn_id"])
    assert float(out["items"][0]["pos_x"]) == pytest.approx(float(ref["items"][0]["pos_x"]), abs=1e-6)
    assert float(out["items"][0]["pos_y"]) == pytest.approx(float(ref["items"][0]["pos_y"]), abs=1e-6)


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
            raise AssertionError(f"stale local dataset cache: rerun forced aggregate preprocess: {exc}")
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
def test_yoshi_shyguy_fixed_ecb_wall_turn_replay_real_pec_946() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    # A left-moving live Heiho passes near Yoshi's right wall. Vanilla integrates with the old
    # leftward speed, then the active Coll callback sees the Article ItemAttr.x40 fixed ECB touch
    # the wall and flips x40_vel/facing for the post-frame state.
    # refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Coll,itHeiho_UnkMotion1_Phys}
    # refs/melee/src/melee/it/it_2725.c::{it_80275DFC,it_80276308}
    # data/stage_items/yoshi_shyguy.bin::MSLSTIO1 collision_ecb
    out, ref = _step_one_row(dataset_path, 946)
    assert int(out["items"][0]["exists"]) == 1
    assert int(out["items"][0]["type"]) == ITEM_KIND_HEIHO
    assert int(out["items"][0]["state"]) == 1
    assert float(out["items"][0]["pos_x"]) == pytest.approx(float(ref["items"][0]["pos_x"]))
    assert float(out["items"][0]["pos_y"]) == pytest.approx(float(ref["items"][0]["pos_y"]))
    assert float(out["items"][0]["direction"]) == pytest.approx(1.0)
    assert float(out["items"][0]["vel_x"]) == pytest.approx(0.3)
    assert float(out["items"][0]["direction"]) == pytest.approx(float(ref["items"][0]["direction"]))
    assert float(out["items"][0]["vel_x"]) == pytest.approx(float(ref["items"][0]["vel_x"]))


@pytest.mark.integration
def test_yoshi_shyguy_active_turn_delay_is_prefix_causal_after_visible_flip_pec_947() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[947]["seed_t"]
    assert int(seed["item_shyguy_delay_valid_u8"][0]) == 1, (
        "stale local dataset cache: rerun forced aggregate preprocess for "
        "active Shy Guy turn-delay derivation"
    )
    # The previous visible frame flipped this Heiho's exported X velocity. Preprocessing can
    # reconstruct the source `itemVar.heiho.x24 = 20` cooldown from that prefix-visible sign flip,
    # without looking at replay-future collision.
    assert int(seed["items"][0]["state"]) == 1
    assert float(seed["items"][0]["vel_x"]) == pytest.approx(0.3)
    assert int(seed["item_shyguy_delay_u16"][0]) == 20
    assert int(seed["item_shyguy_delay_valid_u8"][0]) == 1


@pytest.mark.integration
def test_yoshi_shyguy_fixed_ecb_wall_turn_negative_before_contact_pec_945() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    # Same item and wall approach one frame earlier: the fixed ECB bottom is still above the wall
    # top, so `it_80276308` does not own a turn yet.
    out, ref = _step_one_row(dataset_path, 945)
    assert int(out["items"][0]["exists"]) == 1
    assert int(out["items"][0]["type"]) == ITEM_KIND_HEIHO
    assert int(out["items"][0]["state"]) == 1
    assert float(out["items"][0]["pos_x"]) == pytest.approx(float(ref["items"][0]["pos_x"]))
    assert float(out["items"][0]["pos_y"]) == pytest.approx(float(ref["items"][0]["pos_y"]))
    assert float(out["items"][0]["direction"]) == pytest.approx(-1.0)
    assert float(out["items"][0]["vel_x"]) == pytest.approx(-0.3)
    assert float(out["items"][0]["direction"]) == pytest.approx(float(ref["items"][0]["direction"]))
    assert float(out["items"][0]["vel_x"]) == pytest.approx(float(ref["items"][0]["vel_x"]))


@pytest.mark.integration
def test_yoshi_shyguy_active_floor_contact_resets_anim_export_pec_1289() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    # A live state-1 Heiho descends until its fixed ECB crosses Yoshi's ground. Vanilla keeps the
    # already-integrated item position, but `it_8026DA70` returns true and the active Coll callback
    # restarts the state-1 animation, zeroing exported x40_vel for this post-frame.
    # refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Coll,
    #   itHeiho_UnkMotion1_Anim_inline}
    # refs/melee/src/melee/it/it_266F.c::it_8026DA70
    out, ref = _step_one_row(dataset_path, 1289)
    assert int(out["items"][1]["exists"]) == 1
    assert int(out["items"][1]["type"]) == ITEM_KIND_HEIHO
    assert int(out["items"][1]["state"]) == 1
    assert float(out["items"][1]["pos_x"]) == pytest.approx(float(ref["items"][1]["pos_x"]))
    assert float(out["items"][1]["pos_y"]) == pytest.approx(float(ref["items"][1]["pos_y"]))
    assert float(out["items"][1]["vel_x"]) == pytest.approx(0.0)
    assert float(out["items"][1]["vel_y"]) == pytest.approx(0.0)
    assert float(out["items"][1]["vel_x"]) == pytest.approx(float(ref["items"][1]["vel_x"]))
    assert float(out["items"][1]["vel_y"]) == pytest.approx(float(ref["items"][1]["vel_y"]))


@pytest.mark.integration
def test_yoshi_shyguy_return_flight_generic_blast_clear_replay_real_pec_1294() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    # Low-damage state 4 is return flight, not ordinary active state 1. It inherits xDCC_flag.b3
    # from `it_802D8EC8`, so generic Item_802697D4 clears it on exact side/bottom blast bounds
    # after Phys/integration. The active state-1 `it_802D9714` 20-unit margin does not apply here.
    # refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,it_802D9168,
    #   itHeiho_UnkMotion4_Phys}
    # refs/melee/src/melee/it/item.c::{Item_802697D4,Item_802696CC}
    out, ref = _step_one_row(dataset_path, 1294)
    assert int(ref["items"][0]["spawn_id"]) == 1
    assert int(out["items"][0]["spawn_id"]) == int(ref["items"][0]["spawn_id"])
    assert not any(
        int(item["exists"]) and int(item["type"]) == ITEM_KIND_HEIHO and int(item["spawn_id"]) == 0
        for item in out["items"]
    )


@pytest.mark.integration
def test_yoshi_shyguy_return_flight_generic_blast_clear_negative_before_bounds_pec_1293() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    # One frame earlier the same return-flight Shy Guy remains inside the exact side blast bound
    # after item integration, so the generic item destroy owner must not clear it yet.
    out, ref = _step_one_row(dataset_path, 1293)
    assert int(out["items"][0]["exists"]) == 1
    assert int(out["items"][0]["type"]) == ITEM_KIND_HEIHO
    assert int(out["items"][0]["spawn_id"]) == 0
    assert int(out["items"][0]["state"]) == 4
    assert int(out["items"][0]["spawn_id"]) == int(ref["items"][0]["spawn_id"])
    assert float(out["items"][0]["pos_x"]) == pytest.approx(float(ref["items"][0]["pos_x"]))
    assert float(out["items"][0]["pos_y"]) == pytest.approx(float(ref["items"][0]["pos_y"]))


@pytest.mark.integration
def test_yoshi_shyguy_floor_reset_restarts_phase_lane_pec_1290() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[1290]["seed_t"]
    assert int(seed["item_shyguy_dyn_y_phase_u8"][1]) == 0, (
        "stale local dataset cache: rerun forced aggregate preprocess for "
        "active Shy Guy floor-reset phase derivation"
    )

    assert int(seed["items"][1]["state"]) == 1
    assert float(seed["items"][1]["vel_x"]) == pytest.approx(0.0)
    assert float(seed["items"][1]["vel_y"]) == pytest.approx(0.0)
    assert int(seed["item_shyguy_dyn_y_phase_u8"][1]) == 0
    assert int(seed["item_shyguy_dyn_y_phase_valid_u8"][1]) == 1
    out, ref = _step_one_row(dataset_path, 1290)
    assert float(out["items"][1]["pos_y"]) == pytest.approx(float(ref["items"][1]["pos_y"]))
    assert float(out["items"][1]["vel_y"]) == pytest.approx(float(ref["items"][1]["vel_y"]))


@pytest.mark.integration
def test_yoshi_shyguy_fixed_ecb_floor_does_not_reset_when_already_below_floor_pec_996() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[996]["seed_t"]
    slot = 2
    assert int(seed["items"][slot]["type"]) == ITEM_KIND_HEIHO
    assert int(seed["items"][slot]["state"]) == 1

    # The active state-1 fixed ECB bottom is already below Yoshi's sloped right floor here. Source
    # floor admission (`it_8026DA70 -> mpColl_800471F8/mpCheckFloorRemap`) must not report another
    # floor entry just because the item continues moving laterally while below the floor plane.
    # refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Coll,
    #   itHeiho_UnkMotion1_Anim}
    # refs/melee/src/melee/it/it_266F.c::it_8026DA70
    # refs/melee/src/melee/mp/mplib.c::mpCheckFloorRemap
    for target_record, expected_vel_y in ((997, -0.6891632080078125), (998, -0.6946563720703125)):
        out, ref = _run_rollout_to_record(
            dataset_path, start_record=996, target_record=target_record
        )
        assert float(out["items"][slot]["pos_y"]) == pytest.approx(
            float(ref["items"][slot]["pos_y"]), abs=1e-6
        )
        assert float(out["items"][slot]["vel_x"]) == pytest.approx(
            float(ref["items"][slot]["vel_x"]), abs=1e-6
        )
        assert float(out["items"][slot]["vel_y"]) == pytest.approx(expected_vel_y, abs=1e-6)
        assert float(out["items"][slot]["vel_y"]) == pytest.approx(
            float(ref["items"][slot]["vel_y"]), abs=1e-6
        )


@pytest.mark.integration
def test_yoshi_shyguy_turn_cooldown_suppresses_repeat_wall_turn_pec_1835() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[1835]["seed_t"]
    assert int(seed["item_shyguy_delay_u16"][1]) == 20, (
        "stale local dataset cache: rerun forced aggregate preprocess for "
        "active Shy Guy turn-delay derivation"
    )

    # Same wall/ECB neighborhood as the positive contact path, but this row starts immediately
    # after a visible source turn. `itHeiho_UnkMotion1_Phys` decrements x24, and the Coll callback
    # must not flip again while the cooldown remains positive.
    out, ref = _step_one_row(dataset_path, 1835)
    assert int(seed["items"][1]["state"]) == 1
    assert int(seed["item_shyguy_delay_u16"][1]) == 20
    assert float(out["items"][1]["vel_x"]) == pytest.approx(-0.3)
    assert float(out["items"][1]["direction"]) == pytest.approx(-1.0)
    assert float(out["items"][1]["vel_x"]) == pytest.approx(float(ref["items"][1]["vel_x"]))
    assert float(out["items"][1]["direction"]) == pytest.approx(float(ref["items"][1]["direction"]))


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
            raise AssertionError(f"stale local dataset cache: rerun forced aggregate preprocess: {exc}")
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
            raise AssertionError(f"stale local dataset cache: rerun forced aggregate preprocess: {exc}")
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
            raise AssertionError(f"stale local dataset cache: rerun forced aggregate preprocess: {exc}")
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
            raise AssertionError(
                "stale local dataset cache: rerun forced aggregate preprocess for "
                "Shy Guy repeated-damage x24 derivation"
            )
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


@pytest.mark.integration
def test_yoshi_shyguy_first_visible_speed_marks_prefix_causal_speed_index_pec_5099() -> None:
    # Replay-real lock for prefix-causal hidden group speed ownership:
    # - it_802D8618 stores the spawn-group speed index in itemVar.heiho.x21.
    # - The first active post-frame can still expose x40_vel.x == 0 because state 0 has just entered
    #   state 1 through it_802D8918; the next visible active frame reveals the same hidden group
    #   speed that was already source-owned at spawn.
    # - Native preprocessing must not future-backfill that hidden speed onto earlier zero-velocity
    #   active rows. Runtime-spawned Shy Guys get the value from the scheduler RNG owner; replay
    #   reseeds of already-live items mark the speed lane only from the first visible nonzero row.
    # refs/melee/src/melee/it/items/itheiho.c::{it_802D8618,itHeiho_UnkMotion0_Phys,
    #   it_802D8918,itHeiho_UnkMotion1_Phys}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples.copy()
    lanes = _derive_yoshi_shyguy_native_lanes(samples["seed_t"]["items"], stage_id=STAGE_YOSHIS_STORY)
    speed_index = lanes[7]
    speed_valid = lanes[8]

    slot = 0
    assert int(samples[5098]["seed_t"]["items"][slot]["exists"]) == 0
    assert int(speed_valid[5098, slot]) == 0
    assert int(samples[5099]["seed_t"]["items"][slot]["type"]) == ITEM_KIND_HEIHO
    assert int(samples[5099]["seed_t"]["items"][slot]["state"]) == 1
    assert float(samples[5099]["seed_t"]["items"][slot]["vel_x"]) == pytest.approx(0.0)
    assert int(speed_valid[5099, slot]) == 0
    assert int(speed_valid[5099, 1]) == 0
    assert int(speed_valid[5099, 2]) == 0

    causal_record = next(
        record
        for record in range(5100, 5160)
        if int(samples[record]["seed_t"]["items"][slot]["type"]) == ITEM_KIND_HEIHO
        and abs(float(samples[record]["seed_t"]["items"][slot]["vel_x"])) > 0.001
    )
    assert causal_record == 5100
    assert int(speed_valid[causal_record, slot]) == 1
    assert int(speed_index[causal_record, slot]) == 0
    assert int(speed_valid[causal_record, 1]) == 1
    assert int(speed_valid[causal_record, 2]) == 1

    samples["seed_t"]["item_shyguy_speed_index_u8"] = speed_index
    samples["seed_t"]["item_shyguy_speed_index_valid_u8"] = speed_valid

    for target_record in (causal_record, 5119, 5159):
        out, ref = _run_rollout_samples_to_record(
            samples,
            num_players=int(ds.header["num_players"]),
            start_record=causal_record,
            target_record=target_record,
        )
        assert int(out["items"][slot]["state"]) == int(ref["items"][slot]["state"]) == 1
        assert float(out["items"][slot]["pos_x"]) == pytest.approx(
            float(ref["items"][slot]["pos_x"]), abs=1e-6
        )
        assert float(out["items"][slot]["vel_x"]) == pytest.approx(
            float(ref["items"][slot]["vel_x"]), abs=1e-6
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
