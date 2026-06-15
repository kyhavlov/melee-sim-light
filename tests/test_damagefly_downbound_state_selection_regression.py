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


def _run_one_step(*, dataset_rel: str, record: int, p: int) -> tuple[np.ndarray, np.ndarray]:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short: num_records={num_records} record={record}"

    row = samples[record : record + 1]

    binding = pytest.importorskip("msl_binding")
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

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        ref = row["ref_t1"].reshape(-1)[0].copy()
        return out, ref
    finally:
        binding.destroy(handle)


def _run_one_step_with_seed_mutation(
    *, dataset_rel: str, record: int, mutate_seed
) -> tuple[np.ndarray, np.ndarray]:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short: num_records={num_records} record={record}"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_t = samples[record]["seed_t"].copy()
    mutate_seed(seed_t)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride).copy()
        prev_input_bytes = np.frombuffer(
            samples[record]["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = np.frombuffer(samples[record]["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        ref = samples[record]["ref_t1"].reshape(-1)[0].copy()
        return out, ref
    finally:
        binding.destroy(handle)


def _run_one_step_with_rollout(
    *, dataset_rel: str, record: int, p: int, window_before: int = 24
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short: num_records={num_records} record={record}"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(num_records, sample_stride)
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

    ref = samples["ref_t1"][record].copy()
    return out_one, ref, out_roll


@pytest.mark.integration
def test_downboundu_stays_downboundu_attachedgoodnaturedguanaco_record_2101_p0() -> None:
    # Seed==ref cluster regression:
    # ref=DownBoundU (183) -> out=DamageFlyLw (89) at record=2101 p0.
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    record = 2101
    p = 0

    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == int(row["ref_t1"]["action_id"][p]) == 183
    assert int(row["ref_t1"]["hitlag"][p]) == 0
    assert int(row["ref_t1"]["hitstun"][p]) == 0

    out, ref, out_roll = _run_one_step_with_rollout(dataset_rel=dataset_rel, record=record, p=p)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])


@pytest.mark.integration
def test_downboundu_stays_downboundu_gracefulattachedturtle_record_2317_p1() -> None:
    # Seed==ref cluster regression:
    # ref=DownBoundU (183) -> out=DamageFlyLw (89) at record=2317 p1.
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    record = 2317
    p = 1

    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == int(row["ref_t1"]["action_id"][p]) == 183
    assert int(row["ref_t1"]["hitlag"][p]) == 0
    assert int(row["ref_t1"]["hitstun"][p]) == 0

    out, ref, out_roll = _run_one_step_with_rollout(dataset_rel=dataset_rel, record=record, p=p)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action", "expected_out_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            1804,
            0,
            88,   # DamageFlyN
            183,  # DownBoundU
            183,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            5645,
            1,
            89,   # DamageFlyLw
            183,  # DownBoundU
            183,
        ),
    ],
)
def test_damagefly_land_to_downbound_passive_rows_keep_contact_y_parity_runtime_family(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int, expected_out_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == int(seed_action)
    assert int(row["ref_t1"]["action_id"][p]) == int(ref_action)
    assert int(row["seed_t"]["on_ground"][p]) == 0
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["seed_t"]["hitlag"][p]) == int(row["ref_t1"]["hitlag"][p]) == 0

    out, ref, out_roll = _run_one_step_with_rollout(dataset_rel=dataset_rel, record=record, p=p)

    # Decomp ownership:
    # - DamageFly collision callback enters ftCo_80090184 on grounded contact.
    # - ftCo_80090184 then enters Passive* or DownBound.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097D40
    assert int(out["action_id"][p]) == int(expected_out_action)
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 2e-4

    # Runtime-dominant lock: one-step@t and rollout@t agree for this contact-y lane.
    assert int(out_roll["action_id"][p]) == int(out["action_id"][p])
    assert int(out_roll["on_ground"][p]) == int(out["on_ground"][p])
    assert abs(float(out_roll["pos_y"][p]) - float(out["pos_y"][p])) <= 1e-4


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action", "expected_out_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            1802,
            0,
            88,  # DamageFlyN (pre-landing control)
            88,
            88,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            5644,
            1,
            89,  # DamageFlyLw (pre-landing control)
            89,
            89,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            5821,
            0,
            88,   # DamageFlyN
            183,  # DownBoundU (reference)
            183,  # hitlag-latched x668 makes x684 fail the tech debounce gate.
        ),
    ],
)
def test_damagefly_land_to_downbound_passive_context_controls_stay_replay_real(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int, expected_out_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == int(seed_action)
    assert int(row["ref_t1"]["action_id"][p]) == int(ref_action)

    out, ref, out_roll = _run_one_step_with_rollout(dataset_rel=dataset_rel, record=record, p=p)

    # `GracefulAttachedTurtle:5821/p0` locks the tech-timer seed surface: the L edge happens during
    # active hitlag, so Fighter_Spaghetti's x668 latch repeatedly resets x680 and overwrites x684.
    # That makes ftCo_800986B0's x684 debounce gate fail, and the shared DamageFly contact selector
    # falls through to DownBound rather than PassiveStandB.
    # refs/melee/src/melee/ft/fighter.c::{
    #   Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10
    # }
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_800986B0
    # tools/slippi/seed_history.py::compute_fighter_button_timers
    if dataset_rel.endswith("GracefulAttachedTurtle.msl") and record == 5821 and p == 0:
        assert int(row["seed_t"]["x680"][p]) == 1
        assert int(row["seed_t"]["x684"][p]) == 0
    assert int(out["action_id"][p]) == int(expected_out_action)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert np.isfinite(float(out["pos_y"][p]))
    assert np.isfinite(float(ref["pos_y"][p]))

    assert int(out_roll["action_id"][p]) == int(out["action_id"][p])
    assert int(out_roll["on_ground"][p]) == int(out["on_ground"][p])
    assert abs(float(out_roll["pos_y"][p]) - float(out["pos_y"][p])) <= 1e-4


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "expected_x680", "expected_x684", "expected_ref_action"),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl",
            3956,
            1,
            6,
            110,
            199,  # Passive
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl",
            8165,
            1,
            6,
            49,
            201,  # PassiveStandB
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl",
            5781,
            1,
            15,
            47,
            200,  # PassiveStandF
        ),
    ],
)
def test_damagefly_pre_hitlag_lr_tech_seed_preserves_passive_selector_replay_real(
    dataset_rel: str, record: int, p: int, expected_x680: int, expected_x684: int, expected_ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == 88  # DamageFlyN
    assert int(row["seed_t"]["x680"][p]) == expected_x680
    assert int(row["seed_t"]["x684"][p]) == expected_x684
    assert expected_x680 < 20
    assert expected_x684 >= 40
    assert int(row["ref_t1"]["action_id"][p]) == expected_ref_action

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)

    # Decomp ownership:
    # - ftCo_800986B0 admits floor tech when x680 < x250 and x684 >= x1C.
    # - ftCo_80090184 then routes to PassiveStandF/B before Passive, then DownBound.
    # - Pre-hitlag L/R edges preserve their first x684 debounce capture for this floor-contact
    #   callback; edges first pressed during active hitlag remain covered by the DownBound control.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_800986B0
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_80090184
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_80098928
    # tools/slippi/seed_history.py::compute_fighter_button_timers
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == expected_ref_action
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0
    np.testing.assert_allclose(float(out["speed_x_attack"][p]), float(ref["speed_x_attack"][p]), atol=1e-6)
    np.testing.assert_allclose(float(out["speed_y_attack"][p]), float(ref["speed_y_attack"][p]), atol=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "expected_ref_action", "expected_ref_on_ground", "expected_ref_hitstun", "expected_ref_state_flag3"),
    [
        (5060, 91, 0, 26, 2),
        (5062, 91, 0, 24, 2),
        (5063, 199, 1, 0, 0),
    ],
    ids=["agg_r5060_p0_roll_air", "agg_r5062_p0_roll_air", "agg_r5063_p0_roll_land"],
)
def test_damageflyroll_x221c_b6_floor_ownership_window_attachedgoodnaturedguanaco(
    record: int,
    expected_ref_action: int,
    expected_ref_on_ground: int,
    expected_ref_hitstun: int,
    expected_ref_state_flag3: int,
) -> None:
    # DamageFlyRoll ownership window:
    # - DamageFlyRoll_Anim/Phys keep the x221C_b6 lane active while tumble continues.
    # - DamageFlyRoll_Coll resolves grounded follow-up only when floor ownership turns over.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_DamageFlyRoll_Anim,ftCo_DamageFlyRoll_Phys,ftCo_DamageFlyRoll_Coll
    # }
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    p = 0

    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == 91  # DamageFlyRoll
    assert int(row["seed_t"]["on_ground"][p]) == 0
    assert int(row["seed_t"]["ecb_lock_timer"][p]) == 0
    assert int(row["seed_t"]["state_flags"][p, 3]) & 0x02  # x221C_b6 lane
    assert int(row["ref_t1"]["action_id"][p]) == int(expected_ref_action)
    assert int(row["ref_t1"]["on_ground"][p]) == int(expected_ref_on_ground)
    assert int(row["ref_t1"]["hitstun"][p]) == int(expected_ref_hitstun)
    assert int(row["ref_t1"]["state_flags"][p, 3]) == int(expected_ref_state_flag3)

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)
    assert int(ref["action_id"][p]) == int(expected_ref_action)
    assert int(ref["on_ground"][p]) == int(expected_ref_on_ground)
    assert int(ref["hitstun"][p]) == int(expected_ref_hitstun)
    assert int(ref["state_flags"][p, 3]) == int(expected_ref_state_flag3)

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == int(expected_ref_action)
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == int(expected_ref_on_ground)
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == int(expected_ref_hitstun)
    assert int(out["state_flags"][p, 3]) == int(ref["state_flags"][p, 3]) == int(expected_ref_state_flag3)


