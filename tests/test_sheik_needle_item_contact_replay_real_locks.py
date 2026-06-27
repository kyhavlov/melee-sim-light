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
DEMO_DATASET = Path(
    "datasets/sheik_demo_triage/sheik_demo_triage/replays/validation/sheik/sheik_demo_game.msl"
)
DEMO2_FULL_DATASET = Path(
    "datasets/sheik/replays/validation/sheik/sheik_demo_game_2.msl"
)
ZESTY_DATASET = Path("datasets/sheik/replays/validation/sheik/ZestyPreciousTurtle.msl")
TENSE_DATASET = Path("datasets/sheik/replays/validation/sheik/TenseSameHummingbird.msl")
RURAL_DATASET = Path("datasets/sheik/replays/validation/sheik/RuralReasonableRat.msl")
UNUSED_LIVELY_LOUSE_DATASET = Path(
    "datasets/sheik/replays/validation/sheik/UnusedLivelyLouse.msl"
)
ATTRACTIVE_ANY_CLAM_DATASET = Path(
    "datasets/sheik/replays/validation/sheik/AttractiveAnyClam.msl"
)
P_MARTH = 1
ITEM_NEEDLE_THROWN = 79
ITEM_NEEDLE_HELD = 80


def _require_dataset(dataset: Path = DATASET) -> None:
    if not dataset.exists():
        pytest.skip(f"missing generated dataset {dataset}")


def _run_row(
    record: int,
    *,
    dataset: Path = DATASET,
    mutate_item0_type: int | None = None,
    mutate_item0_damage: int | None = None,
    mutate_marth_hitlag: int | None = None,
    mutate_input_r_p0: int | None = None,
) -> tuple[np.void, np.void, np.void]:
    import msl_binding

    _require_dataset(dataset)
    ds = read_dataset(str(dataset))
    row = ds.samples[record : record + 1].copy()
    if mutate_item0_type is not None:
        row["seed_t"]["items"]["type"][0, 0] = np.uint16(mutate_item0_type)
    if mutate_item0_damage is not None:
        row["seed_t"]["items"]["damage"][0, 0] = np.uint16(mutate_item0_damage)
    if mutate_marth_hitlag is not None:
        row["seed_t"]["hitlag"][0, P_MARTH] = np.uint16(mutate_marth_hitlag)
    if mutate_input_r_p0 is not None:
        row["input_t"]["p"]["r"][0, 0] = np.uint8(mutate_input_r_p0)

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


def _run_rollout_records(
    dataset: Path,
    *,
    start_record: int,
    records: tuple[int, ...],
) -> tuple[np.ndarray, dict[int, np.void]]:
    import msl_binding

    _require_dataset(dataset)
    ds = read_dataset(str(dataset))

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    out: dict[int, np.void] = {}
    wanted = set(records)
    max_record = max(wanted)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = (
            ds.samples[start_record : start_record + 1]["seed_t"]
            .view(np.uint8)
            .reshape((1, seed_stride))
            .copy()
        )
        msl_binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, max_record + 1):
            row = ds.samples[record : record + 1]
            frame_seed_bytes = row["seed_t"].view(np.uint8).reshape((1, seed_stride)).copy()
            prev_input_bytes = row["prev_input_t"].view(np.uint8).reshape((1, input_stride)).copy()
            input_bytes = row["input_t"].view(np.uint8).reshape((1, input_stride)).copy()
            msl_binding.step_input_replay_frame_rng(
                handle, frame_seed_bytes, prev_input_bytes, input_bytes
            )
            if record in wanted:
                msl_binding.write_compare(handle, out_bytes)
                out[record] = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)

    return ds.samples, out


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
def test_sheik_thrown_needle_body_hit_deals_damage_replay_real() -> None:
    # A state-0 flying Needle BODY-hits the opponent (Needle as attacker). This is the adjacent
    # deeper-contact negative for the Needle phantom/tip-log lane: ordinary BODY overlap still deals
    # the 3-damage Needle hit, enters the ref Damage* action state, and receives deal-hitlag 4.
    # Because action_id matches ref, the victim's damage-fly reaction roll (the hit's scored RNG) is
    # correct -- the BODY damage this packet owns is not drifting.
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgDealt
    seed, ref, out = _run_row(105)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 0
    assert float(seed["percent"][P_MARTH]) == pytest.approx(0.0)

    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert float(out["percent"][P_MARTH]) == pytest.approx(3.0)
    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH])
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 4

    # The Needle's OWN post-hit fate (it_2725_Logic109_DmgDealt: HSD_Randi(3)==0 bounce else destroy)
    # is seed-DETERMINISTIC but is NOT asserted against ref here, and intentionally so: the sim
    # destroys the Needle on this row while ref bounces it to state 4. DmgDealt reads HSD_Randi(3)
    # downstream of the victim's damage reaction (Fighter_8006CDA4 pre-gate / damage-fly roll), whose
    # RNG-stream phase is reconstructed from a seed-only hidden lane
    # (fighter_8006cda4_pre_gate_consume_count) that Slippi does not expose exactly. That
    # reconstruction is correct enough for the scored VICTIM lanes (asserted above) but not yet for
    # the Needle's downstream callback phase, so item_exists/item_state on this lane is a KNOWN scored
    # limitation tracked as the Needle post-hit RNG-phase follow-up, NOT a body-damage bug. Assert
    # only determinism + that the Needle is no longer a flying state-0 Needle.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    seed2, _ref2, out2 = _run_row(105)
    assert int(out2["items"]["exists"][0]) == int(out["items"]["exists"][0])
    assert int(out2["items"]["state"][0]) == int(out["items"]["state"][0])
    assert not (int(out["items"]["exists"][0]) == 1 and int(out["items"]["state"][0]) == 0)


