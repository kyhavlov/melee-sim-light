from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


BUTTON_L = 0x0040

ACT_ESCAPE_AIR = 236
ACT_LANDING_FALL_SPECIAL = 43
ACT_FALL_SPECIAL = 35
ACT_JUMP_AERIAL_F = 27
ACT_KNEE_BEND = 24
ACT_FALL = 29
ACT_PASS = 244
ACT_FX_SPECIAL_AIR_N_START = 344
ACT_JUMP_F = 25
ACT_ATTACK_AIR_N = 65
ACT_ATTACK_AIR_LW = 65
ACT_ATTACK_AIR_F = 66
ACT_LANDING_AIR_LW = 70
ACT_LANDING = 42
ACT_GUARD_ON = 178
ACT_GUARD_REFLECT = 182
ACT_ESCAPE_N = 235
SM_ESCAPE_AIR = 44
SM_LANDING_FALL_SPECIAL = 36
SM_ATTACK_AIR_F = 69
SM_LANDING = 35
SM_FX_SPECIAL_AIR_N_START = 298


def _run_one_step(ds, record: int, *, seed_mutator=None, input_mutator=None) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    row = ds.samples[record : record + 1].copy()
    if seed_mutator is not None:
        seed_mutator(row["seed_t"])
    if input_mutator is not None:
        input_mutator(row["prev_input_t"], row["input_t"])

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed(handle, row["seed_t"].view("u1").reshape(1, seed_stride).copy())
        binding.step_input(
            handle,
            row["prev_input_t"].view("u1").reshape(1, input_stride).copy(),
            row["input_t"].view("u1").reshape(1, input_stride).copy(),
        )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()