@pytest.mark.integration
def test_damageflyroll_active_hitlag_without_sdi_keeps_below_floor_pose_his() -> None:
    # Active-hitlag DamageFlyRoll floor ownership:
    # - ftCo_Damage_OnEveryHitlag can create a stay-airborne floor projection only when the
    #   callback-local SDI/ASDI owner actually admits displacement.
    # - Without that owner, frozen DamageFlyRoll hitlag must not run the generic floor projection
    #   before ftCo_Damage_OnExitHitlag / DamageFlyRoll_Coll. HIS 4398 starts below FD floor in
    #   hitlag and vanilla keeps that pose for the frozen row.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnEveryHitlag,ftCo_Damage_OnExitHitlag,ftCo_DamageFlyRoll_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl"
    record = 4398
    p = 0

    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == 91  # DamageFlyRoll
    assert int(row["seed_t"]["hitlag"][p]) > 0
    assert float(row["seed_t"]["pos_y"][p]) < 0.0
    assert float(row["ref_t1"]["pos_y"][p]) < 0.0
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 91
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    np.testing.assert_allclose(float(out["pos_y"][p]), float(ref["pos_y"][p]), atol=1e-6)
    assert float(out["pos_y"][p]) < 0.0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "expect_ref_action", "expect_ref_state_flag3"),
    [
        (11163, 91, 2),
        (11164, 91, 2),
        (11165, 91, 2),
    ],
    ids=["gat_r11163_p1_ctx_prev", "gat_r11164_p1_fixture_row", "gat_r11165_p1_ctx_next"],
)
def test_phasea_lock_gat_11164_action_and_stateflag3_window(
    record: int,
    expect_ref_action: int,
    expect_ref_state_flag3: int,
) -> None:
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    p = 1
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]

    assert int(row["ref_t1"]["action_id"][p]) == expect_ref_action
    assert int(row["ref_t1"]["state_flags"][p, 3]) == expect_ref_state_flag3
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == expect_ref_action
    assert int(out["state_flags"][p, 3]) == int(ref["state_flags"][p, 3]) == expect_ref_state_flag3
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "expected_action", "expected_on_ground"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            11167,
            1,
            191,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            9396,
            0,
            91,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl",
            7519,
            0,
            91,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/PutridJoyousOryx.msl",
            2684,
            1,
            91,
            0,
        ),
    ],
    ids=[
        "gat_falco_terminal_damageflyroll_lands",
        "pte_nonterminal_damageflyroll_airborne",
        "his_fox_lower_terminal_damageflyroll_airborne",
        "pjo_fox_lower_terminal_damageflyroll_airborne",
    ],
)
def test_damageflyroll_live_jobj_terminal_floor_turnover_window(
    dataset_rel: str, record: int, p: int, expected_action: int, expected_on_ground: int
) -> None:
    # DamageFlyRoll_Coll consumes ft_80081DD4 after ftCo_DamageFlyRoll_Phys has applied
    # ft_80084EEC and doFlyRoll's live XRotN JObj pose. GAT 11167 is the terminal-downward Falco
    # turnover row; the Fox and Marth controls protect the rejected broad carried-floor projection
    # and the regular-ECB fallback projection for non-terminal DamageFlyRoll.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_DamageFlyRoll_Phys,doFlyRoll,ftCo_DamageFlyRoll_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_800473CC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == 91  # DamageFlyRoll
    assert int(row["seed_t"]["on_ground"][p]) == 0
    if expected_on_ground:
        assert int(row["seed_t"]["hitlag"][p]) == 0
    assert int(row["seed_t"]["state_flags"][p, 3]) & 0x02
    assert int(row["ref_t1"]["action_id"][p]) == expected_action
    assert int(row["ref_t1"]["on_ground"][p]) == expected_on_ground

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == expected_action
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == expected_on_ground
    np.testing.assert_allclose(float(out["pos_y"][p]), float(ref["pos_y"][p]), atol=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "expect_ref_instance_id"),
    [
        (2376, 509),
        (2377, 509),
        (2378, 510),
    ],
    ids=["tbk_r2376_p0_ctx_prev", "tbk_r2377_p0_fixture_row", "tbk_r2378_p0_ctx_next"],
)
def test_phasea_lock_tbk_2377_instance_id_window(
    record: int,
    expect_ref_instance_id: int,
) -> None:
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    p = 0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]

    assert int(row["ref_t1"]["instance_id"][p]) == expect_ref_instance_id
    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)
    assert int(out["instance_id"][p]) == int(ref["instance_id"][p]) == expect_ref_instance_id


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "expect_ref_on_ground"),
    [
        (5059, 0),
        (5060, 0),
        (5061, 0),
    ],
    ids=["agg_r5059_p0_ctx_prev", "agg_r5060_p0_fixture_row", "agg_r5061_p0_ctx_next"],
)
def test_phasea_lock_agg_5060_on_ground_window(
    record: int,
    expect_ref_on_ground: int,
) -> None:
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    p = 0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]

    assert int(row["ref_t1"]["on_ground"][p]) == expect_ref_on_ground
    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == expect_ref_on_ground