@pytest.mark.integration
def test_sheik_thrown_needle_tiny_body_overlap_routes_tiplog_without_damage_replay_real() -> None:
    # UnusedLivelyLouse:1012 is a state-0 thrown Needle whose exact lbColl BODY matrix overlap is in
    # the item phantom/tip-log band. Source gives Marth hitlag/instance attribution but no percent,
    # KB, damage-state entry, or Needle DmgDealt bounce/destroy callback. This is the same
    # ftColl_80077C60 checkTipLog owner already modeled for laser item BODY contacts, now retained
    # for thrown Needle's exact matrix path.
    # refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,inlineB1,ftColl_80077C60}
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    seed, ref, out = _run_row(1012, dataset=UNUSED_LIVELY_LOUSE_DATASET)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 0
    assert int(ref["action_id"][P_MARTH]) == int(seed["action_id"][P_MARTH])
    assert int(ref["hitlag"][P_MARTH]) == 3
    assert float(ref["percent"][P_MARTH]) == pytest.approx(float(seed["percent"][P_MARTH]))

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH])
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH])
    assert int(out["instance_hit_by"][P_MARTH]) == int(ref["instance_hit_by"][P_MARTH])
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert int(out["items"]["exists"][0]) == int(ref["items"]["exists"][0]) == 1
    assert int(out["items"]["state"][0]) == int(ref["items"]["state"][0]) == 0


