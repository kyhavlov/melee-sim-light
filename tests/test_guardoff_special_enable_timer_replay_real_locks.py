from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tests.replay_buffers_loader import load_replay_buffers


ACT_GUARD = 0x00B3
ACT_GUARD_OFF = 0x00B4
ACT_FX_SPECIAL_LW_START = 0x0168


@pytest.mark.integration
def test_guardoff_without_x1c_does_not_enter_shine_iat_replay_lock() -> None:
    # Regression target: IAT 7302 p0 is GuardOff with B/down input. Vanilla stays in GuardOff; the
    # sim used to enter Fox SpecialLwStart because GuardOff was treated as an unconditional
    # grounded-special owner. Decomp gates GuardOff's special/attack chain on mv.co.guard.x1C,
    # which is only armed by ftCo_80094138 after powershield-active shield contact.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOff_IASA,ftCo_80094138}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    record = 7302
    p = 0
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record]
    seed = row["seed_t"]
    assert int(seed["action_id"][p]) == ACT_GUARD_OFF
    assert int(seed["guard_special_enable_timer_x1c"][p]) == 0
    assert (int(row["prev_input_t"]["p"][p]["buttons"]) & 0x0200) == 0
    assert (int(row["input_t"]["p"][p]["buttons"]) & 0x0200) != 0
    assert int(row["input_t"]["p"][p]["main_y"]) < 0

    _seed_t, ref_t1, out_t1 = _run_one_step_row(dataset_path, record, p)
    assert int(ref_t1["action_id"][p]) == ACT_GUARD_OFF
    assert int(out_t1["action_id"][p]) == ACT_GUARD_OFF
    assert int(out_t1["action_id"][p]) != ACT_FX_SPECIAL_LW_START
    assert int(ref_t1["action_id"][1]) == ACT_GUARD
    assert int(out_t1["action_id"][1]) == ACT_GUARD
    assert int(out_t1["hitlag"][p]) == 0
    assert int(out_t1["hitlag"][1]) == 0
