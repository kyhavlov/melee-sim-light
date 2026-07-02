from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
SM_WAIT1_0 = 2
CHAR_FOX = 1
STAGE_FD = 2
ROLLOUT_CLOCK_HSD_RAND_STREAM = 1


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _step_seed(seed: np.ndarray, *, rng_owned: bool = False) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.zeros((1, input_stride), dtype=np.uint8)
    input_bytes = np.zeros((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.reseed_seed(handle, seed_bytes)
        if rng_owned:
            binding.debug_set_rollout_clock_mode(handle, 0, ROLLOUT_CLOCK_HSD_RAND_STREAM)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


def _step_seed_with_replay_frame_rng(seed: np.ndarray, *, rng_owned: bool = False) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.zeros((1, input_stride), dtype=np.uint8)
    input_bytes = np.zeros((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.reseed_seed(handle, seed_bytes)
        if rng_owned:
            binding.debug_set_rollout_clock_mode(handle, 0, ROLLOUT_CLOCK_HSD_RAND_STREAM)
        binding.step_input_replay_frame_rng(handle, seed_bytes, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


def _wait_seed(frame_pre_random_seed: int, action_frame: int = 119) -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["frame_pre_random_seed"][0] = np.uint32(frame_pre_random_seed)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["action_frame"][0, :2] = np.int16(action_frame)
    seed["anim_frame_f32"][0, :2] = np.float32(float(action_frame))
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    return seed


def test_wait_loop_selects_source_weighted_idle_variant_from_rng() -> None:
    # ftCo_8008A7A8 calls getAnimID at the Wait animation-end gate. Fox's extracted WaitStruct
    # table is [Wait1_0:70, Wait1_1:30], so an HSD_Randi(100)+1 sample of 71 selects submotion 3.
    # refs/melee/src/melee/ft/ftwaitanim.c::{ftCo_8008A7A8,getAnimID}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    out = _step_seed(_wait_seed(14037), rng_owned=True)
    p = 0
    assert int(out["action_id"][p]) == ACT_WAIT
    assert int(out["action_frame"][p]) == 0
    assert int(out["animation_index"][p]) == 3


def test_wait_loop_can_keep_current_idle_variant_from_rng() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    out = _step_seed(_wait_seed(1), rng_owned=True)
    p = 0
    assert int(out["action_id"][p]) == ACT_WAIT
    assert int(out["action_frame"][p]) == 0
    assert int(out["animation_index"][p]) == SM_WAIT1_0


def test_wait_loop_replay_seed_keeps_visible_idle_variant_when_rng_phase_hidden() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    out = _step_seed(_wait_seed(14037), rng_owned=False)
    p = 0
    assert int(out["action_id"][p]) == ACT_WAIT
    assert int(out["action_frame"][p]) == 0
    assert int(out["animation_index"][p]) == SM_WAIT1_0


def test_wait_loop_replay_frame_seed_overrides_match_init_clock_owner_tvr() -> None:
    # Replay playback feeds the current Slippi frame-start RNG before source callbacks run. A
    # rollout that began at match init can still carry HSD_STREAM clock metadata, but a terminal
    # Wait_Anim/getAnimID site in replay playback must consume the installed replay seed.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/ft/ftwaitanim.c::{ftCo_8008A7A8,getAnimID}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    out = _step_seed_with_replay_frame_rng(_wait_seed(1), rng_owned=True)
    p = 0
    assert int(out["action_id"][p]) == ACT_WAIT
    assert int(out["action_frame"][p]) == 0
    assert int(out["animation_index"][p]) == SM_WAIT1_0
