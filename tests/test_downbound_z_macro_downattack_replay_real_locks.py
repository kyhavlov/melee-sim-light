from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _run_rollout_window(
    dataset_path: Path,
    *,
    start_record: int,
    stop_record: int,
    clear_z_window: tuple[int, int] | None = None,
) -> tuple[object, dict[int, np.void]]:
    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > stop_record

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed_bytes = samples[start_record : start_record + 1]["seed_t"].view("u1").reshape(1, seed_stride).copy()
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    outs: dict[int, np.void] = {}

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for rec in range(start_record, stop_record + 1):
            row = samples[rec : rec + 1]
            prev_input = row["prev_input_t"].copy()
            cur_input = row["input_t"].copy()
            if clear_z_window is not None and clear_z_window[0] <= rec <= clear_z_window[1]:
                prev_input["p"]["buttons"][0, 0] &= np.uint16(~0x0010 & 0xFFFF)
                cur_input["p"]["buttons"][0, 0] &= np.uint16(~0x0010 & 0xFFFF)
            prev_bytes = prev_input.view("u1").reshape(1, input_stride).copy()
            cur_bytes = cur_input.view("u1").reshape(1, input_stride).copy()
            binding.step_input(handle, prev_bytes, cur_bytes)
            binding.write_compare(handle, out_bytes)
            outs[rec] = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)

    return ds, outs


@pytest.mark.integration
def test_downbound_z_macro_a_timer_enters_getup_attack_his_rollout_lock() -> None:
    # Replay-real lock for the Z macro input owner used by DownBound getup attack:
    # - Fighter_Spaghetti_8006AD10 maps held Z onto effective HSD_PAD_A plus HSD_PAD_LR before
    #   building input.x668 and updating x67C/x680.
    # - ftCo_DownBound_Anim later calls ftCo_80098400, whose inlineB0 admits DownAttack when x67C
    #   or x67D is within p_ftCommonData->x24C.
    # - HIS presses Z during DownBound; the visible raw A button is never set, so this lock proves
    #   the source-owned Z->A input macro rather than a broad DownBound timer shortcut.
    #
    # refs/melee/src/melee/ft/fighter.c::{
    #   Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_80098400
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "replays/validation/aggregate_recent/HungryImportantSnake.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds, outs = _run_rollout_window(dataset_path, start_record=5121, stop_record=5146)
    samples = ds.rows
    p = 0
    target = 5146
    seed = samples[target]["seed_t"]
    ref = samples[target]["ref_t1"]

    assert int(seed["action_id"][p]) == 183  # DownBoundU
    assert int(seed["action_frame"][p]) == 25
    assert int(seed["x67C"][p]) < 60
    assert (int(samples[5129]["input_t"]["p"]["buttons"][p]) & 0x0010) != 0  # Z edge source
    assert (int(samples[5129]["input_t"]["p"]["buttons"][p]) & 0x0100) == 0  # no raw A edge
    assert int(ref["action_id"][p]) == 187  # DownAttackU

    out = outs[target]
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["hurtbox_state"][p]) == int(ref["hurtbox_state"][p])
    assert list(map(int, out["state_flags"][p])) == list(map(int, ref["state_flags"][p]))


@pytest.mark.integration
def test_downbound_without_z_macro_does_not_enter_getup_attack_his_negative() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "replays/validation/aggregate_recent/HungryImportantSnake.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    _, outs = _run_rollout_window(
        dataset_path,
        start_record=5121,
        stop_record=5146,
        clear_z_window=(5121, 5136),
    )

    # Removing only Z from the DownBound input episode leaves Y and stick history intact but no
    # source-owned A macro edge, so the same anim-end row must not enter DownAttack.
    assert int(outs[5146]["action_id"][0]) != 187
