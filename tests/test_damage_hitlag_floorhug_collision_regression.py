from __future__ import annotations

import os
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset, read_dataset_window
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp
from tests.test_colldata_ecb_substrate import _colldata_ecb_dtype, _ecb_rel_points


_CONTACTS_DTYPE = np.dtype(
    [
        ("wall_kind", ("u1", (4,))),
        ("_pad0", ("u1", (4,))),
        ("wall_id", ("<u2", (4,))),
        ("wall_contact_x", ("<f4", (4,))),
        ("wall_contact_y", ("<f4", (4,))),
        ("wall_normal_x", ("<f4", (4,))),
        ("wall_normal_y", ("<f4", (4,))),
        ("ceiling_id", ("<u2", (4,))),
        ("_pad1", ("<u2", (4,))),
        ("ceiling_contact_x", ("<f4", (4,))),
        ("ceiling_contact_y", ("<f4", (4,))),
        ("ceiling_normal_x", ("<f4", (4,))),
        ("ceiling_normal_y", ("<f4", (4,))),
        ("coll_env_flags", ("<u4", (4,))),
        ("coll_prev_env_flags", ("<u4", (4,))),
        ("damage_hitlag_wall_asdi_latch", ("u1", (4,))),
    ],
    align=False,
)

MSL_COLLIDE_FLOOR_MASK = 0x18000
FLOOR_RESULT_NONE = 0
FLOOR_RESULT_STAY_AIRBORNE = 3
FLOOR_MODE_NONE = 0
FLOOR_MODE_BOTTOM_SWEEP = 1
FLOOR_MODE_STAY_AIRBORNE_PROJECTION = 6
ACT_ATTACK_AIR_LW = 0x0045
ACT_DAMAGE_HI_2 = 0x004C
ACT_DAMAGE_HI_3 = 0x004D
ACT_DAMAGE_N_2 = 0x004F
ACT_DAMAGE_AIR_1 = 0x0054
ACT_DAMAGE_AIR_2 = 0x0055
ACT_DAMAGE_AIR_3 = 0x0056
ACT_DAMAGE_FLY_N = 0x0058
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_DAMAGE_FLY_ROLL = 0x005B
ACT_LANDING = 0x002A
ACT_DOWN_BOUND_U = 0x00B7
ACT_PASSIVE = 0x00C7
ACT_DOWN_DAMAGE_D = 0x00C1
ACT_ESCAPE_AIR = 0x00EC
ACT_MISS_FOOT = 0x00FB
ACT_THROWN_LW = 0x00F2
SM_ATTACK_AIR_LW = 72
SM_DAMAGE_FLY_TOP = 180


def test_damage_hitlag_colldata_ecb_samples_previous_attackair_facing() -> None:
    binding = pytest.importorskip("msl_binding")
    char = np.array([1, 1], dtype=np.uint8)
    action = np.array([ACT_ATTACK_AIR_LW, ACT_DAMAGE_FLY_TOP], dtype=np.uint16)
    anim = np.array([SM_ATTACK_AIR_LW, SM_DAMAGE_FLY_TOP], dtype=np.uint32)
    anim_frame = np.array([5.0, 0.0], dtype=np.float32)
    frame_speed = np.array([1.0, 1.0], dtype=np.float32)
    # The first visible Damage hitlag row can have already flipped facing. Source
    # mpColl_LoadECB_inline still consumes the pre-Damage AttackAir CollData ECB, including its
    # facing-mirrored side points.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800477E0}
    facing = np.array([0, 1], dtype=np.uint8)
    on_ground = np.array([0, 0], dtype=np.uint8)
    hitlag = np.array([0, 5], dtype=np.uint16)

    bottom, top, left, right, side, valid = binding.derive_damage_hitlag_colldata_ecb(
        char, action, anim, anim_frame, frame_speed, facing, on_ground, hitlag
    )

    prev_facing_expected = _ecb_rel_points(SM_ATTACK_AIR_LW, 6, facing=0)
    current_facing_wrong = _ecb_rel_points(SM_ATTACK_AIR_LW, 6, facing=1)
    assert int(valid[1]) == 1
    assert float(bottom[1]) == pytest.approx(prev_facing_expected["bottom"])
    assert float(top[1]) == pytest.approx(prev_facing_expected["top"])
    assert float(left[1]) == pytest.approx(prev_facing_expected["left"])
    assert float(right[1]) == pytest.approx(prev_facing_expected["right"])
    assert float(side[1]) == pytest.approx(prev_facing_expected["side"])
    assert float(left[1]) != pytest.approx(current_facing_wrong["left"])
    assert float(right[1]) != pytest.approx(current_facing_wrong["right"])


