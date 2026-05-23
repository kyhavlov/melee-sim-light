from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _function_body(source: str, name: str) -> str:
    match = re.search(rf"\b(?:static\s+)?(?:int|void)\s+{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
    assert match is not None, f"missing function {name}"
    start = match.end()
    depth = 1
    pos = start
    while pos < len(source) and depth:
        ch = source[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        pos += 1
    assert depth == 0, f"unterminated function {name}"
    return source[start : pos - 1]


def _assert_ordered(text: str, needles: list[str]) -> None:
    cursor = -1
    for needle in needles:
        pos = text.find(needle, cursor + 1)
        assert pos >= 0, f"missing {needle!r}"
        assert pos > cursor, f"{needle!r} appears out of order"
        cursor = pos


def test_step_frame_phase_spine_matches_source_order() -> None:
    source = (ROOT / "src/fighter_callbacks.c").read_text()
    body = _function_body(source, "fighter_callbacks_step_frame")

    # Source anchor:
    # refs/melee/src/melee/ft/fighter.c::Fighter_Create_Inline2 registers the fighter procs in
    # priority order: 0 hitlag/timer, 1 Anim, 4 procUpdate (IASA/Phys), later map/collision and
    # ProcessHit. MSL keeps the supported gameplay subset explicit in one ordered frame spine.
    _assert_ordered(
        body,
        [
            "combat_rng_trace_begin_frame",
            "fighter_callbacks_begin_frame_phase",
            "fighter_callbacks_pre_input_anim_phase",
            "fighter_callbacks_input_phase",
            "fighter_callbacks_iasa_phase",
            "fighter_callbacks_phys_phase",
            "fighter_callbacks_collision_phase",
            "fighter_callbacks_primitive_refresh_phase",
            "fighter_callbacks_item_collision_and_combat_phase",
            "fighter_callbacks_post_frame_phase",
            "combat_rng_trace_end_frame",
        ],
    )

    pre_input = _function_body(source, "fighter_callbacks_pre_input_anim_phase")
    _assert_ordered(
        pre_input,
        [
            "input_apply_pre_input_snapshot",
            "timers_update",
            "timers_consume_post_hitlag_callbacks_pre_input",
            "match_flow_update_pre_anim",
            "anim_timebase_update_pre_input",
            "match_flow_update_post_anim",
            "timers_update_post_anim",
            "items_update_pre_fighter_anim_phase",
            "action_update_anim_callback_pre_input_fighter",
            "action_update_anim_callbacks_pre_input_global",
            "combat_processhit_consume",
        ],
    )


def test_scheduler_source_completion_doc_is_closed() -> None:
    scheduler = (ROOT / "agent_docs/systems/scheduler.md").read_text()
    progress = (ROOT / "agent_docs/systems/PROGRESS.md").read_text()

    assert "| Scheduler | CLOSED | [scheduler.md](scheduler.md)" in progress
    assert "| TODO" not in scheduler
    assert "INVENTORY NEEDED" not in scheduler
    assert "| BLOCKED |" not in scheduler
    assert "document has no BLOCKED rows" in scheduler
