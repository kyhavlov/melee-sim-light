from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views
from tools.eval.streaming_validation import _load_binding


def _skip_if_missing_laser_artifacts(root: Path) -> None:
    if not (root / "data/items/lasers.bin").exists():
        pytest.skip("missing local artifact: data/items/lasers.bin")


def _laser_x138_masks_by_item_type(root: Path, item_type: int) -> tuple[int, int]:
    path = root / "data/items/lasers.bin"
    if not path.exists():
        pytest.skip("missing local artifact: data/items/lasers.bin")
    buf = path.read_bytes()
    assert buf[:8] == b"MSLLASR1"
    version = struct.unpack_from("<I", buf, 8)[0]
    assert version >= 5
    count = struct.unpack_from("<H", buf, 12)[0]
    off = 16
    record_bytes = 226 if version >= 7 else (218 if version >= 6 else 254)
    state0_off = 42 if version >= 6 else 78
    state1_off = 138 if version >= 7 else (130 if version >= 6 else 166)
    for _ in range(int(count)):
        shot_itkind = struct.unpack_from("<H", buf, off + 2)[0]
        if int(shot_itkind) == int(item_type):
            state0 = struct.unpack_from("<H", buf, off + state0_off + 20)[0]
            state1 = struct.unpack_from("<H", buf, off + state1_off + 20)[0]
            return int(state0), int(state1)
        off += record_bytes
    raise AssertionError(f"laser item_type={item_type} not found in {path}")


def _laser_ids(items_row: np.ndarray) -> list[int]:
    out: list[int] = []
    for it in items_row:
        if int(it["exists"]) and int(it["type"]) in (54, 55):
            out.append(int(it["instance_id"]))
    out.sort()
    return out


def _item_by_instance(items_row: np.ndarray, instance_id: int) -> np.void | None:
    for it in items_row:
        if int(it["exists"]) and int(it["instance_id"]) == int(instance_id):
            return it
    return None


def _item_by_instance_and_type(items_row: np.ndarray, instance_id: int, item_type: int) -> np.void | None:
    for it in items_row:
        if int(it["exists"]) and int(it["instance_id"]) == int(instance_id) and int(it["type"]) == int(item_type):
            return it
    return None


def _one_step_out_compare(*, ds, row) -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    finally:
        binding.destroy(handle)


