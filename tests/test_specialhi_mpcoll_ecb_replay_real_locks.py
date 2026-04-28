from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


QGD = (
    "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
    "QuerulousGrandDinosaur.msl"
)
BHH = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/"
    "BlondHardHippopotamus.msl"
)
TCH = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/"
    "TubbyCurlyHerring.msl"
)
GAT = (
    "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
    "GracefulAttachedTurtle.msl"
)
MAJ = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/"
    "MotionlessAggressiveJay.msl"
)
HVG = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/"
    "HilariousVillainousGiraffe.msl"
)


def _run_row(
    record: int, dataset_rel: str = QGD
) -> tuple[np.void, np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    root = Path(__file__).resolve().parents[1]
    ds = read_dataset(str(root / dataset_rel))
    row = ds.samples[record : record + 1]

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    contact_dtype = np.dtype(
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
    contact_bytes = np.zeros((1, contact_dtype.itemsize), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        binding.debug_write_collision_contacts(handle, contact_bytes)
    finally:
        binding.destroy(handle)

    got = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    contacts = contact_bytes.view(contact_dtype).reshape(-1)[0].copy()
    return row["seed_t"][0], row["ref_t1"][0], got, contacts


def _collision_contact_dtype() -> np.dtype:
    return np.dtype(
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


def _run_rollout_rows(
    start_record: int, end_record: int, dataset_rel: str = QGD
) -> dict[int, tuple[np.void, np.void, np.void]]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    root = Path(__file__).resolve().parents[1]
    ds = read_dataset(str(root / dataset_rel))
    rows = ds.samples[start_record : end_record + 1]

    def _bytes(name: str, i: int, stride: int) -> np.ndarray:
        return np.frombuffer(rows[i : i + 1][name].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, stride
        )

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    contact_dtype = _collision_contact_dtype()
    contact_bytes = np.zeros((1, contact_dtype.itemsize), dtype=np.uint8)
    got_by_record: dict[int, tuple[np.void, np.void, np.void]] = {}
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, _bytes("seed_t", 0, seed_stride))
        for i, record in enumerate(range(start_record, end_record + 1)):
            binding.step_input(
                handle,
                _bytes("prev_input_t", i, input_stride),
                _bytes("input_t", i, input_stride),
            )
            binding.write_compare(handle, out_bytes)
            binding.debug_write_collision_contacts(handle, contact_bytes)
            got = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            contacts = contact_bytes.view(contact_dtype).reshape(-1)[0].copy()
            got_by_record[record] = (got, rows["ref_t1"][i].copy(), contacts)
    finally:
        binding.destroy(handle)
    return got_by_record


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "wall_id"),
    [
        (9372, 10),
        (9373, 10),
        (9374, 9),
    ],
)
def test_specialairhi_right_wall_jobj_ecb_envelope_matches_qgd_positive(
    record: int, wall_id: int
) -> None:
    # Decomp owner:
    # - SpecialAirHi_Coll -> ft_CheckGroundAndLedge -> mpColl_800473CC.
    # - mpColl_LoadECB_JObj samples the live FtPart_XRotN-rotated collision joints.
    # - mpColl_800454A4_RightWall resolves the airborne right-wall envelope after the sweep.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_800454A4_RightWall}
    _, ref, got, contacts = _run_row(record)

    assert int(got["action_id"][0]) == int(ref["action_id"][0]) == 356
    assert int(got["action_id"][1]) == int(ref["action_id"][1])
    assert int(got["hitlag"][1]) == int(ref["hitlag"][1])
    assert int(contacts["wall_kind"][0]) == 2
    assert int(contacts["wall_id"][0]) == wall_id
    assert int(contacts["damage_hitlag_wall_asdi_latch"][0]) == 0
    assert float(got["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=1e-5)
    assert float(got["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)


@pytest.mark.integration
def test_specialairhi_jobj_ecb_does_not_create_precontact_wall_on_qgd_control() -> None:
    _, ref, got, contacts = _run_row(9371)

    assert int(got["action_id"][0]) == int(ref["action_id"][0]) == 356
    assert int(contacts["coll_env_flags"][0]) == 0
    assert int(contacts["wall_kind"][0]) == 0
    np.testing.assert_allclose(got["pos_x"], ref["pos_x"], atol=1e-6)
    np.testing.assert_allclose(got["pos_y"], ref["pos_y"], atol=1e-6)


@pytest.mark.integration
def test_specialairhi_horizontal_right_wall_keeps_centered_ecb_control() -> None:
    # Horizontal Firefox/Firebird has an identity XRotN launch angle; this stays on the root-Y
    # facing basis and mpColl_LoadECB_JObj x12C recentering rather than the upward-launch residual.
    _, ref, got, contacts = _run_row(6392, BHH)

    assert int(got["action_id"][0]) == int(ref["action_id"][0]) == 356
    assert int(contacts["wall_kind"][0]) == 2
    assert int(contacts["wall_id"][0]) == 9
    assert float(got["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=1e-6)
    assert float(got["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)


@pytest.mark.integration
def test_specialairhi_right_wall_probe_does_not_overextend_fd_lip_control() -> None:
    # mpCheckRightWall uses raw wall endpoints plus mpLineIntersectionV's local endpoint clamp; it
    # must not use mpLib_8004ED5C's broader linked-line extension and steal this BODY row.
    _, ref, got, contacts = _run_row(2601, TCH)

    assert int(got["action_id"][0]) == int(ref["action_id"][0]) == 86
    assert int(got["hitlag"][0]) == int(ref["hitlag"][0]) == 5
    assert int(contacts["wall_kind"][0]) == 0
    assert float(got["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=1e-6)
    assert float(got["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)


@pytest.mark.integration
def test_specialairhi_right_wall_owner_does_not_broaden_left_wall_control() -> None:
    # The current retained JObj/envelope owner is right-wall-only. LeftWall's symmetric
    # mpColl_80046224 envelope needs its own source-shaped predicate before it can be broadened.
    _, ref, got, contacts = _run_row(3099, GAT)

    assert int(got["action_id"][0]) == int(ref["action_id"][0]) == 356
    assert int(contacts["wall_kind"][0]) == 1
    assert int(contacts["wall_kind"][0]) != 2


@pytest.mark.integration
def test_specialairhi_left_wall_envelope_reduces_maj_rollout_escape() -> None:
    # Source-shaped left-wall counterpart for the launch path:
    # SpecialAirHi_Coll calls the airborne mpColl path; mpColl_80045B74_LeftWall collects
    # side/bottom/top candidates and mpColl_80046224_LeftWall resolves the full ECB envelope.
    # The old point-local left-wall path let this rollout remain in SpecialAirHi past replay's
    # SpecialHiFall handoff and carried a larger X drift through the MAJ cascade.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80045B74_LeftWall,mpColl_80046224_LeftWall}
    rows = _run_rollout_rows(8878, 8922, MAJ)

    got_8917, ref_8917, contacts_8917 = rows[8917]
    got_8918, ref_8918, contacts_8918 = rows[8918]
    got_8922, ref_8922, contacts_8922 = rows[8922]

    assert int(contacts_8917["wall_kind"][1]) == 1
    assert int(contacts_8917["wall_id"][1]) == 11
    assert int(contacts_8918["wall_kind"][1]) == 1
    assert int(contacts_8918["wall_id"][1]) == 11
    assert float(contacts_8917["wall_contact_x"][1]) == pytest.approx(-85.5656967, abs=1e-5)
    assert float(contacts_8918["wall_contact_x"][1]) == pytest.approx(-85.5656967, abs=1e-5)
    # The retained live-JObj basis fixes the left-wall contact identity/envelope and removes the
    # prior rollout X drift. Pin the tiny float residual instead of using a broad tolerance that
    # could hide losing the wall-envelope contact above.
    assert float(got_8917["pos_x"][1] - ref_8917["pos_x"][1]) == pytest.approx(
        7.6293945e-06, abs=1e-6
    )
    assert float(got_8918["pos_x"][1] - ref_8918["pos_x"][1]) == pytest.approx(
        1.5258789e-05, abs=1e-6
    )
    assert int(got_8922["action_id"][1]) == int(ref_8922["action_id"][1]) == 84
    assert int(contacts_8922["wall_kind"][1]) == 0


@pytest.mark.integration
def test_specialairhi_left_wall_hitlag_refresh_prevents_hvg_passivewall_cascade() -> None:
    # HVG's left-wall Firefox/Firebird contact enters DamageFlyHi from a BODY hit while hitlag is
    # active near the wall. Fighter_procMap still runs the collision callback during hitlag, so the
    # DamageFly rows must refresh wall metadata and clear stale SpecialAirHi Hug before hitlag exit.
    # Otherwise the rollout incorrectly enters PassiveWall at the former F04 cascade.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procMap}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    rows = _run_rollout_rows(4780, 4829, HVG)

    got_4795, ref_4795, contacts_4795 = rows[4795]
    got_4796, ref_4796, contacts_4796 = rows[4796]
    got_4801, ref_4801, contacts_4801 = rows[4801]
    got_4804, ref_4804, contacts_4804 = rows[4804]
    got_4829, ref_4829, contacts_4829 = rows[4829]

    assert int(got_4795["action_id"][0]) == int(ref_4795["action_id"][0]) == 87
    assert int(got_4795["hitlag"][0]) == int(ref_4795["hitlag"][0]) == 6
    assert int(contacts_4795["wall_kind"][0]) == 1
    assert int(contacts_4795["wall_id"][0]) == 11
    assert int(contacts_4795["damage_hitlag_wall_asdi_latch"][0]) == 1
    assert int(contacts_4795["coll_env_flags"][0]) & 0x20

    assert int(got_4796["action_id"][0]) == int(ref_4796["action_id"][0]) == 87
    assert int(got_4796["hitlag"][0]) == int(ref_4796["hitlag"][0]) == 5
    assert int(contacts_4796["wall_kind"][0]) == 0
    assert (int(contacts_4796["coll_env_flags"][0]) & 0x20) == 0

    assert int(got_4801["action_id"][0]) == int(ref_4801["action_id"][0]) == 87
    assert int(got_4801["hitlag"][0]) == int(ref_4801["hitlag"][0]) == 0
    assert int(got_4801["hitstun"][0]) == int(ref_4801["hitstun"][0]) == 43
    assert int(contacts_4801["wall_kind"][0]) == 0
    assert int(contacts_4801["damage_hitlag_wall_asdi_latch"][0]) == 0
    assert (int(contacts_4801["coll_env_flags"][0]) & 0x20) == 0
    assert float(got_4801["pos_x"][0] - ref_4801["pos_x"][0]) == pytest.approx(
        0.07463837, abs=1e-6
    )

    assert int(got_4804["action_id"][0]) == int(ref_4804["action_id"][0]) == 87
    assert int(got_4804["hitstun"][0]) == int(ref_4804["hitstun"][0]) == 40
    assert int(contacts_4804["wall_kind"][0]) == 0
    assert (int(contacts_4804["coll_env_flags"][0]) & 0x20) == 0

    assert int(got_4829["action_id"][0]) == int(ref_4829["action_id"][0]) == 0
    assert int(got_4829["stocks"][0]) == int(ref_4829["stocks"][0]) == 2
    assert int(contacts_4829["wall_kind"][0]) == 0


@pytest.mark.integration
def test_damagefly_stale_wall_id_without_wall_latch_does_not_project_asdi() -> None:
    # `CollData.wall_id` persists after detach. Wall ASDI projection on DamageFly hitlag exit must
    # require the phase-local wall-contact latch produced by the same hitlag/collision refresh, not
    # a stale line id left in the seed.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
    rows = _run_rollout_rows(4780, 4804, HVG)
    got_4801, ref_4801, contacts_4801 = rows[4801]
    got_4804, ref_4804, contacts_4804 = rows[4804]
    p = 0

    assert int(got_4801["hitlag"][p]) == int(ref_4801["hitlag"][p]) == 0
    assert int(contacts_4801["wall_kind"][p]) == 0
    assert int(contacts_4801["wall_id"][p]) == 11
    assert int(got_4804["hitlag"][p]) == int(ref_4804["hitlag"][p]) == 0
    assert int(contacts_4804["wall_kind"][p]) == 0
    assert int(contacts_4804["wall_id"][p]) == 11

    first_exit_delta = float(got_4801["pos_x"][p] - ref_4801["pos_x"][p])
    later_stale_delta = float(got_4804["pos_x"][p] - ref_4804["pos_x"][p])
    assert first_exit_delta == pytest.approx(0.07463837, abs=1e-6)
    assert later_stale_delta == pytest.approx(first_exit_delta, abs=1e-6)


@pytest.mark.integration
def test_non_specialairhi_left_wall_does_not_use_specialhi_envelope() -> None:
    # The left-wall envelope is retained only for SpecialAirHi_Coll. Changing the same MAJ wall
    # seed to a generic airborne damage state must not use the SpecialHi envelope path or stamp the
    # MAJ left-wall id.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    ds = read_dataset(str(root / MAJ))
    row = ds.samples[8917:8918].copy()
    row["seed_t"]["action_id"][0, 1] = np.uint16(88)  # DamageFlyN: non-SpecialHi airborne state.
    row["seed_t"]["animation_index"][0, 1] = np.uint32(174)

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    contact_dtype = _collision_contact_dtype()
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    contact_bytes = np.zeros((1, contact_dtype.itemsize), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        binding.debug_write_collision_contacts(handle, contact_bytes)
    finally:
        binding.destroy(handle)

    got = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    contacts = contact_bytes.view(contact_dtype).reshape(-1)[0]
    assert int(contacts["wall_kind"][1]) == 0
    assert int(contacts["wall_id"][1]) == 0xFFFF
    assert float(got["pos_x"][1]) > -84.0
