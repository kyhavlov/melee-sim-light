from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _step_one_row(*, binding, row: np.ndarray, num_players: int) -> np.void:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(num_players))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int
    seed_action: int
    ref_action: int
    ref_facing: int


_BASE = "replays/validation/cardinal_1.0_recent"
_CASES = [
    _Case(f"{_BASE}/GracefulAttachedTurtle.slpz", 8103, 0, 1, 12, 0),
    _Case(f"{_BASE}/GracefulAttachedTurtle.slpz", 9253, 1, 0, 12, 1),
    _Case(f"{_BASE}/QuerulousGrandDinosaur.slpz", 3470, 1, 0, 12, 1),
    _Case(f"{_BASE}/TreasuredBackKangaroo.slpz", 6523, 0, 1, 12, 0),
]


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES)
def test_rebirth_entry_faces_toward_stage_center_from_respawn_side(case: _Case) -> None:
    # Replay-real lock:
    # - Dead* -> Rebirth runs Fighter_UnkInitReset_80067C98 before the Rebirth motion is active.
    # - That reset path reloads facing from the player spawn-facing lane, so the first Rebirth frame
    #   must not inherit the stale Dead* facing.
    # refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitReset_80067C98
    # refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_Dead{Left,Right}_Anim
    # data/stages/final_destination.json: cam_bounds_world, respawn_points
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[case.record : case.record + 1]
    p = int(case.p)

    assert int(row["seed_t"]["action_id"][0, p]) == case.seed_action
    assert int(row["ref_t1"]["action_id"][0, p]) == case.ref_action
    assert int(row["ref_t1"]["facing"][0, p]) == case.ref_facing

    out = _step_one_row(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["facing"][p]) == int(row["ref_t1"]["facing"][0, p])
    assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])
