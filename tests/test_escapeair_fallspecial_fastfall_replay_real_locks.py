from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, SEED_DTYPE, read_dataset


ACT_WAIT = 0x000E
ACT_FALL_SPECIAL = 0x0023
ACT_FX_SPECIAL_HI_FALL = 0x0166
SM_WAIT1_0 = 0x0002
SM_FALL_SPECIAL = 0x001A
CHAR_FOX = 1
STAGE_FINAL_DESTINATION = 2


def _zero_input_bytes() -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
    return np.zeros((1, int(binding.sizes()["input"])), dtype=np.uint8)


def _step_synthetic_seed_once(seed: np.ndarray) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    out = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=True, ucf_cardinals_1_0_enabled=True)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        binding.step_input(handle, _zero_input_bytes(), _zero_input_bytes())
        binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)


def _rollout_outputs(ds, start_record: int, stop_record: int) -> dict[int, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    samples = ds.samples
    seed_bytes = (
        samples[start_record : start_record + 1]["seed_t"]
        .view(np.uint8)
        .reshape(1, seed_stride)
        .copy()
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out: dict[int, np.void] = {}

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, stop_record + 1):
            prev_input = (
                samples[record : record + 1]["prev_input_t"]
                .view(np.uint8)
                .reshape(1, input_stride)
                .copy()
            )
            input_t = (
                samples[record : record + 1]["input_t"]
                .view(np.uint8)
                .reshape(1, input_stride)
                .copy()
            )
            binding.step_input(handle, prev_input, input_t)
            binding.write_compare(handle, out_compare_bytes)
            out[record + 1] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)

    return out


def _rollout_compare_records(ds, start_record: int, records: tuple[int, ...]) -> dict[int, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    samples = ds.samples
    seed_bytes = (
        samples[start_record : start_record + 1]["seed_t"]
        .view(np.uint8)
        .reshape(1, seed_stride)
        .copy()
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out: dict[int, np.void] = {}

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, max(records) + 1):
            prev_input = (
                samples[record : record + 1]["prev_input_t"]
                .view(np.uint8)
                .reshape(1, input_stride)
                .copy()
            )
            input_t = (
                samples[record : record + 1]["input_t"]
                .view(np.uint8)
                .reshape(1, input_stride)
                .copy()
            )
            binding.step_input(handle, prev_input, input_t)
            if record in records:
                binding.write_compare(handle, out_compare_bytes)
                out[record] = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)

    return out


@pytest.mark.integration
def test_escapeair_anim_end_fallspecial_preserves_fastfall_until_landing() -> None:
    # ftCo_EscapeAir_Anim enters FallSpecial through ftCo_80096900, whose inline0 uses
    # Fighter_ChangeMotionState(..., Ft_MF_KeepFastFall). The fastfall bit and fastfall y velocity
    # persist on the FallSpecial entry row, then the later LandingFallSpecial ground transition
    # clears them.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{inline0,ftCo_80096900}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = 1
    start_record = 9883
    fallspecial_record = 9897
    landing_record = 9898

    assert int(samples[start_record]["seed_t"]["action_id"][p]) == 236  # EscapeAir
    assert int(samples[start_record]["seed_t"]["state_flags"][p, 1]) & 0x08

    outputs = _rollout_outputs(ds, start_record, landing_record)
    out_fallspecial = outputs[fallspecial_record]
    ref_fallspecial = samples[fallspecial_record - 1]["ref_t1"]
    assert int(ref_fallspecial["action_id"][p]) == 35  # FallSpecial
    assert int(ref_fallspecial["on_ground"][p]) == 0
    assert int(ref_fallspecial["state_flags"][p, 1]) & 0x08
    assert int(out_fallspecial["action_id"][p]) == int(ref_fallspecial["action_id"][p])
    assert int(out_fallspecial["on_ground"][p]) == int(ref_fallspecial["on_ground"][p])
    assert int(out_fallspecial["state_flags"][p, 1]) == int(ref_fallspecial["state_flags"][p, 1])
    assert float(out_fallspecial["speed_y_self"][p]) == pytest.approx(
        float(ref_fallspecial["speed_y_self"][p]), abs=1e-6
    )
    assert float(out_fallspecial["pos_y"][p]) == pytest.approx(
        float(ref_fallspecial["pos_y"][p]), abs=1e-5
    )

    out_landing = outputs[landing_record]
    ref_landing = samples[landing_record - 1]["ref_t1"]
    assert int(ref_landing["action_id"][p]) == 43  # LandingFallSpecial
    assert int(ref_landing["on_ground"][p]) == 1
    assert (int(ref_landing["state_flags"][p, 1]) & 0x08) == 0
    assert int(out_landing["action_id"][p]) == int(ref_landing["action_id"][p])
    assert int(out_landing["on_ground"][p]) == int(ref_landing["on_ground"][p])
    assert int(out_landing["action_frame"][p]) == int(ref_landing["action_frame"][p])
    assert int(out_landing["state_flags"][p, 1]) == int(ref_landing["state_flags"][p, 1])