def _run_one_step_with_contacts(dataset_path: Path, record: int) -> tuple[np.ndarray, np.ndarray, np.void]:
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    contacts_stride = int(sizes["collision_contacts"])
    assert int(_CONTACTS_DTYPE.itemsize) == contacts_stride

    seed_bytes = row["seed_t"].view("u1").reshape(1, seed_stride).copy()
    prev_input_bytes = row["prev_input_t"].view("u1").reshape(1, input_stride).copy()
    input_bytes = row["input_t"].view("u1").reshape(1, input_stride).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_contacts_bytes = np.empty((1, contacts_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        binding.debug_write_collision_contacts(handle, out_contacts_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    ref = row["ref_t1"][0].copy()
    contacts = np.frombuffer(out_contacts_bytes.tobytes(), dtype=_CONTACTS_DTYPE, count=1)[0]
    return out, ref, contacts


def _run_one_step_with_contacts_and_colldata(
    dataset_path: Path, record: int
) -> tuple[np.ndarray, np.ndarray, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    contacts_stride = int(sizes["collision_contacts"])
    colldata_stride = int(sizes["colldata_ecb"])
    colldata_dtype = _colldata_ecb_dtype()
    assert int(_CONTACTS_DTYPE.itemsize) == contacts_stride
    assert int(colldata_dtype.itemsize) == colldata_stride

    seed_bytes = row["seed_t"].view("u1").reshape(1, seed_stride).copy()
    prev_input_bytes = row["prev_input_t"].view("u1").reshape(1, input_stride).copy()
    input_bytes = row["input_t"].view("u1").reshape(1, input_stride).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_contacts_bytes = np.empty((1, contacts_stride), dtype=np.uint8)
    out_colldata_bytes = np.empty((1, colldata_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        binding.debug_write_collision_contacts(handle, out_contacts_bytes)
        binding.debug_write_colldata_ecb(handle, out_colldata_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    ref = row["ref_t1"][0].copy()
    contacts = np.frombuffer(out_contacts_bytes.tobytes(), dtype=_CONTACTS_DTYPE, count=1)[0]
    colldata = np.frombuffer(out_colldata_bytes.tobytes(), dtype=colldata_dtype, count=1)[0]
    return out, ref, contacts, colldata


def _run_one_step_with_contacts_from_samples(
    samples: np.ndarray, num_players: int, record: int
) -> tuple[np.ndarray, np.ndarray, np.void]:
    row = samples[record : record + 1]
    assert int(row.shape[0]) == 1

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    contacts_stride = int(sizes["collision_contacts"])
    assert int(_CONTACTS_DTYPE.itemsize) == contacts_stride

    seed_bytes = row["seed_t"].view("u1").reshape(1, seed_stride).copy()
    prev_input_bytes = row["prev_input_t"].view("u1").reshape(1, input_stride).copy()
    input_bytes = row["input_t"].view("u1").reshape(1, input_stride).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_contacts_bytes = np.empty((1, contacts_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=num_players,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        binding.debug_write_collision_contacts(handle, out_contacts_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    ref = row["ref_t1"][0].copy()
    contacts = np.frombuffer(out_contacts_bytes.tobytes(), dtype=_CONTACTS_DTYPE, count=1)[0]
    return out, ref, contacts


def _run_one_step_seed_arrays(
    seed: np.ndarray, prev_input: np.ndarray, input_t: np.ndarray, num_players: int
) -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = seed.reshape(1).view("u1").reshape(1, seed_stride).copy()
    prev_input_bytes = prev_input.reshape(1).view("u1").reshape(1, input_stride).copy()
    input_bytes = input_t.reshape(1).view("u1").reshape(1, input_stride).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=num_players,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


def _run_rollout_record(dataset_path: Path, start_record: int, target_record: int) -> tuple[np.void, np.void]:
    ds = read_dataset_window(str(dataset_path), start_record, target_record + 1)
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    try:
        binding.reseed_seed_rollout(
            handle, ds.samples[0:1]["seed_t"].view("u1").reshape(1, seed_stride).copy()
        )
        for off in range(0, target_record - start_record + 1):
            row = ds.samples[off : off + 1]
            binding.step_input(
                handle,
                row["prev_input_t"].view("u1").reshape(1, input_stride).copy(),
                row["input_t"].view("u1").reshape(1, input_stride).copy(),
            )
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
    return (
        ds.samples[target_record - start_record]["ref_t1"].copy(),
        out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(),
    )


@pytest.mark.integration
def test_damageair_reentry_active_hitlag_floorhug_projects_root_his_3147() -> None:
    # Replay-real lock for common DamageAir re-entry below the FD floor:
    # - a BODY refresh changes DamageAir3 -> DamageAir2 while hitlag remains active,
    # - the next ftCo_Damage_Coll / ft_80081DD4 pass refreshes FloorPush|FloorHug from the
    #   persisted floor and keeps the fighter airborne at floor height,
    # - adjacent non-DamageAir and DamageFly rows remain outside this owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[3147:3148]
    p = 0

    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_AIR_2
    assert int(row["seed_t"]["seed_prev_action_id"][0, p]) == ACT_DAMAGE_AIR_3
    assert int(row["seed_t"]["hitlag"][0, p]) == 4
    assert float(row["seed_t"]["pos_y"][0, p]) < 0.0
    assert float(row["ref_t1"]["pos_y"][0, p]) == pytest.approx(0.00010013580322265625, abs=1e-6)

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 3147)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_AIR_2
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK


@pytest.mark.integration
def test_damageair_sustained_active_hitlag_sdi_does_not_reproject_floor_his_3643() -> None:
    # Negative lock for the sibling common-Damage continuation path:
    # - OnEveryHitlag applies a held downward SDI displacement during sustained same-action
    #   DamageAir3 hitlag,
    # - vanilla publishes the below-floor airborne root and does not refresh FloorPush|FloorHug
    #   from the carried hard-floor line on this continuation row,
    # - the positive DamageAir re-entry owner above remains limited to a fresh DamageAir state
    #   transition while hitlag is active.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset_window(str(dataset_path), 3643, 3644)
    row = ds.samples[0:1]
    p = 1

    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_AIR_3
    assert int(row["seed_t"]["seed_prev_action_id"][0, p]) == ACT_DAMAGE_AIR_3
    assert int(row["seed_t"]["hitlag"][0, p]) == 4
    assert int(row["input_t"]["p"]["main_y"][0, p]) < 0
    assert int(row["seed_t"]["tilt_timer_y"][0, p]) == 254
    assert float(row["seed_t"]["pos_y"][0, p]) > 0.0
    assert float(row["ref_t1"]["pos_y"][0, p]) < 0.0

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 3643)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_AIR_3
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 3
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0


@pytest.mark.integration
def test_grounded_damage_floor_loss_uses_fod_world_platform_for_missfoot_mgs_661() -> None:
    # Grounded common Damage floor loss:
    # ftCo_Damage_Coll -> ft_800848DC uses mpColl_8004B108's ledge-slip side bits to choose
    # MissFoot vs the supplied ftCo_8008FC94 ground-to-air callback. FoD side-platform endpoints
    # are live grIzumi JObj geometry; using static MSLSTG01 segment endpoints falsely classifies
    # the transformed left-platform row as a facing-side ledge slip and enters MissFoot.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_8008FC94}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800848DC,ft_80082708}
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    # data/stages/bin/griz.bin::MSLSTG01 platform_transforms
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "MilkyGracefulStingray.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset_window(str(dataset_path), 661, 662)
    row = ds.samples[0]
    p = 0

    assert int(row["seed_t"]["action_id"][p]) == ACT_DAMAGE_N_2
    assert int(row["seed_t"]["on_ground"][p]) == 1
    assert int(row["seed_t"]["ground_id"][p]) == 0
    assert int(row["seed_t"]["hitlag"][p]) == 4
    assert int(row["ref_t1"]["action_id"][p]) == ACT_DAMAGE_N_2
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out, ref, _contacts = _run_one_step_with_contacts_from_samples(ds.samples, int(ds.header["num_players"]), 0)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_N_2
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 3
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 17
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)


@pytest.mark.integration
def test_grounded_damage_floor_loss_still_missfoots_past_facing_fod_world_endpoint_mgs_661_negative() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "MilkyGracefulStingray.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset_window(str(dataset_path), 661, 662)
    row = ds.samples[0]
    p = 0
    seed = row["seed_t"].copy()
    # The transformed left-platform line is roughly [-49.5, -21.0] in world coordinates here. The
    # replay row is past the right endpoint but facing right, so it takes the Damage callback. Flip
    # facing to prove the same source ledge-slip owner still enters MissFoot when the live endpoint
    # and facing-side pair actually match.
    seed["facing"][p] = np.uint8(0)
    seed["facing_dir1"][p] = np.int8(-1)

    out = _run_one_step_seed_arrays(seed, row["prev_input_t"].copy(), row["input_t"].copy(), int(ds.header["num_players"]))

    assert int(out["action_id"][p]) == ACT_MISS_FOOT
    assert int(out["hitstun"][p]) == 0


@pytest.mark.integration
def test_grounded_damagehi_hitlag_exit_asdi_reprojects_to_floor_pte_633() -> None:
    # Replay-real FoD lock for grounded Damage_Coll after post-hitlag ASDI:
    # - Fighter_8006A1BC calls ftCo_Damage_OnExitHitlag when hitlag reaches zero.
    # - Grounded ftCo_Damage_Coll still routes through ft_800848DC -> ft_80082708 ->
    #   mpColl_8004B108, so the post-ASDI root is projected back onto the current hard floor.
    # - The sibling non-damage synthetic below proves this is not a generic grounded "snap down"
    #   path.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnExitHitlag,ftCo_Damage_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800848DC,ft_80082708}
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset_window(str(dataset_path), 633, 634)
    row = ds.samples[0:1]
    p = 0

    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_HI_2
    assert int(row["seed_t"]["on_ground"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 1
    assert int(row["prev_input_t"]["p"]["main_y"][0, p]) > 100
    assert float(row["ref_t1"]["pos_y"][0, p]) == pytest.approx(0.0028748512, abs=1e-7)

    out, ref, contacts = _run_one_step_with_contacts_from_samples(
        row, int(ds.header["num_players"]), 0
    )

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_HI_2
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 5
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-7)
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK


def test_post_hitlag_projection_requires_grounded_damage_coll_pte_negative() -> None:
    # Same FoD input/action shape as the grounded DamageHi post-hitlag row above, but with the
    # victim airborne and safely above the floor. ftCo_Damage_Coll's grounded
    # ft_800848DC/mpColl_8004B108 projection must not run from the air path.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset_window(str(dataset_path), 633, 634)
    row = ds.samples[0]
    p = 0
    seed = row["seed_t"].copy()
    seed["on_ground"][p] = np.uint8(0)
    seed["ground_id"][p] = np.uint16(0xFFFF)
    seed["pos_y"][p] = np.float32(10.0)

    out = _run_one_step_seed_arrays(
        seed, row["prev_input_t"].copy(), row["input_t"].copy(), int(ds.header["num_players"])
    )

    assert int(out["action_id"][p]) == ACT_DAMAGE_HI_2
    assert int(out["hitlag"][p]) == 0
    assert int(out["on_ground"][p]) == 0
    assert float(out["pos_y"][p]) > 12.0


@pytest.mark.integration
def test_terminal_airborne_damage_iasa_can_enter_escapeair_pte_2417() -> None:
    # Replay-real FoD lock for the airborne common-Damage IASA delegate:
    # - terminal DamageAir1 has cleared x221C_b6/hitstun,
    # - ftCo_Damage_IASA forwards to ftCo_Fall_IASA_Inner, and
    # - Fall_IASA_Inner checks EscapeAir before its AttackAir / JumpAerial fallbacks.
    # This is not the DamageFly/DamageFall IASA ladder.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A58
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset_window(str(dataset_path), 2417, 2418)
    row = ds.samples[0:1]
    p = 0

    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_AIR_1
    assert int(row["seed_t"]["hitstun"][0, p]) == 1
    assert int(row["input_t"]["p"]["buttons"][0, p]) & 0x0040
    assert int(row["input_t"]["p"]["l"][0, p]) == 255
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_ESCAPE_AIR

    out, ref, _contacts = _run_one_step_with_contacts_from_samples(
        row, int(ds.header["num_players"]), 0
    )

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0


def test_terminal_airborne_damage_iasa_escapeair_requires_escape_input_pte_2417_negative() -> None:
    # Same terminal DamageAir seed with the shield/trigger press removed: the Fall_IASA_Inner
    # EscapeAir branch must not fire without the source input predicate.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset_window(str(dataset_path), 2417, 2418)
    row = ds.samples[0]
    p = 0
    input_t = row["input_t"].copy()
    input_t["p"]["buttons"][p] = np.uint16(0)
    input_t["p"]["l"][p] = np.uint8(0)
    input_t["p"]["r"][p] = np.uint8(0)

    out = _run_one_step_seed_arrays(
        row["seed_t"].copy(), row["prev_input_t"].copy(), input_t, int(ds.header["num_players"])
    )

    assert int(out["action_id"][p]) != ACT_ESCAPE_AIR


@pytest.mark.integration
def test_damageflytop_active_hitlag_floorhug_stays_airborne_qgd_9683() -> None:
    # Vanilla forensic lock from QGD rec=9683 p1:
    # - active hitlag DamageFlyTop
    # - downward SDI input
    # - ft_80081DD4 -> mpColl_800477E0 raises FloorPush|FloorHug and projects Y to the floor
    # - fighter stays airborne for the frame
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[9683 : 9684]
    p = 1

    assert int(row["seed_t"]["action_id"][0, p]) == 90
    assert int(row["seed_t"]["hitlag"][0, p]) == 2
    assert int(row["seed_t"]["on_ground"][0, p]) == 0
    assert float(row["seed_t"]["pos_y"][0, p]) == pytest.approx(-3.459904909133911, abs=1e-6)
    assert int(row["input_t"]["p"]["main_y"][0, p]) == -106
    assert int(row["ref_t1"]["action_id"][0, p]) == 90
    assert int(row["ref_t1"]["hitlag"][0, p]) == 1
    assert int(row["ref_t1"]["on_ground"][0, p]) == 0
    assert float(row["ref_t1"]["pos_y"][0, p]) == pytest.approx(0.00010013580322265625, abs=1e-6)

    out, ref, contacts, colldata = _run_one_step_with_contacts_and_colldata(dataset_path, 9683)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 90
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 1
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK
    assert int(colldata["floor_result_source"][p]) == FLOOR_RESULT_STAY_AIRBORNE
    assert int(colldata["floor_result_mode"][p]) == FLOOR_MODE_STAY_AIRBORNE_PROJECTION


@pytest.mark.integration
def test_damageflytop_downward_sdi_bottom_above_floor_stays_airborne_selfplay_181413() -> None:
    # Active-hitlag DamageFlyTop SDI bottom-contact boundary:
    # - ftCo_Damage_OnEveryHitlag moves the root below FD's floor on rec1417,
    # - the current ECB bottom is still above the floor, so mpColl_80044628_Floor has not accepted a
    #   bottom-floor contact for mpColl_80044948_Floor to project from,
    # - vanilla keeps the below-floor SDI root airborne; rec1420 is a later control that still
    #   consumes ordinary OnEveryHitlag SDI without publishing a false floor projection.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor}
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    samples = ds.samples
    p = 1

    row = samples[1417]
    assert int(row["seed_t"]["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(row["seed_t"]["hitlag"][p]) == 7
    assert int(row["input_t"]["p"]["main_y"][p]) < 0
    assert float(row["seed_t"]["pos_y"][p]) == pytest.approx(0.0001, abs=1e-7)
    assert float(row["ref_t1"]["pos_y"][p]) == pytest.approx(-4.0499, abs=1e-6)

    out, ref, contacts = _run_one_step_with_contacts_from_samples(
        samples, int(ds.header["num_players"]), 1417
    )
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 6
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0

    out, ref, contacts = _run_one_step_with_contacts_from_samples(
        samples, int(ds.header["num_players"]), 1420
    )
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 3
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0


@pytest.mark.integration
def test_forensic_optional_damageflyn_hitlag_exit_floorhug_latch_requires_current_ecb_floor_hit_selfplay_1045() -> None:
    # Optional replay-forensic rollout check for the self-play rec=1045 p0 cluster. Package
    # coverage for this owner is the committed synthetic guard below plus the committed aggregate
    # floorhug positive/negative replay locks.
    #
    # - active-hitlag DamageFlyN rows can arm the stay-airborne FloorHug latch,
    # - on the hitlag-exit frame, ftCo_DamageFly_Coll may project from the root only after
    #   mpColl_80044628_Floor accepts the currently loaded ECB bottom as a floor hit,
    # - here the carried latch exists, but the DamageFlyN ECB bottom remains above the FD floor, so
    #   vanilla stays airborne in DamageFlyN instead of entering Passive.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnEveryHitlag,ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    if os.environ.get("MSL_RUN_TRIAGE_FORENSICS") != "1":
        pytest.skip(
            "optional gitignored self-play triage forensic; set MSL_RUN_TRIAGE_FORENSICS=1"
        )

    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "reports/triage/mainline_selfplay_datasets/mainline_selfplay_20260514T083640/"
        "reports/triage/mainline_selfplay_replays/Game_20260514T083640.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing optional local self-play triage dataset: {dataset_rel}")

    p = 0
    ds = read_dataset_window(str(dataset_path), 1045, 1046)
    seed = ds.samples[0]["seed_t"]
    ref = ds.samples[0]["ref_t1"]
    assert int(seed["action_id"][p]) == 88  # DamageFlyN
    assert int(seed["hitlag"][p]) == 1
    assert int(seed["hitstun"][p]) == 33
    assert int(seed["damage_post_hitlag_cb_kind"][p]) == 1
    assert int(ref["action_id"][p]) == 88
    assert int(ref["on_ground"][p]) == 0

    ref_rollout, out = _run_rollout_record(dataset_path, 0, 1045)

    assert int(out["action_id"][p]) == int(ref_rollout["action_id"][p]) == 88
    assert int(out["hitlag"][p]) == int(ref_rollout["hitlag"][p]) == 0
    assert int(out["hitstun"][p]) == int(ref_rollout["hitstun"][p]) == 32
    assert int(out["on_ground"][p]) == int(ref_rollout["on_ground"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref_rollout["pos_y"][p]), abs=3e-6)


def test_damageflyn_hitlag_exit_floorhug_requires_current_ecb_floor_hit_synthetic() -> None:
    # Committed package guard for the DamageFlyN hitlag-exit floorhug boundary:
    # a DamageFly_Coll post-hitlag row may carry the damage callback/floorhug context, but it must
    # not enter Passive or snap grounded unless the current ECB floor pass actually accepts a floor.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnEveryHitlag,ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.zeros((1, seed_stride), dtype=np.uint8)
    seed = seed_bytes.view(SEED_DTYPE).reshape(-1)
    p = 0
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["char_id"][0, :2] = np.uint8(1)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["action_id"][0, p] = np.uint16(88)  # DamageFlyN
    seed["animation_index"][0, p] = np.uint32(178)  # ftCo_SM_DamageFlyN
    seed["action_frame"][0, p] = np.int16(4)
    seed["on_ground"][0, p] = np.uint8(0)
    seed["ground_id"][0, p] = np.uint16(0)
    seed["pos_y"][0, p] = np.float32(10.0)
    seed["hitlag"][0, p] = np.uint16(1)
    seed["hitstun"][0, p] = np.uint16(33)
    seed["damage_post_hitlag_cb_kind"][0, p] = np.uint8(1)

    prev_input = np.zeros((1, input_stride), dtype=np.uint8).view(INPUT_DTYPE).reshape((1,))
    cur_input = np.zeros((1, input_stride), dtype=np.uint8).view(INPUT_DTYPE).reshape((1,))
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(
            handle,
            prev_input.view(np.uint8).reshape((1, input_stride)),
            cur_input.view(np.uint8).reshape((1, input_stride)),
        )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][p]) == 88
    assert int(out["hitlag"][p]) == 0
    assert int(out["hitstun"][p]) == 33
    assert int(out["on_ground"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(9.77, abs=1e-6)


@pytest.mark.integration
def test_damageflytop_active_hitlag_floorhug_does_not_trigger_early_qgd_9682() -> None:
    # Negative control on the immediately preceding row:
    # - same DamageFlyTop family, still active hitlag
    # - no downward SDI input yet
    # - vanilla does not raise FloorPush|FloorHug and does not correct Y
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[9682 : 9683]
    p = 1

    assert int(row["seed_t"]["action_id"][0, p]) == 90
    assert int(row["seed_t"]["hitlag"][0, p]) == 3
    assert int(row["input_t"]["p"]["main_y"][0, p]) == 0
    assert float(row["seed_t"]["pos_y"][0, p]) == pytest.approx(-3.459904909133911, abs=1e-6)
    assert float(row["ref_t1"]["pos_y"][0, p]) == pytest.approx(-3.459904909133911, abs=1e-6)

    out, ref, contacts, colldata = _run_one_step_with_contacts_and_colldata(dataset_path, 9682)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 90
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 2
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0
    assert int(colldata["floor_result_source"][p]) == FLOOR_RESULT_NONE
    assert int(colldata["floor_result_mode"][p]) == FLOOR_MODE_NONE


@pytest.mark.integration
def test_damageflytop_active_hitlag_floorhug_does_not_snap_to_ledge_floor_agn_4839() -> None:
    # Negative lock from AGN rec=4839 p1:
    # - active hitlag DamageFlyTop with down-right input near FD's left ledge floor segment
    # - vanilla stays airborne below the stage and does not raise FloorPush|FloorHug on this row
    # - keep the hitlag floorhug owner restricted to the proven main-floor continuation class
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[4839 : 4840]
    p = 1

    assert int(row["seed_t"]["action_id"][0, p]) == 90
    assert int(row["seed_t"]["hitlag"][0, p]) == 3
    assert int(row["seed_t"]["ground_id"][0, p]) == 1
    assert int(row["input_t"]["p"]["main_x"][0, p]) == 77
    assert int(row["input_t"]["p"]["main_y"][0, p]) == -79
    assert float(row["seed_t"]["pos_y"][0, p]) == pytest.approx(-2.0190019607543945, abs=1e-6)
    assert float(row["ref_t1"]["pos_y"][0, p]) == pytest.approx(-2.0190019607543945, abs=1e-6)

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 4839)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 90
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 2
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0


@pytest.mark.integration
def test_damageflyn_from_damageair_active_hitlag_floorhug_projects_root_pec_870() -> None:
    # DamageAir -> DamageFly active-hitlag floorhug:
    # a same-frame ProcessHit can enter DamageFlyN from a prior DamageAir collision state while the
    # victim root is below Yoshi's main hard floor. The first DamageFly_Coll callback still owns the
    # ft_80081DD4/mpColl_800477E0 stay-airborne floor path and projects FloorPush|FloorHug without
    # changing the fighter to grounded. This is source-family ownership, not a Yoshi row special:
    # the paired BHH control below enters DamageFlyN from a grounded attack and must not receive
    # this DamageAir re-entry floor authority.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_8008DCE0,ftCo_Damage_Coll,ftCo_DamageFly_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044948_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "PhysicalElectricCapybara.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[870:871]
    p = 0

    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_FLY_N
    assert int(row["seed_t"]["seed_prev_action_id"][0, p]) == ACT_DAMAGE_AIR_3
    assert int(row["seed_t"]["action_frame"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 6
    assert float(row["seed_t"]["pos_y"][0, p]) < 0.0
    assert int(row["ref_t1"]["on_ground"][0, p]) == 0
    assert float(row["ref_t1"]["pos_y"][0, p]) == pytest.approx(
        0.00010013580322265625, abs=1e-6
    )

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 870)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_N
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 5
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK


@pytest.mark.integration
def test_damageflyn_entry_from_ground_attack_does_not_gain_damageair_floorhug_bhh_1600() -> None:
    # Negative boundary for the DamageAir -> DamageFly entry owner above:
    # BHH enters DamageFlyN from a grounded AttackS4S hit with the root already at floor height.
    # Vanilla stays airborne with no FloorPush/FloorHug env flags. Do not broaden the PEC owner to
    # all first-frame DamageFly entries or to replay-visible hitlag state alone.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "BlondHardHippopotamus.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[1600:1601]
    p = 1

    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_FLY_N
    assert int(row["seed_t"]["seed_prev_action_id"][0, p]) == 56  # AttackS4S
    assert int(row["seed_t"]["hitlag"][0, p]) == 6
    assert int(row["seed_t"]["on_ground"][0, p]) == 0
    assert float(row["seed_t"]["pos_y"][0, p]) == pytest.approx(
        0.00009999999747378752, abs=1e-7
    )

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 1600)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_N
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 5
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0


@pytest.mark.integration
def test_damageair_active_hitlag_sdi_does_not_snap_to_dream_land_platform_feh_11342() -> None:
    # Negative lock from FEH rec=11342 p1:
    # - active hitlag DamageAir2 receives a fresh down-left SDI input below Dream Land's top
    #   platform,
    # - vanilla applies the SDI displacement but does not raise FloorPush/FloorHug against the soft
    #   platform because mpColl_80044628_Floor has no current bottom-sweep platform hit,
    # - a carried CollData floor.index may continue hard-floor damage floorhug rows, but it must not
    #   synthesize a soft-platform projection from replay-visible ground_id alone.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        "FlippantEnchantedHorse.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[11342:11343]
    p = 1

    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_AIR_2
    assert int(row["seed_t"]["hitlag"][0, p]) == 4
    assert int(row["seed_t"]["ground_id"][0, p]) == 2
    assert int(row["input_t"]["p"]["main_x"][0, p]) < 0
    assert int(row["input_t"]["p"]["main_y"][0, p]) < 0
    assert float(row["seed_t"]["pos_y"][0, p]) < 51.0
    assert float(row["ref_t1"]["pos_y"][0, p]) < float(row["seed_t"]["pos_y"][0, p])

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 11342)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_AIR_2
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 3
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0


@pytest.mark.integration
def test_damageflytop_downward_sdi_floorhug_uses_consumed_sdi_latch_agn_794() -> None:
    # Positive lock for the same active-hitlag floorhug owner using committed validation data:
    # - ftCo_Damage_OnEveryHitlag consumes a downward SDI input and then resets x670/x671 to 0xFE,
    # - the following DamageFly_Coll/mpColl floor pass must still know the callback consumed the
    #   downward input and project back to the persisted non-ledge floor while staying airborne.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044948_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[794:795]
    p = 1

    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_FLY_TOP
    assert int(row["seed_t"]["hitlag"][0, p]) == 5
    assert int(row["input_t"]["p"]["main_y"][0, p]) < 0
    assert int(row["seed_t"]["tilt_timer_y"][0, p]) == 0
    # Dolphin probe evidence in reports/triage/active_damage_agn794_forensic/ shows the first
    # visible DamageFlyTop hitlag row still has the pre-Damage AttackAirLw loaded CollData ECB.
    # This seed lane carries that hidden CollData pose; it is not the visible DamageFlyTop ECB.
    assert int(row["seed_t"]["damage_hitlag_ecb_valid_u8"][0, p]) == 1
    assert float(row["seed_t"]["damage_hitlag_ecb_bottom_rel_y_f32"][0, p]) == pytest.approx(
        4.8131137, abs=1e-5
    )
    assert float(row["ref_t1"]["pos_y"][0, p]) == pytest.approx(0.00010013580322265625, abs=1e-6)

    out, ref, contacts, colldata = _run_one_step_with_contacts_and_colldata(dataset_path, 794)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 4
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert int(colldata["current_valid"][p]) == 1
    assert float(colldata["current_bottom_rel_y"][p]) == pytest.approx(
        float(row["seed_t"]["damage_hitlag_ecb_bottom_rel_y_f32"][0, p]), abs=1e-6
    )
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "expected_action"),
    [
        (455, ACT_DAMAGE_AIR_3),
        (4106, ACT_DAMAGE_AIR_2),
        (8123, ACT_DAMAGE_AIR_3),
    ],
)
def test_thrownlw_release_damage_active_hitlag_floorhug_projects_root_qgd(
    record: int, expected_action: int
) -> None:
    # ThrowLw release can enter common Damage while the victim root is already below the persisted
    # floor. The next active-hitlag Damage_Coll floor pass refreshes FloorPush|FloorHug from the
    # release sweep even without a new SDI input. Keep this distinct from the downward-SDI floorhug
    # owner above.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    p = 1

    assert int(row["seed_t"]["action_id"][0, p]) == expected_action
    assert int(row["seed_t"]["seed_prev_action_id"][0, p]) == ACT_THROWN_LW
    assert int(row["seed_t"]["hitlag"][0, p]) > 0
    assert int(row["input_t"]["p"]["main_y"][0, p]) == 0
    assert float(row["seed_t"]["pos_y"][0, p]) < 0.0
    assert float(row["seed_t"]["floor_sweep_prev_pos_y_f32"][0, p]) > float(
        row["seed_t"]["pos_y"][0, p]
    )
    assert float(row["ref_t1"]["pos_y"][0, p]) == pytest.approx(0.00010013580322265625, abs=1e-6)

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, record)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == expected_action
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK


@pytest.mark.integration
def test_horizontal_only_hitlag_sdi_at_floor_height_does_not_false_land_maj_3612() -> None:
    # Negative lock for the teacher-forced full CollData.prev_pos x/y floor-sweep lane.
    # This row has active hitlag DamageN2 at FD floor height and same-Y horizontal SDI/ASDI
    # displacement. mpCheckFloor is allowed to see the full prev_pos -> cur_pos segment, but the
    # Damage hitlag callback must not synthesize a grounded Landing/FloorPush result from a
    # horizontal-only floor-height sweep.
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80043754,mpCheckFloor}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "MotionlessAggressiveJay.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[3612:3613]
    p = 1

    assert int(row["seed_t"]["action_id"][0, p]) == 79
    assert int(row["seed_t"]["hitlag"][0, p]) == 5
    assert int(row["seed_t"]["on_ground"][0, p]) == 0
    assert int(row["ref_t1"]["on_ground"][0, p]) == 0
    assert float(row["seed_t"]["pos_y"][0, p]) == pytest.approx(0.0001, abs=1e-6)
    assert float(row["ref_t1"]["pos_y"][0, p]) == pytest.approx(
        float(row["seed_t"]["pos_y"][0, p]), abs=1e-6
    )
    assert float(row["seed_t"]["floor_sweep_prev_pos_x_f32"][0, p]) == pytest.approx(
        float(row["seed_t"]["pos_x"][0, p]), abs=1e-6
    )
    assert float(row["seed_t"]["floor_sweep_prev_pos_y_f32"][0, p]) == pytest.approx(
        float(row["seed_t"]["pos_y"][0, p]), abs=1e-6
    )
    assert abs(float(row["ref_t1"]["pos_x"][0, p]) - float(row["seed_t"]["pos_x"][0, p])) > 5.9

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 3612)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 79
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 4
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0


@pytest.mark.integration
def test_nonfd_damageair_root_below_floor_current_ecb_bottom_above_does_not_land_cdo_5521() -> None:
    # Non-FD DamageAir floor publication negative:
    # engine probes for this Pokemon Stadium row show ft_80081DD4 -> mpColl_800477E0 loads JObj
    # ECB (kind=1), with no x130 lock and a positive current ECB bottom. The fighter root is below
    # the hard floor in the next row, but vanilla keeps DamageAir2 airborne because
    # mpColl_80044628_Floor has not yet accepted the loaded ECB bottom sweep. Do not admit this
    # family from visible floor id + below-root/root-projection alone.
    # probe artifact: reports/triage/nonfd_damage_ecb_dbg_cdo5521/
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_800477E0,mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044948_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/CornyDelayedOkapi.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[5521:5522]
    p = 1

    assert int(row["seed_t"]["stage_id"][0]) == 3
    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_AIR_2
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["on_ground"][0, p]) == 0
    assert int(row["seed_t"]["damage_hitlag_ecb_valid_u8"][0, p]) == 0
    assert float(row["seed_t"]["pos_y"][0, p]) < 0.0
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_DAMAGE_AIR_2
    assert int(row["ref_t1"]["on_ground"][0, p]) == 0

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 5521)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_AIR_2
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0


