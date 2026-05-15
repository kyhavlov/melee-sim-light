from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp

MSL_ACT_DAMAGE_FLY_N = 90


def _step_row(ds, record: int, *, player: int, prev_main_xy: tuple[int, int] | None = None):
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    row = ds.samples[record : record + 1]
    seed_t = row["seed_t"].copy()
    prev_input_t = row["prev_input_t"].copy()
    if prev_main_xy is not None:
        prev_input_t["p"]["main_x"][0, player] = np.int8(prev_main_xy[0])
        prev_input_t["p"]["main_y"][0, player] = np.int8(prev_main_xy[1])
    input_t = row["input_t"].copy()

    seed_bytes = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = (
        np.frombuffer(prev_input_t.tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, input_stride)
    )
    input_bytes = (
        np.frombuffer(input_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    )

    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return row["seed_t"][0], row["ref_t1"][0], out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


@pytest.mark.integration
def test_damage_hitlag_exit_di_uses_source_msl_trig_for_selfplay_launch() -> None:
    # Replay-real positive for ftCo_8008E5A4's DI math:
    # Damage_OnExitHitlag runs before current-frame input refresh, consumes the prior-frame stick,
    # and writes x8c_kb_vel through the GALE01 MSL atan2/sin/cos path. The self-play short-charge
    # AttackHi4 hit exits hitlag with a diagonal DI input; matching the source trig/fmadds order is
    # visible in the first post-hitlag DamageFlyN velocity and root position.
    #
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006D10C,Fighter_procUpdate}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnExitHitlag,ftCo_8008E5A4}
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Damage.s::ftCo_8008E5A4
    # refs/melee/src/MSL/trigf.c::{sinf,cosf}
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slp"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    player = 1
    seed, ref, out = _step_row(ds, 1423, player=player)
    assert int(seed["action_id"][player]) == MSL_ACT_DAMAGE_FLY_N
    assert int(seed["hitlag"][player]) == 1
    assert int(ref["hitlag"][player]) == int(out["hitlag"][player]) == 0

    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == MSL_ACT_DAMAGE_FLY_N
    assert float(out["speed_x_attack"][player]) == pytest.approx(
        float(ref["speed_x_attack"][player]), abs=1.0e-7
    )
    assert float(out["speed_y_attack"][player]) == pytest.approx(
        float(ref["speed_y_attack"][player]), abs=1.0e-7
    )
    assert float(out["pos_x"][player]) == pytest.approx(float(ref["pos_x"][player]), abs=1.0e-6)
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=1.0e-6)


@pytest.mark.integration
def test_damage_hitlag_exit_di_requires_prior_frame_stick_owner() -> None:
    # Boundary negative on the same row: neutralizing the prior-frame stick removes the DI input
    # consumed by Fighter_8006A1BC -> Fighter_8006D10C, so the launch must not match the replay
    # output. This keeps the source-exact trig repair scoped to real hitlag-exit DI ownership.
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slp"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    player = 1
    _seed, ref, out = _step_row(ds, 1423, player=player, prev_main_xy=(0, 0))
    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == MSL_ACT_DAMAGE_FLY_N
    assert int(out["hitlag"][player]) == int(ref["hitlag"][player]) == 0
    assert float(out["speed_x_attack"][player]) < 0.0
    assert abs(float(out["speed_x_attack"][player]) - float(ref["speed_x_attack"][player])) > 1.0
    assert abs(float(out["pos_x"][player]) - float(ref["pos_x"][player])) > 1.0
