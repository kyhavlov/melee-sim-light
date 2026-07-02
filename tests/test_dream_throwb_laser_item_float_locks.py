from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride)
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        binding.reseed_seed(handle, seed_bytes.copy())
        binding.step_input(handle, prev_input_bytes.copy(), input_bytes.copy())
        binding.write_compare(handle, out_bytes)
        return row["seed_t"][0], row["ref_t1"][0], out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    finally:
        binding.destroy(handle)


@pytest.mark.parametrize(
    ("dataset", "record", "slot"),
    (
        ("ShadyDecimalStarling.slpz", 5182, 1),
        ("ShadyDecimalStarling.slpz", 5184, 1),
        ("FlippantEnchantedHorse.slpz", 8579, 1),
        ("FlippantEnchantedHorse.slpz", 8581, 2),
    ),
)
def test_dream_throwb_state1_lasers_match_visible_item_rows(
    dataset: str, record: int, slot: int
) -> None:
    dataset_path = Path(f"replays/validation/dream_land_recent/{dataset}")
    seed, ref, out = _step_one_row(dataset_path, record)
    assert int(seed["stage_id"]) == 28
    assert int(ref["items"][slot]["exists"]) == 1
    assert int(out["items"][slot]["exists"]) == 1
    assert int(out["items"][slot]["type"]) == int(ref["items"][slot]["type"])
    assert int(out["items"][slot]["state"]) == int(ref["items"][slot]["state"])
    assert int(out["items"][slot]["spawn_id"]) == int(ref["items"][slot]["spawn_id"])
    assert float(out["items"][slot]["pos_x"]) == pytest.approx(float(ref["items"][slot]["pos_x"]))
    assert float(out["items"][slot]["pos_y"]) == pytest.approx(float(ref["items"][slot]["pos_y"]))
    assert float(out["items"][slot]["vel_x"]) == pytest.approx(float(ref["items"][slot]["vel_x"]))
    assert float(out["items"][slot]["vel_y"]) == pytest.approx(
        float(ref["items"][slot]["vel_y"]), abs=5e-6
    )


def test_dream_throwb_final_pulse_consumes_prior_laser_and_compacts_new_article() -> None:
    dataset_path = Path(
        "replays/validation/dream_land_recent/"
        "FlippantEnchantedHorse.slpz"
    )
    seed, ref, out = _step_one_row(dataset_path, 8583)
    victim = 0
    thrower = 1

    assert int(seed["stage_id"]) == 28
    assert int(seed["action_id"][thrower]) == 220  # ThrowB
    assert int(seed["items"][1]["exists"]) == 1
    assert int(seed["items"][1]["type"]) == 55
    assert int(seed["items"][1]["state"]) == 1
    assert int(seed["items"][1]["spawn_id"]) == 53

    assert int(out["combo_count"][thrower]) == int(ref["combo_count"][thrower])
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim])
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim])
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]))
    assert int(out["instance_id"][victim]) == int(ref["instance_id"][victim])

    kept = out["items"][1]
    expected = ref["items"][1]
    assert int(kept["exists"]) == int(expected["exists"]) == 1
    assert int(kept["type"]) == int(expected["type"]) == 55
    assert int(kept["state"]) == int(expected["state"]) == 1
    assert int(kept["spawn_id"]) == int(expected["spawn_id"]) == 54
    assert float(kept["pos_x"]) == pytest.approx(float(expected["pos_x"]), abs=2e-5)
    assert float(kept["pos_y"]) == pytest.approx(float(expected["pos_y"]), abs=2e-5)
    assert float(kept["vel_x"]) == pytest.approx(float(expected["vel_x"]), abs=2e-5)
    assert float(kept["vel_y"]) == pytest.approx(float(expected["vel_y"]), abs=2e-5)
    assert int(out["items"][2]["exists"]) == int(ref["items"][2]["exists"]) == 0


def test_throwb_laser_non_crossed_body_consume_stays_normal() -> None:
    dataset_path = Path(
        "replays/validation/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    seed, ref, out = _step_one_row(dataset_path, 2520)
    victim = 0
    slot = 1
    assert int(seed["stage_id"]) == 32
    assert int(seed["action_id"][1]) == 220
    assert int(seed["items"][slot]["type"]) == 55
    assert int(seed["items"][slot]["state"]) == 1
    assert int(ref["action_id"][victim]) == 86
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim])
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim]) == 4
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]))
    assert int(out["items"][slot]["exists"]) == 1
    assert int(out["items"][slot]["type"]) == int(ref["items"][slot]["type"]) == 55
    assert int(out["items"][slot]["state"]) == int(ref["items"][slot]["state"]) == 1
    assert int(out["items"][slot]["spawn_id"]) == int(ref["items"][slot]["spawn_id"]) == 70
