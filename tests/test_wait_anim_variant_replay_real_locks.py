from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, SEED_DTYPE, read_dataset


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


def _run_one_step(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(
        row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, input_stride)
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    return row["seed_t"].reshape(-1)[0].copy(), row["ref_t1"].reshape(-1)[0].copy(), out


def _run_rollout_replay_frame_rng(dataset_path: Path, start: int, stop: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    def field_bytes(record: int, field: str, stride: int) -> np.ndarray:
        return np.frombuffer(samples[record : record + 1][field].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, stride
        )

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, field_bytes(start, "seed_t", seed_stride))
        for record in range(start, stop + 1):
            binding.step_input_replay_frame_rng(
                handle,
                field_bytes(record, "seed_t", seed_stride),
                field_bytes(record, "prev_input_t", input_stride),
                field_bytes(record, "input_t", input_stride),
            )
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return samples[stop]["ref_t1"].copy(), out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


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
    # Wait_Anim/getAnimID site in replay playback must consume the installed replay seed. TVR:9935
    # is the rollout-real positive: stale match-init stream selected Wait1_1, while the replay row's
    # frame seed keeps Wait1_0.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/ft/ftwaitanim.c::{ftCo_8008A7A8,getAnimID}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    out = _step_seed_with_replay_frame_rng(_wait_seed(1), rng_owned=True)
    p = 0
    assert int(out["action_id"][p]) == ACT_WAIT
    assert int(out["action_frame"][p]) == 0
    assert int(out["animation_index"][p]) == SM_WAIT1_0


def test_wait_loop_tvr_deadupstar_frame26_active_effect_prefix_selects_visible_variant() -> None:
    # TVR:9935 reaches Wait_Anim while the other player is in DeadUpStar phase 1 at action_frame 26.
    # The live async effect generator 0x121 owns one same-frame RNG step before getAnimID; consuming
    # the replay frame seed directly would choose Wait1_1, while the source prefix keeps Wait1_0.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpStar_Anim
    # refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch case 0x42D
    # refs/melee/src/melee/ef/eflib.c::efLib_CreateGenerator case 0x121
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing TVR dataset")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[9935]["seed_t"]
    assert int(seed["action_id"][0]) == ACT_WAIT
    assert int(seed["action_frame"][0]) == 119
    assert int(seed["action_id"][1]) == 4  # DeadUpStar.
    assert int(seed["action_frame"][1]) == 26

    ref, out = _run_rollout_replay_frame_rng(dataset_path, 9812, 9935)
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_WAIT
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 0
    assert int(out["animation_index"][0]) == int(ref["animation_index"][0]) == SM_WAIT1_0


def test_wait_loop_bhh_deadupstar_frame28_does_not_carry_active_effect_prefix() -> None:
    # BHH:10161 is the adjacent stale-late DeadUpStar control. A site-25 prefix at action_frame 28
    # would flip the Wait choice to Wait1_1, so this guards the active-effect bound from becoming a
    # generic DeadUpStar replay prefix.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing BHH dataset")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[10161]["seed_t"]
    assert int(seed["action_id"][0]) == ACT_WAIT
    assert int(seed["action_frame"][0]) == 119
    assert int(seed["action_id"][1]) == 4  # DeadUpStar.
    assert int(seed["action_frame"][1]) == 28

    ref, out = _run_rollout_replay_frame_rng(dataset_path, 10161, 10161)
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_WAIT
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 0
    assert int(out["animation_index"][0]) == int(ref["animation_index"][0]) == SM_WAIT1_0


def test_wait_loop_maj_deadupstar_frame4_uses_startup_prefix_not_active_tail() -> None:
    # MAJ:2748 is the adjacent visible-startup-frame negative for the DeadUpStar generator prefix.
    # The same action_frame window is only live in the final phase-1 tail; timer 82 is an earlier
    # loop pass, and adding the site-25 prefix flips Wait1_0 to Wait1_1.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpStar_Anim
    # refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch case 0x42D
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "MotionlessAggressiveJay.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing MAJ dataset")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[2748]["seed_t"]
    assert int(seed["action_id"][1]) == ACT_WAIT
    assert int(seed["action_frame"][1]) == 119
    assert int(seed["action_id"][0]) == 4  # DeadUpStar.
    assert int(seed["action_frame"][0]) == 4

    ref, out = _run_rollout_replay_frame_rng(dataset_path, 2625, 2748)
    assert int(out["action_id"][1]) == int(ref["action_id"][1]) == ACT_WAIT
    assert int(out["action_frame"][1]) == int(ref["action_frame"][1]) == 0
    assert int(out["animation_index"][1]) == int(ref["animation_index"][1]) == SM_WAIT1_0


def test_wait_loop_does_not_change_submotion_before_loop_boundary_pte() -> None:
    # One frame before the source idle-choice gate, Wait continues on its current submotion.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local FoD dataset")

    seed, ref, out = _run_one_step(dataset_path, 4109)
    p = 0
    assert int(seed["action_id"][p]) == ACT_WAIT
    assert int(ref["action_id"][p]) == ACT_WAIT

    assert int(out["action_id"][p]) == ACT_WAIT
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