def _rollout_rows(dataset_path: Path, start_record: int, end_record_inclusive: int) -> dict[int, tuple[np.void, np.void]]:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    try:
        seed_bytes[0, :] = seed_u8[start_record, :seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        rows: dict[int, tuple[np.void, np.void]] = {}
        for record in range(start_record, end_record_inclusive + 1):
            prev_input_bytes[0, :] = prev_input_u8[record, :input_stride]
            input_bytes[0, :] = input_u8[record, :input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            rows[record] = (out_view[0].copy(), samples["ref_t1"][record].copy())
        return rows
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_late_specialairnloop_clear_cmd0_rejects_laser_body_overlap() -> None:
    # Replay-real lock for the late aerial Blaster Loop command-script boundary:
    # - MGS:-11/-10 has p1 Fox state0 laser geometry overlapping p0 Falco SpecialAirNLoop after
    #   the loop script has cleared cmd_vars[0], so native keeps the laser alive and does not admit
    #   BODY damage on those terminal no-repeat loop rows.
    # - IAT:1581 is a positive post-start SpecialAirNLoop BODY control proving the owner is not a
    #   broad SpecialN or action-family contact suppressor before the command-script clear.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_IASA
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
    # data/scripts/{fox,falco}.bin (MSLFTSC1 set_cmd_var idx=0 for msid 299)
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)
    assert _laser_x138_masks_by_item_type(root, item_type=54) == (0, 0)
    assert _laser_x138_masks_by_item_type(root, item_type=55) == (0, 0)

    mgs_path = (
        root
        / "replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.slpz"
    )
    if not mgs_path.exists():
        pytest.skip(f"missing local replay: {mgs_path}")
    mgs = load_replay_buffers(str(mgs_path))
    for record in (112, 113):
        row = mgs.rows[record : record + 1]
        assert int(row["seed_t"]["frame_id"][0]) < 0
        assert int(row["seed_t"]["items"][0, 0]["type"]) == 54
        assert int(row["seed_t"]["items"][0, 0]["owner"]) == 1
        out = _one_step_out_compare(ds=mgs, row=row)
        ref = row["ref_t1"]
        assert int(out["action_id"][0, 0]) == int(ref["action_id"][0, 0])
        assert float(out["percent"][0, 0]) == pytest.approx(float(ref["percent"][0, 0]), abs=1e-6)
        got_laser = _item_by_instance_and_type(out["items"][0], 16, 54)
        ref_laser = _item_by_instance_and_type(ref["items"][0], 16, 54)
        assert got_laser is not None and ref_laser is not None
        assert float(got_laser["timer"]) == pytest.approx(float(ref_laser["timer"]), abs=1e-6)

    iat_path = root / "replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.slpz"
    if not iat_path.exists():
        pytest.skip(f"missing local replay: {iat_path}")
    iat = load_replay_buffers(str(iat_path))
    row = iat.rows[1581:1582]
    assert int(row["seed_t"]["frame_id"][0]) >= 0
    assert int(row["seed_t"]["action_id"][0, 0]) == 345  # SpecialAirNLoop
    out = _one_step_out_compare(ds=iat, row=row)
    ref = row["ref_t1"]
    assert int(out["action_id"][0, 0]) == int(ref["action_id"][0, 0])
    assert int(out["hitlag"][0, 0]) == int(ref["hitlag"][0, 0])
    assert float(out["percent"][0, 0]) == pytest.approx(float(ref["percent"][0, 0]), abs=1e-6)


@pytest.mark.integration
def test_laser_stage_wall_collision_sets_expiry_before_slot_lifecycle_shift() -> None:
    # Replay-real lock for the PPA F00 item lifecycle cluster:
    # - itFoxlaser_UnkMotion1_Coll calls it_8029C4D4 -> it_8026E9A4, which checks stage collision,
    #   not floor-only collision.
    # - The laser with instance 186 crosses FD's left wall under the ledge on record 1160. It must
    #   get lifeTimer=1 so it is gone before later slot sorting/spawn-id comparisons.
    # - The preceding row is the negative boundary: same laser below the floor but not yet crossing
    #   a stage line must keep normal lifetime countdown.
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Coll,it_8029C4D4}
    # refs/melee/src/melee/it/itgroundcoll.c::it_8026E9A4
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/PriceyPartialAlbatross.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    rows = _rollout_rows(dataset_path, start_record=1153, end_record_inclusive=1162)

    out_1159, ref_1159 = rows[1159]
    neg_out = _item_by_instance(out_1159["items"], 186)
    neg_ref = _item_by_instance(ref_1159["items"], 186)
    assert neg_out is not None and neg_ref is not None
    assert float(neg_out["timer"]) == pytest.approx(float(neg_ref["timer"]), abs=1e-6)
    assert float(neg_out["timer"]) > 1.0

    out_1160, ref_1160 = rows[1160]
    hit_out = _item_by_instance(out_1160["items"], 186)
    hit_ref = _item_by_instance(ref_1160["items"], 186)
    assert hit_out is not None and hit_ref is not None
    assert float(hit_out["timer"]) == pytest.approx(float(hit_ref["timer"]), abs=1e-6)
    assert float(hit_out["timer"]) == pytest.approx(1.0, abs=1e-6)

    out_1161, ref_1161 = rows[1161]
    assert _item_by_instance(out_1161["items"], 186) is None
    assert _item_by_instance(ref_1161["items"], 186) is None
    assert _laser_ids(out_1161["items"]) == _laser_ids(ref_1161["items"])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.slpz",
            1038,
            0,
        ),
        (
            "replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.slpz",
            4128,
            0,
        ),
    ],
)
def test_steady_guard_laser_shield_contact_precedes_stage_expiry(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real locks for steady Guard item->shield contact from ftColl_8007925C.
    #
    # These Pokemon Stadium rows cross a platform line in the laser item-collision callback.
    # Fighter shield contact must run before that stage expiry callback, and settled Guard remains
    # on the authored item HitCapsule endpoint path rather than a broad projectile-origin fallback.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Coll,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, p]) == 179  # Guard
    assert int(row["ref_t1"]["action_id"][0, p]) == 181  # GuardSetOff
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 3
    assert _laser_ids(row["seed_t"]["items"][0])
    assert not _laser_ids(row["ref_t1"]["items"][0])

    out = _one_step_out_compare(ds=ds, row=row)
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "state_flags"):
        np.testing.assert_array_equal(out[field][0, p], row["ref_t1"][field][0, p])
    assert float(out["shield_hp"][0, p]) == pytest.approx(
        float(row["ref_t1"]["shield_hp"][0, p]), abs=1e-6
    )
    assert not _laser_ids(out["items"][0])


