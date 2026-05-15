from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset, read_dataset_window


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
ACT_DAMAGE_AIR_2 = 0x0055
ACT_DAMAGE_AIR_3 = 0x0056
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_THROWN_LW = 0x00F2


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

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 9683)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 90
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 1
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK


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

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 9682)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 90
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 2
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert (int(contacts["coll_env_flags"][p]) & MSL_COLLIDE_FLOOR_MASK) == 0


@pytest.mark.integration
def test_damageflytop_active_hitlag_floorhug_does_not_snap_to_ledge_floor_agn_4839() -> None:
    # Negative lock from AGN rec=4839 p1:
    # - active hitlag DamageFlyTop with down-right input near FD's left ledge floor segment
    # - vanilla stays airborne below the stage and does not raise FloorPush|FloorHug on this row
    # - keep the hitlag floorhug bridge restricted to the proven main-floor continuation class
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
    assert float(row["ref_t1"]["pos_y"][0, p]) == pytest.approx(0.00010013580322265625, abs=1e-6)

    out, ref, contacts = _run_one_step_with_contacts(dataset_path, 794)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 4
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
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
