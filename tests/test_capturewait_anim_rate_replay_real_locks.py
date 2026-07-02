from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _run_rollout_window(dataset_path: Path, start_record: int, end_record: int) -> dict[int, np.ndarray]:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > end_record, "replay too short for rollout lock window"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    outs: dict[int, np.ndarray] = {}
    try:
        binding.reseed_seed(handle, seed_bytes)
        for rec in range(start_record, end_record + 1):
            row = samples[rec : rec + 1]
            prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            outs[rec] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)
    return outs


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "expected_action_frame"),
    [
        (
            "replays/validation/aggregate_recent/BlondHardHippopotamus.slpz",
            922,
            0,
            3,
        ),
        (
            "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            5677,
            0,
            3,
        ),
    ],
)
def test_capturewait_post_loop_rate_publication_reseed_matches_ref(
    dataset_rel: str, record: int, p: int, expected_action_frame: int
) -> None:
    # Replay-real lock for a teacher-forced CaptureWait reseed publication row:
    # x2344 has ticked from x3B0 to x3B0-x3B4 after ftAnim_SetAnimRate(x3B4), but Slippi can still
    # serialize frame_speed_mul as the stale rate-1 value on the post-loop action_frame==1 row.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureWaitHi_Anim
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[record]["seed_t"]
    ref = ds.rows[record]["ref_t1"]
    assert int(seed["action_id"][p]) in (224, 227)
    assert int(seed["action_frame"][p]) == 1
    assert int(seed["seed_prev_action_id"][p]) == int(seed["action_id"][p])
    assert int(seed["seed_prev_action_frame"][p]) == 0
    # Regenerated validation artifacts carry the wait-armed x2344 hold at the full source value on
    # the first post-loop row. `api.c` treats any positive in-window x2344 as AObj-rate proof; the
    # transition lock below remains the source-owned assertion.
    # bindings/msl_preprocess_native.c::msl_py_derive_capture_wait_lanes_from_series
    assert float(seed["capture_wait_anim_rate_timer_f32"][p]) == pytest.approx(10.0)
    assert int(ref["action_frame"][p]) == expected_action_frame

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)


@pytest.mark.integration
def test_capturewait_post_loop_rate_publication_does_not_override_throw_handoff() -> None:
    # Negative lock: the same x2344==x3B0-x3B4 / action_frame==1 shape can be followed by the
    # source-owned CatchWait throw handoff. The rate bridge must not disturb that destination owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CaptureWaitHi_Anim,ftCo_CatchWait_IASA}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "replays/validation/pokemon_stadium_recent/SweatyThisMallard.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    record = 1438
    p = 0
    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[record]["seed_t"]
    ref = ds.rows[record]["ref_t1"]
    assert int(seed["action_id"][p]) == 227
    assert int(seed["action_frame"][p]) == 1
    # Cross-family throw handoff rows intentionally do not seed the x2344 AObj-rate bridge in the
    # regenerated lane. The destination owner below remains the lock's subject.
    # bindings/msl_preprocess_native.c::msl_py_derive_capture_wait_lanes_from_series
    assert float(seed["capture_wait_anim_rate_timer_f32"][p]) == pytest.approx(0.0)
    assert int(ref["action_id"][p]) == 241

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=record, p=p)


@pytest.mark.integration
def test_capturewait_anim_rate_runtime_rollout_matches_qgd_window() -> None:
    # Replay-real lock for the CaptureWaitLw mash-rate runtime owner:
    # - ftCo_CaptureWaitHi/Lw_Anim writes ftAnim_SetAnimRate(x3B4) after the anim advance when
    #   ftCommon_GrabMash succeeds and the internal x2344 hold timer is idle, then keeps the boost
    #   alive for x3B0 frames before restoring rate 1.
    # - The sim already had a reseed continuity bridge for one-step; this lock covers the missing
    #   live rollout owner on the QGD low-throw chain before the later ThrownLw handoff rows.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureWaitHi_Anim
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_GrabMash
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows

    target_rows = (415, 416, 417, 418, 419, 420)
    for rec in target_rows:
        assert int(samples.shape[0]) > rec, f"replay too short for record={rec}"

    seed = samples[415]["seed_t"]
    assert int(seed["action_id"][0]) == 216  # CatchWait
    assert int(seed["action_id"][1]) == 227  # CaptureWaitLw
    assert int(seed["grab_owner_port"][1]) == 0
    assert float(seed["frame_speed_mul_f32"][1]) == pytest.approx(1.0)

    # One-step rows around the first live rate jump should stay replay-exact.
    for rec in (415, 416, 417):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, 0)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=p)

    # Unreseeded rollout from the earlier owner frame must keep the CaptureWaitLw timebase exact.
    outs = _run_rollout_window(dataset_path, 355, 420)
    for rec in target_rows:
        ref_row = samples[rec]["ref_t1"]
        out_row = outs[rec]
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=p)
