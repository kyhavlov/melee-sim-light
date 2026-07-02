from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


ACT_DAMAGE_FALL = 0x0026
ACT_DAMAGE_FLY_N = 0x0058
STATE_FLAG_2218_ALLOW_INTERRUPT = 0x80


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _dataset_path(root: Path, rel: str) -> Path:
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {rel}")
    return dataset_path


def _run_one_step(dataset_path: Path, record: int, seed_mutator=None) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    assert int(row.shape[0]) == 1

    seed_t = row["seed_t"].copy()
    if seed_mutator is not None:
        seed_mutator(seed_t)
    seed_bytes = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(
        row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, input_stride)
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    return row["seed_t"].reshape(-1)[0].copy(), row["ref_t1"].reshape(-1)[0].copy(), out


def test_damagefly_to_damagefall_entry_uses_destination_phys_before_integration() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(
        root, "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    )

    seed, ref, out = _run_one_step(dataset_path, 710)
    p = 0
    assert int(seed["action_id"][p]) == ACT_DAMAGE_FLY_N
    assert int(ref["action_id"][p]) == ACT_DAMAGE_FALL

    assert int(out["action_id"][p]) == ACT_DAMAGE_FALL
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-7)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


def test_steady_damagefall_keeps_seed_driven_current_frame_displacement() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(
        root,
        "replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.slpz",
    )

    seed, ref, out = _run_one_step(dataset_path, 2959)
    p = 0
    assert int(seed["action_id"][p]) == ACT_DAMAGE_FALL
    assert (int(seed["state_flags"][p, 0]) & STATE_FLAG_2218_ALLOW_INTERRUPT) == 0
    assert int(ref["action_id"][p]) == ACT_DAMAGE_FALL

    assert int(out["action_id"][p]) == ACT_DAMAGE_FALL
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-7)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


def test_steady_damagefall_allow_interrupt_uses_pre_integration_gravity() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(
        root,
        "replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.slpz",
    )

    seed, ref, out = _run_one_step(dataset_path, 8326)
    p = 0
    assert int(seed["action_id"][p]) == ACT_DAMAGE_FALL
    assert (int(seed["state_flags"][p, 0]) & STATE_FLAG_2218_ALLOW_INTERRUPT) != 0
    assert int(ref["action_id"][p]) == ACT_DAMAGE_FALL
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-7)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    def _clear_descent(seed_t):
        seed_t["state_flags"][0, p, 0] = np.uint8(
            int(seed_t["state_flags"][0, p, 0]) & ~STATE_FLAG_2218_ALLOW_INTERRUPT
        )
        seed_t["speed_y_self"][0, p] = np.float32(0.1)

    _, _, out_without_descent = _run_one_step(dataset_path, 8326, seed_mutator=_clear_descent)
    assert float(out_without_descent["speed_y_self"][p]) != pytest.approx(
        float(ref["speed_y_self"][p]), abs=1e-7
    )
    assert float(out_without_descent["pos_y"][p]) != pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
