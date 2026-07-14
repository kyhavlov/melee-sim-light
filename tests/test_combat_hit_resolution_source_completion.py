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

    assert "| Combat Hit Resolution | CLOSED | [combat_hit_resolution.md](combat_hit_resolution.md)" in progress
    assert "| TODO" not in combat
    assert "INVENTORY NEEDED" not in combat
    assert "| BLOCKED |" not in combat
    assert "document has no BLOCKED rows" in combat
    assert "KB-triplet-derived MSLLASR1 proxy" not in progress
    assert "KB-triplet-derived proxy" not in combat
    assert "| Item/projectile shared BODY and SHIELD boundary:" in combat
    assert "| CLOSED | Shared shield/BODY/stale substrate is modeled" in combat
    assert "src/combat.c::combat_apply_item_hit" in combat
    assert "Fighter_ProcessHit_8006D1EC" in combat
    assert "enters Damage* only when applied KB is nonzero" in combat


def test_laser_zero_kb_damage_class_is_documented_as_source_derived() -> None:
    extractor = (ROOT / "tools/extraction/extract_lasers.py").read_text()
    laser_params = (ROOT / "src/laser_params.h").read_text()
    combat = (ROOT / "src/combat.c").read_text()

    for text in (extractor, laser_params, combat):
        assert "Fighter_ProcessHit_8006D1EC" in text
        assert "zero-KB-authoritative-signal" not in text
        assert "KB-triplet-derived proxy" not in text
    assert "_derive_zero_kb_damage_class" in extractor
    assert "ftColl_80077C60" in extractor
    assert "all-zero KB tuple is therefore the" in extractor


def test_combat_runtime_phase_order_matches_source_inventory() -> None:
    source = (ROOT / "src/fighter_callbacks.c").read_text()
    body = _function_body(source, "fighter_callbacks_item_collision_and_combat_phase")

    # Source inventory owner:
    # - HitCapsule cooldowns and ShieldDesc/reflector bubbles must refresh before item/fighter
    #   collision.
    # - Fighter HitCapsules traverse before the same fighter's item-HitCapsule contacts.
    # - Post-combat item/knockdown owners observe the combat result.
    _assert_ordered(
        body,
        [
            "hitlist_tick(batch)",
            "shields_refresh(batch)",
            "reflector_bubbles_refresh(batch)",
            "combat_processhit_pending_begin(batch)",
            "fighter_contact_resolve_catch(batch)",
            "fighter_contact_resolve_damage(batch)",
            "items_update_collision_phase(batch)",
            "combat_processhit_resolve(batch)",
            "items_update_post_combat(batch)",
            "knockdown_update_post_combat(batch)",
        ],
    )


def test_combat_resolve_uses_the_shared_causal_contact_chain() -> None:
    source = (ROOT / "src/combat.c").read_text()
    body = _function_body(source, "combat_resolve")

    _assert_ordered(
        body,
        [
            "fighter_contact_resolve_catch(batch)",
            "fighter_contact_resolve_damage(batch)",
            "combat_processhit_resolve(batch)",
        ],
    )

    contact = (ROOT / "src/fighter_contact.c").read_text()
    damage = _function_body(contact, "contact_resolve_damage_row")
    _assert_ordered(
        damage,
        [
            "contact_resolve_clanks(batch, bi, clank_skip)",
            "marth_counter_try_fighter_contact(batch, bi, attacker, defender, hb)",
            "contact_hitbox_shield_overlap(batch, bi, attacker, hb, defender)",
            "combat_source_body_log_record(batch, &logs[defender]",
            "combat_source_body_log_apply(batch, bi, &logs[defender])",
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