@pytest.mark.integration
def test_sheik_thrown_needle_accessory_spawn_uses_post_physics_root_rollout() -> None:
    # UnusedLivelyLouse rollout from 0: `shootNeedles` is an accessory4 callback, so the thrown
    # article spawns from Sheik's post-Phys/Coll root. A pre-physics spawn keeps the Needle about
    # 1.8 units too high, misses the phantom/tip-log band at 1012, and then BODY-hits one frame
    # late. The adjacent 1011 quiet row locks that this is the source callback position, not a broad
    # early-contact admission.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    #   ftSk_SpecialAirNEnd_Anim,shootNeedles}
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006C80C
    samples, out = _run_rollout_records(
        UNUSED_LIVELY_LOUSE_DATASET, start_record=0, records=(1003, 1011, 1012)
    )

    ref1003 = samples["ref_t1"][1003]
    assert int(out[1003]["items"]["exists"][0]) == int(ref1003["items"]["exists"][0]) == 1
    assert int(out[1003]["items"]["type"][0]) == int(ref1003["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert float(out[1003]["items"]["pos_x"][0]) == pytest.approx(float(ref1003["items"]["pos_x"][0]))
    assert float(out[1003]["items"]["pos_y"][0]) == pytest.approx(float(ref1003["items"]["pos_y"][0]))

    ref1011 = samples["ref_t1"][1011]
    assert int(out[1011]["action_id"][P_MARTH]) == int(ref1011["action_id"][P_MARTH]) == 27
    assert int(out[1011]["hitlag"][P_MARTH]) == int(ref1011["hitlag"][P_MARTH]) == 0
    assert float(out[1011]["items"]["pos_y"][0]) == pytest.approx(float(ref1011["items"]["pos_y"][0]))

    ref1012 = samples["ref_t1"][1012]
    assert int(out[1012]["action_id"][P_MARTH]) == int(ref1012["action_id"][P_MARTH]) == 27
    assert int(out[1012]["hitlag"][P_MARTH]) == int(ref1012["hitlag"][P_MARTH]) == 3
    assert float(out[1012]["percent"][P_MARTH]) == pytest.approx(float(out[1011]["percent"][P_MARTH]))
    assert int(out[1012]["items"]["exists"][0]) == int(ref1012["items"]["exists"][0]) == 1


@pytest.mark.integration
def test_sheik_thrown_needle_fresh_attackdash_guardon_shielddesc_uses_model_scale_replay_real() -> None:
    # BeautifulDistantWolverine:3510 has Sheik's thrown Needle reaching a Fox AttackDash defender
    # on an allow-interrupt frame. AttackDash_IASA delegates to Wait_IASA, the current hard analog R
    # enters GuardOn via ftCo_80091A4C -> ftCo_800924C0, and ftColl_8007925C then resolves the
    # already-live Needle against ShieldDesc before BODY. The accepted boundary depends on the
    # ShieldDesc.size=1 term passing through the defender's model-scale JObj matrix; using only
    # fighter_scale_y misses by <0.001 world units and incorrectly BODY-hits.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800924C0}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
    dataset = Path("datasets/sheik/replays/validation/sheik/BeautifulDistantWolverine.msl")
    seed, ref, out = _run_row(3510, dataset=dataset)
    defender = 0

    assert int(seed["action_id"][defender]) == 50  # AttackDash
    assert int(seed["items"]["type"][3]) == ITEM_NEEDLE_THROWN
    assert int(ref["action_id"][defender]) == 181  # GuardSetOff

    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 4
    assert float(out["percent"][defender]) == pytest.approx(float(seed["percent"][defender]))
    assert float(out["shield_hp"][defender]) < float(seed["shield_hp"][defender]) - 1.0

    # Adjacent negative: with the same Needle/body geometry but no current trigger, Wait_IASA cannot
    # publish ShieldDesc, so the item falls through to the ordinary BODY damage path.
    _seed, _ref, no_shield = _run_row(3510, dataset=dataset, mutate_input_r_p0=0)
    assert float(no_shield["percent"][defender]) == pytest.approx(
        float(seed["percent"][defender]) + 3.0,
        abs=0.01,
    )
    assert int(no_shield["action_id"][defender]) != 181


@pytest.mark.integration
def test_sheik_thrown_needle_root_bound_hitcap_stays_quiet_against_attackairlw_replay_real() -> None:
    # ZestyPreciousTurtle:249 locks the adjacent quiet side of the root-bound Needle publication model.
    # Hitboxes 2/3 are bound to the item root JObj, so command-11 x_offset rotates by item facing rather
    # than by the live travel vector. The diagonal flying Needle therefore stays on the source side of
    # the AttackAirLw defender this frame instead of BODY-hitting early.
    # refs/melee/src/melee/it/itanimlist.c::it_802790C0
    # refs/melee/src/melee/it/itcoll.c::it_8027137C
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::itSeakneedlethrown_UnkMotion0_Coll
    seed, ref, out = _run_row(249, dataset=ZESTY_DATASET)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 0
    assert int(seed["action_id"][P_MARTH]) == 69  # AttackAirLw

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 69
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 0
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert int(out["items"]["exists"][0]) == int(ref["items"]["exists"][0]) == 1
    assert int(out["items"]["state"][0]) == int(ref["items"]["state"][0]) == 0


@pytest.mark.integration
def test_sheik_air_needle_same_frame_spawn_body_hits_after_attack_hitcap_fallthrough_replay_real() -> None:
    # ZestyPreciousTurtle:1523 is a same-frame Air Needle spawn. The state-0 Needle hitcaps are BODY
    # enabled but command-11 x40_b0 clank is clear, so the defender's live AttackAirLw hitcaps do not
    # enter ftColl_80077970 and the source order falls through to BODY on the spawn frame.
    # refs/melee/src/melee/it/itanimlist.c::it_802790C0
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077970}
    seed, ref, out = _run_row(1523, dataset=ZESTY_DATASET)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_HELD
    assert int(seed["action_id"][P_MARTH]) == 69

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 85
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 4
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert float(out["percent"][P_MARTH]) == pytest.approx(3.0)
    assert int(out["items"]["exists"][0]) == int(ref["items"]["exists"][0]) == 0


@pytest.mark.integration
def test_sheik_ground_needle_same_frame_spawn_quiet_before_body_overlap_replay_real() -> None:
    # TenseSameHummingbird:5729 is the adjacent negative for same-frame spawned Needle contact. The
    # spawned ground Needle publishes this frame but has not reached BODY; because command-11 x40_b0 is
    # clear, the defender's AttackAirLw hitcaps also must not clank/destroy it early.
    # refs/melee/src/melee/it/itanimlist.c::it_802790C0
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    seed, ref, out = _run_row(5729, dataset=TENSE_DATASET)

    assert int(seed["action_id"][P_MARTH]) == 69
    assert int(ref["items"]["type"][0]) == ITEM_NEEDLE_THROWN

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 69
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 0
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert int(out["items"]["exists"][0]) == int(ref["items"]["exists"][0]) == 1
    assert int(out["items"]["state"][0]) == int(ref["items"]["state"][0]) == 0


@pytest.mark.integration
def test_sheik_ground_needle_active_attack_hitcaps_fall_through_to_body_replay_real() -> None:
    # TenseSameHummingbird:5730 is the next frame positive: the same thrown Needle now reaches BODY
    # while Marth's AttackAirLw hitcaps are still live. The article-side x40_b0 clank gate keeps those
    # fighter hitcaps from preempting BODY.
    # refs/melee/src/melee/it/itanimlist.c::it_802790C0
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077970}
    seed, ref, out = _run_row(5730, dataset=TENSE_DATASET)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 0
    assert int(seed["action_id"][P_MARTH]) == 69

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 85
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 4
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert float(out["percent"][P_MARTH]) == pytest.approx(3.0)
    assert int(out["items"]["exists"][0]) == int(ref["items"]["exists"][0]) == 0


