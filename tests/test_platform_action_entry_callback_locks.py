from __future__ import annotations

from argparse import Namespace
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tests.test_colldata_ecb_substrate import _colldata_ecb_dtype
from tools.eval.discrete_compare_lanes import compile_discrete_compare_lanes, first_mismatch_values
from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.validation_profile import get_validation_profile
from tools.slippi.make_dataset_from_slp import _main_impl


BUTTON_L = 0x0040
BUTTON_R = 0x0020

ACT_ESCAPE_AIR = 236
ACT_LANDING_FALL_SPECIAL = 43
ACT_FALL_SPECIAL = 35
ACT_JUMP_AERIAL_F = 27
ACT_JUMP_AERIAL_B = 28
ACT_KNEE_BEND = 24
ACT_FALL = 29
ACT_PASS = 244
ACT_FX_SPECIAL_AIR_N_START = 344
ACT_JUMP_F = 25
ACT_ATTACK_AIR_N = 65
ACT_ATTACK_AIR_LW = 65
ACT_ATTACK_AIR_F = 66
ACT_ATTACK_AIR_HI = 68
ACT_LANDING_AIR_N = 70
ACT_LANDING_AIR_LW = 70
ACT_LANDING_AIR_HI = 73
ACT_LANDING = 42
ACT_GUARD_ON = 178
ACT_GUARD_REFLECT = 182
ACT_PASSIVE = 199
ACT_THROW_F = 219
ACT_ESCAPE_N = 235
ACT_JUMP_B = 26
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
        return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
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
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


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
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


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
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=3e-6)

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
    # soft platform. Sustained no-lock EscapeAir should not borrow the static-platform sweep owner
    # and snap upward to line 2 when the carried CollData floor is a different transformed platform.
    #
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=static_y,line_id=2)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044628_Floor}
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
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 2170
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_KNEE_BEND
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL

    def clear_lr_edge(_prev_input: np.ndarray, input_t: np.ndarray) -> None:
        input_t["p"][0, p]["buttons"] = np.uint16(
            int(input_t["p"][0, p]["buttons"]) & (0xFFFF ^ (BUTTON_L | BUTTON_R))
        )

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
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=3e-6)


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
    assert int(released["ref_t1"]["action_id"][0]) == ACT_LANDING_AIR_LW
    released_out = _run_one_step(ds, 7843)
    assert int(released_out["action_id"][0]) == ACT_LANDING_AIR_LW
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
