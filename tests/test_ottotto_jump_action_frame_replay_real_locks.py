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
    seed_stride = int(sizes["seed"] if "seed" in sizes else sizes["seed_v0"])
    input_stride = int(sizes["input"] if "input" in sizes else sizes["input_v0"])
    compare_stride = int(sizes["compare"] if "compare" in sizes else sizes["compare_v0"])

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    record: int
    note: str


_CASES = (
    _Case(record=8606, note="target-1 steady Ottotto frame"),
    _Case(record=8607, note="target Ottotto jump-trigger frame"),
    _Case(record=8608, note="target+1 post-jump frame"),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"rec{c.record}")
def test_ottotto_jump_action_frame_rows_match_replay(case: _Case) -> None:
    # Replay-real lock for Ottotto jump IASA action-frame parity:
    # - Ottotto / OttottoWait IASA routes through ftCo_Jump_CheckInput before Dash / Turn / Walk.
    # - The target row is a positive-Y jump edge from steady Ottotto on FD.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{
    #   ftCo_Ottotto_IASA,ftCo_OttottoWait_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_CheckInput
    dataset_rel = "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    assert int(ds.rows.shape[0]) > case.record, f"replay too short for record={case.record}"
    row = ds.rows[case.record : case.record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    p = 0

    if case.record == 8607:
        assert int(seed["action_id"][p]) == 245, case.note  # ftCo_MS_Ottotto
        assert int(seed["animation_index"][p]) == 210, case.note  # ftCo_SM_Ottotto
        assert int(seed["on_ground"][p]) == 1, case.note
        assert int(seed["jumps_left"][p]) == 2, case.note
        assert int(row["input_t"][0]["p"][p]["main_y"]) == 101, case.note
        assert int(ref["action_frame"][p]) == 0, case.note

    out = _one_step_out_compare(ds=ds, row=row)[0]
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p]), (
        f"{Path(dataset_rel).name} rec{case.record} p{p}: "
        f"action_frame {int(out['action_frame'][p])} != {int(ref['action_frame'][p])}"
    )
