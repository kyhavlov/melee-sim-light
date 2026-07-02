from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)
from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _run_rollout_window_rows_with_trace,
)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    attacker_port: int
    defender_port: int
    note: str
    player_fields: tuple[str, ...]
    player_float_fields: tuple[str, ...]
    item_fields: tuple[str, ...]
    item_float_fields: tuple[str, ...]


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="replays/validation/aggregate_recent/DistinctCaringCobra.slpz",
            record=4761,
            attacker_port=0,
            defender_port=1,
            note="airborne Fox Illusion BODY uses ghostEffectPos[2]->ghostEffectPos[1] hitcapsule sweep",
            player_fields=("action_id", "action_frame", "animation_index", "hitlag", "hitstun"),
            player_float_fields=("percent", "speed_x_attack", "speed_y_attack"),
            item_fields=("exists", "type", "state", "owner", "instance_id"),
            item_float_fields=("pos_x", "pos_y", "timer"),
        ),
        _Case(
            dataset_rel="replays/validation/aggregate_recent/HilariousVillainousGiraffe.slpz",
            record=528,
            attacker_port=0,
            defender_port=1,
            note="grounded Illusion body hit applies and the article persists on the contact frame",
            player_fields=("action_id", "action_frame", "animation_index", "hitlag", "hitstun"),
            player_float_fields=("percent", "speed_y_attack"),
            item_fields=("exists", "type", "state", "owner", "instance_id"),
            item_float_fields=("pos_x", "pos_y", "timer"),
        ),
        _Case(
            dataset_rel="replays/validation/aggregate_recent/PriceyPartialAlbatross.slpz",
            record=6632,
            attacker_port=1,
            defender_port=0,
            note="grounded Falco Phantasm tumble hit rebounds upward off the floor while the article persists",
            player_fields=("action_id", "action_frame", "animation_index", "hitlag", "hitstun", "on_ground"),
            player_float_fields=("percent", "speed_x_attack", "speed_y_attack"),
            item_fields=("exists", "type", "state", "owner", "instance_id"),
            item_float_fields=("pos_x", "pos_y", "timer"),
        ),
    ],
)
def test_illusion_body_hit_rows_match_replay_real(case: _Case) -> None:
    # Replay-real Side-B BODY-hit locks:
    # - Illusion/Phantasm BODY hits persist the article (`itFoxIllusion_Logic14_DmgDealt` returns
    #   false) without entering generic item hitlag; the callback clears item->xCA8 before the
    #   generic checkHitLag(xCA8) branch can freeze the article.
    # - BODY hits route into Fighter_ProcessHit, so grounded victims use the same ftCo_8008DCE0
    #   grounded-vs-airborne knockback install as fighter hits.
    # refs/melee/src/melee/it/items/itfoxillusion.c::itFoxIllusion_Logic14_DmgDealt
    # refs/melee/src/melee/it/item.c::{OnGiveDamageThink,checkHitLag}
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    _seed, out, ref = _step_one_row(dataset_path, case.record)
    d = case.defender_port

    for field in case.player_fields:
        assert int(out[field][d]) == int(ref[field][d]), f"{case.note}: defender field={field}"

    for field in case.player_float_fields:
        assert float(out[field][d]) == pytest.approx(float(ref[field][d]), abs=1e-6), (
            f"{case.note}: defender field={field}"
        )

    for field in case.item_fields:
        assert int(out["items"][0][field]) == int(ref["items"][0][field]), (
            f"{case.note}: item field={field}"
        )

    if int(ref["items"][0]["exists"]) != 0:
        for field in case.item_float_fields:
            assert float(out["items"][0][field]) == pytest.approx(float(ref["items"][0][field]), abs=1e-6), (
                f"{case.note}: item field={field}"
            )