@pytest.mark.integration
def test_grounded_vulnerable_downbackd_laser_body_uses_lbcoll_hurt_radius() -> None:
    # Grounded vulnerable item BODY lbColl owner:
    # - ftColl_8007925C routes item BODY against fighter HurtCapsules through lbColl_8000805C.
    # - lbColl_8000805C forwards the shared lbColl_804D7A38 hurt-radius broadphase into the BODY
    #   contact helper. The previous row is the negative boundary: the same DownBackD family still
    #   has intangible hurt status and must not consume the laser.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58,lbColl_804D7A38}
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)
    dataset_path = (
        root
        / "replays/validation/dream_land_recent/"
        "FlippantEnchantedHorse.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))

    neg_row = ds.rows[4493:4494]
    neg_out = _one_step_out_compare(ds=ds, row=neg_row)
    assert int(neg_row["seed_t"]["action_id"][0, 0]) == 197  # DownBackD
    assert int(neg_row["ref_t1"]["action_id"][0, 0]) == 197
    assert _item_by_instance_and_type(neg_out["items"][0], 892, 55) is not None
    assert _item_by_instance_and_type(neg_row["ref_t1"]["items"][0], 892, 55) is not None

    row = ds.rows[4494:4495]
    out = _one_step_out_compare(ds=ds, row=row)
    assert int(row["seed_t"]["action_id"][0, 0]) == 197  # DownBackD
    assert int(row["ref_t1"]["action_id"][0, 0]) == 78  # DamageAir3
    assert int(out["action_id"][0, 0]) == int(row["ref_t1"]["action_id"][0, 0])
    assert int(out["hitlag"][0, 0]) == int(row["ref_t1"]["hitlag"][0, 0])
    assert int(out["hitstun"][0, 0]) == int(row["ref_t1"]["hitstun"][0, 0])
    assert float(out["percent"][0, 0]) == pytest.approx(float(row["ref_t1"]["percent"][0, 0]), abs=1e-5)
    assert _item_by_instance_and_type(out["items"][0], 892, 55) is None
    assert _item_by_instance_and_type(row["ref_t1"]["items"][0], 892, 55) is None


@dataclass(frozen=True)
class _Case:
    name: str
    dataset_rel: str
    record: int
    p: int
    family: str
    lock_state_flags: bool = True
    shield_hp_abs_tol: float = 0.0


_CASES = [
    _Case(
        name="tbk_shield_adj_pre",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=2216,
        p=0,
        family="tbk_shield_guard_no_submotion",
    ),
    _Case(
        name="tbk_shield_sensitive_target",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=2217,
        p=0,
        family="tbk_shield_guard_no_submotion",
    ),
    _Case(
        name="tbk_shield_adj_post",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=2218,
        p=0,
        family="tbk_shield_guard_no_submotion",
    ),
    _Case(
        name="tbk_guardon_shield_adj_pre",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=4143,
        p=0,
        family="tbk_guardon_shield_adjacent",
    ),
    _Case(
        name="tbk_guardon_shield_adj_target0",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=4144,
        p=0,
        family="tbk_guardon_shield_adjacent",
    ),
    _Case(
        name="tbk_guardon_shield_adj_target1",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=4145,
        p=0,
        family="tbk_guardon_shield_adjacent",
    ),
    _Case(
        name="tbk_guardon_shield_adj_post",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=4146,
        p=0,
        family="tbk_guardon_shield_adjacent",
    ),
    _Case(
        name="tbk_real_shield_hit_adj_pre",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=1247,
        p=0,
        family="tbk_real_shield_hit",
    ),
    _Case(
        name="tbk_real_shield_hit_target",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=1248,
        p=0,
        family="tbk_real_shield_hit",
        shield_hp_abs_tol=1e-3,
    ),
    _Case(
        name="tbk_real_shield_hit_adj_post",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=1249,
        p=0,
        family="tbk_real_shield_hit",
    ),
    _Case(
        name="agg_phantom_adj_pre",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
        ),
        record=200,
        p=1,
        family="agg_phantom",
    ),
    _Case(
        name="agg_phantom_target",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
        ),
        record=201,
        p=1,
        family="agg_phantom",
    ),
    _Case(
        name="agg_phantom_adj_post",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
        ),
        record=202,
        p=1,
        family="agg_phantom",
    ),
    _Case(
        name="gat_body_adj_pre",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
        ),
        record=4504,
        p=0,
        family="gat_body",
    ),
    _Case(
        name="gat_body_target",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
        ),
        record=4505,
        p=0,
        family="gat_body",
    ),
    _Case(
        name="gat_body_adj_post",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
        ),
        record=4506,
        p=0,
        family="gat_body",
        lock_state_flags=False,
    ),
]


@dataclass(frozen=True)
class _ShieldBounceCase:
    name: str
    dataset_rel: str
    record: int
    p: int
    laser_count_seed: int
    laser_count_ref: int
    note: str


_SHIELD_BOUNCE_CASES = [
    _ShieldBounceCase(
        name="gat_existing_laser_bounce_keepalive_guardreflect",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
        ),
        record=2276,
        p=0,
        laser_count_seed=1,
        laser_count_ref=1,
        note="Existing laser keeps alive through upper-shield bounce on GuardReflect->GuardSetOff.",
    ),
    _ShieldBounceCase(
        name="gat_existing_laser_bounce_keepalive_shieldstun",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
        ),
        record=5280,
        p=0,
        laser_count_seed=1,
        laser_count_ref=1,
        note="Existing laser keeps alive through upper-shield bounce into GuardSetOff hitlag.",
    ),
    _ShieldBounceCase(
        name="gat_spawn_frame_no_bounce_keepalive_control_1163",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
        ),
        record=1163,
        p=0,
        laser_count_seed=0,
        laser_count_ref=0,
        note="Gun-only spawn frame must not keep an immediately shielded laser alive.",
    ),
    _ShieldBounceCase(
        name="gat_spawn_frame_no_bounce_keepalive_control_4326",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
        ),
        record=4326,
        p=0,
        laser_count_seed=1,
        laser_count_ref=1,
        note="Concurrent long-lived laser + gun spawn row must not retain an extra bounced spawn-frame shot.",
    ),
    _ShieldBounceCase(
        name="gat_spawn_frame_no_bounce_keepalive_control_9412",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
        ),
        record=9412,
        p=0,
        laser_count_seed=1,
        laser_count_ref=1,
        note="Late-suite concurrent gun spawn row must still destroy the same-frame shielded shot.",
    ),
    _ShieldBounceCase(
        name="iat_landingfallspecial_high_shield_no_bounce_seed_destroy",
        dataset_rel=(
            "replays/validation/aggregate_recent/"
            "ImpassionedAlarmedTarsier.slpz"
        ),
        record=3552,
        p=1,
        laser_count_seed=2,
        laser_count_ref=1,
        note="Normal GuardSetOff shield contact without hidden ShieldBounced seed must destroy the high laser.",
    ),
]


@dataclass(frozen=True)
class _DashFullShieldCase:
    name: str
    dataset_rel: str
    record: int
    p: int
    family: str
    note: str


