from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp


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


def _step_one_row_with_rollout_at_record(
    dataset_path: Path, record: int, p: int, *, window_before: int = 24
) -> tuple[np.void, np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for record={record}"

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    one_step_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[record, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[record, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
        binding.reseed_seed(one_step_handle, seed_bytes)
        binding.step_input(one_step_handle, prev_input_bytes, input_bytes)
        binding.write_compare(one_step_handle, out_compare_bytes)
        out_one = out_view[0].copy()
    finally:
        binding.destroy(one_step_handle)

    start = max(0, int(record) - int(window_before))
    rollout_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[start, seed_off : seed_off + seed_stride]
        binding.reseed_seed(rollout_handle, seed_bytes)
        for j in range(start, int(record) + 1):
            prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
            input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
            binding.step_input(rollout_handle, prev_input_bytes, input_bytes)
            if j == int(record):
                binding.write_compare(rollout_handle, out_compare_bytes)
        out_roll = out_view[0].copy()
    finally:
        binding.destroy(rollout_handle)

    seed = samples["seed_t"][record]
    ref = samples["ref_t1"][record]
    return seed, out_one, ref, out_roll


def _run_rollout_samples_to_record(
    samples: np.ndarray, *, num_players: int, start_record: int, target_record: int
) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(num_players),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(int(start_record), int(target_record) + 1):
            prev_input_bytes[0, :] = samples_u8[
                record, prev_input_off : prev_input_off + input_stride
            ]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        ref = samples["ref_t1"][target_record].copy()
        return out, ref
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
def test_landingfallspecial_rollout_uses_single_mplib_floor_bias_selfplay_181413() -> None:
    # Free-run rollout lock for mpColl floor-publication ownership:
    # - EscapeAir_Coll enters LandingFallSpecial through ftCo_LandingFallSpecial_Enter.
    # - mpLib_8004DD90_Floor can already publish the +0.0001 root/floor bias into the collision
    #   contact scratch. Landing entry must not add that bias a second time when it publishes the
    #   post-collision root Y.
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slp"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    samples = ds.samples
    p = 1
    seed_285 = samples[285]["seed_t"]
    ref_286 = samples[286]["ref_t1"]
    assert int(seed_285["action_id"][p]) == 236  # EscapeAir.
    assert int(seed_285["on_ground"][p]) == 0
    assert int(ref_286["action_id"][p]) == 43  # LandingFallSpecial.
    assert int(ref_286["on_ground"][p]) == 1
    assert float(ref_286["pos_y"][p]) == pytest.approx(0.0001, abs=1e-8)

    out, ref = _run_rollout_samples_to_record(
        samples, num_players=int(ds.header["num_players"]), start_record=0, target_record=286
    )
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 43
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=3e-8)
    assert float(out["pos_y"][p]) != pytest.approx(0.0002000166, abs=2e-8)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            1334,
            1,
            28,  # JumpAerialB
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            3486,
            0,
            28,  # JumpAerialB
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            2350,
            1,
            28,  # JumpAerialB
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            3306,
            1,
            28,  # JumpAerialB
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            5074,
            1,
            28,  # JumpAerialB
            42,  # Landing
        ),
    ],
)
def test_landing_basic_rows_keep_contact_y_parity_for_jumpaerialb_family(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record, p)

    # Decomp ownership:
    # - JumpAerial collision callback routes through ft_80082B1C and enters Landing_Enter_Basic.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082B1C
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
            1161,
            0,
            66,  # AttackAirF
            71,  # LandingAirF
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            4974,
            1,
            66,  # AttackAirF
            71,  # LandingAirF
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            1966,
            0,
            66,  # AttackAirF
            71,  # LandingAirF
        ),
    ],
)
def test_landing_airf_rows_keep_contact_y_parity_runtime_family(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(dataset_path, record, p)

    # Decomp ownership:
    # - AttackAir collision callback enters LandingAir_EnterWithLag.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(seed["on_ground"][p]) == 0
    assert int(ref["on_ground"][p]) == 1
    assert int(seed["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) == int(ref["hitstun"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 71
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 2e-4

    # Runtime-dominant lock: one-step@t and rollout@t agree for this float lane.
    assert int(out_roll["action_id"][p]) == int(out["action_id"][p])
    assert abs(float(out_roll["pos_y"][p]) - float(out["pos_y"][p])) <= 1e-4


@pytest.mark.integration
@pytest.mark.parametrize(
    (
        "dataset_rel",
        "record",
        "p",
        "seed_action",
        "ref_action",
        "seed_on_ground",
        "ref_on_ground",
        "rollout_action",
        "rollout_min_delta",
    ),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            385,
            0,
            66,  # AttackAirF (pre-landing frame)
            66,
            0,
            0,
            66,  # rollout matches one-step here
            0.0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            387,
            0,
            71,  # LandingAirF (post-landing frame)
            71,
            1,
            1,
            71,  # rollout now matches one-step after frozen-hitlag hitbox ownership fix
            0.0,
        ),
    ],
)
def test_landing_airf_runtime_context_controls_stay_replay_real(
    dataset_rel: str,
    record: int,
    p: int,
    seed_action: int,
    ref_action: int,
    seed_on_ground: int,
    ref_on_ground: int,
    rollout_action: int,
    rollout_min_delta: float,
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(dataset_path, record, p)

    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(seed["on_ground"][p]) == int(seed_on_ground)
    assert int(ref["on_ground"][p]) == int(ref_on_ground)

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 2e-4
    assert int(out_roll["action_id"][p]) == int(rollout_action)
    if rollout_min_delta <= 0.0:
        assert abs(float(out_roll["pos_y"][p]) - float(out["pos_y"][p])) <= 1e-4
    else:
        assert abs(float(out_roll["pos_y"][p]) - float(out["pos_y"][p])) >= float(rollout_min_delta)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
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


def test_fall_same_floor_final_publication_waits_one_frame_then_lands_qgd() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        / "QuerulousGrandDinosaur.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1
    seed_9530, out_9530, ref_9530 = _step_one_row(dataset_path, 9530, p)
    seed_9531, out_9531, ref_9531 = _step_one_row(dataset_path, 9531, p)

    # Decomp owner:
    # - Fall_Coll enters Landing through ft_80082B1C after the ECB-bottom floor callback.
    # - The first same-floor root projection stays airborne; the following deeper frame lands.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082B1C
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    assert int(seed_9530["action_id"][p]) == int(seed_9531["action_id"][p]) == 29
    assert int(seed_9530["action_frame"][p]) == 4
    assert int(seed_9531["action_frame"][p]) == 5
    assert float(seed_9530["floor_sweep_prev_pos_y_f32"][p]) > 0.0
    assert float(seed_9531["floor_sweep_prev_pos_y_f32"][p]) < 0.0
    assert int(out_9530["action_id"][p]) == int(ref_9530["action_id"][p]) == 29
    assert int(out_9530["on_ground"][p]) == int(ref_9530["on_ground"][p]) == 0
    assert int(out_9531["action_id"][p]) == int(ref_9531["action_id"][p]) == 42
    assert int(out_9531["on_ground"][p]) == int(ref_9531["on_ground"][p]) == 1
    assert float(out_9531["pos_y"][p]) == pytest.approx(float(ref_9531["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "stage_id", "ground_id", "note"),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "BlondHardHippopotamus.msl",
            5729,
            1,
            32,
            1,
            "FD center hard floor publishes ordinary Fall landing immediately",
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "DistinctCaringCobra.msl",
            6438,
            0,
            32,
            2,
            "FD ledge Fall action-entry landing publishes immediately",
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "FlippantEnchantedHorse.msl",
            7758,
            1,
            28,
            3,
            "non-FD legal-stage ledge Fall landing is outside the FD guard",
        ),
    ],
)
def test_fall_same_floor_final_publication_adjacent_landing_negatives(
    dataset_rel: str, record: int, p: int, stage_id: int, ground_id: int, note: str
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record, p)

    assert int(seed["stage_id"]) == stage_id, note
    assert int(seed["action_id"][p]) == 29, note  # Fall
    assert int(seed["ground_id"][p]) == ground_id, note
    assert int(ref["action_id"][p]) == 42, note  # Landing
    assert int(ref["on_ground"][p]) == 1, note
    assert int(out["action_id"][p]) == int(ref["action_id"][p]), note
    assert int(out["on_ground"][p]) == 1, note
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "expected_seed_rate", "expected_ref_frame"),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "FavorableSuperficialPig.msl",
            9898,
            1,
            3.01,
            3,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "TubbyCurlyHerring.msl",
            6133,
            0,
            3.01,
            3,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "DistinctCaringCobra.msl",
            8773,
            1,
            1.6722223,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "FavorableSuperficialPig.msl",
            1540,
            0,
            1.6722223,
            1,
        ),
    ],
)
def test_landing_fallspecial_origin_specific_frame_speed_replay_real_locks(
    dataset_rel: str, record: int, p: int, expected_seed_rate: float, expected_ref_frame: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record, p)

    # Decomp owner:
    # - LandingFallSpecial entry speed uses the landing-lag scalar supplied by the source action.
    # - EscapeAir sources use common x344 and advance to frame 3 on the seeded step.
    # - Firefox/Firebird sources use the character x90 landing lag and advance to frame 1.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099D70
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiFall_Anim,ftFx_SpecialHiBound_Anim}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
    assert int(seed["action_id"][p]) == 43  # LandingFallSpecial
    assert float(seed["frame_speed_mul_f32"][p]) == pytest.approx(expected_seed_rate, abs=2e-5)
    assert int(ref["action_id"][p]) == 43
    assert int(ref["action_frame"][p]) == int(expected_ref_frame)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])


