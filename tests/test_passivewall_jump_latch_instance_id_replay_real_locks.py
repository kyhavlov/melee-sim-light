from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _PassiveWallLatchCase:
    dataset_rel: str
    record: int
    port: int
    expect_instance_bump: bool
    note: str


def _run_one_step_with_main_y(dataset_path: Path, record: int, port: int, main_y: int) -> tuple:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    row = load_replay_buffers(str(dataset_path)).rows[record : record + 1].copy()
    row["input_t"]["p"][0, port]["main_y"] = np.int8(main_y)
    row["prev_input_t"]["p"][0, port]["main_y"] = np.int8(0)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride)
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes.copy())
        binding.step_input(handle, prev_input_bytes.copy(), input_bytes.copy())
        binding.write_compare(handle, out_bytes)
        return row["seed_t"][0], out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _PassiveWallLatchCase(
            dataset_rel="replays/validation/yoshis_story_recent/CheeryNumbMonkey.slpz",
            record=5282,
            port=0,
            expect_instance_bump=True,
            note="CNM timer-expiry PassiveWallJump x8 latch re-enters same action",
        ),
        _PassiveWallLatchCase(
            dataset_rel="replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.slpz",
            record=7520,
            port=1,
            expect_instance_bump=False,
            note="IAT timer-expiry PassiveWallJump without x8 latch only unfreezes animation",
        ),
    ],
)
def test_passivewall_timer_expiry_jump_latch_instance_id_boundary(
    case: _PassiveWallLatchCase,
) -> None:
    # Replay-real lock for PassiveWall_Anim timer expiry:
    # - while mv.co.passivewall.timer is nonzero, PassiveWall_IASA can set mv.co.passivewall.x8;
    # - on the timer-expiry Anim callback, x8 calls inlineA0, a same-action
    #   Fighter_ChangeMotionState(PassiveWallJump, cur_anim_frame, 1) that updates instance_id;
    # - without x8, the same expiry only calls ftAnim_SetAnimRate and launches without identity
    #   consumption.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{
    #   ftCo_800C1E0C,ftCo_PassiveWall_IASA,inlineA0,ftCo_PassiveWall_Anim}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, case.record, case.port)
    p = case.port
    assert int(seed["action_id"][p]) == 203, case.note
    assert int(seed["passivewall_timer"][p]) == 1, case.note
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 203, case.note
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p]) == 0, case.note
    assert int(out["instance_id"][p]) == int(ref["instance_id"][p]), case.note
    assert (int(ref["instance_id"][p]) != int(seed["instance_id"][p])) == case.expect_instance_bump
    assert float(out["speed_air_x_self"][p]) == pytest.approx(float(ref["speed_air_x_self"][p]), abs=1e-4)
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-4)


@pytest.mark.integration
def test_passivewall_timer_expiry_current_tap_jump_latches_x8() -> None:
    # Source branch coverage for ftCo_800C1E0C: the x8 startup latch can be set by the current
    # tap-jump stick even when replay-prefix x67E reconstruction did not already latch it.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{
    #   ftCo_800C1E0C,ftCo_PassiveWall_IASA,inlineA0,ftCo_PassiveWall_Anim}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path.relative_to(root)}")

    record = 7520
    port = 1
    seed, ref, out = _run_one_step_row(dataset_path, record, port)
    assert int(seed["action_id"][port]) == 203
    assert int(seed["passivewall_timer"][port]) == 1
    assert int(ref["instance_id"][port]) == int(seed["instance_id"][port])
    assert int(out["instance_id"][port]) == int(seed["instance_id"][port])

    seed2, out2 = _run_one_step_with_main_y(dataset_path, record, port, main_y=80)
    assert int(seed2["instance_id"][port]) == int(seed["instance_id"][port])
    assert int(out2["action_id"][port]) == 203
    assert int(out2["action_frame"][port]) == 0
    assert int(out2["instance_id"][port]) != int(seed2["instance_id"][port])