@pytest.mark.integration
def test_damageflyhi_stays_damageflyhi_attachedgoodnaturedguanaco_record_1695_p1() -> None:
    # Seed==ref cluster regression:
    # ref=DamageFlyHi (87) -> out=DamageFlyLw (89) at record=1695 p1.
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    record = 1695
    p = 1

    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == int(row["ref_t1"]["action_id"][p]) == 87
    assert int(row["ref_t1"]["hitlag"][p]) == 0
    assert int(row["ref_t1"]["hitstun"][p]) == 35

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "action_id"),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "FavorableSuperficialPig.msl",
            6082,
            0,
            183,  # DownBoundU
        ),
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "TubbyCurlyHerring.msl",
            12266,
            0,
            191,  # DownBoundD
        ),
    ],
)
def test_downbound_floor_endpoint_clamp_keeps_airborne_edge_rows_in_downbound(
    dataset_rel: str, record: int, p: int, action_id: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == int(action_id)
    assert int(row["ref_t1"]["action_id"][p]) == int(action_id)
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)

    # DownBound uses ft_80082708 -> mpColl_8004B108. Its allow-ground-to-air edge exit should not
    # fire while mpLib_8004DD90_Floor would still clamp the floor endpoint by the decomp +0.1 range.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082708
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    # refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == int(action_id)
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0


