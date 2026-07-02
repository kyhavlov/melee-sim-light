from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


ACT_DAMAGE_FLY_ROLL = 0x005B
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_DOWN_BOUND_D = 0x00BF


def _skip_if_marth_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/marth.json",
        "data/anims/marth.tracks.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _dataset_path(root: Path) -> Path:
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")
    return dataset_path


def _marth_dataset_path(root: Path) -> Path:
    dataset_rel = "replays/validation/marth/VictoriousSpitefulAlpaca.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")
    return dataset_path


def _binding_sizes():
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    return binding, int(sizes["seed"]), int(sizes["input"]), int(sizes["compare"])


def _step_one_row_with_seed(dataset_path: Path, record: int, seed: np.ndarray) -> tuple[np.void, np.void]:
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    ref = row["ref_t1"][0].copy()

    binding, seed_stride, input_stride, compare_stride = _binding_sizes()
    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    return out, ref


def _debug_selected_body_hit_count(dataset_path: Path, record: int, seed: np.ndarray) -> int:
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]

    binding, seed_stride, input_stride, _compare_stride = _binding_sizes()
    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        _raw, count = binding.debug_combat_select_body_hits(handle, 0, 64)
        return int(count)
    finally:
        binding.destroy(handle)


def _rollout_records(
    dataset_path: Path, start_record: int, target_records: tuple[int, ...]
) -> dict[int, tuple[np.void, np.void]]:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    target_max = max(target_records)
    assert int(samples.shape[0]) > target_max

    binding, seed_stride, input_stride, compare_stride = _binding_sizes()
    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        out_by_record: dict[int, tuple[np.void, np.void]] = {}
        for record in range(start_record, target_max + 1):
            row = samples[record : record + 1]
            prev_input_bytes = np.frombuffer(
                row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).copy().reshape(1, input_stride)
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            if record in target_records:
                out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
                out_by_record[record] = (samples[record]["ref_t1"].copy(), out)
        return out_by_record
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_downbound_x1994_timer_selects_post_anim_pose_for_attackdash_whiff_tbk_2402() -> None:
    # Replay-real lock for the DownBound post-Anim collision-pose owner:
    # - Damage hitlag exit called ftColl_8007B7A4(..., p_ftCommonData->x130), arming x1994.
    # - Slippi reports visible hurtbox_state=0, but the hidden x1994 timer marks this as the
    #   DownBound post-Anim pose episode already modeled in hurtboxes_refresh.
    # - Removing only x1994 makes the same AttackDash overlap a false high-hurtcap BODY hit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{ftCo_DownBound_Anim,ftCo_DownBound_Coll}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)
    ds = load_replay_buffers(str(dataset_path))

    p = 0
    seed = ds.rows[2402:2403]["seed_t"].copy()
    assert int(seed[0]["action_id"][p]) == ACT_DOWN_BOUND_D
    assert int(seed[0]["hurtbox_state"][p]) == 0
    assert int(seed[0]["colanim_hit_status_x198c"][p]) == 1
    assert int(seed[0]["colanim_timer_x1994"][p]) > 0

    out, ref = _step_one_row_with_seed(dataset_path, 2402, seed)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DOWN_BOUND_D
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-6)

    seed[0]["colanim_timer_x1994"][p] = np.uint16(0)
    out_without_timer, _ = _step_one_row_with_seed(dataset_path, 2402, seed)
    assert int(out_without_timer["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out_without_timer["hitlag"][p]) > 0
    assert float(out_without_timer["percent"][p]) > float(ref["percent"][p])


@pytest.mark.integration
def test_damageflyroll_x1994_rollout_carries_to_downbound_pose_bridge_tbk() -> None:
    # Rollout lock for the primary disruptive packet:
    # starting from DamageFlyRoll before floor contact must keep the damage-owned x1994 countdown
    # alive until DownBound so the existing DownBound post-Anim pose bridge samples the whiffing pose.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)
    ds = load_replay_buffers(str(dataset_path))

    p = 0
    start = 2375
    seed = ds.rows[start]["seed_t"]
    assert int(seed["action_id"][p]) == ACT_DAMAGE_FLY_ROLL
    assert int(seed["colanim_hit_status_x198c"][p]) == 1
    assert int(seed["colanim_timer_x1994"][p]) > 0

    by_record = _rollout_records(dataset_path, start, (2379, 2402, 2403))
    for record in (2379, 2402, 2403):
        ref, out = by_record[record]
        assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DOWN_BOUND_D, record
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0, record
        assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-6)


@pytest.mark.integration
def test_downbound_x1994_pose_bridge_clamps_terminal_pose_for_marth_vsa_651() -> None:
    # Replay-real lock for the terminal side of the DownBound x1994 pose bridge:
    # - VSA:651 is Marth DownBoundD action_frame=24 with x1994 still active.
    # - The frame scheduler has already advanced the live AObj to frame 25 before combat refresh;
    #   the x1994 seed bridge must not synthesize frame 26, past the extracted end_frame=26
    #   non-looping pose. That synthetic terminal pose admits a false AttackAirLw BODY contact.
    # - TBK:2402 above remains the adjacent positive proving the same bridge can still advance
    #   an earlier DownBound seed to the real post-Anim whiffing pose.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
    #   ftCo_DownBound_Anim,ftCo_DownBound_Coll
    # }
    # refs/melee/src/melee/ft/ftanim.c::ftAnim_IsFramesRemaining
    root = Path(__file__).resolve().parents[1]
    _skip_if_marth_artifacts_missing(root)
    dataset_path = _marth_dataset_path(root)
    ds = load_replay_buffers(str(dataset_path))

    defender = 1
    seed = ds.rows[651:652]["seed_t"].copy()
    assert int(seed[0]["action_id"][defender]) == ACT_DOWN_BOUND_D
    assert int(seed[0]["action_frame"][defender]) == 24
    assert int(seed[0]["hurtbox_state"][defender]) == 0
    assert int(seed[0]["colanim_hit_status_x198c"][defender]) == 1
    assert int(seed[0]["colanim_timer_x1994"][defender]) > 0

    assert _debug_selected_body_hit_count(dataset_path, 651, seed) == 0

    out, ref = _step_one_row_with_seed(dataset_path, 651, seed)
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == ACT_DOWN_BOUND_D
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 0
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 0
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)
