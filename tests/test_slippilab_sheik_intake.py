from __future__ import annotations

import json
from pathlib import Path
from types import SimpleNamespace

from tools.eval import slippilab_sheik_intake as intake
from tools.eval.slippilab_sheik_intake import Candidate, _metadata_rows, _split_candidates, _write_audit


def _candidate(
    name: str, *, stage_id: int, opponent: int, played_on: str = "2026-01-01", sha256: str | None = None
) -> Candidate:
    row = {
        "file_name": name,
        "played_on": played_on,
        "id": sum(ord(ch) for ch in name),
        "external_stage_id": stage_id,
        "players": [
            {"player_index": 0, "external_character_id": 19},
            {"player_index": 1, "external_character_id": opponent},
        ],
    }
    audit = {
        "file_name": name,
        "accepted": True,
        "sha256": sha256 or name,
        "num_frames_peppi": 3600,
        "characters": {"1": "Sheik", "2": {2: "Fox", 9: "Marth", 20: "Falco"}[opponent]},
        "ports": [1, 2],
    }
    return Candidate(row=row, audit=audit, cache_path=Path(name))


def test_metadata_rows_accepts_slippilab_data_wrapper() -> None:
    rows = [{"file_name": "Example.slp"}]
    assert _metadata_rows({"data": rows}) == rows
    assert _metadata_rows(rows) == rows


def test_split_candidates_has_no_official_heldout_filename_overlap() -> None:
    candidates = []
    for stage_id in (31, 28, 32, 2, 3, 8):
        for i, opponent in enumerate((2, 9, 20), start=1):
            candidates.append(_candidate(f"{stage_id}_official_{i}.slp", stage_id=stage_id, opponent=opponent))
        for i, opponent in enumerate((2, 9), start=4):
            candidates.append(_candidate(f"{stage_id}_heldout_{i}.slp", stage_id=stage_id, opponent=opponent))

    official, heldout = _split_candidates(candidates, official_per_stage=3, heldout_per_stage=2)

    assert len(official) == 18
    assert len(heldout) == 12
    assert {c.file_name for c in official}.isdisjoint({c.file_name for c in heldout})


def test_split_candidates_has_no_official_heldout_checksum_overlap() -> None:
    candidates = []
    for stage_id in (31, 28, 32, 2, 3, 8):
        candidates.append(
            _candidate(f"{stage_id}_official_1.slp", stage_id=stage_id, opponent=2, sha256=f"{stage_id}_a")
        )
        candidates.append(
            _candidate(f"{stage_id}_official_2.slp", stage_id=stage_id, opponent=9, sha256=f"{stage_id}_b")
        )
        candidates.append(
            _candidate(f"{stage_id}_official_3.slp", stage_id=stage_id, opponent=20, sha256=f"{stage_id}_c")
        )
        candidates.append(
            _candidate(f"{stage_id}_duplicate_name.slp", stage_id=stage_id, opponent=2, sha256=f"{stage_id}_a")
        )
        candidates.append(
            _candidate(f"{stage_id}_heldout_1.slp", stage_id=stage_id, opponent=2, sha256=f"{stage_id}_d")
        )
        candidates.append(
            _candidate(f"{stage_id}_heldout_2.slp", stage_id=stage_id, opponent=9, sha256=f"{stage_id}_e")
        )

    official, heldout = _split_candidates(candidates, official_per_stage=3, heldout_per_stage=2)

    assert len(official) == 18
    assert len(heldout) == 12
    assert {c.audit["sha256"] for c in official}.isdisjoint({c.audit["sha256"] for c in heldout})


def test_split_candidates_balances_heldout_opponents_globally() -> None:
    candidates = []
    for stage_id in (31, 28, 32, 2, 3, 8):
        for rank in range(4):
            for opponent in (2, 9, 20):
                candidates.append(
                    _candidate(
                        f"{stage_id}_{opponent}_{rank}.slp",
                        stage_id=stage_id,
                        opponent=opponent,
                        played_on=f"2026-01-{rank + 1:02d}",
                    )
                )

    official, heldout = _split_candidates(candidates, official_per_stage=3, heldout_per_stage=2)
    heldout_opponents = [candidate.opponent for candidate in heldout]

    assert len(official) == 18
    assert len(heldout) == 12
    assert {opponent: heldout_opponents.count(opponent) for opponent in ("Fox", "Marth", "Falco")} == {
        "Fox": 4,
        "Marth": 4,
        "Falco": 4,
    }


