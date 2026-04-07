from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


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
    target_seed_action: int
    target_ref_action: int
    prev_record: int
    prev_seed_action: int
    prev_ref_action: int
    next_record: int
    next_seed_action: int
    next_ref_action: int
    negative_record: int
    negative_seed_action: int
    negative_ref_action: int
    note: str


_BASE = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=f"{_BASE}/QuerulousGrandDinosaur.msl",
            p=0,
            target_record=10216,
            target_seed_action=27,  # JumpAerialF
            target_ref_action=42,  # Landing
            prev_record=10215,
            prev_seed_action=27,
            prev_ref_action=27,
            next_record=10217,
            next_seed_action=42,
            next_ref_action=42,
            negative_record=1254,
            negative_seed_action=236,  # EscapeAir stays airborne
            negative_ref_action=236,
            note="JumpAerialF -> Landing root-Y target family",
        ),
        _Case(
            dataset_rel=f"{_BASE}/QuerulousGrandDinosaur.msl",
            p=0,
            target_record=1255,
            target_seed_action=236,  # EscapeAir
            target_ref_action=43,  # LandingFallSpecial
            prev_record=1254,
            prev_seed_action=236,
            prev_ref_action=236,
            next_record=1256,
            next_seed_action=43,
            next_ref_action=43,
            negative_record=10215,
            negative_seed_action=27,  # JumpAerialF stays airborne
            negative_ref_action=27,
            note="EscapeAir -> LandingFallSpecial root-Y target family",
        ),
        _Case(
            dataset_rel=f"{_BASE}/GracefulAttachedTurtle.msl",
            p=1,
            target_record=7702,
            target_seed_action=66,  # AttackAirF
            target_ref_action=71,  # LandingAirF
            prev_record=7701,
            prev_seed_action=66,
            prev_ref_action=66,
            next_record=7703,
            next_seed_action=71,
            next_ref_action=71,
            negative_record=7699,
            negative_seed_action=66,  # earlier airborne AttackAirF control
            negative_ref_action=66,
            note="AttackAirF -> LandingAirF root-Y target family",
        ),
    ],
    ids=lambda case: case.note,
)
def test_landing_entry_root_y_targets_pm1_and_negative_controls(case: _Case) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))

    target = ds.samples[case.target_record]
    assert int(target["seed_t"]["action_id"][case.p]) == case.target_seed_action, case.note
    assert int(target["ref_t1"]["action_id"][case.p]) == case.target_ref_action, case.note
    assert int(target["seed_t"]["on_ground"][case.p]) == 0, case.note
    assert int(target["ref_t1"]["on_ground"][case.p]) == 1, case.note
    out, ref = _step_one_row(dataset_path, case.target_record)
    assert int(out["action_id"][case.p]) == int(ref["action_id"][case.p]), case.note
    assert int(out["on_ground"][case.p]) == int(ref["on_ground"][case.p]) == 1, case.note
    assert abs(float(out["pos_y"][case.p]) - float(ref["pos_y"][case.p])) <= 2e-4, case.note

    prev = ds.samples[case.prev_record]
    assert int(prev["seed_t"]["action_id"][case.p]) == case.prev_seed_action, case.note
    assert int(prev["ref_t1"]["action_id"][case.p]) == case.prev_ref_action, case.note
    out_prev, ref_prev = _step_one_row(dataset_path, case.prev_record)
    assert int(out_prev["action_id"][case.p]) == int(ref_prev["action_id"][case.p]), case.note
    assert int(out_prev["on_ground"][case.p]) == int(ref_prev["on_ground"][case.p]) == 0, case.note

    nxt = ds.samples[case.next_record]
    assert int(nxt["seed_t"]["action_id"][case.p]) == case.next_seed_action, case.note
    assert int(nxt["ref_t1"]["action_id"][case.p]) == case.next_ref_action, case.note
    out_next, ref_next = _step_one_row(dataset_path, case.next_record)
    assert int(out_next["action_id"][case.p]) == int(ref_next["action_id"][case.p]), case.note
    assert int(out_next["on_ground"][case.p]) == int(ref_next["on_ground"][case.p]) == 1, case.note
    assert abs(float(out_next["pos_y"][case.p]) - float(ref_next["pos_y"][case.p])) <= 2e-4, case.note

    negative = ds.samples[case.negative_record]
    assert int(negative["seed_t"]["action_id"][case.p]) == case.negative_seed_action, case.note
    assert int(negative["ref_t1"]["action_id"][case.p]) == case.negative_ref_action, case.note
    assert int(negative["seed_t"]["on_ground"][case.p]) == 0, case.note
    assert int(negative["ref_t1"]["on_ground"][case.p]) == 0, case.note
    out_neg, ref_neg = _step_one_row(dataset_path, case.negative_record)
    assert int(out_neg["action_id"][case.p]) == int(ref_neg["action_id"][case.p]), case.note
    assert int(out_neg["on_ground"][case.p]) == int(ref_neg["on_ground"][case.p]) == 0, case.note


@pytest.mark.integration
def test_landing_family_steady_negative_control_stays_replay_real() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_BASE}/QuerulousGrandDinosaur.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 387
    p = 0
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]

    # Explicit non-target landing-family control:
    # - already in LandingAirF on seed_t and remains there on ref_t1
    # - grounded steady-state row must stay replay-real under the broadened landing root-Y rule
    assert int(row["seed_t"]["action_id"][p]) == 71  # LandingAirF
    assert int(row["ref_t1"]["action_id"][p]) == 71
    assert int(row["seed_t"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out, ref = _step_one_row(dataset_path, record)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 71
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 2e-4