def _run_rollout_to_record(ds, start_record: int, target_record: int) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    samples = ds.samples
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(
            handle, samples[start_record : start_record + 1]["seed_t"].view("u1").reshape(1, seed_stride).copy()
        )
        for record in range(start_record, target_record + 1):
            binding.step_input(
                handle,
                samples[record : record + 1]["prev_input_t"].view("u1").reshape(1, input_stride).copy(),
                samples[record : record + 1]["input_t"].view("u1").reshape(1, input_stride).copy(),
            )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl",
            2565,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            6243,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            2771,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "PhysicalElectricCapybara.msl",
            6328,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            1361,
            1,
        ),
    ],
)
def test_locked_escapeair_platform_start_lifetime_enters_landing_fall_special(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real locks for platform-stage EscapeAir over soft platforms.
    #
    # Source owner:
    # - EscapeAir_Coll delegates through ft_80082C74 -> ft_80081D0C.
    # - mpColl_80043754 owns the callback-local substep result.
    # - With CollData_X130_Locked active, mpColl_80046904 can consume a platform floor result via
    #   mpColl_80044838_Floor(ignore_bottom=true), projecting from the fighter root while the locked
    #   bottom point is still zeroed.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_80043754,mpColl_80046904,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["animation_index"][p]) == SM_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in (
        "action_id",
        "animation_index",
        "action_frame",
        "on_ground",
        "ground_id",
        "jumps_left",
        "instance_id",
    ):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_locked_escapeair_deep_cliff_floor_handoff_lands() -> None:
    # Positive replay-real lock for a deep sustained EscapeAir floor handoff on a cliff/ledge floor.
    # The shallow horizontal-ledge guard remains active for early locked contacts, but once the final
    # snap is deeper than the entered EscapeAir ECB-bottom extent plus mpColl's vertical ECB unit,
    # the same ft_80082C74 -> mpColl_800471F8 callback owns LandingFallSpecial.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/CornyDelayedOkapi.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 1097
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_locked_escapeair_shallow_cliff_floor_final_snap_stays_airborne() -> None:
    # Negative replay-real lock for the cliff-depth boundary above. The restored CollData cliff
    # floor can project in-bounds on Battlefield, but this shallow snap has not passed the entered
    # EscapeAir ECB-bottom plus vertical-unit depth needed by the source floor handoff.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/battlefield_recent/DelayedSuperbGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 11685
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "start_record", "target_record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "MediumVirtualPig.msl",
            2598,
            2753,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "MilkyGracefulStingray.msl",
            5479,
            5554,
            0,
        ),
    ],
)
def test_fall_ecb_lock_expiry_rollout_keeps_fastfall_airborne(
    dataset_rel: str, start_record: int, target_record: int, p: int
) -> None:
    # Rollout-real lock for the same Fall_Coll owner as the direct ledge-continuation rows above.
    # When CollData_X130_Locked expires during a fastfall Fall loop, source unlocks the ECB before
    # collision and reloads the ordinary Fall pose. Keeping the stale zero-bottom locked ECB in
    # runtime makes the next frame publish a false Landing; teacher-forced one-step reseeds already
    # reconstruct the unlocked ECB, so this must be covered as a rollout lock.
    #
    # refs/melee/src/melee/ft/fighter.c::Fighter_procMap
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[target_record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 0
    assert int(row["seed_t"]["state_flags"][p, 1]) & 0x08 != 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_FALL
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_rollout_to_record(ds, start_record, target_record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    # This rollout lock owns the stale locked-ECB false-landing branch. The remaining vertical
    # residual is from prior rollout integration before the target row, while the source boundary
    # here is that Fall_Coll stays airborne with the replay-real ground/action state.
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-2)


@pytest.mark.integration
def test_attackair_transformed_platform_floor_skip_clears_after_first_root_crossing() -> None:
    # Rollout-real lock for the FoD AttackAir transformed-platform first-crossing owner. The
    # retained floor_skip blocks only the first callback pass across the height-transformed floor;
    # once the airborne root has crossed below that carried platform, source no longer skips the
    # next AttackAir_Coll floor publication and LandingAirLw can begin.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpClearFloorSkip}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "MilkyGracefulStingray.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    start_record = 2010
    target_record = 2013
    p = 1
    row = ds.samples[target_record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_LW
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert int(row["seed_t"]["ground_id"][p]) != 0xFFFF
    assert int(row["seed_t"]["floor_skip_segment_id_u16"][p]) == 0xFFFF
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_AIR_LW
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_rollout_to_record(ds, start_record, target_record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "p", "seed_action", "ref_action"),
    [
        (7843, 0, ACT_ATTACK_AIR_N, ACT_LANDING_AIR_LW),
    ],
)
def test_attackair_transformed_platform_released_pass_final_handoff_lands(
    record: int, p: int, seed_action: int, ref_action: int
) -> None:
    # Replay-real negative controls for the FoD AttackAir transformed-platform pass owner. When the
    # explicit down-held / floor-skip owner is no longer active, AttackAir_Coll can publish the
    # normal Landing/LandingAir handoff even if the callback root is already below the moving
    # platform.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == seed_action
    assert int(row["seed_t"]["floor_skip_segment_id_u16"][p]) == 0xFFFF
    assert int(row["seed_t"]["floor_skip_segment_valid_u8"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ref_action
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=2e-4
    )


@pytest.mark.integration
def test_attackair_transformed_platform_downheld_final_handoff_stays_airborne() -> None:
    # Replay-real positive lock for the explicit down-held FoD platform-pass owner. This is not an
    # endpoint-only case: source rejects the height-transformed soft platform through the
    # callback-local floor_skip / mpColl floor owner and keeps AttackAirN airborne for this frame.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpUpdateFloorSkip}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 7960
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_N
    assert int(row["seed_t"]["floor_skip_segment_id_u16"][p]) == 0xFFFF
    assert int(row["seed_t"]["floor_skip_segment_valid_u8"][p]) == 0
    assert int(row["input_t"]["p"]["main_y"][p]) < -100
    assert int(row["prev_input_t"]["p"]["main_y"][p]) < -100
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ATTACK_AIR_N
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=2e-4
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            3936,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "PhysicalElectricCapybara.msl",
            4812,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "PhysicalElectricCapybara.msl",
            950,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl",
            2617,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl",
            2618,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl",
            511,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl",
            512,
            1,
        ),
    ],
)
def test_locked_escapeair_first_platform_crossing_stays_airborne(
    dataset_rel: str, record: int, p: int
) -> None:
    # Negative replay-real controls: the first post-physics root crossing under a soft platform does
    # not yet have the callback-local platform result lifetime consumed by the retained owner.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