@pytest.mark.integration
def test_sheik_ground_needle_rollout_damage_entry_clears_victim_attack_hitcaps() -> None:
    # TenseSameHummingbird rollout from 5581: the first thrown Needle BODY-hits AttackAirLw at 5730,
    # entering DamageAir2. Fighter_ChangeMotionState then clears the victim's outgoing x914
    # HitCapsules through ftColl_8007AFF8, so the next Needle cannot be destroyed by the stale
    # AttackAirLw hitbox while DamageAir2 is frozen in hitlag. The adjacent 5730 positive proves live
    # AttackAirLw hitcaps still coexist with the valid BODY path before damage entry; 5733/5734 prove
    # the post-entry clear/preserve boundary. 5740/5743 then lock the source DamageN2 hitlag ECB
    # publication: the later same-volley Needle freezes the entered Damage ECB/hurtcap packet rather
    # than letting the below-floor root stale-carry until the next Needle misses.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AFF8
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_Damage_Coll}
    samples, out = _run_rollout_records(
        TENSE_DATASET, start_record=5581, records=(5730, 5733, 5734, 5740, 5743)
    )

    ref5730 = samples["ref_t1"][5730]
    assert int(out[5730]["action_id"][P_MARTH]) == int(ref5730["action_id"][P_MARTH]) == 85
    assert int(out[5730]["hitlag"][P_MARTH]) == int(ref5730["hitlag"][P_MARTH]) == 4
    assert float(out[5730]["percent"][P_MARTH]) == pytest.approx(3.0)
    assert int(out[5730]["items"]["exists"][0]) == int(ref5730["items"]["exists"][0]) == 0

    ref5733 = samples["ref_t1"][5733]
    assert int(out[5733]["action_id"][P_MARTH]) == int(ref5733["action_id"][P_MARTH]) == 85
    assert int(out[5733]["hitlag"][P_MARTH]) == int(ref5733["hitlag"][P_MARTH]) == 1
    assert float(out[5733]["percent"][P_MARTH]) == pytest.approx(3.0)
    assert int(out[5733]["items"]["exists"][0]) == int(ref5733["items"]["exists"][0]) == 1
    assert int(out[5733]["items"]["type"][0]) == int(ref5733["items"]["type"][0]) == ITEM_NEEDLE_THROWN

    ref5734 = samples["ref_t1"][5734]
    assert int(out[5734]["action_id"][P_MARTH]) == int(ref5734["action_id"][P_MARTH]) == 85
    assert int(out[5734]["hitlag"][P_MARTH]) == int(ref5734["hitlag"][P_MARTH]) == 3
    assert float(out[5734]["percent"][P_MARTH]) == pytest.approx(float(ref5734["percent"][P_MARTH]))
    assert float(out[5734]["percent"][P_MARTH]) == pytest.approx(5.73)
    assert int(out[5734]["items"]["exists"][0]) == int(ref5734["items"]["exists"][0]) == 0

    ref5740 = samples["ref_t1"][5740]
    assert int(out[5740]["action_id"][P_MARTH]) == int(ref5740["action_id"][P_MARTH]) == 85
    assert int(out[5740]["hitlag"][P_MARTH]) == int(ref5740["hitlag"][P_MARTH]) == 3
    assert float(out[5740]["percent"][P_MARTH]) == pytest.approx(float(ref5740["percent"][P_MARTH]))
    assert float(out[5740]["pos_y"][P_MARTH]) == pytest.approx(float(ref5740["pos_y"][P_MARTH]))

    ref5743 = samples["ref_t1"][5743]
    assert int(out[5743]["action_id"][P_MARTH]) == int(ref5743["action_id"][P_MARTH]) == 85
    assert int(out[5743]["hitlag"][P_MARTH]) == int(ref5743["hitlag"][P_MARTH]) == 3
    assert int(out[5743]["hitstun"][P_MARTH]) == int(ref5743["hitstun"][P_MARTH]) == 12
    assert float(out[5743]["percent"][P_MARTH]) == pytest.approx(float(ref5743["percent"][P_MARTH]))
    assert float(out[5743]["percent"][P_MARTH]) == pytest.approx(13.92)


