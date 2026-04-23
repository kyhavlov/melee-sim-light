from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import read_dataset


@pytest.mark.integration
def test_throwlw_first_attached_pulse_source_and_item_lifetime_lock() -> None:
    # Replay-real lock for the first ThrowLw attached-victim pulse bridge in src/items.c.
    #
    # Decomp/data refs for this lane:
    # - ftFx_Throw_Anim consumes throw_flags_b0 and spawns throw-side state1 lasers.
    # - ThrowLw's victim remains attached in ThrownLw while the first projectile pulse can still
    #   apply item-domain source/bookkeeping and clear the fresh article.
    # - data/moves/{fox,falco}.json has ThrowLw set_throw_spawn_projectile events at 23/25/28/31.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    target_record = 9177
    target = ds.samples[target_record]
    seed = target["seed_t"]
    owner_p = 0
    victim_p = 1

    assert int(seed["action_id"][owner_p]) == 222  # ThrowLw
    assert int(seed["action_frame"][owner_p]) == 22
    assert int(seed["throw_pulse_crossed_prev_frame"][owner_p]) == 0
    assert int(seed["throw_pulse_consumed"][owner_p]) == 0
    assert int(seed["action_id"][victim_p]) == 242  # ThrownLw
    assert int(seed["grab_owner_port"][victim_p]) == owner_p

    for rec in (target_record - 1, target_record, target_record + 1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, owner_p)
        for p in (owner_p, victim_p):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )

    _, ref_row, out_row = _run_one_step_row(dataset_path, target_record, owner_p)
    assert int(out_row["items"][1]["exists"]) == int(ref_row["items"][1]["exists"]) == 0
    assert int(out_row["last_attack_landed"][owner_p]) == int(ref_row["last_attack_landed"][owner_p]) == 56
    assert int(out_row["instance_hit_by"][victim_p]) == int(ref_row["instance_hit_by"][victim_p])
    assert out_row["state_flags"][victim_p].tolist() == ref_row["state_flags"][victim_p].tolist()


@pytest.mark.integration
@pytest.mark.parametrize("target_record", [533, 5637])
def test_falco_throwlw_frame28_spawn_callback_lock(target_record: int) -> None:
    # Falco ThrowLw frame-28 spawn-time BODY callback lock:
    # - v10 item-hitlist dumps show Falco kind-55 attached rows carry victims_1 on hitboxes 2/3
    #   while the frame-28 command spawns a fresh state1 shot and applies BODY callback ownership
    #   through hitboxes 0/1.
    # - Runtime keeps the command phase narrow to pending pulse 28, no seeded live state1 shot, and
    #   no seed-start victim hitlag so QGD primary controls remain BODY-eligible but not hit early.
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    # refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[target_record]["seed_t"]
    owner_p = 1
    victim_p = 0
    assert int(seed["char_id"][owner_p]) == 22
    assert int(seed["action_id"][owner_p]) == 222  # ThrowLw
    assert int(seed["action_frame"][owner_p]) == 27
    assert int(seed["throw_command_pending_pulse_frame"][owner_p]) == 28
    assert int(seed["action_id"][victim_p]) == 242  # ThrownLw
    assert int(seed["grab_owner_port"][victim_p]) == owner_p
    assert int(seed["hitlag"][victim_p]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, target_record, owner_p)
    for p in (owner_p, victim_p):
        assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p])
        assert out_row["state_flags"][p].tolist() == ref_row["state_flags"][p].tolist()


@pytest.mark.integration
@pytest.mark.parametrize("target_record", [447, 4098, 8115])
def test_falco_throwlw_frame28_spawn_callback_primary_negatives(target_record: int) -> None:
    # Primary negative controls for the same Falco frame-28 command phase:
    # QGD rows have seed-start victim hitlag from the previous callback phase, so the frame-28
    # command must spawn/carry without applying another attached BODY callback one step early.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[target_record]["seed_t"]
    owner_p = 0
    victim_p = 1
    assert int(seed["action_id"][owner_p]) == 222
    assert int(seed["action_frame"][owner_p]) == 26
    assert int(seed["action_id"][victim_p]) == 242
    assert int(seed["grab_owner_port"][victim_p]) == owner_p
    assert int(seed["hitlag"][victim_p]) != 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, target_record, owner_p)
    for p in (owner_p, victim_p):
        assert int(out_row["hitlag"][p]) == int(ref_row["hitlag"][p])
        assert out_row["state_flags"][p].tolist() == ref_row["state_flags"][p].tolist()
