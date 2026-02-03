from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import read_dataset
from tools.slippi.anim_timebase import derive_frame_speed_mul_f32, load_end_frame_tables


@pytest.mark.integration
def test_frame_speed_mul_f32_prefix_invariant_window_attachedgoodnaturedguanaco() -> None:
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples

    # Player 0 includes the Shine Start hitlag cluster window.
    p = 0
    seed = samples["seed_t"]
    frame_id = seed["frame_id"].astype(np.int32)

    # Anchor the window to the known offender frame id.
    target_seed_frame = 1109
    idxs = np.where(frame_id == target_seed_frame)[0]
    assert idxs.size == 1, f"expected exactly one seed frame_id={target_seed_frame}, got {idxs.size}"
    i = int(idxs[0])

    prefix_end = i + 64
    assert prefix_end < int(seed.shape[0]), "dataset too short for prefix window"

    # Inputs to derive_frame_speed_mul_f32 are per-post-frame; use the seed_t snapshots as a
    # replay prefix proxy (n = num_samples = num_post_frames - 1).
    state_age_f32 = seed["anim_frame_f32"][:prefix_end, p].astype(np.float32)
    action_id = seed["action_id"][:prefix_end, p].astype(np.uint16)
    hitlag = seed["hitlag"][:prefix_end, p].astype(np.uint16)
    char_id = seed["char_id"][:prefix_end, p].astype(np.uint8)
    animation_index = seed["animation_index"][:prefix_end, p].astype(np.uint32)
    lr_press_timer = seed["lr_press_timer"][:prefix_end, p].astype(np.uint8)

    # Sanity: ensure this window actually includes action changes during hitlag (the bug class).
    changed = (action_id[1:] != action_id[:-1]) | (animation_index[1:] != animation_index[:-1]) | (char_id[1:] != char_id[:-1])
    assert np.any(changed & (hitlag[1:] != 0)), "expected at least one action/msid change under hitlag in window"

    common = json.loads((root / "data/common/ft_common_data.json").read_text())
    fox = json.loads((root / "data/characters/fox.json").read_text())
    falco = json.loads((root / "data/characters/falco.json").read_text())
    char_landing_air_lag_frames = {
        1: {
            "airn": int(fox["landing_airn_lag_frames"]),
            "airf": int(fox["landing_airf_lag_frames"]),
            "airb": int(fox["landing_airb_lag_frames"]),
            "airhi": int(fox["landing_airhi_lag_frames"]),
            "airlw": int(fox["landing_airlw_lag_frames"]),
        },
        22: {
            "airn": int(falco["landing_airn_lag_frames"]),
            "airf": int(falco["landing_airf_lag_frames"]),
            "airb": int(falco["landing_airb_lag_frames"]),
            "airhi": int(falco["landing_airhi_lag_frames"]),
            "airlw": int(falco["landing_airlw_lag_frames"]),
        },
    }

    end_frames = load_end_frame_tables(root / "data")

    full = derive_frame_speed_mul_f32(
        state_age_f32=state_age_f32,
        action_id=action_id,
        hitlag=hitlag,
        char_id=char_id,
        animation_index=animation_index,
        lr_press_timer=lr_press_timer,
        end_frames=end_frames,
        common_lcancel_window_frames=int(common["lcancel_window_frames"]),
        common_lcancel_lag_div=float(common["lcancel_lag_div"]),
        common_landing_fall_special_lag_frames=float(common["landing_fall_special_lag_frames"]),
        char_landing_air_lag_frames=char_landing_air_lag_frames,
    )

    # Prefix-invariance: deriving on any strict prefix must match the prefix of the full derivation.
    for cut in (i - 4, i - 1, i, i + 1, i + 8, prefix_end - 1):
        assert 1 < cut < prefix_end
        got = derive_frame_speed_mul_f32(
            state_age_f32=state_age_f32[:cut],
            action_id=action_id[:cut],
            hitlag=hitlag[:cut],
            char_id=char_id[:cut],
            animation_index=animation_index[:cut],
            lr_press_timer=lr_press_timer[:cut],
            end_frames=end_frames,
            common_lcancel_window_frames=int(common["lcancel_window_frames"]),
            common_lcancel_lag_div=float(common["lcancel_lag_div"]),
            common_landing_fall_special_lag_frames=float(common["landing_fall_special_lag_frames"]),
            char_landing_air_lag_frames=char_landing_air_lag_frames,
        )
        np.testing.assert_array_equal(got, full[:cut])

