from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SAMPLE_DTYPE, SEED_DTYPE
from tools.eval.discrete_compare_lanes import (
    compile_discrete_compare_lanes,
    first_mismatch_field,
    first_mismatch_values,
)
from tools.eval.validation_profile import get_validation_profile


def test_rl1_gameplay_ignores_only_state_flags_4_camera_bit_but_strict_scores_it() -> None:
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
        == "state_flags[4]&0x80"
    )


def test_rl1_gameplay_scores_other_state_flags_4_bits() -> None:
    seed = np.zeros(1, dtype=COMPARE_DTYPE)
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    ref["state_flags"][0, 1, 4] = 0x08

    rl1_lanes = compile_discrete_compare_lanes(
        ("action_id", "state_flags"), (0, 1), profile=get_validation_profile("rl1_gameplay")
    )
    mm = first_mismatch_values(seed_row=seed[0], out_row=out[0], ref_row=ref[0], lanes=rl1_lanes)
    assert mm is not None
    assert mm.field == "state_flags"
    assert mm.player == 1
    assert mm.subindex == 4

    ignored_lanes = compile_discrete_compare_lanes(
        ("action_id", "state_flags"), (0, 1), profile=get_validation_profile("rl1_gameplay"), ignored_only=True
    )
    assert first_mismatch_field(out_row=out[0], ref_row=ref[0], lanes=ignored_lanes) is None


def test_rl1_gameplay_scores_non_camera_bits_even_when_camera_bit_differs() -> None:
    seed = np.zeros(1, dtype=COMPARE_DTYPE)
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    out["state_flags"][0, 1, 4] = 0x80
    ref["state_flags"][0, 1, 4] = 0x81

    rl1_lanes = compile_discrete_compare_lanes(
        ("action_id", "state_flags"), (0, 1), profile=get_validation_profile("rl1_gameplay")
    )
    mm = first_mismatch_values(seed_row=seed[0], out_row=out[0], ref_row=ref[0], lanes=rl1_lanes)
    assert mm is not None
    assert mm.field == "state_flags"
    assert mm.player == 1
    assert mm.subindex == 4
    assert mm.out == 0
    assert mm.ref == 1


def test_rollout_ignored_first_state_flags_4_does_not_break_scored_streak() -> None:
    import tools.eval.run_longest_rollout_streaks as streaks

    attempts: list[int] = []

    def reseed_at(_j: int) -> None:
        return None

    def attempt_from_current(j: int):
        attempts.append(j)
        if j == 0:
            return streaks._AttemptResult(scored_field=None, ignored_first_field="state_flags[4]&0x80")
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
    assert out.ignored_first_mismatch_field_counts == {"state_flags[4]&0x80": 1}
    assert out.best_start_record == 1
    assert out.best_end_record_excl == 3
    assert out.best_len == 2


def test_disruptive_profile_scores_only_non_camera_state_flags_4_bits() -> None:
    import tools.eval.disruptive_rollout_desyncs as disruptive

    profile = get_validation_profile("rl1_gameplay")
    assert not hasattr(disruptive, "_compare_first_mismatch")
    assert not hasattr(disruptive, "_score_horizon_row")
    assert not profile.scored_values_differ("state_flags", 4, 0x00, 0x80)
    assert profile.scored_values_differ("state_flags", 4, 0x00, 0x81)
    assert disruptive._native_mask_fields(1 << 10) == ("state_flags[4]",)


def test_native_disruptive_scan_honors_rl1_state_flags_4_camera_bit() -> None:
    import tools.eval.disruptive_rollout_desyncs as disruptive
    from tools.eval.dataset import read_dataset

    msl_binding = pytest.importorskip("msl_binding")
    dataset_path = Path(
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))

    camera_only = ds.samples[:4].copy()
    camera_only["ref_t1"]["state_flags"][0, 1, 4] ^= 0x80
    camera_samples = camera_only.view(np.uint8).reshape(camera_only.shape[0], camera_only.dtype.itemsize)

    rl1_ints, rl1_floats = msl_binding.disruptive_scan(
        camera_samples,
        (1,),
        ("state_flags",),
        (),
        (0, 1),
        int(ds.header["num_players"]),
        1,
        1,
        0.05,
        -1,
        -1,
        1,
        0,
        1,
        1,
    )
    strict_ints, strict_floats = msl_binding.disruptive_scan(
        camera_samples,
        (1,),
        ("state_flags",),
        (),
        (0, 1),
        int(ds.header["num_players"]),
        1,
        1,
        0.05,
        -1,
        -1,
        1,
        0,
        1,
        0,
    )
    assert rl1_ints.shape == (0, 22)
    assert rl1_floats.shape == (0, 6)
    assert strict_ints.shape == (1, 22)
    assert strict_ints[0, disruptive.NATIVE_INT_FIRST_FIELD_CODE] == 104
    assert strict_ints[0, disruptive.NATIVE_INT_FIRST_SUBINDEX] == 4
    assert strict_ints[0, disruptive.NATIVE_INT_FIRST_PLAYER] == 1
    assert strict_floats[0, disruptive.NATIVE_FLOAT_SCORE_DISCRETE] == 15.0

    scored = ds.samples[:4].copy()
    scored["ref_t1"]["state_flags"][0, 1, 4] ^= 0x81
    scored_samples = scored.view(np.uint8).reshape(scored.shape[0], scored.dtype.itemsize)
    rl1_scored_ints, rl1_scored_floats = msl_binding.disruptive_scan(
        scored_samples,
        (1,),
        ("state_flags",),
        (),
        (0, 1),
        int(ds.header["num_players"]),
        1,
        1,
        0.05,
        -1,
        -1,
        1,
        0,
        1,
        1,
    )
    assert rl1_scored_ints.shape == (1, 22)
    assert rl1_scored_ints[0, disruptive.NATIVE_INT_FIRST_FIELD_CODE] == 104
    assert rl1_scored_ints[0, disruptive.NATIVE_INT_FIRST_SUBINDEX] == 4
    assert rl1_scored_ints[0, disruptive.NATIVE_INT_FIRST_PLAYER] == 1
    assert rl1_scored_floats[0, disruptive.NATIVE_FLOAT_SCORE_DISCRETE] == 15.0


