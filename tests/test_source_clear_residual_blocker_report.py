from __future__ import annotations

from tools.eval.source_clear_residual_blocker_report import _Row, _search_simple_rules


def _mk_row(*, action_id: int, timer: int, good: int, bad: int) -> _Row:
    return _Row(
        dataset="d.msl",
        record=0,
        p=0,
        features={
            "char_id": 1,
            "action_id": action_id,
            "action_frame": 0,
            "timer_x18c8": timer,
            "combo_count": 1,
            "last_attack_landed": 1,
            "seed_last_hit_by": 1,
            "prev_action_id": action_id,
            "prev_action_frame": 0,
            "prev_timer_x18c8": timer + 1,
        },
        ref=6 if good else 1,
        out=1 if good else 1,
        good=good,
        bad=bad,
    )


def test_search_simple_rules_no_positive_when_good_rows_not_separable() -> None:
    rows = [
        _mk_row(action_id=20, timer=5, good=1, bad=0),
        _mk_row(action_id=20, timer=6, good=1, bad=0),
        _mk_row(action_id=20, timer=5, good=0, bad=1),
        _mk_row(action_id=20, timer=6, good=0, bad=1),
    ]
    single, pair = _search_simple_rules(rows)
    assert single == []
    assert pair == []


def test_search_simple_rules_finds_positive_single_when_cleanly_separable() -> None:
    rows = [
        _mk_row(action_id=24, timer=5, good=1, bad=0),
        _mk_row(action_id=24, timer=6, good=1, bad=0),
        _mk_row(action_id=20, timer=5, good=0, bad=1),
        _mk_row(action_id=20, timer=6, good=0, bad=1),
    ]
    single, _ = _search_simple_rules(rows)
    assert single
    assert single[0].net > 0
