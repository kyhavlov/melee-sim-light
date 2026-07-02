from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


ACT_PASS = 88
ACT_SQUAT_WAIT = 80
ACT_ESCAPE_AIR = 236
ACT_FX_SPECIAL_LW_START = 360
ACT_FX_SPECIAL_AIR_LW_START = 365

BUTTON_B = 0x0200


def _run_one_step(ds, record: int, *, seed_mutator=None) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    row = ds.rows[record : record + 1].copy()
    if seed_mutator is not None:
        seed_mutator(row)

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed(handle, row["seed_t"].view(np.uint8).reshape(1, seed_stride).copy())
        binding.step_input(
            handle,
            row["prev_input_t"].view(np.uint8).reshape(1, input_stride).copy(),
            row["input_t"].view(np.uint8).reshape(1, input_stride).copy(),
        )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "record", "port"),
    [
        ("yoshis_story_recent/CheeryNumbMonkey.slpz", 3031, 1),
        ("battlefield_recent/MediumVirtualPig.slpz", 5222, 0),
        ("fountain_of_dreams_recent/ParallelTemptingElk.slpz", 303, 0),
    ],
)
def test_squat_speciallw_preempts_platform_pass(rel_path: str, record: int, port: int) -> None:
    # Source ordering:
    # Squat/SquatWait IASA checks grounded special dispatch before the delayed platform-pass helper.
    # The sim's Shine entry pass runs after locomotion, so B+down must suppress the local Pass entry
    # approximation and let SpecialLwStart own the frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation" / rel_path
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record]
    ref = row["ref_t1"]
    assert int(ref["action_id"][port]) == ACT_FX_SPECIAL_LW_START
    assert int(ref["on_ground"][port]) == 1

    out = _run_one_step(ds, record)
    for field in ("action_id", "animation_index", "on_ground", "jumps_left"):
        assert int(out[field][port]) == int(ref[field][port]), field


def test_squatwait_platform_pass_does_not_consume_without_speciallw_button() -> None:
    # Source owner split:
    # `SquatWait_IASA` can arm platform pass state through ftCo_80099F9C, but it does not run the
    # Squat consume path in the same callback. Clearing B must therefore avoid SpecialLw while
    # keeping this row in SquatWait rather than inventing a Pass consume.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::{ftCo_Squat_IASA,ftCo_SquatWait_IASA}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/yoshis_story_recent/CheeryNumbMonkey.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))

    def clear_b(row) -> None:
        mask = np.uint16(0xFFFF ^ BUTTON_B)
        row["input_t"]["p"]["buttons"][0, 1] = np.uint16(
            row["input_t"]["p"]["buttons"][0, 1] & mask
        )
        row["prev_input_t"]["p"]["buttons"][0, 1] = np.uint16(
            row["prev_input_t"]["p"]["buttons"][0, 1] & mask
        )

    out = _run_one_step(ds, 3031, seed_mutator=clear_b)
    assert int(out["action_id"][1]) == ACT_SQUAT_WAIT
    assert int(out["action_id"][1]) != ACT_FX_SPECIAL_LW_START


def test_squat_speciallw_preempts_down_attack_fallback() -> None:
    # Squat IASA checks ftCo_800D68C0 before AttackLw4/AttackLw3 branches. This row carries A+B
    # with down stick; source keeps B+down as grounded Reflector instead of consuming the local
    # attack fallback first.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/battlefield_recent/MediumVirtualPig.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    record = 7743
    port = 1
    ref = ds.rows[record]["ref_t1"]
    assert int(ref["action_id"][port]) == ACT_FX_SPECIAL_LW_START

    out = _run_one_step(ds, record)
    for field in ("action_id", "animation_index", "on_ground"):
        assert int(out[field][port]) == int(ref[field][port]), field


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "record", "port"),
    [
        ("yoshis_story_recent/PhysicalElectricCapybara.slpz", 5191, 0),
        ("yoshis_story_recent/PhysicalElectricCapybara.slpz", 4847, 0),
    ],
)
def test_aerial_speciallw_preempts_escapeair_fallback(
    rel_path: str, record: int, port: int
) -> None:
    # Source ordering:
    # Jump/JumpAerial/Fall-family IASA checks ftCo_SpecialAir_CheckInput before EscapeAir.
    # This simulator enters Reflector in a later pass, so B+down must keep the local EscapeAir
    # approximation from consuming the same input first.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLw_Enter
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation" / rel_path
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    ref = ds.rows[record]["ref_t1"]
    assert int(ref["action_id"][port]) == ACT_FX_SPECIAL_AIR_LW_START
    assert int(ref["on_ground"][port]) == 0

    out = _run_one_step(ds, record)
    for field in ("action_id", "animation_index", "on_ground", "jumps_left"):
        assert int(out[field][port]) == int(ref[field][port]), field


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "record", "port"),
    [
        ("yoshis_story_recent/CheeryNumbMonkey.slpz", 614, 1),
        ("battlefield_recent/MediumVirtualPig.slpz", 3268, 1),
        ("fountain_of_dreams_recent/ParallelTemptingElk.slpz", 304, 0),
        ("pokemon_stadium_recent/ThisVioletRaccoon.slpz", 290, 0),
    ],
)
def test_grounded_speciallw_start_iasa_platform_passes_to_air_start(
    rel_path: str, record: int, port: int
) -> None:
    # Source ordering:
    # Existing grounded Reflector Start runs its own IASA callback on later frames. That callback
    # calls ftFx_SpecialLwStart_CheckPass -> ftCo_8009A184, preserving the current anim frame while
    # entering aerial Reflector Start, writing floor_skip, and setting the pass-through y velocity.
    # This is distinct from the fresh Squat -> Reflector entry rows above; destination IASA does not
    # run on the same frame as the original Squat special dispatch.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLwStart_IASA,ftFx_SpecialLwStart_CheckPass,ftFx_SpecialLwStart_Pass}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{ftCo_80099F1C,ftCo_8009A184}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation" / rel_path
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]
    assert int(seed["action_id"][port]) == ACT_FX_SPECIAL_LW_START
    assert int(seed["action_frame"][port]) == 1
    assert int(ref["action_id"][port]) == ACT_FX_SPECIAL_AIR_LW_START
    assert int(ref["on_ground"][port]) == 0

    out = _run_one_step(ds, record)
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "jumps_left"):
        assert int(out[field][port]) == int(ref[field][port]), field
    assert float(out["speed_y_self"][port]) == pytest.approx(float(ref["speed_y_self"][port]))


def test_grounded_speciallw_start_platform_pass_requires_down_input_edge() -> None:
    # Synthetic negative for the same owner: clearing held down keeps the already-existing grounded
    # Reflector Start row grounded instead of taking ftCo_8009A184's platform-pass path.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/yoshis_story_recent/CheeryNumbMonkey.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))

    def clear_down(row) -> None:
        row["input_t"]["p"]["main_y"][0, 1] = np.int8(0)
        row["prev_input_t"]["p"]["main_y"][0, 1] = np.int8(0)
        row["seed_t"]["tilt_timer_y"][0, 1] = np.uint8(0)

    out = _run_one_step(ds, 614, seed_mutator=clear_down)
    assert int(out["action_id"][1]) == ACT_FX_SPECIAL_LW_START
    assert int(out["on_ground"][1]) == 1
