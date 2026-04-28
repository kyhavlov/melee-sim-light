from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_WAIT = 0x000E
SM_WAIT1_0 = 2
ACT_CAPTURE_WAIT_LW = 0x00E3
SM_CAPTURE_WAIT_LW = 255
ACT_THROW_B = 0x00DC
ACT_THROWN_B = 0x00F0
ACT_DAMAGE_FLY_N = 0x0058


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/ecb/fox_bottom.bin",
        "data/ecb/falco_bottom.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _dataset_path(root: Path) -> Path:
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    return dataset_path


def _binding_sizes():
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    return binding, int(sizes["seed"]), int(sizes["input"]), int(sizes["compare"])


def _run_one_step_seed(dataset_path: Path, record: int, seed_mutator=None) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record

    row = samples[record : record + 1]
    seed_t = row["seed_t"].copy()
    if seed_mutator is not None:
        seed_mutator(seed_t)

    binding, seed_stride, input_stride, compare_stride = _binding_sizes()
    seed_bytes = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    return seed_t.reshape(-1)[0].copy(), row["ref_t1"].reshape(-1)[0].copy(), out


def _run_rollout_record(dataset_path: Path, start_record: int, target_record: int) -> tuple[np.void, np.void]:
    return _run_rollout_records(dataset_path, start_record, (target_record,))[target_record]


