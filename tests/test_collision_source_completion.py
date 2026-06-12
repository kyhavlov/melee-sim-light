from __future__ import annotations

import re
from pathlib import Path
from collections import Counter


ROOT = Path(__file__).resolve().parents[1]


def _read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def _phase5_owner_accounting(plan: str) -> dict[str, str]:
    sections: dict[str, str] = {}
    current_name: str | None = None
    current_lines: list[str] = []
    for line in plan.splitlines():
        match = re.match(r"#### Phase 5 Owner Accounting: (.+)", line)
        if match:
            if current_name is not None:
                sections[current_name] = "\n".join(current_lines)
            current_name = match.group(1)
            current_lines = [line]
            continue
        if current_name is not None:
            if line.startswith("#### Phase 5 ") and not line.startswith(
                "#### Phase 5 Owner Accounting:"
            ):
                sections[current_name] = "\n".join(current_lines)
                current_name = None
                current_lines = []
            else:
                current_lines.append(line)
    if current_name is not None:
        sections[current_name] = "\n".join(current_lines)

    token_to_section: dict[str, str] = {}
    counts: Counter[str] = Counter()
    for section_name, body in sections.items():
        for required in (
            "- Owner family:",
            "- Source function(s):",
            "- Why retained after Phases 1-4:",
            "- Test/proof:",
        ):
            assert required in body, section_name
        assert "missing proof" not in body.lower(), section_name
        assert "| Kind | Exact guard name(s) |" in body, section_name
        for token in re.findall(r"`((?:MSL_MPCOLL_REJECT_[A-Z0-9_]+|suppress_[A-Za-z0-9_]+))`", body):
            counts[token] += 1
            token_to_section[token] = section_name
    for token, count in counts.items():
        assert count == 1, token
    return token_to_section


def test_collision_inventory_accounts_for_every_retained_reject_and_suppression() -> None:
    ground_c = _read("src/mpcoll_ground.c")
    plan = _read("agent_docs/core_collision_stage_clip_source_port_plan.md")
    historical_doc = _read("agent_docs/systems/collision.md")

    assert "Phase 5 structured accounting" in plan
    assert "Live `src/mpcoll_ground.c` reject-bit inventory" not in plan
    assert "Live `src/mpcoll_ground.c` suppression inventory" not in plan
    token_to_section = _phase5_owner_accounting(plan)

    reject_bits = sorted(set(re.findall(r"#define (MSL_MPCOLL_REJECT_[A-Z0-9_]+)", ground_c)))
    assert reject_bits
    for bit in reject_bits:
        assert bit in token_to_section, bit

    suppression_predicates = sorted(
        set(re.findall(r"\bconst uint8_t (suppress_[A-Za-z0-9_]+)\b", ground_c))
    )
    assert suppression_predicates
    for predicate in suppression_predicates:
        assert predicate in token_to_section, predicate

    assert set(token_to_section) == set(reject_bits) | set(suppression_predicates)

    retained_fallbacks = (
        "msl_mpcoll_80044628_floor_wall_adjacent_fallback",
        "mpcoll_action_uses_retained_ft80081d0c_air_collision",
        "Static ledge-grab ECB and prev/cur sampling",
        "Landing-contact root-Y helper",
        "generic floor-loss Fall branch",
        "stage_collision_stage_has_deferred_static_floor_transform",
    )
    for token in retained_fallbacks:
        assert token in plan, token

    required_owner_families = {
        "AttackAir",
        "EscapeAir / Ledge-Cliff Handoff",
        "Damage",
        "Fall / FallSpecial",
        "JumpAerial / Common Air",
        "SpecialHi",
        "SpecialAirLw",
        "Ledge / Cliff",
        "Moving-Platform Deferral",
    }
    assert required_owner_families <= set(_phase5_owner_accounting(plan).values())

    assert "MSL_MPCOLL_REJECT_FALLSPECIAL_SAME_FLOOR_EARLY" not in ground_c
    assert "MSL_MPCOLL_REJECT_FALLSPECIAL_SAME_FLOOR_EARLY" in historical_doc
    assert "was deleted after the trace111 audit" in historical_doc


def test_collision_closed_doc_has_no_unfinished_rows() -> None:
    doc = _read("agent_docs/systems/collision.md")
    progress = _read("agent_docs/systems/PROGRESS.md")

    assert "| Collision | CLOSED | [collision.md](collision.md) |" in progress
    for token in ("WRONG/NEEDS WORK", "| BLOCKED |", "TODO"):
        assert token not in doc
    assert "trace111" in doc
    assert "FallSpecial destination collision" in doc


