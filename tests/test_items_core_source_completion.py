from __future__ import annotations

import re
from pathlib import Path

import pytest

from tools.extraction.known_data_artifacts import dream_whispy_metadata, yoshi_shyguy_metadata


ROOT = Path(__file__).resolve().parents[1]


def _read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_items_core_inventory_and_progress_are_closed_for_supported_scope() -> None:
    doc = _read("agent_docs/systems/items_core.md")
    progress = _read("agent_docs/systems/PROGRESS.md")

    assert "TODO" not in doc
    assert "| TODO:" not in doc
    table_rows = [line for line in doc.splitlines() if line.startswith("| ") and not line.startswith("| Source")]
    assert not any("| BLOCKED |" in line for line in table_rows)
    assert "| Items Core | OPEN |" not in progress
    assert "unsupported arbitrary item kinds" in doc.lower()
    assert "RETAINED SOURCE-POLICY" in doc
    assert "| Items Core | CLOSED | [items_core.md](items_core.md) |" in progress


def test_item_slot_swap_keeps_public_and_hidden_item_lanes_together() -> None:
    # Item sorting is MSL's deterministic public-state policy over source item identity. Any swap
    # omission or duplicated swap can detach hidden source state from the public item lane.
    items_c = _read("src/items.c")
    match = re.search(
        r"static inline void item_slot_swap\(.*?\n\}(?=\n\n)",
        items_c,
        flags=re.DOTALL,
    )
    assert match is not None
    body = match.group(0)
    swapped = re.findall(r"SWAP\([^,]+,\s*batch->state\.([a-zA-Z0-9_]+)\)", body)

    assert len(swapped) == len(set(swapped)), sorted(
        field for field in set(swapped) if swapped.count(field) > 1
    )
    for field in (
        "item_exists",
        "item_state",
        "item_type",
        "item_owner",
        "item_instance_id",
        "item_attack_id",
        "item_attack_instance",
        "item_direction",
        "item_vel_x",
        "item_vel_y",
        "item_pos_x",
        "item_pos_y",
        "item_damage",
        "item_timer",
        "item_hitlag",
        "item_spawn_id",
        "item_hidden_body_hit_victim_port",
        "item_shyguy_prev_vel_y",
        "item_shyguy_dyn_y_phase",
        "item_shyguy_speed_index",
        "item_shyguy_delay",
        "item_shyguy_hitlag",
    ):
        assert field in swapped

    assert "batch->state.item_hitlist[a_base + hb]" in body
    assert "batch->state.item_hitlist[b_base + hb]" in body


def test_supported_item_scope_comments_do_not_claim_unfinished_article_promotion() -> None:
    item_phase_text = _read("src/items.h") + "\n" + _read("src/items.c")

    assert "until each article owner is promoted" not in item_phase_text
    assert "until each article is promoted independently" not in item_phase_text
    assert "Full arbitrary Melee item GObj behavior is" in _read(
        "agent_docs/systems/items_core.md"
    )


def test_supported_item_comments_match_inventory_policy_rows() -> None:
    items_c = _read("src/items.c")
    doc = _read("agent_docs/systems/items_core.md")

    stale_phrases = (
        "until its floor-contact phase owner is completed",
        "Known remaining Side-B/Illusion decomp lanes not yet modeled here",
        "remaining hitlist/callback owners are exposed",
        "TODO(decomp/items-grounded-body-gating)",
        "narrow miss-only LandingFallSpecial bridge",
    )
    for phrase in stale_phrases:
        assert phrase not in items_c

    assert "Side-special illusion/phantasm accessory ghost lanes" in doc
    assert "Laser-specific grounded BODY geometry/admission caveats" in doc
    assert "RETAINED SOURCE-POLICY" in doc


def test_stage_item_source_artifacts_cover_shyguy_and_whispy_gameplay() -> None:
    shyguy = yoshi_shyguy_metadata(ROOT / "data")
    whispy = dream_whispy_metadata(ROOT / "data")

    assert shyguy.stage_id == 8
    assert shyguy.item_kind == 0xD2
    assert shyguy.timer_min == 600
    assert shyguy.timer_rand == 1800
    assert shyguy.hurtboxes
    assert shyguy.speed == pytest.approx((0.3, 0.5, 0.75))
    assert len(shyguy.dyn_y_vel) == 128

    assert whispy.stage_id == 28
    assert whispy.wind_speed == pytest.approx(0.2)
    assert whispy.left_rect_left < whispy.left_rect_right
    assert whispy.right_rect_left < whispy.right_rect_right


def test_item_reseed_copies_supported_hidden_item_source_state() -> None:
    api_c = _read("src/api.c")

    for token in (
        "item_spawn_id_counter",
        "item_reflect_transfer_seed_port",
        "item_shield_bounce_seed_valid",
        "item_hidden_body_hit_victim_port",
        "item_shyguy_prev_vel_y",
        "item_shyguy_dyn_y_phase",
        "item_shyguy_speed_index",
        "item_shyguy_delay",
        "item_shyguy_hitlag",
        "stage_yoshi_shyguy_timer",
        "stage_dream_whispy_wind_timer",
        "item_hitlist_victim_port",
    ):
        assert token in api_c

    assert "Recover the move id from the owner's action-move table" in api_c