@pytest.mark.integration
def test_nonfd_damageair_hard_floor_exit_publication_stays_exact_tvr_5994() -> None:
    # Non-FD hard-floor positive:
    # This Pokemon Stadium row is the paired source-positive control for the CDO negative above.
    # Vanilla transitions DamageAir2 -> DamageN2 with grounded FloorPush|FloorHug after the source
    # callback bottom-sweep/projection sequence; keep that hitlag-exit publication exact without
    # broadening active-hitlag root projection.
    # probe artifact: reports/triage/nonfd_damage_ecb_dbg_tvr5994b/
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/ThisVioletRaccoon.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[5994:5995]
    p = 1

    assert int(row["seed_t"]["stage_id"][0]) == 3
    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_AIR_2
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_DAMAGE_N_2
    assert int(row["ref_t1"]["hitlag"][0, p]) == 3
    assert int(row["ref_t1"]["on_ground"][0, p]) == 1

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 5994)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_N_2
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 3
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK


@pytest.mark.integration
def test_damageair_fd_hard_floor_hitlag_exit_projection_stays_exact_fsp_3593() -> None:
    # FD hard-floor positive and root-vs-bottom boundary companion:
    # FSP lands on hitlag exit through the same ft_80081DD4/mpColl floor publication family. This
    # keeps the existing FD exactness and ensures the CDO non-FD negative above is not implemented
    # by deleting DamageAir hard-floor publication wholesale.
    # probe artifact: reports/triage/nonfd_damage_ecb_dbg_fsp3593b/
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[3593:3594]
    p = 1

    assert int(row["seed_t"]["stage_id"][0]) == 32
    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_AIR_2
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_LANDING
    assert int(row["ref_t1"]["on_ground"][0, p]) == 1

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 3593)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_LANDING
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK


