from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, SAMPLE_DTYPE, SEED_DTYPE


@dataclass
class _FakeDataset:
    header: dict[str, int]
    samples: np.ndarray


class _FakeBinding:
    def __init__(self, out_compare: np.ndarray) -> None:
        self._out_compare = out_compare

    def sizes(self) -> dict[str, int]:
        from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

        return {"seed": int(SEED_DTYPE.itemsize), "input": int(INPUT_DTYPE.itemsize), "compare": int(COMPARE_DTYPE.itemsize)}

    def init(self, *, batch_size: int, num_players: int, **_kwargs):
        self._batch_size = int(batch_size)
        self._num_players = int(num_players)
        return object()

    def reseed_seed(self, _handle, _seed_bytes) -> None:
        return None

    def step_input(self, _handle, _prev_input_bytes, _input_bytes) -> None:
        return None

    def write_compare(self, _handle, out_compare_bytes: np.ndarray) -> None:
        view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        view[:] = self._out_compare[: view.shape[0]]

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


class _FastPathOnlyBinding(_FakeBinding):
    def __init__(self, out_compare: np.ndarray) -> None:
        super().__init__(out_compare)
        self.fast_path_calls = 0

    def one_step_eval_samples(
        self, _handle: object, _samples_u8: np.ndarray, _num_players: int, _profile_rl1: int
    ) -> dict[str, object]:
        self.fast_path_calls += 1
        return self.one_step_summary_finish(object())

    def reseed_seed(self, _handle, _seed_bytes) -> None:
        raise AssertionError("normal one-step validation should use one_step_eval_samples")

    def step_input(self, _handle, _prev_input_bytes, _input_bytes) -> None:
        raise AssertionError("normal one-step validation should use one_step_eval_samples")

    def write_compare(self, _handle, out_compare_bytes: np.ndarray) -> None:
        raise AssertionError("normal one-step validation should use one_step_eval_samples")


def test_normal_one_step_eval_uses_native_sample_fast_path(monkeypatch, tmp_path: Path) -> None:
    import tools.eval.run_one_step_eval as eval_mod

    samples = np.zeros((2,), dtype=SAMPLE_DTYPE)
    binding = _FastPathOnlyBinding(samples["ref_t1"].copy())
    monkeypatch.setattr(eval_mod, "_load_binding", lambda: binding)
    monkeypatch.setattr(
        eval_mod,
        "read_dataset",
        lambda _p: _FakeDataset(header={"num_players": 2}, samples=samples),
    )

    eval_mod.evaluate_dataset(
        dataset_path=tmp_path / "synthetic.slpz",
        chunk=1,
        reporter=eval_mod.Reporter(echo=False),
    )

    assert binding.fast_path_calls == 1


def test_debug_mismatch_state_flags_multidim(capsys, monkeypatch, tmp_path: Path) -> None:
    # This used to crash with "too many values to unpack" because `np.argwhere` on a
    # (records, players, bytes) diff returns 3 columns (r, p, byte).
    import tools.eval.run_one_step_eval as eval_mod

    samples = np.zeros((1,), dtype=SAMPLE_DTYPE)
    samples["seed_t"]["frame_id"][0] = 100
    samples["ref_t1"]["frame_id"][0] = 101

    # Seed state_flags is only for context in the debug line.
    samples["seed_t"]["state_flags"][0, 1, 3] = 9

    # Ref vs out mismatch at (record=0, p=1, byte=3).
    samples["ref_t1"]["state_flags"][0, 1, 3] = 7
    out_compare = samples["ref_t1"].copy()
    out_compare["state_flags"][0, 1, 3] = 0

    monkeypatch.setattr(eval_mod, "_load_binding", lambda: _FakeBinding(out_compare))
    monkeypatch.setattr(eval_mod, "read_dataset", lambda _p: _FakeDataset(header={"num_players": 2}, samples=samples))

    eval_mod.evaluate_dataset(
        dataset_path=tmp_path / "synthetic.msl",
        chunk=1,
        reporter=eval_mod.Reporter(),
        debug_mismatch=("state_flags",),
        debug_limit=10,
    )

    out = capsys.readouterr().out
    assert "debug.mismatch.state_flags:" in out
    assert "p=1 byte=3" in out
    assert "seed=9" in out
    assert "out=0" in out
    assert "ref=7" in out
