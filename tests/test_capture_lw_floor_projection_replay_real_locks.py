from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_WAIT = 0x000E
ACT_DASH = 0x0014
SM_WAIT1_0 = 2
ACT_ATTACK_HI4 = 0x003F
ACT_CAPTURE_PULLED_LW = 0x00E2
ACT_CAPTURE_WAIT_LW = 0x00E3
SM_CAPTURE_WAIT_LW = 255
ACT_THROW_B = 0x00DC
ACT_THROWN_B = 0x00F0
ACT_DAMAGE_FLY_N = 0x0058
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_CAPTURE_PULLED_HI = 0x00DF
ACT_FX_SPECIAL_AIR_LW_LOOP = 0x016E
STATE_FLAG_2218_ALLOW_INTERRUPT = 0x80


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/characters/marth.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/anims/marth.tracks.bin",
        "data/ecb/fox_bottom.bin",
        "data/ecb/falco_bottom.bin",
        "data/ecb/marth_bottom.bin",
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


def _cardinal_dataset_path(root: Path, name: str) -> Path:
    dataset_rel = f"datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/{name}"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    return dataset_path


def _aggregate_validation_dataset_path(root: Path, subdir: str, name: str) -> Path:
    dataset_rel = f"datasets/aggregate_recent/replays/validation/{subdir}/{name}"
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
def test_capturepulledlw_preserves_source_allow_interrupt_flag_dhh() -> None:
    # Low-pulled capture keeps the interrupted source fp+0x2218 allow_interrupt bit visible through
    # CapturePulledLw. The Dash->CapturePulledHi clear is a narrower high-pulled owner and must not
    # erase the low-pulled lane.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CapturePulledHi_Anim,ftCo_CapturePulledLw_Anim}
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 -> state_flags[0])
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _aggregate_validation_dataset_path(root, "marth", "DraftyHealthyHare.msl")

    seed_4593, ref_4593, out_4593 = _run_one_step_seed(dataset_path, 4593)
    seed_4594, ref_4594, out_4594 = _run_one_step_seed(dataset_path, 4594)

    assert int(seed_4593["action_id"][0]) == ACT_DASH
    assert int(out_4593["action_id"][0]) == int(ref_4593["action_id"][0]) == ACT_CAPTURE_PULLED_LW
    assert int(seed_4594["action_id"][0]) == ACT_CAPTURE_PULLED_LW
    assert int(out_4594["action_id"][0]) == int(ref_4594["action_id"][0]) == ACT_CAPTURE_PULLED_LW
    assert int(out_4593["state_flags"][0, 0]) & STATE_FLAG_2218_ALLOW_INTERRUPT
    assert int(out_4594["state_flags"][0, 0]) & STATE_FLAG_2218_ALLOW_INTERRUPT
    assert out_4593["state_flags"][0].tolist() == ref_4593["state_flags"][0].tolist()
    assert out_4594["state_flags"][0].tolist() == ref_4594["state_flags"][0].tolist()

    lim_path = _aggregate_validation_dataset_path(root, "yoshis_story_recent", "LawfulInsistentMeerkat.msl")
    seed_lim, ref_lim, out_lim = _run_one_step_seed(lim_path, 2449)
    assert int(seed_lim["action_id"][0]) == ACT_DASH
    assert int(out_lim["action_id"][0]) == int(ref_lim["action_id"][0]) == ACT_CAPTURE_PULLED_LW
    assert int(seed_lim["state_flags"][0, 0]) & STATE_FLAG_2218_ALLOW_INTERRUPT
    assert int(ref_lim["state_flags"][0, 0]) == 0x20
    assert out_lim["state_flags"][0].tolist() == ref_lim["state_flags"][0].tolist()