def test_yoshi_platform_downbound_does_not_use_stage_side_ledge_exit_cnm_6944() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[6944]
    p = 1
    assert int(row["seed_t"]["action_id"][p]) == 191  # DownBoundD
    assert int(row["seed_t"]["on_ground"][p]) == 0
    assert int(row["seed_t"]["ground_id"][p]) == 1  # Yoshi's left soft platform.
    assert int(row["ref_t1"]["action_id"][p]) == 191

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=6944, p=p)

    # DownBound_Coll uses the active CollData.floor line. A Yoshi soft-platform row must not exit
    # through the generic stage side-ledge helper just because the root is near the stage ledge X;
    # platform line contact/endpoint ownership is handled by platform geometry instead.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082708
    # data/stages/bin/grst.bin::MSLSTG01 segment platform flags
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 191
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 1


def test_airborne_downbound_stage_object_endpoint_cross_enters_fall_mgs_3782() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[3782]
    p = 1
    assert int(row["seed_t"]["action_id"][p]) == 191  # DownBoundD
    assert int(row["seed_t"]["on_ground"][p]) == 0
    assert int(row["seed_t"]["ground_id"][p]) == 2
    assert int(row["ref_t1"]["action_id"][p]) == 29  # Fall

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=3782, p=p)

    # DownBound_Coll calls ft_80082708, which reports allow-ground-to-air floor loss while keeping
    # ground_or_air airborne. On FoD stage-object platform endpoints this is a generated
    # platform-transform floor, not a generic hard-floor ledge endpoint escape.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082708
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 29
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 2


