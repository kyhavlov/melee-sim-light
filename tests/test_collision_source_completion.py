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
        "RETAINED PHASE-6 DEFERRAL",
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