@pytest.mark.integration
@pytest.mark.parametrize(
    (
        "dataset_rel",
        "record",
        "p",
        "expected_ref_action",
        "expected_ref_ground",
        "expected_ref_ground_id",
        "expected_out_action",
        "expected_out_ground",
        "expected_out_ground_id",
    ),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "HungryImportantSnake.msl",
            577,
            1,
            35,  # FallSpecial stays airborne on the main floor at af3.
            0,
            1,
            35,
            0,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "PositiveRevolvingHyena.msl",
            4321,
            1,
            43,  # FallSpecial lands on the adjacent ledge/seam floor at af3.
            1,
            2,
            43,
            1,
            2,
        ),
    ],
)
def test_fallspecial_af3_floor_callback_keeps_unmodeled_seam_handoff_residual(
    dataset_rel: str,
    record: int,
    p: int,
    expected_ref_action: int,
    expected_ref_ground: int,
    expected_ref_ground_id: int,
    expected_out_action: int,
    expected_out_ground: int,
    expected_out_ground_id: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record, p)

    # FallSpecial_Coll uses ft_80083090 -> ftCo_80096D28. The main-floor negative remains airborne,
    # while the adjacent ledge/seam row now lands through the callback-visible mpColl floor owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    #   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80083090
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004A45C_Floor,mpColl_8004B108}
    assert int(seed["action_id"][p]) == 35
    assert int(seed["action_frame"][p]) == 3
    assert int(seed["on_ground"][p]) == 0
    assert int(ref["action_id"][p]) == int(expected_ref_action)
    assert int(ref["on_ground"][p]) == int(expected_ref_ground)
    assert int(ref["ground_id"][p]) == int(expected_ref_ground_id)

    assert int(out["action_id"][p]) == int(expected_out_action)
    assert int(out["on_ground"][p]) == int(expected_out_ground)
    assert int(out["ground_id"][p]) == int(expected_out_ground_id)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            10216,
            0,
            27,  # JumpAerialF
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            1255,
            0,
            236,  # EscapeAir
            43,  # LandingFallSpecial
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            2378,
            0,
            236,  # EscapeAir
            43,  # LandingFallSpecial
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            5671,
            0,
            27,  # JumpAerialF
            42,  # Landing
        ),
    ],
)
def test_landing_rows_keep_self_vel_x_synced_with_ground_velocity(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record, p)

    # Decomp ownership:
    # - Grounding path keeps gr_vel and self_vel.x aligned (`gr_vel = self_vel.x`).
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4
    # - Grounded movement writes self_vel.x from gr_vel each frame.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(seed["on_ground"][p]) == 0
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1

    assert abs(float(out["speed_ground_x_self"][p]) - float(ref["speed_ground_x_self"][p])) <= 1e-6
    assert abs(float(out["speed_air_x_self"][p]) - float(ref["speed_air_x_self"][p])) <= 1e-6


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl",
            75,
            0,
            29,  # Fall
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            80,
            1,
            29,  # Fall
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            75,
            0,
            29,  # Fall
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            80,
            1,
            29,  # Fall
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            4561,
            0,
            29,  # Fall
            42,  # Landing
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            6426,
            1,
            29,  # Fall
            42,  # Landing
        ),
    ],
)
def test_landing_contact_y_bridge_runtime_rows_fall_to_landing_keep_pos_y_parity(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(dataset_path, record, p)

    # Decomp ownership:
    # - Fall collision callback path enters Landing via ft_80082B1C.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082B1C
    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(seed["on_ground"][p]) == 0
    assert int(ref["on_ground"][p]) == 1
    assert int(seed["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) == int(ref["hitstun"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 42
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 2e-4

    # Runtime-dominant lock: one-step@t and rollout@t agree on this float lane.
    assert int(out_roll["action_id"][p]) == int(out["action_id"][p])
    assert abs(float(out_roll["pos_y"][p]) - float(out["pos_y"][p])) <= 1e-4


@pytest.mark.integration
@pytest.mark.parametrize(
    (
        "dataset_rel",
        "record",
        "p",
        "seed_action",
        "ref_action",
        "one_step_action",
        "rollout_action",
        "min_delta",
    ),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            9531,
            1,
            29,  # Fall
            42,  # Landing
            42,  # floor-sweep previous-position seed lets one-step land at this row
            42,  # rollout lands at this row
            0.0,
        ),
    ],
)
def test_landing_contact_y_bridge_runtime_controls_stay_reseed_sensitive(
    dataset_rel: str,
    record: int,
    p: int,
    seed_action: int,
    ref_action: int,
    one_step_action: int,
    rollout_action: int,
    min_delta: float,
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, out, ref, out_roll = _step_one_row_with_rollout_at_record(dataset_path, record, p)

    assert int(seed["action_id"][p]) == int(seed_action)
    assert int(ref["action_id"][p]) == int(ref_action)
    assert int(seed["on_ground"][p]) == 0
    assert int(ref["on_ground"][p]) == 1

    assert int(out["action_id"][p]) == int(one_step_action)
    assert int(out_roll["action_id"][p]) == int(rollout_action)
    if min_delta > 0.0:
        assert abs(float(out["pos_y"][p]) - float(out_roll["pos_y"][p])) >= float(min_delta)
    else:
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
        assert float(out_roll["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