def test_airborne_downbound_stage_object_endpoint_cross_requires_platform_transform_ground_id() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    def stale_nonplatform_floor(seed_t: np.void) -> None:
        seed_t["ground_id"][1] = np.uint16(5)

    out, _ref = _run_one_step_with_seed_mutation(
        dataset_rel=dataset_rel, record=3782, mutate_seed=stale_nonplatform_floor
    )

    # The retained branch is specifically the MSLSTG01 platform-transform endpoint owner. A stale
    # non-transform CollData.floor.index must not enter Fall through the same shortcut.
    assert int(out["action_id"][1]) == 191  # DownBoundD
    assert int(out["on_ground"][1]) == 0


@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "expected_action"),
    [
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/"
            "LoyalDishonestWren.msl",
            4090,
            0,
            183,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            1147,
            0,
            183,
        ),
        (
            "datasets/aggregate_recent/replays/validation/dream_land_recent/"
            "FlippantEnchantedHorse.msl",
            1387,
            0,
            183,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            4563,
            1,
            183,
        ),
    ],
)
def test_airborne_downbound_static_platform_endpoint_cross_enters_fall_replay_real(
    dataset_rel: str, record: int, p: int, expected_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == expected_action  # DownBoundU
    assert int(row["ref_t1"]["on_ground"][p]) == 0
    assert int(row["ref_t1"]["action_id"][p]) == 29  # Fall

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=record, p=p)

    # DownBound_Coll delegates to ft_80082708/mpColl_8004B108 for the active CollData.floor line.
    # Static soft-platform endpoint crossings use the same source floor-span owner as generated
    # platform lines; once the carried floor span is crossed, source enters Fall.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082708
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    # data/stages/bin/*.bin::MSLSTG01 segment platform flags
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 29
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])


def test_terminal_damagefly_iasa_fall_collision_projects_carried_hard_floor_pte_6623() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/fountain_of_dreams_recent/replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[6623]
    p = 0
    assert int(row["seed_t"]["action_id"][p]) == 90  # DamageFlyTop
    assert int(row["seed_t"]["hitstun"][p]) == 1
    assert int(row["seed_t"]["on_ground"][p]) == 0
    assert int(row["seed_t"]["ground_id"][p]) == 5
    assert float(row["seed_t"]["floor_sweep_prev_pos_y_f32"][p]) < 0.0
    assert int(row["ref_t1"]["action_id"][p]) == 42  # Landing
    assert int(row["ref_t1"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["ground_id"][p]) == 5

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=6623, p=p)

    # Terminal DamageFly_IASA can enter Fall before the same frame's collision callback. The
    # destination Fall_Coll (`ft_800831CC -> mpColl_80047E14(flags=6)`) still consumes the carried
    # hard-floor CollData source and lets mpColl_80044838_Floor project an already-below-floor root
    # to Landing.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 42
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 5
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p]) == 2
    np.testing.assert_allclose(float(out["pos_y"][p]), float(ref["pos_y"][p]), atol=1e-5)


def test_terminal_damagefly_iasa_fall_collision_requires_below_floor_carried_root() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/fountain_of_dreams_recent/replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    def move_carried_root_above_floor(seed_t: np.void) -> None:
        seed_t["floor_sweep_prev_pos_y_f32"][0] = np.float32(1.5)

    out, _ref = _run_one_step_with_seed_mutation(
        dataset_rel=dataset_rel, record=6623, mutate_seed=move_carried_root_above_floor
    )

    # The owner is not a generic "DamageFly terminal row lands" shortcut. It requires the carried
    # CollData root to already be on/below the hard floor before the terminal Fall_Coll projection.
    assert int(out["action_id"][0]) == 29  # Fall
    assert int(out["on_ground"][0]) == 0


