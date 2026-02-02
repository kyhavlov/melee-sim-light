from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_one_step_action_and_anim(
    *, dataset_path: Path, record: int, player: int, expected_action_id: int, expected_anim_index: int
) -> None:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    view = samples[record : record + 1]
    assert int(view.shape[0]) == 1

    binding = importlib.import_module("msl_binding")
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

        seed_bytes[:] = np.frombuffer(view["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(view["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(view["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action_id = int(out["action_id"][0, player])
        got_anim_index = int(out["animation_index"][0, player])
    finally:
        binding.destroy(handle)

    assert got_action_id == expected_action_id, (
        f"record={record} p={player} expected action_id={expected_action_id}, got {got_action_id}"
    )
    assert got_anim_index == expected_anim_index, (
        f"record={record} p={player} expected animation_index={expected_anim_index}, got {got_anim_index}"
    )


@pytest.mark.integration
def test_locomotion_parity_cluster1_walk_type_update_not_too_aggressive() -> None:
    # Cluster 1 representative:
    # - seed/ref = WalkSlow (15) but previous sim out = WalkMiddle (16)
    # datasets/.../AttachedGoodNaturedGuanaco.msl record 1150 p=1
    root = Path(__file__).resolve().parents[1]
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")

    _run_one_step_action_and_anim(
        dataset_path=dataset_path,
        record=1150,
        player=1,
        expected_action_id=15,  # MSL_ACT_WALK_SLOW
        expected_anim_index=7,  # MSL_SM_WALK_SLOW
    )


@pytest.mark.integration
def test_locomotion_parity_cluster2_turn_does_not_misfire_into_dash() -> None:
    # Cluster 2 representative:
    # - seed/ref = Turn (18) but previous sim out = Dash (20)
    # datasets/.../GracefulAttachedTurtle.msl record 309 p=0
    root = Path(__file__).resolve().parents[1]
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")

    _run_one_step_action_and_anim(
        dataset_path=dataset_path,
        record=309,
        player=0,
        expected_action_id=18,  # MSL_ACT_TURN
        expected_anim_index=10,  # MSL_SM_TURN
    )


@pytest.mark.integration
def test_locomotion_parity_cluster3_run_does_not_misfire_into_runbrake() -> None:
    # Cluster 3 representative:
    # - seed/ref = Run (21) but previous sim out = RunBrake (23)
    # datasets/.../TreasuredBackKangaroo.msl record 7369 p=0
    root = Path(__file__).resolve().parents[1]
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")

    _run_one_step_action_and_anim(
        dataset_path=dataset_path,
        record=7369,
        player=0,
        expected_action_id=21,  # MSL_ACT_RUN
        expected_anim_index=13,  # MSL_SM_RUN
    )


@pytest.mark.integration
def test_locomotion_parity_turn_to_dash_latch_on_turn_complete() -> None:
    # Regression guard for the decomp Turn->Dash latch (mv.co.turn.x8).
    #
    # Representative of the suite regression introduced by an earlier "same-frame only" model:
    # - seed = Turn (18), ref = Dash (20), previous sim out stayed Turn.
    # datasets/.../AttachedGoodNaturedGuanaco.msl record 258 p=1
    root = Path(__file__).resolve().parents[1]
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")

    _run_one_step_action_and_anim(
        dataset_path=dataset_path,
        record=258,
        player=1,
        expected_action_id=20,  # MSL_ACT_DASH
        expected_anim_index=12,  # MSL_SM_DASH
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel", "record", "player"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            99,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            97,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            97,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            311,
            1,
        ),
    ],
)
def test_locomotion_parity_dash_to_run_on_cmdvar0_enable_frame(rel: str, record: int, player: int) -> None:
    # Regression guard for the Dash->Run late IASA chain (cmd_var[0] gate).
    #
    # Decomp:
    # - Dash command script sets fp->cmd_vars[0], enabling the late IASA chain in ftCo_Dash_IASA
    #   that can enter Run via fn_800CA5F0. The exact enable frame is data-driven and extracted
    #   into data/moves/*.json.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA5F0
    #
    # Representative records:
    # - seed = Dash (20) near Dash anim end, ref = Run (21) at t+1
    root = Path(__file__).resolve().parents[1]
    for required in [
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]:
        if not (root / required).exists():
            pytest.skip(f"missing local ISO-derived move artifacts: {required}")

    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")

    _run_one_step_action_and_anim(
        dataset_path=dataset_path,
        record=record,
        player=player,
        expected_action_id=21,  # MSL_ACT_RUN
        expected_anim_index=13,  # MSL_SM_RUN
    )