_DASH_FULL_SHIELD_CASES = [
    _DashFullShieldCase(
        name="tbk_dash_full_shield_turn_adj_pre",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=4141,
        p=0,
        family="tbk_dash_full_shield_turn",
        note="Full-shield Dash control before the Turn transition; laser must stay alive.",
    ),
    _DashFullShieldCase(
        name="tbk_dash_full_shield_turn_target",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=4142,
        p=0,
        family="tbk_dash_full_shield_turn",
        note="Full-shield Dash row must not invent a same-frame laser shield hit before Turn.",
    ),
    _DashFullShieldCase(
        name="tbk_dash_full_shield_turn_adj_post",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=4143,
        p=0,
        family="tbk_dash_full_shield_turn",
        note="Next-frame GuardOn admission after the preserved no-hit Turn row.",
    ),
    _DashFullShieldCase(
        name="tbk_dash_full_shield_guardreflect_adj_pre",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=7446,
        p=0,
        family="tbk_dash_full_shield_guardreflect",
        note="Full-shield Dash control before the no-hit GuardReflect row; laser must stay alive.",
    ),
    _DashFullShieldCase(
        name="tbk_dash_full_shield_guardreflect_target",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz"
        ),
        record=7447,
        p=0,
        family="tbk_dash_full_shield_guardreflect",
        note="Full-shield Dash row must enter GuardReflect without consuming shield HP or despawning the laser.",
    ),
    _DashFullShieldCase(
        name="agn_dash_full_shield_body_hit_negative_control",
        dataset_rel=(
            "replays/validation/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
        ),
        record=178,
        p=1,
        family="agn_dash_full_shield_negative_control",
        note="Unrelated full-shield Dash body-hit row must stay replay-real; the Dash shield gate must not suppress it.",
    ),
]


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: c.name)
def test_items_collision_space_rows_and_adjacent_controls(case: _Case) -> None:
    # Collision-space replay locks for laser overlap probes. These rows are sensitive to how we
    # transform authored hitbox offsets into overlap-space in shield/body/reflect lanes.
    #
    # Decomp anchors for ownership:
    # - refs/melee/src/melee/it/items/itfoxlaser.c::{
    #     itFoxlaser_UnkMotion1_Anim,itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
    # - refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # - refs/melee/src/melee/it/itcoll.c::it_80272460
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > case.record, (
        f"replay too short for regression check: record={case.record} path={case.dataset_rel}"
    )
    row = samples[case.record : case.record + 1]
    p = case.p

    seed_lasers = _laser_ids(row["seed_t"]["items"][0])
    ref_lasers = _laser_ids(row["ref_t1"]["items"][0])

    if case.family == "tbk_shield_guard_no_submotion":
        assert int(row["seed_t"]["action_id"][0, p]) == 179
        assert int(row["ref_t1"]["action_id"][0, p]) == 179
        assert int(row["seed_t"]["animation_index"][0, p]) == 0xFFFFFFFF
        assert int(row["ref_t1"]["animation_index"][0, p]) == 0xFFFFFFFF
        assert int(row["seed_t"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row["seed_t"]["hitstun"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 0
        assert not seed_lasers and not ref_lasers
    elif case.family == "tbk_real_shield_hit":
        if case.name == "tbk_real_shield_hit_adj_pre":
            assert int(row["seed_t"]["action_id"][0, p]) == 20
            assert int(row["ref_t1"]["action_id"][0, p]) == 21
            assert int(row["ref_t1"]["hitlag"][0, p]) == 0
            assert seed_lasers and ref_lasers
        elif case.name == "tbk_real_shield_hit_target":
            assert int(row["seed_t"]["action_id"][0, p]) == 21
            assert int(row["ref_t1"]["action_id"][0, p]) == 181
            assert int(row["ref_t1"]["hitlag"][0, p]) > 0
            assert int(row["ref_t1"]["hitstun"][0, p]) == 0
            assert seed_lasers and not ref_lasers
        else:
            assert int(row["seed_t"]["action_id"][0, p]) == 181
            assert int(row["ref_t1"]["action_id"][0, p]) == 181
            assert int(row["seed_t"]["hitlag"][0, p]) == 3
            assert int(row["ref_t1"]["hitlag"][0, p]) == 2
            assert not seed_lasers and not ref_lasers
    elif case.family == "tbk_guardon_shield_adjacent":
        if case.name == "tbk_guardon_shield_adj_pre":
            assert int(row["seed_t"]["action_id"][0, p]) == 18
            assert int(row["ref_t1"]["action_id"][0, p]) == 178
        else:
            assert int(row["seed_t"]["action_id"][0, p]) == 178
            assert int(row["ref_t1"]["action_id"][0, p]) == 178
        assert int(row["seed_t"]["animation_index"][0, p]) in (10, 0xFFFFFFFF)
        assert int(row["ref_t1"]["animation_index"][0, p]) == 0xFFFFFFFF
        assert int(row["seed_t"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row["seed_t"]["hitstun"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 0
        assert seed_lasers and ref_lasers
    elif case.family == "agg_phantom":
        assert int(row["seed_t"]["action_id"][0, p]) == 212
        assert int(row["ref_t1"]["action_id"][0, p]) == 212
        assert int(row["seed_t"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row["seed_t"]["hitstun"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    elif case.family == "gat_body":
        if case.name != "gat_body_adj_post":
            assert int(row["seed_t"]["action_id"][0, p]) == 361
            assert int(row["ref_t1"]["action_id"][0, p]) == 361
            assert int(row["seed_t"]["hitlag"][0, p]) == 0
            assert int(row["ref_t1"]["hitlag"][0, p]) == 0
            assert int(row["seed_t"]["hitstun"][0, p]) == 0
            assert int(row["ref_t1"]["hitstun"][0, p]) == 0
        else:
            assert int(row["seed_t"]["action_id"][0, p]) == 361
            assert int(row["ref_t1"]["action_id"][0, p]) == 76
            assert int(row["ref_t1"]["hitlag"][0, p]) > 0
            assert int(row["ref_t1"]["hitstun"][0, p]) > 0
    else:
        raise AssertionError(f"unknown case family: {case.family}")

    out = _one_step_out_compare(ds=ds, row=row)

    for field in ("action_id", "animation_index", "on_ground", "hitlag", "hitstun"):
        got = int(out[field][0, p])
        exp = int(row["ref_t1"][field][0, p])
        assert got == exp, f"{case.name}: field={field} expected={exp} got={got}"

    if case.lock_state_flags:
        got_flags = [int(x) for x in out["state_flags"][0, p]]
        exp_flags = [int(x) for x in row["ref_t1"]["state_flags"][0, p]]
        assert got_flags == exp_flags, f"{case.name}: state_flags expected={exp_flags} got={got_flags}"

    got_lasers = _laser_ids(out["items"][0])
    assert got_lasers == ref_lasers, f"{case.name}: laser_ids expected={ref_lasers} got={got_lasers}"

    got_percent_bits = int(np.float32(out["percent"][0, p]).view(np.uint32))
    exp_percent_bits = int(np.float32(row["ref_t1"]["percent"][0, p]).view(np.uint32))
    assert got_percent_bits == exp_percent_bits, (
        f"{case.name}: percent f32 bits expected=0x{exp_percent_bits:08x} got=0x{got_percent_bits:08x}"
    )

    got_shield_hp = float(out["shield_hp"][0, p])
    exp_shield_hp = float(row["ref_t1"]["shield_hp"][0, p])
    if case.shield_hp_abs_tol > 0.0:
        assert abs(got_shield_hp - exp_shield_hp) <= case.shield_hp_abs_tol, (
            f"{case.name}: shield_hp expected~={exp_shield_hp} got={got_shield_hp} tol={case.shield_hp_abs_tol}"
        )
    else:
        got_shield_bits = int(np.float32(got_shield_hp).view(np.uint32))
        exp_shield_bits = int(np.float32(exp_shield_hp).view(np.uint32))
        assert got_shield_bits == exp_shield_bits, (
            f"{case.name}: shield_hp f32 bits expected=0x{exp_shield_bits:08x} got=0x{got_shield_bits:08x}"
        )


@pytest.mark.integration
@pytest.mark.parametrize("case", _SHIELD_BOUNCE_CASES, ids=lambda c: c.name)
def test_laser_shield_bounce_keepalive_and_spawn_frame_destroy_controls(case: _ShieldBounceCase) -> None:
    # Replay-real locks for laser shield-bounce keepalive in src/items.c::lasers_update_and_collide.
    #
    # Decomp anchors:
    # - refs/melee/src/melee/it/item.c::Item_80269DC8
    # - refs/melee/src/melee/it/items/itfoxlaser.c::{
    #     it_8029C504,itFoxlaser_UnkMotion1_Anim,itFoxLaser_Logic94_ShieldBounced,itFoxLaser_Logic94_HitShield}
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > case.record, (
        f"replay too short for regression check: record={case.record} path={case.dataset_rel}"
    )
    row = samples[case.record : case.record + 1]
    p = int(case.p)

    seed_lasers = _laser_ids(row["seed_t"]["items"][0])
    ref_lasers = _laser_ids(row["ref_t1"]["items"][0])
    assert len(seed_lasers) == case.laser_count_seed, case.note
    assert len(ref_lasers) == case.laser_count_ref, case.note

    out = _one_step_out_compare(ds=ds, row=row)

    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        got = int(out[field][0, p])
        exp = int(row["ref_t1"][field][0, p])
        assert got == exp, f"{case.name}: field={field} expected={exp} got={got}"

    got_flags = [int(x) for x in out["state_flags"][0, p]]
    exp_flags = [int(x) for x in row["ref_t1"]["state_flags"][0, p]]
    assert got_flags == exp_flags, f"{case.name}: state_flags expected={exp_flags} got={got_flags}"

    got_lasers = _laser_ids(out["items"][0])
    assert got_lasers == ref_lasers, f"{case.name}: laser_ids expected={ref_lasers} got={got_lasers}"


@pytest.mark.integration
def test_laser_shield_bounce_open_residual_gat_destroy_path_not_retained() -> None:
    # Open residual / package-boundary negative for the current retained Item_80269DC8 boundary:
    # without explicit ShieldBounced seed provenance or the narrower live normal-owner rows covered
    # below, GAT:5223 remains on the HitShield destroy path in sim while native keeps the reflected
    # laser alive.
    # Do not broaden shield contacts into generic keepalive during package cleanup; this broader
    # ShieldBounced/live normal owner remains open.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007DD8,lbColl_800077A0}
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)
    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    rows = _rollout_rows(dataset_path, 5223, 5281)
    out_5280, ref_5280 = rows[5280]
    out_5281, ref_5281 = rows[5281]

    for out, ref in ((out_5280, ref_5280), (out_5281, ref_5281)):
        assert int(ref["items"][0]["exists"]) == 1
        assert int(ref["items"][0]["type"]) == 55
        assert int(ref["items"][0]["instance_id"]) == 1216
        assert int(out["items"][0]["exists"]) == 0
        assert int(out["action_id"][0]) in (178, 181)


@pytest.mark.integration
def test_laser_shield_bounce_runtime_rollout_keeps_maj_slot_lifecycle_alive() -> None:
    # Runtime-positive for same-step locomotion -> GuardReflect -> GuardSetOff ShieldBounced:
    # - MAJ:751 starts before the shield-bounce row, so no teacher-forced item_shield_bounce seed
    #   lane is available at the contact frame.
    # - At MAJ:763 native keeps the aged Falco laser alive through Item_80269DC8 ShieldBounced.
    # - At MAJ:766 the next blaster gun spawns into the following item slot; clearing the bounced
    #   laser early compacts the gun into slot 0 and creates the top F00 item_exists rollout cluster.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007DD8,lbColl_800077A0}
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/MotionlessAggressiveJay.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    ds = load_replay_buffers(str(dataset_path))
    seed_751 = ds.rows["seed_t"][751]
    assert not any(int(v) for v in seed_751["item_shield_bounce_valid"])

    rows = _rollout_rows(dataset_path, 751, 766)
    out_763, ref_763 = rows[763]
    out_766, ref_766 = rows[766]

    assert int(ref_763["action_id"][1]) == 181
    assert int(out_763["action_id"][1]) == 181
    assert int(out_763["hitlag"][1]) == int(ref_763["hitlag"][1]) == 3
    assert int(ref_763["items"][0]["exists"]) == 1
    assert int(ref_763["items"][0]["type"]) == 55
    assert int(ref_763["items"][0]["instance_id"]) == 211
    assert int(out_763["items"][0]["exists"]) == 1
    assert int(out_763["items"][0]["type"]) == 55
    assert int(out_763["items"][0]["instance_id"]) == 211
    assert float(out_763["items"][0]["vel_y"]) > 0.0

    assert int(ref_766["items"][0]["exists"]) == 1
    assert int(ref_766["items"][0]["type"]) == 55
    assert int(ref_766["items"][0]["instance_id"]) == 211
    assert int(ref_766["items"][1]["exists"]) == 1
    assert int(ref_766["items"][1]["type"]) == 75
    assert int(out_766["items"][0]["exists"]) == 1
    assert int(out_766["items"][0]["type"]) == 55
    assert int(out_766["items"][0]["instance_id"]) == 211
    assert int(out_766["items"][1]["exists"]) == 1
    assert int(out_766["items"][1]["type"]) == 75


@pytest.mark.integration
def test_laser_shield_bounce_runtime_rollout_keeps_agn_walk_guardreflect_laser_alive() -> None:
    # Runtime-positive for the same F00 owner after a Damage -> Walk -> GuardReflect handoff:
    # - AGN:4036 starts before Walk rate and item shield-bounce seed lanes are available.
    # - The Walk action_frame must tick through the entry/IASA handoff before the aged Falco laser
    #   reaches same-frame locomotion -> GuardReflect -> GuardSetOff ShieldBounced at AGN:4044.
    # - Existing GAT/MAJ positives and IAT negatives keep this from becoming a broad shield-hit
    #   keepalive rule.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::{ftCo_Walk_Enter,ftCo_Walk_Anim}
    # refs/melee/src/melee/it/itcoll.c::it_8027137C
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)
    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    ds = load_replay_buffers(str(dataset_path))
    seed_4036 = ds.rows["seed_t"][4036]
    assert not any(int(v) for v in seed_4036["item_shield_bounce_valid"])

    rows = _rollout_rows(dataset_path, 4036, 4045)
    out_4037, ref_4037 = rows[4037]
    out_4044, ref_4044 = rows[4044]
    out_4045, ref_4045 = rows[4045]

    assert int(out_4037["action_id"][1]) == int(ref_4037["action_id"][1]) == 15
    assert int(out_4037["action_frame"][1]) == int(ref_4037["action_frame"][1]) == 2

    assert int(out_4044["action_id"][1]) == int(ref_4044["action_id"][1]) == 181
    assert int(out_4044["hitlag"][1]) == int(ref_4044["hitlag"][1]) == 3
    assert int(ref_4044["items"][1]["exists"]) == 1
    assert int(ref_4044["items"][1]["type"]) == 55
    assert int(ref_4044["items"][1]["instance_id"]) == 861
    assert int(out_4044["items"][1]["exists"]) == 1
    assert int(out_4044["items"][1]["type"]) == 55
    assert int(out_4044["items"][1]["instance_id"]) == 861
    assert float(out_4044["items"][1]["vel_y"]) > 0.0

    assert int(ref_4045["items"][0]["exists"]) == 1
    assert int(ref_4045["items"][0]["type"]) == 55
    assert int(ref_4045["items"][0]["instance_id"]) == 861
    assert int(out_4045["items"][0]["exists"]) == 1
    assert int(out_4045["items"][0]["type"]) == 55
    assert int(out_4045["items"][0]["instance_id"]) == 861


@pytest.mark.integration
def test_laser_shield_bounce_locomotion_guardreflect_cap_blocks_iat_compacted_laser() -> None:
    # Runtime-negative for the same late locomotion -> GuardReflect predicate used by the MAJ
    # keepalive: the compacted IAT laser is in the capped ShieldDesc lane and must not become a
    # broad scaled-segment ShieldBounced hit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Anim,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[11406 : 11406 + 1]
    out = _one_step_out_compare(ds=ds, row=row)
    ref = row["ref_t1"]

    assert int(row["seed_t"]["action_id"][0, 1]) == 16
    assert int(ref["action_id"][0, 1]) == 182
    assert int(out["action_id"][0, 1]) == 182
    assert float(out["shield_hp"][0, 1]) == pytest.approx(float(ref["shield_hp"][0, 1]), abs=1e-6)
    assert int(out["items"][0, 0]["exists"]) == 1
    assert int(out["items"][0, 0]["type"]) == 55
    assert int(out["items"][0, 0]["instance_id"]) == 2147
    assert float(out["items"][0, 0]["vel_x"]) == pytest.approx(-5.0, abs=1e-6)
    assert abs(float(out["items"][0, 0]["vel_y"])) <= 1e-5


@pytest.mark.integration
def test_laser_guardreflect_runtime_rollout_final_x14_hitshield_destroys_maj_laser() -> None:
    # Runtime-negative for broad GuardReflect timer-only reflect staging:
    # - MAJ:201 is still inside the active GuardReflect window, but the laser is outside the live
    #   ReflectDesc vertical lane and must not stage a deferred reflected owner/direction.
    # - MAJ:202 is the final-x14 handoff; the same laser then resolves through Item_80269DC8
    #   HitShield destruction and enters GuardSetOff.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_GuardReflect_Anim}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
    # refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/MotionlessAggressiveJay.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    rows = _rollout_rows(dataset_path, 192, 202)
    out_201, ref_201 = rows[201]
    out_202, ref_202 = rows[202]

    assert int(ref_201["items"][0]["exists"]) == 1
    assert int(ref_201["items"][0]["type"]) == 55
    for fld in ("owner", "instance_id"):
        assert int(out_201["items"][0][fld]) == int(ref_201["items"][0][fld])
    assert float(out_201["items"][0]["direction"]) == float(ref_201["items"][0]["direction"]) == 1.0
    assert float(out_201["items"][0]["vel_x"]) == float(ref_201["items"][0]["vel_x"]) == 5.0

    assert int(out_202["action_id"][1]) == int(ref_202["action_id"][1]) == 181
    assert int(out_202["hitlag"][1]) == int(ref_202["hitlag"][1]) == 3
    assert float(out_202["shield_hp"][1]) == pytest.approx(float(ref_202["shield_hp"][1]), abs=1e-6)
    assert int(out_202["items"][0]["exists"]) == int(ref_202["items"][0]["exists"]) == 0


@pytest.mark.integration
def test_laser_guardreflect_final_x14_boundary_rows_do_not_overbroaden_hitshield() -> None:
    # Boundary locks for the final-x14 F19 split:
    # - GAT:1287 is still a frozen GuardReflect keepalive row (`action_frame=-2`) and must not be
    #   destroyed by the MAJ final-handoff fix.
    # - GAT:2275 is another final-x14 keepalive row with no current shield-bubble overlap.
    # - DCC:353 is a final-x14 row with current shield-bubble overlap and must still route through
    #   HitShield / laser destruction.
    # - MAJ:6341 is a GuardSetOff row carrying the powershield bit without a proven ReflectDesc
    #   owner transfer; it keeps the projectile callback alive without staging a reflected owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_GuardReflect_Anim}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80077464}
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)
    cases = [
        (
            root
            / "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            1287,
            0,
            0,
        ),
        (
            root
            / "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            2275,
            0,
            0,
        ),
        (
            root
            / "replays/validation/aggregate_recent/DistinctCaringCobra.slpz",
            353,
            0,
            0,
        ),
        (
            root
            / "replays/validation/aggregate_recent/MotionlessAggressiveJay.slpz",
            6341,
            1,
            0,
        ),
    ]
    for dataset_path, record, p, item_slot in cases:
        if not dataset_path.exists():
            pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")
        ds = load_replay_buffers(str(dataset_path))
        row = ds.rows[record : record + 1]
        out = _one_step_out_compare(ds=ds, row=row)
        ref = row["ref_t1"]
        for field in ("action_id", "action_frame", "animation_index", "hitlag", "instance_id"):
            assert int(out[field][0, p]) == int(ref[field][0, p]), (
                f"{dataset_path.name}:{record} field={field}"
            )
        for field in ("exists", "type", "owner", "instance_id"):
            assert int(out["items"][0, item_slot][field]) == int(ref["items"][0, item_slot][field]), (
                f"{dataset_path.name}:{record} item field={field}"
            )


@pytest.mark.integration
def test_final_x14_seeded_shield_bounce_overrides_guardreflect_keepalive() -> None:
    # Replay-real positive/negative for the final-x14 ShieldBounced handoff:
    # - GAT:9479 has explicit hidden item_shield_bounce seed provenance from ftColl_80077688 /
    #   Item_80269DC8, so it must enter GuardSetOff and preserve the bounced laser velocity.
    # - GAT:1287 has the same frozen GuardReflect shape but no ShieldBounced seed, so it remains
    #   on the keepalive path with straight laser velocity.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)
    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    ds = load_replay_buffers(str(dataset_path))

    row_pos = ds.rows[9479 : 9480]
    assert int(row_pos["seed_t"]["action_id"][0, 0]) == 182  # GuardReflect
    assert int(row_pos["seed_t"]["guard_reflect_timer_x14"][0, 0]) == 1
    assert int(row_pos["seed_t"]["item_shield_bounce_valid"][0, 0]) == 1
    out_pos = _one_step_out_compare(ds=ds, row=row_pos)
    ref_pos = row_pos["ref_t1"]
    for field in ("action_id", "action_frame", "animation_index", "hitlag"):
        assert int(out_pos[field][0, 0]) == int(ref_pos[field][0, 0]), field
    assert float(out_pos["shield_hp"][0, 0]) == pytest.approx(float(ref_pos["shield_hp"][0, 0]), abs=1e-6)
    for field in ("exists", "type", "owner", "instance_id"):
        assert int(out_pos["items"][0, 0][field]) == int(ref_pos["items"][0, 0][field]), field
    assert float(out_pos["items"][0, 0]["vel_x"]) == pytest.approx(
        float(ref_pos["items"][0, 0]["vel_x"]), abs=1e-6
    )
    assert float(out_pos["items"][0, 0]["vel_y"]) == pytest.approx(
        float(ref_pos["items"][0, 0]["vel_y"]), abs=1e-6
    )

    row_neg = ds.rows[1287 : 1288]
    assert int(row_neg["seed_t"]["action_id"][0, 0]) == 182  # GuardReflect
    assert int(row_neg["seed_t"]["guard_reflect_timer_x14"][0, 0]) == 1
    assert int(row_neg["seed_t"]["item_shield_bounce_valid"][0, 0]) == 0
    out_neg = _one_step_out_compare(ds=ds, row=row_neg)
    ref_neg = row_neg["ref_t1"]
    assert int(out_neg["action_id"][0, 0]) == int(ref_neg["action_id"][0, 0]) == 182
    assert int(out_neg["hitlag"][0, 0]) == int(ref_neg["hitlag"][0, 0]) == 0
    assert float(out_neg["items"][0, 0]["vel_x"]) == pytest.approx(5.0, abs=1e-6)
    assert float(out_neg["items"][0, 0]["vel_y"]) == pytest.approx(0.0, abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize("case", _DASH_FULL_SHIELD_CASES, ids=lambda c: c.name)
def test_laser_dash_full_shield_snapshot_rows(case: _DashFullShieldCase) -> None:
    # Replay-real locks for Dash-seeded laser shield-precedence ownership in
    # src/items.c::lasers_update_and_collide.
    #
    # Decomp anchors:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80092450}
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
    # - refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > case.record, (
        f"replay too short for regression check: record={case.record} path={case.dataset_rel}"
    )
    row = samples[case.record : case.record + 1]
    p = int(case.p)

    seed_lasers = _laser_ids(row["seed_t"]["items"][0])
    ref_lasers = _laser_ids(row["ref_t1"]["items"][0])

    if case.family == "tbk_dash_full_shield_turn":
        assert int(row["seed_t"]["action_id"][0, p]) in (20, 18)
        assert int(row["ref_t1"]["action_id"][0, p]) in (20, 18, 178)
        assert int(row["seed_t"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row["seed_t"]["hitstun"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 0
        assert seed_lasers and ref_lasers
    elif case.family == "tbk_dash_full_shield_guardreflect":
        assert int(row["seed_t"]["action_id"][0, p]) in (20, 182)
        assert int(row["ref_t1"]["action_id"][0, p]) in (20, 182)
        assert int(row["seed_t"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row["seed_t"]["hitstun"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 0
        assert seed_lasers and ref_lasers
    elif case.family == "agn_dash_full_shield_negative_control":
        assert int(row["seed_t"]["action_id"][0, p]) == 20
        assert int(row["ref_t1"]["action_id"][0, p]) == 75
        assert int(row["seed_t"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitlag"][0, p]) == 4
        assert int(row["ref_t1"]["hitstun"][0, p]) == 9
        assert seed_lasers and ref_lasers
    else:
        raise AssertionError(f"unknown case family: {case.family}")

    out = _one_step_out_compare(ds=ds, row=row)

    fields = ("action_id", "animation_index", "on_ground", "hitlag", "hitstun")
    if case.name == "tbk_dash_full_shield_turn_target":
        fields = ("on_ground", "hitlag", "hitstun")
    for field in fields:
        got = int(out[field][0, p])
        exp = int(row["ref_t1"][field][0, p])
        assert got == exp, f"{case.name}: field={field} expected={exp} got={got}"

    if case.family == "tbk_dash_full_shield_turn" and case.name.endswith("target"):
        assert int(out["hitlag"][0, p]) == 0, case.note
        assert int(out["hitstun"][0, p]) == 0, case.note
        assert int(np.float32(out["shield_hp"][0, p]).view(np.uint32)) == int(
            np.float32(row["ref_t1"]["shield_hp"][0, p]).view(np.uint32)
        ), case.note
    else:
        got_flags = [int(x) for x in out["state_flags"][0, p]]
        exp_flags = [int(x) for x in row["ref_t1"]["state_flags"][0, p]]
        assert got_flags == exp_flags, f"{case.name}: state_flags expected={exp_flags} got={got_flags}"

    got_lasers = _laser_ids(out["items"][0])
    assert got_lasers == ref_lasers, f"{case.name}: laser_ids expected={ref_lasers} got={got_lasers}"

    got_shield_bits = int(np.float32(out["shield_hp"][0, p]).view(np.uint32))
    exp_shield_bits = int(np.float32(row["ref_t1"]["shield_hp"][0, p]).view(np.uint32))
    assert got_shield_bits == exp_shield_bits, (
        f"{case.name}: shield_hp f32 bits expected=0x{exp_shield_bits:08x} got=0x{got_shield_bits:08x}"
    )