def test_grounded_damageair_floor_loss_enters_missfoot_pte_7048() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/fountain_of_dreams_recent/replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[7048]
    p = 0
    assert int(row["seed_t"]["action_id"][p]) == 86  # DamageAir3
    assert int(row["seed_t"]["on_ground"][p]) == 1
    assert int(row["seed_t"]["facing"][p]) == 1
    assert int(row["ref_t1"]["action_id"][p]) == 251  # MissFoot
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=7048, p=p)

    # Grounded DamageAir still uses the common Damage_Coll ground branch:
    # ftCo_Damage_Coll -> ft_800848DC -> mpColl_8004B108. The FoD live platform endpoint reports
    # LeftLedgeSlip, and ft_800848DC enters MissFoot for a facing-right fighter crossing the left
    # side of the transformed platform span.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_800848DC
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    # refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 251
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p]) == 215
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0


def test_grounded_damageair_floor_loss_missfoot_requires_matching_ledge_slip_side() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/fountain_of_dreams_recent/replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    def face_away_from_left_ledge_slip(seed_t: np.void) -> None:
        seed_t["facing"][0] = np.uint8(0)

    out, _ref = _run_one_step_with_seed_mutation(
        dataset_rel=dataset_rel, record=7048, mutate_seed=face_away_from_left_ledge_slip
    )

    # ft_800848DC only enters MissFoot for the ledge-slip side that matches fighter facing.
    # The same floor-loss row with the opposite facing should route through the supplied
    # air-transfer callback instead of using the MissFoot shortcut.
    assert int(out["action_id"][0]) != 251


def test_downstand_anim_end_wait_guard_entry_uses_source_overlap_nudge_pte_3551() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/fountain_of_dreams_recent/replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[3551]
    p = 1
    assert int(row["seed_t"]["action_id"][p]) == 186  # DownStandU
    assert int(row["seed_t"]["on_ground"][p]) == 1
    assert int(row["ref_t1"]["action_id"][p]) == 251  # MissFoot
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out, ref = _run_one_step(dataset_rel=dataset_rel, record=3551, p=p)

    # Source order:
    # - DownStand_Anim can exit to Wait in Fighter_8006A360.
    # - The same Fighter_8006A360 pass then runs ftCommon_8007E0E4 and writes xF8 overlap nudge
    #   before Fighter_procUpdate runs the destination Wait IASA and enters GuardOn.
    # - GuardOn_Coll then consumes that already-applied nudge through ft_800845B4 and enters
    #   MissFoot on ledge-slip floor loss.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownStand.c::ftCo_DownStand_Anim
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate,Fighter_procMap}
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E0E4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_GuardOn_Coll}
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 251
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p]) == 215
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p]) == 7
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p]) == 1
    np.testing.assert_allclose(float(out["pos_x"][p]), float(ref["pos_x"][p]), atol=1e-6)
    np.testing.assert_allclose(float(out["pos_y"][p]), float(ref["pos_y"][p]), atol=1e-6)


def test_wait_guard_entry_overlap_nudge_not_generic_guardon_floor_loss() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/fountain_of_dreams_recent/replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    def start_already_guardon(seed_t: np.void) -> None:
        p = 1
        seed_t["action_id"][p] = np.uint16(178)  # GuardOn
        seed_t["animation_index"][p] = np.uint32(0xFFFFFFFF)
        seed_t["action_frame"][p] = np.int16(-1)

    out, _ref = _run_one_step_with_seed_mutation(
        dataset_rel=dataset_rel, record=3551, mutate_seed=start_already_guardon
    )

    # The overlap nudge/floor-loss admission is tied to the current-frame Wait-callback entry
    # marker. A steady GuardOn seed at the same edge may still take ordinary grounded edge-snap
    # position ownership, but it must not inherit the DownStand/Wait source floor-loss gate.
    assert int(out["action_id"][1]) != 251
    np.testing.assert_allclose(float(out["pos_x"][1]), 63.34754943847656, atol=1e-6)