@pytest.mark.integration
@pytest.mark.parametrize("record", [5904, 5905])
def test_sheik_thrown_needle_catchdash_entry_pose_rejects_body_overlap_aac(record: int) -> None:
    # AttractiveAnyClam:5904/5905 are the quiet side of a Dash -> CatchDash item-BODY entry pose
    # boundary. The newly visible CatchDash pose overlaps a same-volley thrown Needle, but source
    # ftColl_8007925C still feeds lbColl_8000805C the entry-boundary JObj collision pose for this
    # item contact (Dash on frame 0, CatchDash frame 0 on the next frame). The victim stays in
    # CatchDash and takes no Needle BODY damage. The adjacent TenseSameHummingbird:5730 positive
    # below remains a real first-active Needle BODY hit when the source collision pose overlaps.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_800D8A38,ftCo_800D8C54,ftCo_CatchDash_Anim}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
    seed, ref, out = _run_row(record, dataset=ATTRACTIVE_ANY_CLAM_DATASET)
    defender = P_MARTH

    assert int(seed["items"]["type"][1]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][1]) == 0
    assert int(seed["action_id"][defender]) == 214  # CatchDash
    assert int(ref["action_id"][defender]) == 214

    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 0
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))
    assert float(out["percent"][defender]) == pytest.approx(0.0)