@pytest.mark.integration
def test_damagefly_hitlag_exit_uses_current_loaded_ecb_not_next_stored_pose_stm_9002() -> None:
    # DamageFly hitlag-exit current-ECB positive:
    # `Fighter_8006D10C` consumes `ftCo_Damage_OnExitHitlag` before map, then
    # `ftCo_DamageFly_Coll -> ft_80081DD4 -> mpColl_800473CC` loads the current DamageFlyN ECB for
    # `mpColl_80044628_Floor`. The following-frame CollData pose is stored after the floor pass, but
    # using it as the exit-frame sweep source leaves this Pokemon Stadium row falsely airborne.
    #
    # Paired negative: QGD 5120 has the same DamageFlyN hitlag-exit shape but the current loaded ECB
    # bottom remains above FD's hard floor, so it must stay airborne. Keep both rows off stage-id
    # branches; the distinction is source ECB bottom-sweep provenance.
    #
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006D10C}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_LoadECB_inline,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local dataset: SweatyThisMallard.msl")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[9002:9003]
    p = 0

    assert int(row["seed_t"]["stage_id"][0]) == 3
    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_FLY_N
    assert int(row["seed_t"]["hitlag"][0, p]) == 1
    assert int(row["seed_t"]["damage_post_hitlag_cb_kind"][0, p]) == 1
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_DOWN_BOUND_U
    assert int(row["ref_t1"]["on_ground"][0, p]) == 1

    out, ref, contacts, colldata = _run_one_step_with_contacts_and_colldata(dataset_path, 9002)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DOWN_BOUND_U
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-4)
    assert int(colldata["floor_result_valid"][p]) == 1
    assert int(colldata["floor_result_segment_id"][p]) == int(ref["ground_id"][p])
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK
    current_bottom_world = float(colldata["substep_cur_pos_y"][p]) + float(
        colldata["current_bottom_rel_y"][p]
    )
    assert current_bottom_world <= 0.0


