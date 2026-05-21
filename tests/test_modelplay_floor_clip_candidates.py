from __future__ import annotations

import json
from pathlib import Path

from tools.modelplay import floor_clip_candidates
from tools.modelplay.floor_clip_candidates import FloorLine, find_floor_clip_candidates


def _frame(frame: int, y: float, *, grounded: bool = False, action: int = 29) -> dict:
    return {
        "frameNumber": frame,
        "players": [
            {
                "playerIndex": 0,
                "state": {
                    "frameNumber": frame,
                    "playerIndex": 0,
                    "actionStateId": action,
                    "actionStateFrameCounter": 0.0,
                    "internalCharacterId": 1,
                    "xPosition": 0.0,
                    "yPosition": y,
                    "isGrounded": grounded,
                    "isDead": False,
                    "isOffscreen": False,
                },
            }
        ],
    }


def test_floor_clip_candidate_groups_airborne_below_solid_floor(
    tmp_path: Path, monkeypatch
) -> None:
    monkeypatch.setattr(
        floor_clip_candidates,
        "stage_floor_lines",
        lambda stage_id: [FloorLine(0, -10.0, 0.0, 10.0, 0.0, False)],
    )
    trace = {
        "settings": {"stageId": 32},
        "frames": [
            _frame(10, 0.5),
            _frame(11, -0.4),
            _frame(12, -1.2, action=85),
            _frame(13, 0.0, grounded=True),
        ],
    }
    path = tmp_path / "trace.json"
    path.write_text(json.dumps(trace), encoding="utf-8")

    rows = find_floor_clip_candidates(path, threshold=0.25, point="root")

    assert len(rows) == 1
    assert rows[0].stage_id == 32
    assert rows[0].player == 0
    assert rows[0].point == "root"
    assert rows[0].start_frame == 11
    assert rows[0].end_frame == 12
    assert rows[0].min_y == -1.2
    assert rows[0].floor_y_at_min == 0.0
    assert rows[0].action_ids == (29, 85)


def test_floor_clip_candidate_ignores_offstage_x(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.setattr(
        floor_clip_candidates,
        "stage_floor_lines",
        lambda stage_id: [FloorLine(0, -10.0, 0.0, 10.0, 0.0, False)],
    )
    trace = {
        "settings": {"stageId": 32},
        "frames": [
            {
                "frameNumber": 20,
                "players": [
                    {
                        "playerIndex": 0,
                        "state": {
                            "frameNumber": 20,
                            "playerIndex": 0,
                            "actionStateId": 29,
                            "actionStateFrameCounter": 0.0,
                            "internalCharacterId": 1,
                            "xPosition": 20.0,
                            "yPosition": -10.0,
                            "isGrounded": False,
                            "isDead": False,
                            "isOffscreen": False,
                        },
                    }
                ],
            }
        ],
    }
    path = tmp_path / "trace.json"
    path.write_text(json.dumps(trace), encoding="utf-8")

    assert find_floor_clip_candidates(path, point="root") == []


def test_floor_clip_candidate_default_uses_ecb_bottom_not_root(
    tmp_path: Path, monkeypatch
) -> None:
    monkeypatch.setattr(
        floor_clip_candidates,
        "stage_floor_lines",
        lambda stage_id: [FloorLine(0, -10.0, 0.0, 10.0, 0.0, False)],
    )
    monkeypatch.setattr(
        floor_clip_candidates,
        "_submotion_by_action",
        lambda char_id: tuple(20 if action == 29 else 0xFFFF for action in range(400)),
    )

    import msl_binding

    monkeypatch.setattr(msl_binding, "ecb_bottom_rel_y", lambda char_id, msid, frame: 2.0)
    trace = {
        "settings": {"stageId": 32},
        "frames": [_frame(30, -1.0, action=29)],
    }
    path = tmp_path / "trace.json"
    path.write_text(json.dumps(trace), encoding="utf-8")

    assert find_floor_clip_candidates(path, threshold=0.25) == []
    rows = find_floor_clip_candidates(path, threshold=0.25, point="root")
    assert len(rows) == 1
    assert rows[0].point == "root"
