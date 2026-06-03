from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_DAMAGE_FLY_ROLL = 0x005B
ACT_THROW_HI = 0x00DD
ROLL_CLOCK_NONE = 0
ROLL_CLOCK_REPLAY_FRAME_SEED = 2


def _dataset_path(root: Path) -> Path:
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    return dataset_path


def _bytes(row_field: np.ndarray, stride: int) -> np.ndarray:
    return np.frombuffer(row_field.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, stride)


@pytest.mark.integration
def test_tbk_post_detach_throw_blaster_rollout_clock_reaches_damageflyroll_gate() -> None:
    # TBK 2748 starts after Falco ThrowHi has detached Fox into same-source throw-laser hitstun.
    # Rollout must keep the replay frame-start RNG clock moving until the delayed ftCo_8008DCE0
    # DamageFlyRoll gate, while the ThrowHi 4/3 anim-rate owner keeps action_frame aligned.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset_path(root)
    ds = read_dataset(str(dataset_path))
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    start_record = 2748
    target_record = 2752
    thrower = 1
    victim = 0
    seed = ds.samples[start_record : start_record + 1]["seed_t"]
    assert int(seed[0]["action_id"][thrower]) == ACT_THROW_HI
    assert int(seed[0]["hitstun"][victim]) > 0
    assert int(seed[0]["last_hit_by"][victim]) == int(seed[0]["source_port0"][thrower])
    assert int(seed[0]["instance_hit_by"][victim]) == int(seed[0]["instance_id"][thrower])

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    out = np.empty((1, compare_stride), dtype=np.uint8)
    try:
        binding.reseed_seed_rollout(handle, _bytes(seed, seed_stride))
        assert int(binding.debug_get_rollout_clock_mode(handle, 0)) == ROLL_CLOCK_REPLAY_FRAME_SEED
        for record in range(start_record, target_record + 1):
            row = ds.samples[record : record + 1]
            binding.step_input(
                handle,
                _bytes(row["prev_input_t"], input_stride),
                _bytes(row["input_t"], input_stride),
            )
        binding.write_compare(handle, out)
    finally:
        binding.destroy(handle)

    actual = out.view(COMPARE_DTYPE).reshape(1)[0]
    ref = ds.samples[target_record]["ref_t1"]
    assert int(actual["frame_pre_random_seed"]) == int(ref["frame_pre_random_seed"])
    assert int(actual["action_frame"][thrower]) == int(ref["action_frame"][thrower])
    assert int(actual["action_id"][victim]) == int(ref["action_id"][victim]) == ACT_DAMAGE_FLY_ROLL


@pytest.mark.integration
def test_tbk_throw_blaster_rollout_clock_requires_live_same_source_article() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset_path(root)
    ds = read_dataset(str(dataset_path))
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])

    seed = ds.samples[2748:2749]["seed_t"].copy()
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, _bytes(seed, seed_stride))
        assert int(binding.debug_get_rollout_clock_mode(handle, 0)) == ROLL_CLOCK_REPLAY_FRAME_SEED

        no_article = seed.copy()
        no_article[0]["items"]["exists"] = np.uint8(0)
        binding.reseed_seed_rollout(handle, _bytes(no_article, seed_stride))
        assert int(binding.debug_get_rollout_clock_mode(handle, 0)) == ROLL_CLOCK_NONE

        no_source_instance = seed.copy()
        no_source_instance[0]["instance_hit_by"][0] = np.uint16(0)
        binding.reseed_seed_rollout(handle, _bytes(no_source_instance, seed_stride))
        assert int(binding.debug_get_rollout_clock_mode(handle, 0)) == ROLL_CLOCK_NONE

        normal_reseed = seed.copy()
        binding.reseed_seed(handle, _bytes(normal_reseed, seed_stride))
        assert int(binding.debug_get_rollout_clock_mode(handle, 0)) == ROLL_CLOCK_NONE
    finally:
        binding.destroy(handle)
