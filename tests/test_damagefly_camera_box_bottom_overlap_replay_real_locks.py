from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_damagefly_camera_box_bottom_overlap_replay_real_locks() -> None:
    # Replay-real lock for the kept DamageFly* bottom-overlap visibility lane:
    # - ftLib_80086A8C sets fp->x221F_b0 when the subject point is off-screen and the
    #   camera subject still overlaps the screen through Camera_80030CFC(subject, 15).
    # - Keep the lock on a DamageFlyHi sequence where the seeded camera target crosses the
    #   bottom camera bound between target-1 and target, and verify nearby controls do not
    #   spuriously flip.
    # refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
    # refs/melee/src/melee/cm/camera.c::Camera_80030CFC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c
    # data/stages/final_destination.json: cam_bounds_world
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = 1

    negative_control = 7280
    target_minus_1 = 7282
    target = 7283
    target_plus_1 = 7284
    for record in (negative_control, target_minus_1, target, target_plus_1):
        assert int(samples.shape[0]) > record, f"replay too short for lock row: record={record}"

    target_seed = samples[target]["seed_t"]
    assert int(target_seed["action_id"][p]) == 87  # DamageFlyHi
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