@pytest.mark.integration
def test_damage_active_hitlag_floor_sweep_lifetime_lands_on_real_contact_stm_8729() -> None:
    # Active Damage hitlag floor-sweep lifetime:
    # The Damage hitlag callbacks keep their damage-entry CollData ECB owner until the hitlag-exit
    # floor callback accepts a real bottom sweep. Post-frame publication must not synthesize or
    # overwrite that sweep from the frozen previous root while no contact exists, but the actual
    # STM 8729 floor contact must still publish Landing on the accepted Pokemon Stadium floor.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnEveryHitlag,ftCo_DamageFly_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local dataset: SweatyThisMallard.msl")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[8729:8730]
    p = 0
    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_HI_3
    assert int(row["seed_t"]["hitlag"][0, p]) == 1
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_LANDING
    assert int(row["ref_t1"]["ground_id"][0, p]) == 52

    out, ref, contacts, colldata = _run_one_step_with_contacts_and_colldata(dataset_path, 8729)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_LANDING
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-7)
    assert int(colldata["floor_result_valid"][p]) == 1
    assert int(colldata["floor_result_segment_id"][p]) == int(ref["ground_id"][p])
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "player", "expected_downbound"),
    (
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/ShadyDecimalStarling.msl",
            322,
            1,
            ACT_DOWN_BOUND_U,
        ),
            (
                "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl",
                7569,
                0,
                ACT_PASSIVE,
            ),
    ),
)
def test_damageflytop_hitlag_exit_resting_hard_floor_carries_floorhug_latch(
    dataset_rel: str, record: int, player: int, expected_downbound: int
) -> None:
    # DamageFlyTop resting hard-floor hitlag-exit owner:
    # active hitlag can freeze the fighter with `ground_or_air` airborne while CollData.floor names
    # the hard floor and FloorPush/FloorHug has already been refreshed by the DamageFly callback.
    # On the hitlag-exit row, `ftCo_Damage_OnExitHitlag -> DamageFly_Coll -> ft_80081DD4` consumes
    # that callback-local floor state and enters DownBound. The source boundary is floor data and
    # loaded-bottom acceptance, not a stage id: Dream Land and Yoshi's main hard floors both qualify.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    p = player
    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_FLY_TOP
    assert int(row["seed_t"]["hitlag"][0, p]) == 1
    assert int(row["seed_t"]["damage_post_hitlag_cb_kind"][0, p]) == 1
    assert int(row["ref_t1"]["action_id"][0, p]) == expected_downbound

    out, ref, contacts, colldata = _run_one_step_with_contacts_and_colldata(dataset_path, record)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == expected_downbound
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert int(colldata["floor_result_valid"][p]) == 1
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK


