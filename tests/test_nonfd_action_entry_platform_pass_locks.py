from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _ActionCase:
    dataset_rel: str
    record: int
    player: int
    seed_action: int
    ref_action: int
    note: str


_BASE = Path("datasets/aggregate_recent/replays/validation")


def _step_one_record(row: np.ndarray, num_players: int):
    msl_binding = pytest.importorskip("msl_binding")
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, seed_stride
    ).copy()
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    ).copy()
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    ).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=int(num_players))
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_compare_bytes)
    finally:
        msl_binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]


def _rollout_record(dataset_path: Path, *, start_record: int, target_record: int):
    msl_binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert start_record <= target_record
    assert int(samples.shape[0]) > target_record

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(
        samples[start_record]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).reshape(1, seed_stride).copy()
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        msl_binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, target_record + 1):
            prev_input_bytes[:] = np.frombuffer(
                samples[record]["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride)
            input_bytes[:] = np.frombuffer(
                samples[record]["input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride)
            msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_compare_bytes)
    finally:
        msl_binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0], samples[target_record]["ref_t1"]


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ActionCase(
            "battlefield_recent/DelayedSuperbGuanaco.msl",
            404,
            1,
            344,
            344,
            "SpecialAirNStart locked ECB bottom keeps early platform crossing airborne",
        ),
        _ActionCase(
            "battlefield_recent/DelayedSuperbGuanaco.msl",
            5575,
            1,
            69,
            69,
            "AttackAirLw locked ECB bottom keeps early platform crossing airborne",
        ),
        _ActionCase(
            "battlefield_recent/DelayedSuperbGuanaco.msl",
            5495,
            0,
            39,
            39,
            "Squat pass countdown arm frame does not enter Pass early",
        ),
        _ActionCase(
            "battlefield_recent/DelayedSuperbGuanaco.msl",
            5496,
            0,
            39,
            244,
            "Squat pass countdown enters Pass after the source x470 delay",
        ),
        _ActionCase(
            "battlefield_recent/DelayedSuperbGuanaco.msl",
            5521,
            1,
            25,
            14,
            "JumpF ft_80082B1C gentle floor contact enters Wait instead of Landing",
        ),
        _ActionCase(
            "battlefield_recent/LoyalDishonestWren.msl",
            928,
            1,
            345,
            14,
            "SpecialAirNLoop AirCatchHit shares ft_80082B1C's Wait/Landing velocity split",
        ),
        _ActionCase(
            "dream_land_recent/FlippantEnchantedHorse.msl",
            3266,
            1,
            251,
            42,
            "MissFoot_Coll routes through ft_80082F28 and enters basic Landing on hard floor contact",
        ),
        _ActionCase(
            "battlefield_recent/DelayedSuperbGuanaco.msl",
            7337,
            1,
            43,
            245,
            "LandingFallSpecial shares Landing_Coll and admits Ottotto on platform edge floor loss",
        ),
        _ActionCase(
            "fountain_of_dreams_recent/ParallelTemptingElk.msl",
            1093,
            0,
            236,
            236,
            "EscapeAir keeps stale floor_skip cleared without landing before source floor contact",
        ),
        _ActionCase(
            "fountain_of_dreams_recent/ParallelTemptingElk.msl",
            1094,
            0,
            236,
            43,
            "EscapeAir ignores stale floor_skip and admits source LandingFallSpecial on FoD platform",
        ),
        _ActionCase(
            "battlefield_recent/DelayedSuperbGuanaco.msl",
            4961,
            1,
            27,
            43,
            "JumpAerialF -> EscapeAir entry uses prior-motion prev_ecb for same-frame landing",
        ),
        _ActionCase(
            "battlefield_recent/LoyalDishonestWren.msl",
            3401,
            1,
            28,
            43,
            "JumpAerialB -> EscapeAir entry uses prior-motion prev_ecb for same-frame landing",
        ),
        _ActionCase(
            "battlefield_recent/LoyalDishonestWren.msl",
            3606,
            1,
            27,
            236,
            "fresh JumpAerialF -> EscapeAir keeps locked CollData floor lifetime airborne",
        ),
        _ActionCase(
            "yoshis_story_recent/CheeryNumbMonkey.msl",
            3941,
            1,
            27,
            236,
            "fresh JumpAerialF -> EscapeAir suppresses generic floor projection on Yoshi slope",
        ),
    ],
)
def test_nonfd_action_entry_platform_pass_replay_real_locks(case: _ActionCase) -> None:
    # Source owners:
    # - AttackAir_Coll and Fox/Falco SpecialAirN*_Coll load a locked ECB through mpColl while
    #   CollData_X130_Locked is active after a ground-to-air handoff.
    # - Squat_IASA arms mv.co.squat.x4 through ftCo_80099F9C, returns, then only decrements the
    #   hidden countdown on later frames before calling ftCo_8009A228.
    # - Fall/Jump/MissFoot and AirCatchHit collision callbacks share ft_80082B1C's
    #   ftCo_800D0EC8 Wait/Landing velocity split.
    # - LandingFallSpecial uses ftCo_Landing_Coll, so edge floor-loss can enter Ottotto through
    #   the same ft_80084280 path as Landing.
    # - Fighter_ChangeMotionState clears CollData.floor_skip before EscapeAir_Coll; replay-prefix
    #   seeds can carry that hidden lane stale, but EscapeAir floor contact should not skip it.
    # - mpCollInterpolateECB carries a one-step prev_ecb snapshot; on same-frame JumpAerial ->
    #   EscapeAir entries, EscapeAir_Coll's previous endpoint can still use the prior motion ECB.
    # - Fresh JumpAerial -> EscapeAir entry rows keep the pre-entry locked CollData floor/ECB
    #   lifetime through the first EscapeAir_Coll pass instead of immediately consuming generic floor
    #   projection into LandingFallSpecial.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::*_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::mpClearFloorSkip
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8}
    # refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082B1C,ft_80082F28,ft_80084280}
    # refs/melee/src/melee/ft/ftchangeparam.c::ftCo_800D0EC8
    # refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_LandingFallSpecial
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / case.dataset_rel
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / case.dataset_rel}")

    ds = read_dataset(str(path))
    if int(ds.samples.shape[0]) <= int(case.record):
        pytest.skip(f"dataset too short for record {case.record}: {path}")

    row = ds.samples[case.record : case.record + 1]
    p = int(case.player)
    assert int(row["seed_t"]["action_id"][0, p]) == int(case.seed_action), case.note
    assert int(row["ref_t1"]["action_id"][0, p]) == int(case.ref_action), case.note
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0

    out = _step_one_record(row, int(ds.header["num_players"]))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p]), case.note


