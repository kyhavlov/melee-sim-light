from __future__ import annotations

from pathlib import Path

import numpy as np

from tools.eval import dataset as ds
from tools.eval import replay_row_probe


def test_replay_row_probe_prints_direct_index_window(tmp_path: Path, capsys, monkeypatch) -> None:
    suite_path = tmp_path / "suite.json"
    suite_path.write_text(
        """
{
  "name": "probe_suite",
  "replays": [
    {"replay": "replays/probe.slp", "ports": [1, 2]}
  ]
}
""".strip(),
        encoding="utf-8",
    )
    dataset_path = tmp_path / "datasets" / "probe_suite" / "replays" / "probe.msl"
    dataset_path.parent.mkdir(parents=True)

    samples = np.zeros(3, dtype=ds.SAMPLE_DTYPE)
    samples["seed_t"]["frame_id"] = np.array([100, 101, 102], dtype=np.int32)
    samples["seed_t"]["action_id"][:, 1] = np.array([14, 15, 16], dtype=np.uint16)
    samples["seed_t"]["action_frame"][:, 1] = np.array([7, 8, 9], dtype=np.int16)
    samples["seed_t"]["animation_index"][:, 1] = np.array([20, 21, 22], dtype=np.uint32)
    samples["seed_t"]["pos_x"][:, 1] = np.array([1.0, 2.0, 3.0], dtype=np.float32)
    samples["seed_t"]["pos_y"][:, 1] = np.array([4.0, 5.0, 6.0], dtype=np.float32)
    samples["seed_t"]["hitlag"][:, 1] = np.array([0, 3, 0], dtype=np.uint16)
    samples["seed_t"]["last_hit_by"][:, 1] = np.array([6, 0, 0], dtype=np.uint8)
    samples["seed_t"]["items"][:, 2]["exists"] = np.array([0, 1, 1], dtype=np.uint8)
    samples["seed_t"]["items"][:, 2]["type"] = np.array([0, 54, 54], dtype=np.uint16)
    samples["seed_t"]["items"][:, 2]["owner"] = np.array([-1, 1, 1], dtype=np.int8)
    samples["seed_t"]["items"][:, 2]["instance_id"] = np.array([0, 77, 78], dtype=np.uint16)
    samples["ref_t1"]["action_id"][:, 1] = np.array([15, 16, 17], dtype=np.uint16)
    samples["ref_t1"]["action_frame"][:, 1] = np.array([8, 9, 10], dtype=np.int16)
    samples["ref_t1"]["items"][:, 2]["exists"] = np.array([1, 1, 0], dtype=np.uint8)
    ds.write_dataset(str(dataset_path), num_players=2, samples=samples)
    calls: list[tuple[str, int, int]] = []
    original_read_window = replay_row_probe.read_dataset_window

    def recording_read_window(path: str, start: int, stop: int):
        calls.append((path, start, stop))
        return original_read_window(path, start, stop)
    monkeypatch.setattr(replay_row_probe, "read_dataset_window", recording_read_window)

    replay_row_probe.main(
        [
            "--suite",
            str(suite_path),
            "--datasets-dir",
            str(tmp_path / "datasets"),
            "--record",
            "1",
            "--player",
            "1",
            "--window",
            "1",
            "--item",
            "2",
        ]
    )

    assert calls == [(str(dataset_path), 0, 3)]
    out = capsys.readouterr().out
    assert f"dataset: {dataset_path}" in out
    assert "access: direct-window rows=0..2" in out
    assert "records: 0..2 target=1 player=1 item=2" in out
    assert "* record=1 seed.frame_id=101" in out
    assert "seed.p1.core: action_id=15 action_frame=8 animation_index=21" in out
    assert "seed.p1.prov:" in out
    assert "ref.p1.prov: source_port0=<missing>" in out
    assert "seed.item2: exists=1 type=54 state=0 owner=1 instance_id=77" in out