def test_mpcoll_source_comments_do_not_claim_retained_approximation_debt() -> None:
    audited_sources = (
        "src/mpcoll_ground.c",
        "src/mpcoll_wall_ceil.c",
        "src/mpcoll_env.c",
        "src/locomotion.c",
        "src/fighter_callbacks.c",
        "src/damage_terminal_owner.h",
        "src/motion_state_owners.h",
        "src/stage_collision.c",
    )
    text = "\n".join(_read(path) for path in audited_sources)
    stale_patterns = (
        r"\bapproximation\b",
        r"\bapproximations\b",
        r"\bproxy\b",
        r"\bunmodeled\b",
        r"\bnot yet modeled\b",
        r"\bdoes not yet\b",
        r"\buntil .*modeled\b",
    )
    for pattern in stale_patterns:
        assert re.search(pattern, text, flags=re.IGNORECASE) is None, pattern

    edited_collision_stage_comments = "\n".join(
        (_read("src/fighter_callbacks.c"), _read("src/state.h"))
    )
    stale_collision_stage_phrases = (
        "does not yet implement mpColl substeps",
        "does not yet substep collision",
        "Approximate the collision-stage",
        "We approximate a single collision",
    )
    for phrase in stale_collision_stage_phrases:
        assert phrase not in edited_collision_stage_comments, phrase

    stale_phase5_fallback_phrases = (
        "before this fallback can be safely broadened",
        "stale-floor fallback",
        "unmodeled MissFoot",
    )
    for phrase in stale_phase5_fallback_phrases:
        assert phrase not in text, phrase

    plan = _read("agent_docs/core_collision_stage_clip_source_port_plan.md")
    assert "#### Phase 5 Fallback / Keyword Accounting" in plan
    for token in (
        "msl_mpcoll_80044838_floor_edge_snap_from_bottom",
        "Seed-only FoD platform height restore",
        "RETAINED SOURCE-AUTHORITY GUARD",
        "RETAINED STATIC-QUERY CLASSIFIER",
        "RETAINED NON-STATIC OWNER ACCOUNTING",
    ):
        assert token in plan, token

    accounted_keyword_files = {
        "src/mpcoll_ground.c",
        "src/mpcoll_wall_ceil.c",
        "src/locomotion.c",
        "src/fighter_callbacks.c",
        "src/damage_terminal_owner.h",
    }
    keyword_pattern = re.compile(r"\b(fallback|bridge|compat|temporary)\b", re.IGNORECASE)
    for path in audited_sources:
        if keyword_pattern.search(_read(path)):
            assert path in accounted_keyword_files, path
            assert path in plan, path


def test_cliff_floor_handoff_has_no_stage_or_shape_special_case() -> None:
    ground_c = _read("src/mpcoll_ground.c")

    for token in (
        "floor_x_inside_left_ledge_source_band",
        "floor_line_is_positive_generated_sloped_ledge_span",
        "yoshi_sloped_ledge_hard_floor_source_band",
        "fod_escapeair_left_ledge_source_band",
        "FoD generated ledge",
        "Yoshi branch",
        "ported separately",
    ):
        assert token not in ground_c

    selected_block = ground_c[
        ground_c.index("uint8_t cliff_ledge_floor_owner_selected"):
        ground_c.index("\n      const uint8_t prefer_line_is_platform =")
    ]
    for token in ("STAGE_", "stage_id ==", "stage_id !=", "trace", "dataset", "ledge band"):
        assert token not in selected_block
    assert "floor_x_within_line_segment_strict" in selected_block
    assert "floor_line_is_generated_stage_slope" not in selected_block
    assert "floor_line_is_generated_sloped_ledge" not in selected_block

    bottom_owner_block = ground_c[
        ground_c.index("const uint8_t hit_line_matches_carried_cliff_ledge_floor"):
        ground_c.index("const uint8_t escapeair_sustained_floor_handoff")
    ]
    assert "!hit_line_is_slope" not in bottom_owner_block
    assert "hit_line_is_carried_cliff_ledge_floor" in bottom_owner_block
    assert "hit_line_x_in_strict_segment" in bottom_owner_block

    fresh_jump_block = ground_c[
        ground_c.index("Fresh JumpAerial -> EscapeAir ledge bottom-sweep handoff"):
        ground_c.index("uint8_t escapeair_missing_bottom_hard_floor_sweep_owner")
    ]
    for token in (
        "stage_has_height_platform_transform",
        "STAGE_",
        "griz.bin",
        "FoD",
        "Yoshi",
        "source band",
        "ledge band",
        "generated-slope exception",
        "flat-only",
    ):
        assert token not in fresh_jump_block
    assert "mpcoll_collect_bottom_sweep_hit" in fresh_jump_block
    assert "floor_sweep.hit_is_ledge" in fresh_jump_block
    assert "floor_x_within_line_bounds" in fresh_jump_block

    high_lift_block = ground_c[
        ground_c.index("const uint8_t suppress_jumpaerial_escapeair_high_lift_ledge_final_land"):
        ground_c.index("const uint8_t suppress_jumpaerial_escapeair_static_platform_overstep_final_land")
    ]
    assert "separate high-lift entry suppression, not the carried-cliff publication owner" in high_lift_block
    assert "generated sloped carried-floor handoffs have their own prefix proof" in high_lift_block
    assert "!final_ground_line_is_sloped_ledge" in high_lift_block


