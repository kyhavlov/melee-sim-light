from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _tracks_end_frame(tracks_path: Path, msid: int) -> float:
    with tracks_path.open("rb") as f:
        magic = f.read(8)
        if magic != b"SSANIMT1":
            raise ValueError(f"bad tracks magic: {magic!r}")
        (version,) = struct.unpack("<I", f.read(4))
        if version not in (1, 2):
            raise ValueError(f"unsupported tracks version: {version}")
        local_count, anim_count = struct.unpack("<HH", f.read(4))
        f.read(local_count)  # local_parts
        f.read(2 * local_count)  # local_parent
        f.read(4 * local_count)  # local_flags

        for _ in range(anim_count):
            (mid,) = struct.unpack("<H", f.read(2))
            (end_frame,) = struct.unpack("<f", f.read(4))
            if version >= 2:
                f.read(1)  # aobj_loop
            for _lp in range(local_count):
                part_u8 = f.read(1)
                if not part_u8:
                    raise ValueError("unexpected EOF in tracks parts")
                (n_tracks,) = struct.unpack("<B", f.read(1))
                for _t in range(n_tracks):
                    hdr = f.read(8)
                    if len(hdr) != 8:
                        raise ValueError("unexpected EOF in tracks header")
                    (_obj_type, _frac_value, _frac_slope, _pad, _startframe, length) = struct.unpack(
                        "<BBBBHH", hdr
                    )
                    f.read(int(length))
            if int(mid) == int(msid):
                return float(end_frame)

    raise KeyError(f"msid {msid} not found in {tracks_path}")


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


