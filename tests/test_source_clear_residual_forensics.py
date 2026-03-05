from __future__ import annotations

import numpy as np

from tools.eval.source_clear_residual_forensics import ResidualSignature, _signature


def test_signature_extracts_source_clear_causal_lanes() -> None:
    seed_dtype = np.dtype(
        [
            ("char_id", "u1", (4,)),
            ("action_id", "u2", (4,)),
            ("action_frame", "i2", (4,)),
            ("on_ground", "u1", (4,)),
            ("source_clear_timer_x18c8", "u1", (4,)),
            ("source_clear_owner_set_phase", "u1", (4,)),
            ("source_clear_owner_transition_epoch", "u1", (4,)),
            ("source_clear_terminal_phase", "u1", (4,)),
            ("source_clear_grounded_damage_clear_phase", "u1", (4,)),
            ("hitlag", "u2", (4,)),
            ("hitstun", "u2", (4,)),
            ("combo_count", "u1", (4,)),
            ("last_attack_landed", "u1", (4,)),
            ("state_flags", "u1", (4, 5)),
            ("last_hit_by", "u1", (4,)),
        ]
    )
    row = np.zeros((), dtype=seed_dtype)
    p = 1
    row["char_id"][p] = np.uint8(22)
    row["action_id"][p] = np.uint16(0x0018)
    row["action_frame"][p] = np.int16(3)
    row["on_ground"][p] = np.uint8(1)
    row["source_clear_timer_x18c8"][p] = np.uint8(9)
    row["source_clear_owner_set_phase"][p] = np.uint8(1)
    row["source_clear_owner_transition_epoch"][p] = np.uint8(2)
    row["source_clear_terminal_phase"][p] = np.uint8(0)
    row["source_clear_grounded_damage_clear_phase"][p] = np.uint8(0)
    row["hitlag"][p] = np.uint16(0)
    row["hitstun"][p] = np.uint16(0)
    row["combo_count"][p] = np.uint8(1)
    row["last_attack_landed"][p] = np.uint8(13)
    row["state_flags"][p, 4] = np.uint8(0x10)
    row["last_hit_by"][p] = np.uint8(1)

    got = _signature(row, p)
    assert got == ResidualSignature(
        char_id=22,
        action_id=0x0018,
        action_frame=3,
        on_ground=1,
        timer_x18c8=9,
        owner_set_phase=1,
        owner_transition_epoch=2,
        terminal_phase=0,
        grounded_clear_phase=0,
        hitlag=0,
        hitstun=0,
        combo_count=1,
        last_attack_landed=13,
        state_flags_221f=0x10,
        seed_last_hit_by=1,
    )


def test_signature_defaults_missing_optional_source_clear_fields_to_zero() -> None:
    seed_dtype = np.dtype(
        [
            ("char_id", "u1", (4,)),
            ("action_id", "u2", (4,)),
            ("action_frame", "i2", (4,)),
            ("on_ground", "u1", (4,)),
            ("source_clear_timer_x18c8", "u1", (4,)),
            ("source_clear_owner_set_phase", "u1", (4,)),
            ("hitlag", "u2", (4,)),
            ("hitstun", "u2", (4,)),
            ("combo_count", "u1", (4,)),
            ("last_attack_landed", "u1", (4,)),
            ("state_flags", "u1", (4, 5)),
            ("last_hit_by", "u1", (4,)),
        ]
    )
    row = np.zeros((), dtype=seed_dtype)
    p = 0
    got = _signature(row, p)
    assert got.owner_transition_epoch == 0
    assert got.terminal_phase == 0
    assert got.grounded_clear_phase == 0
