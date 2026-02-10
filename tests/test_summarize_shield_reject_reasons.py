from __future__ import annotations

from tools.eval.summarize_shield_reject_reasons import _summarize_row


def test_summarize_row_prefers_pair_gate_first_failure() -> None:
    row = {
        "row": {
            "dataset": "datasets/x/Foo.msl",
            "record": 123,
            "p": 1,
        },
        "pre_combat": {
            "shield_candidate_decisions": [
                {
                    "source_kind": 1,
                    "attacker": 0,
                    "defender": 1,
                    "hitbox_id": 255,
                    "reject_reason": 4,
                },
                {
                    "source_kind": 0,
                    "attacker": 0,
                    "defender": 1,
                    "hitbox_id": 0,
                    "reject_reason": 9,
                },
            ],
        },
    }

    out = _summarize_row(row)
    assert out["row"] == "Foo.msl:123:1"
    assert out["first_failing_reason"] == "REJECT_HITLAG_GATE"
    assert out["first_failing_gate"] == "ftColl_80078C70 pair hitlag gate"
    assert "Runtime hitlag latch/decrement ordering mismatch" in out["recommendation"]