@pytest.mark.integration
def test_action_frame_run_anim_rate_scaled_from_ground_speed() -> None:
    # Cluster lock: Action-frame mismatch on Run where Slippi state_age advances by a fractional
    # amount derived from ground speed.
    #
    # Regression target: AGG rec 741 p0 (Run -> Run) with seed.action_frame == ref.action_frame == 1
    # but the sim previously advanced state_age too quickly (out.action_frame != 1).
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 741
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 21  # Run
    assert int(row["ref_t1"]["action_id"][0, p]) == 21
    assert int(row["seed_t"]["action_frame"][0, p]) == 1
    assert int(row["ref_t1"]["action_frame"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert float(row["seed_t"]["speed_ground_x_self"][0, p]) == pytest.approx(1.5)

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_af = int(out["action_frame"][0, p])
        want_af = int(row["ref_t1"]["action_frame"][0, p])
        assert got_af == want_af, f"record={record} p={p} expected action_frame={want_af}, got {got_af}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_action_frame_non_looping_anim_clamps_at_end_frame() -> None:
    # Cluster lock: Action-frame mismatch where a non-looping timeline reaches its end_frame and
    # Slippi state_age stops advancing (rate becomes 0).
    #
    # Regression target: AGG rec 3503 p1 (DamageFlyN) with seed.action_frame == ref.action_frame == 29
    # but the sim previously advanced beyond the end.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 3503
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 88  # DamageFlyN
    assert int(row["ref_t1"]["action_id"][0, p]) == 88
    assert int(row["seed_t"]["animation_index"][0, p]) == 178
    assert int(row["ref_t1"]["animation_index"][0, p]) == 178
    assert int(row["seed_t"]["action_frame"][0, p]) == 29
    assert int(row["ref_t1"]["action_frame"][0, p]) == 29
    assert float(row["seed_t"]["anim_frame_f32"][0, p]) == pytest.approx(29.0)

    # Sanity-check extracted end_frame for this msid (ISO-derived tracks).
    char_id = int(row["seed_t"]["char_id"][0, p])
    tracks_rel = "data/anims/fox.tracks.bin" if char_id == 1 else "data/anims/falco.tracks.bin"
    tracks_path = root / tracks_rel
    if not tracks_path.exists():
        pytest.skip(f"missing local tracks: {tracks_rel}")
    assert _tracks_end_frame(tracks_path, 178) == pytest.approx(29.0)

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_af = int(out["action_frame"][0, p])
        want_af = int(row["ref_t1"]["action_frame"][0, p])
        assert got_af == want_af, f"record={record} p={p} expected action_frame={want_af}, got {got_af}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_state_flags_x221a_b3_not_set_on_throw_release_damage_entry() -> None:
    # Cluster lock: state_flags[1] bit 0x10 (fp->x221A_b3) should not be set for throw-release
    # damage-state entry frames that have hitstun but no hitlag.
    #
    # Regression target: TBK rec 444 p1 (ThrownHi -> DamageFlyTop) with ref.state_flags[1] == 0.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 444
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 241  # ThrownHi
    assert int(row["ref_t1"]["action_id"][0, p]) == 90  # DamageFlyTop
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) > 0
    assert int(row["seed_t"]["state_flags"][0, p, 1]) == 0
    assert int(row["ref_t1"]["state_flags"][0, p, 1]) == 0

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got = int(out["state_flags"][0, p, 1])
        want = int(row["ref_t1"]["state_flags"][0, p, 1])
        assert got == want, f"record={record} p={p} expected state_flags[1]={want}, got {got}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_state_flags_x221c_b2_set_on_guard_reflect_entry() -> None:
    # Schema note: this slice adds `seed_t.guard_reflect_timer_x18`; rebuild cached suite datasets
    # with `uv run python -m tools.slippi.preprocess_suite --suite ... --datasets-dir datasets --force`.
    #
    # Cluster lock: GuardReflect entry sets fp->x221C_b2 ("Powershield Active Bool") alongside
    # fp->x221C_b1/fp->x221C_b3.
    #
    # Decomp:
    # - Entry set: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
    # - Slippi flag packing: refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (0x221C 0x20)
    #
    # Regression target: TBK rec 213 p0 (Dash -> GuardReflect), ref.state_flags[3] == 112
    # (0x40|0x20|0x10), where the sim previously emitted 80 (0x40|0x10).
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 213
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 20  # Dash
    assert int(row["ref_t1"]["action_id"][0, p]) == 182  # GuardReflect
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["state_flags"][0, p, 3]) == 112  # 0x40|0x20|0x10

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got = int(out["state_flags"][0, p, 3])
        want = int(row["ref_t1"]["state_flags"][0, p, 3])
        assert got == want, f"record={record} p={p} expected state_flags[3]={want}, got {got}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_state_flags_x221c_b2_clears_after_guard_reflect_window() -> None:
    # Cluster lock: when the GuardReflect timer is inactive, x221C_b2 should clear instead of
    # carrying a stale seed bit.
    #
    # Decomp clear path: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
    #
    # Regression target: TBK rec 217 p0 (GuardReflect -> GuardReflect), seed.state_flags[3] has 0x20
    # but ref.state_flags[3] is 0.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 217
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 182  # GuardReflect
    assert int(row["ref_t1"]["action_id"][0, p]) == 182
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert int(row["seed_t"]["guard_reflect_timer_x14"][0, p]) == 0
    assert int(row["seed_t"]["guard_reflect_timer_x18"][0, p]) == 1
    assert int(row["seed_t"]["state_flags"][0, p, 3]) == 32
    assert int(row["ref_t1"]["state_flags"][0, p, 3]) == 0

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got = int(out["state_flags"][0, p, 3])
        want = int(row["ref_t1"]["state_flags"][0, p, 3])
        assert got == want, f"record={record} p={p} expected state_flags[3]={want}, got {got}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_action_frame_blaster_start_entry_ticks_once() -> None:
    # Cluster lock: SpecialAirNStart entry calls ftAnim_8006EBA4 immediately after ChangeMotionState,
    # so post-frame action_frame is 1 (not 0) on the entry frame.
    #
    # Regression target: TBK rec 3786 p1 (JumpF -> SpecialAirNStart) with seed.action_frame == ref.action_frame == 1.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 3786
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 25  # JumpF
    assert int(row["ref_t1"]["action_id"][0, p]) == 344  # ftFx_MS_SpecialAirNStart
    assert int(row["seed_t"]["action_frame"][0, p]) == 1
    assert int(row["ref_t1"]["action_frame"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    # Entry input: pressed-edge B.
    prev_buttons = int(row["prev_input_t"]["p"][0, p]["buttons"])
    cur_buttons = int(row["input_t"]["p"][0, p]["buttons"])
    assert (prev_buttons & 0x0200) == 0  # MSL_BUTTON_B
    assert (cur_buttons & 0x0200) != 0

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_a = int(out["action_id"][0, p])
        want_a = int(row["ref_t1"]["action_id"][0, p])
        assert got_a == want_a, f"record={record} p={p} expected action_id={want_a}, got {got_a}"

        got_af = int(out["action_frame"][0, p])
        want_af = int(row["ref_t1"]["action_frame"][0, p])
        assert got_af == want_af, f"record={record} p={p} expected action_frame={want_af}, got {got_af}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_action_frame_shine_start_entry_ticks_once() -> None:
    # Cluster lock: SpecialLwStart entry calls ftAnim_8006EBA4 immediately after ChangeMotionState,
    # so post-frame action_frame is 1 (not 0) on the entry frame.
    #
    # Regression target: TBK rec 1601 p1 (Squat -> SpecialLwStart) with seed.action_frame == ref.action_frame == 1.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 1601
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 39  # Squat
    assert int(row["ref_t1"]["action_id"][0, p]) == 360  # ftFx_MS_SpecialLwStart
    assert int(row["seed_t"]["action_frame"][0, p]) == 1
    assert int(row["ref_t1"]["action_frame"][0, p]) == 1

    # Entry input: pressed-edge B + down stick.
    prev_buttons = int(row["prev_input_t"]["p"][0, p]["buttons"])
    cur_buttons = int(row["input_t"]["p"][0, p]["buttons"])
    assert (prev_buttons & 0x0200) == 0  # MSL_BUTTON_B
    assert (cur_buttons & 0x0200) != 0
    assert int(row["input_t"]["p"][0, p]["main_y"]) < 0

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_a = int(out["action_id"][0, p])
        want_a = int(row["ref_t1"]["action_id"][0, p])
        assert got_a == want_a, f"record={record} p={p} expected action_id={want_a}, got {got_a}"

        got_af = int(out["action_frame"][0, p])
        want_af = int(row["ref_t1"]["action_frame"][0, p])
        assert got_af == want_af, f"record={record} p={p} expected action_frame={want_af}, got {got_af}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_action_frame_attackair_entry_from_cstick_edge() -> None:
    # Cluster lock: AttackAir entry can be triggered via a C-stick threshold crossing (ftCo_800DF478),
    # and the entry path runs ftAnim_8006EBA4 immediately so action_frame is 1 on the entry frame.
    #
    # Regression target: TBK rec 734 p0 (JumpF -> AttackAirLw) with seed.action_frame == ref.action_frame == 1.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 734
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 25  # JumpF
    assert int(row["ref_t1"]["action_id"][0, p]) == 69  # ftCo_MS_AttackAirLw
    assert int(row["seed_t"]["action_frame"][0, p]) == 1
    assert int(row["ref_t1"]["action_frame"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    # Entry input: no A press, but C-stick moves sharply this frame.
    prev_buttons = int(row["prev_input_t"]["p"][0, p]["buttons"])
    cur_buttons = int(row["input_t"]["p"][0, p]["buttons"])
    assert (prev_buttons & 0x0100) == 0  # MSL_BUTTON_A
    assert (cur_buttons & 0x0100) == 0
    assert abs(int(row["prev_input_t"]["p"][0, p]["c_y"])) < 30
    assert abs(int(row["input_t"]["p"][0, p]["c_y"])) > 60

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_a = int(out["action_id"][0, p])
        want_a = int(row["ref_t1"]["action_id"][0, p])
        assert got_a == want_a, f"record={record} p={p} expected action_id={want_a}, got {got_a}"

        got_af = int(out["action_frame"][0, p])
        want_af = int(row["ref_t1"]["action_frame"][0, p])
        assert got_af == want_af, f"record={record} p={p} expected action_frame={want_af}, got {got_af}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_action_frame_attackair_entry_from_cstick_edge_second_example() -> None:
    # Same cluster lock as test_action_frame_attackair_entry_from_cstick_edge, but on a second replay
    # row to reduce overfitting to a single record.
    #
    # Regression target: TBK rec 5460 p1 (JumpB -> AttackAirLw).
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 5460
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 26  # JumpB
    assert int(row["ref_t1"]["action_id"][0, p]) == 69  # ftCo_MS_AttackAirLw
    assert int(row["seed_t"]["action_frame"][0, p]) == 1
    assert int(row["ref_t1"]["action_frame"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    # Entry input: no A press, but C-stick moves sharply this frame.
    prev_buttons = int(row["prev_input_t"]["p"][0, p]["buttons"])
    cur_buttons = int(row["input_t"]["p"][0, p]["buttons"])
    assert (prev_buttons & 0x0100) == 0  # MSL_BUTTON_A
    assert (cur_buttons & 0x0100) == 0
    assert abs(int(row["prev_input_t"]["p"][0, p]["c_y"])) < 30
    assert abs(int(row["input_t"]["p"][0, p]["c_y"])) > 60

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_a = int(out["action_id"][0, p])
        want_a = int(row["ref_t1"]["action_id"][0, p])
        assert got_a == want_a, f"record={record} p={p} expected action_id={want_a}, got {got_a}"

        got_af = int(out["action_frame"][0, p])
        want_af = int(row["ref_t1"]["action_frame"][0, p])
        assert got_af == want_af, f"record={record} p={p} expected action_frame={want_af}, got {got_af}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_state_flags_fastfall_bit_persists_across_fall_anim_wrap() -> None:
    # Cluster lock: fastfall state (fp->fall_fast) should persist across AOBJ_LOOP wraps of the Fall
    # animation; wrapping cur_anim_frame does not imply a Fighter_ChangeMotionState call in decomp.
    #
    # Regression target: TBK rec 2645 p1 (Fall -> Fall) where ref.state_flags[1] stays 0x08 but the
    # sim previously cleared it on the wrap boundary.
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 2645
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 29  # Fall
    assert int(row["ref_t1"]["action_id"][0, p]) == 29
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert int(row["seed_t"]["fall_fast"][0, p]) == 1
    assert int(row["seed_t"]["state_flags"][0, p, 1]) == 8
    assert int(row["ref_t1"]["state_flags"][0, p, 1]) == 8

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got = int(out["state_flags"][0, p, 1])
        want = int(row["ref_t1"]["state_flags"][0, p, 1])
        assert got == want, f"record={record} p={p} expected state_flags[1]={want}, got {got}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_action_frame_landingairn_entry_rate_uses_decomp_lag_formula() -> None:
    # Cluster lock: LandingAirN entry must use the decomp anim-rate formula on entry-shaped
    # snapshots (`action_frame == 0`) so t->t+1 state_age/action_frame matches replay.
    #
    # Decomp:
    # - ftCo_LandingAir_EnterWithLag (lag + L-cancel divide gate)
    # - ftCo_LandingAir_EnterWithMsidLag ((ftAnim_8006F484 + 0.1f) / lag)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c
    #
    # Regression target: AGG rec 320 p1 (LandingAirN -> LandingAirN) where ref action_frame jumps
    # 0->4 but the sim previously advanced only to 3.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 320
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 70  # ftCo_MS_LandingAirN
    assert int(row["ref_t1"]["action_id"][0, p]) == 70
    assert int(row["seed_t"]["action_frame"][0, p]) == 0
    assert int(row["ref_t1"]["action_frame"][0, p]) == 4
    assert int(row["seed_t"]["animation_index"][0, p]) == 73  # ftCo_SM_LandingAirN
    assert int(row["ref_t1"]["animation_index"][0, p]) == 73
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert float(row["seed_t"]["frame_speed_mul_f32"][0, p]) == pytest.approx(3.3444445, rel=1e-6)

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_a = int(out["action_id"][0, p])
        want_a = int(row["ref_t1"]["action_id"][0, p])
        assert got_a == want_a, f"record={record} p={p} expected action_id={want_a}, got {got_a}"

        got_af = int(out["action_frame"][0, p])
        want_af = int(row["ref_t1"]["action_frame"][0, p])
        assert got_af == want_af, f"record={record} p={p} expected action_frame={want_af}, got {got_af}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_action_frame_attackair_entry_rate_resets_to_one() -> None:
    # Cluster lock: AttackAir entry is anim_speed=1.0 in decomp; stale carry-over rates from prior
    # landing states must not over-advance action_frame on the first steady AttackAir frame.
    #
    # Decomp: ftCo_AttackAir_EnterFromMsid -> Fighter_ChangeMotionState(..., anim_speed=1.0f)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_EnterFromMsid
    #
    # Regression target: AGG rec 285 p0 (AttackAirLw -> AttackAirLw), where ref action_frame is 2
    # but the sim previously advanced to 3 due stale frame_speed_mul.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 285
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 69  # ftCo_MS_AttackAirLw
    assert int(row["ref_t1"]["action_id"][0, p]) == 69
    assert int(row["seed_t"]["action_frame"][0, p]) == 1
    assert int(row["ref_t1"]["action_frame"][0, p]) == 2
    assert int(row["seed_t"]["animation_index"][0, p]) == 72  # ftCo_SM_AttackAirLw
    assert int(row["ref_t1"]["animation_index"][0, p]) == 72
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert float(row["seed_t"]["frame_speed_mul_f32"][0, p]) == pytest.approx(2.505, rel=1e-6)

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_a = int(out["action_id"][0, p])
        want_a = int(row["ref_t1"]["action_id"][0, p])
        assert got_a == want_a, f"record={record} p={p} expected action_id={want_a}, got {got_a}"

        got_af = int(out["action_frame"][0, p])
        want_af = int(row["ref_t1"]["action_frame"][0, p])
        assert got_af == want_af, f"record={record} p={p} expected action_frame={want_af}, got {got_af}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_action_frame_escapeair_entry_ticks_once() -> None:
    # Cluster lock: EscapeAir entry calls ftAnim_8006EBA4 immediately after ChangeMotionState, so
    # post-frame action_frame is 1 on the entry frame.
    #
    # Decomp: ftCo_80099A9C (EscapeAir entry)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099A9C
    #
    # Regression target: AGG rec 814 p0 (JumpAerialF -> EscapeAir), where ref action_frame is 1
    # but the sim previously emitted 0.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 814
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 26  # ftCo_MS_JumpB
    assert int(row["ref_t1"]["action_id"][0, p]) == 236  # ftCo_MS_EscapeAir
    assert int(row["seed_t"]["action_frame"][0, p]) == 0
    assert int(row["ref_t1"]["action_frame"][0, p]) == 1
    assert int(row["seed_t"]["animation_index"][0, p]) == 17
    assert int(row["ref_t1"]["animation_index"][0, p]) == 44
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert float(row["seed_t"]["frame_speed_mul_f32"][0, p]) == pytest.approx(1.0)

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
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_a = int(out["action_id"][0, p])
        want_a = int(row["ref_t1"]["action_id"][0, p])
        assert got_a == want_a, f"record={record} p={p} expected action_id={want_a}, got {got_a}"

        got_af = int(out["action_frame"][0, p])
        want_af = int(row["ref_t1"]["action_frame"][0, p])
        assert got_af == want_af, f"record={record} p={p} expected action_frame={want_af}, got {got_af}"
    finally:
        binding.destroy(handle)
