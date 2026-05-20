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


def _stage_state_dtype() -> np.dtype:
    return np.dtype(
        [
            ("fod_platform_height", ("<f4", (2,))),
            ("fod_platform_height_valid", ("u1", (2,))),
            ("fod_platform_height_source", ("u1", (2,))),
        ],
        align=False,
    )


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
            "fountain_of_dreams_recent/ParallelTemptingElk.msl",
            9136,
            0,
            25,
            42,
            "JumpF admits source-owned same-step FoD platform height when the platform reappears low",
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
    # - FoD platform height source flags distinguish fresh/current grIzumi heights from stale sparse
    #   carried heights when the side platform reappears before a Jump_Coll landing check.
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
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
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
def test_fod_jumpf_same_step_height_source_lane_is_generated_for_low_platform_landing() -> None:
    # PTE:9136 is the first JumpF frame that can land on the left FoD platform after grIzumi has
    # reintroduced it at a low height. The seed lane records that this low current height is
    # same-step contact provenance, not an unclassified sparse carry from an old FoD event.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "fountain_of_dreams_recent/ParallelTemptingElk.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / 'fountain_of_dreams_recent/ParallelTemptingElk.msl'}")

    ds = read_dataset(str(path))
    if int(ds.samples.shape[0]) <= 9136:
        pytest.skip(f"dataset too short for record 9136: {path}")

    row = ds.samples[9136:9137].copy()
    p = 0
    assert int(row["seed_t"]["action_id"][0, p]) == 25
    assert int(row["ref_t1"]["action_id"][0, p]) == 42
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][0, 1]) & 0x04

    out = _step_one_record(row, int(ds.header["num_players"]))
    assert int(out["action_id"][p]) == 42


@pytest.mark.integration
def test_fod_same_step_height_source_is_consumed_after_seeded_frame() -> None:
    # `stage_fod_platform_height_source_u8` is current-frame source provenance for the sparse
    # grIzumi/mpLib platform-height lane. The same-step contact bit admits PTE:9136's intended
    # landing, then runtime clears the source mask so later rollout frames require live
    # scheduler/velocity/contact evidence instead of stale seed authority.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    msl_binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "fountain_of_dreams_recent/ParallelTemptingElk.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / 'fountain_of_dreams_recent/ParallelTemptingElk.msl'}")

    ds = read_dataset(str(path))
    record = 71
    if int(ds.samples.shape[0]) <= record:
        pytest.skip(f"dataset too short for record {record}: {path}")

    row = ds.samples[record : record + 1].copy()
    p = 0
    assert int(row["seed_t"]["stage_fod_platform_height_valid_u8"][0, 1]) == 1
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][0, 1]) & 0x04
    assert int(row["ref_t1"]["action_id"][0, p]) == 42

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    stage_stride = int(sizes["stage_state"])
    seed_bytes = row["seed_t"].view("u1").reshape(1, seed_stride).copy()
    prev_input_bytes = row["prev_input_t"].view("u1").reshape(1, input_stride).copy()
    input_bytes = row["input_t"].view("u1").reshape(1, input_stride).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_stage_bytes = np.empty((1, stage_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.debug_write_stage_state(handle, out_stage_bytes)
        stage_before = out_stage_bytes.view(_stage_state_dtype()).reshape((1,))[0].copy()
        assert int(stage_before["fod_platform_height_source"][1]) & 0x04

        msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_compare_bytes)
        msl_binding.debug_write_stage_state(handle, out_stage_bytes)
        stage_after = out_stage_bytes.view(_stage_state_dtype()).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    assert int(out["action_id"][p]) == 42
    assert int(stage_after["fod_platform_height_source"][0]) == 0
    assert int(stage_after["fod_platform_height_source"][1]) == 0


@pytest.mark.integration
def test_fod_sustained_escapeair_high_lock_countdown_rejects_side_platform_root_projection() -> None:
    # MGS:1278 is a free-run rollout boundary for the same EscapeAir_Coll source owner: a
    # ground-jump airdodge is already in sustained EscapeAir with a high CollData lock countdown.
    # FoD's live left platform is under the root projection, but source is still in the
    # mpCollInterpolateECB gap and should publish LandingFallSpecial on the carried main hard
    # floor, not on the transformed side platform, until the countdown reaches the retained
    # side-platform publication phase.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_UnlockECB}
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044838_Floor}
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "fountain_of_dreams_recent/MilkyGracefulStingray.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / 'fountain_of_dreams_recent/MilkyGracefulStingray.msl'}")

    start = 0
    target = 1278
    player = 0

    ds = read_dataset(str(path))
    if int(ds.samples.shape[0]) <= target:
        pytest.skip(f"dataset too short for record {target}: {path}")
    assert int(ds.samples[target]["seed_t"]["action_id"][player]) == 236  # EscapeAir.
    assert int(ds.samples[target]["seed_t"]["seed_prev_action_id"][player]) == 236
    assert int(ds.samples[target]["seed_t"]["stage_fod_platform_height_valid_u8"][1]) == 1
    assert int(ds.samples[target]["seed_t"]["stage_fod_platform_height_source_u8"][1]) == 0
    assert int(ds.samples[target]["ref_t1"]["action_id"][player]) == 43  # LandingFallSpecial.
    assert int(ds.samples[target]["ref_t1"]["ground_id"][player]) == 5

    out, ref = _rollout_record(path, start_record=start, target_record=target)

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 43
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player]) == 5
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1e-6)


