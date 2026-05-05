from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


def _aggregate_dataset(root: Path, name: str) -> Path:
    return root / "datasets" / "aggregate_recent" / "replays" / "validation" / "aggregate_recent" / name


def _primary_cardinal_dataset(root: Path, name: str) -> Path:
    return (
        root
        / "datasets"
        / "fox_falco_fd_ucf084_recent"
        / "replays"
        / "validation"
        / "cardinal_1.0_recent"
        / name
    )


@pytest.mark.integration
@pytest.mark.parametrize("record", [2181, 7338])
def test_processhit_source_owner_uses_raw_source_port_for_aggregate_ports(record: int) -> None:
    # Percent-only / KB-zero ProcessHit ownership must write Slippi's raw source-port domain
    # (`last_hit_by`) while preserving simulator-local instance identity (`instance_hit_by`).
    #
    # Source anchors:
    # - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # - refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078754,ftColl_8007891C}
    # - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _aggregate_dataset(root, "HungryImportantSnake.msl")
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, ref, out = _run_one_step_row(dataset_path, record, 0)

    assert int(seed["source_port0"][1]) == 3
    assert int(seed["last_hit_by"][0]) == 3
    assert int(out["last_hit_by"][0]) == int(ref["last_hit_by"][0]) == 3
    assert int(out["instance_hit_by"][0]) == int(ref["instance_hit_by"][0])


@pytest.mark.integration
def test_processhit_source_owner_synthetic_source_port_mutation_changes_only_port_domain() -> None:
    # Synthetic variant of the same retained owner: remap the live attacker slot to a different raw
    # controller port and require only the source-port lane to follow that remap. This guards
    # against writing compact local slot ids into `last_hit_by`.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _aggregate_dataset(root, "HungryImportantSnake.msl")
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    def remap_attacker_source_port(seed_t):
        seed_t["source_port0"][0, 1] = 2

    seed, ref, out = _run_one_step_row(dataset_path, 2181, 0, seed_mutator=remap_attacker_source_port)

    assert int(seed["source_port0"][1]) == 2
    assert int(out["last_hit_by"][0]) == 2
    assert int(ref["last_hit_by"][0]) == 3
    assert int(out["instance_hit_by"][0]) == int(ref["instance_hit_by"][0])


@pytest.mark.integration
def test_nonflinch_item_source_owner_synthetic_source_port_keeps_item_instance_identity() -> None:
    # Non-flinch item hits route through the percent-only ProcessHit lane: victim percent changes,
    # hitlag/hitstun/action stay non-flinch, `last_hit_by` is the raw owner source-port, and
    # `instance_hit_by` is the live item instance id.
    #
    # Source anchors:
    # - refs/melee/src/melee/it/itcoll.c::it_80272460
    # - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # - refs/melee/src/melee/ft/ftcoll.c::{ftColl_800787B4,ftColl_80078998}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _primary_cardinal_dataset(root, "TreasuredBackKangaroo.msl")
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    def remap_item_owner_source_port(seed_t):
        seed_t["source_port0"][0, 0] = 2

    seed, ref, out = _run_one_step_row(dataset_path, 197, 1, seed_mutator=remap_item_owner_source_port)

    assert int(seed["source_port0"][0]) == 2
    assert int(seed["hitlag"][1]) == int(ref["hitlag"][1]) == int(out["hitlag"][1]) == 0
    assert int(seed["hitstun"][1]) == int(ref["hitstun"][1]) == int(out["hitstun"][1]) == 0
    assert int(seed["action_id"][1]) == int(ref["action_id"][1]) == int(out["action_id"][1])
    assert float(ref["percent"][1]) > float(seed["percent"][1])
    assert int(out["last_hit_by"][1]) == 2
    assert int(ref["last_hit_by"][1]) == 0
    assert int(out["instance_hit_by"][1]) == int(ref["instance_hit_by"][1]) == int(seed["items"][4]["instance_id"])