@pytest.mark.integration
def test_fod_attackairn_downheld_transformed_platform_hitbox_phase_rollout_stays_airborne() -> None:
    # Replay-real rollout lock for sustained AttackAirN on FoD's transformed platforms: p0 holds
    # down through the right moving platform after the replay-prefix seed lane reports the hidden
    # platform skip. Runtime does not fabricate CollData.floor_skip for AttackAir_Coll; it must keep
    # the same late transformed-platform floor contact airborne through the source AttackAir_Coll
    # floor owner instead of publishing LandingAirN early.
    # data/motion_state/owners/{fox,falco}.bin (MSLMSO01 coll_cb_by_action)
    # data/stages/bin/griz.bin (MSLSTG01 height platform transforms)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "fountain_of_dreams_recent/ParallelTemptingElk.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / 'fountain_of_dreams_recent/ParallelTemptingElk.msl'}")

    start = 1663
    target = 1690
    player = 0

    ds = read_dataset(str(path))
    if int(ds.samples.shape[0]) <= target:
        pytest.skip(f"dataset too short for record {target}: {path}")
    assert int(ds.samples[start]["seed_t"]["action_id"][1]) == 212  # Catch selector row.
    assert int(ds.samples[target]["seed_t"]["action_frame"][player]) == 11
    assert int(ds.samples[target]["ref_t1"]["action_id"][player]) == 65  # AttackAirN.
    assert int(ds.samples[target]["ref_t1"]["on_ground"][player]) == 0
    assert int(ds.samples[target]["seed_t"]["floor_skip_segment_valid_u8"][player]) == 1
    assert int(ds.samples[target]["seed_t"]["floor_skip_segment_id_u16"][player]) == 1

    out, ref = _rollout_record(path, start_record=start, target_record=target)

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 65
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 0
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player])
    assert int(out["jumps_left"][player]) == int(ref["jumps_left"][player])
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=3e-4)


