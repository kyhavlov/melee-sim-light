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
    dataset_rel: str
    record: int
    p: int


_CASES = [
    _Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
        record=3901,
        p=1,
    ),
    _Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
        record=3902,
        p=1,
    ),
    _Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
        record=3903,
        p=1,
    ),
]


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).name}:rec{c.record}_p{c.p}")
def test_ottotto_dash_flick_rows_match_replay(case: _Case) -> None:
    # Decomp:
    # - Ottotto / OttottoWait IASA routes through ftCo_Dash_CheckInput before Turn / Walk.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
    #   ftCo_Ottotto_IASA,ftCo_OttottoWait_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
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

    if case.record == 3902:
        assert int(seed["action_id"][p]) == 245  # ftCo_MS_Ottotto
        assert int(ref["action_id"][p]) == 18  # ftCo_MS_Turn
    out = _one_step_out_compare(ds=ds, row=row)[0]

    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "facing",
        "hitlag",
        "hitstun",
        "jumps_left",
        "ground_id",
        "instance_id",
        "state_flags",
    ):
        got = out[field][p]
        exp = ref[field][p]
        if hasattr(got, "tolist"):
            got = got.tolist()
            exp = exp.tolist()
        else:
            got = int(got)
            exp = int(exp)
        assert got == exp, f"{Path(case.dataset_rel).name} rec{case.record} p{p}: {field} {got} != {exp}"
