from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@pytest.mark.integration
def test_damagefly_wall_tech_entry_qgd_replay_real_lock() -> None:
    # Replay-real lock for the kept DamageFlyN -> PassiveWallJump wall-tech subset:
    # - DamageFly_Coll tries ftCo_800C1D38 before floor-tech / DownBound callbacks.
    # - ftCo_800C1D38 upgrades to PassiveWallJump when ftCo_800C1E0C is true.
    # - ftCo_800C1E64 clears hitstun ownership and applies ftColl_8007B760(..., x764) on entry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1D38,ftCo_800C1E0C,ftCo_800C1E64}
    # data/common/ft_common_data.json: colanim_passivewall_x1990_frames
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = 0
    negative_control = 8406
    target_minus_1 = 8403
    target = 8404
    target_plus_1 = 8405
    for record in (negative_control, target_minus_1, target, target_plus_1):
        assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    seed = samples[target]["seed_t"]
    ref = samples[target]["ref_t1"]
    assert int(seed["action_id"][p]) == 88  # DamageFlyN
    assert int(seed["action_frame"][p]) == 12
    assert int(seed["hurtbox_state"][p]) == 0
    assert int(seed["hitstun"][p]) == 21
    assert int(seed["x680"][p]) == 18
    assert int(seed["x684"][p]) == 119
    assert int(seed["x67E"][p]) == 76
    assert int(ref["action_id"][p]) == 203  # PassiveWallJump
    assert int(ref["action_frame"][p]) == 0
    assert int(ref["animation_index"][p]) == 203
    assert int(ref["hurtbox_state"][p]) == 2
    assert int(ref["hitstun"][p]) == 0

    locked_fields = (
        "action_id",
        "action_frame",
        "animation_index",
        "hurtbox_state",
        "hitstun",
        "instance_id",
        "on_ground",
        "facing",
        "jumps_left",
        "state_flags",
    )
    for record in (negative_control, target_minus_1, target, target_plus_1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
        for field in locked_fields:
            got = out_row[field][p]
            want = ref_row[field][p]
            if field == "state_flags":
                assert list(map(int, got)) == list(map(int, want)), (
                    f"record={record} p={p} field={field} expected={list(map(int, want))} "
                    f"got={list(map(int, got))}"
                )
            else:
                assert int(got) == int(want), (
                    f"record={record} p={p} field={field} expected={int(want)} got={int(got)}"
                )
