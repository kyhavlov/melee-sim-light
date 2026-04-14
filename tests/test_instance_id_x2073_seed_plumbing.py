from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import read_dataset


@pytest.mark.integration
def test_dataset_seeds_instance_id_x2073_and_reseed_respects_it() -> None:
    # Integration regression: ensure the dataset seed schema carries fp+0x2073 (compare byte used
    # by ft_800895E0), and that C reseed loads it verbatim (when nonzero).
    #
    # Decomp anchor:
    # - refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0 (lbz fp+0x2073; compare vs flags_low)
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples

    # Lock a record where the field is nonzero (avoid trivial all-zero passes).
    record = 149
    player = 0
    assert int(samples.shape[0]) > record

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    assert int(seed["action_id"][player]) == 360  # ftFx_MS_SpecialLwStart
    assert int(seed["instance_id_x2073"][player]) == 20  # x4_flags low byte for 0x0168 in current tables

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    internals_stride = int(sizes["internals"])

    # Packed MslDebugInternals view (keep in sync with src/api.h::MslDebugInternals).
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

    # Mutate the seed field to a distinct nonzero value and ensure reseed preserves it.
    seed_mut = row["seed_t"].copy()
    seed_mut["instance_id_x2073"][0, player] = np.uint8(99)

    seed_bytes = np.frombuffer(seed_mut.tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride).copy()
    out_int = np.zeros((1, internals_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_write_internals(handle, out_int)
    finally:
        binding.destroy(handle)

    got = out_int.view(INTERNALS_DTYPE).reshape((1,))[0]
    assert int(got["instance_id_x2073"][player]) == 99
