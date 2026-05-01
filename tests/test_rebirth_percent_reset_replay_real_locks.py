from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _RebirthPercentCase:
    dataset_rel: str
    target_record: int
    p: int
    seed_action: int
    note: str


def _run_rollout_window(dataset_path: Path, start: int, stop: int):
    import msl_binding

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    def field_bytes(record: int, off: int, stride: int) -> np.ndarray:
        raw = samples[record : record + 1].view(np.uint8).reshape(1, -1)
        return np.array(raw[:, off : off + stride], dtype=np.uint8, order="C", copy=True)

    handle = msl_binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8, order="C")
    try:
        msl_binding.reseed_seed_rollout(handle, field_bytes(start, seed_off, seed_stride))
        for record in range(start, stop + 1):
            msl_binding.step_input(
                handle,
                field_bytes(record, prev_off, input_stride),
                field_bytes(record, input_off, input_stride),
            )
            msl_binding.write_compare(handle, out_bytes)
    finally:
        msl_binding.destroy(handle)

    return samples["ref_t1"][stop], out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _RebirthPercentCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=6774,
            p=0,
            seed_action=0,
            note="DeadDown -> Rebirth must consume the decomp death-reset percent source on the transition frame",
        ),
        _RebirthPercentCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=6929,
            p=0,
            seed_action=2,
            note="DeadRight -> Rebirth resets percent on the same frame Fighter_UnkProcessDeath runs",
        ),
        _RebirthPercentCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=4482,
            p=0,
            seed_action=4,
            note="DeadUpStar -> Rebirth transition clears percent while preserving the existing Rebirth camera lane",
        ),
        _RebirthPercentCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=4021,
            p=1,
            seed_action=1,
            note="DeadLeft -> Rebirth transition resets damage before the steady Rebirth row",
        ),
    ],
)
def test_rebirth_percent_reset_target_pm1_replay_real(case: _RebirthPercentCase) -> None:
    # Decomp ownership:
    # - Fighter_UnkProcessDeath_80068354 runs before Rebirth entry.
    # - Fighter_UnkInitReset_80067C98 reloads dmg.x1830_percent and clears dmg.x1838_percentTemp.
    # refs/melee/src/melee/ft/fighter.c::{
    #   Fighter_UnkProcessDeath_80068354,Fighter_UnkInitReset_80067C98
    # }
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = case.p

    seed_target = samples[case.target_record]["seed_t"]
    ref_target = samples[case.target_record]["ref_t1"]
    assert int(seed_target["action_id"][p]) == int(case.seed_action), case.note
    assert int(ref_target["action_id"][p]) == 12, case.note
    assert float(seed_target["percent"][p]) > 0.0, case.note
    assert float(ref_target["percent"][p]) == pytest.approx(0.0), case.note

    for rec in (case.target_record - 1, case.target_record, case.target_record + 1):
      _, ref_row, out_row = _run_one_step_row(dataset_path, rec, p)
      ref_percent = float(ref_row["percent"][p])
      out_percent = float(out_row["percent"][p])
      assert np.float32(out_percent).view(np.uint32) == np.float32(ref_percent).view(np.uint32), (
          f"{case.note}: record={rec} expected percent bits="
          f"0x{int(np.float32(ref_percent).view(np.uint32)):08x} got=0x{int(np.float32(out_percent).view(np.uint32)):08x}"
      )

    # Explicit negative control: the pre-transition Dead* row must *not* zero early.
    _, ref_prev, out_prev = _run_one_step_row(dataset_path, case.target_record - 1, p)
    assert float(ref_prev["percent"][p]) > 0.0, case.note
    assert float(out_prev["percent"][p]) == pytest.approx(float(ref_prev["percent"][p])), case.note


@pytest.mark.integration
def test_rebirth_resets_shield_health_and_damage_lanes_rollout() -> None:
    # `Fighter_UnkProcessDeath_80068354 -> Fighter_UnkInitReset_80067C98` resets percent and
    # shield state before Rebirth entry. Rollout from DeadDown must not carry the old depleted
    # shield HP to the first Rebirth frame.
    # refs/melee/src/melee/ft/fighter.c::{
    #   Fighter_UnkProcessDeath_80068354,Fighter_UnkInitReset_80067C98
    # }
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1
    ds = read_dataset(str(dataset_path))
    seed = ds.samples["seed_t"][1889]
    ref = ds.samples["ref_t1"][1889]
    assert int(seed["action_id"][p]) == 0  # DeadDown.
    assert int(ref["action_id"][p]) == 12  # Rebirth.
    assert float(seed["shield_hp"][p]) < 60.0
    assert float(ref["shield_hp"][p]) == pytest.approx(60.0)
    _, ref_row, out_row = _run_one_step_row(dataset_path, 1889, p)
    assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]) == 12
    assert float(out_row["percent"][p]) == pytest.approx(0.0)
    assert float(out_row["shield_hp"][p]) == pytest.approx(float(ref_row["shield_hp"][p]))

    ref_roll, out_roll = _run_rollout_window(dataset_path, 1830, 1889)
    assert int(out_roll["action_id"][p]) == int(ref_roll["action_id"][p]) == 12
    assert float(out_roll["percent"][p]) == pytest.approx(0.0)
    assert float(out_roll["shield_hp"][p]) == pytest.approx(float(ref_roll["shield_hp"][p]))
