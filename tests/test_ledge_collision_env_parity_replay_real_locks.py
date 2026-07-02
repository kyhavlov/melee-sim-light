from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int
    seed_action: int
    ref_action: int
    ref_anim: int


_AGG_DEBUG = "replays/validation/fd_mixed_recent"
_AGG_VALID = "replays/validation/aggregate_recent"
_AGG_CARDINAL = "replays/validation/cardinal_1.0_recent"


_CASES = [
    _Case(
        f"{_AGG_DEBUG}/PriceyPartialAlbatross.slpz",
        2357,
        0,
        251,  # MissFoot
        252,  # CliffCatch
        216,  # ftCo_SM_CliffCatch
    ),
    _Case(
        f"{_AGG_DEBUG}/TubbyCurlyHerring.slpz",
        6012,
        0,
        251,  # MissFoot
        252,  # CliffCatch
        216,  # ftCo_SM_CliffCatch
    ),
    _Case(
        f"{_AGG_DEBUG}/BlondHardHippopotamus.slpz",
        4879,
        0,
        253,  # CliffWait
        254,  # CliffClimbSlow
        219,  # ftCo_SM_CliffClimbSlow
    ),
    _Case(
        f"{_AGG_DEBUG}/PutridJoyousOryx.slpz",
        5254,
        0,
        253,  # CliffWait
        256,  # CliffAttackSlow
        221,  # ftCo_SM_CliffAttackSlow
    ),
    _Case(
        f"{_AGG_DEBUG}/BlondHardHippopotamus.slpz",
        4931,
        0,
        254,  # CliffClimbSlow
        254,  # CliffClimbSlow grounded handoff
        219,  # ftCo_SM_CliffClimbSlow
    ),
    _Case(
        f"{_AGG_DEBUG}/DistinctCaringCobra.slpz",
        1791,
        0,
        253,  # CliffWait terminal x1990 frame
        253,  # CliffWait, now vulnerable
        217,  # ftCo_SM_CliffWait
    ),
    _Case(
        f"{_AGG_DEBUG}/BlondHardHippopotamus.slpz",
        3633,
        1,
        252,  # CliffCatch terminal row
        257,  # CliffAttackQuick via same-proc CliffWait IASA
        222,  # ftCo_SM_CliffAttackQuick
    ),
    _Case(
        f"{_AGG_DEBUG}/PositiveRevolvingHyena.slpz",
        8079,
        0,
        252,  # CliffCatch terminal row
        260,  # CliffJumpSlow1 via same-proc CliffWait IASA
        225,  # ftCo_SM_CliffJumpSlow1
    ),
    _Case(
        f"{_AGG_DEBUG}/TubbyCurlyHerring.slpz",
        6337,
        0,
        253,  # CliffWait
        253,  # raw R edge with held analog trigger does not create a fresh LR edge
        217,  # ftCo_SM_CliffWait
    ),
    _Case(
        f"{_AGG_DEBUG}/PriceyPartialAlbatross.slpz",
        2383,
        0,
        253,  # CliffWait
        257,  # Z maps to A before LR escape priority
        222,  # ftCo_SM_CliffAttackQuick
    ),
    _Case(
        f"{_AGG_VALID}/PositiveRevolvingHyena.slpz",
        8072,
        0,
        29,   # Fall, cooldown terminal seed
        252,  # CliffCatch after x2064 decrements to zero before map collision
        216,  # ftCo_SM_CliffCatch
    ),
    _Case(
        f"{_AGG_VALID}/HilariousVillainousGiraffe.slpz",
        6546,
        0,
        255,  # CliffClimbQuick terminal
        15,   # WalkSlow via same-proc Wait IASA
        7,    # ftCo_SM_WalkSlow
    ),
    _Case(
        f"{_AGG_VALID}/TubbyCurlyHerring.slpz",
        4372,
        0,
        255,  # CliffClimbQuick terminal
        20,   # Dash via same-proc Wait IASA
        12,   # ftCo_SM_Dash
    ),
    _Case(
        f"{_AGG_VALID}/BlondHardHippopotamus.slpz",
        9786,
        0,
        257,  # CliffAttackQuick terminal
        39,   # Squat via same-proc Wait IASA
        30,   # ftCo_SM_Squat
    ),
    _Case(
        f"{_AGG_VALID}/DistinctCaringCobra.slpz",
        1843,
        0,
        259,  # CliffEscapeQuick terminal
        39,   # Squat via same-proc Wait IASA
        30,   # ftCo_SM_Squat
    ),
    _Case(
        f"{_AGG_VALID}/DistinctCaringCobra.slpz",
        3572,
        1,
        193,  # DownDamageD floor-contact fallback
        193,
        193,  # ftCo_SM_DownDamageD
    ),
    _Case(
        f"{_AGG_CARDINAL}/AttachedGoodNaturedGuanaco.slpz",
        4565,
        0,
        183,  # DownBoundU airborne floor-index refresh
        183,
        183,  # ftCo_SM_DownBoundU
    ),
    _Case(
        f"{_AGG_CARDINAL}/AttachedGoodNaturedGuanaco.slpz",
        782,
        0,
        85,   # DamageAir2 same-action floor contact
        85,
        175,  # ftCo_SM_DamageAir2
    ),
    _Case(
        f"{_AGG_CARDINAL}/TreasuredBackKangaroo.slpz",
        4429,
        1,
        85,   # DamageAir2 same-action floor contact, fastfall bit must remain visible
        85,
        175,  # ftCo_SM_DamageAir2
    ),
    _Case(
        f"{_AGG_CARDINAL}/GracefulAttachedTurtle.slpz",
        8607,
        0,
        245,  # Ottotto jump input loses edge floor through KneeBend_Coll -> Fall
        29,
        20,   # ftCo_SM_Fall
    ),
    _Case(
        f"{_AGG_CARDINAL}/QuerulousGrandDinosaur.slpz",
        3334,
        1,
        16,   # WalkMiddle edge handoff enters Ottotto instead of generic Fall
        245,
        210,  # ftCo_SM_Ottotto
    ),
    _Case(
        f"{_AGG_VALID}/HilariousVillainousGiraffe.slpz",
        5095,
        0,
        245,  # Ottotto IASA crouch enters Squat through ftCo_800D5FB0
        39,
        30,   # ftCo_SM_Squat
    ),
    _Case(
        f"{_AGG_VALID}/TubbyCurlyHerring.slpz",
        10089,
        1,
        245,  # Ottotto anim-end enters OttottoWait
        246,
        211,  # ftCo_SM_OttottoWait
    ),
    _Case(
        f"{_AGG_VALID}/PriceyPartialAlbatross.slpz",
        1597,
        0,
        245,  # Ottotto held-stick below turn threshold stays teetering
        245,
        210,  # ftCo_SM_Ottotto
    ),
    _Case(
        f"{_AGG_VALID}/PriceyPartialAlbatross.slpz",
        1598,
        0,
        245,  # Ottotto ordinary Turn IASA after Dash/crouch checks
        18,
        10,   # ftCo_SM_Turn
    ),
]


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES)
def test_missfoot_and_slow_ledge_options_replay_real_rows_exact(case: _Case) -> None:
    # Replay-real locks for ledge / collision-env parity:
    # - MissFoot_Coll routes through ft_80082F28 and can enter CliffCatch through
    #   ftCliffCommon_80081298 when Collide_LedgeGrabMask is set.
    # - Cliff option entries choose Quick vs Slow from p_ftCommonData->x488, and slow options share
    #   the same attach / air-ground ownership as quick options.
    # - CliffCatch terminal rows run new-state CliffWait IASA options in the same proc.
    # - Cliff x1990 invulnerability is entry-owned and expires through Fighter_8006A360; steady
    #   CliffWait refresh must not recreate the terminal timer.
    # - Z is synthesized into HSD_PAD_A before x668 construction; raw L/R button edges do not bypass
    #   the synthesized LR lane when analog trigger was already held.
    # - Reseeded ledge cooldown is aligned to runtime's pre-collision decrement, so terminal
    #   cooldown rows can still catch when x2064 reaches zero before mpColl.
    # - Cliff option terminal callbacks enter a Wait-like grounded destination and can consume the
    #   same-proc Wait IASA locomotion tail.
    # - DamageAir same-action floor contacts expose the jump refresh while preserving visible
    #   fastfall state; DownDamage floor-contact fallbacks apply the common air->ground transfer
    #   helper even when the visible motion state does not change.
    # - DownBound can remain airborne while CollData's floor.index refreshes across FD floor seams.
    # - Ottotto edge handoffs cover Walk/Landing-style ft_80084280 teeter admission and the
    #   immediate L-stick jump-squat edge-loss path through KneeBend_Coll.
    # - Ottotto IASA crouch uses ftCo_800D5FB0, and Ottotto_Anim enters OttottoWait at anim end.
    # - Ottotto / OttottoWait ordinary Turn IASA uses ftCo_Turn_CheckInput after Dash/crouch.
    # - CliffWait c-stick option routing uses ftCo_800DF79C and ftCo_8009AAFC: c-stick may
    #   release/drop from ledge, but cannot enter CliffClimb because arg1=false.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::ftCo_MissFoot_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082F28
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::{
    #   ftCo_8009AA0C,ftCo_8009AAFC,ftCo_8009AB9C,ftCo_CliffClimb_Phys}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AEA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_8009A3C8,ftCo_Ottotto_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_Ottotto_Anim,ftCo_8009A6B8}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_800D5FB0
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Coll
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    seed, ref_row, out_row = _run_one_step_row(dataset_path, case.record, case.p)
    assert int(seed["action_id"][case.p]) == case.seed_action
    assert int(ref_row["action_id"][case.p]) == case.ref_action
    assert int(ref_row["animation_index"][case.p]) == case.ref_anim

    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "hitlag",
        "hitstun",
        "jumps_left",
        "ground_id",
    ):
        assert int(out_row[field][case.p]) == int(ref_row[field][case.p]), field
    assert [int(x) for x in out_row["state_flags"][case.p].tolist()] == [
        int(x) for x in ref_row["state_flags"][case.p].tolist()
    ]


