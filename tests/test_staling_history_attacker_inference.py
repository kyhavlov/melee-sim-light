from __future__ import annotations

import numpy as np

from tools.slippi import staling_history


def test_infer_attacker_slot_from_last_hit_by_instance_unique_match() -> None:
    state_iid_row = np.array([76, 77], dtype=np.uint16)
    got = staling_history._infer_attacker_slot_from_last_hit_by_instance(
        state_iid_row=state_iid_row,
        last_hit_by_instance=76,
    )
    assert got == 0


def test_infer_attacker_slot_from_last_hit_by_instance_ambiguous_match() -> None:
    state_iid_row = np.array([76, 76], dtype=np.uint16)
    got = staling_history._infer_attacker_slot_from_last_hit_by_instance(
        state_iid_row=state_iid_row,
        last_hit_by_instance=76,
    )
    assert got == -1


def test_infer_attacker_slot_from_last_hit_by_instance_zero_or_missing() -> None:
    state_iid_row = np.array([76, 77], dtype=np.uint16)
    got = staling_history._infer_attacker_slot_from_last_hit_by_instance(
        state_iid_row=state_iid_row,
        last_hit_by_instance=0,
    )
    assert got == -1


def test_infer_attacker_slot_from_last_hit_by_instance_no_match() -> None:
    state_iid_row = np.array([76, 77], dtype=np.uint16)
    got = staling_history._infer_attacker_slot_from_last_hit_by_instance(
        state_iid_row=state_iid_row,
        last_hit_by_instance=999,
    )
    assert got == -1

