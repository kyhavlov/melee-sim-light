from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


ACT_THROW_LW = 222
ACT_THROWN_LW = 242


@pytest.mark.integration
def test_throw_owner_body_hitbox_damages_attached_victim_without_victim_hitlag() -> None:
    # Sheik Down Throw has ordinary create_hitbox capsules before set_throw_flags(0) releases the
    # victim. While the defender is still attached in ThrownLw, source publishes damage and
    # thrower-side hitlag/bookkeeping, but the victim does not enter visible hitlag.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE508,ftCo_ThrownLw_Phys,ftCo_ThrownLw_Coll}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007891C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/sheik/replays/validation/sheik/RuralReasonableRat.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 3160
    thrower = 0
    victim = 1
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][thrower]) == ACT_THROW_LW
    assert int(row["seed_t"]["action_id"][victim]) == ACT_THROWN_LW
    assert int(row["seed_t"]["grab_owner_port"][victim]) == thrower
    assert int(row["seed_t"]["hitlag"][thrower]) == 0
    assert int(row["seed_t"]["hitlag"][victim]) == 0
    assert int(row["ref_t1"]["hitlag"][thrower]) > 0
    assert int(row["ref_t1"]["hitlag"][victim]) == 0

    _seed, ref, out = _run_one_step_row(dataset_path, record, victim)
    assert int(out["action_id"][victim]) == ACT_THROWN_LW
    assert int(out["hitlag"][thrower]) == int(ref["hitlag"][thrower])
    assert int(out["hitlag"][victim]) == 0
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]))
    assert int(out["last_attack_landed"][thrower]) == int(ref["last_attack_landed"][thrower])


@pytest.mark.integration
def test_throw_owner_body_hitbox_hitlag_suppression_requires_attachment() -> None:
    # Adjacent synthetic negative: if the same throw-state BODY hitbox overlaps a non-attached
    # defender, it falls back to ordinary BODY hitlag. This protects against a broad ThrowLw or
    # action-id-only suppression.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/sheik/replays/validation/sheik/RuralReasonableRat.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 3160
    victim = 1

    def clear_attachment(seed_t):
        seed_t["grab_owner_port"][0, victim] = 0xFF

    _seed, _ref, out = _run_one_step_row(dataset_path, record, victim, seed_mutator=clear_attachment)
    assert int(out["hitlag"][victim]) > 0


@pytest.mark.integration
def test_direct_kb_pre_release_throw_body_hit_keeps_attached_victim_hitlag() -> None:
    # Adjacent real-row negative: Fox/Falco ThrowF has an ordinary BODY hit before its
    # set_throw_flags(0) release frame, but that script payload carries direct knockback. It must
    # keep the normal attached-victim hitlag path instead of inheriting Sheik's zero-direct-kb
    # pre-release damage pulse behavior.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 215
    thrower = 1
    victim = 0
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][thrower]) == 219
    assert int(row["seed_t"]["grab_owner_port"][victim]) == thrower
    assert int(row["ref_t1"]["hitlag"][victim]) > 0

    _seed, ref, out = _run_one_step_row(dataset_path, record, victim)
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim])
