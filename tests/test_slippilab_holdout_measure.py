from __future__ import annotations

from pathlib import Path

from tools.eval.slippilab_holdout_measure import _dataset_label, _dataset_path_for_summary_entry
from tools.slippi.suite_io import dataset_path_for_suite_replay, repo_root


def test_holdout_summary_dataset_path_matches_preprocess_for_absolute_replay_path(
    tmp_path: Path,
) -> None:
    suite_name = "slippilab_unit_holdout_measure"
    replay = tmp_path / "exports" / "replays" / "Example.slp"
    datasets_dir = tmp_path / "datasets"

    got = _dataset_path_for_summary_entry(
        suite_name=suite_name,
        replay=str(replay),
        datasets_dir=datasets_dir,
    )
    expected = dataset_path_for_suite_replay(
        suite_name=suite_name,
        replay_rel_path=str(replay),
        datasets_dir=datasets_dir,
    )

    assert got == expected
    assert _dataset_label(got, root=repo_root()) == str(expected.resolve())
