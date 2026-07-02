from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


ACT_DAMAGE_AIR_2 = 0x0055
ACT_LANDING = 0x002A
ACT_DOWN_DAMAGE_D = 0x00C1
HITSTUN_FLAG_221C = 0x02


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    port: int
    note: str
    seed_state_flags_3: int
    ref_state_flags_3: int
    seed_hitlag: int
    ref_hitstun: int


_LANDING_CLEAR_CASES = (
    _Case(
        dataset_rel="replays/validation/yoshis_story_recent/CheeryNumbMonkey.slpz",
        record=306,
        port=1,
        note="Cheery DamageAir2 -> Landing clears x221C_b6 on Landing_Enter_Basic",
        seed_state_flags_3=0x02,
        ref_state_flags_3=0x00,
        seed_hitlag=0,
        ref_hitstun=0,
    ),
    _Case(
        dataset_rel="replays/validation/pokemon_stadium_recent/ThisVioletRaccoon.slpz",
        record=705,
        port=1,
        note="ThisViolet later DamageAir2 -> Landing clears x221C_b6 on Landing_Enter_Basic",
        seed_state_flags_3=0x02,
        ref_state_flags_3=0x00,
        seed_hitlag=0,
        ref_hitstun=0,
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz"
        ),
        record=3599,
        port=1,
        note="TBK cardinal DamageAir2 -> Landing clears x221C_b6 on Landing_Enter_Basic",
        seed_state_flags_3=0x02,
        ref_state_flags_3=0x00,
        seed_hitlag=0,
        ref_hitstun=0,
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    _LANDING_CLEAR_CASES,
    ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.record}-p{c.port}",
)
def test_damageair2_landing_entry_clears_hitstun_flag(case: _Case) -> None:
    # Replay-real lock for Landing_Enter_Basic ownership after an airborne Damage collision callback:
    # - ftCo_Damage_Coll can enter Landing_Enter_Basic while damage hitstun remains nonzero,
    # - ftCo_8008F744 clears fp->x221C_b6 once the damage-state x0 timer has expired, and
    # - the Landing destination post-frame should not reconstitute x221C_b6 from the prior
    #   DamageAir2 carry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_8008F744}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > case.record, f"replay too short for lock row: record={case.record}"
    row = samples[case.record]
    seed = row["seed_t"]
    ref = row["ref_t1"]
    p = case.port

    assert int(seed["action_id"][p]) == ACT_DAMAGE_AIR_2, case.note
    assert int(ref["action_id"][p]) == ACT_LANDING, case.note
    assert int(seed["hitlag"][p]) == case.seed_hitlag, case.note
    assert int(ref["hitstun"][p]) == case.ref_hitstun, case.note
    assert int(seed["state_flags"][p, 3]) == case.seed_state_flags_3, case.note
    assert int(ref["state_flags"][p, 3]) == case.ref_state_flags_3, case.note

    _, ref_row, out = _run_one_step_row(dataset_path, case.record, p)
    assert int(out["action_id"][p]) == int(ref_row["action_id"][p]) == ACT_LANDING, case.note
    assert int(out["hitstun"][p]) == int(ref_row["hitstun"][p]), case.note
    assert int(out["state_flags"][p, 3]) == int(ref_row["state_flags"][p, 3]) == case.ref_state_flags_3, (
        f"{case.note}: state_flags[3] expected={case.ref_state_flags_3:#04x} "
        f"got={int(out['state_flags'][p, 3]):#04x}"
    )
    assert (int(out["state_flags"][p, 3]) & HITSTUN_FLAG_221C) == 0, case.note


def test_ongoing_damageair2_hitstun_flag_still_tracks_damage_state_owner() -> None:
    # Negative control: the Landing clear above must not suppress x221C_b6 for an ongoing
    # DamageAir2 row that remains under the damage action callback.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / "replays/validation/yoshis_story_recent/CheeryNumbMonkey.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = 1
    record = 305
    assert int(samples.shape[0]) > record, "replay too short for lock row"
    row = samples[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]

    assert int(seed["action_id"][p]) == ACT_DAMAGE_AIR_2
    assert int(ref["action_id"][p]) == ACT_DAMAGE_AIR_2
    assert (int(ref["state_flags"][p, 3]) & HITSTUN_FLAG_221C) != 0

    _, ref_row, out = _run_one_step_row(dataset_path, record, p)
    assert int(out["action_id"][p]) == int(ref_row["action_id"][p]) == ACT_DAMAGE_AIR_2
    assert int(out["state_flags"][p, 3]) == int(ref_row["state_flags"][p, 3])
    assert (int(out["state_flags"][p, 3]) & HITSTUN_FLAG_221C) != 0


