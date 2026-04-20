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
        return row["seed_t"][0], out_bytes.view(COMPARE_DTYPE).reshape(-1)[0], row["ref_t1"][0]
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    player: int
    seed_action: int
    ref_hitlag: int
    ref_hitstun: int
    ref_percent_delta: float
    note: str


_AGG = "datasets/aggregate_recent/replays/validation/aggregate_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=f"{_AGG}/TubbyCurlyHerring.msl",
            record=8969,
            player=1,
            seed_action=27,  # Fall
            ref_hitlag=3,
            ref_hitstun=0,
            ref_percent_delta=0.0,
            note="Fall laser phantom writes hitlag/attribution and keeps projectile alive",
        ),
        _Case(
            dataset_rel=f"{_AGG}/ImpassionedAlarmedTarsier.msl",
            record=1580,
            player=0,
            seed_action=345,  # SpecialAirNLoop
            ref_hitlag=3,
            ref_hitstun=0,
            ref_percent_delta=0.0,
            note="SpecialAirNLoop laser phantom writes hitlag/attribution and keeps projectile alive",
        ),
        _Case(
            dataset_rel=f"{_AGG}/TubbyCurlyHerring.msl",
            record=8968,
            player=1,
            seed_action=27,  # adjacent Fall no-contact row
            ref_hitlag=0,
            ref_hitstun=0,
            ref_percent_delta=0.0,
            note="adjacent Fall negative before phantom keeps baseline",
        ),
        _Case(
            dataset_rel=f"{_AGG}/TubbyCurlyHerring.msl",
            record=8970,
            player=1,
            seed_action=27,  # following Fall full BODY hit
            ref_hitlag=3,
            ref_hitstun=9,
            ref_percent_delta=2.94,
            note="following Fall full BODY hit still consumes projectile",
        ),
        _Case(
            dataset_rel=f"{_AGG}/ImpassionedAlarmedTarsier.msl",
            record=1579,
            player=0,
            seed_action=345,  # adjacent SpecialAirNLoop no-contact row
            ref_hitlag=0,
            ref_hitstun=0,
            ref_percent_delta=0.0,
            note="adjacent SpecialAirNLoop negative before phantom keeps baseline",
        ),
        _Case(
            dataset_rel=f"{_AGG}/ImpassionedAlarmedTarsier.msl",
            record=1581,
            player=0,
            seed_action=345,  # following full BODY hit
            ref_hitlag=3,
            ref_hitstun=9,
            ref_percent_delta=2.76,
            note="following SpecialAirNLoop full BODY hit still consumes projectile",
        ),
    ],
    ids=lambda case: case.note,
)
def test_laser_item_phantom_body_rows(case: _Case) -> None:
    # Replay-real locks for the item phantom/tip-log BODY lane:
    # - ftColl_80076ED8 routes small positive `coll_distance < p_ftCommonData->x7A8` overlaps to
    #   hitlag-only phantom handling through `checkTipLog`.
    # - Item hitbox damage is normalized by it_80272460 before the same hitlag calculation.
    # refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,inlineB1,ftColl_80076ED8}
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    seed, out, ref = _one_step_out_ref(dataset_path, case.record)
    p = case.player
    assert int(seed["action_id"][p]) == case.seed_action, case.note
    assert int(ref["hitlag"][p]) == case.ref_hitlag, case.note
    assert int(ref["hitstun"][p]) == case.ref_hitstun, case.note

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
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-6), case.note
    assert float(ref["percent"][p]) - float(seed["percent"][p]) == pytest.approx(
        case.ref_percent_delta, abs=0.02
    )

    out_lasers = [
        (int(item["type"]), int(item["instance_id"]))
        for item in out["items"]
        if int(item["exists"]) and int(item["type"]) in (54, 55)
    ]
    ref_lasers = [
        (int(item["type"]), int(item["instance_id"]))
        for item in ref["items"]
        if int(item["exists"]) and int(item["type"]) in (54, 55)
    ]
    assert out_lasers == ref_lasers, case.note
