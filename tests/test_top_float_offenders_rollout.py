from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.eval import top_float_offenders
from tools.eval.dataset import (
    COMPARE_DTYPE,
    HEADER_DTYPE,
    INPUT_DTYPE,
    MAGIC,
    SAMPLE_DTYPE,
    SEED_DTYPE,
    Dataset,
    write_dataset,
)


def _compare_row(*, frame: int, action: int, action_frame: int, pos_y: float) -> np.ndarray:
    row = np.zeros((), dtype=COMPARE_DTYPE)
    row["frame_id"] = frame
    row["action_id"][0] = action
    row["action_frame"][0] = action_frame
    row["pos_y"][0] = np.float32(pos_y)
    return row


class _FakeHandle:
    def __init__(self, outputs: list[np.ndarray]) -> None:
        self.outputs = outputs
        self.next_output = 0
        self.current = outputs[0]


class _FakeBinding:
    def __init__(self, outputs: list[np.ndarray]) -> None:
        self.outputs = outputs

    def sizes(self) -> dict[str, int]:
        return {
            "seed": SEED_DTYPE.itemsize,
            "input": INPUT_DTYPE.itemsize,
            "compare": COMPARE_DTYPE.itemsize,
        }

    def init(self, **_kwargs) -> _FakeHandle:
        return _FakeHandle(self.outputs)

    def destroy(self, _handle: _FakeHandle) -> None:
        return None

    def reseed_seed_rollout(self, _handle: _FakeHandle, _seed_bytes: np.ndarray) -> None:
        return None

    def apply_replay_frame_rng(self, _handle: _FakeHandle, _seed_bytes: np.ndarray) -> None:
        return None

    def step_input(self, handle: _FakeHandle, _prev_input: np.ndarray, _input: np.ndarray) -> None:
        handle.current = handle.outputs[handle.next_output]
        handle.next_output += 1

    def step_input_replay_frame_rng(
        self,
        handle: _FakeHandle,
        _seed_bytes: np.ndarray,
        _prev_input: np.ndarray,
        _input: np.ndarray,
    ) -> None:
        self.step_input(handle, _prev_input, _input)

    def write_compare(self, handle: _FakeHandle, out: np.ndarray) -> None:
        out[:] = np.frombuffer(handle.current.tobytes(order="C"), dtype=np.uint8).reshape(out.shape)


def test_rollout_float_locator_reports_float_rows_and_discrete_match(
    monkeypatch, tmp_path: Path
) -> None:
    samples = np.zeros(3, dtype=SAMPLE_DTYPE)
    for i in range(3):
        samples["seed_t"]["frame_id"][i] = i
        samples["ref_t1"]["frame_id"][i] = i + 1
        samples["seed_t"]["action_id"][i, 0] = 20
        samples["ref_t1"]["action_id"][i, 0] = 20
        samples["seed_t"]["action_frame"][i, 0] = i
        samples["ref_t1"]["action_frame"][i, 0] = i + 1
        samples["seed_t"]["pos_y"][i, 0] = np.float32(0.0001)
        samples["ref_t1"]["pos_y"][i, 0] = np.float32(0.0001)

    path = tmp_path / "game.msl"
    write_dataset(str(path), 2, samples)

    outputs = [
        _compare_row(frame=1, action=20, action_frame=1, pos_y=0.0002),
        _compare_row(frame=2, action=76, action_frame=1, pos_y=0.5001),
        _compare_row(frame=2, action=20, action_frame=2, pos_y=0.0001),
        _compare_row(frame=3, action=20, action_frame=3, pos_y=0.0001),
    ]
    monkeypatch.setattr(top_float_offenders, "_load_binding", lambda: _FakeBinding(outputs))

    rows = top_float_offenders.collect_dataset_top_rollout_float_offenders(
        dataset_path=path,
        fields=("pos_y",),
        players=(0,),
        top=4,
        max_records=0,
        threshold=0.0,
        discrete_fields=top_float_offenders.DEFAULT_DISCRETE_FIELDS,
        profile="rl1_gameplay",
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=False,
    )["pos_y"]

    assert [(r.record, round(r.abs_err, 4), r.discrete_state_matches) for r in rows] == [
        (1, 0.5, False),
        (0, 0.0001, True),
    ]
    assert rows[1].streak_start_record == 0
    assert rows[1].streak_len == 0
    assert rows[1].attempt == "free_run"
    assert rows[1].seeded_retry is False


