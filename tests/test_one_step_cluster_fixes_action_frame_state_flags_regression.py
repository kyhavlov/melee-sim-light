from __future__ import annotations

import json
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


def _landing_air_lag_frames_for_action(char_data: dict[str, object], action_id: int) -> int:
    by_action = {
        70: "landing_airn_lag_frames",  # ftCo_MS_LandingAirN
        71: "landing_airf_lag_frames",  # ftCo_MS_LandingAirF
        72: "landing_airb_lag_frames",  # ftCo_MS_LandingAirB
        73: "landing_airhi_lag_frames",  # ftCo_MS_LandingAirHi
        74: "landing_airlw_lag_frames",  # ftCo_MS_LandingAirLw
    }
    key = by_action.get(int(action_id))
    if key is None:
        raise AssertionError(f"expected LandingAir* action_id, got {action_id}")
    return int(char_data[key])


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
def test_damagefly_iasa_jump_buffer_parity_agg_1681_p1() -> None:
    # Cluster lock: DamageFly -> JumpAerial transition driven by DamageFall_IASA jump buffer.
    #
    # Decomp:
    # - doIasa snapshots mv.co.damage.x14 from x0 on jump input while x221C_b6 is set.
    # - DamageFly_IASA delegates to DamageFall_IASA once x221C_b6 clears.
    # - DamageFall_IASA jump path calls ftCo_800CB870.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doIasa,ftCo_DamageFly_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_800CB870
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
    record = 1681
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 90  # ftCo_MS_DamageFlyTop
    assert int(row["ref_t1"]["action_id"][0, p]) == 27  # ftCo_MS_JumpAerialF
    assert int(row["seed_t"]["animation_index"][0, p]) == 180  # ftCo_SM_DamageFlyTop
    assert int(row["ref_t1"]["animation_index"][0, p]) == 18  # ftCo_SM_JumpAerialF
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 1
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert int(row["seed_t"]["on_ground"][0, p]) == 0
    assert int(row["ref_t1"]["on_ground"][0, p]) == 0
    if int(row["seed_t"]["damage_jump_buffer_x14"][0, p]) == 0:
        pytest.skip(
            "stale cached dataset for damage_jump_buffer_x14; rebuild with "
            "preprocess_suite --force"
        )

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
        assert int(out["action_id"][0, p]) == int(row["ref_t1"]["action_id"][0, p])
        assert int(out["action_frame"][0, p]) == int(row["ref_t1"]["action_frame"][0, p])
        assert int(out["animation_index"][0, p]) == int(row["ref_t1"]["animation_index"][0, p])
        assert int(out["jumps_left"][0, p]) == int(row["ref_t1"]["jumps_left"][0, p])
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
def test_state_flags_x221a_b3_clears_when_hitlag_reaches_zero() -> None:
    # Cluster lock: fp->x221A_b3 (state_flags[1] bit 0x10) clears on the frame hitlag reaches 0.
    #
    # Decomp clear path:
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
    #
    # Regression target: AGG rec 182 p1 where seed carries 0x10 with hitlag=1 but ref clears to 0
    # with hitlag=0 on the next frame.
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
    record = 182
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 75  # ftCo_MS_DamageHi1
    assert int(row["ref_t1"]["action_id"][0, p]) == 75
    assert int(row["seed_t"]["hitlag"][0, p]) == 1
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["state_flags"][0, p, 1]) & 0x10
    assert (int(row["ref_t1"]["state_flags"][0, p, 1]) & 0x10) == 0

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
        got_flags = int(out["state_flags"][0, p, 1])
        want_flags = int(row["ref_t1"]["state_flags"][0, p, 1])
        assert got_flags == want_flags, (
            f"record={record} p={p} expected state_flags[1]={want_flags}, got {got_flags}"
        )
        assert (got_flags & 0x10) == 0, f"record={record} p={p} expected x221A_b3 clear, got {got_flags:#04x}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_state_flags_x221b_b5_set_on_catchpull_entry() -> None:
    # Cluster lock: fp->x221B_b5 (state_flags[2] bit 0x04) sets when catch collision acquires a
    # victim and transitions Catch -> CatchPull.
    #
    # Decomp:
    # - set on acquire: refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftGrabDist}
    # - Slippi packing: refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (0x221B byte)
    #
    # Regression target: AGG rec 203 p1 where ref.state_flags[2] == 0x04 on CatchPull entry.
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
    record = 203
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 212  # Catch
    assert int(row["ref_t1"]["action_id"][0, p]) == 213  # CatchPull
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert int(row["seed_t"]["state_flags"][0, p, 2]) == 0
    assert int(row["ref_t1"]["state_flags"][0, p, 2]) == 4

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
        got = int(out["state_flags"][0, p, 2])
        want = int(row["ref_t1"]["state_flags"][0, p, 2])
        assert got == want, f"record={record} p={p} expected state_flags[2]={want}, got {got}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_catchpull_connect_with_guard_no_submotion_victim() -> None:
    # Cluster lock: Catch connect should still transition Catch->CatchPull when the defender is in
    # Guard with Slippi's no-submotion snapshot shape (animation_index = 0xFFFFFFFF).
    #
    # Decomp:
    # - Catch connect + victim acquire: refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
    # - Guard/GuardReflect motion-state mapping: refs/melee/src/melee/ft/ftmotionstates.c
    #
    # Regression target: AGG rec 4155 p1 where ref transitions:
    # - owner: Catch (212) -> CatchPull (213), and
    # - victim: Guard (179, animation_index=-1) -> CapturePulledLw (226).
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
    record = 4155
    owner_p = 1
    victim_p = 0
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, owner_p]) == 212  # Catch
    assert int(row["ref_t1"]["action_id"][0, owner_p]) == 213  # CatchPull
    assert int(row["seed_t"]["hitlag"][0, owner_p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, owner_p]) == 0
    assert int(row["seed_t"]["hitstun"][0, owner_p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, owner_p]) == 0
    assert int(row["seed_t"]["state_flags"][0, owner_p, 2]) == 0
    assert int(row["ref_t1"]["state_flags"][0, owner_p, 2]) == 4

    assert int(row["seed_t"]["action_id"][0, victim_p]) == 179  # Guard
    assert int(row["seed_t"]["animation_index"][0, victim_p]) == 0xFFFFFFFF
    assert int(row["ref_t1"]["action_id"][0, victim_p]) == 226  # CapturePulledLw
    assert int(row["seed_t"]["hitlag"][0, victim_p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, victim_p]) == 0
    assert int(row["seed_t"]["hitstun"][0, victim_p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, victim_p]) == 0

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
        got_owner_action = int(out["action_id"][0, owner_p])
        got_owner_flag = int(out["state_flags"][0, owner_p, 2])
        got_victim_action = int(out["action_id"][0, victim_p])
        want_owner_action = int(row["ref_t1"]["action_id"][0, owner_p])
        want_owner_flag = int(row["ref_t1"]["state_flags"][0, owner_p, 2])
        want_victim_action = int(row["ref_t1"]["action_id"][0, victim_p])

        assert got_owner_action == want_owner_action, (
            f"record={record} p={owner_p} expected owner action_id={want_owner_action}, "
            f"got {got_owner_action}"
        )
        assert got_owner_flag == want_owner_flag, (
            f"record={record} p={owner_p} expected owner state_flags[2]={want_owner_flag}, "
            f"got {got_owner_flag}"
        )
        assert got_victim_action == want_victim_action, (
            f"record={record} p={victim_p} expected victim action_id={want_victim_action}, "
            f"got {got_victim_action}"
        )
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_guardreflect_no_submotion_seed_eq_ref_stays_stable_gat_1289() -> None:
    # Seed==ref lock (GAT rec 1289 p0): GuardReflect no-submotion snapshot must not synthesize a
    # BODY hit that flips into Damage and introduces new action_id/hitlag/hitstun mismatches.
    #
    # Slippi no-submotion snapshot source:
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    # Shield descriptor ownership:
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = 1289
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 182  # GuardReflect
    assert int(row["ref_t1"]["action_id"][0, p]) == 182
    assert int(row["seed_t"]["animation_index"][0, p]) == 0xFFFFFFFF
    assert int(row["ref_t1"]["animation_index"][0, p]) == 0xFFFFFFFF
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

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
        assert int(out["action_id"][0, p]) == int(row["ref_t1"]["action_id"][0, p])
        assert int(out["hitlag"][0, p]) == int(row["ref_t1"]["hitlag"][0, p])
        assert int(out["hitstun"][0, p]) == int(row["ref_t1"]["hitstun"][0, p])
        assert int(out["animation_index"][0, p]) == int(row["ref_t1"]["animation_index"][0, p])
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_guardreflect_no_submotion_no_new_hitstun_tbk_3610() -> None:
    # Seed==ref lock (TBK rec 3610 p1): this row should not gain a new hitstun mismatch from a
    # synthesized GuardReflect no-submotion BODY contact.
    #
    # Note: action_id/hitlag here are pre-existing non-seed==ref mismatches in the branch base.
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
    record = 3610
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 182  # GuardReflect
    assert int(row["seed_t"]["animation_index"][0, p]) == 0xFFFFFFFF
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

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
        assert int(out["hitstun"][0, p]) == int(row["ref_t1"]["hitstun"][0, p])
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_state_flags_x221b_b5_clears_after_throw_release() -> None:
    # Cluster lock: fp->x221B_b5 (state_flags[2] bit 0x04) clears when throw release drops
    # victim_gobj ownership.
    #
    # Decomp clear path: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    #
    # Regression target: AGG rec 573 p0 where seed.state_flags[2] has 0x04 but ref clears to 0.
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
    record = 573
    p = 0
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 221  # ThrowHi
    assert int(row["ref_t1"]["action_id"][0, p]) == 221
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert int(row["seed_t"]["state_flags"][0, p, 2]) == 4
    assert int(row["ref_t1"]["state_flags"][0, p, 2]) == 0

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
        got = int(out["state_flags"][0, p, 2])
        want = int(row["ref_t1"]["state_flags"][0, p, 2])
        assert got == want, f"record={record} p={p} expected state_flags[2]={want}, got {got}"
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
    # Cluster lock: LandingAir* entry on L-cancel must use the divided-lag decomp branch on
    # entry-shaped snapshots (`action_frame == 0`) so t->t+1 state_age/action_frame matches replay.
    #
    # Decomp:
    # - ftCo_LandingAir_EnterWithLag (lag + L-cancel divide gate)
    # - ftCo_LandingAir_EnterWithMsidLag ((ftAnim_8006F484 + 0.1f) / lag)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c
    # Slippi seed-bridge field used by this lock:
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (`l_cancel` from x67F gate)
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
    assert int(row["seed_t"]["l_cancel"][0, p]) == 1

    char_id = int(row["seed_t"]["char_id"][0, p])
    char_name = "fox" if char_id == 1 else "falco"
    char_path = root / f"data/characters/{char_name}.json"
    assert char_path.exists(), f"missing local data artifact: {char_path}"
    char_data = json.loads(char_path.read_text())

    tracks_rel = "data/anims/fox.tracks.bin" if char_id == 1 else "data/anims/falco.tracks.bin"
    tracks_path = root / tracks_rel
    assert tracks_path.exists(), f"missing local tracks: {tracks_rel}"

    common_path = root / "data/common/ft_common_data.json"
    assert common_path.exists(), "missing local data artifact: data/common/ft_common_data.json"
    common = json.loads(common_path.read_text())

    end_frame = _tracks_end_frame(tracks_path, int(row["seed_t"]["animation_index"][0, p]))
    lag_frames = _landing_air_lag_frames_for_action(char_data, int(row["seed_t"]["action_id"][0, p]))
    div_rate = (float(end_frame) + 0.1) / (float(lag_frames) / float(common["lcancel_lag_div"]))
    no_div_rate = (float(end_frame) + 0.1) / float(lag_frames)
    seed_anim = float(row["seed_t"]["anim_frame_f32"][0, p])
    expected_div_action_frame = int(np.floor(seed_anim + div_rate))
    expected_no_div_action_frame = int(np.floor(seed_anim + no_div_rate))
    want_af = int(row["ref_t1"]["action_frame"][0, p])
    assert expected_div_action_frame == want_af, (
        f"record={record} p={p} expected divided-lag action_frame={expected_div_action_frame}, "
        f"ref has {want_af}"
    )
    assert expected_no_div_action_frame != want_af, (
        f"record={record} p={p} expected non-divided action_frame={expected_no_div_action_frame} "
        f"to differ from ref action_frame={want_af}"
    )

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
def test_action_frame_guard_setoff_hitlag_tail_uses_entry_rate() -> None:
    # Cluster lock: GuardSetOff entry seeds a non-1 anim rate from shield-hit internals, and that
    # rate must persist through hitlag tail so the first non-hitlag GuardSetOff frame lands on the
    # replay action_frame.
    #
    # Decomp:
    # - GuardSetOff entry anim-rate formula:
    #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    # - Hitlag gate (rate preserved while anim tick is frozen):
    #   refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    #
    # Regression target: AGG rec 924 p1 (GuardSetOff -> GuardSetOff), where ref action_frame is 4
    # after hitlag 1->0 but the sim previously emitted 1 with a stale entry rate of 1.0.
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
    record = 924
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 181  # GuardSetOff
    assert int(row["ref_t1"]["action_id"][0, p]) == 181
    assert int(row["seed_t"]["action_frame"][0, p]) == 0
    assert int(row["ref_t1"]["action_frame"][0, p]) == 4
    assert int(row["seed_t"]["hitlag"][0, p]) == 1
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    if record <= 0:
        pytest.skip("record index must be >0 for GuardSetOff entry-rate stale-cache check")
    act_guard_set_off = 181
    entry_record = record
    while entry_record > 0 and int(samples["seed_t"]["action_id"][entry_record - 1, p]) == act_guard_set_off:
        entry_record -= 1
    if entry_record <= 0:
        pytest.skip("unable to locate GuardSetOff entry frame for stale-cache check")
    entry_row = samples[entry_record : entry_record + 1]
    entry_prev_row = samples[entry_record - 1 : entry_record]

    common_path = root / "data/common/ft_common_data.json"
    if not common_path.exists():
        pytest.skip("missing local data artifact: data/common/ft_common_data.json")
    common = json.loads(common_path.read_text())

    char_id = int(row["seed_t"]["char_id"][0, p])
    tracks_rel = "data/anims/fox.tracks.bin" if char_id == 1 else "data/anims/falco.tracks.bin"
    tracks_path = root / tracks_rel
    if not tracks_path.exists():
        pytest.skip(f"missing local tracks: {tracks_rel}")

    msid = int(row["seed_t"]["animation_index"][0, p] & np.uint32(0xFFFF))
    end_frame = _tracks_end_frame(tracks_path, msid)

    seed_rate = float(row["seed_t"]["frame_speed_mul_f32"][0, p])
    shield_drop = float(entry_prev_row["seed_t"]["shield_hp"][0, p] - entry_row["seed_t"]["shield_hp"][0, p])
    lightshield_amount = float(entry_row["seed_t"]["lightshield_amount"][0, p])
    lightshield_amount = max(0.0, min(1.0, lightshield_amount))

    shield_hit_light_term = lightshield_amount * (
        float(common["shield_hit_lightshield_max"]) - float(common["shield_hit_lightshield_min"])
    ) + float(common["shield_hit_lightshield_min"])
    shield_hit_den = float(common["shield_hit_damage_mul"]) * (1.0 - shield_hit_light_term)
    if shield_drop <= 0.0 or shield_hit_den <= 0.0:
        pytest.skip("stale cached datasets for GuardSetOff frame_speed_mul (invalid entry shield-drop context)")

    shield_damage_taken = (shield_drop - float(common["shield_hit_damage_base"])) / shield_hit_den
    if shield_damage_taken < 0.0:
        shield_damage_taken = 0.0
    int_dmg_est = float(np.trunc(shield_damage_taken))

    shield_stun_light_term = lightshield_amount * (
        float(common["shield_stun_lightshield_max"]) - float(common["shield_stun_lightshield_min"])
    ) + float(common["shield_stun_lightshield_min"])
    setoff_f = float(common["shield_stun_mul"]) * (int_dmg_est * (1.0 - shield_stun_light_term)) + float(
        common["shield_stun_base"]
    )
    if setoff_f <= 0.0:
        pytest.skip("stale cached datasets for GuardSetOff frame_speed_mul (invalid entry setoff duration)")

    # Decomp ftCo_80092F2C uses (end_frame + 0.1f)/f for GuardSetOff anim rate.
    expected_seed_rate = (float(end_frame) + 0.1) / setoff_f
    # NOTE(schema): this lock expects datasets rebuilt with `preprocess_suite --force` after
    # Guard/seed-derivation schema updates (for example, guard_reflect_timer_x18).
    if not np.isclose(seed_rate, expected_seed_rate, rtol=1e-6, atol=1e-6):
        pytest.skip(
            "stale cached datasets for GuardSetOff frame_speed_mul; rebuild with "
            "`uv run python -m tools.slippi.preprocess_suite --suite replays/suites/fox_falco_fd_ucf084_recent.json "
            "--datasets-dir datasets --force`"
        )

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
def test_specialhi_fall_landing_wait_transitions_qgd() -> None:
    # Cluster lock: SpecialHi end-state parity on QuerulousGrandDinosaur.
    #
    # Decomp:
    # - SpecialHiFall_Coll enters SpecialHiLanding with anim_start=13 and immediate anim tick.
    # - SpecialHiLanding_Anim transitions to Wait on anim end.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiFall_Coll,ftFx_SpecialHiFall_Enter,ftFx_SpecialHiLanding_Anim
    # }
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples

    # rec 5895 p1: SpecialHiFall (air) -> SpecialHiLanding (ground).
    record_fall_to_landing = 5895
    p = 1
    assert int(samples.shape[0]) > record_fall_to_landing, (
        f"dataset too short: num_records={int(samples.shape[0])}"
    )
    row = samples[record_fall_to_landing : record_fall_to_landing + 1]
    assert int(row["seed_t"]["action_id"][0, p]) == 358  # ftFx_MS_SpecialHiFall
    assert int(row["ref_t1"]["action_id"][0, p]) == 357  # ftFx_MS_SpecialHiLanding
    assert int(row["seed_t"]["animation_index"][0, p]) == 311  # ftFx_SM_SpecialHiFall
    assert int(row["ref_t1"]["animation_index"][0, p]) == 310  # ftFx_SM_SpecialHiLanding
    assert int(row["seed_t"]["on_ground"][0, p]) == 0
    assert int(row["ref_t1"]["on_ground"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

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
        got_af = int(out["action_frame"][0, p])
        got_anim = int(out["animation_index"][0, p])
        got_ground = int(out["on_ground"][0, p])
        want_a = int(row["ref_t1"]["action_id"][0, p])
        want_af = int(row["ref_t1"]["action_frame"][0, p])
        want_anim = int(row["ref_t1"]["animation_index"][0, p])
        want_ground = int(row["ref_t1"]["on_ground"][0, p])
        assert got_a == want_a, (
            f"record={record_fall_to_landing} p={p} expected action_id={want_a}, got {got_a}"
        )
        assert got_af == want_af, (
            f"record={record_fall_to_landing} p={p} expected action_frame={want_af}, got {got_af}"
        )
        assert got_anim == want_anim, (
            f"record={record_fall_to_landing} p={p} expected animation_index={want_anim}, got {got_anim}"
        )
        assert got_ground == want_ground, (
            f"record={record_fall_to_landing} p={p} expected on_ground={want_ground}, got {got_ground}"
        )

        # rec 5901 p1: SpecialHiLanding -> Wait on anim end.
        record_landing_to_wait = 5901
        assert int(samples.shape[0]) > record_landing_to_wait, (
            f"dataset too short: num_records={int(samples.shape[0])}"
        )
        row2 = samples[record_landing_to_wait : record_landing_to_wait + 1]
        assert int(row2["seed_t"]["action_id"][0, p]) == 357  # ftFx_MS_SpecialHiLanding
        assert int(row2["ref_t1"]["action_id"][0, p]) == 14  # ftCo_MS_Wait
        assert int(row2["seed_t"]["hitlag"][0, p]) == 0
        assert int(row2["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row2["seed_t"]["hitstun"][0, p]) == 0
        assert int(row2["ref_t1"]["hitstun"][0, p]) == 0

        seed_bytes[:] = np.frombuffer(row2["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row2["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row2["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out2 = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

        got_a2 = int(out2["action_id"][0, p])
        got_af2 = int(out2["action_frame"][0, p])
        got_anim2 = int(out2["animation_index"][0, p])
        want_a2 = int(row2["ref_t1"]["action_id"][0, p])
        want_af2 = int(row2["ref_t1"]["action_frame"][0, p])
        want_anim2 = int(row2["ref_t1"]["animation_index"][0, p])
        assert got_a2 == want_a2, (
            f"record={record_landing_to_wait} p={p} expected action_id={want_a2}, got {got_a2}"
        )
        assert got_af2 == want_af2, (
            f"record={record_landing_to_wait} p={p} expected action_frame={want_af2}, got {got_af2}"
        )
        assert got_anim2 == want_anim2, (
            f"record={record_landing_to_wait} p={p} expected animation_index={want_anim2}, got {got_anim2}"
        )
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_specialhi_fall_landing_wait_transitions_tbk() -> None:
    # Same lock as test_specialhi_fall_landing_wait_transitions_qgd on a second dataset.
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
    p = 0

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

        # rec 7123 p0: SpecialHiFall (air) -> SpecialHiLanding (ground).
        record_fall_to_landing = 7123
        assert int(samples.shape[0]) > record_fall_to_landing, (
            f"dataset too short: num_records={int(samples.shape[0])}"
        )
        row = samples[record_fall_to_landing : record_fall_to_landing + 1]
        assert int(row["seed_t"]["action_id"][0, p]) == 358  # ftFx_MS_SpecialHiFall
        assert int(row["ref_t1"]["action_id"][0, p]) == 357  # ftFx_MS_SpecialHiLanding
        assert int(row["seed_t"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row["seed_t"]["hitstun"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 0

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
        got_af = int(out["action_frame"][0, p])
        got_anim = int(out["animation_index"][0, p])
        got_ground = int(out["on_ground"][0, p])
        want_a = int(row["ref_t1"]["action_id"][0, p])
        want_af = int(row["ref_t1"]["action_frame"][0, p])
        want_anim = int(row["ref_t1"]["animation_index"][0, p])
        want_ground = int(row["ref_t1"]["on_ground"][0, p])
        assert got_a == want_a, (
            f"record={record_fall_to_landing} p={p} expected action_id={want_a}, got {got_a}"
        )
        assert got_af == want_af, (
            f"record={record_fall_to_landing} p={p} expected action_frame={want_af}, got {got_af}"
        )
        assert got_anim == want_anim, (
            f"record={record_fall_to_landing} p={p} expected animation_index={want_anim}, got {got_anim}"
        )
        assert got_ground == want_ground, (
            f"record={record_fall_to_landing} p={p} expected on_ground={want_ground}, got {got_ground}"
        )

        # rec 7129 p0: SpecialHiLanding -> Wait on anim end.
        record_landing_to_wait = 7129
        assert int(samples.shape[0]) > record_landing_to_wait, (
            f"dataset too short: num_records={int(samples.shape[0])}"
        )
        row2 = samples[record_landing_to_wait : record_landing_to_wait + 1]
        assert int(row2["seed_t"]["action_id"][0, p]) == 357  # ftFx_MS_SpecialHiLanding
        assert int(row2["ref_t1"]["action_id"][0, p]) == 14  # ftCo_MS_Wait
        assert int(row2["seed_t"]["hitlag"][0, p]) == 0
        assert int(row2["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row2["seed_t"]["hitstun"][0, p]) == 0
        assert int(row2["ref_t1"]["hitstun"][0, p]) == 0

        seed_bytes[:] = np.frombuffer(row2["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row2["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row2["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out2 = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

        got_a2 = int(out2["action_id"][0, p])
        got_af2 = int(out2["action_frame"][0, p])
        got_anim2 = int(out2["animation_index"][0, p])
        want_a2 = int(row2["ref_t1"]["action_id"][0, p])
        want_af2 = int(row2["ref_t1"]["action_frame"][0, p])
        want_anim2 = int(row2["ref_t1"]["animation_index"][0, p])
        assert got_a2 == want_a2, (
            f"record={record_landing_to_wait} p={p} expected action_id={want_a2}, got {got_a2}"
        )
        assert got_af2 == want_af2, (
            f"record={record_landing_to_wait} p={p} expected action_frame={want_af2}, got {got_af2}"
        )
        assert got_anim2 == want_anim2, (
            f"record={record_landing_to_wait} p={p} expected animation_index={want_anim2}, got {got_anim2}"
        )
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


@pytest.mark.integration
def test_action_id_catch_wait_throw_entry_uses_x98_threshold_lane() -> None:
    # Cluster lock: CatchWait throw selection should use the ftCommonData x98 stick lane
    # (attack_s3_stick_threshold_x), allowing CatchWait->ThrowF and CaptureWaitLw->ThrownF
    # transitions on this replay-real row.
    #
    # Decomp:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD1E4
    # - refs/melee/src/melee/ft/ft_0DF1.c::ftCo_800DF7F4
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
    record = 206
    owner_p = 1
    victim_p = 0
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, owner_p]) == 216  # CatchWait
    assert int(row["seed_t"]["action_id"][0, victim_p]) == 227  # CaptureWaitLw
    assert int(row["ref_t1"]["action_id"][0, owner_p]) == 219  # ThrowF
    assert int(row["ref_t1"]["action_id"][0, victim_p]) == 239  # ThrownF
    assert int(row["seed_t"]["hitlag"][0, owner_p]) == 0
    assert int(row["seed_t"]["hitlag"][0, victim_p]) == 0
    assert int(row["seed_t"]["hitstun"][0, owner_p]) == 0
    assert int(row["seed_t"]["hitstun"][0, victim_p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, owner_p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, victim_p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, owner_p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, victim_p]) == 0

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
        assert int(out["action_id"][0, owner_p]) == int(row["ref_t1"]["action_id"][0, owner_p])
        assert int(out["action_id"][0, victim_p]) == int(row["ref_t1"]["action_id"][0, victim_p])
        assert int(out["action_frame"][0, owner_p]) == int(row["ref_t1"]["action_frame"][0, owner_p])
        assert int(out["action_frame"][0, victim_p]) == int(row["ref_t1"]["action_frame"][0, victim_p])
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            6465,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            4593,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            7456,
            0,
        ),
    ],
    ids=["agg_r6465_p0", "gat_r4593_p1", "qgd_r7456_p0"],
)
def test_damagefly_hitlag_exit_rows_stay_airborne(dataset_rel: str, record: int, p: int) -> None:
    # Cluster lock: these replay-real seed==ref rows were regressed to early on_ground=1 while in
    # DamageFlyN hitstun-exit ordering. Keep them airborne on t+1 with matching ground_id/action_id.
    # Regression cause/fix: generic floor resting-contact fallback under hitstun produced 0/0/1;
    # runtime fix gates that fallback to hitstun==0 so DamageFlyN callback ownership remains intact.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    # Replay-real preconditions for this cluster.
    assert int(row["seed_t"]["action_id"][0, p]) == 88  # ftCo_MS_DamageFlyN
    assert int(row["ref_t1"]["action_id"][0, p]) == 88
    assert int(row["seed_t"]["damage_post_hitlag_cb_kind"][0, p]) == 1
    assert int(row["ref_t1"]["on_ground"][0, p]) == 0
    assert int(row["seed_t"]["hitlag"][0, p]) == 1
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 32
    assert int(row["ref_t1"]["hitstun"][0, p]) == 31

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

        assert int(out["action_id"][0, p]) == int(row["ref_t1"]["action_id"][0, p])
        assert int(out["on_ground"][0, p]) == 0
        assert int(out["ground_id"][0, p]) == int(row["ref_t1"]["ground_id"][0, p])
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_hitlag", "ref_hitlag", "seed_hitstun", "ref_hitstun"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            4553,
            0,
            1,
            0,
            32,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            4651,
            0,
            1,
            0,
            41,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            482,
            1,
            1,
            0,
            43,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            352,
            1,
            1,
            0,
            32,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            6466,
            0,
            0,
            0,
            31,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            7457,
            0,
            0,
            0,
            31,
            0,
        ),
    ],
    ids=[
        "agg_r4553_p0",
        "gat_r4651_p0",
        "qgd_r482_p1",
        "tbk_r352_p1",
        "agg_r6466_p0",
        "qgd_r7457_p0",
    ],
)
def test_action_id_damagefly_to_downbound_entry_rows(
    dataset_rel: str,
    record: int,
    p: int,
    seed_hitlag: int,
    ref_hitlag: int,
    seed_hitstun: int,
    ref_hitstun: int,
) -> None:
    # Dirty-seed hitlag/hitstun-exit landing cluster:
    # - replay rows where seed still carries hitlag/hitstun (seed != ref on those timers),
    # - but ref_t1 has already transitioned to DownBound.
    # This is not a seed==ref correctness lock; it guards ordering behavior on reseed-dirty rows.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    # Replay-real strict preconditions.
    assert int(row["seed_t"]["action_id"][0, p]) == 88  # ftCo_MS_DamageFlyN
    assert int(row["ref_t1"]["action_id"][0, p]) == 183  # ftCo_MS_DownBoundU
    assert int(row["seed_t"]["damage_post_hitlag_cb_kind"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == seed_hitlag
    assert int(row["ref_t1"]["hitlag"][0, p]) == ref_hitlag
    assert int(row["seed_t"]["hitstun"][0, p]) == seed_hitstun
    assert int(row["ref_t1"]["hitstun"][0, p]) == ref_hitstun
    assert int(row["ref_t1"]["on_ground"][0, p]) == 1

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

        assert int(out["action_id"][0, p]) == int(row["ref_t1"]["action_id"][0, p])
        assert int(out["animation_index"][0, p]) == int(row["ref_t1"]["animation_index"][0, p])
        assert int(out["on_ground"][0, p]) == int(row["ref_t1"]["on_ground"][0, p])
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "ref_action_id"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            2095,
            0,
            183,  # ftCo_MS_DownBoundU
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            2316,
            1,
            183,  # ftCo_MS_DownBoundU
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            5287,
            1,
            199,  # ftCo_MS_Passive
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            1156,
            1,
            199,  # ftCo_MS_Passive
        ),
    ],
    ids=["agg_r2095_p0", "gat_r2316_p1", "qgd_r5287_p1", "tbk_r1156_p1"],
)
def test_action_id_damageflyhi_hitlag_exit_ground_entry_rows(
    dataset_rel: str,
    record: int,
    p: int,
    ref_action_id: int,
) -> None:
    # Dirty-seed hitlag-exit landing cluster for DamageFlyHi:
    # - seed carries hitlag/hitstun while replay t+1 has already entered grounded follow-up
    #   (DownBound/Passive).
    # - this locks callback-ownership ordering for DamageFly* collision resolution.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 87  # ftCo_MS_DamageFlyHi
    assert int(row["ref_t1"]["action_id"][0, p]) == ref_action_id
    assert int(row["seed_t"]["hitlag"][0, p]) == 1
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) > 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert int(row["seed_t"]["on_ground"][0, p]) == 0
    assert int(row["ref_t1"]["on_ground"][0, p]) == 1

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

        assert int(out["action_id"][0, p]) == int(row["ref_t1"]["action_id"][0, p])
        assert int(out["animation_index"][0, p]) == int(row["ref_t1"]["animation_index"][0, p])
        assert int(out["on_ground"][0, p]) == int(row["ref_t1"]["on_ground"][0, p])
        assert int(out["ground_id"][0, p]) == int(row["ref_t1"]["ground_id"][0, p])
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            6468,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            484,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
            "cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            8152,
            1,
        ),
    ],
    ids=["agg_r6468_p0", "qgd_r484_p1", "qgd_r8152_p1"],
)
def test_action_id_downboundu_ledge_release_to_fall_rows(dataset_rel: str, record: int, p: int) -> None:
    # Ledge-release ownership lock for DownBoundU:
    # - decomp ftCo_DownBound_Coll uses ft_80082708 (mpColl_8004B108 path), not the generic
    #   ft_800827A0/mpColl_8004B2DC edge-snap family.
    # - these replay-real rows should release into Fall (29) on t+1 at the ledge edge.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082708,ft_800827A0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == 183  # ftCo_MS_DownBoundU
    assert int(row["ref_t1"]["action_id"][0, p]) == 29  # ftCo_MS_Fall
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

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

        assert int(out["action_id"][0, p]) == int(row["ref_t1"]["action_id"][0, p])
        assert int(out["animation_index"][0, p]) == int(row["ref_t1"]["animation_index"][0, p])
        assert int(out["on_ground"][0, p]) == int(row["ref_t1"]["on_ground"][0, p])
        assert int(out["ground_id"][0, p]) == int(row["ref_t1"]["ground_id"][0, p])
    finally:
        binding.destroy(handle)