@pytest.mark.integration
def test_damageflytop_resting_floorhug_owner_does_not_apply_to_damageflyroll() -> None:
    # Boundary for the resting hard-floor owner above: DamageFlyRoll has separate RNG/roll physics
    # and must not inherit the DamageFlyTop hitlag-exit latch merely because visible root/floor
    # geometry looks similar.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/dream_land_recent/ShadyDecimalStarling.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local dataset: ShadyDecimalStarling.msl")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples[322:323].copy()
    p = 1
    samples["seed_t"]["action_id"][0, p] = np.uint16(ACT_DAMAGE_FLY_ROLL)
    samples["seed_t"]["animation_index"][0, p] = np.uint32(181)  # ftCo_SM_DamageFlyRoll
    samples["ref_t1"]["action_id"][0, p] = np.uint16(ACT_DAMAGE_FLY_ROLL)
    out, _ref, contacts = _run_one_step_with_contacts_from_samples(
        samples, int(ds.header["num_players"]), 0
    )

    assert int(out["action_id"][p]) == ACT_DAMAGE_FLY_ROLL
    assert int(out["on_ground"][p]) == 0
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0


@pytest.mark.integration
def test_damageflytop_resting_floorhug_owner_requires_frame_start_floor_contact() -> None:
    # Boundary for the resting hard-floor owner: a carried floor id and previous floor-sweep root
    # are not enough if the fighter is already below the floor at frame start. Source CollData lacks
    # the same resting FloorHug state, so the row stays airborne instead of entering DownBound.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 999
    p = 1
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_FLY_TOP
    assert int(row["seed_t"]["hitlag"][0, p]) == 1
    assert int(row["seed_t"]["damage_post_hitlag_cb_kind"][0, p]) == 1
    assert float(row["seed_t"]["floor_sweep_prev_pos_y_f32"][0, p]) == pytest.approx(
        0.0001, abs=1e-6
    )
    assert float(row["seed_t"]["pos_y"][0, p]) < -1.0
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_DAMAGE_FLY_TOP

    out, ref, contacts, colldata = _run_one_step_with_contacts_and_colldata(dataset_path, record)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(colldata["floor_result_valid"][p]) == 0
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0


