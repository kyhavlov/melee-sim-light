from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_collision_inventory_accounts_for_every_retained_reject_and_suppression() -> None:
    ground_c = _read("src/mpcoll_ground.c")
    doc = _read("agent_docs/systems/collision.md")

    reject_bits = sorted(set(re.findall(r"#define (MSL_MPCOLL_REJECT_[A-Z0-9_]+)", ground_c)))
    assert reject_bits
    for bit in reject_bits:
        assert f"`{bit}`" in doc, bit

    suppression_predicates = sorted(
        set(re.findall(r"\bconst uint8_t (suppress_[A-Za-z0-9_]+)\b", ground_c))
    )
    assert suppression_predicates
    for predicate in suppression_predicates:
        assert f"`{predicate}`" in doc, predicate

    assert "MSL_MPCOLL_REJECT_FALLSPECIAL_SAME_FLOOR_EARLY" not in ground_c
    assert "MSL_MPCOLL_REJECT_FALLSPECIAL_SAME_FLOOR_EARLY" in doc
    assert "was deleted after the trace111 audit" in doc


def test_collision_closed_doc_has_no_unfinished_rows() -> None:
    doc = _read("agent_docs/systems/collision.md")
    progress = _read("agent_docs/systems/PROGRESS.md")

    assert "| Collision | CLOSED | [collision.md](collision.md) |" in progress
    for token in ("WRONG/NEEDS WORK", "| BLOCKED |", "TODO"):
        assert token not in doc
    assert "trace111" in doc
    assert "FallSpecial destination collision" in doc


def test_mpcoll_source_comments_do_not_claim_retained_approximation_debt() -> None:
    text = "\n".join(
        _read(path)
        for path in (
            "src/mpcoll_ground.c",
            "src/mpcoll_wall_ceil.c",
            "src/stage_collision.c",
        )
    )
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
