from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _one_step_out_ref(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    assert int(row.shape[0]) == 1

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = (
            np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, seed_stride)
            .copy()
        )
        prev_input_bytes = (
            np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, input_stride)
            .copy()
        )
        input_bytes = (
            np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, input_stride)
            .copy()
        )
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        return row["seed_t"][0], out, row["ref_t1"][0]
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    player: int
    seed_action: int
    ref_action: int
    expect_item_clear: bool
    note: str


_AGG = "replays/validation/aggregate_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=f"{_AGG}/DistinctCaringCobra.slpz",
            record=7619,
            player=0,
            seed_action=20,  # Dash
            ref_action=75,  # DamageHi1
            expect_item_clear=True,
            note="late Dash grounded laser segment admits BODY hit",
        ),
        _Case(
            dataset_rel=f"{_AGG}/BlondHardHippopotamus.slpz",
            record=5148,
            player=0,
            seed_action=20,  # Dash, then Turn before item collision
            ref_action=18,  # Turn
            expect_item_clear=True,
            note="Dash-to-Turn grounded laser segment clears item without damage entry",
        ),
        _Case(
            dataset_rel="replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            record=7215,
            player=0,
            seed_action=20,  # Dash, then Turn before item BODY collision
            ref_action=78,  # DamageN1
            expect_item_clear=True,
            note="Dash-to-Turn Falco laser uses established grounded BODY travel lane",
        ),
        _Case(
            dataset_rel=f"{_AGG}/HilariousVillainousGiraffe.slpz",
            record=1024,
            player=1,
            seed_action=56,  # AttackHi3
            ref_action=81,  # DamageLw3
            expect_item_clear=True,
            note="AttackHi3 grounded laser segment admits BODY hit",
        ),
        _Case(
            dataset_rel="replays/validation/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            record=3105,
            player=1,
            seed_action=43,  # LandingFallSpecial
            ref_action=78,  # DamageN1
            expect_item_clear=True,
            note="LandingFallSpecial exact flattened-Z laser BODY admits hit",
        ),
        _Case(
            dataset_rel="replays/validation/"
            "yoshis_story_recent/CheeryNumbMonkey.slpz",
            record=184,
            player=0,
            seed_action=41,  # LandingAirN
            ref_action=75,  # DamageHi1
            expect_item_clear=True,
            note="LandingAirN high-cap laser uses exact lbColl matrix-radius lane",
        ),
        _Case(
            dataset_rel="replays/validation/"
            "yoshis_story_recent/CheeryNumbMonkey.slpz",
            record=250,
            player=0,
            seed_action=60,  # AttackS4
            ref_action=75,  # DamageHi1
            expect_item_clear=True,
            note="AttackS4 high-cap laser uses exact lbColl matrix-radius lane",
        ),
        _Case(
            dataset_rel="replays/validation/"
            "yoshis_story_recent/CheeryNumbMonkey.slpz",
            record=4719,
            player=0,
            seed_action=72,  # LandingAirB
            ref_action=75,  # DamageHi1
            expect_item_clear=True,
            note="LandingAirB high-cap laser uses exact lbColl matrix-radius lane",
        ),
        _Case(
            dataset_rel="replays/validation/"
            "fountain_of_dreams_recent/ElatedWearyTermite.slpz",
            record=4216,
            player=0,
            seed_action=72,  # LandingAirB
            ref_action=75,  # DamageHi1
            expect_item_clear=True,
            note="FoD LandingAirB high-cap laser uses exact lbColl matrix-radius lane",
        ),
        _Case(
            dataset_rel=f"{_AGG}/DistinctCaringCobra.slpz",
            record=7618,
            player=0,
            seed_action=20,  # adjacent Dash no-hit frame
            ref_action=20,
            expect_item_clear=False,
            note="adjacent late Dash negative keeps laser alive",
        ),
        _Case(
            dataset_rel=f"{_AGG}/BlondHardHippopotamus.slpz",
            record=5147,
            player=0,
            seed_action=20,  # adjacent pre-clear Dash frame
            ref_action=20,
            expect_item_clear=False,
            note="adjacent Dash-to-Turn negative keeps laser alive",
        ),
        _Case(
            dataset_rel="replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            record=7214,
            player=0,
            seed_action=20,  # adjacent pre-Turn Dash frame
            ref_action=20,
            expect_item_clear=False,
            note="adjacent Dash frame keeps Falco laser alive before grounded BODY contact",
        ),
        _Case(
            dataset_rel="replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
            record=4136,
            player=0,
            seed_action=20,  # adjacent high-cap Dash-to-Turn no-hit
            ref_action=18,
            expect_item_clear=False,
            note="high-cap Dash-to-Turn Falco laser stable-scale candidate stays rejected",
        ),
        _Case(
            dataset_rel="replays/validation/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
            record=4142,
            player=0,
            seed_action=20,  # fresh L edge still takes Dash_CheckInput Turn before guard.
            ref_action=18,
            expect_item_clear=False,
            note="high-cap Dash-to-Turn with shield edge keeps laser alive",
        ),
        _Case(
            dataset_rel=f"{_AGG}/HilariousVillainousGiraffe.slpz",
            record=1023,
            player=1,
            seed_action=56,  # adjacent AttackHi3 no-hit frame
            ref_action=56,
            expect_item_clear=False,
            note="adjacent AttackHi3 negative keeps laser alive",
        ),
        _Case(
            dataset_rel="replays/validation/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            record=3104,
            player=1,
            seed_action=43,  # adjacent LandingFallSpecial no-hit frame
            ref_action=43,
            expect_item_clear=False,
            note="adjacent LandingFallSpecial exact flattened-Z negative keeps laser alive",
        ),
        _Case(
            dataset_rel="replays/validation/"
            "pokemon_stadium_recent/CornyDelayedOkapi.slpz",
            record=112,
            player=0,
            seed_action=20,  # Dash
            ref_action=20,
            expect_item_clear=False,
            note="tiny exact lbColl overlap routes to grounded phantom without BODY damage",
        ),
    ],
    ids=lambda case: case.note,
)
def test_grounded_laser_body_segment_rows(case: _Case) -> None:
    # Replay-real locks for the grounded item BODY segment owner in src/items.c:
    # - itFoxlaser_UnkMotion1_Phys snapshots previous projectile position.
    # - it_8029C4D4 dispatches fighter collision over the previous-to-current projectile segment.
    # - it_80272460 applies item-vs-fighter BODY damage/despawn once contact is accepted.
    # - ftColl_80077C60 consumes lbColl_80006E58's matrix/local coll_distance to split full BODY
    #   damage from phantom/tip-log overlap.
    # - First-frame Turn/Dash handoff negatives are locked to source-probed item HitCapsule x58/x4C
    #   scale phase, not to replay ids.
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077C60
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    seed, out, ref = _one_step_out_ref(dataset_path, case.record)
    p = case.player
    assert int(seed["action_id"][p]) == case.seed_action, case.note
    assert int(ref["action_id"][p]) == case.ref_action, case.note

    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "hitlag",
        "hitstun",
        "instance_hit_by",
        "instance_id",
        "last_hit_by",
    ):
        assert int(out[field][p]) == int(ref[field][p]), f"{case.note}: {field}"

    out_live_lasers = [
        (int(item["type"]), int(item["instance_id"]))
        for item in out["items"]
        if int(item["exists"]) and int(item["type"]) in (54, 55)
    ]
    ref_live_lasers = [
        (int(item["type"]), int(item["instance_id"]))
        for item in ref["items"]
        if int(item["exists"]) and int(item["type"]) in (54, 55)
    ]
    assert out_live_lasers == ref_live_lasers, case.note
    if case.expect_item_clear:
        assert not ref_live_lasers, case.note
    else:
        assert ref_live_lasers, case.note