@pytest.mark.integration
def test_fod_attackair_floor_skip_still_lands_on_nonplatform_floor_rollout() -> None:
    # Negative replay-real boundary for the same AttackAir_Coll transformed-platform family: EWT p0
    # has a replay-prefix floor-skip lane during AttackAirB, but the callback-visible floor
    # projection reaches a different non-platform floor. The retained N/Lw transformed-platform
    # owner must not suppress that later hard-floor LandingAirB handoff.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "fountain_of_dreams_recent/ElatedWearyTermite.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / 'fountain_of_dreams_recent/ElatedWearyTermite.msl'}")

    start = 7647
    target = 7996
    player = 0

    ds = read_dataset(str(path))
    if int(ds.samples.shape[0]) <= target:
        pytest.skip(f"dataset too short for record {target}: {path}")
    assert int(ds.samples[target]["seed_t"]["action_id"][player]) == 67  # AttackAirB.
    assert int(ds.samples[target]["seed_t"]["floor_skip_segment_valid_u8"][player]) == 1
    assert int(ds.samples[target]["ref_t1"]["action_id"][player]) == 72  # LandingAirB.
    assert int(ds.samples[target]["ref_t1"]["on_ground"][player]) == 1

    out, ref = _rollout_record(path, start_record=start, target_record=target)

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 72
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 1
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player])


@pytest.mark.integration
def test_battlefield_landingfallspecial_overlap_nudge_reaches_ottotto_rollout() -> None:
    # Replay-real rollout lock for common grounded fighter-overlap nudge before edge collision:
    # - ftCommon_8007E0E4 / ftCommon_8007DD7C computes xF8_playerNudgeVel.x before
    #   Fighter_procUpdate.
    # - Fighter_procUpdate applies that +x450 displacement while p0 is in LandingFallSpecial on
    #   Battlefield's left platform and overlapping p1.
    # - The later Landing/LandingFallSpecial collision callback (`ft_80084280`) consumes the nudged
    #   edge position and enters Ottotto, then falls from the platform edge on the following frame.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084280
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_8009A3C8
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "battlefield_recent/LoyalDishonestWren.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / 'battlefield_recent/LoyalDishonestWren.msl'}")

    start = 2581
    player = 0

    out_nudge, ref_nudge = _rollout_record(path, start_record=start, target_record=2612)
    assert int(ref_nudge["action_id"][player]) == 43  # LandingFallSpecial
    assert int(out_nudge["action_id"][player]) == int(ref_nudge["action_id"][player])
    assert float(out_nudge["pos_x"][player]) == pytest.approx(float(ref_nudge["pos_x"][player]))

    out_teeter, ref_teeter = _rollout_record(path, start_record=start, target_record=2613)
    assert int(ref_teeter["action_id"][player]) == 245  # Ottotto
    assert int(out_teeter["action_id"][player]) == int(ref_teeter["action_id"][player])
    assert float(out_teeter["pos_x"][player]) == pytest.approx(float(ref_teeter["pos_x"][player]))

    out_fall, ref_fall = _rollout_record(path, start_record=start, target_record=2615)
    assert int(ref_fall["action_id"][player]) == 29  # Fall
    assert int(out_fall["action_id"][player]) == int(ref_fall["action_id"][player])
    assert int(out_fall["on_ground"][player]) == int(ref_fall["on_ground"][player])
    assert float(out_fall["pos_x"][player]) == pytest.approx(float(ref_fall["pos_x"][player]))
