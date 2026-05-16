from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp

MSL_ACT_ATTACK_HI4 = 63
MSL_BUTTON_A = 0x0100


def _skip_if_dataset_missing(root: Path) -> None:
    rel = "replays/validation/aggregate_recent/FavorableSuperficialPig.slpz"
    if not (root / rel).exists():
        pytest.skip(f"missing local replay: {rel}")


def _build_fsp_dataset(root: Path):
    return build_dataset_from_slp(
        slp_path=str(root / "replays/validation/aggregate_recent/FavorableSuperficialPig.slpz"),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )


def _step_row(
    *,
    ds,
    record: int,
    defender: int,
    current_a_held: bool | None = None,
    seed_mutator=None,
) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    row = ds.samples[record : record + 1]
    seed_t = row["seed_t"].copy()
    if seed_mutator is not None:
        seed_mutator(seed_t)
    seed_bytes = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
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
    ds = _build_fsp_dataset(root)
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
    ds = _build_fsp_dataset(root)
    record = 6454
    defender = 0

    ref, out = _step_row(ds=ds, record=record, defender=defender, current_a_held=False)
    assert int(ref["hitstun"][defender]) == 17
    assert int(out["hitstun"][defender]) == 14
    assert float(out["speed_x_attack"][defender]) < float(ref["speed_x_attack"][defender])
    assert float(out["speed_y_attack"][defender]) < float(ref["speed_y_attack"][defender])


@pytest.mark.integration
def test_attackhi4_smash_release_seed_state_accepts_selfplay_hitbox_damage() -> None:
    # Replay-real positive for the attacker-side released-smash damage owner:
    # - AttackHi4 crosses `start_smash_charge`, held A freezes the timeline, then releasing A keeps
    #   SmashState_Release with the held-frame count.
    # - ftColl_8007ABD0 calls ftCo_800DEEB8, so the hitbox damage/KB at the later hit must use the
    #   hidden release frames rather than the raw uncharged HitCapsule damage.
    #
    # refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEE84,ftCo_800DF0D0,ftCo_800DEEB8}
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80073008
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007ABD0
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    record = 2362
    attacker = 1
    victim = 0

    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][attacker]) == MSL_ACT_ATTACK_HI4
    assert int(seed["smash_charge_state"][attacker]) == 3
    assert int(seed["smash_charge_frames"][attacker]) == 11
    assert int(seed["smash_charge_hold_frames_max"][attacker]) == 60
    assert int(ds.samples[record + 1]["seed_t"]["smash_charge_state"][attacker]) == 0

    ref, out = _step_row(ds=ds, record=record, defender=attacker)
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim]) == 90
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim]) == 87
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]), abs=1.0e-6)
    assert float(out["speed_x_attack"][victim]) == pytest.approx(
        float(ref["speed_x_attack"][victim]), abs=1.0e-6
    )
    assert float(out["speed_y_attack"][victim]) == pytest.approx(
        float(ref["speed_y_attack"][victim]), abs=1.0e-6
    )


@pytest.mark.integration
def test_attackhi4_smash_release_api_seed_sanitizes_bad_saved_rate() -> None:
    # API seed guardrail for the same release state: source `ftCo_800DEF38` only saves a positive
    # frame rate before entering release/charge, but external reseed callers can supply arbitrary
    # bytes. A negative saved rate must fall back to normal 1.0 speed without changing dataset
    # derived release damage.
    # refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEF38,ftCo_800DF0D0}
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    record = 2362
    attacker = 1
    victim = 0

    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][attacker]) == MSL_ACT_ATTACK_HI4
    assert int(seed["smash_charge_state"][attacker]) == 3
    assert int(seed["smash_charge_hold_frames_max"][attacker]) == 60
    assert int(seed["smash_charge_saved_rate_fp_q16_16"][attacker]) == 1 << 16

    def corrupt_saved_rate(seed_t) -> None:
        seed_t["smash_charge_saved_rate_fp_q16_16"][0, attacker] = np.int32(-1)

    ref, out = _step_row(ds=ds, record=record, defender=attacker, seed_mutator=corrupt_saved_rate)
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim]) == 90
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim]) == 87
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]), abs=1.0e-6)
    assert float(out["speed_x_attack"][victim]) == pytest.approx(
        float(ref["speed_x_attack"][victim]), abs=1.0e-6
    )