@pytest.mark.integration
def test_illusion_ghost2_sweep_does_not_hit_adjacent_attackair_entry_row() -> None:
    # Negative control for the ghostEffectPos[2] BODY-sweep lane: the preceding AttackAir entry row
    # keeps the article alive without applying the Illusion hit. The retained lane starts from the
    # previous hitcapsule endpoint but must still require actual swept capsule overlap.
    # refs/melee/src/melee/it/itcoll.c::it_8027137C
    # refs/melee/src/melee/it/items/itfoxillusion.c::itFoxillusion_UnkMotion1_Phys
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/DistinctCaringCobra.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 4760)
    victim = 1
    assert int(seed["items"][0]["type"]) == 56
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim]) == 67
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim]) == 0
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]), abs=1e-6)
    assert int(out["items"][0]["exists"]) == int(ref["items"][0]["exists"]) == 1


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "slot", "note"),
    [
        (
            "replays/validation/aggregate_recent/DistinctCaringCobra.slpz",
            4762,
            0,
            "Fox Illusion state1 BODY-hit article advances into state2 during victim hitlag",
        ),
        (
            "replays/validation/aggregate_recent/DistinctCaringCobra.slpz",
            4764,
            0,
            "Fox Illusion state2 BODY-hit article expires during victim hitlag",
        ),
        (
            "replays/validation/aggregate_recent/HilariousVillainousGiraffe.slpz",
            6975,
            0,
            "Falco Phantasm state2 article ticks once when owner exits Side-B in the same step",
        ),
        (
            "replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.slpz",
            7712,
            0,
            "Falco Phantasm state1 article ticks once when owner exits Side-B in the same step",
        ),
    ],
)
def test_illusion_article_lifetime_rows_tick_through_body_hitlag_and_owner_exit(
    dataset_rel: str, record: int, slot: int, note: str
) -> None:
    # F16c replay-real locks:
    # - BODY-hit article lifetime is not frozen by victim hitlag because dmg_dealt clears xCA8.
    # - ftFx_SpecialS_CheckGhostRemove is evaluated on the item animation callback; when the owner
    #   exits Side-B during this frame, the frame-start Side-B motion still owns the current tick.
    # refs/melee/src/melee/it/items/itfoxillusion.c::{
    #   itFoxIllusion_Logic14_DmgDealt,itFoxillusion_UnkMotion0_Anim,itFoxillusion_UnkMotion2_Anim}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialS_CheckGhostRemove
    # refs/melee/src/melee/it/item.c::{OnGiveDamageThink,checkHitLag}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record)
    item_out = out["items"][slot]
    item_ref = ref["items"][slot]

    for field in ("exists", "type", "state", "owner", "instance_id"):
        assert int(item_out[field]) == int(item_ref[field]), f"{note}: item field={field}"
    assert float(item_out["timer"]) == pytest.approx(float(item_ref["timer"]), abs=1e-6), (
        f"{note}: item timer"
    )

    if record in {4762, 4764}:
        victim = 1
        assert int(seed["hitlag"][victim]) > 0, f"{note}: expected BODY-hit victim hitlag seed"
        assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim]), f"{note}: victim hitlag"
    else:
        owner = int(seed["items"][slot]["owner"])
        assert 347 <= int(seed["action_id"][owner]) <= 352, f"{note}: owner starts in Side-B"
        assert not (347 <= int(ref["action_id"][owner]) <= 352), f"{note}: owner exits Side-B"


@pytest.mark.integration
def test_phantasm_body_hit_consumes_runbrake_source_pose_before_squat_publication() -> None:
    # DSG rec 296 source owner:
    # - the defender starts the frame in just-entered RunBrake action_frame=0, then held-down input
    #   publishes Squat before the live Falco Phantasm article resolves BODY collision;
    # - the item BODY candidate still consumes RunBrake pose frame 0, not a generic frame-start
    #   RunBrake reconstruction and not the freshly published Squat hurtcaps.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::{ftCo_800D5FB0,ftCo_Squat_Enter}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/yoshis_story_recent/DependentSteelGrouse.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 296)
    defender = 1
    slot = 1

    assert int(seed["action_id"][defender]) == 0x0017  # RunBrake
    assert int(seed["action_frame"][defender]) == 0
    assert int(seed["animation_index"][defender]) == 14  # ftCo_SM_RunBrake
    assert int(ref["action_id"][defender]) == 80  # DamageN3
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"defender field={field}"
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)

    for field in ("exists", "type", "state", "owner", "instance_id", "attack_id", "attack_instance"):
        assert int(out["items"][slot][field]) == int(ref["items"][slot][field]), f"item field={field}"
    assert float(out["items"][slot]["timer"]) == pytest.approx(float(ref["items"][slot]["timer"]), abs=1e-6)