@pytest.mark.integration
def test_fod_landing_entry_same_step_height_source_reprojects_to_current_platform() -> None:
    # PTE:5288 starts Landing on the static floor index, while the transient grIzumi same-step
    # source bit proves that Landing_Coll/ft_80084280 should consume the current transformed FoD
    # platform floor in this callback. This is an entry-frame owner: the adjacent sustained
    # LandingFallSpecial negative below must not reuse the sparse seed bit.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084280
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "fountain_of_dreams_recent/ParallelTemptingElk.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / 'fountain_of_dreams_recent/ParallelTemptingElk.msl'}")

    ds = read_dataset(str(path))
    record = 5288
    player = 1
    if int(ds.samples.shape[0]) <= record:
        pytest.skip(f"dataset too short for record {record}: {path}")

    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][player]) == 42
    assert int(row["seed_t"]["action_frame"][player]) == 0
    assert int(row["seed_t"]["ground_id"][player]) == 5
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][1]) & 0x04
    assert int(row["ref_t1"]["ground_id"][player]) == 0

    out = _step_one_record(ds.samples[record : record + 1].copy(), int(ds.header["num_players"]))
    ref = row["ref_t1"]
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player])
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=2e-4)


@pytest.mark.integration
def test_fod_sustained_landingfallspecial_does_not_reuse_same_step_height_source() -> None:
    # EWT:5155 carries the same transient FoD source bit while already deep into
    # LandingFallSpecial. Source collision remains on the current floor index; consuming the sparse
    # seed bit here stale-lifts the fighter to the opposite height platform for one step.
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "fountain_of_dreams_recent/ElatedWearyTermite.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / 'fountain_of_dreams_recent/ElatedWearyTermite.msl'}")

    ds = read_dataset(str(path))
    record = 5155
    player = 0
    if int(ds.samples.shape[0]) <= record:
        pytest.skip(f"dataset too short for record {record}: {path}")

    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][player]) == 43
    assert int(row["seed_t"]["action_frame"][player]) == 24
    assert int(row["seed_t"]["ground_id"][player]) == 5
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][1]) & 0x04
    assert int(row["ref_t1"]["ground_id"][player]) == 5

    out = _step_one_record(ds.samples[record : record + 1].copy(), int(ds.header["num_players"]))
    ref = row["ref_t1"]
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player])
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1e-6)


@pytest.mark.integration
def test_fod_jumpb_downheld_transformed_platform_skip_carries_after_release_rollout() -> None:
    # Replay-real rollout lock for common-air FoD transformed-platform pass-through:
    # JumpB_Coll uses ft_800835B0 with the ftCo_80096CC8 platform callback. A down-held crossing
    # of the moving platform rejects that floor and carries the CollData.floor_skip owner into the
    # next released callback frame; starting the rollout before the serialized seed lane exists
    # must still keep the player airborne over the transformed platform.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
    # data/stages/bin/griz.bin (MSLSTG01 height platform transforms)
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "fountain_of_dreams_recent/ParallelTemptingElk.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / 'fountain_of_dreams_recent/ParallelTemptingElk.msl'}")

    start = 1456
    target = 1510
    player = 1

    ds = read_dataset(str(path))
    if int(ds.samples.shape[0]) <= target:
        pytest.skip(f"dataset too short for record {target}: {path}")
    assert int(ds.samples[start]["seed_t"]["floor_skip_segment_valid_u8"][player]) == 0
    assert int(ds.samples[target]["seed_t"]["action_id"][player]) == 26  # JumpB.
    assert int(ds.samples[target]["seed_t"]["floor_skip_segment_valid_u8"][player]) == 1
    assert int(ds.samples[target]["seed_t"]["floor_skip_segment_id_u16"][player]) == 1
    assert int(ds.samples[target]["ref_t1"]["action_id"][player]) == 26
    assert int(ds.samples[target]["ref_t1"]["on_ground"][player]) == 0

    out, ref = _rollout_record(path, start_record=start, target_record=target)

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 26
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 0
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=3e-4)


