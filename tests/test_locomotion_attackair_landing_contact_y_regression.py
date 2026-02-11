from __future__ import annotations

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


def _step_one_row(dataset_path: Path, record: int, p: int) -> tuple[np.void, np.void, np.void]:
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
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
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
        seed = row["seed_t"][0]
        ref = row["ref_t1"][0]
        return seed, out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            304,
            0,
            69,  # AttackAirLw
            74,  # LandingAirLw
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            319,
            1,
            65,  # AttackAirN
            70,  # LandingAirN
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            295,
            0,
            68,  # AttackAirHi
            73,  # LandingAirHi
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            739,
            0,
            68,  # AttackAirHi
            42,  # Landing (auto-cancel)
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            563,
            0,
            67,  # AttackAirB
            72,  # LandingAirB
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            288,
            0,
            68,  # AttackAirHi
            73,  # LandingAirHi
        ),
    ],
)
def test_attackair_landing_rows_keep_contact_y_parity(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record, p)

    # Decomp ownership:
    # - AttackAir collision callback enters LandingAir_* or Landing based on cmd_var[0].
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(seed["on_ground"][p]) == 0
    assert float(seed["pos_y"][p]) < 0.0

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 2e-4


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            7702,
            1,
            66,  # AttackAirF
            71,  # LandingAirF
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            1310,
            1,
            86,  # DamageAir3
            42,  # Landing
        ),
    ],
)
def test_attackair_landing_context_controls_keep_local_family(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record, p)

    # Context controls only: keep local family/action context stable without enforcing error floors.
    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert np.isfinite(float(out["pos_y"][p]))
    assert np.isfinite(float(ref["pos_y"][p]))


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            3188,
            0,
            345,  # SpecialAirNLoop
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            3498,
            1,
            345,  # SpecialAirNLoop
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            6178,
            0,
            345,  # SpecialAirNLoop
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            4570,
            1,
            345,  # SpecialAirNLoop
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            8218,
            0,
            25,  # JumpF
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            6360,
            0,
            26,  # JumpB
            42,  # Landing
        ),
    ],
)
def test_landing_basic_rows_keep_contact_y_parity_for_jump_and_specialairn_family(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record, p)

    # Decomp ownership:
    # - JumpF/B collision callback family routes through ft_80082B1C -> Landing_Enter_Basic.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082B1C
    # - Fox/Falco SpecialAirN* collision callbacks route through AirCatchHit -> ft_80082B1C.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialAirNStart_Coll,ftFx_SpecialAirNLoop_Coll,ftFx_SpecialAirNEnd_Coll
    # }
    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(seed["on_ground"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 42
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 2e-4


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            2678,
            1,
            27,  # JumpAerialF
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            10477,
            1,
            86,  # DamageAir3
            42,  # Landing
        ),
    ],
)
def test_landing_basic_contact_y_context_controls_remain_outside_bridge_scope(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record, p)

    # Context controls: keep action-family context and landing transition stable without enforcing
    # fixed mismatch floors for rows outside this bridge's source-action scope.
    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 42
    assert np.isfinite(float(out["pos_y"][p]))
    assert np.isfinite(float(ref["pos_y"][p]))
