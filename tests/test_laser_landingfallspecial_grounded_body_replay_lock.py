from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


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

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    name: str
    record: int
    p: int
    family: str


_DATASET_REL = (
    "replays/validation/cardinal_1.0_recent/"
    "AttachedGoodNaturedGuanaco.slpz"
)

_CASES = [
    _Case(name="agn_lfs_laser_adj_pre", record=3104, p=1, family="agn_lfs_laser"),
    _Case(name="agn_lfs_laser_target", record=3105, p=1, family="agn_lfs_laser"),
    _Case(name="agn_lfs_laser_adj_post", record=3106, p=1, family="agn_lfs_laser"),
    _Case(name="agn_phantom_adj_pre", record=200, p=1, family="agn_phantom"),
    _Case(name="agn_phantom_target", record=201, p=1, family="agn_phantom"),
    _Case(name="agn_phantom_adj_post", record=202, p=1, family="agn_phantom"),
]


def _laser_types(items_row: np.ndarray) -> list[int]:
    return [int(it["type"]) for it in items_row if int(it["exists"]) and int(it["type"]) in (54, 55)]


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: c.name)
def test_laser_landingfallspecial_grounded_body_replay_rows(case: _Case) -> None:
    # Replay-real locks for src/items.c grounded laser BODY parity.
    #
    # Decomp anchors:
    # - refs/melee/src/melee/it/itcoll.c::it_80272460
    # - refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
    #     ftCo_LandingFallSpecial_Enter,ftCo_LandingFallSpecial_Anim}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _DATASET_REL
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {_DATASET_REL}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > case.record, f"replay too short for record={case.record}"
    row = samples[case.record : case.record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    p = int(case.p)

    if case.family == "agn_lfs_laser":
        assert int(seed["action_id"][p]) in (43, 78)
        assert int(seed["on_ground"][p]) == 1
        assert _laser_types(seed["items"]) == [55] or _laser_types(seed["items"]) == []
    elif case.family == "agn_phantom":
        assert int(seed["action_id"][p]) == 212
        assert int(seed["on_ground"][p]) == 1
    else:
        raise AssertionError(f"unexpected family: {case.family}")

    out = _one_step_out_compare(ds=ds, row=row)[0]

    for field in ("action_id", "animation_index", "on_ground", "hitlag", "hitstun"):
        got = int(out[field][p])
        exp = int(ref[field][p])
        assert got == exp, f"{case.name}: field={field} expected={exp} got={got}"

    got_flags = [int(x) for x in out["state_flags"][p]]
    exp_flags = [int(x) for x in ref["state_flags"][p]]
    assert got_flags == exp_flags, f"{case.name}: state_flags expected={exp_flags} got={got_flags}"

    got_items = _laser_types(out["items"])
    exp_items = _laser_types(ref["items"])
    assert got_items == exp_items, f"{case.name}: laser types expected={exp_items} got={got_items}"
