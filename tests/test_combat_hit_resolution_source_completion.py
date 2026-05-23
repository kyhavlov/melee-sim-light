from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _function_body(source: str, name: str) -> str:
    match = re.search(
        rf"\b(?:static\s+)?(?:inline\s+)?(?:int|void|uint8_t)\s+{re.escape(name)}\s*\([^)]*\)\s*\{{",
        source,
    )
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


def test_combat_source_completion_doc_tracks_audit_status() -> None:
    combat = (ROOT / "agent_docs/systems/combat_hit_resolution.md").read_text()
    progress = (ROOT / "agent_docs/systems/PROGRESS.md").read_text()

    assert "| Combat Hit Resolution | OPEN | [combat_hit_resolution.md](combat_hit_resolution.md)" in progress
    assert "| TODO" not in combat
    assert "INVENTORY NEEDED" not in combat
    assert "| BLOCKED |" not in combat
    assert "document has no BLOCKED rows" in combat
    assert "laser non-flinch damage-class gating still uses a KB-triplet-derived MSLLASR1 proxy" in progress
    assert "| Item/projectile shared BODY and SHIELD boundary:" in combat
    assert "| OPEN | Shared shield/BODY/stale substrate is modeled" in combat
    assert "src/combat.c::combat_apply_item_hit" in combat


def test_combat_runtime_phase_order_matches_source_inventory() -> None:
    source = (ROOT / "src/fighter_callbacks.c").read_text()
    body = _function_body(source, "fighter_callbacks_item_collision_and_combat_phase")

    # Source inventory owner:
    # - HitCapsule cooldowns and ShieldDesc/reflector bubbles must refresh before item/fighter
    #   collision.
    # - Item/projectile contacts feed the shared combat substrate before fighter-vs-fighter
    #   combat.
    # - Post-combat item/knockdown owners observe the combat result.
    _assert_ordered(
        body,
        [
            "hitlist_tick(batch)",
            "shields_refresh(batch)",
            "reflector_bubbles_refresh(batch)",
            "items_update_collision_phase(batch)",
            "throw_flow_update_post_items(batch)",
            "combat_resolve(batch)",
            "items_update_post_combat(batch)",
            "knockdown_update_post_combat(batch)",
        ],
    )


def test_combat_resolve_applies_damage_logs_after_selection_pass() -> None:
    source = (ROOT / "src/combat.c").read_text()
    body = _function_body(source, "combat_resolve")

    _assert_ordered(
        body,
        [
            "combat_select_body_hits_one_mutating(batch, bi)",
            "combat_preserve_fresh_air_damage_entry_root_y(batch)",
        ],
    )

    select_body = _function_body(source, "combat_select_body_hits_one_mutating")
    _assert_ordered(
        select_body,
        [
            "combat_select_catch_hits_one_mutating(batch, bi)",
            "combat_mutations_pass1_future_apply_shield_hit",
            "combat_body_damage_log_record",
            "combat_body_damage_log_apply",
        ],
    )


def test_hit_outcome_tags_keep_decomp_body_shield_clank_mapping() -> None:
    source = (ROOT / "src/hit_outcome.h").read_text()

    assert "MSL_LBCOLL_HITTYPE_BODY = 0" in source
    assert "MSL_LBCOLL_HITTYPE_SHIELD = 1" in source
    assert "MSL_LBCOLL_HITTYPE_CLANK = 3" in source
    assert "ftColl_80076ED8" in source
    assert "ftColl_80076CBC" in source
    assert "ftColl_8007699C" in source