@pytest.mark.integration
def test_marth_fallspecial_xc0_uses_fastfall_terminal_until_bottom_blast() -> None:
    # FallSpecial xC==0 vertical terminal owner:
    # - ftCo_80096900 stores mv.co.fallspecial.xC.
    # - ftCo_FallSpecial_Phys uses `ca->fast_fall_velocity` as the non-fastfall terminal when
    #   xC==0, while the public fall_fast bit can remain clear.
    # - The source then integrates that -2.5 Marth velocity before ftCo_800D3158's bottom-blast
    #   check enters DeadDown.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_Phys
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/marth/replays/validation/marth/VictoriousSpitefulAlpaca.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = 0
    start_record = 2571
    terminal_record = 2615
    death_record = 2616

    assert int(samples[terminal_record]["seed_t"]["action_id"][p]) == 35  # FallSpecial
    assert int(samples[start_record]["seed_t"]["action_id"][p]) == 368  # Marth SpecialAirHi
    assert int(samples[terminal_record]["seed_t"]["fall_fast"][p]) == 0
    assert float(samples[terminal_record]["ref_t1"]["speed_y_self"][p]) == pytest.approx(-2.5)
    assert int(samples[death_record]["ref_t1"]["action_id"][p]) == 0  # DeadDown

    outputs = _rollout_compare_records(ds, start_record, (terminal_record, death_record))
    out_terminal = outputs[terminal_record]
    ref_terminal = samples[terminal_record]["ref_t1"]
    assert int(out_terminal["action_id"][p]) == int(ref_terminal["action_id"][p]) == 35
    assert float(out_terminal["speed_y_self"][p]) == pytest.approx(
        float(ref_terminal["speed_y_self"][p]), abs=1e-6
    )
    assert float(out_terminal["pos_y"][p]) == pytest.approx(float(ref_terminal["pos_y"][p]), abs=1e-5)

    out_death = outputs[death_record]
    ref_death = samples[death_record]["ref_t1"]
    assert int(out_death["action_id"][p]) == int(ref_death["action_id"][p]) == 0
    assert int(out_death["stocks"][p]) == int(ref_death["stocks"][p]) == 3


@pytest.mark.integration
def test_marth_fallspecial_xc1_keeps_ordinary_terminal_negative() -> None:
    # Adjacent xC!=0 control: when the seed velocity does not prove the xC==0 FallSpecial branch,
    # api.c reconstructs mv.co.fallspecial.xC as 1. That path must continue using ordinary
    # `ca->terminal_vel` instead of inheriting the fast-fall-velocity terminal.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/marth/replays/validation/marth/FemaleWorthyAlpaca.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    p = 1
    record = 5062
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == 35  # FallSpecial
    assert int(seed["fall_fast"][p]) == 0
    assert float(seed["speed_y_self"][p]) == pytest.approx(-2.2, abs=1e-5)
    assert int(ref["action_id"][p]) == 35
    assert float(ref["speed_y_self"][p]) == pytest.approx(-2.2, abs=1e-5)

    outputs = _rollout_compare_records(ds, record, (record,))
    out = outputs[record]
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 35
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-5)


def test_spacie_specialhi_fall_source_does_not_inherit_xc0_overlay() -> None:
    # Adjacent source-callsite negative: Fox SpecialHiFall also exits through ftCo_80096900, but
    # its callsites pass arg1=1. Reconstruct xC from the data-backed fx-kind overlay, not from raw
    # SpecialHi ancestry. The seed starts just above Fox terminal velocity; xC=1 clamps after
    # gravity to -terminal_vel, while xC=0 would allow the fast-fall terminal lane.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096900
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c
    seed = np.zeros((1,), dtype=SEED_DTYPE)
    p = 0
    seed["stage_id"][0] = np.uint32(STAGE_FINAL_DESTINATION)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT1_0)
    seed["action_id"][0, p] = np.uint16(ACT_FALL_SPECIAL)
    seed["animation_index"][0, p] = np.uint32(SM_FALL_SPECIAL)
    seed["seed_prev_action_id"][0, p] = np.uint16(ACT_FX_SPECIAL_HI_FALL)
    seed["action_frame"][0, p] = np.int16(4)
    seed["anim_frame_f32"][0, p] = np.float32(4.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["pos_y"][0, p] = np.float32(30.0)
    seed["speed_y_self"][0, p] = np.float32(-2.7)
    seed["jumps_left"][0, :2] = np.uint8(1)
    seed["facing"][0, :2] = np.uint8(1)

    out = _step_synthetic_seed_once(seed)

    assert int(out["action_id"][p]) == ACT_FALL_SPECIAL
    assert float(out["speed_y_self"][p]) == pytest.approx(-2.8, abs=1e-6)
