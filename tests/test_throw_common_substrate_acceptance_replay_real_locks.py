from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


@dataclass(frozen=True)
class _Case:
    name: str
    dataset_rel: str
    record: int
    thrower: int
    victim: int
    family: str


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            name="throwf_attached_hit",
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "AttachedGoodNaturedGuanaco.slpz"
            ),
            record=215,
            thrower=1,
            victim=0,
            family="ThrowF",
        ),
        _Case(
            name="throwhi_release",
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "TreasuredBackKangaroo.slpz"
            ),
            record=444,
            thrower=0,
            victim=1,
            family="ThrowHi",
        ),
        _Case(
            name="throwb_attached",
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.slpz"
            ),
            record=2509,
            thrower=1,
            victim=0,
            family="ThrowB",
        ),
        _Case(
            name="throwlw_attached_pulse",
            dataset_rel=(
                "replays/validation/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.slpz"
            ),
            record=443,
            thrower=0,
            victim=1,
            family="ThrowLw",
        ),
    ],
)
def test_common_throw_substrate_rows_keep_core_attached_release_shape(case: _Case) -> None:
    root = _root()
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.record, case.thrower)

    for p in (case.thrower, case.victim):
        for field in (
            "action_id",
            "action_frame",
            "animation_index",
            "hitlag",
            "on_ground",
            "ground_id",
            "instance_id",
        ):
            assert int(out_row[field][p]) == int(ref_row[field][p]), (
                f"{case.family}: record={case.record} p={p} field={field} "
                f"expected={int(ref_row[field][p])} got={int(out_row[field][p])}"
            )


@pytest.mark.integration
def test_common_throw_substrate_throwb_keeps_intentional_combo_bridge_behavior() -> None:
    # ThrowB still has an intentional per-throw projectile bookkeeping bridge on top of the shared
    # attachment/release substrate. Keep that accepted behavior explicit while the common substrate
    # work broadens underneath it.
    root = _root()
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    thrower = 1
    victim = 0
    record = 9960
    _, ref_row, out_row = _run_one_step_row(dataset_path, record, thrower)

    assert int(out_row["action_id"][thrower]) == int(ref_row["action_id"][thrower]) == 220
    assert int(out_row["combo_count"][thrower]) == int(ref_row["combo_count"][thrower])
    assert int(out_row["last_attack_landed"][thrower]) == int(ref_row["last_attack_landed"][thrower])
    assert int(out_row["last_hit_by"][victim]) == int(ref_row["last_hit_by"][victim])
