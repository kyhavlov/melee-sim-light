from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SAMPLE_DTYPE, SEED_DTYPE
from tools.eval.discrete_compare_lanes import (
    compile_discrete_compare_lanes,
    first_mismatch_field,
    first_mismatch_values,
)
from tools.eval.validation_profile import get_validation_profile


def test_rl1_gameplay_ignores_state_flags_4_but_strict_scores_it() -> None:
    seed = np.zeros(1, dtype=COMPARE_DTYPE)
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    seed["state_flags"][0, 1, 4] = 0
    out["state_flags"][0, 1, 4] = 0
    ref["state_flags"][0, 1, 4] = 0x80

    strict_lanes = compile_discrete_compare_lanes(
        ("action_id", "state_flags"), (0, 1), profile=get_validation_profile("strict")
    )
    strict_mm = first_mismatch_values(seed_row=seed[0], out_row=out[0], ref_row=ref[0], lanes=strict_lanes)
    assert strict_mm is not None
    assert strict_mm.field == "state_flags"
    assert strict_mm.player == 1
    assert strict_mm.subindex == 4

    rl1_lanes = compile_discrete_compare_lanes(
        ("action_id", "state_flags"), (0, 1), profile=get_validation_profile("rl1_gameplay")
    )
    assert first_mismatch_values(seed_row=seed[0], out_row=out[0], ref_row=ref[0], lanes=rl1_lanes) is None

    ignored_lanes = compile_discrete_compare_lanes(
        ("action_id", "state_flags"), (0, 1), profile=get_validation_profile("rl1_gameplay"), ignored_only=True
    )
    assert first_mismatch_field(out_row=out[0], ref_row=ref[0], lanes=ignored_lanes) == "state_flags"
    assert (
        first_mismatch_field(out_row=out[0], ref_row=ref[0], lanes=ignored_lanes, label_subindex=True)
        == "state_flags[4]"
    )


def test_rollout_ignored_first_state_flags_4_does_not_break_scored_streak() -> None:
    import tools.eval.run_longest_rollout_streaks as streaks

    attempts: list[int] = []

    def reseed_at(_j: int) -> None:
        return None

    def attempt_from_current(j: int):
        attempts.append(j)
        if j == 0:
            return streaks._AttemptResult(scored_field=None, ignored_first_field="state_flags[4]")
        if j == 1:
            return streaks._AttemptResult(scored_field="action_id", ignored_first_field=None)
        return None

    def attempt_seeded_at_record(_j: int):
        return None

    out = streaks._scan_rollout_streaks(
        n=3,
        reseed_at=reseed_at,
        attempt_from_current=attempt_from_current,
        attempt_seeded_at_record=attempt_seeded_at_record,
    )

    assert attempts == [0, 1, 2]
    assert out.first_mismatch_field_counts == {"action_id": 1}
    assert out.ignored_first_mismatch_field_counts == {"state_flags[4]": 1}
    assert out.best_start_record == 1
    assert out.best_end_record_excl == 3
    assert out.best_len == 2


@dataclass
class _FakeDataset:
    header: dict[str, int]
    samples: np.ndarray


class _FakeBinding:
    def __init__(self, out_compare: np.ndarray) -> None:
        self._out_compare = out_compare

    def sizes(self) -> dict[str, int]:
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


def test_one_step_profile_counts_state_flags_4_as_ignored_only(monkeypatch, tmp_path: Path) -> None:
    import tools.eval.run_one_step_eval as eval_mod

    samples = np.zeros((1,), dtype=SAMPLE_DTYPE)
    samples["seed_t"]["frame_id"][0] = 100
    samples["ref_t1"]["frame_id"][0] = 101
    samples["ref_t1"]["state_flags"][0, 1, 4] = 0x80
    out_compare = samples["ref_t1"].copy()
    out_compare["state_flags"][0, 1, 4] = 0

    monkeypatch.setattr(eval_mod, "_load_binding", lambda: _FakeBinding(out_compare))
    monkeypatch.setattr(eval_mod, "read_dataset", lambda _p: _FakeDataset(header={"num_players": 2}, samples=samples))

    rl1 = eval_mod.evaluate_dataset(dataset_path=tmp_path / "synthetic.msl", chunk=1, profile="rl1_gameplay")
    assert rl1.mismatches["state_flags"] == 0
    assert rl1.strict_mismatches["state_flags"] == 1
    assert rl1.ignored_mismatches["state_flags[4]"] == 1

    strict = eval_mod.evaluate_dataset(dataset_path=tmp_path / "synthetic.msl", chunk=1, profile="strict")
    assert strict.mismatches["state_flags"] == 1
    assert strict.strict_mismatches["state_flags"] == 1
    assert strict.ignored_mismatches == {}
