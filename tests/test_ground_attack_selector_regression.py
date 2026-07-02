from __future__ import annotations

import json
import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


_BASE_REL = "replays/validation/cardinal_1.0_recent"
_MARTH_BASE_REL = "replays/validation/marth"
_BUTTON_A = 0x0100

_ACT_WAIT = 14
_ACT_WALK_SLOW = 15
_ACT_DASH = 20
_ACT_LANDING = 42
_ACT_ATTACK_DASH = 50
_ACT_ATTACK_S3_HI = 51
_ACT_ATTACK_S3 = 53
_ACT_ATTACK_HI3 = 56
_ACT_ATTACK_LW3 = 57
_ACT_GUARD_ON = 178

_SM_ATTACK_DASH = 52
_SM_ATTACK_S3_HI = 53
_SM_ATTACK_S3 = 55

_REQUIRED_ARTIFACTS = (
    "data/common/ft_common_data.json",
    "data/moves/fox.json",
    "data/moves/falco.json",
    "data/moves/marth.json",
    "data/anims/fox.tracks.bin",
    "data/anims/falco.tracks.bin",
    "data/anims/marth.tracks.bin",
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
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    num_records = int(samples.shape[0])
    assert num_records > record, f"replay too short for regression check: num_records={num_records}"
    row = samples[record : record + 1]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = _size_key(sizes, "seed")
    input_stride = _size_key(sizes, "input")
    compare_stride = _size_key(sizes, "compare")

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
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
        ("AttachedGoodNaturedGuanaco.slpz", 841, 0, _ACT_WAIT, _ACT_ATTACK_HI3, True, True),
        # AttackHi3 staying in-state.
        ("GracefulAttachedTurtle.slpz", 498, 0, _ACT_ATTACK_HI3, _ACT_ATTACK_HI3, False, False),
        # AttackDash selection (baseline 50->20 cluster representative).
        ("AttachedGoodNaturedGuanaco.slpz", 878, 0, _ACT_DASH, _ACT_ATTACK_DASH, True, True),
        # AttackDash staying in-state.
        ("AttachedGoodNaturedGuanaco.slpz", 2321, 1, _ACT_ATTACK_DASH, _ACT_ATTACK_DASH, False, False),
        # Side tilt selection.
        ("QuerulousGrandDinosaur.slpz", 6240, 1, _ACT_WALK_SLOW, _ACT_ATTACK_S3, True, True),
        # Down tilt staying in-state.
        ("TreasuredBackKangaroo.slpz", 868, 0, _ACT_ATTACK_LW3, _ACT_ATTACK_LW3, False, False),
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
        pytest.skip(f"missing local replay: {dataset_rel}")

    params = _load_common_params(root)
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]

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
def test_marth_attacks3_angle_falls_back_to_neutral_when_variant_anim_missing_sdw_7895() -> None:
    # Marth exposes common AttackS3* MotionState rows, but only neutral AttackS3S has a source
    # animation in SSANIMT1. Source `decideAngle` gates angled side-tilt branches on the requested
    # motion pointer before changing state, so this Landing IASA row must fall through to neutral
    # AttackS3S even though the current stick angle is above the high-side threshold.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::decideAngle
    # data/anims/marth.tracks.bin::SSANIMT1
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_MARTH_BASE_REL}/StiffDraftyWalrus.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    assert _tracks_end_frame(root / "data/anims/marth.tracks.bin", _SM_ATTACK_S3_HI) == pytest.approx(
        0.0
    )
    assert _tracks_end_frame(root / "data/anims/marth.tracks.bin", _SM_ATTACK_S3) > 0.0

    ds = load_replay_buffers(str(dataset_path))
    record = 7895
    p = 0
    row = ds.rows[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, p]) == _ACT_LANDING
    assert int(row["ref_t1"]["action_id"][0, p]) == _ACT_ATTACK_S3
    assert _a_press_edge(row, p)

    params = _load_common_params(root)
    stick_x = _apply_deadzone(
        _stick_i8_to_unit(int(row["input_t"]["p"][0, p]["main_x"])),
        float(params["lstick_deadzone_x"]),
    )
    stick_y = _apply_deadzone(
        _stick_i8_to_unit(int(row["input_t"]["p"][0, p]["main_y"])),
        float(params["lstick_deadzone_y"]),
    )
    assert float(np.arctan2(stick_y, abs(stick_x))) > float(params["attack_s3_hi_angle_radians"])

    out, _ = _run_record(dataset_path, record)
    assert int(out["action_id"][0, p]) == _ACT_ATTACK_S3
    assert int(out["animation_index"][0, p]) == _SM_ATTACK_S3


