from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp


TCH = Path("datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl")
FEH_SLP = Path("replays/validation/dream_land_recent/FlippantEnchantedHorse.slp")
SELFPLAY_181413_SLP = Path("replays/validation/aggregate_recent/Game_20260514T181413.slp")


def _skip_if_dataset_missing(ds_path: Path) -> None:
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path}")


def _run_one_step_row(ds_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    _skip_if_dataset_missing(ds_path)
    ds = read_dataset(str(ds_path))
    row = ds.samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    return seed, ref, out


def _run_one_step_from_slp(slp_path: Path, record: int, *, ports: list[int]) -> tuple[np.void, np.void, np.void]:
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=ports,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    row = ds.samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    return seed, ref, out


@pytest.mark.integration
def test_damage_air_tap_jump_x14_refresh_uses_x671_window_replay_real() -> None:
    # TCH 11852 is the top F08d rollout island's direct owner:
    # - an earlier XY press left a stale high x14 snapshot (38),
    # - the later UCF-processed tap-jump crosses the threshold on x671=1,
    # - doIasa refreshes x14 into the <=x1D0 window before terminal Damage_IASA enters JumpAerialF.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,ftCo_Damage_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_GetInput
    seed, ref, out = _run_one_step_row(TCH, 11852)
    p = 0
    assert int(seed["action_id"][p]) == 86  # DamageAir3
    assert int(seed["damage_jump_buffer_x14"][p]) == 17
    assert int(seed["tilt_timer_y"][p]) == 254

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 27  # JumpAerialF
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p]) == 0
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]))


@pytest.mark.integration
def test_damage_air_tap_jump_x14_does_not_refresh_below_threshold_replay_real() -> None:
    # The first x671=0 frame in the same TCH sequence is below tap-jump threshold after UCF diagonal
    # adjustment, so the stale high x14 remains inert and must not enter JumpAerialF early.
    seed, ref, out = _run_one_step_row(TCH, 11834)
    p = 0
    assert int(seed["action_id"][p]) == 86  # DamageAir3
    assert int(seed["tilt_timer_y"][p]) == 0
    assert int(seed["damage_jump_buffer_x14"][p]) == 38

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 86
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p]) == 1


@pytest.mark.integration
def test_flyreflectwall_terminal_x14_buffer_enters_jumpaerialb_replay_real() -> None:
    # FEH 9274 is a FlyReflectWall terminal hitstun row:
    # - ftCo_FlyReflect_IASA delegates to ftCo_DamageFly_IASA during active hitstun.
    # - Y pressed earlier in the reflected-hitstun episode writes mv.co.damage.x14 through doIasa.
    # - On the terminal Anim frame, inlineC0 consumes x14 before the fallback DamageFall handoff,
    #   entering JumpAerialB with the source jump velocity.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::{
    #   ftCo_FlyReflect_Anim,ftCo_FlyReflect_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,inlineC0,ftCo_DamageFly_Anim}
    seed, ref, out = _run_one_step_from_slp(FEH_SLP, 9274, ports=[1, 2])
    p = 0
    assert int(seed["action_id"][p]) == 247  # FlyReflectWall
    assert int(seed["hitstun"][p]) == 1
    assert int(seed["damage_jump_buffer_x14"][p]) == 7

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 28  # JumpAerialB
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p]) == 0
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]))
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]))


@pytest.mark.integration
def test_damagefly_terminal_x14_jump_entry_uses_buffered_xy_then_live_drift_selfplay_181413() -> None:
    # Game_20260514T181413 1530 is a terminal DamageFlyN x14 inlineC0 row:
    # - mv.co.damage.x14 is inside the p_ftCommonData->x1D0 buffer window,
    # - the buffered frame-start XY owner has neutral X while the current stick is rightward,
    # - JumpAerial entry therefore starts from neutral x velocity, then the destination
    #   JumpAerial Phys callback applies current-stick air drift in the same frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{inlineC0,ftCo_DamageFly_Anim}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{
    #   ftCo_JumpAerial_Enter_Basic,ftCo_JumpAerial_Phys}
    seed, ref, out = _run_one_step_from_slp(SELFPLAY_181413_SLP, 1530, ports=[1, 2])
    p = 1
    assert int(seed["action_id"][p]) == 88  # DamageFlyN
    assert int(seed["hitstun"][p]) == 1
    assert int(seed["damage_jump_buffer_x14"][p]) == 6

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 27  # JumpAerialF
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]))
    assert float(out["speed_air_x_self"][p]) == pytest.approx(float(ref["speed_air_x_self"][p]))
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]))


@pytest.mark.integration
def test_damagefly_high_x14_current_button_jump_keeps_current_xy_selfplay_181413() -> None:
    # Game_20260514T181413 859 is the adjacent negative owner: x14 is above the inlineC0 buffer
    # window, so the row enters JumpAerial from the current-frame button jump path and must keep
    # current-stick horizontal jump velocity rather than frame-start buffered XY.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{inlineC0,ftCo_DamageFly_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
    seed, ref, out = _run_one_step_from_slp(SELFPLAY_181413_SLP, 859, ports=[1, 2])
    p = 0
    assert int(seed["action_id"][p]) == 90  # DamageFlyTop
    assert int(seed["hitstun"][p]) == 1
    assert int(seed["damage_jump_buffer_x14"][p]) == 28

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 27  # JumpAerialF
    assert float(out["speed_air_x_self"][p]) == pytest.approx(float(ref["speed_air_x_self"][p]))
    assert float(out["speed_air_x_self"][p]) != pytest.approx(0.0)