@pytest.mark.integration
def test_capturepulledlw_floor_loss_handoff_uses_stage_line_and_vertical_carry_discriminators() -> None:
    # CapturePulledLw floor-loss callback boundary:
    # - Main-floor rows can have sparse compact floor probes after the replay-visible capture anchor
    #   delta; they must stay grounded PulledLw when source CollData floor ownership remains valid.
    # - Carried platform/ledge floors, or a source-visible vertical carry above p_ftCommonData->x3C4,
    #   can enter the airborne PulledHi callback when the source floor mask is gone.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CapturePulledLw_Phys,ftCo_CapturePulledLw_Coll,fn_800DB230}
    # data/stages/bin/*.bin::MSLSTG01 segment.{platform,ledge}
    # data/common/ft_common_data.json::capture_pulled_lw_air_delta_y
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    cases = [
        (
            _aggregate_validation_dataset_path(root, "marth", "ExtraLargeScaryHornet.msl"),
            440,
            1,
            ACT_CAPTURE_PULLED_LW,
            "Pokemon Stadium main floor keeps PulledLw",
        ),
        (
            _aggregate_validation_dataset_path(root, "marth", "ExtraLargeScaryHornet.msl"),
            540,
            0,
            ACT_CAPTURE_PULLED_LW,
            "Pokemon Stadium main floor keeps PulledLw with CatchWait owner",
        ),
        (
            _aggregate_validation_dataset_path(root, "marth", "InternalPowerlessWallaby.msl"),
            7312,
            0,
            ACT_CAPTURE_PULLED_HI,
            "Fountain carried vertical separation enters PulledHi",
        ),
        (
            _aggregate_validation_dataset_path(root, "marth", "MetallicUniqueGrouse.msl"),
            2274,
            0,
            ACT_CAPTURE_PULLED_HI,
            "Yoshi ledge floor-loss enters PulledHi",
        ),
        (
            root / "datasets/sheik/replays/validation/sheik/AttractiveAnyClam.msl",
            3836,
            1,
            ACT_CAPTURE_PULLED_HI,
            "Pokemon Stadium platform floor-loss enters PulledHi",
        ),
    ]
    for dataset_path, record, p, expected_action, note in cases:
        seed, ref, out = _run_one_step_seed(dataset_path, record)
        assert int(seed["action_id"][p]) == ACT_CAPTURE_PULLED_LW, note
        assert int(ref["action_id"][p]) == expected_action, note
        assert int(out["action_id"][p]) == int(ref["action_id"][p]), note
        assert int(out["on_ground"][p]) == int(ref["on_ground"][p]), note
        assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p]), note


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "p", "note"),
    [
        (
            "BeautifulDistantWolverine.msl",
            2686,
            0,
            "Yoshi left ledge frame-1 CapturePulledLw Phys enters airborne PulledHi",
        ),
        (
            "DrabMeanArmadillo.msl",
            2208,
            1,
            "Yoshi right ledge frame-1 CapturePulledLw Phys enters airborne PulledHi",
        ),
        (
            "sheik_demo_game_2.msl",
            5603,
            1,
            "Battlefield platform frame-1 CapturePulledLw Phys enters airborne PulledHi",
        ),
    ],
)
def test_sheik_capturepulledlw_frame1_floor_loss_enters_capturepulledhi(
    dataset_name: str, record: int, p: int, note: str
) -> None:
    # CapturePulledLw frame-1 Phys/Coll floor-loss owner:
    # - `ftCo_CapturePulledLw_Phys` runs `fn_800DAD18` on already-visible PulledLw rows even when
    #   the prior replay action was Catch/Wait/Passive instead of PulledLw.
    # - If the following `ft_8008403C -> mpColl_800477E0` floor mask misses a carried MSLSTG01
    #   platform/ledge floor, `fn_800DB230` enters CapturePulledHi and applies the airborne anchor.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CapturePulledLw_Phys,ftCo_CapturePulledLw_Coll,fn_800DAD18,fn_800DB230}
    # data/stages/bin/*.bin::MSLSTG01 segment.{platform,ledge}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / f"datasets/sheik/replays/validation/sheik/{dataset_name}"
    if not dataset_path.exists():
      pytest.skip(f"missing local dataset: {dataset_path}")

    seed, ref, out = _run_one_step_seed(dataset_path, record)
    assert int(seed["action_id"][p]) == ACT_CAPTURE_PULLED_LW, note
    assert int(seed["action_frame"][p]) == 1, note
    assert int(ref["action_id"][p]) == ACT_CAPTURE_PULLED_HI, note
    assert int(out["action_id"][p]) == ACT_CAPTURE_PULLED_HI, note
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p]), note
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-5)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-5)


