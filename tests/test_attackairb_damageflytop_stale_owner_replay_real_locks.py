from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


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


_CASES = [
    _Case(
        name="dcc_attackairb_stale_owner_adj_pre",
        dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
        record=3151,
        p=0,
        family="dcc_damageflytop",
    ),
    _Case(
        name="dcc_attackairb_stale_owner_target",
        dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
        record=3152,
        p=0,
        family="dcc_damageflytop",
    ),
    _Case(
        name="dcc_attackairb_stale_owner_adj_post",
        dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
        record=3153,
        p=0,
        family="dcc_damageflytop",
    ),
    _Case(
        name="qgd_attackairb_control_pre",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
        ),
        record=8637,
        p=1,
        family="qgd_control",
    ),
    _Case(
        name="qgd_attackairb_control_post",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
        ),
        record=8639,
        p=1,
        family="qgd_control",
    ),
    _Case(
        name="tbk_attackairb_control_pre0",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=6993,
        p=1,
        family="tbk_control",
    ),
    _Case(
        name="tbk_attackairb_control_pre1",
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
        ),
        record=6994,
        p=1,
        family="tbk_control",
    ),
]


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: c.name)
def test_attackairb_damageflytop_stale_owner_subset_rows(case: _Case) -> None:
    # Replay-real lock for the kept AttackAirB continuation stale-owner subset.
    #
    # Decomp anchors:
    # - BODY rehit suppression is keyed by HitCapsule.victims_1 presence:
    #   refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
    # - BODY hit acceptance / attribution rewrite happens on confirmed continuation contact:
    #   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    #
    # Scope kept in runtime:
    # - late DamageFlyTop continuation rows from an older same-port attacker instance (DCC 3152)
    # - adjacent QGD/TBK controls stay replay-exact while the phantom / DamageFlyN follow-on slice
    #   remains unmodeled.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > case.record, f"dataset too short for record={case.record}"
    row = samples[case.record : case.record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    p = int(case.p)

    if case.family == "dcc_damageflytop":
        assert int(seed["last_hit_by"][p]) == 1
        if case.record <= 3152:
            assert int(seed["action_id"][p]) == 90  # DamageFlyTop
        else:
            assert int(seed["action_id"][p]) == 88  # DamageFlyN post-hit control
        if case.record == 3152:
            assert int(seed["instance_hit_by"][p]) != int(seed["instance_id"][1])
    elif case.family == "qgd_control":
        assert int(seed["action_id"][p]) == 90  # DamageFlyTop
        assert int(seed["last_hit_by"][p]) == 0
    elif case.family == "tbk_control":
        assert int(seed["action_id"][p]) == 91  # DamageFlyN
        assert int(seed["last_hit_by"][p]) == 0
    else:
        raise AssertionError(f"unexpected family: {case.family}")

    out = _one_step_out_compare(ds=ds, row=row)[0]

    for field in ("action_id", "action_frame", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        got = int(out[field][p])
        exp = int(ref[field][p])
        assert got == exp, f"{case.name}: field={field} expected={exp} got={got}"

    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-6), case.name
