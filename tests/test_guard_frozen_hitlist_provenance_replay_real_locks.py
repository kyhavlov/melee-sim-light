from __future__ import annotations

from pathlib import Path

import pytest

from tools.eval.dataset import read_dataset

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row


def _skip_if_datasets_missing(root: Path) -> None:
    required = [
        "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl",
        "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl",
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local datasets: {', '.join(missing)}")


@pytest.mark.integration
def test_frozen_guard_provenance_generator_rows_match_proving_set() -> None:
    # Frozen-Guard shield-provenance bridge:
    # - authoritative per-HitCapsule lineage is seeded on the first replay-visible GuardSetOff
    #   hitlag row, then carried across the frozen Guard aftermath rows
    # - QGD clear-required row remains dense-only on the pre-onset Guard row
    #
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    _skip_if_datasets_missing(root)

    cases = [
        (
            root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl",
            (954, 955),
            0,
            1,
            [1, 1, 1, 0],
            [0xFFFF, 0xFFFF, 0xFFFF, 0],
        ),
        (
            root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl",
            (763, 764),
            1,
            0,
            [0, 1, 0, 0],
            [0, 0xFFFF, 0, 0],
        ),
        (
            root / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            (2485,),
            0,
            1,
            [0, 0, 0, 0],
            [0, 0, 0, 0],
        ),
    ]

    for ds_path, records, attacker, victim, expect_valid, expect_cd in cases:
        ds = read_dataset(str(ds_path))
        for record in records:
            seed = ds.samples[record]["seed_t"]
            assert list(map(int, seed["combat_hitlist_hb_valid"][attacker])) == expect_valid
            assert list(map(int, seed["combat_hitlist_hb_cd"][attacker, :, victim])) == expect_cd


@pytest.mark.integration
def test_frozen_guard_provenance_one_step_proving_rows() -> None:
    # Persist-required rows must stay Guard; clear-required row must still enter GuardSetOff.
    root = Path(__file__).resolve().parents[1]
    _skip_if_datasets_missing(root)

    persist_cases = [
        (
            root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl",
            954,
            1,
        ),
        (
            root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl",
            955,
            1,
        ),
        (
            root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl",
            763,
            0,
        ),
        (
            root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl",
            764,
            0,
        ),
    ]
    for ds_path, record, victim in persist_cases:
        _, ref_row, out_row = _run_one_step_row(ds_path, record, victim)
        assert int(ref_row["action_id"][victim]) == 179
        assert int(ref_row["hitlag"][victim]) == 0
        assert int(out_row["action_id"][victim]) == int(ref_row["action_id"][victim])
        assert int(out_row["hitlag"][victim]) == int(ref_row["hitlag"][victim])
        assert int(out_row["hitstun"][victim]) == int(ref_row["hitstun"][victim])

    qgd = root / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    _, ref_row, out_row = _run_one_step_row(qgd, 2485, 1)
    assert int(ref_row["action_id"][1]) == 181
    assert int(ref_row["hitlag"][1]) == 6
    assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1])
    assert int(out_row["hitlag"][1]) == int(ref_row["hitlag"][1])
    assert int(out_row["hitstun"][1]) == int(ref_row["hitstun"][1])