@pytest.mark.integration
def test_phantasm_runbrake_source_pose_owner_requires_frame_start_runbrake_action() -> None:
    # Negative control for the DSG source-pose owner: the fallback is not a generic Squat/Phantasm
    # overlap. If the frame-start source action is not RunBrake, the item does not use the stale
    # RunBrake pose lane to fabricate a BODY hit.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/yoshis_story_recent/DependentSteelGrouse.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    def mutate_source_action(seed_t) -> None:
        seed_t["action_id"][0, 1] = 0x0027  # Squat, not RunBrake.

    _seed, ref, out = _run_one_step_row(dataset_path, 296, 1, seed_mutator=mutate_source_action)
    defender = 1
    assert int(ref["action_id"][defender]) == 80
    assert int(out["action_id"][defender]) != 80
    assert int(out["hitlag"][defender]) == 0
    assert int(out["hitstun"][defender]) == 0
    assert float(out["percent"][defender]) == pytest.approx(0.0, abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "expected_action", "expected_percent", "note"),
    [
        (295, 23, 0.0, "adjacent Run -> RunBrake source row does not take the Phantasm hit early"),
        (297, 80, 7.0, "post-hitlag horizon row stays on the existing DamageN3 path"),
    ],
)
def test_phantasm_runbrake_source_pose_adjacent_rows_stay_exact(
    record: int, expected_action: int, expected_percent: float, note: str
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/yoshis_story_recent/DependentSteelGrouse.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, record)
    defender = 1
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"{note}: field={field}"
    assert int(out["action_id"][defender]) == expected_action
    assert float(out["percent"][defender]) == pytest.approx(expected_percent, abs=1e-6)


@pytest.mark.integration
def test_phantasm_runbrake_rollout_hit_uses_guardsetoff_edge_instance_counter_owner(
    tmp_path: Path,
) -> None:
    # Rollout lock for the source instance owner upstream of DSG rec 296:
    # - rec 232 is GuardSetOff_Coll losing a soft-platform edge through ft_80084104 before Fall
    #   entry, which advances the shared plAttack_80037B08 counter once before ftCo_Fall_Enter
    #   writes fp->x2088;
    # - the later Phantasm article spawn copies the owner fighter x2088 through it_8027B070, so
    #   the BODY hit at rec 296 must publish instance_hit_by=68 rather than the stale rollout id.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084104
    # refs/melee/src/melee/it/it_2725.c::it_8027B070
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/yoshis_story_recent/DependentSteelGrouse.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=0,
        window_records=(231, 232, 296),
        rng_damage_fly_roll_gate=True,
        trace_path=tmp_path / "dsg_rng_trace.tsv",
    )

    ref_231, out_231, _ = rows[231]
    ref_232, out_232, _ = rows[232]
    ref_296, out_296, _ = rows[296]

    defender = 1
    assert int(out_231["action_id"][defender]) == int(ref_231["action_id"][defender]) == 181
    assert int(out_231["instance_id"][defender]) == int(ref_231["instance_id"][defender]) == 53

    assert int(out_232["action_id"][defender]) == int(ref_232["action_id"][defender]) == 29
    # The bounded source owner modeled here is the callback-local hidden counter consume before
    # Fall entry. Full global direct instance-id parity for this intermediate row still has another
    # unrelated counter gap, but the stale id must no longer survive into the later article owner.
    assert int(out_232["instance_id"][defender]) == 55
    assert int(out_232["instance_id"][defender]) != int(out_231["instance_id"][defender]) + 1

    assert int(out_296["action_id"][defender]) == int(ref_296["action_id"][defender]) == 80
    assert int(out_296["instance_hit_by"][defender]) == int(ref_296["instance_hit_by"][defender]) == 68
    assert float(out_296["percent"][defender]) == pytest.approx(float(ref_296["percent"][defender]), abs=1e-6)