def test_replay_rollout_frame_clock_keeps_randall_phase_current_for_sideb() -> None:
    # Replay-real rollout lock for Yoshi's Story Randall phase during Fox aerial Side-B end.
    #
    # Gameplay situation:
    # - The rollout starts before Fox is launched into aerial Side-B end.
    # - At LIM:4125, vanilla is still airborne in SpecialAirSEnd; using the reseed frame for
    #   Randall's platform path makes the sim collide with an old Randall phase and enter
    #   LandingFallSpecial early.
    #
    # Source owner:
    # - Randall's collision line is frame-indexed stage-object motion generated into MSLSTG01
    #   platform_path records.
    # - replay rollout owns frame_id advancement even when the replay RNG seed remains seed-owned.
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,grStory_801E33E0}
    # refs/melee/src/melee/gr/ground.c::Ground_801C2FE0
    # data/stages/bin/grst.bin::MSLSTG01 platform_path records
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "LawfulInsistentMeerkat.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    start_record = 3562
    target_record = 4125
    p = 0
    out = _run_rollout_to_record(ds, start_record, target_record)
    ref = ds.samples[target_record]["ref_t1"]

    assert int(out["frame_id"]) == int(ref["frame_id"])
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 352
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1.0e-5)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            7436,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "ShadyDecimalStarling.msl",
            7522,
            1,
        ),
    ],
)
def test_sustained_escapeair_different_platform_segment_lands(
    dataset_rel: str, record: int, p: int
) -> None:
    # Positive replay-real locks for sustained EscapeAir rows where the accepted soft platform is
    # not the carried CollData.floor.index segment. The same-platform negative locks above remain
    # airborne until the preserved desired bottom sweep reaches the carried platform, but different
    # platform segments are new floor candidates for the normal EscapeAir_Coll callback.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["ground_id"][p]) != int(row["ref_t1"]["ground_id"][p])
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_locked_escapeair_platform_consumer_does_not_broaden_fd_or_hard_floor() -> None:
    # Synthetic negative controls using a replay-real platform row:
    # - FD/cardinal has no soft platform line to consume.
    # - a hard-floor-only pose above Battlefield main floor is not admitted by the platform helper.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 2565
    p = 1

    def fd_mutation(seed: np.ndarray) -> None:
        seed["stage_id"][0] = np.uint32(32)
        seed["ground_id"][0, p] = np.uint16(0xFFFF)
        seed["pos_x"][0, p] = np.float32(0.0)
        seed["pos_y"][0, p] = np.float32(24.0)
        seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(0.0)
        seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(26.0)

    out_fd = _run_one_step(ds, record, seed_mutator=fd_mutation)
    assert int(out_fd["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_fd["on_ground"][p]) == 0

    def hard_floor_mutation(seed: np.ndarray) -> None:
        seed["stage_id"][0] = np.uint32(31)
        seed["ground_id"][0, p] = np.uint16(1)
        seed["pos_x"][0, p] = np.float32(0.0)
        seed["pos_y"][0, p] = np.float32(3.0)
        seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(0.0)
        seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(5.0)

    out_hard_floor = _run_one_step(ds, record, seed_mutator=hard_floor_mutation)
    assert int(out_hard_floor["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_hard_floor["on_ground"][p]) == 0


@pytest.mark.integration
def test_pass_specialairn_platform_floor_skip_handoff_stays_airborne() -> None:
    # Pass_IASA can enter SpecialAirN before Fighter_procMap, but the source CollData.floor_skip
    # written by Pass entry remains the local floor-skip owner for the same map callback. The
    # platform line is therefore rejected for this pass-through frame rather than immediately
    # becoming Landing.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{ftCo_8009A228,ftCo_Pass_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNStart_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 4283
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_PASS
    assert int(row["ref_t1"]["action_id"][p]) == ACT_FX_SPECIAL_AIR_N_START
    assert int(row["ref_t1"]["animation_index"][p]) == SM_FX_SPECIAL_AIR_N_START
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
def test_pass_specialairn_platform_floor_skip_handoff_requires_carried_platform() -> None:
    # Negative coverage: the retained owner is the callback-local floor_skip from the carried
    # platform floor.index, not a broad SpecialAirN action gate.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 4283
    p = 1

    def remove_carried_platform(seed: np.ndarray) -> None:
        seed["ground_id"][0, p] = np.uint16(0xFFFF)

    out = _run_one_step(ds, record, seed_mutator=remove_carried_platform)
    assert int(out["action_id"][p]) == ACT_LANDING
    assert int(out["on_ground"][p]) == 1


@pytest.mark.integration
def test_jumpaerial_escapeair_platform_entry_frame4_keeps_source_airborne() -> None:
    # Replay-real lock for the next JumpAerial -> EscapeAir platform entry residual after the
    # locked root-projection checkpoint. The source IASA can enter EscapeAir before the map
    # callback, but the frame-4 carried JumpAerial CollData lifetime still suppresses the same-frame
    # platform landing result.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    #   ftCo_80099A58,ftCo_EscapeAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80043754}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 958
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["seed_prev_action_frame"][p]) == 4
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl",
            1863,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "SweatyThisMallard.msl",
            5078,
            1,
        ),
    ],
)
def test_jumpaerial_escapeair_ledge_entry_keeps_source_airborne(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real locks for the same fresh JumpAerial -> EscapeAir locked-ECB owner on carried
    # ledge floors. Source IASA enters EscapeAir before Fighter_procMap, but the first
    # EscapeAir_Coll pass still consumes the pre-entry CollData/ECB lifetime and keeps the row
    # airborne instead of publishing LandingFallSpecial from a generic ledge-floor sweep.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    #   ftCo_80099A58,ftCo_EscapeAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80043754}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["ecb_lock_timer"][p]) != 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_jumpaerial_escapeair_platform_entry_owner_is_not_broadened() -> None:
    # Synthetic negatives for the retained source owners:
    # - later carried JumpAerial prefix age is not part of the shallow-entry suppression slice when
    #   visible floor ownership already names the same ledge;
    # - JumpF provenance does not borrow the JumpAerial callback lifetime;
    # - FD and hard-floor mutations do not use the soft-platform suppression path.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 958
    p = 0

    def prev_frame5(seed: np.ndarray) -> None:
        seed["seed_prev_action_frame"][0, p] = np.uint16(5)

    out_prev_frame5 = _run_one_step(ds, record, seed_mutator=prev_frame5)
    assert int(out_prev_frame5["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(out_prev_frame5["on_ground"][p]) == 1

    def jumpf_provenance(seed: np.ndarray) -> None:
        seed["seed_prev_action_id"][0, p] = np.uint16(25)

    out_jumpf = _run_one_step(ds, record, seed_mutator=jumpf_provenance)
    assert int(out_jumpf["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(out_jumpf["on_ground"][p]) == 1

    def fd_mutation(seed: np.ndarray) -> None:
        seed["stage_id"][0] = np.uint32(32)
        seed["ground_id"][0, p] = np.uint16(1)

    out_fd = _run_one_step(ds, record, seed_mutator=fd_mutation)
    assert int(out_fd["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_fd["on_ground"][p]) == 0

    def hard_floor_mutation(seed: np.ndarray) -> None:
        seed["ground_id"][0, p] = np.uint16(3)
        seed["pos_y"][0, p] = np.float32(2.0)
        seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(4.0)

    out_hard_floor = _run_one_step(ds, record, seed_mutator=hard_floor_mutation)
    assert int(out_hard_floor["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_hard_floor["on_ground"][p]) == 0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            6725,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "PhysicalElectricCapybara.msl",
            7972,
        ),
    ],
)
def test_jumpaerial_escapeair_shallow_yoshi_ledge_remap_stays_airborne(
    dataset_rel: str, record: int
) -> None:
    # Fresh JumpAerial -> EscapeAir can carry Yoshi center-floor CollData.floor.index while the
    # projection helper remaps toward the adjacent ledge. Shallow remaps are not source floor
    # callback results; vanilla publishes the EscapeAir row airborne at the post-physics root.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    p = 1
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_jumpaerial_escapeair_deep_yoshi_ledge_remap_still_lands() -> None:
    # Negative control for the shallow ledge-remap guard: once the accepted ledge snap is deeper
    # than the entered EscapeAir ECB bottom owner, the same floor callback enters
    # LandingFallSpecial.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 5959
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "p"),
    [
        (3516, 1),
        (3709, 1),
        (6281, 0),
    ],
)
def test_jumpaerial_escapeair_fd_zero_bottom_root_projection_lands(
    record: int, p: int
) -> None:
    # Replay-real positives for same-frame JumpAerial -> EscapeAir over a hard floor. The source
    # callback enters EscapeAir before Fighter_procMap, then ft_80082C74/mpColl_800471F8 consumes the
    # locked desired bottom at zero and publishes LandingFallSpecial from the current root sweep.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PriceyPartialAlbatross.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["ground_id"][p]) != 0xFFFF
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize("record", [3345, 5057, 6997])
def test_sustained_escapeair_yoshi_floor_rows_do_not_borrow_fd_zero_bottom_owner(
    record: int,
) -> None:
    # Negative controls: Yoshi EscapeAir rows over ledge/platform-adjacent floor owners must not use
    # the hard-floor zero-bottom root projection. Sustained EscapeAir, platform transforms, and
    # ledge remaps stay owned by their narrower callback-local paths.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 0
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            2445,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            157,
            0,
        ),
    ],
)
def test_sustained_escapeair_early_locked_vertical_window_stays_airborne(
    dataset_rel: str, record: int, p: int
) -> None:
    # Sustained EscapeAir early-lock owner:
    # EscapeAir_Coll delegates to ft_80082C74 while CollData_X130_Locked is still active. Source
    # mpCollInterpolateECB has not yet published the stable floor result on these frame-4 window
    # rows, so vertical-only floor sweeps remain airborne until a later EscapeAir callback.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80043754}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["action_frame"][p]) == 3
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            3937,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl",
            787,
            1,
        ),
    ],
)
def test_locked_escapeair_deep_platform_crossing_lands(
    dataset_rel: str, record: int, p: int
) -> None:
    # Positive replay-real controls for the sustained EscapeAir window. Shallow first crossings
    # remain airborne above, but once the current root is already below the accepted platform by at
    # least the entered EscapeAir ECB bottom extent, ft_80082C74/mpColl_800471F8 owns the
    # LandingFallSpecial handoff.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_no_lock_sustained_escapeair_platform_contact_lands() -> None:
    # Once CollData_X130_Locked has cleared, sustained EscapeAir floor contact is the ordinary
    # ft_80082C74/mpColl_800471F8 owner. Do not keep the old no-lock vertical-frame bridge
    # airborne: the source callback consumes this Pokemon Stadium side-platform crossing into
    # LandingFallSpecial.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/CornyDelayedOkapi.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 9345
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_locked_escapeair_late_above_root_platform_projection_uses_callback_prev_root() -> None:
    # EscapeAir_Coll delegates through ft_80082C74 -> ft_80081D0C -> mpColl_800471F8.
    # During the locked ECB window, mpColl_80046904 can accept a soft-platform floor result and
    # mpColl_80044838_Floor(ignore_bottom=true) then projects from cur_pos because
    # ecb.bottom.y > 0. The source start point for this callback is CollData.prev_pos, represented
    # by the replay seed's floor_sweep_prev_pos lane.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_80043754,mpColl_80046904,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    positives = (
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "DelayedSuperbGuanaco.msl",
            953,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            8411,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "MilkyGracefulStingray.msl",
            5485,
            0,
        ),
    )
    for dataset_rel, record, p in positives:
        dataset_path = root / dataset_rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_rel}")

        ds = read_dataset(str(dataset_path))
        row = ds.samples[record]
        assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
        assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
        assert int(row["seed_t"]["ecb_lock_timer"][p]) == 6
        assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL

        out = _run_one_step(ds, record)
        ref = row["ref_t1"]
        for field in ("action_id", "animation_index", "on_ground", "ground_id"):
            assert int(out[field][p]) == int(ref[field][p]), (dataset_rel, record, field)
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            5203,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            3500,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "DelayedSuperbGuanaco.msl",
            4746,
            1,
        ),
    ],
)
def test_locked_escapeair_uses_frame_start_colldata_last_pos_and_desired_bottom(
    dataset_rel: str, record: int, p: int
) -> None:
    # Positive-height platform EscapeAir owner: EscapeAir_Coll enters the mpColl_800471F8 path after
    # Fighter_procUpdate has already run IASA and Phys, but ft_80082C74 still copies the
    # callback-local CollData.cur_pos into last_pos before writing the post-Phys root to cur_pos.
    # During CollData_X130_Locked, mpColl_LoadECB_inline preserves desired_ecb.bottom; zero-bottom
    # platform locks use the same CollData root sweep.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_LoadECB_inline,mpColl_80043754,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["ecb_lock_timer"][p]) != 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), (dataset_rel, record, field)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-4)


