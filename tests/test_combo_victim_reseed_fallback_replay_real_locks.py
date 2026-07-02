from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.validation_dtypes import COMPARE_DTYPE, SEED_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    attacker_p: int
    victim_p: int
    note: str


_CASES = (
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        record=3761,
        attacker_p=0,
        victim_p=1,
        note="hitstun-backed combo victim reseed fallback",
    ),
    _Case(
        dataset_rel=(
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz"
        ),
        record=7518,
        attacker_p=1,
        victim_p=0,
        note="combo-timer-backed combo victim reseed fallback",
    ),
)


def _read_compare(handle) -> np.void:
    import msl_binding

    compare_stride = int(msl_binding.sizes()["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize
    out = np.zeros((1, compare_stride), dtype=np.uint8)
    msl_binding.write_compare(handle, out)
    return out.view(COMPARE_DTYPE).reshape((1,))[0]


def _synthetic_combo_seed_ambiguity_row() -> np.ndarray:
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["num_players"][0] = np.uint8(4)
    seed["stocks"][0, :4] = np.uint8(4)
    seed["char_id"][0, :4] = np.uint8(1)
    seed["attack_ratio"][0, :4] = np.float32(1.0)
    seed["defense_ratio"][0, :4] = np.float32(1.0)
    seed["fighter_scale_y"][0, :4] = np.float32(1.0)
    seed["facing"][0, :4] = np.uint8(1)
    seed["on_ground"][0, :4] = np.uint8(1)
    seed["action_id"][0, :4] = np.uint16(0x000E)  # Wait
    seed["animation_index"][0, :4] = np.uint32(2)  # Wait1
    seed["frame_speed_mul_f32"][0, :4] = np.float32(1.0)
    seed["instance_id"][0, 0] = np.uint16(111)
    seed["instance_id"][0, 1] = np.uint16(222)
    seed["instance_id"][0, 2] = np.uint16(333)
    seed["instance_id"][0, 3] = np.uint16(444)

    attacker = 0
    seed["attack_id"][0, attacker] = np.uint16(7)
    seed["combo_count"][0, attacker] = np.uint8(2)
    seed["last_attack_landed"][0, attacker] = np.uint8(7)
    seed["combo_victim_port"][0, attacker] = np.uint8(0xFF)

    # Ambiguous active-combo fallback shape:
    # - both candidates are attributed to the attacker by `last_hit_by`,
    # - both remain in active combo context,
    # - neither matches the strict BODY owner lane (`instance_hit_by != attacker instance_id`).
    # The reseed bridge must not infer fp->x2094 here.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800763C0,ftColl_800764DC}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
    for victim_p, hitstun, combo_timer in ((1, 9, 0), (2, 0, 6)):
        seed["last_hit_by"][0, victim_p] = np.uint8(attacker)
        seed["instance_hit_by"][0, victim_p] = np.uint16(999)
        seed["hitstun"][0, victim_p] = np.uint16(hitstun)
        seed["combo_timer_x2098"][0, victim_p] = np.uint16(combo_timer)

    return seed


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).stem}:rec{c.record}:p{c.attacker_p}")
def test_combo_victim_reseed_fallback_rows_are_replay_exact(case: _Case) -> None:
    # Replay-real lock for the fallback x2094 reseed bridge:
    # - ftColl_800763C0 continuation compares the stored victim pointer and current attack id.
    # - ftColl_800764DC / ftCo_8008F744 keep that pointer live while the victim remains in active
    #   combo context (victim hitstun or victim combo timer).
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800763C0,ftColl_800764DC}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > case.record, f"replay too short for record={case.record}"

    row = samples[case.record : case.record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    attacker_p = int(case.attacker_p)
    victim_p = int(case.victim_p)

    assert int(seed["combo_count"][attacker_p]) > 0, case.note
    assert int(seed["combo_victim_port"][attacker_p]) == 0xFF, case.note
    assert int(seed["last_hit_by"][victim_p]) == attacker_p, case.note

    if attacker_p == 0:
        assert int(seed["hitstun"][victim_p]) > 0, case.note
        assert int(seed["combo_timer_x2098"][victim_p]) == 0, case.note
        assert int(ref["combo_count"][attacker_p]) == 0, case.note
    else:
        assert int(seed["hitstun"][victim_p]) == 0, case.note
        assert int(seed["combo_timer_x2098"][victim_p]) > 0, case.note
        assert int(ref["combo_count"][attacker_p]) == int(seed["combo_count"][attacker_p]) + 1, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.record, attacker_p)
    for field in ("combo_count", "last_attack_landed", "last_hit_by"):
        assert int(out_row[field][attacker_p]) == int(ref_row[field][attacker_p]), (
            f"{case.note}: field={field} expected={int(ref_row[field][attacker_p])} "
            f"got={int(out_row[field][attacker_p])}"
        )


def test_combo_victim_reseed_fallback_does_not_infer_ambiguous_multi_candidate_context() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_stride == SEED_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=4)
    try:
        seed = _synthetic_combo_seed_ambiguity_row()
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))

        # BODY contact on one candidate after an ambiguous reseed:
        # - ftColl_800763C0 should see "no stored victim yet" and start a fresh combo at 1.
        # - If the fallback guessed a victim despite multiple active candidates, this would bump
        #   from 2 -> 3 instead.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_800763C0
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, 1 << 9)
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        msl_binding.debug_combat_resolve(handle)
        out = _read_compare(handle)

        assert int(out["combo_count"][0]) == 1
        assert int(out["last_attack_landed"][0]) == 7
    finally:
        msl_binding.destroy(handle)
