from __future__ import annotations

import functools

import numpy as np

import msl_binding
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views
from tools.slippi.suite_io import repo_root


@functools.lru_cache(maxsize=2)
def _replay_buffers(replay_rel: str, ports: tuple[int, ...]):
    root = repo_root()
    return load_replay_buffers(
        slp_path=str(root / replay_rel),
        ports=[int(p) for p in ports],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )


def _copy_rows(ds, rows: np.ndarray, *, capacity: int):
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    seed_bytes = np.empty((capacity, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((capacity, input_stride), dtype=np.uint8)
    input_bytes = np.empty((capacity, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((capacity, compare_stride), dtype=np.uint8)
    n = int(rows.shape[0])
    seed_bytes[:n] = seed_u8[rows, :seed_stride]
    prev_input_bytes[:n] = prev_input_u8[rows, :input_stride]
    input_bytes[:n] = input_u8[rows, :input_stride]
    if n < capacity:
        seed_bytes[n:] = seed_bytes[0]
        prev_input_bytes[n:] = prev_input_bytes[0]
        input_bytes[n:] = input_bytes[0]
    return seed_bytes, prev_input_bytes, input_bytes, out_compare_bytes


def _run_one_step_rows(ds, rows: np.ndarray, *, prior_rows: np.ndarray | None = None) -> np.ndarray:
    capacity = int(rows.shape[0])
    handle = msl_binding.init(
        batch_size=capacity,
        num_players=2,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        if prior_rows is not None:
            seed, prev_input, cur_input, _out = _copy_rows(ds, prior_rows, capacity=capacity)
            msl_binding.reseed_seed(handle, seed)
            msl_binding.step_input(handle, prev_input, cur_input)

        seed, prev_input, cur_input, out = _copy_rows(ds, rows, capacity=capacity)
        msl_binding.reseed_seed(handle, seed)
        msl_binding.step_input(handle, prev_input, cur_input)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape(-1).copy()
    finally:
        msl_binding.destroy(handle)


def test_one_step_reseed_clears_hitlag_input_edge_latches_between_chunks() -> None:
    ds = _replay_buffers("replays/validation/aggregate_recent/TubbyCurlyHerring.slpz", (1, 3))
    rows = np.arange(8192, 12288, dtype=np.int64)
    prior_rows = np.arange(4096, 8192, dtype=np.int64)
    lane = 8970 - 8192

    fresh = _run_one_step_rows(ds, rows)
    prior = _run_one_step_rows(ds, prior_rows)
    reused = _run_one_step_rows(ds, rows, prior_rows=prior_rows)

    assert reused["speed_x_attack"][lane, 1] == fresh["speed_x_attack"][lane, 1]
    assert reused["speed_y_attack"][lane, 1] == fresh["speed_y_attack"][lane, 1]
    # This is an isolation lock: prior chunk occupancy must not leak hitlag edge latches into the
    # current chunk. The previous chunk's same lane carries different attack-velocity output, so a
    # stale lane leak would be visible here without asserting unrelated ref exactness.
    assert prior["speed_x_attack"][lane, 1] != fresh["speed_x_attack"][lane, 1]
    assert prior["speed_y_attack"][lane, 1] != fresh["speed_y_attack"][lane, 1]


def test_one_step_reseed_clears_guard_reflect_entry_latches_between_chunks() -> None:
    ds = _replay_buffers("replays/validation/battlefield_recent/DelayedSuperbGuanaco.slpz", (1, 2))
    samples = ds.rows
    capacity = 256
    lane = 248
    target_row = 2552

    rows = np.full((capacity,), target_row, dtype=np.int64)
    prior_rows = np.full((capacity,), target_row, dtype=np.int64)
    prior_rows[lane] = 2296

    fresh = _run_one_step_rows(ds, rows)
    reused = _run_one_step_rows(ds, rows, prior_rows=prior_rows)
    ref_item = samples["ref_t1"]["items"][target_row, 0]

    assert reused["items"][lane, 0]["owner"] == fresh["items"][lane, 0]["owner"]
    assert reused["items"][lane, 0]["instance_id"] == fresh["items"][lane, 0]["instance_id"]
    assert int(reused["items"][lane, 0]["owner"]) == int(ref_item["owner"])
    assert int(reused["items"][lane, 0]["instance_id"]) == int(ref_item["instance_id"])
