from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row


ACT_ATTACK_LW4 = 0x0040
ACT_ATTACK_HI4 = 0x003F
ACT_SK_SPECIAL_N_START = 341
BUTTON_B = 0x0200
STATE_FLAG_2218_ALLOW_INTERRUPT = 0x80


def _dataset(rel: str) -> Path:
    root = Path(__file__).resolve().parents[1]
    path = root / rel
    if not path.exists():
        pytest.skip(f"missing replay dataset: {path}")
    return path


@pytest.mark.parametrize(
    ("rel", "record", "player"),
    [
        ("replays/validation/sheik/StiffLustrousZebra.slpz", 4980, 0),
        ("replays/validation/sheik/WavyRundownAardvark.slpz", 6498, 0),
    ],
)
def test_sheik_attacklw4_iasa_b_edge_enters_grounded_needle(
    rel: str, record: int, player: int
) -> None:
    seed, ref, out = _run_one_step_row(_dataset(rel), record, player)

    assert int(seed["action_id"][player]) == ACT_ATTACK_LW4
    assert int(ref["action_id"][player]) == ACT_SK_SPECIAL_N_START
    assert int(out["action_id"][player]) == ACT_SK_SPECIAL_N_START
    assert int(out["action_frame"][player]) == int(ref["action_frame"][player]) == 1


def test_sheik_attacklw4_iasa_without_b_edge_stays_in_attack() -> None:
    def clear_b(_prev_input_t: np.ndarray, input_t: np.ndarray) -> None:
        input_t["p"]["buttons"][0, 0] = np.uint16(int(input_t["p"]["buttons"][0, 0]) & ~BUTTON_B)

    seed, _ref, out = _run_one_step_row(
        _dataset("replays/validation/sheik/StiffLustrousZebra.slpz"),
        4980,
        0,
        input_mutator=clear_b,
    )

    assert int(seed["action_id"][0]) == ACT_ATTACK_LW4
    assert int(out["action_id"][0]) == ACT_ATTACK_LW4


def test_sheik_early_attackhi4_b_edge_waits_for_allow_interrupt() -> None:
    # BeautifulDistantWolverine:2805 has a fresh B edge during early AttackHi4, but source
    # fp+0x2218_b0/allow_interrupt is still clear. The grounded special preamble is only reached
    # after the AttackHi4 IASA gate, so this must not enter Needle.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi4.c::ftCo_AttackHi4_IASA
    seed, ref, out = _run_one_step_row(
        _dataset("replays/validation/sheik/BeautifulDistantWolverine.slpz"),
        2805,
        1,
    )

    assert int(seed["action_id"][1]) == ACT_ATTACK_HI4
    assert (int(seed["state_flags"][1][0]) & STATE_FLAG_2218_ALLOW_INTERRUPT) == 0
    assert int(ref["action_id"][1]) == ACT_ATTACK_HI4
    assert int(out["action_id"][1]) == ACT_ATTACK_HI4
