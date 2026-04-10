from __future__ import annotations

from pathlib import Path

import json
import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)

ACT_WAIT = 0x000E
SM_WAIT = 2
STAGE_FD = 32
CHAR_FOX = 1


def _size(sizes: dict[str, int], key: str) -> int:
    if key in sizes:
        return int(sizes[key])
    return int(sizes[f"{key}_v0"])


@pytest.mark.integration
@pytest.mark.parametrize("record", [940, 941])
def test_common_grounded_player_nudge_before_grab_connect(record: int) -> None:
    # Replay-real lock for the common grounded fighter-overlap nudge lane:
    # - Fighter_8006A360 runs ftCommon_8007E0E4 before Fighter_procUpdate.
    # - ftCommon_8007DD7C accumulates +/-p_ftCommonData->x450 on horizontal pushbox overlap.
    # - Fighter_procUpdate applies xF8_playerNudgeVel before self/KB velocity integration.
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, record, 1)

    # Keep this scoped to the grab-connect neighborhood that exposed downstream rollout drift.
    assert int(seed["action_id"][1]) == 212
    assert int(ref["action_id"][1]) in (212, 213)
    assert int(out["action_id"][1]) == int(ref["action_id"][1])

    for p in (0, 1):
        for field in ("action_id", "action_frame", "on_ground", "hitlag", "hitstun"):
            assert int(out[field][p]) == int(ref[field][p]), f"p{p} {field}"
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-6), (
            f"p{p} pos_x"
        )
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-6), (
            f"p{p} pos_y"
        )


def test_common_grounded_player_nudge_exact_overlap_uses_player_order() -> None:
    # Decomp tie path:
    # - ftCommon_8007DD7C keeps a `phi_r28` flag while walking the global fighter GObj list.
    # - When pushbox centers are exactly equal, fighters before/after the current GObj pick opposite
    #   signs for p_ftCommonData->x450.
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


@pytest.mark.integration
@pytest.mark.parametrize("record", [8600, 8601, 8602, 8603, 8604, 8605, 8606])
def test_common_grounded_player_nudge_does_not_push_over_fd_right_floor_edge(record: int) -> None:
    # Negative lock for the floor-bound safety gate:
    # - Preflight caught GracefulAttachedTurtle rows 8599..8606 as new seed==ref on_ground rows when
    #   common x450 was allowed to move p0 from x=85.5656967 to x=85.8656998.
    # - That push crosses the current FD floor segment endpoint in this simplified mpColl model.
    # - Until the full mpColl endpoint follow-up is implemented, the common nudge cannot own a move
    #   that immediately leaves the current floor segment.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E0E4
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/mp/mpcoll.c
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, record, 0)
    p = 0
    assert int(seed["on_ground"][p]) == 1
    assert int(ref["on_ground"][p]) == 1
    assert int(out["on_ground"][p]) == 1
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-6)