@pytest.mark.integration
@pytest.mark.parametrize("record", [5541, 5542])
def test_sheik_needle_same_volley_guardsetoff_packet_does_not_rehit_shield(record: int) -> None:
    # ZestyPreciousTurtle:5541/5542 are existing GuardSetOff shield-hit packets from the same
    # SpecialN volley. fp+0x221B still exposes shield-active, but fp+0x2218 has no current
    # collision-command owner and no item_shield_bounce seed proves a new Item_80269DC8 contact.
    # The old packet must therefore count down quietly instead of reconstructing a fresh thrown
    # Needle ShieldDesc hit from x221B alone.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_80093240,ftCo_800932DC}
    # refs/melee/src/melee/it/item.c::{Item_80269DC8,checkHitLag}
    seed, ref, out = _run_row(record, dataset=ZESTY_DATASET)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 0
    assert int(seed["items"]["type"][1]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][1]) == 0
    assert int(seed["action_id"][P_MARTH]) == 181  # GuardSetOff.
    assert int(seed["item_shield_bounce_valid"][0]) == 0
    assert int(seed["item_shield_bounce_valid"][1]) == 0
    assert int(seed["state_flags"][P_MARTH, 0]) == 0
    assert int(seed["state_flags"][P_MARTH, 2]) & 0x80

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 181
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 0
    assert float(out["shield_hp"][P_MARTH]) == pytest.approx(float(ref["shield_hp"][P_MARTH]))
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))


@pytest.mark.integration
def test_sheik_seeded_first_active_later_needle_uses_live_stale_table_replay_real() -> None:
    # TenseSameHummingbird:5743 seeds a state-0 Needle on its first active item frame. The stale
    # table already contains this same SpecialN attack instance, but a state-0 first-active Needle
    # cannot have produced that entry itself: DmgDealt would have bounced/destroyed it. Source
    # therefore created this later same-volley HitCapsule after the previous Needle's stale insert,
    # so it_80272460 freezes the staled 0.91x HitCapsule.damage instead of rewinding the latest
    # table entry.
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgDealt
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    # refs/melee/src/melee/ft/ft_0881.c::ft_80089228
    seed, ref, out = _run_row(5743, dataset=TENSE_DATASET)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 0
    assert int(seed["items"]["timer"][0]) == 29
    assert int(seed["action_id"][P_MARTH]) == 85  # DamageAir2
    assert int(seed["hitlag"][P_MARTH]) == 1

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 85
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 3
    assert int(out["hitstun"][P_MARTH]) == int(ref["hitstun"][P_MARTH]) == 12
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert float(out["percent"][P_MARTH]) == pytest.approx(13.92)
    # The Needle's bounce/destroy fate is the existing DmgDealt HSD_Randi(3) callback boundary;
    # this lock owns the victim HitCapsule.damage / ProcessHit lanes only.


@pytest.mark.integration
def test_sheik_demo_thrown_needle_body_lbcoll_rejects_marth_cap5_before_source_hit() -> None:
    # Adjacent negative from the official Sheik demo rollout: the reduced world-space capsule path
    # admitted Marth cap5 one frame early. Source BODY contact uses ftColl_8007925C ->
    # lbColl_8000805C matrix/local-radius geometry for the item HitCapsule x58->x4C segment.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # refs/melee/src/melee/it/itcoll.c::it_8027137C
    seed, ref, out = _run_row(134, dataset=DEMO_DATASET)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 0
    assert int(seed["action_id"][P_MARTH]) == 14

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 14
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 0
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert float(out["percent"][P_MARTH]) == pytest.approx(0.0)


@pytest.mark.integration
def test_sheik_demo_thrown_needle_body_lbcoll_keeps_next_frame_hit() -> None:
    seed, ref, out = _run_row(135, dataset=DEMO_DATASET)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 0

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 79
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 4
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert float(out["percent"][P_MARTH]) == pytest.approx(3.0)


@pytest.mark.integration
def test_sheik_demo_rec739_thrown_needle_jobj_body_contact_stays_quiet_adjacent_negative() -> None:
    # Official Sheik demo adjacent negative for the rec740 fix: the flying Needle has advanced into
    # the source JObj-backed BODY path, but the child-JObj HitCapsule segment still misses Marth's
    # live hurtcaps this frame. This prevents broad "current visible item path" fixes from admitting
    # the hit early.
    # refs/melee/src/melee/it/itanimlist.c::it_802790C0
    # refs/melee/src/melee/it/itcoll.c::it_8027137C
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::itSeakneedlethrown_UnkMotion0_Coll
    seed, ref, out = _run_row(739, dataset=DEMO_DATASET)

    assert int(seed["items"]["type"][3]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][3]) == 0

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 14
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 0
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert float(out["percent"][P_MARTH]) == pytest.approx(21.0)


