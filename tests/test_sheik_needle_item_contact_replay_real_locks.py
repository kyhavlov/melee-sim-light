"""Sheik thrown-Needle item-contact locks.

These locks cover item/article contact ownership, not Sheik special state dispatch:
- thrown Needle Article data comes from MSLITAR1,
- fighter HitCapsule -> item hurtbox contact writes deal-hitlag and item damage/callback state,
- bounced Needle state-4 carries hidden item hitlag during replay reseed.

Sources:
- refs/melee/src/melee/ft/chara/ftSeak/ftSk_Init.c::ftSk_Init_OnLoad
- refs/melee/src/melee/it/itcoll.c::it_802703E8
- refs/melee/src/melee/it/item.c::{OnTakeDamageThink,Item_8026A294,Item_802697D4}
- refs/melee/src/melee/it/items/itseakneedlethrown.c
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

pytest.importorskip("msl_binding")

from tools.eval.dataset import COMPARE_DTYPE, read_dataset  # noqa: E402

DATASET = Path("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl")
P_MARTH = 1
ITEM_NEEDLE_THROWN = 79
ITEM_NEEDLE_HELD = 80


def _require_dataset() -> None:
    if not DATASET.exists():
        pytest.skip(f"missing generated dataset {DATASET}")


def _run_row(record: int, *, mutate_item0_type: int | None = None) -> tuple[np.void, np.void, np.void]:
    import msl_binding

    _require_dataset()
    ds = read_dataset(str(DATASET))
    row = ds.samples[record : record + 1].copy()
    if mutate_item0_type is not None:
        row["seed_t"]["items"]["type"][0, 0] = np.uint16(mutate_item0_type)

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_bytes = row["seed_t"].view(np.uint8).reshape((1, seed_stride)).copy()
    prev_input_bytes = row["prev_input_t"].view(np.uint8).reshape((1, input_stride)).copy()
    input_bytes = row["input_t"].view(np.uint8).reshape((1, input_stride)).copy()
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        return row["seed_t"][0].copy(), row["ref_t1"][0].copy(), out
    finally:
        msl_binding.destroy(handle)


@pytest.mark.integration
def test_sheik_stuck_needle_fighter_hitbox_contact_deal_hitlag_and_destroys_replay_real() -> None:
    seed, ref, out = _run_row(176)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 2
    assert int(seed["hitlag"][P_MARTH]) == 0

    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 6
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    # The DmgReceived callback returned true on this source RNG stream, so the thrown Needle is
    # destroyed and item sorting moves the held Needle into slot 0.
    assert int(out["items"]["type"][0]) == int(ref["items"]["type"][0]) == ITEM_NEEDLE_HELD
    assert int(out["items"]["exists"][1]) == int(ref["items"]["exists"][1]) == 0


@pytest.mark.integration
def test_sheik_bounced_needle_clank_contact_uses_item_hitlag_replay_real() -> None:
    seed, ref, out = _run_row(213)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 4
    assert int(seed["hitlag"][P_MARTH]) == 0

    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 6
    assert int(out["items"]["exists"][0]) == int(ref["items"]["exists"][0]) == 0


@pytest.mark.integration
def test_sheik_needle_contact_does_not_apply_before_overlap_or_for_held_article_replay_real() -> None:
    _seed, ref_before, out_before = _run_row(175)

    assert int(out_before["hitlag"][P_MARTH]) == int(ref_before["hitlag"][P_MARTH]) == 0
    assert int(out_before["items"]["type"][0]) == int(ref_before["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert float(out_before["items"]["timer"][0]) == pytest.approx(float(ref_before["items"]["timer"][0]))

    _seed, _ref, out_mut = _run_row(176, mutate_item0_type=ITEM_NEEDLE_HELD)
    assert int(out_mut["hitlag"][P_MARTH]) == 0


@pytest.mark.integration
def test_sheik_bounced_needle_reseed_reconstructs_hidden_item_hitlag_freeze_replay_real() -> None:
    seed, ref, out = _run_row(266)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 4
    assert int(seed["items"]["damage"][0]) == 9
    assert int(seed["hitlag"][P_MARTH]) == 6

    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 5
    assert int(out["items"]["exists"][0]) == int(ref["items"]["exists"][0]) == 1
    assert float(out["items"]["pos_y"][0]) == pytest.approx(float(ref["items"]["pos_y"][0]))
    assert float(out["items"]["vel_y"][0]) == pytest.approx(float(ref["items"]["vel_y"][0]))
    assert float(out["items"]["timer"][0]) == pytest.approx(float(ref["items"]["timer"][0]))
