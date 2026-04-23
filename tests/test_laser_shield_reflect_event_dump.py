from __future__ import annotations

from pathlib import Path

import pytest

from tools.dolphin.laser_shield_reflect_event_dump import (
    _event_summary,
    read_laser_shield_reflect_events,
)


def test_read_laser_shield_reflect_event_jsonl(tmp_path: Path) -> None:
    path = tmp_path / "events.jsonl"
    path.write_text(
        "\n".join(
            [
                '{"frame": 6207, "event": "reflect_collision", "phase": "entry",'
                ' "fn": "ftColl_80077464", "pc": 2147972196, "lr": 2147980620,'
                ' "item": {"kind": 55, "state": 0, "ground_or_air": 1, "xDA8_short": 149,'
                ' "xC54_bits": 1073741824, "xC58_bits": [0, 0, 0], "xDCE_flags": 5,'
                ' "xDCE_b5": 1, "xDCE_b4": 0, "item_common_unk_degrees": 45.0,'
                ' "shield_bounce_threshold": 2.3561945, "shield_bounce_predicate": 1},'
                ' "fighter": {"port": 1, "state_flags_2218": 4, "state_flags_221B": 0},'
                ' "capsule": {"group": 0, "damage_bits": 1084227584, "flags41": 0, "flags42": 0}}',
                '{"frame": 6207, "event": "reflect_owner_transfer", "phase": "return",'
                ' "fn": "Item_80269F14", "outcome": "keepalive", "return_value": 0,'
                ' "item": {"kind": 55, "state": 0, "xDA8_short": 2}}',
                "",
            ]
        ),
        encoding="utf-8",
    )

    dump = read_laser_shield_reflect_events(path)

    assert dump.path == path
    assert len(dump.events) == 2
    assert dump.events[0]["event"] == "reflect_collision"
    assert dump.events[0]["fighter"]["port"] == 1
    assert dump.events[1]["outcome"] == "keepalive"
    summary = _event_summary(dump.events[0])
    assert summary["pc"] == 2147972196
    assert summary["lr"] == 2147980620
    assert summary["ground_or_air"] == 1
    assert summary["xDCE_b5"] == 1
    assert summary["xDCE_b4"] == 0
    assert summary["item_common_unk_degrees"] == 45.0
    assert summary["shield_bounce_threshold"] == 2.3561945
    assert summary["shield_bounce_predicate"] == 1


def test_event_summary_decodes_xdce_bits_from_legacy_flags() -> None:
    summary = _event_summary(
        {
            "frame": 231,
            "event": "shield_collision",
            "item": {
                "kind": 55,
                "state": 0,
                "xDCE_flags": 5,
                "xC54_bits": 1077533180,
            },
        }
    )

    assert summary["xDCE_flags"] == 5
    assert summary["xDCE_b5"] == 1
    assert summary["xDCE_b4"] == 0
    assert summary["xC54"] == pytest.approx(2.9039297)


def test_read_laser_shield_reflect_event_rejects_missing_required_fields(tmp_path: Path) -> None:
    path = tmp_path / "events.jsonl"
    path.write_text('{"frame": 6207}\n', encoding="utf-8")

    with pytest.raises(ValueError, match="missing required frame/event"):
        read_laser_shield_reflect_events(path)