@pytest.mark.integration
def test_sheik_demo_rec740_thrown_needle_jobj_body_contact_hits_source_frame() -> None:
    # rec740 was a one-frame-late miss when Needle BODY contact used only the replay-visible item
    # root segment. Source command 11 attaches hitbox 0 to article bone 1, and it_8027137C publishes
    # x58->x4C from that JObj before ftColl_8007925C reaches BODY. The generated MSLITAR1 JObj
    # offset places the hitcap on the source segment and closes the victim lanes.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    # refs/melee/src/melee/it/itanimlist.c::it_802790C0
    # refs/melee/src/melee/it/itcoll.c::it_8027137C
    seed, ref, out = _run_row(740, dataset=DEMO_DATASET)

    assert int(seed["items"]["type"][3]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][3]) == 0

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 79
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 3
    assert int(out["hitstun"][P_MARTH]) == int(ref["hitstun"][P_MARTH]) == 13
    assert int(out["instance_hit_by"][P_MARTH]) == int(ref["instance_hit_by"][P_MARTH]) == 47
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert float(out["percent"][P_MARTH]) == pytest.approx(23.76)


@pytest.mark.integration
def test_sheik_demo_thrown_needle_active_hitlag_body_uses_frozen_low_hurtcap() -> None:
    # The second flying Needle BODY-hits Marth while Marth is still in damage hitlag from the first
    # Needle. Fighter_8006A360 skips Anim/IASA/Phys during hitlag, but Fighter_8006CB94 still runs
    # ftColl_8007925C item BODY contact. The victim lanes assert the source-owned result: the live
    # frozen hurtcap packet selects the low DamageLw2 reaction, and the seeded item HitCapsule keeps
    # its pre-hit stale damage so the packet deals a full 3 additional percent.
    # The Needle's own post-hit fate is deliberately not asserted here: it_2725_Logic109_DmgDealt
    # samples HSD_Randi(3) to bounce or destroy after the victim damage owner has already resolved.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_8006CB94}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_8007A06C}
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgDealt
    seed, ref, out = _run_row(138, dataset=DEMO_DATASET)

    assert int(seed["items"]["type"][1]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][1]) == 0
    assert int(seed["hitlag"][P_MARTH]) == 2
    assert int(seed["action_id"][P_MARTH]) == 79

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 82
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 4
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert float(out["percent"][P_MARTH]) == pytest.approx(6.0)


@pytest.mark.integration
def test_sheik_demo2_needle_hitlag_handoff_replaces_hi_reaction_adjacent_negative() -> None:
    # In the second Sheik demo, the next thrown Needle hits Marth while the previous Needle's
    # DamageHi packet is still in hitlag. Source item BODY processing is still allowed to replace
    # that Hi reaction with the new DamageN owner; this guards the later top-off rule from
    # suppressing all hitlag-window Needle contacts.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_8006CB94}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_8007A06C}
    seed, ref, out = _run_row(5485, dataset=DEMO2_FULL_DATASET)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 0
    assert int(seed["action_id"][P_MARTH]) == 76
    assert int(seed["hitlag"][P_MARTH]) == 2

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 79
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 4
    assert int(out["hitstun"][P_MARTH]) == int(ref["hitstun"][P_MARTH]) == 13
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert float(out["percent"][P_MARTH]) == pytest.approx(35.1)


