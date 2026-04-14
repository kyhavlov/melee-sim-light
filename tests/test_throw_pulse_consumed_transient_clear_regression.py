from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import INPUT_DTYPE, SEED_DTYPE, read_dataset


@pytest.mark.integration
def test_throw_pulse_consumed_clears_after_step_and_does_not_sticky_carry() -> None:
    # Transient ownership contract:
    # - `seed_t.throw_pulse_consumed` bridges one-step throw_flags_b0 consumption ownership.
    # - `seed_t.throw_pulse_crossed_prev_frame` carries prior-step throw pulse crossing phase.
    # - Runtime may consume it in frame N, but it must clear before frame N+1.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    # ThrowB stale-context lock family row.
    record = 8291
    thrower = 1
    if int(ds.samples.shape[0]) <= record + 1:
        pytest.skip("dataset too short for target+1 transient check")

    row0 = ds.samples[record]
    row1 = ds.samples[record + 1]
    assert int(row0["seed_t"]["throw_pulse_consumed"][thrower]) == 1
    assert int(row0["seed_t"]["throw_pulse_crossed_prev_frame"][thrower]) > 0

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    internals_stride = int(sizes["internals"])
    assert int(SEED_DTYPE.itemsize) == seed_stride
    assert int(INPUT_DTYPE.itemsize) == input_stride

    # Keep in sync with src/api.h::MslDebugInternals.
    MAX_PLAYERS = 4
    INTERNALS_DTYPE = np.dtype(
        [
            ("tilt_timer_x", ("u1", (MAX_PLAYERS,))),
            ("turn_frames_to_turn", ("u1", (MAX_PLAYERS,))),
            ("turn_has_turned", ("u1", (MAX_PLAYERS,))),
            ("guard_reflect_timer_x14", ("u1", (MAX_PLAYERS,))),
            ("entry_end_fall_lock", ("u1", (MAX_PLAYERS,))),
            ("attack_id", ("<u2", (MAX_PLAYERS,))),
            ("attack_instance", ("<u2", (MAX_PLAYERS,))),
            ("attack_identity_last_action_id", ("<u2", (MAX_PLAYERS,))),
            ("instance_id", ("<u2", (MAX_PLAYERS,))),
            ("instance_id_x2073", ("u1", (MAX_PLAYERS,))),
            ("instance_identity_last_action_id", ("<u2", (MAX_PLAYERS,))),
                ("instance_id_counter", "<u2"),
                ("throw_pulse_consumed", ("u1", (MAX_PLAYERS,))),
                ("throw_pulse_crossed_prev_frame", ("u1", (MAX_PLAYERS,))),
                ("throw_pending_victim_port", ("u1", (MAX_PLAYERS,))),
                ("throw_pending_hit_idx", ("u1", (MAX_PLAYERS,))),
                ("attached_victim_port", ("u1", (MAX_PLAYERS,))),
            ],
            align=False,
        )
    assert int(INTERNALS_DTYPE.itemsize) == internals_stride

    seed0 = row0["seed_t"].copy().reshape((1,))
    seed0_bytes = seed0.view(np.uint8).reshape((1, seed_stride)).copy()
    prev0 = row0["prev_input_t"].copy().reshape((1,))
    inp0 = row0["input_t"].copy().reshape((1,))
    prev0_bytes = prev0.view(np.uint8).reshape((1, input_stride)).copy()
    inp0_bytes = inp0.view(np.uint8).reshape((1, input_stride)).copy()

    prev1 = row1["prev_input_t"].copy().reshape((1,))
    inp1 = row1["input_t"].copy().reshape((1,))
    prev1_bytes = prev1.view(np.uint8).reshape((1, input_stride)).copy()
    inp1_bytes = inp1.view(np.uint8).reshape((1, input_stride)).copy()

    out_int = np.zeros((1, internals_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed0_bytes)
        binding.debug_write_internals(handle, out_int)
        int0 = out_int.view(INTERNALS_DTYPE).reshape((1,))[0].copy()
        assert int(int0["throw_pulse_consumed"][thrower]) == 1
        assert int(int0["throw_pulse_crossed_prev_frame"][thrower]) == int(
            row0["seed_t"]["throw_pulse_crossed_prev_frame"][thrower]
        )

        binding.step_input(handle, prev0_bytes, inp0_bytes)
        binding.debug_write_internals(handle, out_int)
        int1 = out_int.view(INTERNALS_DTYPE).reshape((1,))[0].copy()
        assert int(int1["throw_pulse_consumed"][thrower]) == 0
        assert int(int1["throw_pulse_crossed_prev_frame"][thrower]) == 0

        binding.step_input(handle, prev1_bytes, inp1_bytes)
        binding.debug_write_internals(handle, out_int)
        int2 = out_int.view(INTERNALS_DTYPE).reshape((1,))[0].copy()
        assert int(int2["throw_pulse_consumed"][thrower]) == 0
        assert int(int2["throw_pulse_crossed_prev_frame"][thrower]) == 0
    finally:
        binding.destroy(handle)
