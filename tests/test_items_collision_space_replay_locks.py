from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.run_longest_rollout_streaks import _load_binding


def _skip_if_missing_laser_artifacts(root: Path) -> None:
    if not (root / "data/items/lasers.bin").exists():
        pytest.skip("missing local artifact: data/items/lasers.bin")


def _laser_ids(items_row: np.ndarray) -> list[int]:
    out: list[int] = []
    for it in items_row:
        if int(it["exists"]) and int(it["type"]) in (54, 55):
            out.append(int(it["instance_id"]))
    out.sort()
    return out


def _one_step_out_compare(*, ds, row) -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
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
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
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
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        rows: dict[int, tuple[np.void, np.void]] = {}
        for record in range(start_record, end_record_inclusive + 1):
            prev_input_bytes[0, :] = samples_u8[
                record, prev_input_off : prev_input_off + input_stride
            ]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            rows[record] = (out_view[0].copy(), samples["ref_t1"][record].copy())
        return rows
    finally:
        binding.destroy(handle)


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
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=2216,
        p=0,
        family="tbk_shield_guard_no_submotion",
    ),
    _Case(
        name="tbk_shield_sensitive_target",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=2217,
        p=0,
        family="tbk_shield_guard_no_submotion",
    ),
    _Case(
        name="tbk_shield_adj_post",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=2218,
        p=0,
        family="tbk_shield_guard_no_submotion",
    ),
    _Case(
        name="tbk_guardon_shield_adj_pre",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=4143,
        p=0,
        family="tbk_guardon_shield_adjacent",
    ),
    _Case(
        name="tbk_guardon_shield_adj_target0",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=4144,
        p=0,
        family="tbk_guardon_shield_adjacent",
    ),
    _Case(
        name="tbk_guardon_shield_adj_target1",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=4145,
        p=0,
        family="tbk_guardon_shield_adjacent",
    ),
    _Case(
        name="tbk_guardon_shield_adj_post",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=4146,
        p=0,
        family="tbk_guardon_shield_adjacent",
    ),
    _Case(
        name="tbk_real_shield_hit_adj_pre",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=1247,
        p=0,
        family="tbk_real_shield_hit",
    ),
    _Case(
        name="tbk_real_shield_hit_target",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=1248,
        p=0,
        family="tbk_real_shield_hit",
        shield_hp_abs_tol=1e-3,
    ),
    _Case(
        name="tbk_real_shield_hit_adj_post",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=1249,
        p=0,
        family="tbk_real_shield_hit",
    ),
    _Case(
        name="agg_phantom_adj_pre",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
        ),
        record=200,
        p=1,
        family="agg_phantom",
    ),
    _Case(
        name="agg_phantom_target",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
        ),
        record=201,
        p=1,
        family="agg_phantom",
    ),
    _Case(
        name="agg_phantom_adj_post",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
        ),
        record=202,
        p=1,
        family="agg_phantom",
    ),
    _Case(
        name="gat_body_adj_pre",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
        ),
        record=4504,
        p=0,
        family="gat_body",
    ),
    _Case(
        name="gat_body_target",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
        ),
        record=4505,
        p=0,
        family="gat_body",
    ),
    _Case(
        name="gat_body_adj_post",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
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
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
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
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
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
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
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
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
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
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
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
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "ImpassionedAlarmedTarsier.msl"
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
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=4141,
        p=0,
        family="tbk_dash_full_shield_turn",
        note="Full-shield Dash control before the Turn transition; laser must stay alive.",
    ),
    _DashFullShieldCase(
        name="tbk_dash_full_shield_turn_target",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=4142,
        p=0,
        family="tbk_dash_full_shield_turn",
        note="Full-shield Dash row must not invent a same-frame laser shield hit before Turn.",
    ),
    _DashFullShieldCase(
        name="tbk_dash_full_shield_turn_adj_post",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=4143,
        p=0,
        family="tbk_dash_full_shield_turn",
        note="Next-frame GuardOn admission after the preserved no-hit Turn row.",
    ),
    _DashFullShieldCase(
        name="tbk_dash_full_shield_guardreflect_adj_pre",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=7446,
        p=0,
        family="tbk_dash_full_shield_guardreflect",
        note="Full-shield Dash control before the no-hit GuardReflect row; laser must stay alive.",
    ),
    _DashFullShieldCase(
        name="tbk_dash_full_shield_guardreflect_target",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=7447,
        p=0,
        family="tbk_dash_full_shield_guardreflect",
        note="Full-shield Dash row must enter GuardReflect without consuming shield HP or despawning the laser.",
    ),
    _DashFullShieldCase(
        name="agn_dash_full_shield_body_hit_negative_control",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
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
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > case.record, (
        f"dataset too short for regression check: record={case.record} path={case.dataset_rel}"
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
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > case.record, (
        f"dataset too short for regression check: record={case.record} path={case.dataset_rel}"
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
def test_laser_shield_bounce_runtime_rollout_keeps_gat_laser_alive() -> None:
    # Runtime-positive for Item_80269DC8 ShieldBounced keepalive without the teacher-forced
    # item_shield_bounce seed lane:
    # - GAT:5223 rollout reaches the shield contact at 5280 with the laser alive from sim state.
    # - Native keeps the aged Falco laser through ShieldBounced instead of taking HitShield destroy.
    # - IAT/GAT one-step controls above prove this is not a broad "shield hit keeps laser" rule.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007DD8,lbColl_800077A0}
    # refs/melee/src/melee/it/item.c::Item_80269DC8
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_laser_artifacts(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path.relative_to(root)}")

    rows = _rollout_rows(dataset_path, 5223, 5281)
    out_5280, ref_5280 = rows[5280]
    out_5281, ref_5281 = rows[5281]

    for out, ref in ((out_5280, ref_5280), (out_5281, ref_5281)):
        assert int(ref["items"][0]["exists"]) == 1
        assert int(ref["items"][0]["type"]) == 55
        assert int(ref["items"][0]["instance_id"]) == 1216
        assert int(out["items"][0]["exists"]) == 1
        assert int(out["items"][0]["type"]) == 55
        assert int(out["items"][0]["instance_id"]) == 1216
        assert int(out["action_id"][0]) == int(ref["action_id"][0]) == 181
        assert int(out["hitlag"][0]) == int(ref["hitlag"][0])


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
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path.relative_to(root)}")

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
            / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            1287,
            0,
            0,
        ),
        (
            root
            / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            2275,
            0,
            0,
        ),
        (
            root
            / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
            353,
            0,
            0,
        ),
        (
            root
            / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl",
            6341,
            1,
            0,
        ),
    ]
    for dataset_path, record, p, item_slot in cases:
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {dataset_path.relative_to(root)}")
        ds = read_dataset(str(dataset_path))
        row = ds.samples[record : record + 1]
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
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > case.record, (
        f"dataset too short for regression check: record={case.record} path={case.dataset_rel}"
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
