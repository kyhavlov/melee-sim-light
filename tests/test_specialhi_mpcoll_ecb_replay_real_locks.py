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
HIS = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/"
    "HungryImportantSnake.msl"
)
MGS = (
    "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
    "MilkyGracefulStingray.msl"
)
PTE = (
    "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
    "ParallelTemptingElk.msl"
)
G18447 = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/"
    "Game_20260515T182447_frozenps.msl"
)
IAT = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/"
    "ImpassionedAlarmedTarsier.msl"
)
CNM = (
    "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
    "CheeryNumbMonkey.msl"
)
CDO = (
    "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
    "CornyDelayedOkapi.msl"
)
EWT = (
    "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
    "ElatedWearyTermite.msl"
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
    start_record: int,
    end_record: int,
    dataset_rel: str = QGD,
    *,
    rollout_seed: bool = False,
    replay_frame_rng: bool = False,
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
        if rollout_seed:
            binding.reseed_seed_rollout(handle, _bytes("seed_t", 0, seed_stride))
        else:
            binding.reseed_seed(handle, _bytes("seed_t", 0, seed_stride))
        for i, record in enumerate(range(start_record, end_record + 1)):
            if replay_frame_rng:
                binding.step_input_replay_frame_rng(
                    handle,
                    _bytes("seed_t", i, seed_stride),
                    _bytes("prev_input_t", i, input_stride),
                    _bytes("input_t", i, input_stride),
                )
            else:
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
def test_specialairhi_left_wall_collision_facing_enables_tch_cliffcatch_rollout() -> None:
    # SpecialAirHi_Coll does more than resolve the wall envelope: after a wall/ceiling/floor
    # collision whose normal is within `90 + da->x94_FOX_FIREFOX_BOUND_ANGLE` of self_vel, it writes
    # facing from sign(self_vel.x) and recomputes rotateModel. TCH exposes the owner because the
    # wall contact is exact but the rollout later misses CliffCatch unless the facing flip happens
    # during launch.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
    # data/characters/{fox,falco}.json::firefox_bound_angle_degrees
    _, ref_6989, got_6989, contacts_6989 = _run_row(6989, TCH)
    _, ref_6990, got_6990, contacts_6990 = _run_row(6990, TCH)
    p = 1

    assert int(contacts_6989["wall_kind"][p]) == 1
    assert int(got_6989["action_id"][p]) == int(ref_6989["action_id"][p]) == 356
    assert int(got_6989["facing"][p]) == int(ref_6989["facing"][p]) == 0

    assert int(contacts_6990["wall_kind"][p]) == 1
    assert int(contacts_6990["wall_id"][p]) == 11
    assert int(got_6990["action_id"][p]) == int(ref_6990["action_id"][p]) == 356
    assert int(got_6990["facing"][p]) == int(ref_6990["facing"][p]) == 1

    rows = _run_rollout_rows(6864, 6998, TCH)
    got_6990_r, ref_6990_r, _ = rows[6990]
    got_6998_r, ref_6998_r, contacts_6998_r = rows[6998]

    assert int(got_6990_r["action_id"][p]) == int(ref_6990_r["action_id"][p]) == 356
    assert int(got_6990_r["facing"][p]) == int(ref_6990_r["facing"][p]) == 1
    assert int(got_6998_r["action_id"][p]) == int(ref_6998_r["action_id"][p]) == 252
    assert int(got_6998_r["facing"][p]) == int(ref_6998_r["facing"][p]) == 1
    assert int(contacts_6998_r["coll_env_flags"][p]) & 0x01000000


@pytest.mark.integration
def test_ft80084db0_left_wall_endpoint_one_step_keeps_authoritative_replay_contact() -> None:
    # One-step replay seeds already contain vanilla's current wall state. The stale endpoint-clear
    # owner must not erase that authoritative seed state; otherwise broad one-step pos_x metrics
    # regress across unrelated replays.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
    seed, ref, got, contacts = _run_row(9149, IAT)
    p = 0

    assert int(seed["action_id"][p]) == 356
    assert float(seed["speed_air_x_self"][p]) > 0.0
    assert int(got["action_id"][p]) == int(ref["action_id"][p]) == 356
    assert int(contacts["wall_kind"][p]) == 1
    assert float(got["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)


@pytest.mark.integration
def test_ft80084db0_left_wall_endpoint_rollout_stale_contact_clears_iat_wall_contact() -> None:
    # IAT's free-running SpecialAirHi launch segment can carry MSL's stale persisted left-wall
    # endpoint after replay rollout has advanced past the seed frame. Source clears that endpoint
    # instead of re-clamping through it; broad wall persistence later lets the aerial blaster
    # landing frame hit the runner in rollout.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
    rows = _run_rollout_rows(9143, 9157, IAT, rollout_seed=True, replay_frame_rng=True)
    got_9149, ref_9149, contacts_9149 = rows[9149]
    got_9157, ref_9157, contacts_9157 = rows[9157]
    p = 0

    assert int(got_9149["action_id"][p]) == int(ref_9149["action_id"][p]) == 356
    assert int(contacts_9149["wall_kind"][p]) == 1
    assert int(got_9157["action_id"][p]) == int(ref_9157["action_id"][p]) == 356
    assert int(contacts_9157["wall_kind"][p]) == 0
    assert float(got_9157["pos_x"][p]) == pytest.approx(float(ref_9157["pos_x"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (CNM, 7032, 1),
        (CDO, 8509, 1),
        (EWT, 7592, 0),
    ],
)
def test_specialairhi_leftward_endpoint_keeps_wall_facing_controls(
    dataset_rel: str, record: int, p: int
) -> None:
    # Negative boundary for the endpoint-clear owner: true SpecialAirHi rows whose self velocity is
    # leftward/into the left wall still consume the wall correction and collision-facing write.
    # These are not the stale away-from-wall endpoint case above.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
    seed, ref, got, _contacts = _run_row(record, dataset_rel)

    assert int(seed["action_id"][p]) == 356
    assert float(seed["speed_air_x_self"][p]) <= 0.0
    assert int(got["facing"][p]) == int(ref["facing"][p]) == 0
    assert float(got["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)


@pytest.mark.integration
def test_specialhifall_uses_ft_check_ground_ledge_air_wall_envelope_182447() -> None:
    # SpecialHiFall_Coll is still part of Fox/Falco's `ft_CheckGroundAndLedge` source callback
    # family after the launch action. It uses the full airborne `mpColl_80046904` wall envelope,
    # not the point-local wall probe; otherwise Pokemon Stadium's left wall corner releases the root
    # about 0.067 units too far on this direct one-step row.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiFall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80046904}
    seed, ref, got, contacts = _run_row(852, G18447)
    p = 1

    assert int(seed["action_id"][p]) == int(got["action_id"][p]) == int(ref["action_id"][p]) == 358
    assert int(contacts["wall_kind"][p]) == 1
    assert int(contacts["wall_id"][p]) == 106
    assert float(got["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=1e-6
    )
    assert float(got["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(got["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_specialhi_rotate_model_seed_persists_into_fall_landing_and_bound_rows() -> None:
    # Replay-real seed-lane boundary for the shared SpecialHi XRotN owner. Source writes
    # rotateModel during launch/collision callbacks; Fall/Landing/Bound do not rewrite the JObj
    # rotation, so the hidden pose lane remains valid until a non-SpecialHi motion owns the model.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Anim,
    #   ftFx_SpecialHiLanding_Anim,ftFx_SpecialHiBound_Enter}
    root = Path(__file__).resolve().parents[1]
    cases = (
        (HVG, 4085, 1, 358),  # SpecialHiFall after SpecialAirHi collision
        (HVG, 6545, 1, 357),  # SpecialHiLanding after SpecialHiFall
        (HIS, 561, 1, 359),   # SpecialHiBound after SpecialAirHi rebound entry
    )
    for dataset_rel, record, player, action_id in cases:
        ds = read_dataset(str(root / dataset_rel))
        seed = ds.samples[record]["seed_t"]
        assert int(seed["action_id"][player]) == action_id
        assert int(seed["specialhi_rotate_model_valid_u8"][player]) == 1
        assert np.isfinite(float(seed["specialhi_rotate_model_f32"][player]))


@pytest.mark.integration
def test_specialairhi_stale_endpoint_floor_owner_continues_launch_his_1409() -> None:
    # HIS starts this row with Fox's SpecialAirHi root carrying FD main-floor ground_id after the
    # previous root has already moved past that floor segment's right endpoint. Source
    # ft_CheckGroundAndLedge does not synthesize SpecialHiBound by endpoint-clamping back through
    # the stale carried floor; HIS:559 remains the existing Bound control in
    # test_specialhi_rotate_model_seed_persists_into_fall_landing_and_bound_rows.
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    ds = read_dataset(str(root / HIS))
    seed = ds.samples[1409]["seed_t"]
    ref = ds.samples[1409]["ref_t1"]
    p = 1

    assert int(seed["char_id"][p]) == 1
    assert int(seed["action_id"][p]) == 356
    assert int(seed["action_frame"][p]) == 14
    assert float(seed["pos_y"][p]) < 0.0
    assert int(ref["action_id"][p]) == 356

    rows = _run_rollout_rows(1408, 1409, HIS, rollout_seed=True)
    got, ref, contacts = rows[1409]

    assert int(got["action_id"][p]) == int(ref["action_id"][p]) == 356
    assert int(got["action_frame"][p]) == int(ref["action_frame"][p]) == 15
    assert int(got["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert (int(contacts["coll_env_flags"][p]) & 0x00018000) == 0
    assert float(got["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "player"),
    [
        (PTE, 4554, 0),
        (TCH, 839, 0),
    ],
)
def test_specialairhi_steep_hard_floor_angle_enters_bound(
    dataset_rel: str, record: int, player: int
) -> None:
    # Replay-real positives for the other side of the same source predicate: hard-floor contacts
    # whose floor.normal/self_vel angle is outside the bound threshold must remain publishable so
    # ftFx_SpecialAirHi_Coll can enter SpecialHiBound immediately, even while the launch root is
    # already inside the stage shell.
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound,ftFx_SpecialHiBound_Enter}
    # data/characters/{fox,falco}.json::firefox_bound_angle_degrees
    _, ref, got, contacts = _run_row(record, dataset_rel)

    assert int(got["action_id"][player]) == int(ref["action_id"][player]) == 359
    assert int(got["animation_index"][player]) == int(ref["animation_index"][player]) == 312
    assert int(got["action_frame"][player]) == int(ref["action_frame"][player]) == 1
    assert int(got["on_ground"][player]) == int(ref["on_ground"][player]) == 0
    assert int(contacts["coll_env_flags"][player]) & 0x00018000
    assert float(got["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1e-5)


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
    assert int(contacts_4795["coll_env_flags"][0]) & 0x1
    assert (int(contacts_4795["coll_env_flags"][0]) & 0x20) == 0

    assert int(got_4796["action_id"][0]) == int(ref_4796["action_id"][0]) == 87
    assert int(got_4796["hitlag"][0]) == int(ref_4796["hitlag"][0]) == 5
    assert int(contacts_4796["wall_kind"][0]) == 0
    assert (int(contacts_4796["coll_env_flags"][0]) & 0x20) == 0

    assert int(got_4801["action_id"][0]) == int(ref_4801["action_id"][0]) == 87
    assert int(got_4801["hitlag"][0]) == int(ref_4801["hitlag"][0]) == 0
    assert int(got_4801["hitstun"][0]) == int(ref_4801["hitstun"][0]) == 43
    assert int(contacts_4801["wall_kind"][0]) == 1
    assert int(contacts_4801["damage_hitlag_wall_asdi_latch"][0]) == 1
    assert (int(contacts_4801["coll_env_flags"][0]) & 0x20) == 0
    assert float(got_4801["pos_x"][0]) == pytest.approx(float(ref_4801["pos_x"][0]), abs=1e-6)

    assert int(got_4804["action_id"][0]) == int(ref_4804["action_id"][0]) == 87
    assert int(got_4804["hitstun"][0]) == int(ref_4804["hitstun"][0]) == 40
    assert int(contacts_4804["wall_kind"][0]) == 1
    assert int(contacts_4804["damage_hitlag_wall_asdi_latch"][0]) == 0
    assert (int(contacts_4804["coll_env_flags"][0]) & 0x20) == 0
    assert float(got_4804["pos_x"][0]) == pytest.approx(float(ref_4804["pos_x"][0]), abs=1e-6)

    assert int(got_4829["action_id"][0]) == int(ref_4829["action_id"][0]) == 0
    assert int(got_4829["stocks"][0]) == int(ref_4829["stocks"][0]) == 2
    assert (int(contacts_4829["coll_env_flags"][0]) & 0x20) == 0


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
    assert int(contacts_4801["wall_kind"][p]) == 1
    assert int(contacts_4801["wall_id"][p]) == 11
    assert int(contacts_4801["damage_hitlag_wall_asdi_latch"][p]) == 1
    assert (int(contacts_4801["coll_env_flags"][p]) & 0x20) == 0
    assert int(got_4804["hitlag"][p]) == int(ref_4804["hitlag"][p]) == 0
    assert int(contacts_4804["wall_kind"][p]) == 1
    assert int(contacts_4804["wall_id"][p]) == 13
    assert int(contacts_4804["damage_hitlag_wall_asdi_latch"][p]) == 0
    assert (int(contacts_4804["coll_env_flags"][p]) & 0x20) == 0

    first_exit_delta = float(got_4801["pos_x"][p] - ref_4801["pos_x"][p])
    later_stale_delta = float(got_4804["pos_x"][p] - ref_4804["pos_x"][p])
    assert first_exit_delta == pytest.approx(0.0, abs=1e-6)
    assert later_stale_delta == pytest.approx(0.0, abs=1e-6)


@pytest.mark.integration
def test_same_frame_specialairhi_wall_contact_does_not_stale_project_damagefly_asdi() -> None:
    # HIS has a SpecialAirHi right-wall contact on the same frame Falco is hit into DamageFlyHi.
    # That pre-damage SpecialAirHi_Coll contact must not arm Damage_OnExitHitlag wall-ASDI
    # projection unless DamageFly_Coll observes a wall during active hitlag.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
    rows = _run_rollout_rows(8513, 8519, HIS, rollout_seed=True)
    got_8513, ref_8513, contacts_8513 = rows[8513]
    got_8519, ref_8519, contacts_8519 = rows[8519]
    p = 1

    assert int(got_8513["action_id"][p]) == int(ref_8513["action_id"][p]) == 87
    assert int(got_8513["hitlag"][p]) == int(ref_8513["hitlag"][p]) == 6
    assert int(contacts_8513["wall_kind"][p]) == 2
    assert int(contacts_8513["wall_id"][p]) == 9
    assert int(contacts_8513["damage_hitlag_wall_asdi_latch"][p]) == 0

    assert int(got_8519["hitlag"][p]) == int(ref_8519["hitlag"][p]) == 0
    assert int(contacts_8519["wall_kind"][p]) == 0
    assert int(contacts_8519["wall_id"][p]) == 9
    assert int(contacts_8519["damage_hitlag_wall_asdi_latch"][p]) == 0
    assert float(got_8519["pos_x"][p]) == pytest.approx(float(ref_8519["pos_x"][p]), abs=1e-6)
    assert float(got_8519["pos_y"][p]) == pytest.approx(float(ref_8519["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_specialairhi_rotated_jobj_ecb_delays_fod_left_ledge_catch_until_source_frame() -> None:
    # SpecialAirHi_Coll feeds ft_CheckGroundAndLedge, whose mpColl ledge AABB consumes the live
    # JObj ECB after ftFox_SpecialHi_RotateModel has rotated FtPart_XRotN. Static SSANIM ECB
    # extents are wide enough to catch FoD's left ledge one frame early in MGS:3980; the rotated
    # JObj owner stays airborne there and catches on the next source frame.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_RotateModel}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044164}
    _, ref_3980, got_3980, contacts_3980 = _run_row(3980, MGS)
    _, ref_3981, got_3981, contacts_3981 = _run_row(3981, MGS)
    p = 1

    assert int(got_3980["action_id"][p]) == int(ref_3980["action_id"][p]) == 356
    assert int(contacts_3980["coll_env_flags"][p]) & 0x01000000 == 0
    assert float(got_3980["pos_x"][p]) == pytest.approx(float(ref_3980["pos_x"][p]), abs=1e-6)
    assert float(got_3980["pos_y"][p]) == pytest.approx(float(ref_3980["pos_y"][p]), abs=1e-6)

    assert int(got_3981["action_id"][p]) == int(ref_3981["action_id"][p]) == 252
    assert int(contacts_3981["coll_env_flags"][p]) & 0x01000000
    # CliffCatch entry ordering: ftCliffCommon_80081370 flips facing, ticks the new CliffCatch
    # motion, then ftCo_CliffCatch_Phys snaps to ledge_point + the post-entry TransN frame.
    # refs/melee/src/melee/ft/ftcliffcommon.c::{ftCliffCommon_80081370,ftCo_CliffCatch_Phys}
    assert float(got_3981["pos_x"][p]) == pytest.approx(float(ref_3981["pos_x"][p]), abs=1e-6)
    assert float(got_3981["pos_y"][p]) == pytest.approx(float(ref_3981["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_jumpaerial_locked_desired_bottom_feeds_fod_left_wall_envelope() -> None:
    # Ledge jump leaves CollData_X130_Locked live while JumpAerial_Coll routes through
    # ft_800835B0 -> mpColl_80047E14. Source mpColl_LoadECB_inline preserves desired_ecb.bottom
    # for the wall envelope as well as floor checks; using the pose bottom makes the FoD lip
    # projection too shallow on the first wall-push frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80045B74_LeftWall}
    seed_3998, ref_3998, got_3998, contacts_3998 = _run_row(3998, MGS)
    seed_3999, ref_3999, got_3999, contacts_3999 = _run_row(3999, MGS)
    p = 1

    assert int(seed_3998["action_id"][p]) == int(seed_3999["action_id"][p]) == 27
    assert int(seed_3999["ecb_lock_timer"][p]) != 0
    assert int(seed_3999["ecb_lock_bottom_rel_y_valid_u8"][p]) == 1

    assert int(contacts_3998["wall_kind"][p]) == 1
    assert int(contacts_3998["wall_id"][p]) == 23
    assert float(got_3998["pos_x"][p]) == pytest.approx(float(ref_3998["pos_x"][p]), abs=1e-6)
    assert float(got_3998["pos_y"][p]) == pytest.approx(float(ref_3998["pos_y"][p]), abs=1e-6)

    assert int(contacts_3999["wall_kind"][p]) == 1
    assert int(contacts_3999["wall_id"][p]) == 23
    assert float(got_3999["pos_x"][p]) == pytest.approx(float(ref_3999["pos_x"][p]), abs=1e-6)
    assert float(got_3999["pos_y"][p]) == pytest.approx(float(ref_3999["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_jumpaerial_runtime_preserves_pre_entry_desired_bottom_for_fod_left_wall() -> None:
    # Runtime rollout variant of the same owner:
    # ftCo_JumpAerial_Enter_Basic calls ftCommon_8007D5D4 before Fighter_ChangeMotionState, so
    # CollData_X130_Locked preserves the pre-entry Fall desired_ecb.bottom while JumpAerial_Coll
    # refreshes the current/top/side ECB. If runtime snapshots the lock after entering JumpAerial,
    # the next left-wall projection under-pushes the fighter at MGS:3999.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80045B74_LeftWall}
    rows = _run_rollout_rows(3977, 4002, MGS, rollout_seed=True)
    p = 1
    got_3998, ref_3998, contacts_3998 = rows[3998]
    got_3999, ref_3999, contacts_3999 = rows[3999]
    got_4002, ref_4002, _ = rows[4002]

    assert int(got_3998["action_id"][p]) == int(ref_3998["action_id"][p]) == 27
    assert int(contacts_3998["wall_kind"][p]) == 1
    assert int(contacts_3998["wall_id"][p]) == 23
    assert float(got_3998["pos_x"][p]) == pytest.approx(float(ref_3998["pos_x"][p]), abs=1e-6)

    assert int(got_3999["action_id"][p]) == int(ref_3999["action_id"][p]) == 27
    assert int(contacts_3999["wall_kind"][p]) == 1
    assert int(contacts_3999["wall_id"][p]) == 23
    assert float(got_3999["pos_x"][p]) == pytest.approx(float(ref_3999["pos_x"][p]), abs=1e-6)
    assert float(got_3999["pos_y"][p]) == pytest.approx(float(ref_3999["pos_y"][p]), abs=1e-6)

    # Negative boundary: after the wall-contact frame, the owner only carries the resulting
    # correction through ordinary velocity integration; no new action/ground/floor handoff is
    # fabricated by the lock.
    assert int(got_4002["action_id"][p]) == int(ref_4002["action_id"][p]) == 27
    assert int(got_4002["on_ground"][p]) == int(ref_4002["on_ground"][p]) == 0
    assert float(got_4002["pos_x"][p]) == pytest.approx(float(ref_4002["pos_x"][p]), abs=1e-6)


@pytest.mark.integration
def test_jumpaerial_locked_common_air_wall_persistence_182447() -> None:
    # Game_20260515T182447 exposes the combined source boundary:
    # - JumpAerialB_Coll uses the common-air ft_800835B0 callback. Its mpColl path may project from
    #   the previous CollData left-wall index before the broader airborne envelope; skipping that
    #   persistence lets the Pokemon Stadium left-wall lip release the root about 0.877 units right.
    # The following JumpAerialB -> EscapeAir hard-floor landing remains a separate owner: this lock
    # only proves the retained common-air wall projection that removes the earlier float drift.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{
    #   ftCo_JumpAerial_Coll,ftCo_JumpAerial_IASA}
    # refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_80046224_LeftWall
    rows = _run_rollout_rows(4590, 4598, G18447, rollout_seed=True)
    p = 1

    got_4593, ref_4593, contacts_4593 = rows[4593]
    got_4594, ref_4594, contacts_4594 = rows[4594]
    got_4597, ref_4597, _ = rows[4597]

    assert int(got_4593["action_id"][p]) == int(ref_4593["action_id"][p]) == 27
    assert int(contacts_4593["wall_kind"][p]) == 1
    assert int(contacts_4593["wall_id"][p]) == 108

    assert int(got_4594["action_id"][p]) == int(ref_4594["action_id"][p]) == 27
    assert int(contacts_4594["wall_kind"][p]) == 1
    assert int(contacts_4594["wall_id"][p]) == 106
    assert float(got_4594["pos_x"][p]) == pytest.approx(float(ref_4594["pos_x"][p]), abs=1e-6)
    assert float(got_4594["pos_y"][p]) == pytest.approx(float(ref_4594["pos_y"][p]), abs=1e-6)

    # Negative boundary: the first EscapeAir callback is still airborne; the same owner must not
    # ground immediately on entry.
    assert int(got_4597["action_id"][p]) == int(ref_4597["action_id"][p]) == 236
    assert int(got_4597["on_ground"][p]) == int(ref_4597["on_ground"][p]) == 0


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