def test_rollout_float_locator_reports_seeded_retry_float_residual(
    monkeypatch, tmp_path: Path
) -> None:
    samples = np.zeros(2, dtype=SAMPLE_DTYPE)
    for i in range(2):
        samples["seed_t"]["frame_id"][i] = i
        samples["ref_t1"]["frame_id"][i] = i + 1
        samples["seed_t"]["action_id"][i, 0] = 20
        samples["ref_t1"]["action_id"][i, 0] = 20
        samples["seed_t"]["action_frame"][i, 0] = i
        samples["ref_t1"]["action_frame"][i, 0] = i + 1
        samples["seed_t"]["pos_y"][i, 0] = np.float32(0.0001)
        samples["ref_t1"]["pos_y"][i, 0] = np.float32(0.0001)

    path = tmp_path / "game.msl"
    write_dataset(str(path), 2, samples)

    outputs = [
        # Free-run attempt at record 0: discrete mismatch, no relevant float residual.
        _compare_row(frame=1, action=76, action_frame=1, pos_y=0.0001),
        # Seeded retry at record 0: discrete match, but visible float residual.
        _compare_row(frame=1, action=20, action_frame=1, pos_y=0.0002),
        # Next free-run record keeps matching with no residual.
        _compare_row(frame=2, action=20, action_frame=2, pos_y=0.0001),
    ]
    monkeypatch.setattr(top_float_offenders, "_load_binding", lambda: _FakeBinding(outputs))

    rows = top_float_offenders.collect_dataset_top_rollout_float_offenders(
        dataset_path=path,
        fields=("pos_y",),
        players=(0,),
        top=4,
        max_records=0,
        threshold=0.0,
        discrete_fields=top_float_offenders.DEFAULT_DISCRETE_FIELDS,
        profile="rl1_gameplay",
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=False,
    )["pos_y"]

    assert len(rows) == 1
    row = rows[0]
    assert row.record == 0
    assert round(row.abs_err, 4) == 0.0001
    assert row.discrete_state_matches is True
    assert row.attempt == "seeded_retry"
    assert row.seeded_retry is True


def test_rollout_float_locator_suite_reads_replay_directly(
    monkeypatch, tmp_path: Path
) -> None:
    suite_path = tmp_path / "suite.json"
    replay = tmp_path / "game.slp"
    replay.write_bytes(b"fake slp")
    suite_path.write_text(
        json.dumps(
            {
                "name": "tiny",
                "ucf_enabled": True,
                "ucf_cardinals_1_0_enabled": False,
                "replays": [{"replay": str(replay), "ports": [1, 2]}],
            }
        ),
        encoding="utf-8",
    )
    samples = np.zeros(1, dtype=SAMPLE_DTYPE)
    samples["seed_t"]["frame_id"][0] = 0
    samples["ref_t1"]["frame_id"][0] = 1
    samples["seed_t"]["action_id"][0, 0] = 20
    samples["ref_t1"]["action_id"][0, 0] = 20
    samples["ref_t1"]["pos_y"][0, 0] = np.float32(0.0001)
    header = np.zeros((), dtype=HEADER_DTYPE)
    header["magic"] = MAGIC
    header["record_size"] = SAMPLE_DTYPE.itemsize
    header["num_records"] = 1
    header["num_players"] = 2
    dataset = Dataset(header=header, samples=samples)
    calls: dict[str, object] = {}

    def fake_build_dataset_from_slp(**kwargs):
        calls["build"] = kwargs
        return dataset

    def fake_collect(**kwargs):
        calls["collect"] = kwargs
        return {"pos_y": []}

    monkeypatch.setattr(top_float_offenders, "build_dataset_from_slp", fake_build_dataset_from_slp)
    monkeypatch.setattr(top_float_offenders, "collect_dataset_top_rollout_float_offenders", fake_collect)

    payload = top_float_offenders.collect_suite_top_float_offenders(
        suite=suite_path,
        fields=("pos_y",),
        chunk=16,
        top=3,
        mode="rollout",
    )

    assert payload["mode"] == "rollout"
    assert calls["build"]["slp_path"] == str(replay.resolve())
    assert calls["collect"]["ds"] is dataset