def _physical_electric_capybara_samples() -> tuple[np.ndarray, int]:
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/yoshis_story_recent/PhysicalElectricCapybara.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = load_replay_buffers(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    return ds.rows, int(ds.num_players)


def _step_one_row(row: np.ndarray, *, num_players: int) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, seed_stride
    ).copy()
    prev_input_bytes = np.frombuffer(
        row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
    ).reshape(1, input_stride).copy()
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    ).copy()
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=num_players,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


@pytest.mark.integration
def test_cliffwait_cstick_down_releases_from_ledge_pec_948() -> None:
    # Yoshi's Story replay-real lock for CliffWait c-stick release/drop routing:
    # ftCo_8009AA0C checks main-stick first, then ftCo_800DF79C c-stick option input. The c-stick
    # path calls ftCo_8009AAFC with arg1=false, so down c-stick can release/drop from ledge but
    # cannot start CliffClimb.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::{ftCo_8009AA0C,ftCo_8009AAFC}
    samples, num_players = _physical_electric_capybara_samples()
    row = samples[948:949]
    p = 0

    assert int(row["seed_t"]["action_id"][0, p]) == 253  # CliffWait
    assert int(row["input_t"]["p"][0, p]["main_x"]) == 0
    assert int(row["input_t"]["p"][0, p]["main_y"]) == 0
    assert int(row["input_t"]["p"][0, p]["c_y"]) < 0
    assert int(row["ref_t1"]["action_id"][0, p]) == 29
    assert int(row["ref_t1"]["animation_index"][0, p]) == 20

    out = _step_one_row(row, num_players=num_players)

    for field in ("action_id", "animation_index", "action_frame", "speed_y_self"):
        got = int(out[field][p]) if field != "speed_y_self" else float(out[field][p])
        exp = int(row["ref_t1"][field][0, p]) if field != "speed_y_self" else float(row["ref_t1"][field][0, p])
        assert got == exp, f"field={field} expected={exp} got={got}"


@pytest.mark.integration
def test_cliffwait_cstick_drop_requires_prior_no_option_frame() -> None:
    # ftCo_8009AA0C sets mv.co.cliff.x8 only on frames with no main-stick or c-stick ledge option
    # input. The runtime consumes the explicit x8 latch; a teacher-forced seed with x8 still clear
    # must not release/drop even if the visible previous input is also in the option range.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AA0C
    samples, num_players = _physical_electric_capybara_samples()
    row = samples[948:949].copy()
    p = 0

    row["seed_t"]["cliff_option_stick_latch_x8"][0, p] = 0
    row["prev_input_t"]["p"][0, p]["c_y"] = -80

    out_row = _step_one_row(row, num_players=num_players)
    assert int(row["seed_t"]["action_id"][0, p]) == 253  # CliffWait
    assert int(out_row["action_id"][p]) == 253
    assert int(out_row["animation_index"][p]) == 217
