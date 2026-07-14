from __future__ import annotations

import sys
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from melee_sim.decomp_port import (  # noqa: E402
    fox_fd_config,
    locomotion_tape,
    run_phase1,
)


def main() -> int:
    config = fox_fd_config(random_seed=0x12345678)
    inputs = locomotion_tape()
    first = run_phase1(config, inputs, timeout=10)
    second = run_phase1(config, inputs, timeout=10)

    if first.tobytes() != second.tobytes():
        raise AssertionError("repeated Phase 1 runs emitted different bytes")
    if len(first) != len(inputs):
        raise AssertionError("Phase 1 output row count does not match input")
    if not np.array_equal(first["frame_id"], np.arange(len(inputs), dtype=np.int32)):
        raise AssertionError("Phase 1 frame ids are not contiguous")
    if not np.all(first["stage_id"] == 32):
        raise AssertionError("Phase 1 output stage is not Final Destination")
    if not np.all(first["char_id"][:, :2] == 1):
        raise AssertionError("Phase 1 output fighters are not both Fox")
    if not np.any(first["on_ground"][:, 0]):
        raise AssertionError("Phase 1 tape never established floor contact")

    actions = set(int(value) for value in first["action_id"][:, 0])
    expected_families = {
        "walk": {0x0F, 0x10, 0x11},
        "turn": {0x12, 0x13},
        "dash/run": {0x14, 0x15, 0x16, 0x17},
        "knee bend": {0x18},
        "ground jump": {0x19, 0x1A},
        "fall": {0x1D, 0x1E, 0x1F},
        "landing": {0x2A},
    }
    missing = [name for name, ids in expected_families.items() if not actions & ids]
    if missing:
        raise AssertionError(f"Phase 1 tape missed action families: {', '.join(missing)}")

    print(
        f"Phase 1 deterministic locomotion: {len(first)} frames; "
        f"actions={sorted(actions)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