def test_phase6_moving_surface_owner_is_packet_driven_not_replay_or_stage_shortcut() -> None:
    stage_c = _read("src/stage_collision.c")
    moving_owner = stage_c[
        stage_c.index("uint8_t stage_collision_floor_line_moving_surface_state"):
        stage_c.index("const MslStageCeilingGraph* stage_collision_get_ceiling_graph")
    ]

    assert "stage_collision_floor_line_moving_surface_state_impl(batch, bi, line, 0u, &surface)" in moving_owner
    assert "stage_collision_floor_line_moving_surface_state_impl(batch, bi, line, 1u, out)" in moving_owner
    assert "stage_collision_platform_path_world_line" in moving_owner
    assert "stage_collision_fod_height_platform_line_state" in moving_owner
    assert "stage_fod_platform_velocity" in moving_owner

    for token in (
        "trace",
        "dataset",
        ".msl",
        ".slp",
        "replay record",
        "hardcoded record",
        "stage_id ==",
        "stage_id !=",
        "MSL_STAGE_ID_FOUNTAIN_OF_DREAMS",
        "MSL_STAGE_ID_YOSHIS_STORY",
        "ledge band",
        "source band",
    ):
        assert token not in moving_owner, token

    static_floor_filter = stage_c[
        stage_c.index("static inline uint8_t stage_static_floor_line_query_active"):
        stage_c.index("static inline uint8_t stage_static_line_query_active")
    ]
    assert "MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT" not in static_floor_filter
    assert "MSL_STAGE_PLATFORM_TRANSFORM_RANDALL" not in static_floor_filter
    assert "MSL_STAGE_PLATFORM_TRANSFORM_STATIC_Y" in static_floor_filter


def test_phase2_mpcoll_substrate_is_not_routed_through_item_or_special_only_sources() -> None:
    # Phase 2 owns common CollData/mpColl substrate, not item-only collision routing or bespoke
    # special-action callbacks. Keep this as a source-scope guard so future helper plumbing cannot
    # satisfy the Phase 2 tests by entering item/special owners.
    item_sources = "\n".join(
        _read(path)
        for path in (
            "src/items.c",
            "src/item_reflect.h",
            "src/item_common_params.c",
            "src/item_article_params.c",
            "src/stage_item_params.c",
        )
    )
    for token in (
        "mpcoll_colldata_copy",
        "mpcoll_check_bounding",
        "mpcoll_end_static_events",
        "stage_collision_static_query",
        "debug_copy_colldata",
    ):
        assert token not in item_sources, token

    special_only_sources = "\n".join(
        _read(path)
        for path in (
            "src/shine.c",
            "src/specialhi_pose.h",
            "src/special_msids.c",
        )
    )
    for token in (
        "mpcoll_colldata_copy",
        "mpcoll_check_bounding",
        "mpcoll_end_static_events",
        "stage_collision_static_query",
        "debug_copy_colldata",
    ):
        assert token not in special_only_sources, token


def test_specialhi_jobj_ecb_floor_owner_stays_launch_scoped_until_full_packet() -> None:
    ground_c = _read("src/mpcoll_ground.c")
    helper_match = re.search(
        r"static inline uint8_t mpcoll_ground_specialhi_uses_jobj_ecb"
        r"\([^)]*\) \{(?P<body>.*?)\n\}",
        ground_c,
        flags=re.DOTALL,
    )
    assert helper_match is not None
    helper_body = helper_match.group("body")

    # Owner identity now comes from the extracted MotionState row kind (the spacie
    # dispatch migration); the launch-scoped set is SpecialHi/SpecialAirHi only.
    assert "MSL_FX_KIND_SPECIAL_AIR_HI" in helper_body
    assert "MSL_FX_KIND_SPECIAL_HI_FALL" not in helper_body
    assert "mpColl_LoadECB_JObj" in helper_body