@dataclass
class _FakeDataset:
    header: dict[str, int]
    samples: np.ndarray


class _FakeBinding:
    def __init__(self, out_compare: np.ndarray) -> None:
        self._out_compare = out_compare
        self._summary: dict[str, object] | None = None
        self._num_players = 0
        self._profile_rl1 = 0

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

    def one_step_summary_create(self, _total_records: int, num_players: int, profile_rl1: int) -> object:
        self._summary = None
        self._num_players = int(num_players)
        self._profile_rl1 = int(profile_rl1)
        return object()

    def one_step_summary_accumulate(
        self, _summary_handle: object, out_compare_bytes: np.ndarray, samples_u8: np.ndarray
    ) -> None:
        from tools.eval.one_step_report import DISCRETE_FIELDS, FLOAT_FIELDS

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        samples = samples_u8.view(SAMPLE_DTYPE).reshape(-1)
        ref = samples["ref_t1"]
        active = slice(0, self._num_players)
        mismatches = [0 for _ in DISCRETE_FIELDS]
        strict = [0 for _ in DISCRETE_FIELDS]
        sf_idx = DISCRETE_FIELDS.index("state_flags")
        xor = out["state_flags"][:, active, :].astype(np.uint16) ^ ref["state_flags"][
            :, active, :
        ].astype(np.uint16)
        strict[sf_idx] = int((xor != 0).sum())
        scored = xor.copy()
        ignored = int(((xor[:, :, 4] & 0x80) != 0).sum()) if self._profile_rl1 else 0
        if self._profile_rl1:
            scored[:, :, 4] &= np.uint16(0x7F)
        mismatches[sf_idx] = int((scored != 0).sum())
        self._summary = {
            "mismatches": mismatches,
            "strict_mismatches": strict,
            "ignored_state_flags_4_0x80": ignored,
            "float_metrics": {
                field: {"mae": 0.0, "p95": 0.0, "max": 0.0, "count": 0}
                for field in FLOAT_FIELDS
            },
            "float_norm_sum": 0.0,
            "float_norm_count": 0,
        }

    def one_step_summary_finish(self, _summary_handle: object) -> dict[str, object]:
        assert self._summary is not None
        return self._summary

    def one_step_eval_samples(
        self, _handle: object, samples_u8: np.ndarray, num_players: int, profile_rl1: int
    ) -> dict[str, object]:
        summary = self.one_step_summary_create(int(samples_u8.shape[0]), int(num_players), int(profile_rl1))
        out_compare_bytes = np.empty((int(samples_u8.shape[0]), int(COMPARE_DTYPE.itemsize)), dtype=np.uint8)
        out_compare = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        out_compare[:] = self._out_compare[: out_compare.shape[0]]
        self.one_step_summary_accumulate(summary, out_compare_bytes, samples_u8)
        return self.one_step_summary_finish(summary)


def test_one_step_profile_counts_state_flags_4_camera_bit_as_ignored_only(
    monkeypatch, tmp_path: Path
) -> None:
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
    assert rl1.ignored_mismatches["state_flags[4]&0x80"] == 1

    strict = eval_mod.evaluate_dataset(dataset_path=tmp_path / "synthetic.msl", chunk=1, profile="strict")
    assert strict.mismatches["state_flags"] == 1
    assert strict.strict_mismatches["state_flags"] == 1
    assert strict.ignored_mismatches == {}


def test_one_step_profile_scores_other_state_flags_4_bits(monkeypatch, tmp_path: Path) -> None:
    import tools.eval.run_one_step_eval as eval_mod

    samples = np.zeros((1,), dtype=SAMPLE_DTYPE)
    samples["seed_t"]["frame_id"][0] = 100
    samples["ref_t1"]["frame_id"][0] = 101
    samples["ref_t1"]["state_flags"][0, 1, 4] = 0x08
    out_compare = samples["ref_t1"].copy()
    out_compare["state_flags"][0, 1, 4] = 0

    monkeypatch.setattr(eval_mod, "_load_binding", lambda: _FakeBinding(out_compare))
    monkeypatch.setattr(eval_mod, "read_dataset", lambda _p: _FakeDataset(header={"num_players": 2}, samples=samples))

    rl1 = eval_mod.evaluate_dataset(dataset_path=tmp_path / "synthetic.msl", chunk=1, profile="rl1_gameplay")
    assert rl1.mismatches["state_flags"] == 1
    assert rl1.strict_mismatches["state_flags"] == 1
    assert rl1.ignored_mismatches["state_flags[4]&0x80"] == 0
