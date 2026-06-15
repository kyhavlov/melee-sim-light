from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h.
ACT_TURN = 18
ACT_DASH = 20

# Locked records (2 per dataset) where Turn should not spuriously dash at t+1.
# Regression note: this keeps the previously observed Turn->Dash (18->20) cluster pinned at 0.
# Preconditions:
# - seed_t.action_id == Turn
# - ref_t1.action_id == Turn
# - seed/ref hitlag == 0 and hitstun == 0
# - previous seed action_id matches expected context action (entry context lock)
_CASES: tuple[tuple[str, int, int, int], ...] = (
    (
        "datasets/marth/replays/validation/marth/MetallicUniqueGrouse.msl",
        3721,
        1,
        14,  # Wait -> basic Turn; Marth turn_frames must not seed as zero.
    ),
    (
        "datasets/marth/replays/validation/marth/RipeWealthySeahorse.msl",
        8048,
        1,
        43,  # LandingAirF -> basic Turn; same-direction flick must not UCF-dash.
    ),
    (
        "datasets/marth/replays/validation/marth/ColossalYellowishBison.msl",
        986,
        1,
        233,  # DamageFlyN -> basic Turn; same-direction flick must not UCF-dash.
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
        1414,
        1,
        14,  # Wait -> Turn
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
        5740,
        0,
        20,  # Dash -> Turn
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl",
        305,
        0,
        73,  # LandingAirHi -> Turn
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl",
        4475,
        1,
        42,  # Landing -> Turn
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
        1214,
        0,
        42,  # Landing -> Turn
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
        9173,
        1,
        14,  # Wait -> Turn
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl",
        805,
        0,
        14,  # Wait -> Turn
    ),
    (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl",
        470,
        0,
        221,  # ThrowHi -> Turn
    ),
)


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/characters/marth.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/anims/marth.tracks.bin",
        "data/moves/fox.json",
        "data/moves/falco.json",
        "data/moves/marth.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _row_or_skip(root: Path, dataset_rel: str, record: int) -> tuple[object, np.ndarray, np.ndarray]:
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    if int(samples.shape[0]) <= record:
        pytest.skip(f"dataset too short for regression check: {dataset_rel} rec={record}")
    return ds, samples, samples[record : record + 1]


@pytest.mark.integration
@pytest.mark.parametrize(("dataset_rel", "record", "player", "expected_prev_action"), _CASES)
def test_turn_iasa_no_spurious_dash_when_ref_stays_turn(
    dataset_rel: str, record: int, player: int, expected_prev_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    binding = pytest.importorskip("msl_binding")
    ds, samples, row = _row_or_skip(root, dataset_rel, record)

    assert record > 0, f"record must be > 0 for context lock: {dataset_rel} rec={record}"
    assert int(samples["seed_t"]["action_id"][record - 1, player]) == expected_prev_action

    assert int(row["seed_t"]["action_id"][0, player]) == ACT_TURN
    assert int(row["ref_t1"]["action_id"][0, player]) == ACT_TURN
    if "marth/" in dataset_rel:
        # Source: ftCo_Turn_Enter_Basic copies the per-character turn_frames attr into
        # mv.co.turn.frames_to_turn. Marth must not fall through the old Fox/Falco-only
        # preprocessing LUT and seed a zero countdown, which lets Turn_IASA latch Dash early.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Basic
        assert int(row["seed_t"]["turn_frames_to_turn"][0, player]) > 0
        assert int(row["seed_t"]["turn_x8"][0, player]) == 0
    assert int(row["seed_t"]["hitlag"][0, player]) == 0
    assert int(row["seed_t"]["hitstun"][0, player]) == 0
    assert int(row["ref_t1"]["hitlag"][0, player]) == 0
    assert int(row["ref_t1"]["hitstun"][0, player]) == 0

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
        out_action = int(out["action_id"][player])
        ref_action = int(row["ref_t1"]["action_id"][0, player])
        out_anim = int(out["animation_index"][player])
        ref_anim = int(row["ref_t1"]["animation_index"][0, player])

        assert out_action == ref_action, f"{dataset_rel} rec={record} p={player}"
        assert out_action != ACT_DASH, f"{dataset_rel} rec={record} p={player}"
        assert out_anim == ref_anim, f"{dataset_rel} rec={record} p={player}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_marth_basic_turn_ucf_dashback_uses_temporary_facing_after_positive() -> None:
    # UCF dashback hooks `Interrupt_AS_Turn+0x4C`, the temporary-facing store inside
    # `ftCo_Turn_IASA`. The x-smash test is against `mv.co.turn.facing_after`, not absolute stick
    # magnitude. IPW:2413 is an opposite-facing Marth basic-Turn positive; RWS/CYB above are
    # same-direction negatives.
    # refs/ucf/src/dashback/dashback.cpp
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Turn.s
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    binding = pytest.importorskip("msl_binding")
    ds, _samples, row = _row_or_skip(
        root, "datasets/marth/replays/validation/marth/InternalPowerlessWallaby.msl", 2413
    )
    p = 0
    assert int(row["seed_t"]["action_id"][0, p]) == ACT_TURN
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_DASH
    assert int(row["seed_t"]["turn_has_turned"][0, p]) == 0
    assert int(row["seed_t"]["turn_frames_to_turn"][0, p]) > 0
    assert int(row["seed_t"]["turn_x8"][0, p]) == 0

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride).copy()
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
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
        assert int(out["action_id"][p]) == ACT_DASH
        assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])
    finally:
        binding.destroy(handle)
