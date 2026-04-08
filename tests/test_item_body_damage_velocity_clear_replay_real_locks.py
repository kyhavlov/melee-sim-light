from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void]:
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
        ref = row["ref_t1"][0]
        return out, ref
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    p: int
    target_record: int
    prev_record: int
    next_record: int
    negative_record: int
    seed_action: int
    ref_action: int
    negative_action: int
    note: str


_BASE = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=f"{_BASE}/QuerulousGrandDinosaur.msl",
            p=1,
            target_record=10113,
            prev_record=10112,
            next_record=10114,
            negative_record=10111,
            seed_action=356,  # SpecialAirHi
            ref_action=84,  # DamageAir1
            negative_action=356,
            note="item BODY hit clears carried self X on SpecialAirHi -> DamageAir1 entry",
        ),
        _Case(
            dataset_rel=f"{_BASE}/AttachedGoodNaturedGuanaco.msl",
            p=0,
            target_record=429,
            prev_record=428,
            next_record=430,
            negative_record=427,
            seed_action=345,  # SpecialAirNLoop
            ref_action=75,  # DamageHi1
            negative_action=345,
            note="item BODY hit clears carried self X/Y on SpecialAirNLoop -> DamageHi1 entry",
        ),
    ],
    ids=lambda case: case.note,
)
def test_item_body_hit_damage_entry_velocity_clear_targets_pm1_and_negative(case: _Case) -> None:
    # Decomp ownership:
    # - Fighter_ProcessHit item/fighter BODY-hit routes into ftCo_8008DCE0.
    # - ftCo_8008DCE0 clears fp->self_vel.{x,y,z} and fp->gr_vel at block_28 before selecting the
    #   destination damage state.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))

    target = ds.samples[case.target_record]
    p = case.p
    assert int(target["seed_t"]["action_id"][p]) == case.seed_action, case.note
    assert int(target["ref_t1"]["action_id"][p]) == case.ref_action, case.note
    out, ref = _step_one_row(dataset_path, case.target_record)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]), case.note
    assert abs(float(out["speed_air_x_self"][p]) - float(ref["speed_air_x_self"][p])) <= 1e-6, case.note
    assert abs(float(out["speed_y_self"][p]) - float(ref["speed_y_self"][p])) <= 1e-6, case.note

    prev = ds.samples[case.prev_record]
    assert int(prev["seed_t"]["action_id"][p]) == case.seed_action, case.note
    out_prev, ref_prev = _step_one_row(dataset_path, case.prev_record)
    assert int(out_prev["action_id"][p]) == int(ref_prev["action_id"][p]), case.note
    assert int(out_prev["on_ground"][p]) == int(ref_prev["on_ground"][p]), case.note

    nxt = ds.samples[case.next_record]
    assert int(nxt["seed_t"]["action_id"][p]) == case.ref_action, case.note
    out_next, ref_next = _step_one_row(dataset_path, case.next_record)
    assert int(out_next["action_id"][p]) == int(ref_next["action_id"][p]), case.note
    assert abs(float(out_next["speed_air_x_self"][p]) - float(ref_next["speed_air_x_self"][p])) <= 1e-6, case.note
    assert abs(float(out_next["speed_y_self"][p]) - float(ref_next["speed_y_self"][p])) <= 1e-6, case.note

    negative = ds.samples[case.negative_record]
    assert int(negative["seed_t"]["action_id"][p]) == case.negative_action, case.note
    assert int(negative["ref_t1"]["action_id"][p]) == case.negative_action, case.note
    out_neg, ref_neg = _step_one_row(dataset_path, case.negative_record)
    assert int(out_neg["action_id"][p]) == int(ref_neg["action_id"][p]), case.note
    assert int(out_neg["on_ground"][p]) == int(ref_neg["on_ground"][p]), case.note