@pytest.mark.integration
def test_fallspecial_static_platform_first_crossing_stays_airborne_then_lands() -> None:
    # FallSpecial_Coll uses ft_80083090 -> mpColl_80047E14 with the ftCo_80096CC8 platform
    # callback. The first static-platform root crossing remains airborne in FallSpecial; the next
    # already-below-platform callback owns LandingFallSpecial through the same platform floor.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    #   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/CornyDelayedOkapi.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))

    airborne_record = 12282
    airborne = ds.samples[airborne_record]
    assert int(airborne["seed_t"]["action_id"][0]) == ACT_FALL_SPECIAL
    assert int(airborne["ref_t1"]["action_id"][0]) == ACT_FALL_SPECIAL
    out_air = _run_one_step(ds, airborne_record)
    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out_air[field][0]) == int(airborne["ref_t1"][field][0]), field
    assert float(out_air["pos_y"][0]) == pytest.approx(float(airborne["ref_t1"]["pos_y"][0]), abs=1e-4)

    landing_record = 12283
    landing = ds.samples[landing_record]
    assert int(landing["seed_t"]["action_id"][0]) == ACT_FALL_SPECIAL
    assert int(landing["ref_t1"]["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    out_land = _run_one_step(ds, landing_record)
    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out_land[field][0]) == int(landing["ref_t1"][field][0]), field
    assert float(out_land["pos_y"][0]) == pytest.approx(float(landing["ref_t1"]["pos_y"][0]), abs=1e-4)


@pytest.mark.integration
def test_locked_escapeair_late_platform_projection_keeps_shallow_current_root_airborne() -> None:
    # Same ecb_lock_timer=6 owner as the positive test above, but the current root has not reached
    # the above-root ECB-bottom depth required by mpColl_80044838_Floor's callback-local projection.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/CornyDelayedOkapi.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 3936
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 6
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            5736,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "MediumVirtualPig.msl",
            6897,
            1,
        ),
    ],
)
def test_kneebend_escapeair_early_locked_vertical_window_still_lands(
    dataset_rel: str, record: int, p: int
) -> None:
    # Negative replay-real controls for the sustained EscapeAir window: fresh KneeBend ->
    # EscapeAir is a different source handoff. KneeBend_Anim can enter jump startup, then
    # EscapeAir_Coll resolves LandingFallSpecial on the same callback pass.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    #   ftCo_80099A58,ftCo_EscapeAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_KNEE_BEND
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            4875,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            1581,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            10773,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            4778,
            1,
        ),
    ],
)
def test_jumpaerial_escapeair_transformed_platform_remap_enters_landing_fall_special(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real locks for fresh JumpAerial -> EscapeAir over FoD transformed platforms. Source
    # IASA can enter EscapeAir before the map callback, and the same EscapeAir_Coll pass consumes
    # the callback-local transformed-platform floor result even when mpLib projection remaps the
    # accepted platform line.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    #   ftCo_80099A58,ftCo_EscapeAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_jumpaerial_escapeair_transformed_platform_remap_gate_is_source_scoped() -> None:
    # Synthetic negatives using a retained FoD row:
    # - sustained EscapeAir provenance still keeps the transformed-platform remap guard;
    # - FD/cardinal hard floor behavior is not admitted by the transformed-platform owner.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 4875
    p = 1

    def sustained_escapeair_provenance(seed: np.ndarray) -> None:
        seed["seed_prev_action_id"][0, p] = np.uint16(ACT_ESCAPE_AIR)

    out_sustained = _run_one_step(ds, record, seed_mutator=sustained_escapeair_provenance)
    assert int(out_sustained["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_sustained["on_ground"][p]) == 0

    def fd_hard_floor_mutation(seed: np.ndarray) -> None:
        seed["stage_id"][0] = np.uint32(32)
        seed["ground_id"][0, p] = np.uint16(1)
        seed["pos_x"][0, p] = np.float32(0.0)
        seed["pos_y"][0, p] = np.float32(4.0)
        seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(0.0)
        seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(2.0)

    out_fd = _run_one_step(ds, record, seed_mutator=fd_hard_floor_mutation)
    assert int(out_fd["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_fd["on_ground"][p]) == 0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            680,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "LawfulInsistentMeerkat.msl",
            3654,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            10926,
            0,
        ),
    ],
)
def test_fall_attackair_entry_platform_contact_stays_airborne(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real locks for Fall -> AttackAir IASA over platform contacts. The entry frame carries
    # the pre-entry CollData/ECB callback lifetime and must not be consumed as a generic Landing
    # before AttackAir_Coll owns a stable floor result.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::{
    #   ftCo_AttackAir_EnterFromMsid,ftCo_AttackAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ATTACK_AIR_F
    assert int(row["ref_t1"]["animation_index"][p]) == SM_ATTACK_AIR_F
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            5886,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "SweatyThisMallard.msl",
            6606,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            9375,
            0,
        ),
    ],
)
def test_jump_attackair_platform_entry_does_not_use_fall_owner(
    dataset_rel: str, record: int, p: int
) -> None:
    # Negative replay-real controls from the rejected broad Jump/JumpAerial AttackAir suppression:
    # Jump-family AttackAir entries have distinct locked-ECB/platform ownership and should still
    # land here.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_F
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row["ref_t1"]["animation_index"][p]) == SM_LANDING
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "p"),
    [
        (1532, 0),
        (5954, 1),
    ],
)
def test_fall_transformed_platform_fastfall_callback_lifetime_stays_airborne(
    record: int, p: int
) -> None:
    # Replay-real locks for sustained Fall fastfall contacts on FoD transformed platforms. Fall_Coll
    # routes through ft_800831CC -> mpColl_80047E14; transformed-platform sweeps can see a remapped
    # platform floor before the source callback publishes the landing result.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80047E14}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_FALL
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert int(row["ref_t1"]["action_id"][p]) == ACT_FALL
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
def test_fall_transformed_platform_fastfall_gate_is_source_scoped() -> None:
    # Synthetic negatives using a retained FoD row:
    # - clearing fp->fall_fast exits the fastfall owner and admits the same platform contact.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    # Use a retained Fall frame where clearing fastfall still leaves enough descent to contact the
    # transformed platform, so the negative isolates the callback gate instead of a no-contact row.
    record = 5954
    p = 1

    def not_fastfall(seed: np.ndarray) -> None:
        seed["fall_fast"][0, p] = np.uint8(0)
        seed["state_flags"][0, p, 1] = np.uint8(int(seed["state_flags"][0, p, 1]) & ~0x08)

    out_not_fastfall = _run_one_step(ds, record, seed_mutator=not_fastfall)
    assert int(out_not_fastfall["action_id"][p]) == ACT_LANDING
    assert int(out_not_fastfall["on_ground"][p]) == 1


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "expected_ground"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            1574,
            0,
            3,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            1971,
            0,
            3,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            8911,
            1,
            51,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "PriceyPartialAlbatross.msl",
            7609,
            1,
            2,
        ),
    ],
)
def test_fall_flags6_root_projection_lands_after_callback_floor_contact(
    dataset_rel: str, record: int, p: int, expected_ground: int
) -> None:
    # Replay-real locks for Fall fastfall floor publication when the loaded ECB bottom is above the
    # fighter root. The source path still uses ft_800831CC -> mpColl_80047E14 and then
    # mpColl_80044838_Floor(ignore_bottom=true) to publish Landing from cur_pos when a CollData lock
    # or fastfall ledge-floor owner owns that crossing; this is common-air callback ownership, not a
    # Yoshi/PS/Battlefield row exception.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row["ref_t1"]["animation_index"][p]) == SM_LANDING
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == expected_ground

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "PriceyPartialAlbatross.msl",
            3012,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "PhysicalElectricCapybara.msl",
            7193,
            0,
        ),
    ],
)
def test_fall_no_floor_shallow_fastfall_contact_stays_airborne(
    dataset_rel: str, record: int, p: int
) -> None:
    # Negative locks for no-floor-index Fall fastfall rows whose first bottom contact is shallow and
    # whose movescript allow-interrupt bit is not live yet. These stay in Fall for one more
    # callback; the next deeper row owns Landing through the ordinary Fall_Coll floor path.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert int(row["seed_t"]["ground_id"][p]) == 0xFFFF
    assert int(row["seed_t"]["state_flags"][p, 0]) & 0x80 == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_FALL
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fall_no_floor_shallow_allow_interrupt_contact_lands() -> None:
    # Positive control for the same shallow no-floor Fall path: once the script allow-interrupt bit
    # is live, the ordinary ft_800831CC/mpColl_80047E14 floor handoff may publish Landing even with
    # a shallow bottom contact and no persisted floor.index.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/ft/types.h::Fighter::allow_interrupt (fp+0x2218:0)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 1967
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert int(row["seed_t"]["ground_id"][p]) == 0xFFFF
    assert int(row["seed_t"]["state_flags"][p, 0]) & 0x80 != 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "MediumVirtualPig.msl",
            2753,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "MilkyGracefulStingray.msl",
            5554,
            0,
        ),
    ],
)
def test_fall_ledge_continuation_before_allow_interrupt_stays_airborne(
    dataset_rel: str, record: int, p: int
) -> None:
    # Negative locks for the non-locked ledge-floor continuation branch of Fall_Coll. These rows
    # carry a replay-visible floor.index, but the source allow-interrupt phase is not live yet, so
    # vanilla keeps the first fastfall ledge crossing airborne for one more callback. ECB-locked
    # rows and allow-interrupt rows are covered by the positive flags-6 root-projection tests.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/types.h::Fighter::allow_interrupt (fp+0x2218:0)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert int(row["seed_t"]["ground_id"][p]) != 0xFFFF
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 0
    assert int(row["seed_t"]["state_flags"][p, 0]) & 0x80 == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_FALL
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "FlippantEnchantedHorse.msl",
            94,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "FlippantEnchantedHorse.msl",
            107,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            10947,
            1,
        ),
    ],
)
def test_fall_flags6_root_projection_rejects_unowned_stale_floor_index(
    dataset_rel: str, record: int, p: int
) -> None:
    # Negative locks for stale Fall floor.index snapshots that do not have the retained source
    # owner. Record 94 has an ECB lock but is not fastfall; record 107 is fastfall but has neither a
    # prefix-causal CollData lock nor a ledge-floor continuation; record 10947 is already on the same
    # ledge floor instead of continuing from an adjacent ledge segment. All stay airborne in vanilla.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["ref_t1"]["action_id"][p]) == ACT_FALL
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_guardon_powershield_reflect_preempts_platform_pass() -> None:
    # GuardOn IASA source order puts ftCo_80093694 powershield reflect before
    # ftCo_8009A080 platform pass. A held-down platform shield row with a fresh LR edge must
    # therefore enter GuardReflect, not consume the down input as Pass.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardOn_IASA,ftCo_80093694}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A080
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 3516
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_GUARD_ON
    assert int(row["ref_t1"]["action_id"][p]) == ACT_GUARD_REFLECT
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
def test_guardon_ucf_shielddrop_suppresses_spotdodge_without_lr_edge() -> None:
    # Negative control on the same replay-real row: if the current LR input is held but not a fresh
    # edge, powershield-reflect is absent. The diagonal rim down input is an Axe-method UCF
    # shield-drop row, so UCF suppresses the earlier spotdodge branch and platform pass wins.
    #
    # refs/ucf/src/shielddrop/shielddrop.S
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOn_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A080
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 3516
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_GUARD_ON

    def remove_lr_edge(prev_input: np.ndarray, input_t: np.ndarray) -> None:
        prev_input["p"][0, p]["buttons"] = input_t["p"][0, p]["buttons"]
        prev_input["p"][0, p]["r"] = input_t["p"][0, p]["r"]

    out = _run_one_step(ds, record, input_mutator=remove_lr_edge)
    assert int(out["action_id"][p]) == ACT_PASS
    assert int(out["on_ground"][p]) == 0


