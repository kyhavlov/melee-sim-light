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
    ],
)
def test_illusion_body_hit_rows_match_replay_real(case: _Case) -> None:
    # Replay-real Side-B BODY-hit locks:
    # - Illusion/Phantasm BODY hits persist the article (`itFoxIllusion_Logic14_DmgDealt` returns
    #   false) and still enter generic item hitlag.
    # - BODY hits route into Fighter_ProcessHit, so grounded victims use the same ftCo_8008DCE0
    #   grounded-vs-airborne knockback install as fighter hits.
    # refs/melee/src/melee/it/items/itfoxillusion.c::itFoxIllusion_Logic14_DmgDealt
    # refs/melee/src/melee/it/item.c::{Item_802697D4,checkHitLag}
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
