from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _one_step_out_ref(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
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


_AGG = "datasets/aggregate_recent/replays/validation/aggregate_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=f"{_AGG}/DistinctCaringCobra.msl",
            record=7619,
            player=0,
            seed_action=20,  # Dash
            ref_action=75,  # DamageHi1
            expect_item_clear=True,
            note="late Dash grounded laser segment admits BODY hit",
        ),
        _Case(
            dataset_rel=f"{_AGG}/BlondHardHippopotamus.msl",
            record=5148,
            player=0,
            seed_action=20,  # Dash, then Turn before item collision
            ref_action=18,  # Turn
            expect_item_clear=True,
            note="Dash-to-Turn grounded laser segment clears item without damage entry",
        ),
        _Case(
            dataset_rel=f"{_AGG}/HilariousVillainousGiraffe.msl",
            record=1024,
            player=1,
            seed_action=56,  # AttackHi3
            ref_action=81,  # DamageLw3
            expect_item_clear=True,
            note="AttackHi3 grounded laser segment admits BODY hit",
        ),
        _Case(
            dataset_rel=f"{_AGG}/DistinctCaringCobra.msl",
            record=7618,
            player=0,
            seed_action=20,  # adjacent Dash no-hit frame
            ref_action=20,
            expect_item_clear=False,
            note="adjacent late Dash negative keeps laser alive",
        ),
        _Case(
            dataset_rel=f"{_AGG}/BlondHardHippopotamus.msl",
            record=5147,
            player=0,
            seed_action=20,  # adjacent pre-clear Dash frame
            ref_action=20,
            expect_item_clear=False,
            note="adjacent Dash-to-Turn negative keeps laser alive",
        ),
        _Case(
            dataset_rel=f"{_AGG}/HilariousVillainousGiraffe.msl",
            record=1023,
            player=1,
            seed_action=56,  # adjacent AttackHi3 no-hit frame
            ref_action=56,
            expect_item_clear=False,
            note="adjacent AttackHi3 negative keeps laser alive",
        ),
    ],
    ids=lambda case: case.note,
)
def test_grounded_laser_body_segment_rows(case: _Case) -> None:
    # Replay-real locks for the grounded item BODY segment owner in src/items.c:
    # - itFoxlaser_UnkMotion1_Phys snapshots previous projectile position.
    # - it_8029C4D4 dispatches fighter collision over the previous-to-current projectile segment.
    # - it_80272460 applies item-vs-fighter BODY damage/despawn once contact is accepted.
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

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