@pytest.mark.integration
def test_attackhi4_short_release_seed_state_accepts_selfplay_hitbox_damage() -> None:
    # Same source owner as the longer release case, but with a short four-frame release. This locks
    # the opcode-56 damage scale itself: `ftAction_80073008` decodes damage_mul with the
    # single-precision ftAction_804D82A0 literal, then `ftColl_8007ABD0` calls ftCo_800DEEB8 before
    # knockback calculation.
    #
    # refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_804D82A0
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80073008
    # refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DF0D0,ftCo_800DEEB8}
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    record = 1414
    attacker = 0
    victim = 1

    seed = ds.samples[record]["seed_t"]
    assert int(seed["action_id"][attacker]) == MSL_ACT_ATTACK_HI4
    assert int(seed["smash_charge_state"][attacker]) == 3
    assert int(seed["smash_charge_frames"][attacker]) == 4

    ref, out = _step_row(ds=ds, record=record, defender=attacker)
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim]) == 90
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim]) == 71
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]), abs=1.0e-6)
    assert float(out["speed_x_attack"][victim]) == pytest.approx(
        float(ref["speed_x_attack"][victim]), abs=1.0e-6
    )
    assert float(out["speed_y_attack"][victim]) == pytest.approx(
        float(ref["speed_y_attack"][victim]), abs=1.0e-6
    )


@pytest.mark.integration
def test_attackhi4_smash_release_seed_state_denies_raw_uncharged_damage() -> None:
    # Boundary negative on the same hit: clearing the hidden release state falls back to raw
    # uncharged hitbox damage and visibly underestimates percent/knockback. This protects the seed
    # lane from being mistaken for a broad contact or stale-move workaround.
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    record = 2362
    attacker = 1
    victim = 0

    def clear_release_state(seed_t) -> None:
        seed_t["smash_charge_state"][0, attacker] = np.uint8(0)
        seed_t["smash_charge_frames"][0, attacker] = np.uint8(0)
        seed_t["smash_charge_hold_frames_max"][0, attacker] = np.uint8(0)
        seed_t["smash_charge_saved_rate_fp_q16_16"][0, attacker] = np.int32(0)

    ref, out = _step_row(ds=ds, record=record, defender=attacker, seed_mutator=clear_release_state)
    assert float(ref["percent"][victim]) - float(out["percent"][victim]) > 1.0
    assert int(out["hitstun"][victim]) < int(ref["hitstun"][victim])


@pytest.mark.integration
def test_attackhi4_smash_release_api_seed_zero_hold_clears_inconsistent_state() -> None:
    # Inconsistent external seed bytes with Release state but no saved hold window are not a valid
    # source state; reseed clears them instead of restoring a stale charged-damage lane.
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    record = 2362
    attacker = 1
    victim = 0

    def corrupt_zero_hold(seed_t) -> None:
        seed_t["smash_charge_state"][0, attacker] = np.uint8(3)
        seed_t["smash_charge_hold_frames_max"][0, attacker] = np.uint8(0)
        seed_t["smash_charge_saved_rate_fp_q16_16"][0, attacker] = np.int32(1 << 16)

    ref, out = _step_row(ds=ds, record=record, defender=attacker, seed_mutator=corrupt_zero_hold)
    assert float(ref["percent"][victim]) - float(out["percent"][victim]) > 1.0
    assert int(out["hitstun"][victim]) < int(ref["hitstun"][victim])
