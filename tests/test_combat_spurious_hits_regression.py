from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_spurious_body_hitstun_not_applied_treasuredbackkangaroo_record_1075_p1() -> None:
    # Locks in a suite offender where a spurious BODY hit was being applied to p=1 even though
    # the replay ref has hitlag/hitstun == 0 (seed==ref for those fields).
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 1075
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # Seed/ref sanity: this regression is for false-positive hits when seed==ref (t -> t+1) expects no hit.
    assert int(row["seed_t"]["hitlag"][0, 1]) == 0
    assert int(row["ref_t1"]["hitlag"][0, 1]) == 0
    assert int(row["seed_t"]["hitstun"][0, 1]) == 0
    assert int(row["ref_t1"]["hitstun"][0, 1]) == 0
    assert int(row["seed_t"]["action_id"][0, 1]) == int(row["ref_t1"]["action_id"][0, 1])
    assert int(row["seed_t"]["animation_index"][0, 1]) == int(row["ref_t1"]["animation_index"][0, 1])

    expected_action = int(row["ref_t1"]["action_id"][0, 1])
    expected_anim = int(row["ref_t1"]["animation_index"][0, 1])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, 1])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, 1])

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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, 1])
        got_anim = int(out["animation_index"][0, 1])
        got_hitlag = int(out["hitlag"][0, 1])
        got_hitstun = int(out["hitstun"][0, 1])

        assert got_action == expected_action, f"record=1075 p=1 expected action_id={expected_action}, got {got_action}"
        assert got_anim == expected_anim, f"record=1075 p=1 expected animation_index={expected_anim}, got {got_anim}"
        assert got_hitlag == expected_hitlag, f"record=1075 p=1 expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert got_hitstun == expected_hitstun, f"record=1075 p=1 expected hitstun={expected_hitstun}, got {got_hitstun}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_spurious_body_hitlag_not_applied_gracefulattachedturtle_record_4505_p0() -> None:
    # Locks in a seed==ref false-positive BODY hit (hitlag/hitstun/action_id divergence) that was
    # caused by pose-driven hitbox geometry being mis-scaled relative to ISO-derived hurtcaps.
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 4505
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # Seed/ref sanity: in this offender, the replay expects no hit at t+1.
    assert int(row["seed_t"]["hitlag"][0, 0]) == 0
    assert int(row["ref_t1"]["hitlag"][0, 0]) == 0
    assert int(row["seed_t"]["hitstun"][0, 0]) == 0
    assert int(row["ref_t1"]["hitstun"][0, 0]) == 0
    assert int(row["seed_t"]["action_id"][0, 0]) == int(row["ref_t1"]["action_id"][0, 0])

    expected_action = int(row["ref_t1"]["action_id"][0, 0])
    expected_anim = int(row["ref_t1"]["animation_index"][0, 0])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, 0])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, 0])

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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, 0])
        got_anim = int(out["animation_index"][0, 0])
        got_hitlag = int(out["hitlag"][0, 0])
        got_hitstun = int(out["hitstun"][0, 0])

        assert got_action == expected_action, f"record=4505 p=0 expected action_id={expected_action}, got {got_action}"
        assert got_anim == expected_anim, f"record=4505 p=0 expected animation_index={expected_anim}, got {got_anim}"
        assert got_hitlag == expected_hitlag, f"record=4505 p=0 expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert got_hitstun == expected_hitstun, f"record=4505 p=0 expected hitstun={expected_hitstun}, got {got_hitstun}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_spurious_shield_hit_not_applied_treasuredbackkangaroo_record_2217_p0() -> None:
    # Locks in a seed==ref false-positive SHIELD hit (GuardSetOff + shield HP depletion) that was
    # caused by shield-bubble geometry being mis-scaled / mis-rotated relative to pose primitives.
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 2217
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    assert int(row["seed_t"]["hitlag"][0, 0]) == 0
    assert int(row["ref_t1"]["hitlag"][0, 0]) == 0

    assert int(row["seed_t"]["action_id"][0, 0]) == int(row["ref_t1"]["action_id"][0, 0])

    expected_action = int(row["ref_t1"]["action_id"][0, 0])
    expected_anim = int(row["ref_t1"]["animation_index"][0, 0])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, 0])
    expected_shield_hp = float(row["ref_t1"]["shield_hp"][0, 0])

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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, 0])
        got_anim = int(out["animation_index"][0, 0])
        got_hitlag = int(out["hitlag"][0, 0])
        got_shield_hp = float(out["shield_hp"][0, 0])

        assert got_action == expected_action, f"record=2217 p=0 expected action_id={expected_action}, got {got_action}"
        assert got_anim == expected_anim, f"record=2217 p=0 expected animation_index={expected_anim}, got {got_anim}"
        assert got_hitlag == expected_hitlag, f"record=2217 p=0 expected hitlag={expected_hitlag}, got {got_hitlag}"
        got_bits = int(np.float32(got_shield_hp).view(np.uint32))
        exp_bits = int(np.float32(expected_shield_hp).view(np.uint32))
        assert got_bits == exp_bits, (
            f"record=2217 p=0 expected shield_hp={expected_shield_hp} (0x{exp_bits:08x}), "
            f"got {got_shield_hp} (0x{got_bits:08x})"
        )
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_hurtbox_state_not_overwritten_on_entry_frame_gracefulattachedturtle_record_201_p1() -> None:
    # Locks in a suite offender where we were incorrectly outputting hurtbox_state=2 when the replay
    # ref has 0 (seed==ref for hurtbox_state), typically caused by applying move-induced hit status
    # on the same frame as a post-Anim motion-state transition.
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 201
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    assert int(row["seed_t"]["hurtbox_state"][0, 1]) == 0
    assert int(row["ref_t1"]["hurtbox_state"][0, 1]) == 0

    expected_hurtbox_state = int(row["ref_t1"]["hurtbox_state"][0, 1])

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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got = int(out["hurtbox_state"][0, 1])

        assert got == expected_hurtbox_state, (
            f"record=201 p=1 expected hurtbox_state={expected_hurtbox_state}, got {got}"
        )
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_spurious_hitstun_not_applied_shine_start_gracefulattachedturtle_record_148_p0() -> None:
    # Locks in a seed==ref offender where the sim was applying a spurious hit on the Shine-start
    # entry frame because move-induced hit status (opcode 26 / fp->x1988) was not reflected into
    # hurtbox_state until the next frame.
    #
    # Decomp anchor: ftFx_SpecialLw_Enter / ftFx_SpecialAirLw_Enter call ftAnim_8006EBA4 immediately
    # on entry, so the Shine-start cmd script can set hit status on the entry frame.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 148
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    # Seed/ref sanity: target is p0 (Fox) entering Shine Start with no hit applied.
    assert int(row["seed_t"]["hitlag"][0, 0]) == 0
    assert int(row["ref_t1"]["hitlag"][0, 0]) == 0
    assert int(row["seed_t"]["hitstun"][0, 0]) == 0
    assert int(row["ref_t1"]["hitstun"][0, 0]) == 0

    expected_action = int(row["ref_t1"]["action_id"][0, 0])
    expected_anim = int(row["ref_t1"]["animation_index"][0, 0])
    expected_hurtbox_state = int(row["ref_t1"]["hurtbox_state"][0, 0])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, 0])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, 0])

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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, 0])
        got_anim = int(out["animation_index"][0, 0])
        got_hurtbox_state = int(out["hurtbox_state"][0, 0])
        got_hitlag = int(out["hitlag"][0, 0])
        got_hitstun = int(out["hitstun"][0, 0])

        assert got_action == expected_action, f"record=148 p=0 expected action_id={expected_action}, got {got_action}"
        assert got_anim == expected_anim, f"record=148 p=0 expected animation_index={expected_anim}, got {got_anim}"
        assert got_hurtbox_state == expected_hurtbox_state, (
            f"record=148 p=0 expected hurtbox_state={expected_hurtbox_state}, got {got_hurtbox_state}"
        )
        assert got_hitlag == expected_hitlag, f"record=148 p=0 expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert got_hitstun == expected_hitstun, f"record=148 p=0 expected hitstun={expected_hitstun}, got {got_hitstun}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_spurious_hitstun_not_applied_shine_start_gracefulattachedturtle_record_4500_p0() -> None:
    # Same Shine-start entry-frame hit status regression as record 148, but with a different
    # surrounding interaction (suite coverage broadening).
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 4500
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    assert int(row["seed_t"]["hitlag"][0, 0]) == 0
    assert int(row["ref_t1"]["hitlag"][0, 0]) == 0
    assert int(row["seed_t"]["hitstun"][0, 0]) == 0
    assert int(row["ref_t1"]["hitstun"][0, 0]) == 0

    expected_action = int(row["ref_t1"]["action_id"][0, 0])
    expected_anim = int(row["ref_t1"]["animation_index"][0, 0])
    expected_hurtbox_state = int(row["ref_t1"]["hurtbox_state"][0, 0])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, 0])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, 0])

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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, 0])
        got_anim = int(out["animation_index"][0, 0])
        got_hurtbox_state = int(out["hurtbox_state"][0, 0])
        got_hitlag = int(out["hitlag"][0, 0])
        got_hitstun = int(out["hitstun"][0, 0])

        assert got_action == expected_action, f"record=4500 p=0 expected action_id={expected_action}, got {got_action}"
        assert got_anim == expected_anim, f"record=4500 p=0 expected animation_index={expected_anim}, got {got_anim}"
        assert got_hurtbox_state == expected_hurtbox_state, (
            f"record=4500 p=0 expected hurtbox_state={expected_hurtbox_state}, got {got_hurtbox_state}"
        )
        assert got_hitlag == expected_hitlag, f"record=4500 p=0 expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert got_hitstun == expected_hitstun, f"record=4500 p=0 expected hitstun={expected_hitstun}, got {got_hitstun}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_spurious_body_hit_not_applied_falco_bair_vs_fox_shine_loop_gracefulattachedturtle_record_4504_p0() -> None:
    # Locks in a seed==ref offender where Falco bair (msid=70) was spuriously connecting against
    # Fox ShineLoop due to incorrect hitbox facing transform (X-mirror vs decomp rotY mixing X/Z),
    # producing out.hitlag/hitstun > 0 when the replay ref has none.
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 4504
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, 0]) == int(row["ref_t1"]["action_id"][0, 0])
    assert int(row["seed_t"]["animation_index"][0, 0]) == int(row["ref_t1"]["animation_index"][0, 0])
    assert int(row["seed_t"]["hitlag"][0, 0]) == 0
    assert int(row["ref_t1"]["hitlag"][0, 0]) == 0
    assert int(row["seed_t"]["hitstun"][0, 0]) == 0
    assert int(row["ref_t1"]["hitstun"][0, 0]) == 0

    expected_action = int(row["ref_t1"]["action_id"][0, 0])
    expected_anim = int(row["ref_t1"]["animation_index"][0, 0])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, 0])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, 0])

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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, 0])
        got_anim = int(out["animation_index"][0, 0])
        got_hitlag = int(out["hitlag"][0, 0])
        got_hitstun = int(out["hitstun"][0, 0])

        assert got_action == expected_action, f"record=4504 p=0 expected action_id={expected_action}, got {got_action}"
        assert got_anim == expected_anim, f"record=4504 p=0 expected animation_index={expected_anim}, got {got_anim}"
        assert got_hitlag == expected_hitlag, f"record=4504 p=0 expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert got_hitstun == expected_hitstun, f"record=4504 p=0 expected hitstun={expected_hitstun}, got {got_hitstun}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_spurious_body_hit_not_applied_falco_bair_mirror_attachedgoodnaturedguanaco_record_2693_p0() -> None:
    # Another suite-visible seed==ref offender for the same root cause as record 4504 above
    # (Falco bair BODY overlap false-positive).
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 2693
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, 0]) == int(row["ref_t1"]["action_id"][0, 0])
    assert int(row["seed_t"]["animation_index"][0, 0]) == int(row["ref_t1"]["animation_index"][0, 0])
    assert int(row["seed_t"]["hitlag"][0, 0]) == 0
    assert int(row["ref_t1"]["hitlag"][0, 0]) == 0
    assert int(row["seed_t"]["hitstun"][0, 0]) == 0
    assert int(row["ref_t1"]["hitstun"][0, 0]) == 0

    expected_action = int(row["ref_t1"]["action_id"][0, 0])
    expected_anim = int(row["ref_t1"]["animation_index"][0, 0])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, 0])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, 0])

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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, 0])
        got_anim = int(out["animation_index"][0, 0])
        got_hitlag = int(out["hitlag"][0, 0])
        got_hitstun = int(out["hitstun"][0, 0])

        assert got_action == expected_action, f"record=2693 p=0 expected action_id={expected_action}, got {got_action}"
        assert got_anim == expected_anim, f"record=2693 p=0 expected animation_index={expected_anim}, got {got_anim}"
        assert got_hitlag == expected_hitlag, f"record=2693 p=0 expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert got_hitstun == expected_hitstun, f"record=2693 p=0 expected hitstun={expected_hitstun}, got {got_hitstun}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_fox_laser_damage_does_not_apply_hitlag_or_hitstun_treasuredbackkangaroo_record_197_p1() -> None:
    # Fox blaster shots (kbg/wsk/bkb all zero in data/items/lasers.bin) deal damage but should not
    # apply hitlag/hitstun or force a damage-state transition.
    root = Path(__file__).resolve().parents[1]
    lasers_bin_rel = "data/items/lasers.bin"
    lasers_bin_path = root / lasers_bin_rel
    if not lasers_bin_path.exists():
        pytest.skip(f"missing local artifact: {lasers_bin_rel}")
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 197
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, 1]) == int(row["ref_t1"]["action_id"][0, 1])
    assert int(row["seed_t"]["animation_index"][0, 1]) == int(row["ref_t1"]["animation_index"][0, 1])
    assert int(row["seed_t"]["hitlag"][0, 1]) == 0
    assert int(row["ref_t1"]["hitlag"][0, 1]) == 0
    assert int(row["seed_t"]["hitstun"][0, 1]) == 0
    assert int(row["ref_t1"]["hitstun"][0, 1]) == 0

    expected_action = int(row["ref_t1"]["action_id"][0, 1])
    expected_anim = int(row["ref_t1"]["animation_index"][0, 1])
    expected_hitlag = int(row["ref_t1"]["hitlag"][0, 1])
    expected_hitstun = int(row["ref_t1"]["hitstun"][0, 1])
    expected_percent = float(row["ref_t1"]["percent"][0, 1])

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

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, 1])
        got_anim = int(out["animation_index"][0, 1])
        got_hitlag = int(out["hitlag"][0, 1])
        got_hitstun = int(out["hitstun"][0, 1])
        got_percent = float(out["percent"][0, 1])

        assert got_action == expected_action, f"record=197 p=1 expected action_id={expected_action}, got {got_action}"
        assert got_anim == expected_anim, f"record=197 p=1 expected animation_index={expected_anim}, got {got_anim}"
        assert got_hitlag == expected_hitlag, f"record=197 p=1 expected hitlag={expected_hitlag}, got {got_hitlag}"
        assert got_hitstun == expected_hitstun, f"record=197 p=1 expected hitstun={expected_hitstun}, got {got_hitstun}"
        got_bits = np.array([np.float32(got_percent)], dtype=np.float32).view(np.uint32)
        exp_bits = np.array([np.float32(expected_percent)], dtype=np.float32).view(np.uint32)
        assert np.array_equal(
            got_bits, exp_bits
        ), f"record=197 p=1 expected percent={expected_percent}, got {got_percent}"
    finally:
        binding.destroy(handle)
