from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class SuiteReplay:
    replay: str
    ports: tuple[int, ...]
    stage_id: int | None = None
    characters: dict[str, str] | None = None


@dataclass(frozen=True)
class ReplaySuite:
    name: str
    notes: str | None
    replays: tuple[SuiteReplay, ...]
    ucf_enabled: bool | None = None
    ucf_cardinals_1_0_enabled: bool | None = None


def load_suite(path: str | Path) -> ReplaySuite:
    p = Path(path)
    data = json.loads(p.read_text())
    name = str(data["name"])
    notes = data.get("notes")
    ucf_enabled = data.get("ucf_enabled")
    ucf_cardinals_1_0_enabled = data.get("ucf_cardinals_1_0_enabled")
    replays = []
    for r in data["replays"]:
        replay = str(r["replay"])
        ports = tuple(int(x) for x in r.get("ports", []))
        if not ports:
            raise ValueError(f"Suite replay missing ports: {replay}")
        replays.append(
            SuiteReplay(
                replay=replay,
                ports=ports,
                stage_id=r.get("stage_id"),
                characters=r.get("characters"),
            )
        )
    return ReplaySuite(
        name=name,
        notes=notes,
        ucf_enabled=bool(ucf_enabled) if ucf_enabled is not None else None,
        ucf_cardinals_1_0_enabled=bool(ucf_cardinals_1_0_enabled)
        if ucf_cardinals_1_0_enabled is not None
        else None,
        replays=tuple(replays),
    )


def repo_root() -> Path:
    # tools/slippi/suite_io.py -> tools/slippi -> tools -> repo root
    return Path(__file__).resolve().parents[2]


def dataset_path_for_suite_replay(
    suite_name: str,
    replay_rel_path: str,
    datasets_dir: str | Path = "datasets",
) -> Path:
    root = repo_root()
    rel = Path(replay_rel_path)
    out = Path(datasets_dir) / suite_name / rel
    return (root / out).with_suffix(".msl")