@pytest.mark.integration
def test_fresh_guardon_nonshield_entry_does_not_platform_pass_same_callback() -> None:
    # Same replay-real platform row, but with powershield-reflect and spotdodge removed. This
    # no-submotion GuardOn snapshot came from a non-shield callback owner, so the current fighter
    # proc has already consumed its input callback and must not immediately run the platform-pass
    # IASA tail.
    #
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardOn_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_8009980C
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A080
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 3516
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_GUARD_ON

    def refresh_pass_tilt_timer(seed: np.ndarray) -> None:
        seed["tilt_timer_y"][0, p] = np.uint8(0)

    def remove_lr_edge_and_spotdodge(prev_input: np.ndarray, input_t: np.ndarray) -> None:
        input_t["p"][0, p]["buttons"] = np.uint16(int(input_t["p"][0, p]["buttons"]) | BUTTON_L)
        prev_input["p"][0, p]["buttons"] = input_t["p"][0, p]["buttons"]
        prev_input["p"][0, p]["r"] = input_t["p"][0, p]["r"]
        prev_input["p"][0, p]["main_y"] = np.int8(0)
        input_t["p"][0, p]["main_y"] = np.int8(-55)

    out = _run_one_step(
        ds,
        record,
        seed_mutator=refresh_pass_tilt_timer,
        input_mutator=remove_lr_edge_and_spotdodge,
    )
    assert int(out["action_id"][p]) == ACT_GUARD_ON
    assert int(out["on_ground"][p]) == 1
