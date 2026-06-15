from __future__ import annotations

import math
import json
from argparse import Namespace
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tests.test_colldata_ecb_substrate import _colldata_ecb_dtype
from tools.eval.discrete_compare_lanes import compile_discrete_compare_lanes, first_mismatch_values
from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset
from tools.eval.validation_profile import get_validation_profile
from tools.slippi.make_dataset_from_slp import _main_impl


BUTTON_L = 0x0040
BUTTON_R = 0x0020

ACT_ESCAPE_AIR = 236
ACT_LANDING_FALL_SPECIAL = 43
ACT_FX_SPECIAL_AIR_LW_START = 365
ACT_FX_SPECIAL_AIR_LW_LOOP = 366
ACT_FALL_SPECIAL = 35
ACT_FALL_AERIAL = 32
ACT_WAIT = 14
ACT_JUMP_AERIAL_F = 27
ACT_JUMP_AERIAL_B = 28
ACT_KNEE_BEND = 24
ACT_FALL = 29
ACT_PASS = 244
ACT_FX_SPECIAL_AIR_N_START = 344
ACT_JUMP_F = 25
ACT_ATTACK_AIR_N = 65
ACT_ATTACK_AIR_F = 66
ACT_ATTACK_AIR_B = 67
ACT_ATTACK_AIR_HI = 68
ACT_ATTACK_AIR_LW = 69
ACT_ATTACK_AIR_LW_TRUE = ACT_ATTACK_AIR_LW
ACT_DAMAGE_AIR_2 = 85
ACT_LANDING_AIR_N = 70
ACT_LANDING_AIR_B = 72
ACT_LANDING_AIR_HI = 73
ACT_LANDING_AIR_LW = 74
ACT_LANDING_AIR_LW_TRUE = ACT_LANDING_AIR_LW
ACT_LANDING = 42
ACT_GUARD_ON = 178
ACT_GUARD = 179
ACT_GUARD_OFF = 180
ACT_GUARD_REFLECT = 182
ACT_PASSIVE = 199
ACT_THROW_F = 219
ACT_ESCAPE_N = 235
ACT_MISS_FOOT = 251
ACT_JUMP_B = 26
SM_ESCAPE_AIR = 44
SM_LANDING_FALL_SPECIAL = 36
SM_JUMP_AERIAL_F = 18
SM_WAIT1_0 = 2
SM_FALL = 20
SM_ATTACK_AIR_F = 69
SM_LANDING = 35
SM_FALL_AERIAL = 23
SM_FX_SPECIAL_AIR_N_START = 298
CHAR_FOX = 1
CHAR_FALCO = 22
CHAR_MARTH = 18
CHAR_SHEIK = 7
STAGE_FD = 32
STAGE_FOD = 2
ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_HARD_FLOOR = 3
MPCOLL_REJECT_ATTACKAIR_TRANSFORMED_PLATFORM_ECB_ONLY = 1 << 11
MPCOLL_REJECT_MISSFOOT_ECB_LOCK_FIRST_FLOOR = 1 << 37


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
        return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)


def _debug_commonfall_seed_state(ds, record: int, player: int) -> tuple[float, int]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])

    row = ds.samples[record : record + 1].copy()
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed(handle, row["seed_t"].view("u1").reshape(1, seed_stride).copy())
        x4, msid = binding.debug_common_fall_blend_state(handle, 0, player)
        return float(x4), int(msid)
    finally:
        binding.destroy(handle)


def _rollout_first_mismatch_through(ds, target_record: int):
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    samples = ds.samples
    num_players = int(ds.header["num_players"])
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_bytes.view(COMPARE_DTYPE).reshape(1)
    lanes = compile_discrete_compare_lanes(
        ("action_id", "animation_index", "on_ground", "hitlag", "hitstun", "state_flags"),
        tuple(range(num_players)),
        profile=get_validation_profile("rl1_gameplay"),
    )

    handle = binding.init(
        batch_size=1,
        num_players=num_players,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        needs_seed = True
        last_out = None
        last_mismatch = None
        for record in range(target_record + 1):
            if needs_seed:
                seed_bytes[0, :] = samples_u8[record, seed_off : seed_off + seed_stride]
                binding.reseed_seed_rollout(handle, seed_bytes)
                needs_seed = False
            prev_input_bytes[0, :] = samples_u8[
                record, prev_input_off : prev_input_off + input_stride
            ]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_bytes)
            last_out = out_view[0].copy()
            last_mismatch = first_mismatch_values(
                seed_row=samples["seed_t"][record],
                out_row=last_out,
                ref_row=samples["ref_t1"][record],
                lanes=lanes,
            )
            if last_mismatch is None:
                continue

            seed_bytes[0, :] = samples_u8[record, seed_off : seed_off + seed_stride]
            binding.reseed_seed_rollout(handle, seed_bytes)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_bytes)
            last_out = out_view[0].copy()
            last_mismatch = first_mismatch_values(
                seed_row=samples["seed_t"][record],
                out_row=last_out,
                ref_row=samples["ref_t1"][record],
                lanes=lanes,
            )
            if last_mismatch is not None:
                needs_seed = True
        return last_out, last_mismatch
    finally:
        binding.destroy(handle)


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