def _run_rollout_records(
    dataset_path: Path, start_record: int, target_records: tuple[int, ...]
) -> dict[int, tuple[np.void, np.void]]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert target_records
    target_max = max(target_records)
    targets = set(target_records)
    assert int(samples.shape[0]) > target_max
    assert start_record <= min(target_records)

    binding, seed_stride, input_stride, compare_stride = _binding_sizes()
    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        out_by_record: dict[int, tuple[np.void, np.void]] = {}
        for record in range(start_record, target_max + 1):
            row = samples[record : record + 1]
            prev_input_bytes = np.frombuffer(
                row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).copy().reshape(1, input_stride)
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            if record in targets:
                out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
                out_by_record[record] = (samples[record]["ref_t1"].copy(), out)
        return out_by_record
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_capturewaitlw_allow_ground_to_air_collision_reprojects_floor_direct_and_rollout() -> None:
    # Replay-real lock for low-capture grounded collision:
    # - CaptureWaitLw_Phys runs `fn_800DAD18`, which can move the victim XRotN slightly above floor.
    # - CaptureWaitLw_Coll then routes through `ft_8008403C -> ft_80082708 -> mpColl_8004B108`,
    #   which projects the grounded low-capture victim back onto the current floor.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_CaptureWaitLw_Phys,ftCo_CaptureWaitLw_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_8008403C,ft_80082708}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)

    p = 0
    seed, ref, out = _run_one_step_seed(dataset_path, 2480)
    assert int(seed["action_id"][p]) == ACT_CAPTURE_WAIT_LW
    assert int(seed["action_frame"][p]) == 2
    assert int(seed["on_ground"][p]) == 1
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_CAPTURE_WAIT_LW
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p]) == 3
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    rollout_ref, rollout_out = _run_rollout_record(dataset_path, 2477, 2480)
    assert int(rollout_out["action_id"][p]) == int(rollout_ref["action_id"][p]) == ACT_CAPTURE_WAIT_LW
    assert float(rollout_out["pos_y"][p]) == pytest.approx(float(rollout_ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_capturewaitlw_floor_projection_does_not_broaden_generic_ground_snap() -> None:
    # Negative boundary: the low-capture allow-ground-to-air callback is the source of the downward
    # projection. A generic grounded action starting slightly above floor must not be snapped down by
    # the broad stage-collision anti-drift substrate.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)

    p = 0
    lifted_y = np.float32(0.07854971289634705)

    def mutate(seed_t: np.ndarray) -> None:
        seed_t["action_id"][0, p] = np.uint16(ACT_WAIT)
        seed_t["animation_index"][0, p] = np.uint32(SM_WAIT1_0)
        seed_t["action_frame"][0, p] = np.int16(0)
        seed_t["anim_frame_f32"][0, p] = np.float32(0.0)
        seed_t["frame_speed_mul_f32"][0, p] = np.float32(1.0)
        seed_t["pos_y"][0, p] = lifted_y
        seed_t["grab_owner_port"][0, p] = np.uint8(0xFF)

    seed, _ref, out = _run_one_step_seed(dataset_path, 2480, mutate)
    assert int(seed["action_id"][p]) == ACT_WAIT
    assert int(out["action_id"][p]) == ACT_WAIT
    assert float(out["pos_y"][p]) == pytest.approx(float(lifted_y), abs=1e-6)


@pytest.mark.integration
def test_capturewaitlw_to_throwb_rollout_rate_and_release_pose_facing_snapshot() -> None:
    # Replay-real lock for the same F03 owner after the low-capture floor handoff:
    # - CatchWait selects ThrowB, and ftCo_800DD398 installs the shared ftCo_800DD4B0 throw rate.
    # - The 4/3 Q16 runtime must not land one LSB below integer script frames, delaying ThrowB's
    #   set_throw_flags release.
    # - ThrowB's flip and release flags cross on the same script frame. The owner-facing scalar
    #   flips for post-frame state, but the release JObj pose sampled by ftCo_800DDDE4 still uses
    #   the pre-flip pose-facing snapshot from the already-interpreted AObj.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD398,ftCo_800DD4B0,ftCo_800DD724,ftCo_800DDDE4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)

    victim_p = 0
    owner_p = 1

    rollout = _run_rollout_records(dataset_path, 2491, (2509, 2512, 2513))
    ref_2509, out_2509 = rollout[2509]
    assert int(out_2509["action_id"][victim_p]) == int(ref_2509["action_id"][victim_p]) == ACT_THROWN_B
    assert int(out_2509["action_frame"][victim_p]) == int(ref_2509["action_frame"][victim_p]) == 4
    assert int(out_2509["action_frame"][owner_p]) == int(ref_2509["action_frame"][owner_p]) == 4

    # Negative boundary for the release path: the attached pre-release frame stays on the normal
    # ftCo_800DE508 attached-position owner and is not treated as a release-pose snapshot.
    ref_2512, out_2512 = rollout[2512]
    assert int(out_2512["action_id"][victim_p]) == int(ref_2512["action_id"][victim_p]) == ACT_THROWN_B
    assert int(out_2512["action_frame"][victim_p]) == int(ref_2512["action_frame"][victim_p]) == 8
    assert float(out_2512["pos_x"][victim_p]) == pytest.approx(float(ref_2512["pos_x"][victim_p]), abs=1e-5)
    assert float(out_2512["pos_y"][victim_p]) == pytest.approx(float(ref_2512["pos_y"][victim_p]), abs=1e-5)

    ref_2513, out_2513 = rollout[2513]
    assert int(out_2513["action_id"][victim_p]) == int(ref_2513["action_id"][victim_p]) == ACT_DAMAGE_FLY_N
    assert int(out_2513["action_frame"][victim_p]) == int(ref_2513["action_frame"][victim_p]) == 1
    assert float(out_2513["pos_x"][victim_p]) == pytest.approx(float(ref_2513["pos_x"][victim_p]), abs=1e-5)
    assert float(out_2513["pos_y"][victim_p]) == pytest.approx(float(ref_2513["pos_y"][victim_p]), abs=1e-5)

    # Teacher-forced one-step boundary: the same release-pose owner must be correct from a direct
    # replay seed, not only when reached through rollout.
    seed, direct_ref, direct_out = _run_one_step_seed(dataset_path, 2513)
    assert int(seed["action_id"][victim_p]) == ACT_THROWN_B
    assert int(seed["action_id"][owner_p]) == ACT_THROW_B
    assert int(direct_out["action_id"][victim_p]) == int(direct_ref["action_id"][victim_p]) == ACT_DAMAGE_FLY_N
    assert float(direct_out["pos_x"][victim_p]) == pytest.approx(float(direct_ref["pos_x"][victim_p]), abs=1e-5)
    assert float(direct_out["pos_y"][victim_p]) == pytest.approx(float(direct_ref["pos_y"][victim_p]), abs=1e-5)


@pytest.mark.integration
def test_capturewaitlw_to_throwb_throw_laser_uses_motion_entry_root_facing() -> None:
    # Replay-real lock for the adjacent F03 ThrowB article path:
    # - ftCo_800DD724 flips fp->facing_dir at the frame-9 ThrowB release, but it does not re-enter
    #   the motion state or reinstall the fighter root JObj rotation.
    # - Later ftFx_Throw_Anim laser pulses sample RThumbNb through lb_8000B1CC, so the throw-side
    #   laser pose uses the motion-entry root facing (`facing_dir1`) while scalar gameplay facing
    #   remains flipped.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,ftPartSetRotY}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{ftFx_SpecialN_FtGetHoldJoint,ftFx_Throw_Anim}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)

    owner_p = 1
    rollout = _run_rollout_records(dataset_path, 2491, (2518, 2520, 2522))

    ref_2518, out_2518 = rollout[2518]
    first_out = out_2518["items"][1]
    first_ref = ref_2518["items"][1]
    assert int(out_2518["facing"][owner_p]) == int(ref_2518["facing"][owner_p]) == 1
    assert int(first_out["exists"]) == int(first_ref["exists"]) == 1
    assert int(first_out["state"]) == int(first_ref["state"]) == 1
    assert float(first_out["pos_x"]) > float(out_2518["pos_x"][owner_p])
    assert float(first_out["vel_x"]) > 0.0

    # The second and third ThrowB pulses are the same source owner after the facing flip. They must
    # remain replay-exact in the live item set, proving the root-facing fix is not a broad
    # item-direction override. The first-pulse article has a remaining visible pose residual and can
    # occupy an earlier sorted slot until its item-var/collision endpoint owner is fully ported.
    for record in (2520, 2522):
        ref, out = rollout[record]
        ref_item = ref["items"][1]
        assert int(ref_item["exists"]) == 1
        found = False
        for slot in range(4):
            out_item = out["items"][slot]
            if int(out_item["exists"]) != 1 or int(out_item["state"]) != 1:
                continue
            if (
                float(out_item["pos_x"]) == pytest.approx(float(ref_item["pos_x"]), abs=1e-4)
                and float(out_item["pos_y"]) == pytest.approx(float(ref_item["pos_y"]), abs=1e-4)
                and float(out_item["vel_x"]) == pytest.approx(float(ref_item["vel_x"]), abs=1e-4)
                and float(out_item["vel_y"]) == pytest.approx(float(ref_item["vel_y"]), abs=1e-4)
            ):
                found = True
                break
        assert found, f"missing replay-exact ThrowB pulse at record {record}"

    def mutate(seed_t: np.ndarray) -> None:
        seed_t["facing_dir1"][0, owner_p] = np.int8(1)

    _seed, _ref, mutated_out = _run_one_step_seed(dataset_path, 2518, mutate)
    assert float(mutated_out["items"][1]["vel_x"]) < 0.0
