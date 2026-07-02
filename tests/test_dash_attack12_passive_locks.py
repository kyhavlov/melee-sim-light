from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
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


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    assert int(row.shape[0]) == 1

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = (
            np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, seed_stride)
            .copy()
        )
        prev_input_bytes = (
            np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, input_stride)
            .copy()
        )
        input_bytes = (
            np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, input_stride)
            .copy()
        )
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        return row["seed_t"][0], row["ref_t1"][0], out
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_dash_x4_zero_row_does_not_enter_escapef() -> None:
    # Replay-real lock for Dash IASA guard branch ordering/gating:
    # - ftCo_Dash_IASA only routes to ftCo_80099264 when dash.x4 != 0.
    # - with dash.x4 == 0 this row must remain Dash (action/submotion).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_80099264
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    record = 4529
    p = 0
    seed, ref, out = _step_one_row(dataset_path, record)
    assert int(seed["action_id"][p]) == 20
    assert int(ref["action_id"][p]) == 20
    assert int(seed["dash_x4"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 20
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p]) == 12


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "replays/validation/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.slpz",
            5325,
            0,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.slpz",
            5330,
            0,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            5404,
            0,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            5409,
            0,
        ),
    ],
)
def test_attack12_ft_80084fa8_rows_match_ref_velocity(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real lock for Attack11/12/13 Phys ownership:
    # - motion state table maps Attack12/13 Phys to ftCo_Attack11_Phys.
    # - ftCo_Attack11_Phys uses ft_80084FA8 -> ft_80085030 (TransN velocity target / friction fallback).
    # refs/melee/src/melee/ft/ftmotionstates.c (Attack11/12/13 entries)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::ftCo_Attack11_Phys
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80084FA8,ft_80085030}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, ref, out = _step_one_row(dataset_path, record)
    assert int(seed["action_id"][p]) == int(ref["action_id"][p]) == 45
    assert int(seed["animation_index"][p]) == int(ref["animation_index"][p]) == 47

    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=2e-6
    )
    assert float(out["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=2e-6
    )


def test_passive_stand_action_owns_animation_index() -> None:
    # Runtime lock for PassiveStand animation-index ownership:
    # - PassiveStand maps to ftCo_SM_PassiveStandF/B.
    # refs/melee/src/melee/ft/ftmotionstates.c (ftCo_MS_PassiveStandF/B entries)
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(32)  # Final Destination
    seed["num_players"][0] = np.uint8(2)
    seed["char_id"][0, 0] = np.uint8(1)  # Fox
    seed["stocks"][0, :2] = np.uint8(4)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["facing"][0, 0] = np.uint8(1)
    seed["action_id"][0, 0] = np.uint16(200)  # PassiveStandF
    seed["animation_index"][0, 0] = np.uint32(999)  # intentionally wrong
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)

    prev_input_bytes = np.zeros((1, input_stride), dtype=np.uint8)
    input_bytes = np.zeros((1, input_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
        assert input_stride == INPUT_DTYPE.itemsize

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]

        assert int(out["action_id"][0]) == 200
        assert int(out["animation_index"][0]) == 200
    finally:
        binding.destroy(handle)