def _run_rollout_to_record_with_colldata(ds, start_record: int, target_record: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    colldata_stride = int(sizes["colldata_ecb"])
    colldata_dtype = _colldata_ecb_dtype()
    assert compare_stride == COMPARE_DTYPE.itemsize
    assert colldata_stride == colldata_dtype.itemsize

    samples = ds.samples
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    colldata_bytes = np.empty((1, colldata_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(
            handle,
            samples[start_record : start_record + 1]["seed_t"]
            .view("u1")
            .reshape(1, seed_stride)
            .copy(),
        )
        for record in range(start_record, target_record + 1):
            binding.step_input(
                handle,
                samples[record : record + 1]["prev_input_t"]
                .view("u1")
                .reshape(1, input_stride)
                .copy(),
                samples[record : record + 1]["input_t"]
                .view("u1")
                .reshape(1, input_stride)
                .copy(),
            )
        binding.write_compare(handle, out_bytes)
        binding.debug_write_colldata_ecb(handle, colldata_bytes)
    finally:
        binding.destroy(handle)
    return (
        out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy(),
        colldata_bytes.view(colldata_dtype).reshape((1,))[0].copy(),
    )


def _run_rollout_to_record_with_seed_mutator(
    ds, start_record: int, target_record: int, *, seed_mutator
) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    samples = ds.samples
    start = samples[start_record : start_record + 1].copy()
    seed_mutator(start["seed_t"])
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(
            handle, start["seed_t"].view("u1").reshape(1, seed_stride).copy()
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


def _run_one_step_with_colldata(ds, record: int, *, seed_mutator=None) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    colldata_stride = int(sizes["colldata_ecb"])
    colldata_dtype = _colldata_ecb_dtype()
    assert compare_stride == COMPARE_DTYPE.itemsize
    assert colldata_stride == colldata_dtype.itemsize

    row = ds.samples[record : record + 1].copy()
    if seed_mutator is not None:
        seed_mutator(row["seed_t"])

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    colldata_bytes = np.empty((1, colldata_stride), dtype=np.uint8)
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
        binding.debug_write_colldata_ecb(handle, colldata_bytes)
    finally:
        binding.destroy(handle)
    return (
        out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy(),
        colldata_bytes.view(colldata_dtype).reshape((1,))[0].copy(),
    )


def _blank_input(input_stride: int) -> np.ndarray:
    return np.zeros((1, input_stride), dtype=np.uint8)


def _synthetic_air_seed(
    *, stage_id: int, action_id: int, animation_index: int, x: float, y: float
) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(stage_id)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["pos_x"][0, 0] = np.float32(x)
    seed["pos_y"][0, 0] = np.float32(y)
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, 1] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    seed["ground_id"][0, 1] = np.uint16(1)
    seed["action_id"][0, 0] = np.uint16(action_id)
    seed["animation_index"][0, 0] = np.uint32(animation_index)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["action_id"][0, 1] = np.uint16(14)
    seed["animation_index"][0, 1] = np.uint32(SM_WAIT1_0)
    return seed


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
    assert int(row["seed_t"]["action_id"][p]) in (ACT_JUMP_AERIAL_F, ACT_ESCAPE_AIR)
    assert int(row["seed_t"]["seed_prev_action_id"][p]) in (ACT_JUMP_AERIAL_F, ACT_ESCAPE_AIR)
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
    assert int(row["seed_t"]["action_id"][p]) in (ACT_JUMP_AERIAL_F, ACT_ESCAPE_AIR)
    assert int(row["seed_t"]["seed_prev_action_id"][p]) in (ACT_JUMP_AERIAL_F, ACT_ESCAPE_AIR)
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_fod_throwf_slope_to_flat_seam_uses_returned_floor_line() -> None:
    # FoD grounded ThrowF over the main-floor slope/flat seam:
    # - Grounded ThrowF_Coll routes through ft_800841B8 -> ft_800827A0 -> mpColl_8004B2DC.
    # - mpLib_8004DD90_Floor can traverse from FoD's right main-floor slope segment to the
    #   connected flat ledge segment in one callback. The root height must use the returned flat
    #   floor line, not the slope extrapolated beyond its endpoint.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_ThrowF_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800841B8,ft_800827A0}
    # refs/melee/src/melee/mp/{mpcoll.c::mpColl_8004B2DC,mplib.c::mpLib_8004DD90_Floor}
    # data/motion_state/owners/{fox,falco}.bin::MSLMSO01 class GROUNDED_STAGE_OBJECT_CARRY_COLL
    # data/stages/bin/griz.bin::MSLSTG01 floor segment links
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
    p = 1

    pre_seam = ds.samples[4483]
    assert int(pre_seam["seed_t"]["action_id"][p]) == ACT_THROW_F
    assert int(pre_seam["ref_t1"]["ground_id"][p]) == 6
    pre_out, pre_dbg = _run_one_step_with_colldata(ds, 4483)
    assert int(pre_out["ground_id"][p]) == 6
    assert int(pre_dbg["floor_result_segment_id"][p]) == 6
    assert float(pre_out["pos_y"][p]) == pytest.approx(float(pre_seam["ref_t1"]["pos_y"][p]), abs=1e-6)

    seam = ds.samples[4484]
    assert int(seam["seed_t"]["action_id"][p]) == ACT_THROW_F
    assert int(seam["seed_t"]["ground_id"][p]) == 6
    assert int(seam["ref_t1"]["ground_id"][p]) == 7
    out, dbg = _run_one_step_with_colldata(ds, 4484)
    ref = seam["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
      assert int(out[field][p]) == int(ref[field][p]), field
    assert int(dbg["floor_result_segment_id"][p]) == 7
    assert float(dbg["floor_result_contact_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=3e-6)


@pytest.mark.integration
def test_fod_locked_escapeair_hard_floor_root_crossing_lands() -> None:
    # Positive replay-real lock for an early sustained EscapeAir hard-floor handoff on FoD.
    # EscapeAir_Coll delegates through ft_80082C74 -> mpColl_800471F8 while CollData_X130_Locked
    # is active. On non-ledge hard floors the source can publish LandingFallSpecial from the
    # callback-local root crossing even when the replay seed does not expose a desired-bottom owner.
    # Platform and ledge contacts remain covered by their separate EscapeAir guards.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
    # data/stages/bin/griz.bin::MSLSTG01 floor segment links
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
    record = 1278
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["ecb_lock_timer"][p]) != 0
    assert int(row["seed_t"]["ecb_lock_bottom_rel_y_valid_u8"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 5

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_fod_fresh_jumpaerial_escapeair_locked_bottom_sweep_lands_on_hard_floor() -> None:
    # Fresh JumpAerial -> EscapeAir over FoD hard floor:
    # - JumpAerial IASA can enter EscapeAir before Fighter_procMap.
    # - EscapeAir_Coll then runs ft_80082C74 -> mpColl_800471F8 while CollData_X130_Locked
    #   preserves the desired ECB bottom. `mpColl_80044628_Floor` consumes the current desired
    #   bottom sweep, and `mpColl_80044838_Floor(ignore_bottom=true)` projects the root to the
    #   accepted hard floor.
    # - The adjacent pre-entry JumpAerial row must remain airborne; the retained owner begins on
    #   the entered EscapeAir callback row.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_LoadECB_inline,mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
    # data/stages/bin/griz.bin::MSLSTG01 floor segment links
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    p = 1

    pre = ds.samples[9749]
    assert int(pre["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(pre["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    pre_out = _run_one_step(ds, 9749)
    assert int(pre_out["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(pre_out["on_ground"][p]) == 0

    row = ds.samples[9750]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 4
    assert int(row["seed_t"]["ecb_lock_bottom_rel_y_valid_u8"][p]) == 1
    assert float(row["seed_t"]["ecb_lock_bottom_rel_y_f32"][p]) > 0.0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["ground_id"][p]) == 7

    out, dbg = _run_one_step_with_colldata(ds, 9750)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert int(dbg["floor_result_segment_id"][p]) == 7
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=3e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "max_abs_pos_x_err"),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "DistinctCaringCobra.msl",
            7308,
            1,
            0.05,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "PutridJoyousOryx.msl",
            3339,
            1,
            0.10,
        ),
    ],
)
def test_jump_entry_escapeair_does_not_use_ground_departure_wall_packet(
    dataset_rel: str, record: int, p: int, max_abs_pos_x_err: float
) -> None:
    # Regression lock for the DCC/PJO float movement found during the newchar rebase:
    # JumpAerial/KneeBend -> EscapeAir rows use their own callback-local floor/ECB producers and
    # must not inherit the Fall/run-off ground-departure locked-bottom wall packet. Over-applying
    # that packet preserved the discrete state but moved pos_x by ~0.2-0.85 units.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["seed_prev_action_id"][p]) in (ACT_KNEE_BEND, ACT_JUMP_AERIAL_F, ACT_JUMP_AERIAL_B)
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR

    out, _dbg = _run_one_step_with_colldata(ds, record)
    ref = row["ref_t1"]
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=3e-6)
    assert abs(float(out["pos_x"][p]) - float(ref["pos_x"][p])) <= max_abs_pos_x_err


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "expected_ground"),
    [
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "DelayedSuperbGuanaco.msl",
            3596,
            1,
            4,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "ShadyDecimalStarling.msl",
            5304,
            1,
            1,
        ),
    ],
)
def test_escapeair_locked_loaded_ecb_bottom_sweep_lands_on_static_platform(
    dataset_rel: str, record: int, p: int, expected_ground: int
) -> None:
    # EscapeAir_Coll uses ft_80082C74 -> mpColl_800471F8 with the callback-local loaded ECB.
    # When CollData_X130_Locked is still live, the previous loaded ECB bottom can sweep through a
    # static platform even if the current loaded bottom has collapsed back to root height and no
    # replay desired-bottom owner is serialized. That is still a live mpColl_80044628_Floor producer
    # and then mpColl_80044838_Floor projects LandingFallSpecial onto the accepted platform. The
    # Battlefield sustained EscapeAir and Dream Land JumpAerial -> EscapeAir rows exercise the same
    # source owner without any stage/action-frame exception.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) in (ACT_JUMP_AERIAL_F, ACT_ESCAPE_AIR)
    assert int(row["seed_t"]["seed_prev_action_id"][p]) in (ACT_JUMP_AERIAL_F, ACT_ESCAPE_AIR)
    assert int(row["seed_t"]["ecb_lock_timer"][p]) != 0
    assert float(row["seed_t"]["ecb_lock_bottom_rel_y_f32"][p]) > 0.0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["ground_id"][p]) == expected_ground

    out, dbg = _run_one_step_with_colldata(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert int(dbg["floor_result_segment_id"][p]) == expected_ground
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_fod_fresh_jump_escapeair_height_platform_lands_without_broad_ground_jump_snap() -> None:
    # Fresh ground-jump -> EscapeAir over FoD height platforms:
    # JumpF/JumpB can enter EscapeAir before Fighter_procMap, then EscapeAir_Coll/mpColl_800471F8
    # accepts the current source-trusted grIzumi height-platform line under the root even though the
    # carried floor.index still names main floor. This is FoD height-transform ownership, not a broad
    # ground-jump airdodge snap; the Pokemon Stadium control has the same JumpF -> EscapeAir shape but
    # no generated height-platform line and remains airborne.
    #
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    #   ftCo_80099A58,ftCo_EscapeAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    fod_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    ps_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "SweatyThisMallard.msl"
    )
    if not fod_path.exists() or not ps_path.exists():
        pytest.skip("missing local FoD/Pokemon Stadium validation datasets")

    fod = read_dataset(str(fod_path))
    p = 0
    row = fod.samples[2951]
    assert int(row["seed_t"]["stage_id"]) == 2
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_B
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_B
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 0

    out = _run_one_step(fod, 2951)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)

    ps = read_dataset(str(ps_path))
    control = ps.samples[7388]
    assert int(control["seed_t"]["stage_id"]) != 2
    assert int(control["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(control["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_F
    assert int(control["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(control["ref_t1"]["on_ground"][p]) == 0

    control_out = _run_one_step(ps, 7388)
    control_ref = control["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(control_out[field][p]) == int(control_ref[field][p]), field


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
def test_sustained_escapeair_desired_bottom_platform_sweep_publishes_despite_downheld_rollout() -> None:
    # EscapeAir_Coll delegates to ft_80082C74 -> ft_80081D0C -> mpColl_800471F8. That path calls
    # mpColl_80044628_Floor with cb=NULL, so a pass-through stick does not reject a soft platform
    # once the preserved desired ECB bottom has accepted the current callback's floor sweep.
    #
    # Regression target: a JumpAerial -> EscapeAir rollout over Battlefield's right platform was
    # kept airborne by treating down-held input as an independent platform-pass veto after the
    # source bottom sweep had already hit the carried platform.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_800471F8,mpColl_80046904,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/battlefield_recent/DelayedSuperbGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    start_record = 3558
    target_record = 3596
    p = 1
    row = ds.samples[target_record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["input_t"]["p"][p]["main_y"]) < 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 4

    out = _run_rollout_to_record(ds, start_record, target_record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


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
                5485,
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
    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_N
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert int(row["seed_t"]["ground_id"][p]) != 0xFFFF
    assert int(row["seed_t"]["floor_skip_segment_id_u16"][p]) == 0xFFFF
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_AIR_N
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_rollout_to_record(ds, start_record, target_record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("start_record", "target_record", "p", "action_id"),
    [
        (1638, 1640, 0, ACT_ATTACK_AIR_HI),
        (1688, 1692, 0, ACT_ATTACK_AIR_N),
        (1602, 1604, 1, ACT_ATTACK_AIR_LW_TRUE),
    ],
)
def test_attackair_transformed_platform_rollout_publishes_floor_skip_lifetime_pte(
    start_record: int, target_record: int, p: int, action_id: int
) -> None:
    # Rollout-real positive for PTE's FoD AttackAir transformed-platform floor-skip lifetime.
    # The source owner is AttackAir_Coll -> ft_80082C74 -> mpColl_800471F8: while callback-visible
    # stick is below p_ftCommonData->x25C and no current floor-source bit has accepted the FoD
    # height platform, the root-crossing pass publishes the platform as CollData.floor_skip. Live
    # grIzumi scheduler velocity proves the platform pose only; it does not bypass the one-way
    # platform rejection. The released LandingAirN control below and MGS 2013 clear-after-crossing
    # rollout lock guard the adjacent normal landing handoffs.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpUpdateFloorSkip}
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
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
    row = ds.samples[target_record]
    assert int(row["seed_t"]["stage_id"]) == STAGE_FOD
    assert int(row["seed_t"]["action_id"][p]) == action_id
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == action_id
    assert int(row["seed_t"]["floor_skip_segment_id_u16"][p]) == 1
    assert int(row["seed_t"]["floor_skip_segment_valid_u8"][p]) == 1
    assert int(row["ref_t1"]["action_id"][p]) == action_id
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out, colldata = _run_rollout_to_record_with_colldata(ds, start_record, target_record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert int(colldata["floor_skip_valid"][p]) == 1
    assert int(colldata["floor_skip_segment_id"][p]) == 1


@pytest.mark.integration
def test_fall_stale_static_platform_first_hard_floor_contact_stays_airborne_sds() -> None:
    # Replay-real lock for a non-fastfall Fall_Coll row carrying a stale one-way platform
    # CollData.floor.index. The first shallow hard-floor bottom crossing below the platform does not
    # satisfy the source `mpColl_80044628_Floor` producer for `mpColl_80044838_Floor`; vanilla keeps
    # Fall airborne for one callback, then the next deeper callback publishes ordinary Landing.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        "ShadyDecimalStarling.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    p = 0
    shallow = ds.samples[7169]
    assert int(shallow["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(shallow["seed_t"]["fall_fast"][p]) == 0
    assert int(shallow["seed_t"]["ground_id"][p]) == 0
    assert float(shallow["seed_t"]["speed_air_x_self"][p]) * float(
        shallow["seed_t"]["facing_dir1"][p]
    ) < -0.5
    assert int(shallow["ref_t1"]["action_id"][p]) == ACT_FALL
    assert int(shallow["ref_t1"]["on_ground"][p]) == 0

    shallow_out = _run_one_step(ds, 7169)
    shallow_ref = shallow["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(shallow_out[field][p]) == int(shallow_ref[field][p]), field
    assert float(shallow_out["pos_y"][p]) == pytest.approx(float(shallow_ref["pos_y"][p]), abs=1e-6)

    deeper = ds.samples[7170]
    assert int(deeper["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(deeper["seed_t"]["fall_fast"][p]) == 0
    assert int(deeper["seed_t"]["ground_id"][p]) == 0
    assert int(deeper["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(deeper["ref_t1"]["on_ground"][p]) == 1
    assert int(deeper["ref_t1"]["ground_id"][p]) == 4

    deeper_out = _run_one_step(ds, 7170)
    deeper_ref = deeper["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(deeper_out[field][p]) == int(deeper_ref[field][p]), field
    assert float(deeper_out["pos_y"][p]) == pytest.approx(float(deeper_ref["pos_y"][p]), abs=1e-6)

    for record in (3439, 5819):
        neutral = ds.samples[record]
        assert int(neutral["seed_t"]["action_id"][p]) == ACT_FALL
        assert int(neutral["seed_t"]["fall_fast"][p]) == 0
        assert int(neutral["seed_t"]["ground_id"][p]) == 0
        assert float(neutral["seed_t"]["speed_air_x_self"][p]) * float(
            neutral["seed_t"]["facing_dir1"][p]
        ) > 0.5
        assert int(neutral["ref_t1"]["action_id"][p]) == ACT_LANDING
        assert int(neutral["ref_t1"]["on_ground"][p]) == 1
        assert int(neutral["ref_t1"]["ground_id"][p]) == 4

        neutral_out = _run_one_step(ds, record)
        neutral_ref = neutral["ref_t1"]
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
            assert int(neutral_out[field][p]) == int(neutral_ref[field][p]), field
        assert float(neutral_out["pos_y"][p]) == pytest.approx(
            float(neutral_ref["pos_y"][p]), abs=1e-6
        )

    for dataset_rel, record, player in (
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            1427,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            1601,
            1,
        ),
    ):
        dataset_path = root / dataset_rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_rel}")
        control_ds = read_dataset(str(dataset_path))
        control = control_ds.samples[record]
        assert int(control["seed_t"]["action_id"][player]) == ACT_FALL
        assert int(control["seed_t"]["fall_fast"][player]) == 0
        assert float(control["seed_t"]["speed_air_x_self"][player]) * float(
            control["seed_t"]["facing_dir1"][player]
        ) < -0.5
        assert int(control["ref_t1"]["action_id"][player]) == ACT_LANDING
        assert int(control["ref_t1"]["on_ground"][player]) == 1

        control_out = _run_one_step(control_ds, record)
        control_ref = control["ref_t1"]
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
            assert int(control_out[field][player]) == int(control_ref[field][player]), field
        assert float(control_out["pos_y"][player]) == pytest.approx(
            float(control_ref["pos_y"][player]), abs=2e-4
        )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "p", "seed_action", "ref_action"),
    [
        (7843, 0, ACT_ATTACK_AIR_N, ACT_LANDING_AIR_N),
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
    ("dataset_rel", "record", "p", "expected_char", "expected_source_bits"),
    [
        (
            "datasets/marth/replays/validation/marth/InternalPowerlessWallaby.msl",
            9718,
            1,
            CHAR_MARTH,
            (0x02, 0x00),
        ),
        (
            "datasets/marth/replays/validation/marth/VigorousRelievedLlama.msl",
            3110,
            0,
            CHAR_MARTH,
            (0x00, 0x00),
        ),
        (
            "datasets/marth/replays/validation/marth/FemaleWorthyAlpaca.msl",
            3738,
            0,
            CHAR_FOX,
            (0x00, 0x02),
        ),
    ],
)
def test_attackairn_fod_height_platform_requires_current_source_to_land(
    dataset_rel: str, record: int, p: int, expected_char: int, expected_source_bits: tuple[int, int]
) -> None:
    # AttackAirN_Coll can only publish LandingAirN from a FoD height platform after the callback
    # has current mpColl/grIzumi authority for that platform. Sparse restored heights and carried
    # ground-contact provenance reconstruct line geometry, but are not source floor producers for
    # the common AttackAir final publication path.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # data/motion_state/owners/<char>.bin::MSLMSO01 submotion_id
    # data/scripts/<char>.bin::MSLFTSC1 set_cmd_var/create_hitbox/clear_hitboxes events
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_id"]) == STAGE_FOD
    assert int(row["seed_t"]["char_id"][p]) == expected_char
    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_N
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ATTACK_AIR_N
    assert tuple(int(v) for v in row["seed_t"]["stage_fod_platform_height_source_u8"]) == expected_source_bits
    assert not any(int(v) & 0x04 for v in row["seed_t"]["stage_fod_platform_height_source_u8"])
    assert binding.move_tables_debug_query(
        "attackair_cmd0",
        int(row["seed_t"]["char_id"][p]),
        ACT_ATTACK_AIR_N,
        float(row["seed_t"]["anim_frame_f32"][p]),
        0.0,
    )
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ATTACK_AIR_N
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action", "source_platform_i"),
    [
        (
            "datasets/marth/replays/validation/marth/InternalPowerlessWallaby.msl",
            488,
            1,
            ACT_ATTACK_AIR_N,
            ACT_LANDING_AIR_N,
            1,
        ),
        (
            "datasets/marth/replays/validation/marth/InternalPowerlessWallaby.msl",
            3095,
            1,
            ACT_ATTACK_AIR_N,
            ACT_LANDING_AIR_N,
            0,
        ),
    ],
)
def test_attackairn_fod_same_step_height_platform_source_lands(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int, source_platform_i: int
) -> None:
    # Adjacent positive control: same-step grIzumi contact is current platform authority, so the
    # same generated AttackAirN common collision callback may publish LandingAirN.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_id"]) == STAGE_FOD
    assert int(row["seed_t"]["action_id"][p]) == seed_action
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][source_platform_i]) & 0x04
    assert int(row["ref_t1"]["action_id"][p]) == ref_action
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action", "source_platform_i", "source_bit"),
    [
        (
            "datasets/marth/replays/validation/marth/InternalPowerlessWallaby.msl",
            9574,
            1,
            ACT_ATTACK_AIR_F,
            71,
            1,
            0x02,
        ),
        (
            "datasets/marth/replays/validation/marth/VigorousRelievedLlama.msl",
            8578,
            1,
            69,
            74,
            0,
            0x02,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "MilkyGracefulStingray.msl",
            739,
            0,
            ACT_ATTACK_AIR_F,
            71,
            1,
            0x01,
        ),
    ],
)
def test_non_nair_attackair_fod_height_platform_source_controls_still_land(
    dataset_rel: str,
    record: int,
    p: int,
    seed_action: int,
    ref_action: int,
    source_platform_i: int,
    source_bit: int,
) -> None:
    # Negative controls for the Nair-only source owner above: single-create Fair and DAir's
    # callback-local live-floor owner keep their existing LandingAir publication paths.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_id"]) == STAGE_FOD
    assert int(row["seed_t"]["action_id"][p]) == seed_action
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][source_platform_i]) & source_bit
    assert int(row["ref_t1"]["action_id"][p]) == ref_action
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


def _callback_first_create_frame_from_moves_json(char_key: str, move_key: str) -> int:
    moves_path = Path(__file__).resolve().parents[1] / "data/moves" / f"{char_key}.json"
    data = json.loads(moves_path.read_text())
    events = data["moves"][move_key]["events"]
    first_create = min(int(ev["frame"]) for ev in events if ev["kind"] == "create_hitbox")
    # Extracted script frames are command frames; the collision callback observes the post-Anim
    # action-frame coordinate one tick earlier.
    return first_create - 1


@pytest.mark.integration
@pytest.mark.parametrize("record", [8576, 8577])
def test_attackairlw_fod_height_platform_pre_first_create_stays_airborne(record: int) -> None:
    # Falco DAir's common AttackAir_Coll path cannot publish a FoD height-platform landing before
    # the extracted first create_hitbox callback edge. The seed has a reconstructed platform line,
    # but source has not reached the script-authored DAir platform publication owner yet.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # data/moves/falco.json::moves.ftCo_SM_AttackAirLw.events
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/marth/replays/validation/marth/VigorousRelievedLlama.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    first_create_frame = _callback_first_create_frame_from_moves_json("falco", "ftCo_SM_AttackAirLw")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    p = 1
    assert int(row["seed_t"]["stage_id"]) == STAGE_FOD
    assert int(row["seed_t"]["char_id"][p]) == CHAR_FALCO
    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_LW_TRUE
    assert int(row["seed_t"]["action_frame"][p]) < first_create_frame
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][0]) & 0x02
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ATTACK_AIR_LW_TRUE
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_attackairlw_fod_height_platform_first_create_lands() -> None:
    # Adjacent positive: at the extracted first create_hitbox callback edge, Falco DAir has reached
    # the source script owner and may publish LandingAirLw through the accepted FoD platform floor.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/marth/replays/validation/marth/VigorousRelievedLlama.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    first_create_frame = _callback_first_create_frame_from_moves_json("falco", "ftCo_SM_AttackAirLw")
    ds = read_dataset(str(dataset_path))
    record = 8578
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_id"]) == STAGE_FOD
    assert int(row["seed_t"]["char_id"][p]) == CHAR_FALCO
    assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_LW_TRUE
    assert int(row["seed_t"]["action_frame"][p]) == first_create_frame
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][0]) & 0x02
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_AIR_LW_TRUE
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


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
def test_randall_path_uses_post_start_frame_id_for_damageair_landing_cnm_7056() -> None:
    # Replay-real lock for Yoshi's Story Randall phase in one-step collision.
    #
    # Gameplay situation:
    # - Fox is falling in DamageAir2 above the left-side Randall pass.
    # - Vanilla lands on the cloud at CNM:7056; indexing the generated path as `frame_id - 123`
    #   leaves Randall 123 frames stale and misses the floor.
    #
    # Source owner:
    # - `frame_id` in MSL seed/compare rows is already the post-start Slippi game frame.
    # - Randall collision consumes MSLSTG01 platform_path records refreshed by the GrSt
    #   stage-object callback, so the path lookup uses `frame_id % 1200`.
    # refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,grStory_801E33E0}
    # refs/melee/src/melee/gr/ground.c::Ground_801C2FE0
    # data/stages/bin/grst.bin::MSLSTG01 platform_path records
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "CheeryNumbMonkey.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 7056
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_DAMAGE_AIR_2
    assert int(row["ref_t1"]["action_id"][p]) == ACT_DAMAGE_AIR_2
    assert int(row["seed_t"]["on_ground"][p]) == 0
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2.0e-4)


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
    # platform floor.index, not a broad SpecialAirN action gate or unconditional platform land.
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
    assert int(out["action_id"][p]) == ACT_WAIT
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
def test_jumpaerial_escapeair_static_platform_overstep_stays_airborne_feh() -> None:
    # FEH reaches Dream Land top platform from fresh JumpAerial -> EscapeAir with a shallow
    # callback-local bottom crossing whose final platform correction is larger than the current
    # EscapeAir vertical step. That is still the pre-entry CollData/ECB handoff, not a source
    # LandingFallSpecial publication.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        "FlippantEnchantedHorse.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 11156
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_no_lock_escapeair_static_platform_overstep_stays_airborne_feh() -> None:
    # Continuation boundary for the common no-lock EscapeAir static-platform owner: after the
    # JumpAerial entry handoff has become sustained EscapeAir, the source mpColl_80044628_Floor
    # still requires the current callback ECB bottom to reach the static platform within the current
    # vertical step. FEH's top-platform row crosses only by the interpolated high bottom and remains
    # airborne in vanilla.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        "FlippantEnchantedHorse.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 11157
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_no_lock_escapeair_ignores_fod_static_y_platform_transform_pte() -> None:
    # FoD's center platform is a generated `static_y` platform transform, not an ordinary static
    # soft platform. Source `mpColl_80044628_Floor` still tests the callback-local CollData
    # prev/current bottom interval through `mpCheckFloorRemap`; this Fox control stays airborne
    # because the probed previous bottom is already below line 2, so `mpLineIntersectionH` rejects.
    #
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=static_y,line_id=2)
    # reports/triage/newchar_sheik/fall_floor_probe_pte_422_p0/
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044628_Floor}
    # refs/melee/src/melee/mp/mplib.c::{mpCheckFloorRemap,mpLineIntersectionH}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 422
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record"),
    [
        ("datasets/sheik/replays/validation/sheik/ConstantStiffOtter.msl", 119),
        ("datasets/sheik/replays/validation/sheik/ConstantStiffOtter.msl", 254),
        ("datasets/sheik/replays/validation/sheik/ConstantStiffOtter.msl", 318),
        ("datasets/sheik/replays/validation/sheik/ConstantStiffOtter.msl", 644),
        ("datasets/sheik/replays/validation/sheik/ConstantStiffOtter.msl", 706),
        ("datasets/sheik/replays/validation/sheik/SnarlingHelplessBeaver.msl", 704),
    ],
)
def test_sheik_escapeair_fod_static_y_remap_bottom_sweep_lands(
    dataset_rel: str, record: int
) -> None:
    # Positive controls for FoD static-y transformed-platform remap. Vanilla accepts these Sheik
    # no-lock EscapeAir rows through `mpColl_80044628_Floor`: the callback-local CollData previous
    # bottom is above the generated center platform and the current bottom is below it, so
    # `mpCheckFloorRemap`/`mpLineIntersectionH` publishes line 2 and `mpColl_80044838_Floor` snaps
    # LandingFallSpecial to y=42.7501. The row set includes sustained EscapeAir and
    # JumpAerial-entered EscapeAir callbacks; both share the same source floor-sweep owner. This is
    # generated stage-transform + CollData endpoint ownership, not a Sheik/action exception.
    #
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=static_y,line_id=2)
    # reports/triage/newchar_sheik/fall_floor_probe_cso_318_p0/
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
    # refs/melee/src/melee/mp/mplib.c::{mpCheckFloorRemap,mpLineIntersectionH}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["char_id"][p]) == CHAR_SHEIK
    assert int(row["seed_t"]["stage_id"]) == STAGE_FOD
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) in (
        ACT_ESCAPE_AIR,
        ACT_JUMP_AERIAL_F,
        ACT_JUMP_AERIAL_B,
    )
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["animation_index"][p]) == SM_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 2

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize("record", [9669, 11255])
def test_jumpaerial_escapeair_static_platform_deeper_step_still_lands_feh(record: int) -> None:
    # Negative controls for the static-platform overstep guard: deeper Dream Land top-platform
    # crossings are ordinary EscapeAir_Coll floor publications.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        "FlippantEnchantedHorse.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

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
    # - FD hard-floor mutation uses the ordinary EscapeAir floor handoff, while the Yoshi hard-floor
    #   mutation remains on the shallow locked-owner path.
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
    assert int(out_fd["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(out_fd["on_ground"][p]) == 1

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
    ("dataset_rel", "start_record", "target_record", "p", "expected_ground_id"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            3357,
            3544,
            1,
            6,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            3942,
            4083,
            1,
            6,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            6725,
            6727,
            1,
            2,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "LawfulInsistentMeerkat.msl",
            3490,
            3492,
            1,
            2,
        ),
    ],
)
def test_rollout_jumpaerial_escapeair_preserves_x130_bottom_to_yoshi_ledge(
    dataset_rel: str, start_record: int, target_record: int, p: int, expected_ground_id: int
) -> None:
    # Rollout-real positives for JumpAerial -> EscapeAir over Yoshi's sloped ledge floors.
    # The public one-step seed reconstructs the CollData_X130 desired-bottom lane, but free-run
    # rollout must also carry the live JumpAerial desired ECB bottom through the EscapeAir entry.
    # Once the following EscapeAir_Coll callback's preserved desired bottom crosses the generated
    # sloped ledge/static-platform floor, ft_80082C74/mpColl_800471F8 publishes
    # LandingFallSpecial. The adjacent fresh JumpAerial and shallow-remap controls above remain
    # airborne.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800471F8}
    # data/stages/bin/grst.bin::MSLSTG01 sloped ledge floor segments
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[target_record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) in (
        ACT_JUMP_AERIAL_F,
        ACT_ESCAPE_AIR,
    )
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == expected_ground_id

    out = _run_rollout_to_record(ds, start_record, target_record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
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
def test_marth_jumpaerial_escapeair_no_lock_flat_ledge_bottom_sweep_lands() -> None:
    # Marth no-lock JumpAerial -> EscapeAir over flat ledge floors:
    # JumpAerial_IASA can enter EscapeAir before Fighter_procMap, then EscapeAir_Coll's
    # ft_80082C74/mpColl_800471F8 path consumes the pre-entry JumpAerial prev ECB and the loaded
    # EscapeAir current ECB. Flat fighter-solid ledges/hard floors are real mpCheckFloor bottom-sweep
    # publications even when the carried seed floor.index is stale or names the adjacent center floor.
    # Generated sloped ledges remain covered by the negative/rollout controls below.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    # data/stages/bin/*.bin::MSLSTG01 fighter_solid/is_ledge/platform_transform metadata
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    cases = [
        ("InternalPowerlessWallaby.msl", 290, 0, 3),
        ("InternalPowerlessWallaby.msl", 4210, 0, 7),
        ("ParallelFamiliarZebra.msl", 7821, 0, 5),
        ("QuestionableHarmfulPanther.msl", 4388, 1, 5),
        ("WellWornSmallGoshawk.msl", 1605, 1, 3),
        ("WellWornSmallGoshawk.msl", 12063, 1, 3),
    ]

    for replay, record, p, expected_ground_id in cases:
        dataset_path = root / "datasets/marth/replays/validation/marth" / replay
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_path}")
        ds = read_dataset(str(dataset_path))
        row = ds.samples[record]
        assert int(row["seed_t"]["action_id"][p]) in (ACT_JUMP_AERIAL_F, ACT_ESCAPE_AIR)
        assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
        assert int(row["seed_t"]["ecb_lock_timer"][p]) == 0
        assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
        assert int(row["ref_t1"]["ground_id"][p]) == expected_ground_id

        out, dbg = _run_one_step_with_colldata(ds, record)
        ref = row["ref_t1"]
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
            assert int(out[field][p]) == int(ref[field][p]), (replay, record, field)
        assert int(dbg["floor_result_mode"][p]) == 1  # MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP.
        assert int(dbg["floor_result_segment_id"][p]) == expected_ground_id
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_jumpaerial_escapeair_fresh_static_platform_side_owner_lands_qhp() -> None:
    # Fresh JumpAerial -> EscapeAir can publish a static soft-platform floor through the entered
    # EscapeAir ECB side/root owner even when the ECB bottom point does not sweep downward across
    # the platform. This is same-callback entry ownership; sustained no-lock EscapeAir rows below
    # stay airborne until a bottom-sweep producer is live.
    #
    # data/motion_state/owners/*.bin::MSLMSO01 PHASE4_ESCAPE_AIR_COLL
    # data/stages/bin/grop.bin::MSLSTG01 Dream Land top platform
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/marth/replays/validation/marth/QuestionableHarmfulPanther.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 5432
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["ground_id"][p]) == 2

    out, dbg = _run_one_step_with_colldata(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert int(dbg["floor_result_mode"][p]) == 2  # MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION.
    assert int(dbg["floor_result_segment_id"][p]) == 2
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "FlippantEnchantedHorse.msl",
            2986,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "FlippantEnchantedHorse.msl",
            11156,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "FlippantEnchantedHorse.msl",
            11157,
            0,
        ),
        ("datasets/marth/replays/validation/marth/VigorousRelievedLlama.msl", 9073, 0),
    ],
)
def test_escapeair_static_platform_side_owner_requires_fresh_jumpaerial_entry(
    dataset_rel: str, record: int, p: int
) -> None:
    # Adjacent negatives for the QHP owner: sustained no-lock EscapeAir over static/moving
    # platforms is not admitted by the fresh-entry side/root publication path.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) in (ACT_JUMP_AERIAL_F, ACT_ESCAPE_AIR)
    assert int(row["seed_t"]["seed_prev_action_id"][p]) in (ACT_JUMP_AERIAL_F, ACT_ESCAPE_AIR)
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), (dataset_rel, record, field)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_marth_jumpaerial_escapeair_no_lock_sloped_ledge_entry_waits_for_next_callback() -> None:
    # Yoshi's generated sloped ledge strips are not flat floor publication points on the
    # JumpAerial -> EscapeAir entry callback when the carried floor still names the adjacent flat
    # floor. Vanilla leaves that remap row airborne, then the following EscapeAir_Coll callback lands
    # once the runtime floor producer is live. A same-slope carried floor is the positive control:
    # it can land on the entry callback.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    # data/stages/bin/grst.bin::MSLSTG01 sloped ledge floor segments
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/marth/replays/validation/marth/LoudDullGoat.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")
    ds = read_dataset(str(dataset_path))
    p = 1

    entry = ds.samples[3757]
    assert int(entry["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(entry["input_t"]["p"]["buttons"][p]) & (BUTTON_L | BUTTON_R)
    assert int(entry["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(entry["ref_t1"]["on_ground"][p]) == 0
    out, dbg = _run_one_step_with_colldata(ds, 3757)
    ref = entry["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert int(dbg["floor_result_segment_id"][p]) == 2
    assert int(dbg["floor_result_mode"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)

    follow = ds.samples[3758]
    assert int(follow["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(follow["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    rollout, rollout_dbg = _run_rollout_to_record_with_colldata(ds, 3757, 3758)
    ref = follow["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(rollout[field][p]) == int(ref[field][p]), field
    assert int(rollout_dbg["floor_result_mode"][p]) == 1
    assert int(rollout_dbg["floor_result_segment_id"][p]) == 2
    assert float(rollout["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)

    positive_path = root / "datasets/marth/replays/validation/marth/MetallicUniqueGrouse.msl"
    if not positive_path.exists():
        pytest.skip(f"missing local dataset: {positive_path}")
    positive_ds = read_dataset(str(positive_path))
    linked_slope_positives = [
        (947, 1, 2, 2),
        (1990, 0, 3, 6),
        (3967, 1, 3, 6),
    ]
    for record, port, seed_ground_id, expected_ground_id in linked_slope_positives:
        positive = positive_ds.samples[record]
        assert int(positive["seed_t"]["action_id"][port]) == ACT_JUMP_AERIAL_F
        assert int(positive["seed_t"]["ground_id"][port]) == seed_ground_id
        assert int(positive["ref_t1"]["action_id"][port]) == ACT_LANDING_FALL_SPECIAL
        assert int(positive["ref_t1"]["ground_id"][port]) == expected_ground_id
        out, dbg = _run_one_step_with_colldata(positive_ds, record)
        ref = positive["ref_t1"]
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
            assert int(out[field][port]) == int(ref[field][port]), (record, field)
        assert int(dbg["floor_result_segment_id"][port]) == expected_ground_id
        assert float(out["pos_y"][port]) == pytest.approx(float(ref["pos_y"][port]), abs=2e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("start_record", "producer_record", "target_record", "p"),
    [
        (1405, 1894, 1895, 0),
        (1895, 2233, 2234, 1),
    ],
)
def test_rollout_escapeair_live_floor_producer_authority_lands_on_carried_hard_floor(
    start_record: int, producer_record: int, target_record: int, p: int
) -> None:
    # Rollout-only live authority for sustained JumpAerial -> EscapeAir hard-floor handoff:
    # the direct one-step seed can restore CollData.floor and desired ECB, but the free-running
    # prefix must carry a source-owned floor authority lane from the preceding live callback. Some
    # rows carry the EscapeAir runtime floor producer plus CollData_X130 desired bottom; others keep
    # the generic floor-probe owner only. The following EscapeAir_Coll callback may then publish
    # LandingFallSpecial on the same carried non-platform floor. This protects the PPA 236/236/43
    # cluster without using visible ground_id/root crossing as authority for teacher-forced rows.
    #
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
    producer_row = ds.samples[producer_record]
    assert int(producer_row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(producer_row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(producer_row["ref_t1"]["on_ground"][p]) == 0

    producer_out, producer_dbg = _run_rollout_to_record_with_colldata(
        ds, start_record, producer_record
    )
    assert int(producer_out["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(producer_out["on_ground"][p]) == 0
    assert (
        int(producer_dbg["escapeair_floor_producer_runtime"][p]) == 1
        or int(producer_dbg["floor_probe_owner"][p]) != 0
    )

    row = ds.samples[target_record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 1

    out, dbg = _run_rollout_to_record_with_colldata(ds, start_record, target_record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert int(dbg["escapeair_floor_producer_runtime"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            156,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "MediumVirtualPig.msl",
            3104,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            2444,
            1,
        ),
    ],
)
def test_escapeair_restored_floor_state_without_live_producer_stays_airborne(
    dataset_rel: str, record: int, p: int
) -> None:
    # Adjacent stale-provenance negatives: restored CollData.floor, desired-bottom/root-crossing
    # geometry, or carried hard/ledge floor state from a direct seed is not runtime source
    # authority. Only the live EscapeAir producer lane above may admit the carried hard-floor
    # publication.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
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

    out, dbg = _run_one_step_with_colldata(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert int(dbg["escapeair_floor_producer_runtime"][p]) == 0


def test_escapeair_floor_producer_runtime_clears_on_jumpaerial_before_later_escapeair() -> None:
    # The runtime authority lane belongs to EscapeAir_Coll only. Even if debug tooling arms it
    # before a JumpAerial frame, the non-EscapeAir callback boundary must clear it before a later
    # air-dodge entry can observe it.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    colldata_stride = int(sizes["colldata_ecb"])
    colldata_dtype = _colldata_ecb_dtype()

    seed = _synthetic_air_seed(
        stage_id=STAGE_FD,
        action_id=ACT_JUMP_AERIAL_F,
        animation_index=SM_JUMP_AERIAL_F,
        x=0.0,
        y=12.0,
    )
    seed["ground_id"][0, 0] = np.uint16(1)
    neutral = _blank_input(input_stride)
    airdodge = _blank_input(input_stride)
    airdodge.view(INPUT_DTYPE).reshape((1,))["p"]["buttons"][0, 0] = np.uint16(BUTTON_L)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    colldata_bytes = np.empty((1, colldata_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=2,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(handle, seed.view("u1").reshape(1, seed_stride).copy())
        binding.debug_set_escapeair_floor_producer_runtime(
            handle, 0, 0, 1, ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_HARD_FLOOR
        )

        binding.step_input(handle, neutral, neutral)
        binding.debug_write_colldata_ecb(handle, colldata_bytes)
        after_jump = colldata_bytes.view(colldata_dtype).reshape((1,))[0].copy()
        assert int(after_jump["escapeair_floor_producer_runtime"][0]) == 0

        binding.step_input(handle, neutral, airdodge)
        binding.write_compare(handle, out_bytes)
        binding.debug_write_colldata_ecb(handle, colldata_bytes)
    finally:
        binding.destroy(handle)

    out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    after_escapeair = colldata_bytes.view(colldata_dtype).reshape((1,))[0]
    assert int(out["action_id"][0]) == ACT_ESCAPE_AIR
    assert int(after_escapeair["escapeair_floor_producer_runtime"][0]) == 0
    assert int(after_escapeair["desired_locked_owner"][0]) == 0


def test_escapeair_live_hard_floor_projection_publishes_sloped_line_normal() -> None:
    # The live carried hard-floor path is generic over fighter-solid non-platform floor lines.
    # When it publishes a sloped source line, the CollData floor result must carry that line's
    # normal rather than a flat fallback normal.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    # data/stages/bin/griz.bin::MSLSTG01 segment 4
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    colldata_stride = int(sizes["colldata_ecb"])
    colldata_dtype = _colldata_ecb_dtype()

    line = binding.stage_floor_segment(STAGE_FOD, 4)
    assert line is not None
    assert int(line["is_platform"]) == 0
    assert int(line["is_ledge"]) == 0
    assert int(line["fighter_solid"]) == 1
    x = (float(line["x0"]) + float(line["x1"])) * 0.5
    dx = float(line["x1"]) - float(line["x0"])
    dy = float(line["y1"]) - float(line["y0"])
    floor_y = float(line["y0"]) + (dy * ((x - float(line["x0"])) / dx))
    length = math.hypot(dx, dy)
    expected_nx = -dy / length
    expected_ny = dx / length
    assert abs(expected_nx) > 0.01
    assert expected_ny != pytest.approx(1.0)

    seed = _synthetic_air_seed(
        stage_id=STAGE_FOD,
        action_id=ACT_ESCAPE_AIR,
        animation_index=SM_ESCAPE_AIR,
        x=x,
        y=floor_y - 0.25,
    )
    seed["ground_id"][0, 0] = np.uint16(4)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_ESCAPE_AIR)
    seed["seed_prev_action_frame"][0, 0] = np.int16(4)
    seed["floor_sweep_prev_pos_x_f32"][0, 0] = np.float32(x)
    seed["floor_sweep_prev_pos_y_f32"][0, 0] = np.float32(floor_y + 1.0)
    seed["floor_sweep_prev_pos_valid_u8"][0, 0] = np.uint8(1)
    seed["ecb_lock_timer"][0, 0] = np.uint8(4)

    neutral = _blank_input(input_stride)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    colldata_bytes = np.empty((1, colldata_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=2,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed(handle, seed.view("u1").reshape(1, seed_stride).copy())
        binding.debug_set_escapeair_floor_producer_runtime(
            handle, 0, 0, 1, ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_HARD_FLOOR
        )
        binding.step_input(handle, neutral, neutral)
        binding.write_compare(handle, out_bytes)
        binding.debug_write_colldata_ecb(handle, colldata_bytes)
    finally:
        binding.destroy(handle)

    out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    dbg = colldata_bytes.view(colldata_dtype).reshape((1,))[0]
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == 4
    assert int(dbg["floor_result_segment_id"][0]) == 4
    assert float(dbg["floor_result_normal_x"][0]) == pytest.approx(expected_nx, abs=1e-6)
    assert float(dbg["floor_result_normal_y"][0]) == pytest.approx(expected_ny, abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("producer_record", "target_record", "expected_ground", "expected_seed_timer"),
    [(3038, 3039, 2, 1), (5299, 5304, 1, 5), (5587, 5589, 2, 1)],
)
def test_escapeair_terminal_locked_static_platform_sweep_lands_sds(
    producer_record: int, target_record: int, expected_ground: int, expected_seed_timer: int
) -> None:
    # CollData_X130 EscapeAir_Coll static-platform source owner:
    # the producer row enters/continues EscapeAir with a live locked-bottom CollData packet. Source
    # `ft_80082C74 -> mpColl_800471F8` consumes the callback-local previous root and the preserved
    # desired_ecb.bottom through `mpColl_80044628_Floor`. This admits Dream Land static platforms
    # only when that source bottom packet is present and actually sweeps the platform: 5304 starts
    # at the aerial Reflector loop IASA producer, where source `ftCo_800CB870` routes through
    # `ftCo_JumpAerial_Enter_Basic`, so the test proves live JumpAerial ownership instead of
    # relying on replay-seeded CollData_X130, while 3039/5589 are terminal locked callbacks.
    #
    # refs/melee/src/melee/ft/fighter.c::Fighter_procMap
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_LoadECB_inline,
    #   mpColl_800471F8,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        "ShadyDecimalStarling.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[target_record]
    p = 1
    assert int(ds.samples["seed_t"][producer_record]["action_id"][p]) in (
        ACT_FX_SPECIAL_AIR_LW_LOOP,
        ACT_JUMP_AERIAL_F,
        ACT_ESCAPE_AIR,
    )
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == expected_seed_timer
    assert int(row["seed_t"]["ecb_lock_bottom_rel_y_valid_u8"][p]) == 1
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["ground_id"][p]) == expected_ground

    out, dbg = _run_rollout_to_record_with_colldata(ds, producer_record, target_record)
    ref = row["ref_t1"]

    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert int(dbg["floor_result_valid"][p]) == 1
    assert int(dbg["floor_result_mode"][p]) != 0
    assert int(dbg["floor_result_segment_id"][p]) == expected_ground


@pytest.mark.integration
@pytest.mark.parametrize(("producer_record", "target_record"), [(3038, 3039), (5303, 5304)])
def test_escapeair_static_platform_needs_locked_bottom_source_owner_sds(
    producer_record: int, target_record: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        "ShadyDecimalStarling.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 1
    assert int(ds.samples["seed_t"][target_record]["action_id"][p]) == ACT_ESCAPE_AIR

    def clear_locked_bottom(seed_t) -> None:
        seed_t["ecb_lock_bottom_rel_y_valid_u8"][0, p] = np.uint8(0)

    out = _run_rollout_to_record_with_seed_mutator(
        ds, producer_record, target_record, seed_mutator=clear_locked_bottom
    )

    assert int(out["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out["on_ground"][p]) == 0


@pytest.mark.integration
def test_escapeair_start_to_loop_shine_jump_preserves_bottom_without_early_platform_publish_cnm() -> None:
    # Aerial Reflector Start can finish in `ftFx_SpecialAirLwStart_Anim`, enter Loop, then have
    # Loop IASA immediately call `ftCo_800CB870` into JumpAerial before the map callback. The
    # resulting CollData_X130 desired-bottom packet is real source state, but EscapeAir still cannot
    # publish the carried platform until the source bottom-sweep phase reaches it.
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialAirLwStart_Anim,ftFx_SpecialAirLwLoop_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044628_Floor}
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
    p = 1
    assert int(ds.samples["seed_t"][2440]["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START
    assert int(ds.samples["ref_t1"][2441]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(ds.samples["seed_t"][2444]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(ds.samples["seed_t"][2444]["ecb_lock_bottom_rel_y_valid_u8"][p]) == 1
    assert int(ds.samples["ref_t1"][2444]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(ds.samples["ref_t1"][2447]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL

    early_out, early_dbg = _run_rollout_to_record_with_colldata(ds, 2440, 2444)
    assert int(early_out["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(early_out["on_ground"][p]) == 0
    assert int(early_dbg["floor_result_mode"][p]) == 0

    late_out, late_dbg = _run_rollout_to_record_with_colldata(ds, 2440, 2447)
    assert int(late_out["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(late_out["on_ground"][p]) == 1
    assert int(late_dbg["floor_result_valid"][p]) == 1


@pytest.mark.integration
def test_yoshi_jumpaerial_escapeair_uses_frame_start_last_pos_for_platform_landing() -> None:
    # Yoshi static-platform positive for a later JumpAerialB -> EscapeAir entry:
    # IASA changes to EscapeAir before Fighter_procMap, then ft_80082C74 calls mpCollPrev in the
    # entered EscapeAir collision callback. Source therefore sweeps from the frame-start JumpAerial
    # CollData root/ECB to the post-Phys EscapeAir root even though the public seed's
    # floor_sweep_prev_pos lane points at the prior replay row. The sustained EscapeAir controls
    # below remain airborne and prevent this owner from becoming a generic platform snap.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8,mpColl_80044628_Floor}
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
    record = 2717
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_B
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_B
    assert int(row["seed_t"]["seed_prev_action_frame"][p]) == 7
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 4

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
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "expected_ground_id"),
    [
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            9345,
            0,
            35,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "MilkyGracefulStingray.msl",
            4325,
            0,
            2,
        ),
    ],
)
def test_no_lock_sustained_escapeair_platform_contact_lands(
    dataset_rel: str, record: int, p: int, expected_ground_id: int
) -> None:
    # Once CollData_X130_Locked has cleared, sustained EscapeAir floor contact is the ordinary
    # ft_80082C74/mpColl_800471F8 owner. Do not keep the old no-lock vertical-frame bridge airborne:
    # the source callback consumes static soft-platform crossings into LandingFallSpecial, including
    # FoD's static top platform after the explicit current-EscapeAir ECB bottom sweep crosses it.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == expected_ground_id

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
def test_fod_locked_escapeair_platform_bottom_sweep_lands_after_lock_phase() -> None:
    # FoD platform positives for EscapeAir_Coll's locked bottom-sweep owner:
    # CollData_X130_Locked can preserve a positive previous/desired bottom even when the visible
    # EscapeAir pose bottom is zero. Once mpColl_80044628_Floor accepts that callback-local platform
    # sweep, mpColl_80044838_Floor(ignore_bottom=true) projects from the root into
    # LandingFallSpecial. EWT:941 is the adjacent transformed side-platform lock-phase negative;
    # EWT:942 is the first side-platform publication frame, while PTE:6998 covers static center
    # platform publication.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    cases = (
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            942,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            6998,
            1,
        ),
    )
    for dataset_rel, record, p in cases:
        dataset_path = root / dataset_rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_rel}")
        ds = read_dataset(str(dataset_path))
        row = ds.samples[record]
        assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
        assert int(row["seed_t"]["ecb_lock_timer"][p]) != 0
        assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
        assert int(row["ref_t1"]["on_ground"][p]) == 1

        out = _run_one_step(ds, record)
        ref = row["ref_t1"]
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
            assert int(out[field][p]) == int(ref[field][p]), (record, field)
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_fod_locked_escapeair_side_platform_timer3_stays_airborne() -> None:
    # Negative boundary for the retained FoD side-platform owner: one frame earlier than the EWT:942
    # positive, CollData_X130_Locked is still in the transformed-platform interpolation gap and
    # source keeps EscapeAir airborne.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    record = 941
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 3
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_rollout_escapeair_timer3_does_not_reuse_jumpaerial_current_bottom() -> None:
    # Rollout boundary for the same transformed side-platform slice as the direct EWT:941 negative:
    # after the first EscapeAir entry callback has run, source keeps only CollData.desired_ecb.bottom
    # locked. The current ECB bottom is reloaded/interpolated from the sustained EscapeAir pose, so
    # a free rollout must not keep the earlier JumpAerial desired bottom as current ECB authority and
    # publish the timer-3 floor contact one frame early.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    start_record = 867
    target_record = 941
    p = 1
    target = ds.samples[target_record]
    assert int(target["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(target["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(target["seed_t"]["ecb_lock_timer"][p]) == 3
    assert int(target["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(target["ref_t1"]["on_ground"][p]) == 0

    out = _run_rollout_to_record(ds, start_record, target_record)
    ref = target["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_sustained_escapeair_ledge_span_crossing_lands_without_endpoint_entry() -> None:
    # Sustained EscapeAir over FoD's side ledge floors uses the same
    # ft_80082C74/mpColl_800471F8 locked floor owner as hard-floor continuations, but it is a
    # callback-local in-span floor crossing, not an endpoint catch. EWT:2683 has both previous and
    # current roots inside the left ledge span and lands; EWT:6442 enters the mirrored right ledge
    # span from outside its endpoint and stays airborne.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    p = 1

    positive = ds.samples[2683]
    assert int(positive["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(positive["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(positive["seed_t"]["ecb_lock_timer"][p]) == 3
    assert int(positive["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(positive["ref_t1"]["ground_id"][p]) == 3
    out_positive = _run_one_step(ds, 2683)
    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out_positive[field][p]) == int(positive["ref_t1"][field][p]), field
    assert float(out_positive["pos_y"][p]) == pytest.approx(
        float(positive["ref_t1"]["pos_y"][p]), abs=2e-4
    )

    negative = ds.samples[6442]
    assert int(negative["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(negative["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(negative["seed_t"]["ecb_lock_timer"][p]) == 3
    assert int(negative["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(negative["ref_t1"]["on_ground"][p]) == 0
    out_negative = _run_one_step(ds, 6442)
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out_negative[field][p]) == int(negative["ref_t1"][field][p]), field
    assert float(out_negative["pos_y"][p]) == pytest.approx(
        float(negative["ref_t1"]["pos_y"][p]), abs=1e-6
    )


@pytest.mark.integration
def test_fod_locked_escapeair_side_platform_timer2_lands_after_rollout_carry() -> None:
    # Rollout boundary for the same FoD side-platform owner as the timer-3 negative above:
    # direct EWT:942 reseeds expose CollData_X130_Locked desired.bottom through the seed lane, but a
    # free rollout from EWT:941 must preserve that source CollData desired ECB into the next
    # EscapeAir_Coll callback. Timer 3 remains in the transformed-platform interpolation gap; timer
    # 2 is the first source-owned `ft_80082C74 -> mpColl_800471F8` LandingFallSpecial publication.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_UnlockECB}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    start_record = 941
    target_record = 942
    p = 1
    row_start = ds.samples[start_record]
    row_target = ds.samples[target_record]
    assert int(row_start["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row_start["seed_t"]["ecb_lock_timer"][p]) == 3
    assert int(row_target["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row_target["seed_t"]["ecb_lock_timer"][p]) == 2
    assert int(row_target["seed_t"]["ecb_lock_bottom_rel_y_valid_u8"][p]) == 1
    assert int(row_target["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row_target["ref_t1"]["on_ground"][p]) == 1

    out = _run_rollout_to_record(ds, start_record, target_record)
    ref = row_target["ref_t1"]
    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_fod_jumpaerial_escapeair_offspan_platform_carries_desired_bottom_to_landing() -> None:
    # Late JumpAerial -> EscapeAir on a FoD height-transform platform can leave the platform's
    # horizontal span before the first EscapeAir_Coll floor publication. Source still carries the
    # JumpAerial CollData_X130 desired bottom through the EscapeAir entry, so the following callback
    # can land on the adjacent hard floor.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{ftCo_80099A58,ftCo_EscapeAir_Coll}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    start_record = 3762
    entry_record = 3905
    target_record = 3907
    p = 1
    entry = ds.samples[entry_record]
    target = ds.samples[target_record]
    assert int(entry["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(entry["seed_t"]["ground_id"][p]) == 2
    assert int(entry["seed_t"]["ecb_lock_bottom_rel_y_valid_u8"][p]) == 1
    assert int(target["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL

    out = _run_rollout_to_record(ds, start_record, target_record)
    ref = target["ref_t1"]
    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_fod_jumpaerial_escapeair_desired_bottom_carry_requires_source_floor() -> None:
    # Negative boundary for the off-span transformed-platform owner: without a carried source floor
    # id, EscapeAir entry must not invent a CollData desired-bottom lifetime from the replay row.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    record = 3905
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["ground_id"][p]) == 2

    _out, colldata = _run_one_step_with_colldata(
        ds,
        record,
        seed_mutator=lambda seed: seed["ground_id"].__setitem__((0, p), np.uint16(0xFFFF)),
    )
    assert float(colldata["desired_bottom_rel_y"][p]) == pytest.approx(0.0, abs=1e-6)


@pytest.mark.integration
def test_fod_escapeair_seeded_desired_bottom_crossing_lands_on_hard_floor() -> None:
    # Replay-seeded owner-1 CollData desired-bottom rows use the explicit desired bottom for the
    # non-platform floor crossing, not the entered EscapeAir zero-bottom pose. The adjacent previous
    # frame has not crossed the hard floor yet and stays airborne.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800471F8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    p = 1

    previous = ds.samples[4056]
    assert int(previous["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    out_prev = _run_one_step(ds, 4056)
    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out_prev[field][p]) == int(previous["ref_t1"][field][p]), field

    row = ds.samples[4057]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["ecb_lock_bottom_rel_y_valid_u8"][p]) == 1
    assert int(row["seed_t"]["ground_id"][p]) == 3
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    out = _run_one_step(ds, 4057)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_fod_escapeair_rollout_ed5c_floor_corner_bottom_crossing_lands() -> None:
    # Rollout-positive lock for the same FoD hard-floor handoff after a small wall-projection drift.
    # Source EscapeAir_Coll still consumes the carried non-platform floor.index and the
    # CollData_X130_Locked bottom sweep through mpCheckFloor/mpColl_80044628_Floor. The previous
    # frame remains airborne, then the next callback publishes LandingFallSpecial on that hard floor.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    # refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004ED5C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    p = 1

    out_prev = _run_rollout_to_record(ds, 3762, 4056)
    ref_prev = ds.samples[4056]["ref_t1"]
    assert int(out_prev["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_prev["on_ground"][p]) == 0
    assert int(out_prev["action_id"][p]) == int(ref_prev["action_id"][p])
    assert int(out_prev["on_ground"][p]) == int(ref_prev["on_ground"][p])
    assert int(out_prev["ground_id"][p]) == 3

    out = _run_rollout_to_record(ds, 3762, 4057)
    ref = ds.samples[4057]["ref_t1"]
    for field in ("action_id", "animation_index", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_locked_escapeair_seed6_different_platform_root_projection_stays_airborne() -> None:
    # The ecb_lock_timer=6 interpolation-gap owner also applies when root projection can see a
    # different static soft platform than the carried CollData.floor.index. Source has not reached the
    # later ft_80082C74/mpColl_800471F8 platform publication phase yet, so this first projected
    # platform candidate remains airborne.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_UnlockECB}
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80043754}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/CornyDelayedOkapi.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 2838
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 6
    assert int(row["seed_t"]["ground_id"][p]) != 35
    assert int(row["ref_t1"]["ground_id"][p]) != 35
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
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            2170,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "ShadyDecimalStarling.msl",
            571,
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
def test_kneebend_escapeair_platform_handoff_requires_downward_entry_step() -> None:
    # Fresh KneeBend -> Jump -> EscapeAir reaches EscapeAir_Coll in the same callback.
    # Vanilla only publishes the same-platform root projection when the entered EscapeAir step is
    # actually descending into the platform; a horizontal airdodge already resting on Dream Land's
    # elevated platform remains airborne.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    negative_path = (
        root / "datasets/marth/replays/validation/marth/QuestionableHarmfulPanther.msl"
    )
    if not negative_path.exists():
        pytest.skip(f"missing local dataset: {negative_path}")
    negative_ds = read_dataset(str(negative_path))
    negative = negative_ds.samples[90]
    p = 0
    assert int(negative["seed_t"]["action_id"][p]) == ACT_KNEE_BEND
    assert float(negative["ref_t1"]["speed_y_self"][p]) == pytest.approx(0.0, abs=1e-7)
    assert int(negative["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(negative["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(negative_ds, 90)
    ref = negative["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    for record in (91, 3900):
        row = negative_ds.samples[record]
        assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
        assert float(row["ref_t1"]["speed_y_self"][p]) == pytest.approx(0.0, abs=1e-7)
        assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
        assert int(row["ref_t1"]["on_ground"][p]) == 0

        out = _run_one_step(negative_ds, record)
        ref = row["ref_t1"]
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
            assert int(out[field][p]) == int(ref[field][p]), (record, field)
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    positive_cases = (
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            2170,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "ShadyDecimalStarling.msl",
            571,
            1,
        ),
    )
    for dataset_rel, record, player in positive_cases:
        dataset_path = root / dataset_rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_rel}")
        ds = read_dataset(str(dataset_path))
        row = ds.samples[record]
        assert int(row["seed_t"]["action_id"][player]) == ACT_KNEE_BEND
        assert float(row["ref_t1"]["speed_y_self"][player]) < 0.0
        assert int(row["ref_t1"]["action_id"][player]) == ACT_LANDING_FALL_SPECIAL
        assert int(row["ref_t1"]["on_ground"][player]) == 1

        out = _run_one_step(ds, record)
        ref = row["ref_t1"]
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
            assert int(out[field][player]) == int(ref[field][player]), (dataset_rel, field)


@pytest.mark.integration
def test_kneebend_escapeair_platform_handoff_requires_escapeair_entry() -> None:
    # Boundary control for the fresh KneeBend -> Jump -> EscapeAir platform handoff: without the
    # L/R edge, KneeBend_Anim still enters Jump, but Jump_IASA does not enter EscapeAir and
    # EscapeAir_Coll cannot publish LandingFallSpecial on the same callback.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    def clear_lr_edge(_prev_input: np.ndarray, input_t: np.ndarray) -> None:
        for player_i in range(input_t["p"].shape[1]):
            input_t["p"][0, player_i]["buttons"] = np.uint16(
                int(input_t["p"][0, player_i]["buttons"]) & (0xFFFF ^ (BUTTON_L | BUTTON_R))
            )

    for dataset_rel, record, p in (
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            2170,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "ShadyDecimalStarling.msl",
            571,
            1,
        ),
    ):
        dataset_path = root / dataset_rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_rel}")

        ds = read_dataset(str(dataset_path))
        row = ds.samples[record]
        assert int(row["seed_t"]["action_id"][p]) == ACT_KNEE_BEND
        assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL

        out = _run_one_step(ds, record, input_mutator=clear_lr_edge)
        assert int(out["action_id"][p]) in (ACT_JUMP_F, ACT_JUMP_B)
        assert int(out["on_ground"][p]) == 0


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
            4947,
            0,
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
    # accepted platform line or the preserved locked desired-bottom seed is the zero-bottom handoff.
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
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


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
def test_jumpaerial_attackair_entry_floor_requires_live_bottom_owner() -> None:
    # Replay-real lock for the adjacent no-owner boundary to the source-completion
    # JumpAerial -> AttackAir floor bottom-sweep owner:
    # - WWS:679 enters AttackAirB from JumpAerial_IASA, but vanilla stays airborne because both
    #   callback ECB-bottom endpoints are already below Yoshi's left platform; no current
    #   `mpColl_80044628_Floor` bottom crossing exists for the entered AttackAir callback.
    # - HIS:3623 is a nearby JumpAerial platform landing with no AttackAir entry; it must keep the
    #   ordinary JumpAerial_Coll landing path.
    #
    # data/stages/bin/grst.bin::MSLSTG01 platform/fighter_solid metadata
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{
    #   ftCo_JumpAerial_IASA,ftCo_JumpAerial_Coll}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_800835B0}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    wws_rel = "datasets/aggregate_recent/replays/validation/marth/WellWornSmallGoshawk.msl"
    wws_path = root / wws_rel
    if not wws_path.exists():
        pytest.skip(f"missing local dataset: {wws_rel}")
    wws = read_dataset(str(wws_path))
    wws_record = 679
    p = 0
    wws_row = wws.samples[wws_record]
    assert int(wws_row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(wws_row["ref_t1"]["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(wws_row["ref_t1"]["on_ground"][p]) == 0

    out, dbg = _run_one_step_with_colldata(wws, wws_record)
    ref = wws_row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert int(dbg["floor_probe_valid"][p]) == 0
    assert int(dbg["floor_probe_raw_bottom_sweep_hit"][p]) == 0
    assert int(dbg["floor_probe_projection_hit"][p]) == 0
    assert int(dbg["floor_result_valid"][p]) == 0
    source_prev_bottom_y = float(dbg["prev_bottom_rel_y"][p]) + float(
        dbg["floor_sweep_prev_pos_y"][p]
    )
    source_current_bottom_y = float(dbg["prev_bottom_rel_y"][p]) + float(ref["pos_y"][p])
    contact_y = float(dbg["floor_result_contact_y"][p])
    assert source_prev_bottom_y < contact_y
    assert source_current_bottom_y < contact_y

    his_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl"
    his_path = root / his_rel
    if not his_path.exists():
        pytest.skip(f"missing local dataset: {his_rel}")
    his = read_dataset(str(his_path))
    his_record = 3623
    his_row = his.samples[his_record]
    assert int(his_row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(his_row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(his_row["ref_t1"]["on_ground"][p]) == 1

    his_out, his_dbg = _run_one_step_with_colldata(his, his_record)
    his_ref = his_row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(his_out[field][p]) == int(his_ref[field][p]), field
    assert int(his_dbg["floor_result_valid"][p]) == 1


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "p", "expected_action", "expected_on_ground", "expected_ground_id", "raw_stick_y"),
    [
        (1403, 0, ACT_JUMP_AERIAL_F, 0, 3, -58),
        (1616, 1, ACT_LANDING, 1, 4, 0),
    ],
)
def test_jumpaerial_static_platform_pass_callback_boundary(
    record: int,
    p: int,
    expected_action: int,
    expected_on_ground: int,
    expected_ground_id: int,
    raw_stick_y: int,
) -> None:
    # Sustained JumpAerial_Coll routes through ft_800835B0 -> mpColl_80047E14. Source floor
    # projection first requires mpColl_80044628_Floor to accept ftCo_80096CC8's platform callback:
    # held-down platform pass stays airborne, while released/no-pass rows keep the ordinary platform
    # Landing path.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
    # refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "CheeryNumbMonkey.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    ucf_slot = int(row["seed_t"]["ucf_padbuf_index"][p]) & 3
    assert int(row["seed_t"]["ucf_padbuf_stick_y"][p][ucf_slot]) == raw_stick_y
    assert int(row["ref_t1"]["action_id"][p]) == expected_action
    assert int(row["ref_t1"]["on_ground"][p]) == expected_on_ground
    assert int(row["ref_t1"]["ground_id"][p]) == expected_ground_id

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    for field in ("pos_x", "pos_y", "speed_air_x_self", "speed_y_self"):
        assert float(out[field][p]) == pytest.approx(float(ref[field][p]), abs=1e-6), field


@pytest.mark.integration
def test_fod_jumpaerial_fastfall_transformed_platform_suppression_restores_root_y() -> None:
    # Sustained JumpAerial_Coll over FoD height-transform platforms can reject the final platform
    # publication while remaining airborne. The suppression must restore callback root Y as well as
    # on_ground/ground_id; otherwise replay-real rows stay airborne but keep a platform-snapped root.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 1333
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_B
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_B
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert int(row["ref_t1"]["action_id"][p]) == ACT_JUMP_AERIAL_B
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "expected_ground_id"),
    [
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "MilkyGracefulStingray.msl",
            1341,
            0,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            6748,
            1,
            1,
        ),
    ],
)
def test_fod_jumpaerial_fastfall_fresh_platform_floor_contact_lands(
    dataset_rel: str, record: int, p: int, expected_ground_id: int
) -> None:
    # Positive controls for the JumpAerial_Coll height-platform split. The stay-airborne owner above
    # requires a carried generated platform floor.index; rows starting from a hard-floor index and
    # crossing onto FoD's side platforms publish ordinary Landing through ft_800835B0.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80084DB0,ft_800835B0}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["ground_id"][p]) == 5
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row["ref_t1"]["ground_id"][p]) == expected_ground_id

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    # The source owner under test is the landing publication boundary; existing FoD platform
    # projection leaves the familiar mpLib 0.0001 floor-bias residual on this path.
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_fod_jumpaerial_fastfall_same_step_platform_contact_lands_pte_2026() -> None:
    # PTE:2026 starts from a carried FoD platform floor id, but grIzumi has published a
    # same-step height-platform contact source for the left platform. That is current-frame
    # platform authority, not stale carried floor state, so JumpAerial_Coll can publish Landing.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
    # refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
    # data/stages/bin/griz.bin::MSLSTG01 height platform transforms
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 2026
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_B
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_B
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert int(row["seed_t"]["ground_id"][p]) == 0
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][1]) & 0x04
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("start_record", "target_record", "p", "expected_seed_action"),
    [
        (1263, 1395, 1, ACT_FALL),
        (1835, 2026, 0, ACT_JUMP_AERIAL_B),
    ],
)
def test_fod_fall_root_crossing_height_platform_lands_in_rollout(
    start_record: int, target_record: int, p: int, expected_seed_action: int
) -> None:
    # Rollout boundary for the same Fall_Coll / JumpAerial_Coll transformed-platform owner as the
    # direct same-step locks above. The replay seed's same-step platform source bit is transient,
    # but when the callback root crosses FoD's generated height-platform line, source
    # mpColl_80044838_Floor can publish Landing from that current crossing instead of treating the
    # contact as stale carried platform state.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044838_Floor}
    # data/stages/bin/griz.bin::MSLSTG01 height platform transforms
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[target_record]
    assert int(row["seed_t"]["action_id"][p]) == expected_seed_action
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 0

    out = _run_rollout_to_record(ds, start_record, target_record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


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
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_terminal_jump_anim_fall_uses_transformed_platform_lifetime_pte_1455() -> None:
    # JumpF/B terminal Anim ownership enters Fall before the current map callback. On FoD height
    # platforms this uses the same Fall_Coll transformed-platform lifetime split as sustained Fall:
    # the first terminal Jump -> Fall callback can remain airborne instead of publishing Landing
    # from stale carried platform floor state.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_800835B0}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 1455
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_F
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_F
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert int(row["ref_t1"]["action_id"][p]) == ACT_FALL
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fall_transformed_platform_fastfall_gate_is_source_scoped() -> None:
    # Synthetic negative using a retained FoD row:
    # - breaking the sustained same-action Fall_Coll lifetime exits the fastfall transformed-platform
    #   owner and admits the same platform contact. Clearing fp->fall_fast alone reduces the Fall
    #   descent on current v7 generated seeds, so it no longer isolates this contact row.
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
    # Use the last retained airborne Fall frame before the platform publishes Landing. With the
    # same-action lifetime broken, the row is no longer owned by sustained Fall_Coll and lands.
    record = 5960
    p = 1

    def not_sustained_fall(seed: np.ndarray) -> None:
        seed["seed_prev_action_id"][0, p] = np.uint16(ACT_JUMP_AERIAL_F)

    out_not_sustained_fall = _run_one_step(ds, record, seed_mutator=not_sustained_fall)
    assert int(out_not_sustained_fall["action_id"][p]) == ACT_LANDING
    assert int(out_not_sustained_fall["on_ground"][p]) == 1


@pytest.mark.integration
def test_fod_fall_loop_wrap_delays_transformed_platform_hard_floor_publish() -> None:
    # Replay-real boundary for a sustained fastfall Fall_Coll row immediately after the Fall AObj
    # loops. The carried CollData floor.index is still FoD's height-transformed platform, and source
    # keeps the loop-wrap callback airborne before publishing the hard main-floor landing on the
    # following callback. This is distinct from the broader transformed-platform platform-contact
    # suppression above; the next row below proves the hard-floor handoff still lands normally.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Anim,ftCo_Fall_Coll}
    # refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBE8
    # refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    p = 1
    loop_row = ds.samples[5960]
    assert int(loop_row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(loop_row["seed_t"]["seed_prev_action_id"][p]) == ACT_FALL
    assert int(loop_row["seed_t"]["seed_prev_action_frame"][p]) == 7
    assert int(loop_row["seed_t"]["action_frame"][p]) == 0
    assert int(loop_row["seed_t"]["ground_id"][p]) == 2
    assert int(loop_row["seed_t"]["fall_fast"][p]) == 1
    assert int(loop_row["seed_t"]["state_flags"][p, 0]) & 0x80 == 0
    assert int(loop_row["ref_t1"]["action_id"][p]) == ACT_FALL
    assert int(loop_row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, 5960)
    ref = loop_row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    landing_row = ds.samples[5961]
    assert int(landing_row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(landing_row["seed_t"]["seed_prev_action_frame"][p]) == 0
    assert int(landing_row["ref_t1"]["action_id"][p]) == ACT_LANDING
    out_landing = _run_one_step(ds, 5961)
    ref_landing = landing_row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out_landing[field][p]) == int(ref_landing[field][p]), field
    assert float(out_landing["pos_y"][p]) == pytest.approx(float(ref_landing["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_fall_loop_wrap_already_below_hard_floor_lands() -> None:
    # Positive boundary for the loop-wrap owner above: once the sustained Fall_Coll callback-local
    # root is already below the hard floor, source has moved past the first transformed-platform
    # floor-index handoff gap and publishes Landing through the ordinary ft_800831CC/mpColl floor
    # result. This prevents the loop-wrap guard from suppressing the following below-floor callback.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Anim,ftCo_Fall_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
    record = 4354
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_FALL
    assert int(row["seed_t"]["seed_prev_action_frame"][p]) == 7
    assert int(row["seed_t"]["action_frame"][p]) == 0
    assert int(row["seed_t"]["ground_id"][p]) == 2
    assert int(row["seed_t"]["fall_fast"][p]) == 1
    assert float(row["seed_t"]["floor_sweep_prev_pos_y_f32"][p]) < float(row["ref_t1"]["pos_y"][p])
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 5

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_nonfastfall_fall_generic_ecb_lock_does_not_stale_lift_wall_envelope() -> None:
    # Replay-real rollout boundary for ordinary Fall near FD's left lip. A stale generic
    # desired-bottom owner raises the current ECB bottom high enough that mpColl_80045B74 misses the
    # current bottom->right wall edge, delaying CliffCatch. Non-fastfall Fall is outside the
    # retained locked-bottom consumer slice; fastfall/transformed-platform Fall coverage above
    # remains the positive owner.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80045B74_LeftWall}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "ImpassionedAlarmedTarsier.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    target_record = 11572
    p = 1
    row = ds.samples[target_record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["seed_t"]["fall_fast"][p]) == 0
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == 252  # CliffCatch

    out, mismatch = _rollout_first_mismatch_through(ds, target_record)
    assert mismatch is None
    assert int(out["action_id"][p]) == 252
    assert float(out["pos_x"][p]) == pytest.approx(float(row["ref_t1"]["pos_x"][p]), abs=1e-6)


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
def test_ground_jump_uses_self_vel_not_gr_vel_on_yoshi_slope_cnm_1268() -> None:
    # Ground jump launch scales fp->self_vel.x in ftCo_800CB110. On Yoshi's sloped floor the
    # replay-visible self_vel.x and gr_vel differ; using gr_vel adds extra horizontal launch speed
    # that later cascades into a false shield contact.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB110
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
    record = 1268
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_KNEE_BEND
    assert int(row["ref_t1"]["action_id"][p]) == ACT_JUMP_B
    assert int(row["seed_t"]["ground_id"][p]) == 6
    assert abs(
        float(row["seed_t"]["speed_ground_x_self"][p])
        - float(row["seed_t"]["speed_air_x_self"][p])
    ) > 0.02

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=1e-6
    )
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)


@pytest.mark.integration
def test_ground_jump_launch_does_not_borrow_slope_gr_vel_cnm_1268() -> None:
    # Boundary control for the same source owner: if the self_vel.x lane is overwritten with the
    # slope gr_vel value, the launch reproduces the rejected too-fast X speed. This locks the lane
    # distinction instead of merely accepting the motivating row.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB110
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
    record = 1268
    p = 0
    row = ds.samples[record]

    def replace_self_vel_with_gr_vel(seed_t: np.ndarray) -> None:
        seed_t["speed_air_x_self"][0, p] = seed_t["speed_ground_x_self"][0, p]

    out = _run_one_step(ds, record, seed_mutator=replace_self_vel_with_gr_vel)
    ref = row["ref_t1"]
    assert int(out["action_id"][p]) == ACT_JUMP_B
    assert float(out["speed_air_x_self"][p]) - float(ref["speed_air_x_self"][p]) > 0.02


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


@pytest.mark.integration
def test_fod_fall_coll_floor_skip_seed_carries_transformed_platform_pass(tmp_path: Path) -> None:
    # FoD Fall_Coll floor-skip seed owner:
    # - Fall_Coll routes through ft_800831CC with ftCo_80096CC8, the same soft-platform predicate
    #   used by Jump/JumpAerial. A down-held transformed-platform crossing writes hidden
    #   CollData.floor_skip, and the x470 pass-through lifetime carries it after the stick is
    #   released.
    # - EWT:5954 is a direct replay seed in that carry window. Without the Fall_Coll derivation the
    #   sim applies a stay-airborne platform Y correction to FoD's left platform; with the source
    #   skip lane it stays at vanilla Fall root Y.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp = root / "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slpz"
    if not slp.exists():
        pytest.skip(f"missing local replay: {slp}")

    out_path = tmp_path / "ElatedWearyTermite.msl"
    _main_impl(
        Namespace(
            slp=str(slp),
            out=str(out_path),
            ports=None,
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
    )
    ds = read_dataset(str(out_path))
    record = 5954
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["seed_t"]["floor_skip_segment_id_u16"][p]) == 1
    assert int(row["seed_t"]["floor_skip_segment_valid_u8"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_FALL
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 2
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-6)


@pytest.mark.integration
def test_fod_fall_coll_flags6_root_projection_lands_source_trusted_floors() -> None:
    # Common Fall_Coll floor publication:
    # - Fall_Coll routes through ft_800831CC -> mpColl_80047F40(flags=6), which uses
    #   ftCo_80096CC8 for platform admission and then mpColl_80044838_Floor can project from the
    #   callback root after a source floor result.
    # - FoD transformed side platforms use generated MSLSTG01 height state when the seed marks that
    #   height source-trusted; connected hard-floor handoffs use the generated floor graph.
    # - A previous-frame down input alone must not synthesize floor_skip when the callback-visible
    #   current stick has released above x25C.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047F40,mpColl_80044628_Floor,mpColl_80044838_Floor}
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    cases = (
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            1565,
            0,
            5,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            10060,
            0,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            1395,
            1,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            4681,
            1,
            5,
        ),
    )
    for dataset_rel, record, p, ground_id in cases:
        dataset_path = root / dataset_rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_rel}")
        ds = read_dataset(str(dataset_path))
        row = ds.samples[record]
        assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
        assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
        assert int(row["ref_t1"]["ground_id"][p]) == ground_id

        out = _run_one_step(ds, record)
        ref = row["ref_t1"]
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
            assert int(out[field][p]) == int(ref[field][p]), (dataset_rel, record, field)
        # The owner under test is the Fall_Coll floor publication boundary. The retained C path
        # still carries mpLib_8004DD90_Floor's 0.0001 root/floor bias through f32 projection, while
        # Slippi's row is rounded after the source callback writeback.
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_fod_fall_coll_current_down_input_keeps_transformed_platform_pass() -> None:
    # Negative boundary for the source platform predicate above: ftCo_80096CC8 consumes current
    # callback input, so a below-threshold current stick rejects a FoD transformed platform and
    # writes floor_skip instead of landing. The prior input alone is covered by the positive EWT row
    # where previous stick is below x25C but current stick has released.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
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
    record = 1395
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING

    def hold_down(_prev_input: np.ndarray, input_t: np.ndarray) -> None:
        input_t["p"][0, p]["main_y"] = np.int8(-80)

    out = _run_one_step(ds, record, input_mutator=hold_down)
    assert int(out["action_id"][p]) == ACT_FALL
    assert int(out["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(row["seed_t"]["ground_id"][p])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "p", "expected_ground"),
    [
        ("ExtraLargeScaryHornet.msl", 4180, 1, 34),
        ("WingedGorgeousPanther.msl", 2804, 1, 34),
    ],
)
def test_marth_fallaerial_commonfall_blended_ecb_delays_shallow_stadium_landing(
    dataset_name: str, record: int, p: int, expected_ground: int
) -> None:
    # FallAerial_Coll routes through ft_800831CC -> mpColl_80047E14 with the same platform-pass
    # callback as Fall/FallSpecial. Its Anim callback also runs ftCo_Fall_Anim_Inner and blends
    # TransN descendants through ftAnim_8006FE9C before the map callback. Shallow Stadium contacts
    # therefore sample the blended FallAerial/FallAerialB ECB envelope, not the neutral
    # FallAerial table alone; the blended current bottom is still above the floor/platform here,
    # so vanilla remains airborne for one more callback.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallAerial.c::{
    #   ftCo_FallAerial_Anim,ftCo_FallAerial_Coll}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
    #   ftCo_Fall_Anim_Inner,ftCo_800CC988}
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/marth/replays/validation/marth" / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["char_id"][p]) == 18
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL_AERIAL
    assert int(row["seed_t"]["animation_index"][p]) == SM_FALL_AERIAL
    x4, msid = _debug_commonfall_seed_state(ds, record, p)
    assert x4 > 0.0
    assert msid == 25
    assert int(row["ref_t1"]["action_id"][p]) == ACT_FALL_AERIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert int(out["ground_id"][p]) == expected_ground
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "p", "expected_ground"),
    [
        ("ExtraLargeScaryHornet.msl", 4181, 1, 34),
        ("WingedGorgeousPanther.msl", 2805, 1, 35),
        ("DraftyHealthyHare.msl", 6555, 0, 3),
    ],
)
def test_marth_fallaerial_commonfall_blended_ecb_still_lands_deeper_contacts(
    dataset_name: str, record: int, p: int, expected_ground: int
) -> None:
    # Adjacent positive boundary for the blended ECB owner above: once the same FallAerial callback
    # reaches a deeper floor/platform crossing, source mpColl_80044628_Floor owns the landing.
    # This keeps the blended-ECB repair from becoming a generic FallAerial landing delay.
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/marth/replays/validation/marth" / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL_AERIAL
    x4, msid = _debug_commonfall_seed_state(ds, record, p)
    assert x4 > 0.0
    assert msid in (24, 25)
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == expected_ground

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "p", "expected_action", "expected_msid"),
    [
        ("RuralReasonableRat.msl", 523, 0, ACT_LANDING, 21),
        ("StiffLustrousZebra.msl", 1619, 0, ACT_FALL, 22),
        ("StiffLustrousZebra.msl", 1620, 0, ACT_LANDING, 22),
        ("StiffLustrousZebra.msl", 3696, 0, ACT_FALL, 22),
    ],
)
def test_sheik_fall_commonfall_blended_ecb_controls_floor_sweep(
    dataset_name: str, record: int, p: int, expected_action: int, expected_msid: int
) -> None:
    # Sheik ordinary Fall uses the same ftCo_Fall_Anim_Inner directional submotion blend as Marth
    # FallAerial above. A focused Dolphin probe on these witness rows captured
    # ftCo_Fall_Coll -> ft_800831CC -> mpColl_80047E14 and showed vanilla's
    # mpColl_80044628_Floor accept/reject split follows the CommonFall-blended CollData ECB
    # bottom, not the raw neutral Fall table:
    # - RuralReasonableRat:523 lowers the Fall/F bottom enough to land.
    # - StiffLustrousZebra:1619 raises the Fall/B bottom and stays airborne.
    # - StiffLustrousZebra:1620 is the adjacent deeper Fall/B contact that lands next frame.
    # - StiffLustrousZebra:3696 is a fastfall Fall/B row where current-speed reconstruction
    #   over-blends x4 and false-publishes Landing; the explicit replay-prefix x4 seed stays
    #   airborne like source.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
    #   ftCo_Fall_Anim_Inner,ftCo_Fall_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
    # Probe logs:
    # reports/triage/newchar_sheik/fall_floor_probe_{rural_523_p0,stiff_1619_p0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/sheik/replays/validation/sheik" / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["char_id"][p]) == CHAR_SHEIK
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["seed_t"]["animation_index"][p]) == SM_FALL
    assert int(row["seed_t"]["common_fall_blend_valid_u8"][p]) == 1
    x4, msid = _debug_commonfall_seed_state(ds, record, p)
    assert x4 > 0.0
    assert msid == expected_msid
    assert x4 == pytest.approx(float(row["seed_t"]["common_fall_blend_x4_f32"][p]), abs=1e-7)
    assert msid == int(row["seed_t"]["common_fall_blend_msid_u16"][p])
    assert int(row["ref_t1"]["action_id"][p]) == expected_action

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "p", "platform_i", "seed_action", "ref_action", "named_height"),
    [
        ("ElatedWearyTermite.msl", 2523, 0, 1, ACT_ATTACK_AIR_B, ACT_LANDING_AIR_B, 25.0),
        ("ParallelTemptingElk.msl", 1395, 1, 1, ACT_FALL, ACT_LANDING, 20.0),
    ],
)
def test_fod_landing_uses_named_grizumi_platform_pose_for_source_height(
    dataset_name: str,
    record: int,
    p: int,
    platform_i: int,
    seed_action: int,
    ref_action: int,
    named_height: float,
) -> None:
    # FoD grIzumi/platform events can expose a source-owned platform pose rounded just below a
    # generated target/initial height. Source collision consumes the JObj/mpLib line at the
    # generated pose, not the rounded Slippi event float, before Landing_Coll / JumpAerial_Coll
    # publishes the landing row.
    #
    # refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CCBDC,grIzumi_801CC358}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
    # refs/melee/src/melee/mp/mplib.c::{mpLib_80055E9C,mpLib_8004DD90_Floor}
    # data/stages/bin/griz.bin::MSLSTG01 platform_transform/platform_motion records
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == seed_action
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][platform_i]) & 0x04
    assert abs(float(row["seed_t"]["stage_fod_platform_height_f32"][platform_i]) - named_height) < 1e-3
    assert int(row["ref_t1"]["action_id"][p]) == ref_action

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)


@pytest.mark.integration
def test_fod_fall_locked_root_initial_pose_does_not_land_without_current_source() -> None:
    # PTE:203 is Fall over FoD's initial/named right platform height. The live rollout carries an
    # ECB-lock/root projection from JumpF -> Fall, but vanilla does not publish Landing until the
    # following frame. The initial grIzumi pose is valid platform geometry, not same-frame collision
    # authority for Fall_Coll's locked root owner.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # data/stages/bin/griz.bin::MSLSTG01 platform_transform/platform_motion records
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
    p = 1

    row203 = ds.samples[203]
    assert int(row203["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row203["seed_t"]["stage_fod_platform_height_source_u8"][1]) == 0
    assert int(row203["ref_t1"]["action_id"][p]) == ACT_FALL

    out203 = _run_rollout_to_record(ds, 0, 203)
    assert int(out203["action_id"][p]) == ACT_FALL
    assert int(out203["on_ground"][p]) == 0
    assert int(out203["ground_id"][p]) == int(row203["ref_t1"]["ground_id"][p])
    assert float(out203["pos_y"][p]) == pytest.approx(float(row203["ref_t1"]["pos_y"][p]), abs=1e-6)

    row204 = ds.samples[204]
    assert int(row204["ref_t1"]["action_id"][p]) == ACT_LANDING
    out204 = _run_rollout_to_record(ds, 0, 204)
    assert int(out204["action_id"][p]) == ACT_LANDING
    assert int(out204["on_ground"][p]) == 1
    assert int(out204["ground_id"][p]) == int(row204["ref_t1"]["ground_id"][p])
    assert float(out204["pos_y"][p]) == pytest.approx(float(row204["ref_t1"]["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_fod_landing_named_platform_pose_snap_is_not_generic_height_rounding() -> None:
    # Boundary for the named-pose snap above: an arbitrary same-step source height near, but not at,
    # the generated initial pose must still move the collision line by the supplied height. This
    # keeps the owner tied to grIzumi's generated target/initial constants rather than a generic
    # platform-y clamp.
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
    record = 1395
    p = 1
    platform_i = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][platform_i]) & 0x04

    def move_outside_named_pose(seed_t: np.ndarray) -> None:
        seed_t["stage_fod_platform_height_f32"][0, platform_i] = np.float32(20.01)

    out = _run_one_step(ds, record, seed_mutator=move_outside_named_pose)
    assert int(out["action_id"][p]) == ACT_LANDING
    assert float(out["pos_y"][p]) > float(row["ref_t1"]["pos_y"][p]) + 0.005


@pytest.mark.integration
def test_fod_landing_named_platform_pose_snap_requires_current_source_owner() -> None:
    # Boundary for stale sparse heights: a valid FoD platform height near a generated grIzumi pose
    # is not enough to become current mpLib/JObj authority. Without the source event/contact bit or
    # live velocity, the runtime must keep the carried sparse height instead of snapping it to a
    # named pose.
    # refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CCBDC,grIzumi_801CC358}
    # refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
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
    record = 1395
    p = 1
    platform_i = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][platform_i]) & 0x04

    def make_near_named_pose_stale(seed_t: np.ndarray) -> None:
        seed_t["stage_fod_platform_height_source_u8"][0, platform_i] = np.uint8(0)
        seed_t["stage_fod_platform_velocity_valid_u8"][0, platform_i] = np.uint8(0)
        seed_t["stage_fod_platform_height_valid_u8"][0, platform_i] = np.uint8(1)
        seed_t["stage_fod_platform_height_f32"][0, platform_i] = np.float32(
            float(row["ref_t1"]["pos_y"][p]) - 0.0005
        )

    out = _run_one_step(ds, record, seed_mutator=make_near_named_pose_stale)
    assert int(out["action_id"][p]) != int(row["ref_t1"]["action_id"][p])
    assert abs(float(out["pos_y"][p]) - float(row["ref_t1"]["pos_y"][p])) > 1.0e-4


@pytest.mark.integration
def test_fod_passive_grounded_knockback_uses_stage_material_friction() -> None:
    # Passive's first grounded knockback decay after the DamageFly -> Passive handoff uses
    # Fighter_procUpdate's grounded KB branch:
    #   ft_GetGroundFrictionMultiplier(fp) * co_attrs.gr_friction * p_ftCommonData->x200.
    # On FoD main floor segment 3, MapLine.lo_flags low byte is material 2, whose mplib x0
    # friction multiplier is 1.5. The replay seed lane remains 1.0, so the source owner must come
    # from generated MSLSTG01 floor material data rather than row-local seed fitting.
    #
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/ft_081B.c::ft_GetGroundFrictionMultiplier
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004CA6C
    # refs/melee/src/melee/mp/mplib.c::{mpLib_800569EC,mpLib_803BD488}
    # data/stages/bin/griz.bin::MSLSTG01 segment 3 ground_friction_mul
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    record = 3976
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_PASSIVE
    assert int(row["seed_t"]["ground_id"][p]) == 3
    assert float(row["seed_t"]["ground_friction_mul"][p]) == pytest.approx(1.0)

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["speed_x_attack"][p]) == pytest.approx(
        float(ref["speed_x_attack"][p]), abs=1e-7
    )
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)

    no_floor_out = _run_one_step(
        ds,
        record,
        seed_mutator=lambda seed: seed["ground_id"].__setitem__((0, p), np.uint16(0xFFFF)),
    )
    assert float(no_floor_out["speed_x_attack"][p]) != pytest.approx(
        float(ref["speed_x_attack"][p]), abs=1e-5
    )


@pytest.mark.integration
def test_fod_passive_material_friction_prevents_early_edge_fall_rollout() -> None:
    # In rollout, under-decaying the Passive grounded KB scalar slides Falco off FoD segment 3 one
    # frame early. The generated floor material multiplier keeps the grounded Passive episode
    # aligned through the edge boundary.
    #
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/mp/mplib.c::mpLib_800569EC
    # data/stages/bin/griz.bin::MSLSTG01 segment 3 ground_friction_mul
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    p = 1

    out = _run_rollout_to_record(ds, 3762, 3981)
    ref = ds.samples[3981]["ref_t1"]
    assert int(ref["action_id"][p]) == ACT_PASSIVE
    for field in ("action_id", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["speed_x_attack"][p]) == pytest.approx(
        float(ref["speed_x_attack"][p]), abs=1e-6
    )
    # This lock owns the generated-material friction/edge-state outcome, not bit-exact long-rollout
    # X accumulation. The remaining ~2.7e-5 displacement is f32 operation-order drift after the
    # source-backed speed has already matched and does not move the edge admission.
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=3e-5)


@pytest.mark.integration
def test_fod_match_start_initial_platform_height_rollout_lands_damage_on_side_platform() -> None:
    # Match-start FoD platform initialization:
    # grIzumi creates the side-platform JObjs at their extracted initial heights before any hidden
    # RNG target/phase can move them. Sparse replay streams can leave the frame -123 height lane
    # marked invalid, but the carried initial height is source-owned stage data and must be trusted
    # for rollout floor collision until a later explicit platform source owner appears.
    #
    # refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CCBDC,grIzumi_801CC358}
    # data/stages/bin/griz.bin::MSLSTG01 platform_transform.y_const
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
    p = 1

    start = ds.samples[0]["seed_t"]
    assert int(start["frame_id"]) == -123
    assert int(start["stage_fod_platform_height_valid_u8"][0]) == 0
    assert float(start["stage_fod_platform_height_f32"][0]) == pytest.approx(28.0)

    out = _run_rollout_to_record(ds, 0, 442)
    ref = ds.samples[442]["ref_t1"]
    assert int(ref["action_id"][p]) == ACT_LANDING
    for field in ("action_id", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-4)

    no_match_start_out = _run_rollout_to_record_with_seed_mutator(
        ds, 0, 442, seed_mutator=lambda seed: seed["frame_id"].__setitem__(0, np.int32(-122))
    )
    assert int(no_match_start_out["action_id"][p]) != ACT_LANDING


@pytest.mark.integration
def test_fod_attackair_shallow_transformed_platform_seed_carries_floor_owner(
    tmp_path: Path,
) -> None:
    # FoD AttackAir_Coll shallow transformed-platform owner:
    # - AttackAir_Coll routes through ft_80082C74 -> mpColl_800471F8. During the extracted
    #   AttackAirN/Hi/Lw first HitCapsule create->clear phase, a shallow first contact with FoD's
    #   height-transformed side platform can reject LandingAir* and carry the callback-local
    #   CollData floor owner into the next direct reseed row.
    # - PTE:1640/1641 cover Falco AttackAirHi around its first clear/second create boundary.
    #   PTE:1691/7960 cover no-floor-skip down-held in-span platform pass. PTE:1692 covers Fox
    #   AttackAirN late-hit lifetime with carried floor_skip. PTE:7843/10297 are released/same-floor
    #   controls that must still publish LandingAirN normally.
    # data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
    # data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
    # data/stages/bin/griz.bin::MSLSTG01 height platform transforms
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp = root / "replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.slpz"
    if not slp.exists():
        pytest.skip(f"missing local replay: {slp}")

    out_path = tmp_path / "ParallelTemptingElk.msl"
    _main_impl(
        Namespace(
            slp=str(slp),
            out=str(out_path),
            ports="1,2",
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
    )
    ds = read_dataset(str(out_path))

    for record, action in ((1640, ACT_ATTACK_AIR_HI), (1641, ACT_ATTACK_AIR_HI), (1692, ACT_ATTACK_AIR_N)):
        row = ds.samples[record]
        assert int(row["seed_t"]["action_id"][0]) == action
        assert int(row["seed_t"]["floor_skip_segment_id_u16"][0]) == 1
        assert int(row["seed_t"]["floor_skip_segment_valid_u8"][0]) == 1
        assert int(row["ref_t1"]["action_id"][0]) == action
        assert int(row["ref_t1"]["on_ground"][0]) == 0

        out = _run_one_step(ds, record)
        ref = row["ref_t1"]
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
            assert int(out[field][0]) == int(ref[field][0]), (record, field)
        assert float(out["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)

    for record in (1691, 7960):
        row = ds.samples[record]
        assert int(row["seed_t"]["action_id"][0]) == ACT_ATTACK_AIR_N
        assert int(row["seed_t"]["floor_skip_segment_id_u16"][0]) == 0xFFFF
        assert int(row["seed_t"]["floor_skip_segment_valid_u8"][0]) == 0
        assert int(row["input_t"]["p"][0]["main_y"]) < -70
        assert int(row["seed_t"]["ground_id"][0]) != 1
        assert int(row["ref_t1"]["action_id"][0]) == ACT_ATTACK_AIR_N
        assert int(row["ref_t1"]["on_ground"][0]) == 0

        out = _run_one_step(ds, record)
        ref = row["ref_t1"]
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
            assert int(out[field][0]) == int(ref[field][0]), (record, field)
        assert float(out["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)

    released = ds.samples[7843]
    assert int(released["seed_t"]["action_id"][0]) == ACT_ATTACK_AIR_N
    assert int(released["seed_t"]["floor_skip_segment_id_u16"][0]) == 0xFFFF
    assert int(released["seed_t"]["floor_skip_segment_valid_u8"][0]) == 0
    assert int(released["ref_t1"]["action_id"][0]) == ACT_LANDING_AIR_N
    released_out = _run_one_step(ds, 7843)
    assert int(released_out["action_id"][0]) == ACT_LANDING_AIR_N
    assert int(released_out["on_ground"][0]) == 1
    assert float(released_out["pos_y"][0]) == pytest.approx(
        float(released["ref_t1"]["pos_y"][0]), abs=2e-4
    )

    downheld = ds.samples[10297]
    assert int(downheld["seed_t"]["action_id"][0]) == ACT_ATTACK_AIR_N
    assert int(downheld["seed_t"]["floor_skip_segment_id_u16"][0]) == 0xFFFF
    assert int(downheld["seed_t"]["floor_skip_segment_valid_u8"][0]) == 0
    assert int(downheld["input_t"]["p"][0]["main_y"]) < -100
    assert int(downheld["ref_t1"]["action_id"][0]) == ACT_LANDING_AIR_N
    downheld_out = _run_one_step(ds, 10297)
    assert int(downheld_out["action_id"][0]) == ACT_LANDING_AIR_N
    assert int(downheld_out["on_ground"][0]) == 1
    assert float(downheld_out["pos_y"][0]) == pytest.approx(
        float(downheld["ref_t1"]["pos_y"][0]), abs=2e-4
    )


@pytest.mark.integration
def test_marth_attackairn_late_cmd0_tail_needs_current_fod_platform_source() -> None:
    # Marth NAir late tail floor-publication owner:
    # - AttackAir_Coll routes through ft_80082C74 -> mpColl_800471F8.
    # - After the late hit clear, cmd_var[0] is still active, but a FoD height-platform line
    #   reconstructed only from sparse seed height is not enough to publish LandingAirN. Source
    #   still needs current grIzumi/mpLib authority or a callback-local bottom/projection producer.
    # - IPW:1037/1038 have no current/same-step/live source and no floor probe; IPW:488/3095 are
    #   adjacent current-source negatives that must continue to land.
    # data/motion_state/owners/<char>.bin::MSLMSO01 submotion_id
    # data/scripts/<char>.bin::MSLFTSC1 set_cmd_var/create_hitbox/clear_hitboxes events
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
    #   mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/marth/replays/validation/marth/InternalPowerlessWallaby.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")
    ds = read_dataset(str(dataset_path))
    p = 1

    for record in (1037, 1038):
        row = ds.samples[record]
        assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_N
        assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][0]) == 0
        assert int(row["seed_t"]["stage_fod_platform_height_source_u8"][1]) == 0
        assert int(row["ref_t1"]["action_id"][p]) == ACT_ATTACK_AIR_N

        out, dbg = _run_one_step_with_colldata(ds, record)
        ref = row["ref_t1"]
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
            assert int(out[field][p]) == int(ref[field][p]), (record, field)
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
        assert int(dbg["floor_result_valid"][p]) == 0
        assert (
            int(dbg["floor_probe_reject_bits"][p])
            & MPCOLL_REJECT_ATTACKAIR_TRANSFORMED_PLATFORM_ECB_ONLY
        )

    for record, expected_ground in ((488, 0), (3095, 1)):
        row = ds.samples[record]
        assert int(row["seed_t"]["action_id"][p]) == ACT_ATTACK_AIR_N
        assert any(int(v) != 0 for v in row["seed_t"]["stage_fod_platform_height_source_u8"])
        assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_AIR_N

        out, dbg = _run_one_step_with_colldata(ds, record)
        ref = row["ref_t1"]
        for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
            assert int(out[field][p]) == int(ref[field][p]), (record, field)
        assert int(out["ground_id"][p]) == expected_ground
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)
        assert int(dbg["floor_result_valid"][p]) == 1
        assert int(dbg["floor_probe_reject_bits"][p]) == 0


@pytest.mark.integration
def test_yoshi_guard_player_nudge_floor_loss_enters_missfoot_cnm_1086() -> None:
    # Yoshi's Story Guard floor-loss owner:
    # - Fighter_8006A360 runs ftCommon_8007E0E4 before Fighter_procUpdate, so the common
    #   fighter-overlap x450 nudge can move an active shield state past an open floor endpoint.
    # - Guard/GuardOff/GuardReflect Coll callbacks call ft_800845B4, which routes a ledge-slip
    #   floor loss behind facing into MissFoot through ftCo_8009F39C.
    # - CNM:1085 is the in-span control. CNM:1086 is the first left-endpoint nudge that leaves
    #   Yoshi's left sloped ledge floor while Fox still faces right, so vanilla enters MissFoot
    #   instead of publishing the ordinary GuardOff release row.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_Guard_Coll,ftCo_GuardOff_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_800845B4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::ftCo_8009F39C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "CheeryNumbMonkey.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    p = 1

    control = ds.samples[1085]
    assert int(control["seed_t"]["action_id"][p]) == ACT_GUARD
    assert int(control["ref_t1"]["action_id"][p]) == ACT_GUARD
    control_out = _run_one_step(ds, 1085)
    assert int(control_out["action_id"][p]) == ACT_GUARD
    assert int(control_out["on_ground"][p]) == 1
    assert float(control_out["pos_x"][p]) == pytest.approx(
        float(control["ref_t1"]["pos_x"][p]), abs=1e-6
    )

    row = ds.samples[1086]
    assert int(row["seed_t"]["action_id"][p]) == ACT_GUARD
    assert int(row["ref_t1"]["action_id"][p]) == ACT_MISS_FOOT
    out = _run_one_step(ds, 1086)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        ("datasets/marth/replays/validation/marth/FemaleFarOffSalmon.msl", 11615, 0),
        ("datasets/marth/replays/validation/marth/VictoriousSpitefulAlpaca.msl", 2540, 1),
        ("datasets/marth/replays/validation/marth/DraftyHealthyHare.msl", 10438, 1),
    ],
    ids=["marth_locked_missfoot", "falco_locked_missfoot", "fox_locked_missfoot"],
)
def test_locked_missfoot_first_floor_contact_stays_airborne(
    dataset_rel: str, record: int, p: int
) -> None:
    # Fresh MissFoot floor-loss source owner:
    # ftCo_8009F39C enters MissFoot through ftCommon_8007D5D4, which sets fp->ecb_lock and
    # CollData_X130_Locked. The first serialized MissFoot_Coll has already advanced to
    # action_frame=1 before ft_80082F28 -> ft_CheckGroundAndLedge -> mpColl_800473CC, but vanilla
    # keeps this shallow same-floor contact airborne instead of entering Wait/Landing.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::{ftCo_8009F39C,ftCo_MissFoot_Coll}
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    # refs/melee/src/melee/ft/fighter.c::Fighter_procMap
    # refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800473CC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]

    seed = row["seed_t"]
    ref = row["ref_t1"]
    assert int(seed["action_id"][p]) == ACT_MISS_FOOT
    assert int(seed["action_frame"][p]) == 0
    assert int(seed["seed_prev_action_id"][p]) != ACT_MISS_FOOT
    assert int(seed["ecb_lock_timer"][p]) >= 9
    assert int(ref["action_id"][p]) == ACT_MISS_FOOT
    assert int(ref["on_ground"][p]) == 0
    assert int(ref["action_frame"][p]) == 1

    out, dbg = _run_one_step_with_colldata(ds, record)
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), (record, field)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert int(dbg["floor_result_valid"][p]) == 0
    assert (
        int(dbg["floor_probe_reject_bits"][p])
        & MPCOLL_REJECT_MISSFOOT_ECB_LOCK_FIRST_FLOOR
    )


@pytest.mark.integration
def test_no_lock_missfoot_late_floor_contact_lands_feh_3266() -> None:
    # Adjacent negative: once the CollData_X130 lock episode is gone and MissFoot is sustained,
    # the same ft_CheckGroundAndLedge/mpColl_800473CC floor callback can publish the normal Landing.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        "FlippantEnchantedHorse.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    record = 3266
    p = 1
    row = ds.samples[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]

    assert int(seed["action_id"][p]) == ACT_MISS_FOOT
    assert int(seed["seed_prev_action_id"][p]) == ACT_MISS_FOOT
    assert int(seed["ecb_lock_timer"][p]) == 0
    assert int(ref["action_id"][p]) == ACT_LANDING
    assert int(ref["on_ground"][p]) == 1

    out, dbg = _run_one_step_with_colldata(ds, record)
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-4)
    assert int(dbg["floor_result_valid"][p]) == 1
    assert (
        int(dbg["floor_probe_reject_bits"][p])
        & MPCOLL_REJECT_MISSFOOT_ECB_LOCK_FIRST_FLOOR
    ) == 0
