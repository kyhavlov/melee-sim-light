from __future__ import annotations

import functools
from pathlib import Path

from tools.eval.dataset import Dataset, read_dataset
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp
from tools.slippi.slpz import resolve_replay_path
from tools.slippi.suite_io import load_suite, repo_root


def _suite_label_parts(path: Path) -> tuple[str, Path] | None:
    parts = path.parts
    try:
        idx = len(parts) - 1 - list(reversed(parts)).index("datasets")
    except ValueError:
        return None
    if idx + 2 > len(parts):
        return None
    suite_name = parts[idx + 1]
    rel = Path(*parts[idx + 2 :])
    if not rel.parts or rel.parts[0] != "replays":
        return None
    return suite_name, rel.with_suffix(".slpz")


@functools.lru_cache(maxsize=128)
def _load_replay_dataset(label: str) -> Dataset:
    root = repo_root()
    path = Path(label)
    suite_parts = _suite_label_parts(path)
    if suite_parts is None:
        return read_dataset(str(path))

    suite_name, replay_rel = suite_parts
    suite_path = root / "replays" / "suites" / f"{suite_name}.json"
    suite = load_suite(suite_path)
    replay_stem = replay_rel.with_suffix("").as_posix()
    for entry in suite.replays:
        if Path(entry.replay).with_suffix("").as_posix() != replay_stem:
            continue
        replay_path = resolve_replay_path((root / entry.replay).resolve())
        return build_dataset_from_slp(
            slp_path=str(replay_path),
            ports=list(entry.ports),
            ucf_enabled=True if suite.ucf_enabled is None else bool(suite.ucf_enabled),
            ucf_cardinals_1_0_enabled=bool(suite.ucf_cardinals_1_0_enabled),
        )
    raise FileNotFoundError(f"no replay suite entry for legacy dataset label: {label}")


def load_replay_dataset(label: str | Path) -> Dataset:
    return _load_replay_dataset(str(label))


def replay_dataset_available(label: str | Path) -> bool:
    path = Path(label)
    if _suite_label_parts(path) is None:
        return path.exists()
    try:
        load_replay_dataset(path)
    except FileNotFoundError:
        return False
    return True
