from __future__ import annotations

from pathlib import Path

import pytest

from tools.eval.dataset import read_dataset


@pytest.mark.integration
def test_throwhi_command_pending_pulse_follows_timer_cursor() -> None:
    # Prefix-causal seed-lane lock for throw-side projectile command timing:
    # - ftAction_80073354 owns the command timer and does not simply replay every visible
    #   action-frame crossing.
    # - ftAction_80071974 sets one bool throw_flags_b0, consumed once by ftFx_Throw_Anim.
    # - In BHH's Fox ThrowHi run, frame 18 executes, the frame-20 command is still pending on the
    #   next post-frame, and then frame 20 executes one source tick later.
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    cases = [
        (4335, 0, 17, 0, 18),
        (1250, 0, 18, 18, 0),
        (1251, 0, 20, 20, 20),
    ]
    for record, player, action_frame, crossed_prev, pending in cases:
        seed = ds.samples[record]["seed_t"]
        assert int(seed["action_id"][player]) == 221
        assert int(seed["action_frame"][player]) == action_frame
        assert int(seed["throw_pulse_crossed_prev_frame"][player]) == crossed_prev
        assert int(seed["throw_command_pending_pulse_frame"][player]) == pending


@pytest.mark.integration
def test_throwlw_command_pending_pulse_covers_first_mid_terminal_pulses() -> None:
    # ThrowLw has projectile commands at 23/25/28/31. The seed lane records the command-timer
    # pending pulse for the current source step, not just raw visible frame crossings.
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # data/moves/{fox,falco}.json moves["ftCo_SM_ThrowLw"].events
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    cases = [
        (9177, 0, 22, 0, 23),
        (9180, 0, 25, 25, 0),
        (9182, 0, 27, 0, 28),
        (9185, 0, 30, 0, 31),
    ]
    for record, player, action_frame, crossed_prev, pending in cases:
        seed = ds.samples[record]["seed_t"]
        assert int(seed["action_id"][player]) == 222
        assert int(seed["action_frame"][player]) == action_frame
        assert int(seed["throw_pulse_crossed_prev_frame"][player]) == crossed_prev
        assert int(seed["throw_command_pending_pulse_frame"][player]) == pending


@pytest.mark.integration
def test_throwlw_attached_laser_hitlist_seed_lane_latches_victim_ring() -> None:
    # Prefix-causal item HitCapsule seed lane:
    # - FSP:9180 carries a ThrowLw state1 Fox laser (item1 / iid 1497) beside an attached
    #   ThrownLw victim whose instance_hit_by matches that item.
    # - Dolphin engine-dump v10 shows the throw laser's item HitCapsule victims_1 entry populated
    #   in this family. it_8026FA2C propagates victim writes across enabled item hitcaps sharing
    #   the same HitCapsule.x4 group, so preprocessing seeds the compact group mask directly
    #   instead of inferring from command timing alone.
    # refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_8026FA2C,it_80272460}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed = read_dataset(str(dataset_path)).samples[9180]["seed_t"]
    assert int(seed["items"][1]["exists"]) == 1
    assert int(seed["items"][1]["type"]) == 54
    assert int(seed["items"][1]["state"]) == 1
    assert int(seed["items"][1]["instance_id"]) == 1497
    assert int(seed["action_id"][1]) == 242
    assert int(seed["grab_owner_port"][1]) == 0
    assert int(seed["instance_hit_by"][1]) == 1497
    assert int(seed["item_hitlist_victim_port"][1]) == 1
    assert int(seed["item_hitlist_victim_cd"][1]) == 16
    assert int(seed["item_hitlist_victim_hitbox_mask"][1]) == 0x03
    assert int(seed["item_hitlist_victim_iid"][1]) == int(seed["instance_id"][1])


@pytest.mark.integration
def test_throwhi_non_attached_laser_hitlist_seed_lane_negative_control() -> None:
    # Negative control for the item-hitlist seed lane:
    # - BHH:937 has a live ThrowHi state1 laser and victim instance_hit_by matching the item, but
    #   the v10 dump showed no relevant populated item hitlist entry.
    # - Because the victim is not attached/grabbed, the seed lane remains empty; this keeps the
    #   lane from becoming a broad visible instance_hit_by proxy.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed = read_dataset(str(dataset_path)).samples[937]["seed_t"]
    assert int(seed["items"][1]["exists"]) == 1
    assert int(seed["items"][1]["type"]) == 54
    assert int(seed["items"][1]["state"]) == 1
    assert int(seed["items"][1]["instance_id"]) == int(seed["instance_hit_by"][0])
    assert int(seed["grab_owner_port"][0]) == 255
    assert int(seed["item_hitlist_victim_port"][1]) == 255
    assert int(seed["item_hitlist_victim_hitbox_mask"][1]) == 0


@pytest.mark.integration
def test_falco_throwlw_attached_hitlist_seed_lane_keeps_body_callback_phase_split() -> None:
    # Falco primary-control row:
    # - QGD:443 has the same visible attached ThrowLw shape as the Fox FSP carry rows, but v10
    #   dumps show the first visible victim-ring entries on hitboxes 2/3 while the BODY callback
    #   still needs hitboxes 0/1 to stay eligible.
    # - The compact seed producer now records the 2/3 victim-ring mask; runtime must consume it as
    #   per-HitCapsule state so 0/1 remain BODY-eligible instead of suppressing the whole item.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed = read_dataset(str(dataset_path)).samples[443]["seed_t"]
    assert int(seed["action_id"][0]) == 222
    assert int(seed["items"][1]["exists"]) == 1
    assert int(seed["items"][1]["type"]) == 55
    assert int(seed["items"][1]["state"]) == 1
    assert int(seed["grab_owner_port"][1]) == 0
    assert int(seed["instance_hit_by"][1]) == int(seed["items"][1]["instance_id"])
    assert int(seed["item_hitlist_victim_port"][1]) == 1
    assert int(seed["item_hitlist_victim_cd"][1]) == 16
    assert int(seed["item_hitlist_victim_hitbox_mask"][1]) == 0x0C
    assert int(seed["item_hitlist_victim_iid"][1]) == int(seed["instance_id"][1])
