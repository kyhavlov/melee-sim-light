from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset
from tools.slippi.make_dataset_from_slp import (
    _derive_yoshi_shyguy_prev_vel_y,
    _derive_yoshi_shyguy_seed_lanes,
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
    if int(seed["item_shyguy_prev_vel_y_valid"][slot]) == 0:
        if int(seed["items"][slot]["state"]) == 1 and abs(cur) <= 0.001:
            return dyn_y_vel[0]
        return cur
    prev = float(seed["item_shyguy_prev_vel_y"][slot])
    for i, value in enumerate(dyn_y_vel):
        if abs(value - cur) <= 0.001 and abs(dyn_y_vel[(i - 1) % len(dyn_y_vel)] - prev) <= 0.001:
            return dyn_y_vel[(i + 1) % len(dyn_y_vel)]
    return cur


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
