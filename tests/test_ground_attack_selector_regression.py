from __future__ import annotations

import json
import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_BASE_REL = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"
_BUTTON_A = 0x0100

_ACT_WAIT = 14
_ACT_WALK_SLOW = 15
_ACT_DASH = 20
_ACT_ATTACK_DASH = 50
_ACT_ATTACK_S3 = 53
_ACT_ATTACK_HI3 = 56
_ACT_ATTACK_LW3 = 57

_SM_ATTACK_DASH = 52

_REQUIRED_ARTIFACTS = (
    "data/common/ft_common_data.json",
    "data/moves/fox.json",
    "data/moves/falco.json",
    "data/anims/fox.tracks.bin",
    "data/anims/falco.tracks.bin",
)


def _size_key(sizes: dict[str, int], key: str) -> int:
    if key in sizes:
        return int(sizes[key])
    return int(sizes[f"{key}_v0"])


def _skip_if_required_artifacts_missing(root: Path) -> None:
    missing = [rel for rel in _REQUIRED_ARTIFACTS if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local extracted artifacts: {', '.join(missing)}")


def _load_common_params(root: Path) -> dict[str, float]:
    with (root / "data/common/ft_common_data.json").open("r", encoding="utf-8") as f:
        return json.load(f)


def _stick_i8_to_unit(v: int) -> float:
    clamped = max(-80, min(80, int(v)))
    return float(clamped) / 80.0


def _apply_deadzone(v: float, dz: float) -> float:
    return 0.0 if abs(v) < dz else v


def _a_press_edge(row: np.ndarray, port: int) -> bool:
    prev = int(row["prev_input_t"]["p"][0, port]["buttons"])
    cur = int(row["input_t"]["p"][0, port]["buttons"])
    return (cur & _BUTTON_A) != 0 and (prev & _BUTTON_A) == 0


def _tracks_end_frame(tracks_path: Path, msid: int) -> float:
    with tracks_path.open("rb") as f:
        magic = f.read(8)
        if magic != b"SSANIMT1":
            raise ValueError(f"bad tracks magic: {magic!r}")
        (version,) = struct.unpack("<I", f.read(4))
        if version != 3:
            raise ValueError(f"unsupported tracks version: {version}")
        local_count, anim_count = struct.unpack("<HH", f.read(4))
        f.read(local_count)  # local_parts
        f.read(2 * local_count)  # local_parent
        f.read(4 * local_count)  # local_flags

        for _ in range(anim_count):
            (mid,) = struct.unpack("<H", f.read(2))
            (end_frame,) = struct.unpack("<f", f.read(4))
            f.read(1)  # aobj_loop
            f.read(1)  # uses_root_motion
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


def _run_record(dataset_path: Path, record: int) -> tuple[np.ndarray, np.ndarray]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"
    row = samples[record : record + 1]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = _size_key(sizes, "seed")
    input_stride = _size_key(sizes, "input")
    compare_stride = _size_key(sizes, "compare")

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
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
        return out, row
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "p", "seed_action", "expected_action", "assert_action_frame", "require_a_edge"),
    [
        # AttackHi3 selection (baseline 56->14 cluster representative).
        ("AttachedGoodNaturedGuanaco.msl", 841, 0, _ACT_WAIT, _ACT_ATTACK_HI3, True, True),
        # AttackHi3 staying in-state.
        ("GracefulAttachedTurtle.msl", 498, 0, _ACT_ATTACK_HI3, _ACT_ATTACK_HI3, False, False),
        # AttackDash selection (baseline 50->20 cluster representative).
        ("AttachedGoodNaturedGuanaco.msl", 878, 0, _ACT_DASH, _ACT_ATTACK_DASH, True, True),
        # AttackDash staying in-state.
        ("AttachedGoodNaturedGuanaco.msl", 2321, 1, _ACT_ATTACK_DASH, _ACT_ATTACK_DASH, False, False),
        # Side tilt selection.
        ("QuerulousGrandDinosaur.msl", 6240, 1, _ACT_WALK_SLOW, _ACT_ATTACK_S3, True, True),
        # Down tilt staying in-state.
        ("TreasuredBackKangaroo.msl", 868, 0, _ACT_ATTACK_LW3, _ACT_ATTACK_LW3, False, False),
    ],
)
def test_ground_attack_selector_regression(
    dataset_name: str,
    record: int,
    p: int,
    seed_action: int,
    expected_action: int,
    assert_action_frame: bool,
    require_a_edge: bool,
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_BASE_REL}/{dataset_name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    params = _load_common_params(root)
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == seed_action
    assert int(row["ref_t1"]["action_id"][0, p]) == expected_action
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    if require_a_edge:
        assert _a_press_edge(row, p)

    stick_x = _apply_deadzone(
        _stick_i8_to_unit(int(row["input_t"]["p"][0, p]["main_x"])),
        float(params["lstick_deadzone_x"]),
    )
    stick_y = _apply_deadzone(
        _stick_i8_to_unit(int(row["input_t"]["p"][0, p]["main_y"])),
        float(params["lstick_deadzone_y"]),
    )
    facing_dir = 1.0 if int(row["seed_t"]["facing"][0, p]) != 0 else -1.0
    stick_ang = float(np.arctan2(stick_y, abs(stick_x)))

    if expected_action == _ACT_ATTACK_S3:
        assert (stick_x * facing_dir) >= float(params["attack_s3_stick_threshold_x"])
        assert abs(stick_ang) < float(params["attack_angle_threshold_radians"])
    elif expected_action == _ACT_ATTACK_HI3:
        assert stick_y >= float(params["attack_hi3_stick_threshold_y"])
        assert stick_ang > float(params["attack_angle_threshold_radians"])
    elif expected_action == _ACT_ATTACK_LW3:
        assert stick_y <= float(params["attack_lw3_stick_threshold_y"])
        assert stick_ang < -float(params["attack_angle_threshold_radians"])

    out, row_eval = _run_record(dataset_path, record)
    ref = row_eval["ref_t1"].reshape(-1)[0]

    assert int(out["action_id"][0, p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][0, p]) == int(ref["animation_index"][p])
    if assert_action_frame:
        assert int(out["action_frame"][0, p]) == int(ref["action_frame"][p])


@pytest.mark.integration
def test_attackdash_wait_iasa_dash_transition_agg_2351_p1() -> None:
    # Replay-real lock for the closed AttackDash -> Wait_IASA selector row:
    # - seed action is already AttackDash (50), no A-edge.
    # - ref expects Dash (20) at t+1.
    # - extracted Falco AttackDash end_frame (msid=52) is 40.0 while seed anim_frame_f32 is 37.0,
    #   so this row is not an anim-end off-by-one in the simple end-frame check itself.
    #
    # Decomp anchors:
    # - AttackDash Anim end gate: refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_Anim
    # - AttackDash IASA delegates into Wait IASA under allow_interrupt:
    #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = f"{_BASE_REL}/AttachedGoodNaturedGuanaco.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 2351
    p = 1
    row = ds.samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == _ACT_ATTACK_DASH
    assert int(row["ref_t1"]["action_id"][0, p]) == _ACT_DASH
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert not _a_press_edge(row, p)

    char_id = int(row["seed_t"]["char_id"][0, p])
    tracks_rel = "data/anims/fox.tracks.bin" if char_id == 1 else "data/anims/falco.tracks.bin"
    seed_anim_frame = float(row["seed_t"]["anim_frame_f32"][0, p])
    assert seed_anim_frame == pytest.approx(37.0)

    tracks_path = root / tracks_rel
    attackdash_end = _tracks_end_frame(tracks_path, _SM_ATTACK_DASH)
    assert attackdash_end == pytest.approx(40.0)
    assert seed_anim_frame < attackdash_end

    out, _ = _run_record(dataset_path, record)
    assert int(out["action_id"][0, p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["animation_index"][0, p]) == int(row["ref_t1"]["animation_index"][0, p])