def test_write_audit_records_every_manifest_category(tmp_path: Path) -> None:
    official = _candidate("official.slp", stage_id=31, opponent=2)
    heldout = _candidate("heldout.slp", stage_id=31, opponent=9)
    unused = _candidate("unused.slp", stage_id=31, opponent=20)
    accepted_shape = _candidate("audit_reject.slp", stage_id=31, opponent=2)
    rejected = Candidate(
        row=accepted_shape.row,
        audit={**accepted_shape.audit, "accepted": False, "rejection_reason": "non_human_player"},
        cache_path=accepted_shape.cache_path,
    )
    prelim = {
        "file_name": "prelim.slp",
        "split": "rejected",
        "rejection_reason": "unsupported_character",
    }
    not_audited = {
        "file_name": "not_audited.slp",
        "split": "eligible_not_audited",
        "rejection_reason": "not_audited_due_to_max_audit_per_stage",
    }
    out = tmp_path / "manifest.json"

    _write_audit(
        path=out,
        candidates=[official, heldout, unused, rejected],
        official=[official],
        heldout=[heldout],
        prelim_rejected=[prelim],
        eligible_not_audited=[not_audited],
    )

    rows = json.loads(out.read_text())
    split_by_name = {row["file_name"]: row["split"] for row in rows}
    assert split_by_name == {
        "official.slp": "official_train_eval",
        "heldout.slp": "held_out",
        "unused.slp": "accepted_unused",
        "audit_reject.slp": "rejected",
        "prelim.slp": "rejected",
        "not_audited.slp": "eligible_not_audited",
    }


def _row(stage_id: int = 31, character_ids: tuple[int, int] = (19, 2)) -> dict[str, object]:
    return {
        "file_name": "candidate.slp",
        "played_on": "2026-01-01",
        "id": 10,
        "external_stage_id": stage_id,
        "num_frames": 3600,
        "players": [
            {"player_index": 0, "external_character_id": character_ids[0]},
            {"player_index": 1, "external_character_id": character_ids[1]},
        ],
    }


def _fake_game(
    *,
    stage_id: int = 31,
    frozen_ps: bool = True,
    player_type: str = "Human",
    dash_back: str = "Ucf",
    shield_drop: str = "Ucf",
    start_character_ids: tuple[int, int] = (19, 2),
    post_character_ids: tuple[int, int] = (7, 1),
) -> SimpleNamespace:
    start = {
        "stage": stage_id,
        "is_frozen_ps": frozen_ps,
        "is_teams": False,
        "players": [
            {
                "port": "P1",
                "character": start_character_ids[0],
                "type": player_type,
                "ucf": {"dash_back": dash_back, "shield_drop": shield_drop},
            },
            {
                "port": "P2",
                "character": start_character_ids[1],
                "type": "Human",
                "ucf": {"dash_back": "Ucf", "shield_drop": "Ucf"},
            },
        ],
    }
    frames = [
        {
            "ports": {
                "P1": {"leader": {"post": {"character": post_character_ids[0]}}},
                "P2": {"leader": {"post": {"character": post_character_ids[1]}}},
            }
        }
    ]
    return SimpleNamespace(start=start, frames=frames)


def _audit_with_fake_game(monkeypatch, tmp_path: Path, game: SimpleNamespace) -> Candidate:
    replay = tmp_path / "candidate.slp"
    replay.write_bytes(b"fake-replay")
    monkeypatch.setattr(intake, "_download_replay", lambda row, replay_dir: replay)
    monkeypatch.setattr(intake, "_read_slippi", lambda path, skip_frames: game)
    return intake._audit_candidate(_row(stage_id=int(game.start["stage"])), tmp_path)


def test_audit_candidate_rejects_non_human_player(monkeypatch, tmp_path: Path) -> None:
    candidate = _audit_with_fake_game(monkeypatch, tmp_path, _fake_game(player_type="CPU"))

    assert candidate.audit["accepted"] is False
    assert candidate.audit["rejection_reason"] == "non_human_player"


def test_audit_candidate_rejects_ucf_disabled(monkeypatch, tmp_path: Path) -> None:
    candidate = _audit_with_fake_game(monkeypatch, tmp_path, _fake_game(dash_back="None"))

    assert candidate.audit["accepted"] is False
    assert candidate.audit["rejection_reason"] == "ucf_not_enabled"


def test_audit_candidate_rejects_unfrozen_pokemon_stadium(monkeypatch, tmp_path: Path) -> None:
    candidate = _audit_with_fake_game(monkeypatch, tmp_path, _fake_game(stage_id=3, frozen_ps=False))

    assert candidate.audit["accepted"] is False
    assert candidate.audit["rejection_reason"] == "pokemon_stadium_not_frozen"


def test_audit_candidate_rejects_start_unsupported_character(monkeypatch, tmp_path: Path) -> None:
    game = _fake_game(start_character_ids=(19, 5), post_character_ids=(7, 1))
    candidate = _audit_with_fake_game(monkeypatch, tmp_path, game)

    assert candidate.audit["accepted"] is False
    assert candidate.audit["rejection_reason"] == "start_unsupported_character"


def test_audit_candidate_rejects_post_frame_character_change(monkeypatch, tmp_path: Path) -> None:
    candidate = _audit_with_fake_game(monkeypatch, tmp_path, _fake_game(post_character_ids=(19, 1)))

    assert candidate.audit["accepted"] is False
    assert candidate.audit["rejection_reason"] == "character_changed_or_transformed"
