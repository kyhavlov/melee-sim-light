from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from melee_sim.decomp_port import fox_fd_config, locomotion_tape, phase1_binary, run_phase1


ROOT = Path(__file__).resolve().parents[1]
GAME_DATA = ROOT / "refs" / "melee-disc" / "files"


pytestmark = pytest.mark.skipif(
    not (GAME_DATA / "PlFx.dat").is_file(), reason="local Melee game data is unavailable"
)


def test_phase1_locomotion_is_deterministic_and_source_scheduled() -> None:
    assert phase1_binary().is_file(), "build with: make -f src/decomp_port/Makefile mvp"
    config = fox_fd_config(random_seed=0x12345678)
    inputs = locomotion_tape()

    first = run_phase1(config, inputs, data_dir=GAME_DATA)
    second = run_phase1(config, inputs, data_dir=GAME_DATA)

    assert first.tobytes() == second.tobytes()
    assert len(first) == len(inputs)
    assert np.array_equal(first["frame_id"], np.arange(len(inputs), dtype=np.int32))
    assert np.all(first["stage_id"] == 32)
    assert np.all(first["char_id"][:, :2] == 1)
    assert np.any(first["pos_x"][:, 0] != first["pos_x"][0, 0])

    actions = set(int(value) for value in first["action_id"][:, 0])
    assert actions & {0x0F, 0x10, 0x11}  # walk family
    assert actions & {0x12, 0x13}  # turn family
    assert actions & {0x14, 0x15, 0x16, 0x17}  # dash/run family
    assert 0x18 in actions  # knee bend
    assert actions & {0x19, 0x1A}  # ground jump
    assert actions & {0x1D, 0x1E, 0x1F}  # fall
    assert 0x2A in actions  # landing
