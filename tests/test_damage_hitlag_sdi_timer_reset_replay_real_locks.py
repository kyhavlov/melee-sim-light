from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_one_step(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    if int(row.shape[0]) != 1:
        raise AssertionError(f"record {record} not found in {dataset_path}")

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_bytes = row["seed_t"].view("u1").reshape(1, seed_stride).copy()
    prev_input_bytes = row["prev_input_t"].view("u1").reshape(1, input_stride).copy()
    input_bytes = row["input_t"].view("u1").reshape(1, input_stride).copy()
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return row["seed_t"][0].copy(), row["ref_t1"][0].copy(), out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


@pytest.mark.integration
def test_damageair3_hitlag_x670_window_sdi_applies_tbk_4222() -> None:
    # Replay-real TBK row from the F08d rollout cluster:
    # - prior input put x670 in the SDI timer window,
    # - x221A_b3 proves ordinary ProcessHit hitlag provenance,
    # - ftCo_Damage_OnEveryHitlag applies +5.775 X SDI despite no fresh full-stick edge.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step(dataset_path, 4222)
    p = 0
    assert int(seed["action_id"][p]) == 86  # DamageAir3
    assert int(seed["hitlag"][p]) == 6
    assert int(seed["state_flags"][p, 1]) & 0x10
    assert int(seed["tilt_timer_x"][p]) < 4
    assert float(ref["pos_x"][p] - seed["pos_x"][p]) == pytest.approx(5.7749977, abs=1e-5)

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_damage_n3_hitlag_sdi_radius_uses_deadzoned_lstick_fsp_10790() -> None:
    # Replay-real negative for the `ftCo_Damage_OnEveryHitlag` stick-radius owner:
    # - Fighter_Spaghetti zeroes fp->input.lstick axes inside the global deadzone before hitlag_cb.
    # - FSP:10790 has raw stick (54,16), but the Y axis is deadzone-zeroed, so the source radius
    #   predicate sees only X=0.675 and vanilla does not consume SDI until the next stronger input.
    # refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "FavorableSuperficialPig.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[10790:10791]
    assert int(row.shape[0]) == 1
    seed, ref, out = _run_one_step(dataset_path, 10790)
    p = 0
    assert int(seed["action_id"][p]) == 80  # DamageN3
    assert int(seed["hitlag"][p]) == 4
    assert int(seed["tilt_timer_x"][p]) < 4
    assert int(row["input_t"]["p"][0, p]["main_x"]) == 54
    assert int(row["input_t"]["p"][0, p]["main_y"]) == 16
    assert float(ref["pos_x"][p]) == pytest.approx(float(seed["pos_x"][p]), abs=1e-6)
    assert float(ref["pos_y"][p]) == pytest.approx(float(seed["pos_y"][p]), abs=1e-6)

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_damageair3_prior_sdi_reset_blocks_stale_timer_window_tbk_4224() -> None:
    # Negative adjacent row: the prior hitlag callback reset x670/x671 to 0xFE. Raw replay inputs
    # would otherwise look like a held-stick timer-window row, but vanilla applies no SDI here.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step(dataset_path, 4224)
    p = 0
    assert int(seed["action_id"][p]) == 86  # DamageAir3
    assert int(seed["hitlag"][p]) == 4
    assert int(seed["state_flags"][p, 1]) & 0x10
    assert int(seed["tilt_timer_x"][p]) == 0xFE
    assert int(seed["tilt_timer_y"][p]) == 0xFE
    assert float(ref["pos_x"][p]) == pytest.approx(float(seed["pos_x"][p]), abs=1e-6)
    assert float(ref["pos_y"][p]) == pytest.approx(float(seed["pos_y"][p]), abs=1e-6)

    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_damagefly_fresh_entry_reset_blocks_radius_crossing_sdi_bhh_1600() -> None:
    # Source negative for first-active radius crossing:
    # - ftCo_8008DCE0 has reset x670/x671 to 0xFE on DamageFly entry.
    # - Fighter_Spaghetti's lb_8000D148 input segment did not reset x679/x67A, so the horizontal
    #   DamageFlyN row stays on the ordinary x670/x671 path instead of the first-active SDI bridge.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_8008DCE0,ftCo_Damage_OnEveryHitlag,ftCo_DamageFly_Coll}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10,lb_8000D148}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "BlondHardHippopotamus.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step(dataset_path, 1600)
    p = 1
    assert int(seed["action_id"][p]) == 88  # DamageFlyN
    assert int(seed["hitlag"][p]) == 6
    assert int(seed["tilt_timer_x"][p]) == 0xFE
    assert int(seed["tilt_timer_y"][p]) == 0xFE
    assert int(seed["x679_x"][p]) != 0
    assert int(seed["x67A_y"][p]) != 0
    assert float(ref["pos_x"][p]) == pytest.approx(float(seed["pos_x"][p]), abs=1e-6)
    assert float(ref["pos_y"][p]) == pytest.approx(float(seed["pos_y"][p]), abs=1e-6)

    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_damageflyn_first_active_source_input_crossing_sdi_applies_pyo_364() -> None:
    # Positive counterpart for the DamageFlyN first-active bridge. The row has the same fresh
    # ftCo_8008DCE0 x670/x671 reset shape as BHH:1600, but Fighter_Spaghetti's source input segment
    # reset x679/x67A through lb_8000D148. Vanilla consumes that callback-local SDI pulse before
    # hitlag decrements.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10,lb_8000D148}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PutridJoyousOryx.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step(dataset_path, 364)
    p = 1
    assert int(seed["action_id"][p]) == 88  # DamageFlyN
    assert int(seed["hitlag"][p]) == 6
    assert int(seed["tilt_timer_x"][p]) == 0xFE
    assert int(seed["tilt_timer_y"][p]) == 0xFE
    assert int(seed["x679_x"][p]) == 0
    assert int(seed["x67A_y"][p]) == 0
    assert float(ref["pos_x"][p] - seed["pos_x"][p]) == pytest.approx(5.7749996, abs=1e-5)

    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_damageflytop_to_damagefall_preserves_x670_for_exit_gat_8021() -> None:
    # Negative for the damage-entry backfill: natural DamageFlyTop -> DamageFall is not
    # ftCo_8008DCE0 and must not clear x670/x671. The preserved x670 lets DamageFall immediately
    # complete into Fall on the next frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Anim,ftCo_8008DCE0}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step(dataset_path, 8021)
    p = 0
    assert int(seed["action_id"][p]) == 38  # DamageFall
    assert int(seed["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) == 0
    assert int(seed["tilt_timer_x"][p]) == 0
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 29  # Fall
    assert int(out["instance_id"][p]) == int(ref["instance_id"][p])


@pytest.mark.integration
def test_damagefall_hitlag_exit_preserves_x670_asdi_window_prh_5280() -> None:
    # Negative for over-broad reset on damage-family transitions. The DamageFall/FlyReflect hitlag
    # exit still owns ASDI via the preserved x670 timer; clearing it loses the downward projection
    # and the x221A_b3 state flag on exit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PositiveRevolvingHyena.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step(dataset_path, 5280)
    p = 0
    assert int(seed["action_id"][p]) == 67
    assert int(seed["hitlag"][p]) == 1
    assert int(seed["tilt_timer_x"][p]) < 30
    assert int(out["state_flags"][p, 1]) == int(ref["state_flags"][p, 1])
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