@dataclass(frozen=True)
class _DownDamageCase:
    dataset_rel: str
    record: int
    port: int
    note: str
    seed_hitstun: int
    ref_hitstun: int


_DOWN_DAMAGE_CARRY_CASES = (
    _DownDamageCase(
        dataset_rel="replays/validation/aggregate_recent/MotionlessAggressiveJay.slpz",
        record=5484,
        port=1,
        note="Motionless DownDamageD keeps x221C_b6 after visible hitstun scalar reaches zero",
        seed_hitstun=1,
        ref_hitstun=0,
    ),
    _DownDamageCase(
        dataset_rel="replays/validation/aggregate_recent/HungryImportantSnake.slpz",
        record=5221,
        port=0,
        note="Hungry DownDamageD carries x221C_b6 with no visible hitstun scalar",
        seed_hitstun=0,
        ref_hitstun=0,
    ),
    _DownDamageCase(
        dataset_rel="replays/validation/pokemon_stadium_recent/ThisVioletRaccoon.slpz",
        record=1181,
        port=0,
        note="ThisViolet DownDamageD keeps x221C_b6 through the DownDamage anim callback",
        seed_hitstun=1,
        ref_hitstun=0,
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    _DOWN_DAMAGE_CARRY_CASES,
    ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.record}-p{c.port}",
)
def test_downdamaged_preserves_hitstun_flag_independent_of_visible_hitstun(case: _DownDamageCase) -> None:
    # Replay-real lock for DownDamage's separate timer/callback owner:
    # - ftCo_8009F184 re-enters common damage setup with DownDamageD,
    # - DownDamage_Anim decrements mv.co.downdamage.x0 and does not call ftCo_8008F744 while the
    #   fighter remains in DownDamageD, and
    # - the replay-facing hitstun scalar can reach zero while x221C_b6 remains visible.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{ftCo_8009F184,ftCo_DownDamage_Anim}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > case.record, f"replay too short for lock row: record={case.record}"
    row = samples[case.record]
    seed = row["seed_t"]
    ref = row["ref_t1"]
    p = case.port

    assert int(seed["action_id"][p]) == ACT_DOWN_DAMAGE_D, case.note
    assert int(ref["action_id"][p]) == ACT_DOWN_DAMAGE_D, case.note
    assert int(seed["hitstun"][p]) == case.seed_hitstun, case.note
    assert int(ref["hitstun"][p]) == case.ref_hitstun, case.note
    assert (int(seed["state_flags"][p, 3]) & HITSTUN_FLAG_221C) != 0, case.note
    assert (int(ref["state_flags"][p, 3]) & HITSTUN_FLAG_221C) != 0, case.note

    _, ref_row, out = _run_one_step_row(dataset_path, case.record, p)
    assert int(out["action_id"][p]) == int(ref_row["action_id"][p]) == ACT_DOWN_DAMAGE_D, case.note
    assert int(out["hitstun"][p]) == int(ref_row["hitstun"][p]) == case.ref_hitstun, case.note
    assert int(out["state_flags"][p, 3]) == int(ref_row["state_flags"][p, 3]), (
        f"{case.note}: state_flags[3] expected={int(ref_row['state_flags'][p, 3]):#04x} "
        f"got={int(out['state_flags'][p, 3]):#04x}"
    )
    assert (int(out["state_flags"][p, 3]) & HITSTUN_FLAG_221C) != 0, case.note


def test_downdamaged_does_not_synthesize_missing_hitstun_flag() -> None:
    # Negative: the runtime owner is the carried DownDamage x221C_b6 lane. If the seed row lacks
    # that source-owned flag, DownDamage must not synthesize it from action id alone.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / "replays/validation/aggregate_recent/MotionlessAggressiveJay.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    p = 1
    record = 5484

    def clear_hitstun_flag(seed_t) -> None:
        seed_t["state_flags"][0, p, 3] = int(seed_t["state_flags"][0, p, 3]) & (0xFF ^ HITSTUN_FLAG_221C)
        seed_t["hitstun"][0, p] = 0

    seed, _, out = _run_one_step_row(dataset_path, record, p, seed_mutator=clear_hitstun_flag)
    assert int(seed["action_id"][p]) == ACT_DOWN_DAMAGE_D
    assert int(seed["hitstun"][p]) == 0
    assert (int(seed["state_flags"][p, 3]) & HITSTUN_FLAG_221C) == 0
    assert (int(out["state_flags"][p, 3]) & HITSTUN_FLAG_221C) == 0
