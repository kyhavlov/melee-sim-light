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
    seed_action: int
    note: str


_BASE = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=f"{_BASE}/QuerulousGrandDinosaur.msl",
            p=0,
            target_record=1616,
            prev_record=1615,
            next_record=1617,
            seed_action=200,  # PassiveStandF
            note="PassiveStandF grounded ft_80084FA8 row keeps replay-real TransN velocity",
        ),
        _Case(
            dataset_rel=f"{_BASE}/AttachedGoodNaturedGuanaco.msl",
            p=0,
            target_record=3265,
            prev_record=3264,
            next_record=3266,
            seed_action=201,  # PassiveStandB
            note="PassiveStandB grounded ft_80084FA8 row keeps replay-real TransN velocity",
        ),
        _Case(
            dataset_rel=f"{_BASE}/GracefulAttachedTurtle.msl",
            p=1,
            target_record=8217,
            prev_record=8216,
            next_record=8218,
            seed_action=257,  # CliffAttackQuick
            note="CliffAttackQuick grounded ft_80084FA8 row keeps replay-real grounded carry",
        ),
    ],
    ids=lambda case: case.note,
)
def test_grounded_ft80084fa8_targets_pm1(case: _Case) -> None:
    # Decomp ownership:
    # - ftCo_PassiveStand_Phys calls ft_80084FA8.
    # - grounded CliffClimb/Attack/Escape quick Phys shares ftCo_CliffClimb_Phys -> ft_80084FA8.
    # - Fighter_procUpdate keeps self_vel.x synchronized from gr_vel while grounded.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Phys
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Phys
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80084FA8,ft_80085030}
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    p = case.p

    target = ds.samples[case.target_record]
    assert int(target["seed_t"]["action_id"][p]) == case.seed_action, case.note
    assert int(target["ref_t1"]["action_id"][p]) == case.seed_action, case.note
    out, ref = _step_one_row(dataset_path, case.target_record)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]), case.note
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]), case.note
    assert abs(float(out["pos_x"][p]) - float(ref["pos_x"][p])) <= 2e-4, case.note
    assert abs(float(out["speed_air_x_self"][p]) - float(ref["speed_air_x_self"][p])) <= 2e-4, case.note
    assert abs(float(out["speed_ground_x_self"][p]) - float(ref["speed_ground_x_self"][p])) <= 2e-4, case.note

    prev = ds.samples[case.prev_record]
    out_prev, ref_prev = _step_one_row(dataset_path, case.prev_record)
    assert int(out_prev["action_id"][p]) == int(ref_prev["action_id"][p]), case.note
    assert int(out_prev["on_ground"][p]) == int(ref_prev["on_ground"][p]), case.note
    assert abs(float(out_prev["speed_air_x_self"][p]) - float(ref_prev["speed_air_x_self"][p])) <= 2e-4, case.note
    assert abs(float(out_prev["speed_ground_x_self"][p]) - float(ref_prev["speed_ground_x_self"][p])) <= 2e-4, case.note

    nxt = ds.samples[case.next_record]
    out_next, ref_next = _step_one_row(dataset_path, case.next_record)
    assert int(out_next["action_id"][p]) == int(ref_next["action_id"][p]), case.note
    assert int(out_next["on_ground"][p]) == int(ref_next["on_ground"][p]), case.note
    assert abs(float(out_next["speed_air_x_self"][p]) - float(ref_next["speed_air_x_self"][p])) <= 2e-4, case.note
    assert abs(float(out_next["speed_ground_x_self"][p]) - float(ref_next["speed_ground_x_self"][p])) <= 2e-4, case.note


@pytest.mark.integration
def test_grounded_ft80084fa8_negative_control_passive_stays_replay_real() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE}/QuerulousGrandDinosaur.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 1107
    p = 0
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]

    # Explicit non-target control: Passive uses ft_80084F3C, not ft_80084FA8.
    assert int(row["seed_t"]["action_id"][p]) == 199  # Passive
    assert int(row["ref_t1"]["action_id"][p]) == 199
    assert int(row["seed_t"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out, ref = _step_one_row(dataset_path, record)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 199
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert abs(float(out["speed_air_x_self"][p]) - float(ref["speed_air_x_self"][p])) <= 1e-6
    assert abs(float(out["speed_ground_x_self"][p]) - float(ref["speed_ground_x_self"][p])) <= 1e-6
