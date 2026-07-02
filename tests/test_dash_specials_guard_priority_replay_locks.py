from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


_BASE = Path("replays/validation")
_ACT_DASH = 20
_ACT_FALCO_SPECIAL_S_START = 347


def _step_one_record(row: np.ndarray, num_players: int):
    msl_binding = pytest.importorskip("msl_binding")
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, seed_stride
    ).copy()
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    ).copy()
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    ).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=int(num_players))
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_compare_bytes)
    finally:
        msl_binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]


@pytest.mark.integration
def test_dash_sideb_input_preempts_same_frame_guardreflect() -> None:
    # Source owner:
    # Dash_IASA checks ftCo_SpecialS_CheckInput before item/CatchDash/AttackDash and before the
    # later ftCo_80091AD8 guard branch. A held shield trigger on the same frame must not route Dash
    # to GuardReflect when B+side already entered Side-B.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{
    #   ftCo_SpecialS_CheckInput,ftCo_SpecialS_HasInput}
    root = Path(__file__).resolve().parents[1]
    path = root / _BASE / "dream_land_recent/ShadyDecimalStarling.slpz"
    if not path.exists():
        pytest.skip(f"missing local replay: {_BASE / 'dream_land_recent/ShadyDecimalStarling.slpz'}")

    ds = load_replay_buffers(str(path))
    record = 3171
    if int(ds.rows.shape[0]) <= record:
        pytest.skip(f"replay too short for record {record}: {path}")

    row = ds.rows[record : record + 1]
    p = 1
    assert int(row["seed_t"]["action_id"][0, p]) == _ACT_DASH
    assert int(row["ref_t1"]["action_id"][0, p]) == _ACT_FALCO_SPECIAL_S_START

    out = _step_one_record(row, int(ds.num_players))
    assert int(out["action_id"][p]) == _ACT_FALCO_SPECIAL_S_START
