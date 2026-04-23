from __future__ import annotations

from pathlib import Path

import pytest

from tools.dolphin.throw_laser_event_dump import read_throw_laser_events


def test_read_throw_laser_event_jsonl(tmp_path: Path) -> None:
    path = tmp_path / "events.jsonl"
    path.write_text(
        "\n".join(
            [
                '{"frame": 4144, "event": "spawn", "phase": "return", "fn": "it_8029C6CC",'
                ' "item": {"kind": 54, "state": 1, "xDA8_short": 1073},'
                ' "hitbox": {"index": 2, "group": 0, "victims1_active_slots": [0]}}',
                '{"frame": 4144, "event": "destroy", "phase": "entry", "fn": "Item_8026A8EC"}',
                "",
            ]
        ),
        encoding="utf-8",
    )

    dump = read_throw_laser_events(path)

    assert dump.path == path
    assert len(dump.events) == 2
    assert dump.events[0]["event"] == "spawn"
    assert dump.events[0]["item"]["kind"] == 54
    assert dump.events[0]["hitbox"]["index"] == 2


def test_read_throw_laser_event_rejects_missing_required_fields(tmp_path: Path) -> None:
    path = tmp_path / "events.jsonl"
    path.write_text('{"frame": 4144}\n', encoding="utf-8")

    with pytest.raises(ValueError, match="missing required frame/event"):
        read_throw_laser_events(path)
