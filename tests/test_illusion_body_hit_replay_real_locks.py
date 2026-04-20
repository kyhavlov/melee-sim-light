from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
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
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl",
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
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
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
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

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
@pytest.mark.parametrize(
    ("dataset_rel", "record", "slot", "note"),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
            4762,
            0,
            "Fox Illusion state1 BODY-hit article advances into state2 during victim hitlag",
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl",
            4764,
            0,
            "Fox Illusion state2 BODY-hit article expires during victim hitlag",
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl",
            6975,
            0,
            "Falco Phantasm state2 article ticks once when owner exits Side-B in the same step",
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl",
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
        pytest.skip(f"missing local dataset: {dataset_rel}")

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
