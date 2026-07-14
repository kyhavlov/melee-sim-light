from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE

# Character ids (GALE01): Slippi post-frame `character`.
CHAR_FOX = 1
CHAR_FALCO = 22

# Stage ids (GALE01): Final Destination = 32.
STAGE_FD = 32

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h.
ACT_WAIT = 0x000E
ACT_CAPTURE_DAMAGE_LW = 0x00E4  # ftCo_MS_CaptureDamageLw (228)

def _require_local_artifacts_or_skip() -> None:
    paths = [
        Path("data/stages/final_destination.json"),
        Path("data/characters/fox.json"),
        Path("data/anims/fox.bin"),
        Path("data/anims/falco.bin"),
    ]
    if any(not p.exists() for p in paths):
        pytest.skip("requires local data/ artifacts (stage + chars + anims); run tools/extraction to generate")
@pytest.mark.integration
def test_capture_anim_loop_wrap_applies_before_delta() -> None:
    _require_local_artifacts_or_skip()
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    # Pick a looping capture msid with end_frame=20.0 in the suite artifacts, and start at the last
    # in-range frame so that advancing by 1 hits exactly end_frame and should wrap to 0.
    msid_loop = 256
    frame_start = 19

    owner_pos = (0.0, 50.0, 0.0)
    victim_pos = (10.0, 50.0, 0.0)
    scale_y = 1.0
    facing = 1  # right

    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        seed = np.zeros((1,), dtype=SEED_DTYPE)

        seed["stage_id"][0] = np.uint32(STAGE_FD)
        seed["num_players"][0] = np.uint8(2)
        seed["stocks"][0, :2] = np.uint8(4)
        seed["ground_id"][0, :2] = np.uint16(0xFFFF)

        # Owner (p0): stable Wait, frozen anim clock.
        seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
        seed["pos_x"][0, 0] = np.float32(owner_pos[0])
        seed["pos_y"][0, 0] = np.float32(owner_pos[1])
        seed["pos_z"][0, 0] = np.float32(owner_pos[2])
        seed["fighter_scale_y"][0, 0] = np.float32(scale_y)
        seed["facing"][0, 0] = np.uint8(facing)
        seed["on_ground"][0, 0] = np.uint8(0)
        seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
        seed["action_frame"][0, 0] = np.int16(0)
        seed["animation_index"][0, 0] = np.uint32(2)
        seed["anim_frame_f32"][0, 0] = np.float32(0.0)
        seed["frame_speed_mul_f32"][0, 0] = np.float32(0.0)

        # Victim (p1): capture victim with looping msid, so (19 + 1) wraps to 0 before delta.
        seed["char_id"][0, 1] = np.uint8(CHAR_FALCO)
        seed["pos_x"][0, 1] = np.float32(victim_pos[0])
        seed["pos_y"][0, 1] = np.float32(victim_pos[1])
        seed["pos_z"][0, 1] = np.float32(victim_pos[2])
        seed["fighter_scale_y"][0, 1] = np.float32(scale_y)
        seed["facing"][0, 1] = np.uint8(facing)
        seed["on_ground"][0, 1] = np.uint8(0)
        seed["action_id"][0, 1] = np.uint16(ACT_CAPTURE_DAMAGE_LW)
        seed["action_frame"][0, 1] = np.int16(frame_start)
        seed["animation_index"][0, 1] = np.uint32(msid_loop)
        seed["anim_frame_f32"][0, 1] = np.float32(float(frame_start))
        seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)
        seed["grab_owner_port"][0, 1] = np.uint8(0)

        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)

        def run_once(run_seed: np.ndarray) -> np.void:
            out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
            seed_bytes = run_seed.view(np.uint8).reshape((1, seed_stride))
            msl_binding.reseed_seed(handle, seed_bytes)
            msl_binding.step_input(handle, prev_inp, inp)
            msl_binding.write_compare(handle, out_bytes)
            return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()

        wrapped = run_once(seed)

        # The unified live pose owner is the oracle here: after the source AObj clock wraps, the
        # capture constraint must see the same pose as an otherwise-identical frozen frame-0 seed.
        # This locks callback order without duplicating pose evaluation in Python.
        control = seed.copy()
        control["action_frame"][0, 1] = np.int16(0)
        control["anim_frame_f32"][0, 1] = np.float32(0.0)
        control["frame_speed_mul_f32"][0, 1] = np.float32(0.0)
        frame_zero = run_once(control)

        assert float(wrapped["pos_x"][1]) != pytest.approx(victim_pos[0], abs=1e-6)
        assert float(wrapped["pos_x"][1]) == pytest.approx(float(frame_zero["pos_x"][1]), abs=1e-6)
        assert float(wrapped["pos_y"][1]) == pytest.approx(float(frame_zero["pos_y"][1]), abs=1e-6)
        assert int(wrapped["action_frame"][1]) == int(frame_zero["action_frame"][1])
    finally:
        msl_binding.destroy(handle)