@pytest.mark.integration
def test_fod_attackairn_downheld_transformed_platform_hitbox_phase_rollout_stays_airborne() -> None:
    # Replay-real rollout lock for sustained AttackAirN after a FoD transformed-platform
    # pass-through: p0 holds down through the right moving platform, carries the hidden floor-skip
    # owner, then crosses the connected hard-floor edge. Runtime must keep that first hard-floor
    # crossing airborne and consume the skip so the following callback can enter LandingAirN.
    # data/motion_state/owners/{fox,falco}.bin (MSLMSO01 coll_cb_by_action)
    # data/stages/bin/griz.bin (MSLSTG01 height platform transforms)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "fountain_of_dreams_recent/ParallelTemptingElk.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / 'fountain_of_dreams_recent/ParallelTemptingElk.msl'}")

    start = 1663
    target = 1701
    player = 0

    ds = read_dataset(str(path))
    if int(ds.samples.shape[0]) <= target:
        pytest.skip(f"dataset too short for record {target}: {path}")
    assert int(ds.samples[start]["seed_t"]["action_id"][1]) == 212  # Catch selector row.
    assert int(ds.samples[target]["seed_t"]["action_frame"][player]) == 18
    assert int(ds.samples[target]["ref_t1"]["action_id"][player]) == 65  # AttackAirN.
    assert int(ds.samples[target]["ref_t1"]["on_ground"][player]) == 0
    assert int(ds.samples[target]["seed_t"]["floor_skip_segment_valid_u8"][player]) == 1
    assert int(ds.samples[target]["seed_t"]["floor_skip_segment_id_u16"][player]) == 1
    assert int(ds.samples[target + 1]["seed_t"]["floor_skip_segment_valid_u8"][player]) == 0
    assert int(ds.samples[target + 1]["ref_t1"]["action_id"][player]) == 70  # LandingAirN.

    out, ref = _rollout_record(path, start_record=start, target_record=target)

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 65
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 0
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player])
    assert int(out["jumps_left"][player]) == int(ref["jumps_left"][player])
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=3e-4)


@pytest.mark.integration
def test_fod_attackair_floor_skip_still_lands_on_nonplatform_floor_rollout() -> None:
    # Negative replay-real boundary for the same AttackAir_Coll transformed-platform family: EWT p0
    # has an AttackAirB hard-floor handoff after the transformed-platform pass window. The retained
    # N/Lw endpoint owner must not serialize or carry a platform floor-skip into this later
    # non-platform LandingAirB handoff.
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
    assert int(ds.samples[target]["seed_t"]["floor_skip_segment_valid_u8"][player]) == 0
    assert int(ds.samples[target]["ref_t1"]["action_id"][player]) == 72  # LandingAirB.
    assert int(ds.samples[target]["ref_t1"]["on_ground"][player]) == 1

    out, ref = _rollout_record(path, start_record=start, target_record=target)

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 72
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 1
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player])


@pytest.mark.integration
def test_fod_attackairb_downheld_root_crossing_publishes_floor_skip_rollout() -> None:
    # Replay-real positive for sustained AttackAirB's FoD transformed-platform pass-through. PTE p1
    # holds down while the AttackAirB root crosses below the right height-platform line before the
    # ECB bottom sweep itself produces a floor hit. Source AttackAir_Coll still uses
    # ft_80082C74 -> mpColl_800471F8, but CollData.floor_skip has already been published by the
    # soft-platform pass owner, so the later platform floor candidate must stay airborne.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpUpdateFloorSkip}
    # data/stages/bin/griz.bin (MSLSTG01 height platform transforms)
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "fountain_of_dreams_recent/ParallelTemptingElk.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / 'fountain_of_dreams_recent/ParallelTemptingElk.msl'}")

    start = 1263
    target = 1835
    player = 1

    ds = read_dataset(str(path))
    if int(ds.samples.shape[0]) <= target:
        pytest.skip(f"dataset too short for record {target}: {path}")
    assert int(ds.samples[target]["seed_t"]["action_id"][player]) == 67  # AttackAirB.
    assert int(ds.samples[target]["seed_t"]["floor_skip_segment_valid_u8"][player]) == 1
    assert int(ds.samples[target]["seed_t"]["floor_skip_segment_id_u16"][player]) == 1
    assert int(ds.samples[target]["ref_t1"]["action_id"][player]) == 67
    assert int(ds.samples[target]["ref_t1"]["on_ground"][player]) == 0

    out, ref = _rollout_record(path, start_record=start, target_record=target)

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 67
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 0
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player])
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=3e-4)


