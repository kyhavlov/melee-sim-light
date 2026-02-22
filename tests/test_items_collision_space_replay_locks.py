from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


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

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
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
