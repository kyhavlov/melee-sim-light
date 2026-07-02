from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views


ACT_GUARD_ON = 0x00B2
ACT_GUARD = 0x00B3
ACT_GUARD_OFF = 0x00B4
ACT_GUARD_SET_OFF = 0x00B5
ACT_GUARD_REFLECT = 0x00B6

_BASE = (
    "replays/validation/"
    "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
)
_PJO = "replays/validation/aggregate_recent/PutridJoyousOryx.slpz"


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local extracted artifacts: {', '.join(missing)}")


def _step_one_record(binding, samples: np.ndarray, record: int, num_players: int) -> np.void:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    row = samples[record : record + 1]
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
    prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    )
    input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

    handle = binding.init(batch_size=1, num_players=int(num_players))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)


def _rollout_to_record(
    binding,
    ds,
    *,
    start_record: int,
    target_record: int,
    num_players: int,
) -> np.void:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    samples = ds.rows
    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(num_players))
    try:
        seed_bytes[0, :] = seed_u8[start_record, :seed_stride]
        binding.reseed_seed(handle, seed_bytes)
        for record in range(int(start_record), int(target_record) + 1):
            prev_input_bytes[0, :] = prev_input_u8[record, :input_stride]
            input_bytes[0, :] = input_u8[record, :input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _RolloutCase:
    start_record: int
    target_record: int
    player: int
    seed_action: int
    ref_action: int
    note: str


@pytest.mark.integration
def test_guardon_one_step_boundary_stays_on_predecrement_x10() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _BASE
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {_BASE}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    num_players = int(ds.num_players)
    p = 1

    # Decomp owner model:
    # - GuardOff release gates use the pre-decrement x10 value in inlineC0.
    # - GuardOn completion uses the next no-submotion remaining-frame snapshot, so one-step rows
    #   with seed x10=1 must not advance early, while seed x10=0 does advance.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800925A4,ftCo_GuardOn_Anim,inlineC0}
    assert int(samples[333]["seed_t"]["action_id"][p]) == ACT_GUARD_ON
    assert int(samples[333]["seed_t"]["guard_x10"][p]) == 1
    assert int(samples[333]["ref_t1"]["action_id"][p]) == ACT_GUARD_ON
    out_333 = _step_one_record(binding, samples, 333, num_players)
    assert int(out_333["action_id"][p]) == ACT_GUARD_ON

    assert int(samples[334]["seed_t"]["action_id"][p]) == ACT_GUARD_ON
    assert int(samples[334]["seed_t"]["guard_x10"][p]) == 0
    assert int(samples[334]["ref_t1"]["action_id"][p]) == ACT_GUARD
    out_334 = _step_one_record(binding, samples, 334, num_players)
    assert int(out_334["action_id"][p]) == ACT_GUARD


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _RolloutCase(
            start_record=1292,
            target_record=1346,
            player=1,
            seed_action=ACT_GUARD_ON,
            ref_action=ACT_GUARD,
            note="GuardOn no-submotion rollout should reach Guard on the x10 completion frame",
        ),
        _RolloutCase(
            start_record=1446,
            target_record=1605,
            player=1,
            seed_action=ACT_GUARD_REFLECT,
            ref_action=ACT_GUARD,
            note="GuardReflect no-submotion rollout should chain through GuardOn_Anim into Guard",
        ),
    ],
)
def test_guard_no_submotion_x10_rollout_owner_windows(case: _RolloutCase) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _BASE
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {_BASE}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > int(case.target_record)

    p = int(case.player)
    assert int(samples[case.target_record]["seed_t"]["action_id"][p]) == int(case.seed_action), case.note
    assert int(samples[case.target_record]["ref_t1"]["action_id"][p]) == int(case.ref_action), case.note

    out = _rollout_to_record(
        binding,
        ds,
        start_record=int(case.start_record),
        target_record=int(case.target_record),
        num_players=int(ds.num_players),
    )
    assert int(out["action_id"][p]) == int(case.ref_action), case.note


@pytest.mark.integration
def test_guardsetoff_to_guard_release_latch_survives_late_trigger_repress_pjo_lock() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _PJO
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {_PJO}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    start_record = 901
    target_record = 926
    p = 0

    # Source owner:
    # - GuardSetOff_Anim enters Guard when GuardDamage completes.
    # - Destination Guard_IASA runs later in the same frame and ftCo_80092BCC latches xC while
    #   shield is released.
    # - Once x10 reaches zero, inlineC0 exits to GuardOff even if shield has been pressed again;
    #   held shield does not clear an already-latched xC.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardSetOff_Anim,ftCo_800928CC,ftCo_Guard_IASA,ftCo_80092BCC,inlineC0}
    assert int(samples[start_record]["seed_t"]["action_id"][p]) == ACT_GUARD_SET_OFF
    assert int(samples[919]["seed_t"]["action_id"][p]) == ACT_GUARD
    assert int(samples[919]["seed_t"]["guard_release_latched_xc"][p]) == 1
    assert int(samples[target_record]["seed_t"]["action_id"][p]) == ACT_GUARD
    assert int(samples[target_record]["seed_t"]["guard_release_latched_xc"][p]) == 1
    assert int(samples[target_record]["seed_t"]["guard_x10"][p]) == 0
    assert int(samples[target_record]["input_t"]["p"][p]["buttons"]) != 0
    assert int(samples[target_record]["ref_t1"]["action_id"][p]) == ACT_GUARD_OFF

    out = _rollout_to_record(
        binding,
        ds,
        start_record=start_record,
        target_record=target_record,
        num_players=int(ds.num_players),
    )
    assert int(out["action_id"][p]) == ACT_GUARD_OFF
    assert abs(float(out["shield_hp"][p]) - float(samples[target_record]["ref_t1"]["shield_hp"][p])) < 0.01