@pytest.mark.integration
def test_fod_attackairn_hard_floor_edge_root_projection_lands() -> None:
    # Replay-real positive for AttackAir_Coll's ordinary hard-floor edge handoff:
    # ft_80082C74 -> mpColl_800471F8 first accepts a connected hard-floor bottom sweep, then
    # mpColl_80044838_Floor(ignore_bottom=true) snaps the root to FoD's right main-stage edge
    # because the loaded AttackAirN ECB bottom is above the root. This is separate from the
    # transformed-platform floor-skip owner: no platform skip is serialized and the accepted line is
    # MSLSTG01 hard-floor segment 6.
    # data/motion_state/owners/{fox,falco}.bin (MSLMSO01 coll_cb_by_action)
    # data/stages/bin/griz.bin (MSLSTG01 floor segment links)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "fountain_of_dreams_recent/ParallelTemptingElk.msl"
    if not path.exists():
        pytest.skip(f"missing local dataset: {_BASE / 'fountain_of_dreams_recent/ParallelTemptingElk.msl'}")

    record = 1702
    player = 0
    ds = read_dataset(str(path))
    if int(ds.samples.shape[0]) <= record:
        pytest.skip(f"dataset too short for record {record}: {path}")
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][player]) == 65  # AttackAirN.
    assert int(row["seed_t"]["floor_skip_segment_valid_u8"][player]) == 0
    assert int(row["ref_t1"]["action_id"][player]) == 70  # LandingAirN.
    assert int(row["ref_t1"]["on_ground"][player]) == 1
    assert int(row["ref_t1"]["ground_id"][player]) == 6

    out = _step_one_record(ds.samples[record : record + 1].copy(), int(ds.header["num_players"]))
    ref = row["ref_t1"]

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 70
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 1
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player]) == 6
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=2e-6)


@pytest.mark.integration
def test_fod_attackairlw_source_trusted_platform_root_projection_lands() -> None:
    # Replay-real positive for AttackAir_Coll's ordinary source-trusted FoD platform handoff:
    # Falco dair is inside its generated first HitCapsule script lifetime, but the callback root is
    # already more than two ECB units below the left height-transform platform, so this is no longer
    # the shallow first-contact/pass-through owner. AttackAir_Coll routes through ft_80082C74 and
    # does not pass ftCo_80096CC8, so held-down input alone must not reject this deep platform root
    # snap once the MSLSTG01 FoD height source is current/same-step trusted.
    # data/motion_state/owners/{fox,falco}.bin (MSLMSO01 coll_cb_by_action, submotion_id)
    # data/scripts/{fox,falco}.bin (MSLFTSC1 create_hitbox/clear_hitboxes events)
    # data/stages/bin/griz.bin (MSLSTG01 height platform transforms)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "fountain_of_dreams_recent/MilkyGracefulStingray.msl"
    if not path.exists():
        pytest.skip(
            f"missing local dataset: {_BASE / 'fountain_of_dreams_recent/MilkyGracefulStingray.msl'}"
        )

    record = 5344
    player = 0
    ds = read_dataset(str(path))
    if int(ds.samples.shape[0]) <= record:
        pytest.skip(f"dataset too short for record {record}: {path}")
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][player]) == 69  # AttackAirLw.
    assert int(row["seed_t"]["action_frame"][player]) == 10
    assert int(row["seed_t"]["floor_skip_segment_valid_u8"][player]) == 0
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][1]) & 0x04
    assert int(row["ref_t1"]["action_id"][player]) == 74  # LandingAirLw.
    assert int(row["ref_t1"]["on_ground"][player]) == 1
    assert int(row["ref_t1"]["ground_id"][player]) == 0

    out = _step_one_record(ds.samples[record : record + 1].copy(), int(ds.header["num_players"]))
    ref = row["ref_t1"]

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 74
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player]) == 1
    assert int(out["ground_id"][player]) == int(ref["ground_id"][player]) == 0
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=2e-4)


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
