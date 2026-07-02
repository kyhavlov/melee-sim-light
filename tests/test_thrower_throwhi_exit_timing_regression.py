from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


_BASE = "replays/validation/cardinal_1.0_recent"
_REQUIRED_ARTIFACTS = (
    "data/moves/fox.json",
    "data/moves/falco.json",
    "data/anims/fox.tracks.bin",
    "data/anims/falco.tracks.bin",
    "data/characters/fox.json",
    "data/characters/falco.json",
)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int
    seed_frame: int
    ref_frame: int


def _skip_if_required_artifacts_missing(root: Path) -> None:
    missing = [rel for rel in _REQUIRED_ARTIFACTS if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local extracted artifacts: {', '.join(missing)}")


def _step_one_record(*, binding, row: np.ndarray, num_players: int) -> np.ndarray:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(num_players))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.slpz", 3884, 1, 3761, 3762),
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.slpz", 4216, 1, 4093, 4094),
        _Case(f"{_BASE}/GracefulAttachedTurtle.slpz", 252, 0, 129, 130),
        _Case(f"{_BASE}/GracefulAttachedTurtle.slpz", 3438, 1, 3315, 3316),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.slpz", 3105, 0, 2982, 2983),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.slpz", 5419, 0, 5296, 5297),
        _Case(f"{_BASE}/TreasuredBackKangaroo.slpz", 2757, 1, 2634, 2635),
        _Case(f"{_BASE}/TreasuredBackKangaroo.slpz", 3881, 0, 3758, 3759),
    ],
)
def test_throwhi_anim_end_exits_to_wait(case: _Case) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > int(case.record), f"replay too short: num_records={int(samples.shape[0])}"

    row = samples[case.record : case.record + 1]
    p = int(case.p)

    assert int(row["seed_t"]["frame_id"][0]) == int(case.seed_frame)
    assert int(row["ref_t1"]["frame_id"][0]) == int(case.ref_frame)

    # Locked offender shape on baseline: seed already in ThrowHi, but ref_t1 has exited to Wait.
    assert int(row["seed_t"]["action_id"][0, p]) == 221
    assert int(row["ref_t1"]["action_id"][0, p]) == 14

    # ThrowHi terminal frame preconditions used by this regression slice.
    assert int(row["seed_t"]["action_frame"][0, p]) == 38
    assert int(row["seed_t"]["animation_index"][0, p]) == 249
    assert int(row["ref_t1"]["action_frame"][0, p]) == 0
    assert int(row["ref_t1"]["animation_index"][0, p]) == 2
    assert int(row["seed_t"]["on_ground"][0, p]) == 1
    assert int(row["ref_t1"]["on_ground"][0, p]) == 1

    # Clean context: no hitlag/hitstun and no active grab ownership on the thrower.
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    assert int(row["seed_t"]["grab_owner_port"][0, p]) == 0xFF

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])
    assert int(out["hitlag"][p]) == int(row["ref_t1"]["hitlag"][0, p])
    assert int(out["hitstun"][p]) == int(row["ref_t1"]["hitstun"][0, p])
