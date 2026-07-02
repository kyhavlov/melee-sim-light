from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_items_spawn_joint_replay_real_locks import _skip_if_required_artifacts_missing
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views
from tools.eval.streaming_validation import _load_binding


_PJO = Path("replays/validation/aggregate_recent/PutridJoyousOryx.slpz")


def _byte_views(ds):
    views = replay_buffer_byte_views(ds)
    return views.seed_t, views.prev_input_t, views.input_t


def _step_one(dataset_path: Path, record: int) -> tuple[np.void, np.void]:
    binding = _load_binding()
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    seed_u8, prev_input_u8, input_u8 = _byte_views(ds)
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes[0, :] = seed_u8[record, :seed_stride]
        prev_input_bytes[0, :] = prev_input_u8[record, :input_stride]
        input_bytes[0, :] = input_u8[record, :input_stride]
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0], samples["ref_t1"][record]


def _rollout_to(dataset_path: Path, start_record: int, end_record: int) -> tuple[np.void, np.void]:
    binding = _load_binding()
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    seed_u8, prev_input_u8, input_u8 = _byte_views(ds)
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes[0, :] = seed_u8[start_record, :seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, end_record + 1):
            prev_input_bytes[0, :] = prev_input_u8[record, :input_stride]
            input_bytes[0, :] = input_u8[record, :input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0], samples["ref_t1"][end_record]


def _precombat_body_selection_count(dataset_path: Path, record: int) -> int:
    binding = _load_binding()
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    seed_u8, prev_input_u8, input_u8 = _byte_views(ds)
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes[0, :] = seed_u8[record, :seed_stride]
        prev_input_bytes[0, :] = prev_input_u8[record, :input_stride]
        input_bytes[0, :] = input_u8[record, :input_stride]
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        binding.debug_refresh_combat_geometry(handle)
        _rows, count = binding.debug_combat_select_body_hits(handle, 0, 64)
    finally:
        binding.destroy(handle)
    return int(count)


@pytest.mark.integration
def test_guardreflect_no_submotion_grabbable_capsules_allow_catch_pjo_4240() -> None:
    # GuardReflect no-submotion grabbable Catch boundary:
    # - Slippi can serialize GuardReflect as animation_index=-1/-2 while decomp still has the
    #   motion-state GuardOn submotion table for grabbable hurt capsules.
    # - ftColl_80078A2C uses `hurt_capsules[j].is_grabbable` for Catch/CatchDash selection, not the
    #   BODY hurtbox-mode mask, so this Catch frame must still enter CatchPull/CapturePulledLw.
    # refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_GuardReflect
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _PJO
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    out, ref = _step_one(dataset_path, 4240)
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == 213  # CatchPull
    assert int(out["action_id"][1]) == int(ref["action_id"][1]) == 226  # CapturePulledLw
    assert int(out["instance_id"][0]) == int(ref["instance_id"][0])
    assert int(out["instance_id"][1]) == int(ref["instance_id"][1])
    assert int(out["instance_hit_by"][1]) == int(ref["instance_hit_by"][1])


@pytest.mark.integration
def test_guardreflect_no_submotion_grabbable_capsules_do_not_enable_body_pjo_4240() -> None:
    # Same source slice as the positive Catch lock, but BODY remains disabled. This proves the
    # fallback is not a broad "late GuardReflect has normal hurtcaps" bridge.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _PJO
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    assert _precombat_body_selection_count(dataset_path, 4240) == 0


@pytest.mark.integration
def test_guardreflect_no_submotion_catch_rollout_bridge_pjo_4221_to_4240() -> None:
    # The top disruptive F09c PJO cluster seeds from p1 PassiveStandB and rolls into this
    # GuardReflect/Catch frame. The retained owner removes the first Catch-vs-CatchPull break.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _PJO
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    out, ref = _rollout_to(dataset_path, 4221, 4240)
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == 213
    assert int(out["action_id"][1]) == int(ref["action_id"][1]) == 226
    assert int(out["instance_id"][0]) == int(ref["instance_id"][0])
    assert int(out["instance_id"][1]) == int(ref["instance_id"][1])