@pytest.mark.integration
def test_spacie_attacks3_angle_still_uses_available_hi_variant() -> None:
    # Adjacent data-backed negative: Fox/Falco have extracted SSANIMT1 tracks for the angled
    # AttackS3 variants, so the source-motion availability gate must not collapse them to neutral.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    assert _tracks_end_frame(root / "data/anims/fox.tracks.bin", _SM_ATTACK_S3_HI) > 0.0

    from tests.test_locomotion import (
        ACT_TURN,
        BUTTON_A,
        INPUT_DTYPE,
        SM_TURN,
        _mk_input_bytes,
        _seed_base,
        _step_once,
    )

    import msl_binding

    input_stride = int(msl_binding.sizes()["input"])
    seed = _seed_base()
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(ACT_TURN)
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["animation_index"][0, 0] = np.uint32(SM_TURN)
    seed["facing"][0, 0] = np.uint8(1)
    seed["turn_frames_to_turn"][0, 0] = np.uint8(1)
    seed["turn_has_turned"][0, 0] = np.uint8(0)
    seed["tilt_timer_x"][0, 0] = np.uint8(10)

    prev_inp = _mk_input_bytes(1, input_stride)
    inp = _mk_input_bytes(1, input_stride)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
    cur_view = inp.view(INPUT_DTYPE).reshape((1,))
    prev_view["p"]["main_x"][0, 0] = np.int8(-23)
    cur_view["p"]["buttons"][0, 0] = np.uint16(BUTTON_A)
    cur_view["p"]["main_x"][0, 0] = np.int8(-74)
    cur_view["p"]["main_y"][0, 0] = np.int8(31)

    out0 = _step_once(seed, prev_inp, inp)
    assert int(out0["action_id"][0]) == _ACT_ATTACK_S3_HI
    assert int(out0["animation_index"][0]) == _SM_ATTACK_S3_HI


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

    dataset_rel = f"{_BASE_REL}/AttachedGoodNaturedGuanaco.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    record = 2351
    p = 1
    row = ds.rows[record : record + 1]

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


@pytest.mark.integration
def test_attacklw3_iasa_destination_walk_does_not_run_same_frame_guardon() -> None:
    # Replay-real lock for AttackLw3 IASA callback consumption:
    # - ftCo_AttackLw3_IASA can enter the locomotion subset through Wait-style helpers.
    # - Once that MotionState callback returns, Fighter_procUpdate does not run the destination
    #   Walk IASA in the same frame, so the newborn Walk state must not admit GuardOn from the
    #   same held L/R input.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "replays/validation/aggregate_recent/PositiveRevolvingHyena.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    record = 3488
    p = 1
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == _ACT_ATTACK_LW3
    assert int(row["seed_t"]["action_frame"][0, p]) == 27
    assert int(row["seed_t"]["animation_index"][0, p]) == 59
    assert int(row["ref_t1"]["action_id"][0, p]) == _ACT_WALK_SLOW
    assert int(row["ref_t1"]["animation_index"][0, p]) == 7
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert int(row["input_t"]["p"][0, p]["buttons"]) & (0x20 | 0x40)

    out, _ = _run_record(dataset_path, record)
    assert int(out["action_id"][0, p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["action_id"][0, p]) != _ACT_GUARD_ON
    assert int(out["action_frame"][0, p]) == int(row["ref_t1"]["action_frame"][0, p])
    assert int(out["animation_index"][0, p]) == int(row["ref_t1"]["animation_index"][0, p])


@pytest.mark.integration
def test_attacklw3_downheld_stops_before_later_turn_walk_iasa_owner() -> None:
    # AttackLw3_IASA checks Squat before Turn/Walk. The runtime's AttackLw3 helper deliberately
    # leaves the Squat/terminal crouch handoff to the existing AttackLw3_Anim owner, so down-held
    # rows must not skip that omitted source owner and enter Turn/Walk in the same callback.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_800D5FB0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "replays/validation/aggregate_recent/PositiveRevolvingHyena.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    record = 362
    p = 1
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == _ACT_ATTACK_LW3
    assert int(row["ref_t1"]["action_id"][0, p]) == _ACT_ATTACK_LW3
    assert _stick_i8_to_unit(int(row["input_t"]["p"][0, p]["main_y"])) < -0.6875

    out, _ = _run_record(dataset_path, record)
    assert int(out["action_id"][0, p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["animation_index"][0, p]) == int(row["ref_t1"]["animation_index"][0, p])
    assert int(out["action_frame"][0, p]) == int(row["ref_t1"]["action_frame"][0, p])
