from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


ACT_DOWN_DAMAGE_U = 0x00B9


def test_downdamageu_active_hitlag_sdi_uses_common_damage_owner() -> None:
    # Replay-real positive:
    # DownDamageU re-enters the common damage setup through ftCo_8009F184, so active hitlag uses
    # the same `allow_sdi` / ftCo_Damage_OnEveryHitlag callback as DownDamageD and DamageFly.
    # PTE 7598 has a right-stick SDI input during active DownDamageU hitlag; vanilla moves the root
    # by the source sdi_pos_scale displacement while hitlag remains active.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{
    #   ftCo_8009F184,ftCo_DownDamage_Phys}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_8008DCE0,ftCo_Damage_OnEveryHitlag}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        / "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, ref, out = _run_one_step_row(dataset_path, 7598, 1)
    p = 1
    assert int(seed["action_id"][p]) == ACT_DOWN_DAMAGE_U
    assert int(seed["hitlag"][p]) == 2
    assert int(ref["action_id"][p]) == ACT_DOWN_DAMAGE_U
    assert int(ref["hitlag"][p]) == 1

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-5)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-5)


def test_downdamageu_hitlag_exit_asdi_di_uses_same_allow_sdi_lane() -> None:
    # Replay-real continuation:
    # on the hitlag-exit row, the same DownDamageU `allow_sdi` seed lane admits ASDI before
    # ftCo_8008E5A4 DI adjusts knockback. This keeps the hitlag-active SDI fix tied to the full
    # common damage callback owner instead of a single active-hitlag row.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnExitHitlag,ftCo_8008E5A4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        / "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, ref, out = _run_one_step_row(dataset_path, 7599, 1)
    p = 1
    assert int(seed["action_id"][p]) == ACT_DOWN_DAMAGE_U
    assert int(seed["hitlag"][p]) == 1
    assert int(ref["action_id"][p]) == ACT_DOWN_DAMAGE_U
    assert int(ref["hitlag"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-5)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-5)
    assert float(out["speed_x_attack"][p]) == pytest.approx(float(ref["speed_x_attack"][p]), abs=1e-5)
    assert float(out["speed_y_attack"][p]) == pytest.approx(float(ref["speed_y_attack"][p]), abs=1e-5)


def test_downdamageu_hitlag_no_sdi_without_source_stick_window() -> None:
    # Replay-real negative:
    # the immediately preceding DownDamageU hitlag row has no source-valid SDI input. The U/D
    # parity repair must not move every DownDamageU hitlag frame.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        / "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, ref, out = _run_one_step_row(dataset_path, 7597, 1)
    p = 1
    assert int(seed["action_id"][p]) == ACT_DOWN_DAMAGE_U
    assert int(seed["hitlag"][p]) == 3
    assert int(ref["hitlag"][p]) == 2
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-5)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-5)
