from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, read_dataset


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
    ],
    align=False,
)

MSL_COLLIDE_FLOOR_MASK = 0x18000


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

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
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
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
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
