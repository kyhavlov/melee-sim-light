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
    ucf_cardinals_1_0_enabled: bool | None = None
    ucf_shield_sdi_enabled: bool | None = None
    ucf_sdi_enabled: bool | None = None


@dataclass(frozen=True)
class ReplaySuite:
    name: str
    notes: str | None
    replays: tuple[SuiteReplay, ...]
    ucf_enabled: bool | None = None
    ucf_cardinals_1_0_enabled: bool | None = None
    ucf_shield_sdi_enabled: bool | None = None
    ucf_sdi_enabled: bool | None = None
    team_attack_on: bool | None = None


def team_attack_on_from_start(start: dict[str, Any]) -> bool | None:
    bitfield = start.get("bitfield")
    if isinstance(bitfield, list) and len(bitfield) >= 2:
        return (int(bitfield[1]) & 0x01) != 0
    return None


def load_suite(path: str | Path) -> ReplaySuite:
    p = Path(path)
    data = json.loads(p.read_text())
    name = str(data["name"])
    notes = data.get("notes")
    ucf_enabled = data.get("ucf_enabled")
    ucf_cardinals_1_0_enabled = data.get("ucf_cardinals_1_0_enabled")
    ucf_shield_sdi_enabled = data.get("ucf_shield_sdi_enabled")
    ucf_sdi_enabled = data.get("ucf_sdi_enabled")
    team_attack_on = data.get("team_attack_on")
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
                ucf_cardinals_1_0_enabled=(
                    bool(r["ucf_cardinals_1_0_enabled"])
                    if "ucf_cardinals_1_0_enabled" in r
                    else None
                ),
                ucf_shield_sdi_enabled=(
                    bool(r["ucf_shield_sdi_enabled"])
                    if "ucf_shield_sdi_enabled" in r
                    else None
                ),
                ucf_sdi_enabled=(
                    bool(r["ucf_sdi_enabled"])
                    if "ucf_sdi_enabled" in r
                    else None
                ),
            )
        )
    if any(len(r.ports) > 2 for r in replays) and team_attack_on is not True:
        raise ValueError(
            f"{p}: suites selecting more than two ports must declare \"team_attack_on\": true; "
            "team-attack-off doubles are outside the supported simulator domain"
        )
    return ReplaySuite(
        name=name,
        notes=notes,
        ucf_enabled=bool(ucf_enabled) if ucf_enabled is not None else None,
        ucf_cardinals_1_0_enabled=bool(ucf_cardinals_1_0_enabled)
        if ucf_cardinals_1_0_enabled is not None
        else None,
        ucf_shield_sdi_enabled=bool(ucf_shield_sdi_enabled)
        if ucf_shield_sdi_enabled is not None
        else None,
        ucf_sdi_enabled=bool(ucf_sdi_enabled)
        if ucf_sdi_enabled is not None
        else None,
        team_attack_on=bool(team_attack_on) if team_attack_on is not None else None,
        replays=tuple(replays),
    )


def repo_root() -> Path:
    # tools/slippi/suite_io.py -> tools/slippi -> tools -> repo root
    return Path(__file__).resolve().parents[2]


def display_path_under_repo(path: str | Path, root: str | Path | None = None) -> str:
    """Return a stable repo-relative display path when possible.

    Keep paths that are syntactically under the checkout before resolving
    symlinks. This lets worktree-local links such as replays/ point elsewhere
    on disk without leaking the source checkout path into reports.
    """
    repo = Path(root) if root is not None else repo_root()
    p = Path(path)
    if not p.is_absolute():
        p = repo / p
    try:
        return p.relative_to(repo).as_posix()
    except ValueError:
        pass
    try:
        return p.resolve().relative_to(repo.resolve()).as_posix()
    except ValueError:
        return str(p)
