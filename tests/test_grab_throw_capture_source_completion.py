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


def test_grab_throw_capture_source_completion_doc_is_closed() -> None:
    doc = (ROOT / "agent_docs/systems/grab_throw_capture.md").read_text()
    progress = (ROOT / "agent_docs/systems/PROGRESS.md").read_text()

    assert "| Grab / Throw / Capture | CLOSED | [grab_throw_capture.md](grab_throw_capture.md)" in progress
    assert "| TODO" not in doc
    assert "INVENTORY NEEDED" not in doc
    assert "| OPEN |" not in doc
    assert "| BLOCKED |" not in doc
    assert "document has no BLOCKED rows" in doc
    assert "Catch target selection" in doc
    assert "Capture wait timer/mash/jump/breakout" in doc
    assert "Throw release flags and facing flip" in doc
    assert "Throw-side blaster command pulses" in doc


def test_throw_release_damage_family_uses_generated_damage_owners() -> None:
    source = (ROOT / "src/throw_flow.c").read_text()
    body = _function_body(source, "throw_flow_action_is_damage_family")

    assert "msl_damage_owner_is_damage_ground_action" in body
    assert "msl_damage_owner_is_damage_air_action" in body
    assert "msl_damage_owner_is_damagefly_action" in body
    assert "MSL_ACT_DAMAGE_FALL" in body
    assert "case MSL_ACT_DAMAGE_HI_1" not in body
    assert "case MSL_ACT_DAMAGE_FLY_HI" not in body


def test_throw_release_callback_order_stays_anim_before_compatibility_cleanup() -> None:
    source = (ROOT / "src/fighter_callbacks.c").read_text()
    body = _function_body(source, "fighter_callbacks_pre_input_anim_phase")

    _assert_ordered(
        body,
        [
            "anim_timebase_update_pre_input(batch)",
            "items_update_pre_fighter_anim_phase(batch)",
            "msl_fighter_callback_context_make(",
            "action_update_anim_callback_pre_input_fighter(&ctx)",
            "action_update_anim_callbacks_pre_input_global(batch)",
            "combat_processhit_consume(batch)",
        ],
    )


def test_throw_ground_loss_uses_source_fall_entry_for_both_fighters() -> None:
    source = (ROOT / "src/throw_flow.c").read_text()
    helper = _function_body(source, "throw_enter_fall_via_ftco_fall_enter")
    body = _function_body(source, "throw_flow_ground_loss_release")

    _assert_ordered(
        helper,
        [
            "const uint8_t was_grounded",
            "msl_locomotion_enter_fall_via_ftco_fall_enter",
            "combat_apply_ftCommon_8007D5D4_ground_to_air",
        ],
    )
    assert body.count("throw_enter_fall_via_ftco_fall_enter") == 2
    assert "enter_fall_release" not in body


def test_dc920_connected_floor_materializes_source_ecb_clear() -> None:
    grab_source = (ROOT / "src/grab_attachment.c").read_text()
    pose_source = (ROOT / "src/mpcoll_ecb_pose.c").read_text()
    release = _function_body(grab_source, "grab_attachment_dc920_release_now")
    clear = _function_body(pose_source, "mpcoll_clear_current_ecb_packet")

    _assert_ordered(
        release,
        [
            "if (connected_floor)",
            "batch->state.coll_substep_cur_pos_y[constrained_idx] = floor.corrected_pos_y",
            "mpcoll_clear_current_ecb_packet(batch, constrained_idx)",
        ],
    )
    assert "mpcoll_store_current_ecb_points" in clear
    assert "mpcoll_store_prev_ecb_points" in clear
    assert "coll_squeeze_restore_ecb_valid[idx] = 0u" in clear

    throw_source = (ROOT / "src/throw_flow.c").read_text()
    anim_body = _function_body(throw_source, "throw_flow_update_anim_callback_pre_input")
    _assert_ordered(
        anim_body,
        [
            "move_tables_throw_should_flip_facing",
            "move_tables_throw_release_hit_idx",
            "grab_attachment_apply_thrown_release_anchor_now",
            "combat_apply_throw_hit",
        ],
    )


def test_throw_release_floor_sweep_prev_endpoint_is_live_for_release_callback() -> None:
    # ftCo_800DDDE4 calls mpColl_800471F8 during the same Throw release callback that detaches and
    # damages the victim. Keep the pre-release attached root in the live sweep endpoint before the
    # simulator release-local DamageFly bridge; the next-frame seed lane carries source
    # CollData.last_pos from the thrower root + thrower ECB midpoint.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpCollPrev,mpColl_80043754}
    source = (ROOT / "src/throw_flow.c").read_text()
    body = _function_body(source, "throw_flow_update_anim_callback_pre_input")
    post_items_body = _function_body(source, "throw_flow_update_post_items")

    _assert_ordered(
        body,
        [
            "const float release_sweep_root_x = batch->state.pos_x[vidx]",
            "throw_release_source_colldata_last_pos(&source_release_last_pos_x",
            "grab_attachment_apply_thrown_release_anchor_now",
            "batch->state.floor_sweep_prev_pos_x[vidx] = release_sweep_root_x",
            "batch->state.floor_sweep_prev_pos_y[vidx] = release_sweep_root_y",
            "batch->state.floor_sweep_seed_prev_pos_x[vidx] = source_release_last_pos_x",
            "batch->state.floor_sweep_seed_prev_valid[vidx] = 1u",
            "batch->state.floor_sweep_prev_runtime_owned[vidx] = 1u",
            "mpcoll_source_air_run_release_471f8",
        ],
    )
    _assert_ordered(
        post_items_body,
        [
            "combat_apply_throw_hit",
            "knockdown_try_throw_release_damage_floor_contact",
        ],
    )


def test_thrown_attachment_comments_match_closed_source_owner() -> None:
    source = (ROOT / "src/grab_attachment.c").read_text()
    checked_bodies = [
        _function_body(source, "grabbed_victim_anchor_world_at_owner_frame"),
        _function_body(source, "grab_attachment_apply_thrown_anchor_now"),
        _function_body(source, "grab_attachment_apply_thrown_release_anchor_now"),
        _function_body(source, "grab_attachment_recompute_offsets_for_thrown_entry"),
        _function_body(source, "grab_attachment_use_static_offsets_for_thrown_entry"),
    ]
    combined = "\n".join(checked_bodies)

    assert "ftCo_800DE508" in combined
    assert "ftCo_800DB368" in combined
    assert "grab_capture_anchor_part_id" in combined
    assert "grab_offset_{y,z}" in combined
    assert "fp->x1A70" in combined
    stale_terms = [
        "Simulator approximation",
        "Decomp-shaped proxy",
        "older anchor proxy",
        "closed cleanly",
        "not a new attached-world anchor formula",
        "until that lane is promoted fully live",
    ]
    for term in stale_terms:
        assert term not in combined
