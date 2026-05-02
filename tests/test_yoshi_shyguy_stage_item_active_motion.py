from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        ).copy()
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0], row["ref_t1"][0]
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_active_yoshi_shyguy_integrates_visible_velocity() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 1235
    row = ds.samples[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]

    # Replay-real active Shy Guys: kind 0xD2, owner=-1, state 1. The admitted causal slice is the
    # generic item-position integration with current visible x40_vel; hidden dynamic-bone velocity
    # remains a separate open owner, so pos_y is asserted as the causal integration formula rather
    # than replay parity.
    # refs/melee/src/melee/it/items/itheiho.c::itHeiho_UnkMotion1_Phys
    # refs/melee/src/melee/it/item.c::Item_802697D4
    active_slots = [
        it
        for it, item in enumerate(seed["items"])
        if int(item["exists"])
        and int(item["type"]) == 0xD2
        and int(item["owner"]) == -1
        and int(item["state"]) == 1
    ]
    assert active_slots[:5] == [0, 1, 2, 3, 4]

    out, _ = _step_one_row(dataset_path, record)
    for it in active_slots[:5]:
        item = seed["items"][it]
        assert float(out["items"][it]["pos_x"]) == pytest.approx(
            float(item["pos_x"]) + float(item["vel_x"]), abs=1e-6
        )
        assert float(out["items"][it]["pos_y"]) == pytest.approx(
            float(item["pos_y"]) + float(item["vel_y"]), abs=1e-6
        )
        assert float(out["items"][it]["pos_x"]) == pytest.approx(
            float(ref["items"][it]["pos_x"]), abs=1e-6
        )


@pytest.mark.integration
def test_state4_yoshi_shyguy_integrates_visible_velocity() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/CheeryNumbMonkey.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 614
    row = ds.samples[record]
    seed = row["seed_t"]

    # refs/melee/src/melee/it/items/itheiho.c::itHeiho_UnkMotion4_Phys
    # refs/melee/src/melee/it/item.c::Item_802697D4
    active_slots = [
        it
        for it, item in enumerate(seed["items"])
        if int(item["exists"])
        and int(item["type"]) == 0xD2
        and int(item["owner"]) == -1
        and int(item["state"]) == 4
    ]
    assert active_slots == [3]

    out, _ = _step_one_row(dataset_path, record)
    for it in active_slots:
        item = seed["items"][it]
        assert float(out["items"][it]["pos_x"]) == pytest.approx(
            float(item["pos_x"]) + float(item["vel_x"]), abs=1e-6
        )
        assert float(out["items"][it]["pos_y"]) == pytest.approx(
            float(item["pos_y"]) + float(item["vel_y"]), abs=1e-6
        )