@pytest.mark.integration
def test_damageflytop_resting_floorhug_owner_does_not_apply_to_damageflyn() -> None:
    # DamageFlyN has separate active-hitlag/loaded-bottom floor owners. A frame-start floor-resting
    # row in DamageFlyN must not inherit the DamageFlyTop-specific resting hard-floor hitlag-exit
    # handoff.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/battlefield_recent/DelayedSuperbGuanaco.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 781
    p = 1
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_FLY_N
    assert int(row["seed_t"]["hitlag"][0, p]) == 1
    assert int(row["seed_t"]["damage_post_hitlag_cb_kind"][0, p]) == 1
    assert float(row["seed_t"]["pos_y"][0, p]) == pytest.approx(0.0001, abs=1e-6)
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_DAMAGE_FLY_N

    out, ref, contacts, colldata = _run_one_step_with_contacts_and_colldata(dataset_path, record)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_N
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(colldata["floor_result_valid"][p]) == 0
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "player", "expect_floor_result"),
    (
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            5521,
            1,
            False,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            5994,
            1,
            True,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "FavorableSuperficialPig.msl",
            3593,
            1,
            True,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "SweatyThisMallard.msl",
            9002,
            0,
            True,
        ),
    ),
)
def test_damageair_hard_floor_publication_follows_loaded_ecb_bottom_sweep_provenance(
    dataset_rel: str, record: int, player: int, expect_floor_result: bool
) -> None:
    # Source-boundary lock for the non-FD DamageAir hard-floor publication blocker:
    # `ft_80081DD4 -> mpColl_800477E0` loads the DamageAir JObj ECB before
    # `mpColl_80044628_Floor`. CDO has a root below the floor but the loaded current ECB bottom is
    # still above the hard floor, so no callback floor result is source-owned. TVR/FSP cross the
    # hard floor with the loaded ECB bottom and publish the direct floor result before the Damage
    # state transition.
    #
    # probe artifacts:
    # - reports/triage/nonfd_damage_ecb_dbg_cdo5521/
    # - reports/triage/nonfd_damage_ecb_dbg_tvr5994b/
    # - reports/triage/nonfd_damage_ecb_dbg_fsp3593b/
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_800477E0,mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    out, ref, contacts, colldata = _run_one_step_with_contacts_and_colldata(dataset_path, record)
    p = player

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert int(colldata["current_valid"][p]) == 1
    assert int(colldata["prev_valid"][p]) == 1

    current_bottom_world = float(colldata["substep_cur_pos_y"][p]) + float(
        colldata["current_bottom_rel_y"][p]
    )
    prev_bottom_world = float(colldata["substep_prev_pos_y"][p]) + float(
        colldata["prev_bottom_rel_y"][p]
    )
    if expect_floor_result:
        assert int(colldata["floor_result_valid"][p]) == 1
        assert int(colldata["floor_result_mode"][p]) == FLOOR_MODE_BOTTOM_SWEEP
        assert int(colldata["floor_result_segment_id"][p]) == int(ref["ground_id"][p])
        assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK
        assert prev_bottom_world > 0.0
        assert current_bottom_world <= 0.0
    else:
        assert int(colldata["floor_result_valid"][p]) == 0
        assert int(colldata["floor_result_mode"][p]) == FLOOR_MODE_NONE
        assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0
        assert current_bottom_world > 0.0


