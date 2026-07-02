from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tools.eval.discrete_compare_lanes import (
    compile_discrete_compare_lanes,
    first_mismatch_field,
    first_mismatch_values,
)
from tools.eval.validation_profile import get_validation_profile


def test_rl1_gameplay_ignores_only_state_flags_4_camera_bit_but_strict_scores_it() -> None:
    seed = np.zeros(1, dtype=COMPARE_DTYPE)
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    ref["state_flags"][0, 1, 4] = 0x80

    strict_lanes = compile_discrete_compare_lanes(
        ("action_id", "state_flags"), (0, 1), profile=get_validation_profile("strict")
    )
    strict_mm = first_mismatch_values(
        seed_row=seed[0], out_row=out[0], ref_row=ref[0], lanes=strict_lanes
    )
    assert strict_mm is not None
    assert strict_mm.field == "state_flags"
    assert strict_mm.player == 1
    assert strict_mm.subindex == 4

    rl1_lanes = compile_discrete_compare_lanes(
        ("action_id", "state_flags"), (0, 1), profile=get_validation_profile("rl1_gameplay")
    )
    assert first_mismatch_values(seed_row=seed[0], out_row=out[0], ref_row=ref[0], lanes=rl1_lanes) is None

    ignored_lanes = compile_discrete_compare_lanes(
        ("action_id", "state_flags"),
        (0, 1),
        profile=get_validation_profile("rl1_gameplay"),
        ignored_only=True,
    )
    assert first_mismatch_field(out_row=out[0], ref_row=ref[0], lanes=ignored_lanes) == "state_flags"
    assert (
        first_mismatch_field(out_row=out[0], ref_row=ref[0], lanes=ignored_lanes, label_subindex=True)
        == "state_flags[4]&0x80"
    )


def test_rl1_gameplay_scores_other_state_flags_4_bits() -> None:
    seed = np.zeros(1, dtype=COMPARE_DTYPE)
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    ref["state_flags"][0, 1, 4] = 0x08

    rl1_lanes = compile_discrete_compare_lanes(
        ("action_id", "state_flags"), (0, 1), profile=get_validation_profile("rl1_gameplay")
    )
    mm = first_mismatch_values(seed_row=seed[0], out_row=out[0], ref_row=ref[0], lanes=rl1_lanes)
    assert mm is not None
    assert mm.field == "state_flags"
    assert mm.player == 1
    assert mm.subindex == 4

    ignored_lanes = compile_discrete_compare_lanes(
        ("action_id", "state_flags"),
        (0, 1),
        profile=get_validation_profile("rl1_gameplay"),
        ignored_only=True,
    )
    assert first_mismatch_field(out_row=out[0], ref_row=ref[0], lanes=ignored_lanes) is None


def test_rl1_gameplay_scores_non_camera_bits_even_when_camera_bit_differs() -> None:
    seed = np.zeros(1, dtype=COMPARE_DTYPE)
    out = np.zeros(1, dtype=COMPARE_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    out["state_flags"][0, 1, 4] = 0x80
    ref["state_flags"][0, 1, 4] = 0x81

    rl1_lanes = compile_discrete_compare_lanes(
        ("action_id", "state_flags"), (0, 1), profile=get_validation_profile("rl1_gameplay")
    )
    mm = first_mismatch_values(seed_row=seed[0], out_row=out[0], ref_row=ref[0], lanes=rl1_lanes)
    assert mm is not None
    assert mm.field == "state_flags"
    assert mm.player == 1
    assert mm.subindex == 4
    assert mm.out == 0
    assert mm.ref == 1
