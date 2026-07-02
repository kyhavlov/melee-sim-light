from __future__ import annotations

from pathlib import Path
import json

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


ACT_WAIT = 0x000E
SM_WAIT = 2
STAGE_FD = 32
CHAR_FOX = 1


def _size(sizes: dict[str, int], key: str) -> int:
    if key in sizes:
        return int(sizes[key])
    return int(sizes[f"{key}_v0"])


def test_common_grounded_player_nudge_exact_overlap_uses_player_order() -> None:
    # Decomp tie path:
    # - ftCommon_8007DD7C keeps a `phi_r28` flag while walking the global fighter GObj list.
    # - When pushbox centers are exactly equal, fighters before/after the current GObj pick
    #   opposite signs for p_ftCommonData->x450.
    # Sim tie contract: player slot order is the deterministic GObj-list proxy for this two-player
    # lite sim.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007DD7C
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    common = json.loads((root / "data/common/ft_common_data.json").read_text())
    nudge_x = float(common["player_nudge_x"])

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(1)
    seed["pos_x"][0, :2] = np.float32(0.0)
    seed["pos_y"][0, :2] = np.float32(0.0001)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, :2] = np.uint32(SM_WAIT)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)

    sizes = binding.sizes()
    seed_stride = _size(sizes, "seed")
    input_stride = _size(sizes, "input")
    compare_stride = _size(sizes, "compare")
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    neutral = np.zeros((1,), dtype=INPUT_DTYPE).view(np.uint8).reshape((1, input_stride))

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        binding.step_input(handle, neutral, neutral)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)

    assert float(out["pos_x"][0]) == pytest.approx(+nudge_x, abs=1e-6)
    assert float(out["pos_x"][1]) == pytest.approx(-nudge_x, abs=1e-6)
    assert int(out["on_ground"][0]) == 1
    assert int(out["on_ground"][1]) == 1
