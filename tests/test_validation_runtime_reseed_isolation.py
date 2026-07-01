from __future__ import annotations

import functools
from pathlib import Path

import numpy as np
import pytest

import msl_binding
from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SAMPLE_DTYPE, SEED_DTYPE, Dataset
from tools.eval.run_one_step_eval import (
    OneStepEvalRuntime,
    Reporter,
    evaluate_dataset,
)
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp
from tools.slippi.suite_io import repo_root


@functools.lru_cache(maxsize=2)
def _replay_dataset(replay_rel: str, ports: tuple[int, ...]) -> Dataset:
    root = repo_root()
    return build_dataset_from_slp(
        slp_path=str(root / replay_rel),
        ports=[int(p) for p in ports],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )


def _copy_rows(samples: np.ndarray, rows: np.ndarray, *, capacity: int):
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(samples.shape[0], sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((capacity, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((capacity, input_stride), dtype=np.uint8)
    input_bytes = np.empty((capacity, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((capacity, compare_stride), dtype=np.uint8)
    n = int(rows.shape[0])
    seed_bytes[:n] = samples_u8[rows, seed_off : seed_off + seed_stride]
    prev_input_bytes[:n] = samples_u8[rows, prev_input_off : prev_input_off + input_stride]
    input_bytes[:n] = samples_u8[rows, input_off : input_off + input_stride]
    if n < capacity:
        seed_bytes[n:] = seed_bytes[0]
        prev_input_bytes[n:] = prev_input_bytes[0]
        input_bytes[n:] = input_bytes[0]
    return seed_bytes, prev_input_bytes, input_bytes, out_compare_bytes


def _run_one_step_rows(
    samples: np.ndarray, rows: np.ndarray, *, prior_rows: np.ndarray | None = None
) -> np.ndarray:
    capacity = int(rows.shape[0])
    handle = msl_binding.init(
        batch_size=capacity,
        num_players=2,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        if prior_rows is not None:
            seed, prev_input, cur_input, _out = _copy_rows(samples, prior_rows, capacity=capacity)
            msl_binding.reseed_seed(handle, seed)
            msl_binding.step_input(handle, prev_input, cur_input)

        seed, prev_input, cur_input, out = _copy_rows(samples, rows, capacity=capacity)
        msl_binding.reseed_seed(handle, seed)
        msl_binding.step_input(handle, prev_input, cur_input)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape(-1).copy()
    finally:
        msl_binding.destroy(handle)


def test_one_step_reseed_clears_hitlag_input_edge_latches_between_chunks() -> None:
    ds = _replay_dataset("replays/validation/aggregate_recent/TubbyCurlyHerring.slpz", (1, 3))
    samples = ds.samples
    rows = np.arange(8192, 12288, dtype=np.int64)
    prior_rows = np.arange(4096, 8192, dtype=np.int64)
    lane = 8970 - 8192

    fresh = _run_one_step_rows(samples, rows)
    reused = _run_one_step_rows(samples, rows, prior_rows=prior_rows)
    ref = samples["ref_t1"][8970]

    assert reused["speed_x_attack"][lane, 1] == fresh["speed_x_attack"][lane, 1]
    assert reused["speed_y_attack"][lane, 1] == fresh["speed_y_attack"][lane, 1]
    assert np.isclose(reused["speed_x_attack"][lane, 1], ref["speed_x_attack"][1])
    assert np.isclose(reused["speed_y_attack"][lane, 1], ref["speed_y_attack"][1])


def test_one_step_reseed_clears_guard_reflect_entry_latches_between_chunks() -> None:
    ds = _replay_dataset("replays/validation/battlefield_recent/DelayedSuperbGuanaco.slpz", (1, 2))
    samples = ds.samples
    capacity = 256
    lane = 248
    target_row = 2552

    rows = np.full((capacity,), target_row, dtype=np.int64)
    prior_rows = np.full((capacity,), target_row, dtype=np.int64)
    prior_rows[lane] = 2296

    fresh = _run_one_step_rows(samples, rows)
    reused = _run_one_step_rows(samples, rows, prior_rows=prior_rows)
    ref_item = samples["ref_t1"]["items"][target_row, 0]

    assert reused["items"][lane, 0]["owner"] == fresh["items"][lane, 0]["owner"]
    assert reused["items"][lane, 0]["instance_id"] == fresh["items"][lane, 0]["instance_id"]
    assert int(reused["items"][lane, 0]["owner"]) == int(ref_item["owner"])
    assert int(reused["items"][lane, 0]["instance_id"]) == int(ref_item["instance_id"])


class _FakeBinding:
    def destroy(self, _handle) -> None:
        return None

    def reseed_seed(self, _handle, _seed_bytes) -> None:
        return None

    def step_input(self, _handle, _prev_input_bytes, _input_bytes) -> None:
        return None

    def write_compare(self, _handle, out_compare_bytes: np.ndarray) -> None:
        out_compare_bytes.fill(0)

    def one_step_eval_samples(
        self, _handle: object, _samples_u8: np.ndarray, _num_players: int, _profile_rl1: int
    ) -> dict[str, object]:
        return self.one_step_summary_finish(object())

    def one_step_summary_create(self, _total_records: int, _num_players: int, _profile_rl1: int) -> object:
        return object()

    def one_step_summary_accumulate(
        self, _summary_handle: object, _out_compare_bytes: np.ndarray, _samples_u8: np.ndarray
    ) -> None:
        return None

    def one_step_summary_finish(self, _summary_handle: object) -> dict[str, object]:
        from tools.eval.one_step_report import DISCRETE_FIELDS, FLOAT_FIELDS

        return {
            "mismatches": [0 for _ in DISCRETE_FIELDS],
            "strict_mismatches": [0 for _ in DISCRETE_FIELDS],
            "ignored_state_flags_4_0x80": 0,
            "float_metrics": {
                field: {"mae": 0.0, "p95": 0.0, "max": 0.0, "count": 0}
                for field in FLOAT_FIELDS
            },
            "float_norm_sum": 0.0,
            "float_norm_count": 0,
        }


def _runtime_for_synthetic_layout() -> OneStepEvalRuntime:
    seed_bytes = np.empty((1, int(SEED_DTYPE.itemsize)), dtype=np.uint8)
    prev_input_bytes = np.empty((1, int(INPUT_DTYPE.itemsize)), dtype=np.uint8)
    input_bytes = np.empty((1, int(INPUT_DTYPE.itemsize)), dtype=np.uint8)
    out_compare_bytes = np.empty((1, int(COMPARE_DTYPE.itemsize)), dtype=np.uint8)
    return OneStepEvalRuntime(
        binding=_FakeBinding(),
        handle=object(),
        capacity=1,
        num_players=2,
        source_port0_layout=None,
        seed_stride=int(SEED_DTYPE.itemsize),
        input_stride=int(INPUT_DTYPE.itemsize),
        compare_stride=int(COMPARE_DTYPE.itemsize),
        seed_bytes=seed_bytes,
        prev_input_bytes=prev_input_bytes,
        input_bytes=input_bytes,
        out_compare_bytes=out_compare_bytes,
        out_compare_view=out_compare_bytes.view(COMPARE_DTYPE).reshape(-1),
    )


def _synthetic_dataset_with_source_layout(layout: tuple[int, int]) -> Dataset:
    samples = np.zeros((1,), dtype=SAMPLE_DTYPE)
    samples["seed_t"]["source_port0"][0, :2] = np.asarray(layout, dtype=np.uint8)
    header = {"num_players": 2, "num_records": 1}
    return Dataset(header=header, samples=samples)


def test_shared_one_step_runtime_reuse_rejects_different_source_port_layout(tmp_path: Path) -> None:
    runtime = _runtime_for_synthetic_layout()
    reporter = Reporter(echo=False)

    evaluate_dataset(
        dataset_path=tmp_path / "ports_12.slpz",
        dataset=_synthetic_dataset_with_source_layout((0, 1)),
        chunk=1,
        runtime=runtime,
        reporter=reporter,
        print_profile=False,
    )

    with pytest.raises(ValueError, match="source_port0 layout"):
        evaluate_dataset(
            dataset_path=tmp_path / "ports_24.slpz",
            dataset=_synthetic_dataset_with_source_layout((1, 3)),
            chunk=1,
            runtime=runtime,
            reporter=reporter,
            print_profile=False,
        )