@pytest.mark.integration
def test_capturepulledlw_platform_anchor_still_on_surface_stays_low_slz() -> None:
    # Adjacent negative for the platform floor-loss callback:
    # `ftCo_CapturePulledLw_Phys` applies fn_800DAD18 first, then `ftCo_CapturePulledLw_Coll`
    # calls ft_8008403C(gobj, fn_800DB230). A compact simulator floor probe can miss a carried
    # Battlefield platform even though the post-anchor root is still on the same current platform;
    # that is not source floor loss and must stay in the low capture variant.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CapturePulledLw_Phys,ftCo_CapturePulledLw_Coll,fn_800DAD18,fn_800DB230}
    # data/stages/bin/grnba.bin::MSLSTG01 platform segment 2
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1
    seed_2720, ref_2720, out_2720 = _run_one_step_seed(dataset_path, 2720)
    assert int(seed_2720["action_id"][p]) == ACT_CAPTURE_PULLED_LW
    assert int(seed_2720["action_frame"][p]) == 1
    assert int(seed_2720["ground_id"][p]) == 2
    assert int(ref_2720["action_id"][p]) == ACT_CAPTURE_PULLED_LW
    assert int(out_2720["action_id"][p]) == ACT_CAPTURE_PULLED_LW
    assert int(out_2720["on_ground"][p]) == int(ref_2720["on_ground"][p]) == 1
    assert int(out_2720["jumps_left"][p]) == int(ref_2720["jumps_left"][p]) == 2
    assert float(out_2720["pos_x"][p]) == pytest.approx(float(ref_2720["pos_x"][p]), abs=1e-5)
    assert float(out_2720["pos_y"][p]) == pytest.approx(float(ref_2720["pos_y"][p]), abs=1e-6)

    seed_2721, ref_2721, out_2721 = _run_one_step_seed(dataset_path, 2721)
    assert int(seed_2721["action_id"][p]) == ACT_CAPTURE_PULLED_LW
    assert int(seed_2721["action_frame"][p]) == 2
    assert int(seed_2721["ground_id"][p]) == 2
    assert int(ref_2721["action_id"][p]) == ACT_CAPTURE_WAIT_LW
    assert int(out_2721["action_id"][p]) == ACT_CAPTURE_WAIT_LW
    assert int(out_2721["on_ground"][p]) == int(ref_2721["on_ground"][p]) == 1
    assert int(out_2721["jumps_left"][p]) == int(ref_2721["jumps_left"][p]) == 2
    assert float(out_2721["pos_x"][p]) == pytest.approx(float(ref_2721["pos_x"][p]), abs=1e-5)
    assert float(out_2721["pos_y"][p]) == pytest.approx(float(ref_2721["pos_y"][p]), abs=1e-6)


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
def test_capturepulledhi_entry_uses_locked_floor_owner_for_same_frame_lw_handoff() -> None:
    # Replay-real lock for catch-connect capture-state ownership:
    # - fn_800DAADC enters CapturePulledHi for an airborne victim.
    # - The immediate CapturePulledHi collision callback can consume the victim's locked CollData
    #   floor owner after the capture-anchor delta and enter CapturePulledLw in the same frame.
    # - DamageFly-family floor ids remain owned by damage collision and must not force the Lw handoff.
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{
    #   fn_800DAADC,fn_800DAC78}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CapturePulledHi_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80083C00
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = _cardinal_dataset_path(root, "AttachedGoodNaturedGuanaco.msl")
    p = 0
    seed, ref, out = _run_one_step_seed(dataset_path, 3204)
    assert int(seed["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_LOOP
    assert int(seed["on_ground"][p]) == 0
    assert int(seed["ground_id"][p]) != 0xFFFF
    assert int(seed["ecb_lock_timer"][p]) != 0
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_CAPTURE_PULLED_LW
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p]) == 2

    dataset_path = _cardinal_dataset_path(root, "TreasuredBackKangaroo.msl")
    p = 1
    seed, ref, out = _run_one_step_seed(dataset_path, 5861)
    assert int(seed["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(seed["on_ground"][p]) == 0
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_CAPTURE_PULLED_HI
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0


@pytest.mark.integration
def test_agg_capture_entry_rollout_window_remains_exact_after_locked_floor_handoff() -> None:
    # Replay-real lock for the AGG rollout median autopsy:
    # - Before the locked-floor owner, rollout from record 2694 broke at 3204 and reseeded at 3205.
    # - With the source owner, record 3204 is exact and a rollout seeded at 3204 remains exact
    #   through the later 3468 hitlag window.
    # - The remaining 2694 -> 3468 rollout break is therefore a downstream continuity residual, not
    #   an over-broad local capture entry regression.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   fn_800DAADC,ftCo_CapturePulledHi_Coll,fn_800DAECC,fn_800DAEEC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _cardinal_dataset_path(root, "AttachedGoodNaturedGuanaco.msl")

    victim_p = 0
    owner_p = 1
    rollout = _run_rollout_records(dataset_path, 3204, (3204, 3468))

    ref_3204, out_3204 = rollout[3204]
    assert int(out_3204["action_id"][victim_p]) == int(ref_3204["action_id"][victim_p])
    assert int(out_3204["animation_index"][victim_p]) == int(ref_3204["animation_index"][victim_p])
    assert int(out_3204["on_ground"][victim_p]) == int(ref_3204["on_ground"][victim_p]) == 1
    assert int(out_3204["action_id"][owner_p]) == int(ref_3204["action_id"][owner_p])

    ref_3468, out_3468 = rollout[3468]
    for p in (victim_p, owner_p):
        assert int(out_3468["action_id"][p]) == int(ref_3468["action_id"][p])
        assert int(out_3468["animation_index"][p]) == int(ref_3468["animation_index"][p])
        assert int(out_3468["hitlag"][p]) == int(ref_3468["hitlag"][p])
        assert int(out_3468["hitstun"][p]) == int(ref_3468["hitstun"][p])


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


@pytest.mark.integration
def test_capturepulledlw_catchdash_connect_runs_immediate_floor_callbacks_qgd_rollout() -> None:
    # Replay-real lock for catch-connect callback ordering:
    # - Fighter_8006A360 can run the victim's startup-complete KneeBend Anim callback before a later
    #   Fighter_UnkProcessGrab_8006CA5C catch-connect callback from the grab owner.
    # - This row's vanilla order is victim KneeBend -> JumpF -> AttackAirHi, then owner CatchDash ->
    #   CatchDashPull, then victim CapturePulledHi -> CapturePulledLw through the immediate Coll
    #   callback (`fn_800DAADC` calling fp+0x21A8).
    # - The current root-floor projection, not a stale grounded snap, owns the captured victim's first
    #   replay-visible CapturePulledLw frame.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_UnkProcessGrab_8006CA5C,Fighter_procUpdate}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CapturePulledHi_Coll,ftCo_CapturePulledLw_Coll,fn_800DAADC,fn_800DAECC,fn_800DAEEC}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_8008403C,ft_80083C00}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B108,mpColl_800477E0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _cardinal_dataset_path(root, "QuerulousGrandDinosaur.msl")

    victim_p = 1
    owner_p = 0
    rollout = _run_rollout_records(dataset_path, 5366, (5369, 5370, 5371, 5372, 5373, 5374, 5375))

    ref_5369, out_5369 = rollout[5369]
    assert int(out_5369["action_id"][owner_p]) == int(ref_5369["action_id"][owner_p]) == 215
    assert int(out_5369["instance_id"][owner_p]) == int(ref_5369["instance_id"][owner_p]) == 988
    assert int(ref_5369["action_id"][victim_p]) == ACT_CAPTURE_PULLED_LW
    assert int(out_5369["action_id"][victim_p]) == ACT_CAPTURE_PULLED_LW
    assert int(out_5369["instance_id"][victim_p]) == int(ref_5369["instance_id"][victim_p]) == 990
    assert int(out_5369["action_frame"][victim_p]) == int(ref_5369["action_frame"][victim_p]) == 1
    assert int(out_5369["on_ground"][victim_p]) == int(ref_5369["on_ground"][victim_p]) == 1
    assert int(out_5369["jumps_left"][victim_p]) == int(ref_5369["jumps_left"][victim_p]) == 2
    assert float(out_5369["pos_x"][victim_p]) == pytest.approx(float(ref_5369["pos_x"][victim_p]), abs=1e-5)
    assert float(out_5369["pos_y"][victim_p]) == pytest.approx(float(ref_5369["pos_y"][victim_p]), abs=1e-6)

    ref_5370, out_5370 = rollout[5370]
    assert int(out_5370["action_id"][owner_p]) == int(ref_5370["action_id"][owner_p]) == 215
    assert int(out_5370["instance_id"][owner_p]) == int(ref_5370["instance_id"][owner_p]) == 988
    assert int(out_5370["action_id"][victim_p]) == int(ref_5370["action_id"][victim_p]) == ACT_CAPTURE_PULLED_LW
    assert int(out_5370["instance_id"][victim_p]) == int(ref_5370["instance_id"][victim_p]) == 990
    assert int(out_5370["action_frame"][victim_p]) == int(ref_5370["action_frame"][victim_p]) == 2
    assert float(out_5370["pos_x"][victim_p]) == pytest.approx(float(ref_5370["pos_x"][victim_p]), abs=1e-5)
    assert float(out_5370["pos_y"][victim_p]) == pytest.approx(float(ref_5370["pos_y"][victim_p]), abs=1e-6)

    # CapturePulled* -> CaptureWait* can be installed from the grab owner's callback after the
    # victim's prio-1 Anim callback already ran for this frame. The rollout lock covers that live
    # callback order; normal teacher-forced one-step still lacks the hidden current-AObj-rate
    # snapshot for some later CaptureWait rows.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_UnkProcessGrab_8006CA5C}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CaptureWaitHi_Anim,ftCo_CatchPull_Anim,fn_800DA1D8,fn_800DB6C8}
    expected_frames = {
        5371: 1,
        5372: 2,
        5373: 3,
        5374: 5,
        5375: 7,
    }
    for record, expected_frame in expected_frames.items():
        ref, out = rollout[record]
        assert int(out["instance_id"][owner_p]) == int(ref["instance_id"][owner_p])
        assert int(out["action_id"][victim_p]) == int(ref["action_id"][victim_p]) == ACT_CAPTURE_WAIT_LW
        assert int(out["instance_id"][victim_p]) == int(ref["instance_id"][victim_p]) == 992
        assert int(out["action_frame"][victim_p]) == int(ref["action_frame"][victim_p]) == expected_frame


@pytest.mark.integration
def test_capturepulledlw_steady_phys_uses_live_jobj_delta_qhp_marth_rollout() -> None:
    # Replay-real lock for steady grounded CapturePulledLw attachment:
    # - fn_800DAADC grounded catch-connect entry does not move X immediately.
    # - The next CapturePulledLw Phys callback runs fn_800DAD18, sampling the grabber's
    #   capturedamage.x18 joint and victim XRotN through live JObjs (`lb_8000B1CC`) after AObj
    #   interpretation. Integer SSANIM matrices underpublish Marth CatchPull's attachment delta and
    #   leave a persistent X drift that later changes BODY contact selection.
    # - Row 265's remaining DamageFlyN/Lw split is intentionally not locked here; that belongs to
    #   the terminal DamageFlyTop hurtcap-pose owner once the capture position is correct.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DAADC,fn_800DAD18}
    # refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _aggregate_validation_dataset_path(
        root, "marth", "QuestionableHarmfulPanther.msl"
    )

    victim_p = 0
    owner_p = 1
    rollout = _run_rollout_records(dataset_path, 190, (191, 192, 264))

    ref_191, out_191 = rollout[191]
    assert int(out_191["action_id"][victim_p]) == int(ref_191["action_id"][victim_p]) == ACT_CAPTURE_PULLED_LW
    assert int(out_191["action_frame"][victim_p]) == int(ref_191["action_frame"][victim_p]) == 2
    assert int(out_191["action_id"][owner_p]) == int(ref_191["action_id"][owner_p]) == 213
    assert int(out_191["action_frame"][owner_p]) == int(ref_191["action_frame"][owner_p]) == 7
    assert float(out_191["pos_x"][victim_p]) == pytest.approx(float(ref_191["pos_x"][victim_p]), abs=1e-5)
    assert float(out_191["pos_y"][victim_p]) == pytest.approx(float(ref_191["pos_y"][victim_p]), abs=1e-6)

    ref_192, out_192 = rollout[192]
    assert int(out_192["action_id"][victim_p]) == int(ref_192["action_id"][victim_p]) == ACT_CAPTURE_WAIT_LW
    assert int(out_192["action_id"][owner_p]) == int(ref_192["action_id"][owner_p]) == 216
    assert float(out_192["pos_x"][victim_p]) == pytest.approx(float(ref_192["pos_x"][victim_p]), abs=1e-5)
    assert float(out_192["pos_y"][victim_p]) == pytest.approx(float(ref_192["pos_y"][victim_p]), abs=1e-6)

    ref_264, out_264 = rollout[264]
    assert int(out_264["action_id"][victim_p]) == int(ref_264["action_id"][victim_p]) == ACT_DAMAGE_FLY_TOP
    assert int(out_264["action_frame"][victim_p]) == int(ref_264["action_frame"][victim_p]) == 34
    assert float(out_264["pos_x"][victim_p]) == pytest.approx(float(ref_264["pos_x"][victim_p]), abs=1e-5)
    assert float(out_264["pos_y"][victim_p]) == pytest.approx(float(ref_264["pos_y"][victim_p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "victim_p", "seed_action"),
    [
        ("QuerulousGrandDinosaur.msl", 4063, 1, ACT_ATTACK_HI4),
        ("GracefulAttachedTurtle.msl", 7721, 0, 178),
        ("TreasuredBackKangaroo.msl", 5726, 1, 187),
    ],
)
def test_capturepulledlw_immediate_floor_callback_rejects_ordinary_grounded_captures(
    dataset_name: str, record: int, victim_p: int, seed_action: int
) -> None:
    # Negative controls for the root-floor probe above. Ordinary grounded capture connections and
    # steady AttackHi4 victims keep floor contact in ft_8008403C and must not run the current-frame
    # AttackHi4-entry Lw -> Hi -> Lw immediate root projection. Broad versions over-projected these
    # victims' X position.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _cardinal_dataset_path(root, dataset_name)

    seed, ref, out = _run_one_step_seed(dataset_path, record)
    assert int(seed["action_id"][victim_p]) == seed_action
    if seed_action == ACT_ATTACK_HI4:
        assert int(seed["action_frame"][victim_p]) == 1
    else:
        assert int(seed["action_id"][victim_p]) != ACT_ATTACK_HI4
    assert int(ref["action_id"][victim_p]) == ACT_CAPTURE_PULLED_LW
    assert int(out["action_id"][victim_p]) == ACT_CAPTURE_PULLED_LW
    assert int(out["instance_id"][victim_p]) == int(ref["instance_id"][victim_p])
    assert int(out["ground_id"][victim_p]) == int(ref["ground_id"][victim_p])
    assert float(out["pos_x"][victim_p]) == pytest.approx(float(ref["pos_x"][victim_p]), abs=1e-5)
    assert float(out["pos_y"][victim_p]) == pytest.approx(float(ref["pos_y"][victim_p]), abs=1e-6)


@pytest.mark.integration
def test_grounded_capturepulledlw_catch_connect_does_not_run_lw_coll_prh_6851() -> None:
    # Replay-real lock for grounded catch-connect entry:
    # - The victim's current action callback enters grounded AttackHi4 before the catch collision.
    # - fn_800DAADC then installs CapturePulledLw directly from the grounded xE0 lane.
    # - The newly-entered Lw Phys/Coll (`fn_800DAD18` / `ftCo_CapturePulledLw_Coll`) does not run
    #   inside that catch-connect callback; applying the Lw -> Hi -> Lw projection here overmoves X.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   fn_800DAADC,ftCo_CapturePulledLw_Phys,ftCo_CapturePulledLw_Coll}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/aggregate_recent/"
        "PositiveRevolvingHyena.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    victim_p = 1
    seed, ref, out = _run_one_step_seed(dataset_path, 6851)
    assert int(seed["action_id"][victim_p]) == 72  # LandingAirB
    assert int(ref["action_id"][victim_p]) == ACT_CAPTURE_PULLED_LW
    assert int(out["action_id"][victim_p]) == ACT_CAPTURE_PULLED_LW
    assert int(out["action_frame"][victim_p]) == int(ref["action_frame"][victim_p]) == 1
    assert float(out["pos_x"][victim_p]) == pytest.approx(float(ref["pos_x"][victim_p]), abs=1e-6)
    assert float(out["pos_y"][victim_p]) == pytest.approx(float(ref["pos_y"][victim_p]), abs=1e-6)


@pytest.mark.integration
def test_pass_floor_skip_capturepulledhi_floor_mask_ignores_dropped_platform_mgs_1791() -> None:
    # Replay-real positive for Pass floor-skip into catch-connect capture:
    # - Pass entry writes CollData.floor_skip through mpUpdateFloorSkip for the platform being
    #   dropped through.
    # - fn_800DAADC installs airborne CapturePulledHi, applies fn_800DAC78/fn_800DAA40, then
    #   CapturePulledHi_Coll runs ft_80083C00/mpColl_800477E0.
    # - The floor-mask query must keep skipping the stale platform floor and select the lower
    #   fighter-solid floor, entering CapturePulledLw at the replay floor height.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{ftCo_8009A184,ftCo_8009A228}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DAADC,ftCo_CapturePulledHi_Coll}
    # refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_800477E0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _aggregate_validation_dataset_path(
        root, "fountain_of_dreams_recent", "MilkyGracefulStingray.msl"
    )

    p = 1
    seed, ref, out = _run_one_step_seed(dataset_path, 1791)
    assert int(seed["action_id"][p]) == 244  # Pass
    assert int(seed["ground_id"][p]) == 1  # skipped FoD platform
    assert int(ref["action_id"][p]) == ACT_CAPTURE_PULLED_LW
    assert int(out["action_id"][p]) == ACT_CAPTURE_PULLED_LW
    assert int(ref["ground_id"][p]) == 5
    assert int(out["ground_id"][p]) == 5
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-5)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-5)
