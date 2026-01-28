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

