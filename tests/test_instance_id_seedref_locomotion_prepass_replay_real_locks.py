from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _SeedRefInstanceIdCase:
    dataset_rel: str
    record: int
    port: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _SeedRefInstanceIdCase(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "AttachedGoodNaturedGuanaco.slpz"
            ),
            record=4388,
            port=1,
            note="AGN seed==ref instance_id guard row (grounded callback transition)",
        ),
        _SeedRefInstanceIdCase(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            record=1370,
            port=0,
            note="GAT seed==ref instance_id guard row",
        ),
        _SeedRefInstanceIdCase(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.slpz"
            ),
            record=1933,
            port=0,
            note="QGD seed==ref instance_id guard row",
        ),
        _SeedRefInstanceIdCase(
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "TreasuredBackKangaroo.slpz"
            ),
            record=2377,
            port=0,
            note="TBK seed==ref instance_id guard row",
        ),
    ],
)
def test_seedref_instance_id_rows_stay_replay_exact_after_locomotion_prepass(
    case: _SeedRefInstanceIdCase,
) -> None:
    # Replay-real guard for same-frame locomotion/callback ordering:
    # - startup-complete KneeBend jump enters are callback-owned before later grounded IASA chains;
    # - that ordering must not over-consume the shared Fighter_ChangeMotionState instance_id lane on
    #   rows where replay already seeds the next fighter instance_id exactly.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::{ftCo_KneeBend_Anim,ftCo_KneeBend_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Wait.c,ftCo_Turn.c,ftCo_Dash.c,ftCo_Walk.c}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    rec = int(case.record)
    p = int(case.port)
    assert int(samples.shape[0]) > rec, f"replay too short for lock row: record={rec}"

    row = samples[rec]
    assert int(row["seed_t"]["instance_id"][p]) == int(row["ref_t1"]["instance_id"][p]), case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
    _assert_transition_lock_fields_match_ref(
        out_row=out_row,
        ref_row=ref_row,
        record=rec,
        p=p,
    )
