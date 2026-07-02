from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_source_port0_maps_raw_slippi_last_hit_by_domain_on_noncontiguous_ports() -> None:
    # HilariousVillainousGiraffe uses raw controller ports P2/P4, so local attacker slot 1 must
    # write Slippi source port 3 rather than compact local slot 1.
    #
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by export)
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    dataset_path = (
        Path(__file__).resolve().parents[1]
        / "replays/validation/aggregate_recent/HilariousVillainousGiraffe.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    record = 134
    player = 0
    row = ds.rows[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    assert int(seed["source_port0"][0]) == 1
    assert int(seed["source_port0"][1]) == 3
    assert int(seed["last_hit_by"][player]) == 6
    assert int(ref["last_hit_by"][player]) == 3

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed(
            handle,
            np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride),
        )
        binding.step_input(
            handle,
            np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
            .copy()
            .reshape(1, input_stride),
            np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride),
        )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["last_hit_by"][player]) == int(ref["last_hit_by"][player]) == 3
