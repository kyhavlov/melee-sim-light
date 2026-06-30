from __future__ import annotations

from tools.eval.facing_residual_blocker_report import (
    FacingResidualRow,
    build_summary,
    load_action_id_names,
)


def _mk_row(
    *,
    dataset: str = "d.msl",
    record: int,
    p: int = 0,
    seed_action_id: int,
    ref_action_id: int,
    out_action_id: int,
    prev_action_id: int,
    on_ground: int,
    ref_facing: int,
    out_facing: int,
) -> FacingResidualRow:
    return FacingResidualRow(
        dataset=dataset,
        record=record,
        seed_frame=100 + record,
        ref_frame=101 + record,
        p=p,
        seed_action_id=seed_action_id,
        ref_action_id=ref_action_id,
        out_action_id=out_action_id,
        prev_action_id=prev_action_id,
        on_ground=on_ground,
        hitlag=0,
        hitstun=0,
        seed_facing=1,
        ref_facing=ref_facing,
        out_facing=out_facing,
        x2228_b7=0,
        last_hit_by=6,
        combo_count=1,
    )


def test_load_action_id_names_reads_known_ids() -> None:
    names = load_action_id_names()
    assert names[0x000E] == "WAIT"
    assert names[0x00E9] == "ESCAPE_F"
    assert names[0x005B] == "DAMAGE_FLY_ROLL"


def test_build_summary_groups_top_cluster_and_names_actions() -> None:
    names = {
        0x000E: "WAIT",
        0x0058: "DAMAGE_FLY_N",
        0x005B: "DAMAGE_FLY_ROLL",
        0x0019: "JUMP_F",
    }
    rows = [
        _mk_row(
            record=1,
            seed_action_id=0x005B,
            ref_action_id=0x0058,
            out_action_id=0x005B,
            prev_action_id=0x0019,
            on_ground=0,
            ref_facing=1,
            out_facing=0,
        ),
        _mk_row(
            record=2,
            seed_action_id=0x005B,
            ref_action_id=0x0058,
            out_action_id=0x005B,
            prev_action_id=0x0019,
            on_ground=0,
            ref_facing=1,
            out_facing=0,
        ),
        _mk_row(
            record=3,
            seed_action_id=0x000E,
            ref_action_id=0x000E,
            out_action_id=0x000E,
            prev_action_id=0x000E,
            on_ground=1,
            ref_facing=0,
            out_facing=1,
        ),
    ]

    summary = build_summary(rows, action_names=names, top_n=4)

    assert summary["row_count"] == 3
    assert summary["top_ref_out"][0] == {"ref_out": "1->0", "count": 2}

    top_cluster = summary["top_clusters"][0]
    assert top_cluster["count"] == 2
    assert top_cluster["seed_action_name"] == "DAMAGE_FLY_ROLL"
    assert top_cluster["ref_action_name"] == "DAMAGE_FLY_N"
    assert top_cluster["prev_action_name"] == "JUMP_F"
    assert top_cluster["ref_out"] == "1->0"

    blocker_row = summary["blocker_rows"][0]
    assert blocker_row["dataset"] == "d.msl"
    assert blocker_row["seed_action_name"] in {"WAIT", "DAMAGE_FLY_ROLL"}
