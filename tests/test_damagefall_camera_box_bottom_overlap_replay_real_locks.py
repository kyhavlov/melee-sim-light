from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@pytest.mark.integration
def test_damagefall_camera_box_bottom_overlap_replay_real_locks() -> None:
    # Replay-real lock for the kept Falco DamageFall bottom-overlap visibility lane:
    # - ftLib_80086A8C sets fp->x221F_b0 when the subject point is off-screen and the
    #   camera subject still overlaps the screen through Camera_80030CFC(subject, 15).
    # - Keep the lock on a Falco DamageFall sequence where the seeded camera target crosses
    #   the bottom bound between target-1 and target, while Fox controls remain clear.
    # refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
    # refs/melee/src/melee/cm/camera.c::Camera_80030CFC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c
    # data/characters/falco.json: terminal_vel
    # data/stages/final_destination.json: cam_bounds_world
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = 0

    negative_control = 2941
    target_minus_1 = 2942
    target = 2943
    target_plus_1 = 2944
    for record in (negative_control, target_minus_1, target, target_plus_1):
        assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    target_seed = samples[target]["seed_t"]
    assert int(target_seed["char_id"][p]) == 22  # Falco
    assert int(target_seed["action_id"][p]) == 38  # DamageFall
    assert int(target_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 0
    assert float(target_seed["camera_target_world_y_f32"][p]) < -80.0
    assert float(target_seed["camera_box_radius_f32"][p]) == pytest.approx(11.2, abs=1e-4)

    minus_seed = samples[target_minus_1]["seed_t"]
    plus_seed = samples[target_plus_1]["seed_t"]
    negative_seed = samples[negative_control]["seed_t"]
    assert float(minus_seed["camera_target_world_y_f32"][p]) >= -80.0
    assert float(plus_seed["camera_target_world_y_f32"][p]) < -80.0
    assert float(negative_seed["camera_target_world_y_f32"][p]) >= -80.0

    _, ref_target, out_target = _run_one_step_row(dataset_path, target, p)
    assert int(out_target["state_flags"][p, 4]) == 128
    assert int(ref_target["state_flags"][p, 4]) == 128

    for record, expected_out, expected_ref in (
        (target_minus_1, 0, 0),
        (target_plus_1, 128, 128),
        (negative_control, 0, 0),
    ):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]), record
        assert int(out_row["action_frame"][p]) == int(ref_row["action_frame"][p]), record
        assert int(out_row["state_flags"][p, 4]) == expected_out, record
        assert int(ref_row["state_flags"][p, 4]) == expected_ref, record

    fox_control_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    fox_control_path = root / fox_control_rel
    if not fox_control_path.exists():
        pytest.skip(f"missing local dataset: {fox_control_rel}")

    for record, expected_out, expected_ref in (
        (4643, 0, 0),
        (4644, 0, 128),
        (4645, 128, 128),
    ):
        _, ref_row, out_row = _run_one_step_row(fox_control_path, record, 0)
        assert int(out_row["state_flags"][0, 4]) == expected_out, record
        assert int(ref_row["state_flags"][0, 4]) == expected_ref, record
