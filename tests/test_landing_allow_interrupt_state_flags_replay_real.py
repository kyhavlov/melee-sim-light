from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp


SELFPLAY_181413_SLP = Path("replays/validation/aggregate_recent/Game_20260514T181413.slpz")

ACT_ATTACK_AIR_B = 67
ACT_FALL = 29
ACT_LANDING = 42
SM_ATTACK_AIR_B = 70

STATE_FLAG_2218_ALLOW_INTERRUPT = 0x80


def _run_one_step_from_slp(record: int, *, seed_mutator=None) -> tuple[np.void, np.void, np.void]:
    if not SELFPLAY_181413_SLP.exists():
        pytest.skip(f"missing local replay: {SELFPLAY_181413_SLP}")
    ds = build_dataset_from_slp(
        slp_path=str(SELFPLAY_181413_SLP),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    row = ds.samples[record : record + 1].copy()
    if seed_mutator is not None:
        seed_mutator(row["seed_t"])
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


def _run_one_step_from_dataset(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1].copy()
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
def test_late_attackairb_basic_landing_carries_raw_allow_interrupt_selfplay_181413() -> None:
    # Self-play 181413 rec1522 is a late Fox AttackAirB floor contact:
    # - AttackAirB's command script already set fp->allow_interrupt.
    # - ftCo_AttackAir_Coll enters ftCo_Landing_Enter_Basic, which writes
    #   mv.co.landing.allow_interrupt=true but does not clear the raw fp+0x2218 bit.
    # - Slippi therefore publishes state_flags[0] bit0x80 on the Landing entry row.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
    #   ftCo_Landing_Enter,ftCo_Landing_Enter_Basic}
    seed, ref, out = _run_one_step_from_slp(1522)
    p = 0
    assert int(seed["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_LANDING
    assert int(ref["state_flags"][p][0]) & STATE_FLAG_2218_ALLOW_INTERRUPT
    assert int(out["state_flags"][p][0]) & STATE_FLAG_2218_ALLOW_INTERRUPT


@pytest.mark.integration
def test_early_attackairb_basic_landing_does_not_create_raw_allow_interrupt_selfplay_181413() -> None:
    # Negative boundary for the same owner: basic Landing is interruptible through
    # mv.co.landing.allow_interrupt, but raw fp+0x2218 bit0 is only carried if the source aerial's
    # command script had actually set fp->allow_interrupt. An early synthetic floor contact must not
    # manufacture that raw bit.
    def early_attackairb(seed_t: np.ndarray) -> None:
        p = 0
        seed_t["action_id"][0, p] = np.uint16(ACT_ATTACK_AIR_B)
        seed_t["animation_index"][0, p] = np.uint32(SM_ATTACK_AIR_B)
        seed_t["action_frame"][0, p] = np.int16(1)
        seed_t["anim_frame_f32"][0, p] = np.float32(1.0)
        seed_t["state_flags"][0, p, 0] = np.uint8(0)

    _seed, _ref, out = _run_one_step_from_slp(1522, seed_mutator=early_attackairb)
    p = 0
    assert int(out["action_id"][p]) == ACT_LANDING
    assert (int(out["state_flags"][p][0]) & STATE_FLAG_2218_ALLOW_INTERRUPT) == 0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        ("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl", 3693, 0),
        ("datasets/sheik/replays/validation/sheik/ToughOutlyingChicken.msl", 2340, 0),
        ("datasets/sheik/replays/validation/sheik/RuralReasonableRat.msl", 3492, 0),
    ],
)
def test_late_sheik_attackairb_fall_carries_raw_allow_interrupt(
    dataset_rel: str, record: int, p: int
) -> None:
    # Sheik bair's allow-interrupt command can run before AttackAir_Anim enters Fall on animation
    # expiry. ftCo_Fall_Enter does not clear the raw fp+0x2218_b0 command bit, so Slippi publishes
    # it on the first Fall row.
    #
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
    # data/moves/sheik.json::ftCo_SM_AttackAirB events allow_interrupt
    seed, ref, out = _run_one_step_from_dataset(Path(dataset_rel), record)
    assert int(seed["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(ref["action_id"][p]) == ACT_FALL
    assert int(ref["state_flags"][p][0]) & STATE_FLAG_2218_ALLOW_INTERRUPT
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["state_flags"][p][0]) & STATE_FLAG_2218_ALLOW_INTERRUPT


@pytest.mark.integration
def test_sheik_attackairb_quiet_frame_before_fall_does_not_set_allow_interrupt() -> None:
    # Adjacent quiet-frame lock: one frame before AttackAir_Anim enters Fall, Sheik bair has not
    # crossed the source allow-interrupt command window in the runtime table, so the raw bit stays
    # quiet.
    seed, ref, out = _run_one_step_from_dataset(
        Path("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl"), 3692
    )
    p = 0
    assert int(seed["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(ref["action_id"][p]) == ACT_ATTACK_AIR_B
    assert (int(ref["state_flags"][p][0]) & STATE_FLAG_2218_ALLOW_INTERRUPT) == 0
    assert (int(out["state_flags"][p][0]) & STATE_FLAG_2218_ALLOW_INTERRUPT) == 0