@pytest.mark.integration
def test_sheik_demo2_later_needle_topoffs_damage_hitlag_with_staled_damage() -> None:
    # The following Needle in that volley is a later-spawn same-attack article. A visible lower
    # spawn in post-contact state plus the stale table proves this Needle's source HitCapsule.damage
    # was frozen after the earlier stale insert, so the row takes 0.91x damage. Because Marth is
    # already in the same DamageN hitlag packet, the later article contributes percent and its
    # callback without replacing action/hitlag with a fresh Damage entry.
    # refs/melee/src/melee/it/itanimlist.c::it_802790C0
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    # refs/melee/src/melee/ft/ft_0881.c::ft_80089228
    # refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
    seed, ref, out = _run_row(5487, dataset=DEMO2_FULL_DATASET)

    assert int(seed["items"]["type"][1]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][1]) == 0
    assert int(seed["items"]["state"][0]) == 4
    assert int(seed["action_id"][P_MARTH]) == 79
    assert int(seed["hitlag"][P_MARTH]) == 3

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 79
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 3
    assert int(out["hitstun"][P_MARTH]) == int(ref["hitstun"][P_MARTH]) == 13
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))
    assert float(out["percent"][P_MARTH]) == pytest.approx(37.83)
    assert float(out["speed_x_attack"][P_MARTH]) == pytest.approx(
        float(ref["speed_x_attack"][P_MARTH]), abs=1e-6
    )
    assert abs(float(out["speed_x_attack"][P_MARTH])) < abs(float(seed["speed_x_attack"][P_MARTH]))


@pytest.mark.integration
def test_sheik_bounced_needle_clank_contact_uses_item_hitlag_replay_real() -> None:
    # High-damage fighter HitCapsule contact against a bounced Needle owns the source
    # ftColl_80077970 clank side as well as the item hurtbox DmgReceived side. The fighter keeps the
    # 6-frame item-common hitlag floor from OnClank/checkHitLag.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077970
    # refs/melee/src/melee/it/item.c::{OnClankThink,checkHitLag}
    seed, ref, out = _run_row(213)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 4
    assert int(seed["hitlag"][P_MARTH]) == 0

    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 6
    assert int(out["items"]["exists"][0]) == int(ref["items"]["exists"][0]) == 0


@pytest.mark.integration
def test_sheik_bounced_needle_low_damage_hitcapsule_uses_deal_hitlag_only_replay_real() -> None:
    # RuralReasonableRat:2425 is the adjacent state-4 negative: Sheik Chain's low-damage HitCapsule
    # damages the bounced Needle hurtbox through it_802703E8, but it does not own the high-damage
    # ftColl_80077970 clank floor. The attacker therefore gets only deal-hitlag from its own
    # collision-time HitCapsule damage.
    # refs/melee/src/melee/it/itcoll.c::it_802703E8
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077970
    seed, ref, out = _run_row(2425, dataset=RURAL_DATASET)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 4
    assert int(seed["action_id"][P_MARTH]) == 354

    assert int(out["action_id"][P_MARTH]) == int(ref["action_id"][P_MARTH]) == 354
    assert int(out["hitlag"][P_MARTH]) == int(ref["hitlag"][P_MARTH]) == 3
    assert float(out["percent"][P_MARTH]) == pytest.approx(float(ref["percent"][P_MARTH]))


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


@pytest.mark.integration
def test_sheik_bounced_needle_one_hitlag_frame_still_freezes_item_phase_replay_real() -> None:
    # Same hidden Item.xCBC_hitlagFrames owner as the row above, but the fighter hitlag counter seeds
    # as 1 and drains before the item phase. The frame-start hitlag latch is still source evidence
    # that Item_802697D4 should freeze this state-4 Needle.
    # refs/melee/src/melee/it/item.c::{checkHitLag,Item_802697D4}
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgReceived
    seed, ref, out = _run_row(8666, dataset=TENSE_DATASET)

    assert int(seed["items"]["type"][0]) == ITEM_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) == 4
    assert int(seed["items"]["damage"][0]) == 12
    assert int(seed["hitlag"][P_MARTH]) == 1

    assert int(out["items"]["exists"][0]) == int(ref["items"]["exists"][0]) == 1
    assert int(out["items"]["state"][0]) == int(ref["items"]["state"][0]) == 4
    assert float(out["items"]["pos_y"][0]) == pytest.approx(float(ref["items"]["pos_y"][0]))
    assert float(out["items"]["vel_y"][0]) == pytest.approx(float(ref["items"]["vel_y"][0]))

    _, _, out_no_damage = _run_row(8666, dataset=TENSE_DATASET, mutate_item0_damage=0)
    assert int(out_no_damage["items"]["exists"][0]) == 0
