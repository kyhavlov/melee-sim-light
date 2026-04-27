from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

MSL_ACT_ATTACK_HI4 = 63
MSL_BUTTON_A = 0x0100


def _skip_if_dataset_missing(root: Path) -> None:
    rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    if not (root / rel).exists():
        pytest.skip(f"missing local dataset: {rel}")


def _step_row(*, ds, record: int, defender: int, current_a_held: bool | None = None) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    row = ds.samples[record : record + 1]
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = (
        np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    )

    input_t = row["input_t"].copy()
    if current_a_held is not None:
        buttons = int(input_t["p"]["buttons"][0, defender])
        if current_a_held:
            buttons |= MSL_BUTTON_A
        else:
            buttons &= ~MSL_BUTTON_A
        input_t["p"]["buttons"][0, defender] = np.uint16(buttons)
    input_bytes = np.frombuffer(input_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)

    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return row["ref_t1"][0], out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


@pytest.mark.integration
def test_attackhi4_smash_charge_knockback_multiplier_accepts_fsp_body_hit() -> None:
    # Replay-real positive for the decomp damage path:
    # - Fox is in grounded AttackHi4 and crosses the extracted start_smash_charge command.
    # - held A lets ftCo_800DF0D0 promote SmashState_PreCharge -> SmashState_Charging.
    # - ftCo_Damage_CalcKnockback then applies p_ftCommonData->kb_smashcharge_mul.
    #
    # refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEE84,ftCo_800DF0D0}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
    # data/moves/fox.json::ftCo_SM_AttackHi4.events start_smash_charge
    root = Path(__file__).resolve().parents[1]
    _skip_if_dataset_missing(root)
    ds = read_dataset(str(root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"))
    record = 6454
    defender = 0

    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][defender]) == MSL_ACT_ATTACK_HI4
    assert int(row["seed_t"]["action_frame"][defender]) == 1
    assert int(row["prev_input_t"]["p"]["buttons"][defender]) & MSL_BUTTON_A
    assert int(row["input_t"]["p"]["buttons"][defender]) & MSL_BUTTON_A

    ref, out = _step_row(ds=ds, record=record, defender=defender)
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 7
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 17
    assert float(out["speed_x_attack"][defender]) == pytest.approx(float(ref["speed_x_attack"][defender]))
    assert float(out["speed_y_attack"][defender]) == pytest.approx(float(ref["speed_y_attack"][defender]))


@pytest.mark.integration
def test_attackhi4_smash_charge_knockback_multiplier_denies_without_current_a() -> None:
    # Boundary negative on the same contact: previous A alone is insufficient. The source predicate
    # is SmashState_Charging, and ftCo_800DF0D0 only promotes PreCharge while current held A is set.
    root = Path(__file__).resolve().parents[1]
    _skip_if_dataset_missing(root)
    ds = read_dataset(str(root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"))
    record = 6454
    defender = 0

    ref, out = _step_row(ds=ds, record=record, defender=defender, current_a_held=False)
    assert int(ref["hitstun"][defender]) == 17
    assert int(out["hitstun"][defender]) == 14
    assert float(out["speed_x_attack"][defender]) < float(ref["speed_x_attack"][defender])
    assert float(out["speed_y_attack"][defender]) < float(ref["speed_y_attack"][defender])
