from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _DamageFallLandingCase:
    dataset_rel: str
    record: int
    port: int
    seed_action_id: int
    ref_action_id: int
    note: str


@dataclass(frozen=True)
class _DamageFallNoEscapeCase:
    record: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DamageFallLandingCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
            ),
            record=4299,
            port=0,
            seed_action_id=38,
            ref_action_id=38,
            note="AGG DamageFall landing family A target-1 row",
        ),
        _DamageFallLandingCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
            ),
            record=4300,
            port=0,
            seed_action_id=38,
            ref_action_id=199,
            note="AGG DamageFall landing family A target row",
        ),
        _DamageFallLandingCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
            ),
            record=4301,
            port=0,
            seed_action_id=199,
            ref_action_id=199,
            note="AGG DamageFall landing family A target+1 row",
        ),
        _DamageFallLandingCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
            ),
            record=6068,
            port=1,
            seed_action_id=38,
            ref_action_id=38,
            note="GAT DamageFall landing family B target-1 row",
        ),
        _DamageFallLandingCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
            ),
            record=6069,
            port=1,
            seed_action_id=38,
            ref_action_id=199,
            note="GAT DamageFall landing family B target row",
        ),
        _DamageFallLandingCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
            ),
            record=6070,
            port=1,
            seed_action_id=199,
            ref_action_id=199,
            note="GAT DamageFall landing family B target+1 row",
        ),
    ],
)
def test_damagefall_landing_passive_replay_real_lock(case: _DamageFallLandingCase) -> None:
    # Replay-real lock for DamageFall ground contact:
    # - ftCo_DamageFall_Coll delegates to ft_80090984 on floor contact.
    # - ftCo_80090984 tries PassiveStand, then Passive, then DownBound.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::{
    #   ftCo_DamageFall_Coll,ftCo_80090984
    # }
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_80098928
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_8009872C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(case.record)
    p = int(case.port)
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == int(case.seed_action_id), case.note
    assert int(ref["action_id"][p]) == int(case.ref_action_id), case.note
    assert int(seed["hitlag"][p]) == 0, case.note
    assert int(ref["hitlag"][p]) == 0, case.note
    assert int(seed["hitstun"][p]) == 0, case.note
    assert int(ref["hitstun"][p]) == 0, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "on_ground", "jumps_left"):
        got = int(out_row[field][p])
        want = int(ref_row[field][p])
        assert got == want, f"record={record} p={p} field={field} expected={want} got={got}"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _DamageFallNoEscapeCase(
            record=6065,
            note="GAT DamageFall no-EscapeAir family target-1 row",
        ),
        _DamageFallNoEscapeCase(
            record=6066,
            note="GAT DamageFall no-EscapeAir family target row",
        ),
        _DamageFallNoEscapeCase(
            record=6067,
            note="GAT DamageFall no-EscapeAir family target+1 row",
        ),
    ],
)
def test_damagefall_iasa_no_escapeair_replay_real_lock(case: _DamageFallNoEscapeCase) -> None:
    # Replay-real lock for DamageFall IASA ownership:
    # - DamageFly IASA can delegate into DamageFall/JumpAerial handling.
    # - DamageFall_IASA does not call ftCo_80099A58, so EscapeAir is not a legal follow-up here.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_DamageFly_IASA,ftCo_DamageFly_Anim
    # }
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A58
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(case.record)
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["hitlag"][p]) == 0, case.note
    assert int(ref["hitlag"][p]) == 0, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "hitstun", "on_ground"):
        got = int(out_row[field][p])
        want = int(ref_row[field][p])
        assert got == want, f"record={record} p={p} field={field} expected={want} got={got}"