@pytest.mark.integration
def test_downed_contact_downbound_entry_preserves_frame_start_airborne_ground_or_air_fsp_3713() -> None:
    # Downed-contact source-order lock:
    # The sim's map pass can refresh a carried DownBound floor before combat resolves AttackAirLw
    # contact, but source ftCo_8009F0F0/ftCo_8009F184 installs DownDamageU/D from the current
    # damage/contact callback lifetime and does not turn a frame-start airborne DownBound into a
    # grounded publication. CollData.floor remains available for the next DownDamage_Coll callback.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{
    #   ftCo_8009F0F0,ftCo_8009F184,ftCo_DownDamage_Coll}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[3713:3714]
    p = 0

    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DOWN_BOUND_U
    assert int(row["seed_t"]["action_frame"][0, p]) == 21
    assert int(row["seed_t"]["on_ground"][0, p]) == 0
    assert int(row["seed_t"]["ground_id"][0, p]) == 1
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_DOWN_DAMAGE_D
    assert int(row["ref_t1"]["on_ground"][0, p]) == 0

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 3713)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DOWN_DAMAGE_D
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 1
    assert float(out["speed_y_attack"][p]) == pytest.approx(float(ref["speed_y_attack"][p]), abs=1e-6)
    assert float(out["speed_x_attack"][p]) == pytest.approx(float(ref["speed_x_attack"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert list(map(int, out["state_flags"][p])) == list(map(int, ref["state_flags"][p]))
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK


@pytest.mark.integration
def test_downed_contact_forced_tumble_does_not_apply_to_ordinary_grounded_damage_fsp_3713() -> None:
    # Boundary control for the explicit-DownDamage severity owner above. The same contact geometry
    # retargeted to an ordinary grounded action must remain on the normal low-severity grounded KB
    # projection path instead of inheriting DownDamage's forced-tumble bounce.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[3713]
    p = 0
    seed = row["seed_t"].copy()
    seed["action_id"][p] = 14  # Wait.
    seed["action_frame"][p] = 1
    seed["on_ground"][p] = 1

    out = _run_one_step_seed_arrays(
        seed, row["prev_input_t"].copy(), row["input_t"].copy(), int(ds.header["num_players"])
    )

    assert int(out["action_id"][p]) == ACT_DAMAGE_HI_2
    assert int(out["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(seed["ground_id"][p]) == 1
    assert float(out["speed_y_attack"][p]) == pytest.approx(0.0, abs=1e-6)
    assert float(out["speed_x_attack"][p]) != pytest.approx(0.0, abs=1e-6)
