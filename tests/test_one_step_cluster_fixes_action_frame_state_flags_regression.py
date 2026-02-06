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

