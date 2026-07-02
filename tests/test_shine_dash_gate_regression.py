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
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _step_one_record(*, binding, row: np.ndarray, num_players: int) -> np.void:
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


_BASE = "replays/validation/cardinal_1.0_recent"
_AGG_BASE = "replays/validation/aggregate_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        # Replay-real negative lock: Dash (20) should not directly dispatch to SpecialLwStart.
        _Case(f"{_BASE}/GracefulAttachedTurtle.slpz", 4529, 0, 20, 20),
        # Replay-real negative lock: RunBrake IASA does not route through ftCo_800D68C0, so a
        # same-frame B+down input must not enter Reflector/shine from RunBrake.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
        _Case(f"{_AGG_BASE}/PutridJoyousOryx.slpz", 575, 0, 23, 39),
        # Replay-real negative lock: GuardSetOff_IASA is empty, so a B+down row must not enter
        # Reflector/shine from GuardSetOff and create a replay-false Shine Start hit.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_IASA
        _Case(f"{_AGG_BASE}/PositiveRevolvingHyena.slpz", 4884, 1, 181, 181),
        # Adjacent positive control: SquatWait (40) still dispatches to SpecialLwStart.
        _Case(f"{_BASE}/GracefulAttachedTurtle.slpz", 148, 0, 40, 360),
    ],
)
def test_shine_entry_respects_dash_iasa_gate(case: _Case) -> None:
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

    assert int(row["seed_t"]["action_id"][0, p]) == int(case.seed_action)
    assert int(row["ref_t1"]["action_id"][0, p]) == int(case.ref_action)
    assert int(row["seed_t"]["on_ground"][0, p]) == 1
    assert int(row["ref_t1"]["on_ground"][0, p]) == 1
    assert int(row["input_t"]["p"][0, p]["buttons"]) & 0x0200  # B held/pressed lane.

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])
    assert int(out["hitlag"][p]) == int(row["ref_t1"]["hitlag"][0, p])
    assert int(out["hitstun"][p]) == int(row["ref_t1"]["hitstun"][0, p])
