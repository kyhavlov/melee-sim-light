from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing


def _assert_branch_identity_match_ref(*, out_row, ref_row, p: int) -> None:
    for field in ("action_id", "action_frame", "on_ground", "hitlag", "hitstun"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), field
    assert [int(x) for x in out_row["state_flags"][p].tolist()] == [
        int(x) for x in ref_row["state_flags"][p].tolist()
    ]


@pytest.mark.integration
def test_guardsetoff_ground_push_target_pm1_and_negative() -> None:
    # Replay-real lock for grounded GuardSetOff pushback ownership:
    # - ftColl_80076CBC writes x19A4/specialn_facing_dir on shield contact.
    # - ftCo_80092F2C computes the grounded GuardSetOff push and writes fp->gr_vel.
    # This uses a committed validation-suite row instead of the old ignored
    # replays/validation/** cache row, so local debug cache regeneration cannot affect it.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    target_path = root / (
        "replays/validation/aggregate_recent/"
        "TubbyCurlyHerring.slpz"
    )
    if not target_path.exists():
        pytest.skip("missing local replay artifacts")

    p = 1
    seed_neg, ref_neg, out_neg = _run_one_step_row(target_path, 432, p)
    _assert_branch_identity_match_ref(out_row=out_neg, ref_row=ref_neg, p=p)
    assert int(seed_neg["action_id"][p]) == 179  # Guard
    assert int(ref_neg["action_id"][p]) == 179  # Guard, not GuardSetOff
    assert float(out_neg["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_neg["speed_ground_x_self"][p]), abs=1e-6
    )

    seed_target, ref_target, out_target = _run_one_step_row(target_path, 433, p)
    _assert_branch_identity_match_ref(out_row=out_target, ref_row=ref_target, p=p)
    assert int(seed_target["action_id"][p]) == 179  # Guard
    assert int(ref_target["action_id"][p]) == 181  # GuardSetOff
    assert float(out_target["speed_ground_x_self"][p]) == pytest.approx(
        float(ref_target["speed_ground_x_self"][p]), abs=1e-6
    )
    assert abs(float(out_target["speed_ground_x_self"][p]) - float(ref_target["speed_ground_x_self"][p])) < abs(
        float(seed_target["speed_ground_x_self"][p]) - float(ref_target["speed_ground_x_self"][p])
    )
    assert float(out_target["speed_air_x_self"][p]) == pytest.approx(
        float(ref_target["speed_air_x_self"][p]), abs=1e-6
    )


@pytest.mark.integration
def test_guardreflect_hitshield_guardsetoff_recoil_uses_x221c_b2_boundary() -> None:
    # Replay-real lock for the GuardReflect -> item HitShield -> GuardSetOff recoil boundary:
    # - ftCo_80092F2C's recoil multiplier reads fp->x221C_b2 directly.
    # - GuardReflect's x18 timer owns that bit beyond the shorter x14 ReflectDesc window.
    # - Clearing x18/0x221C_b2 must fall back to the non-powershield recoil multiplier instead of
    #   treating every GuardReflect HitShield row as powershield-active.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_80092F2C}
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Guard.s:0x80093080..0x800930A0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    target_path = root / (
        "replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.slpz"
    )
    if not target_path.exists():
        pytest.skip("missing local replay artifacts")

    p = 0
    seed, ref, out = _run_one_step_row(target_path, 2323, p)
    assert int(seed["action_id"][p]) == 182  # GuardReflect
    assert int(ref["action_id"][p]) == 181  # GuardSetOff
    assert int(seed["guard_reflect_timer_x18"][p]) > 0
    assert int(seed["state_flags"][p, 3]) & 0x20
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=1e-6
    )
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(0.5800000429153442, abs=1e-6)

    def clear_x221c_b2(seed_t) -> None:
        seed_t["guard_reflect_timer_x18"][0, p] = 0
        seed_t["state_flags"][0, p, 3] = int(seed_t["state_flags"][0, p, 3]) & ~0x20

    _, _, out_without_b2 = _run_one_step_row(target_path, 2323, p, seed_mutator=clear_x221c_b2)
    assert int(out_without_b2["action_id"][p]) == 181
    assert float(out_without_b2["speed_ground_x_self"][p]) == pytest.approx(0.34800004959106445, abs=1e-6)
    assert float(out_without_b2["speed_ground_x_self"][p]) != pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=1e-6
    )
