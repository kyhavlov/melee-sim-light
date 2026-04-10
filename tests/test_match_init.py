from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import (
    CHAR_FALCO,
    CHAR_FOX,
    MATCH_CONFIG_DTYPE,
    build_match_config_array,
)
from tools.modelplay.state_adapter import MSL_STAGE_FINAL_DESTINATION
from tools.modelplay.sim_env import SimSession


ACT_ENTRY = 0x0142
ANIM_NONE = 0xFFFFFFFF
STAGE_PATH = Path("data/stages/final_destination.json")


def _binding_or_skip():
    return pytest.importorskip("msl_binding")


def _init_handle_or_skip(binding, *, batch_size: int = 1, num_players: int = 2):
    try:
        return binding.init(batch_size=batch_size, num_players=num_players)
    except (MemoryError, RuntimeError) as exc:
        pytest.skip(f"missing local C init artifacts for match-init test: {exc}")


def _config_bytes(config: np.ndarray) -> np.ndarray:
    return config.view(np.uint8).reshape(config.shape[0], -1)


def _compare_bytes(rows: int = 1) -> np.ndarray:
    return np.zeros((rows, COMPARE_DTYPE.itemsize), dtype=np.uint8)


def _fd_spawn_points() -> np.ndarray:
    if not STAGE_PATH.exists():
        pytest.skip(f"missing local artifact: {STAGE_PATH}")
    data = json.loads(STAGE_PATH.read_text(encoding="utf-8"))
    points = data.get("spawn_points")
    if not isinstance(points, list) or len(points) < 2:
        pytest.skip(f"{STAGE_PATH}: missing spawn_points")
    return np.array([(float(p["x"]), float(p["y"])) for p in points], dtype=np.float32)


def test_match_config_dtype_matches_c_size() -> None:
    binding = _binding_or_skip()
    assert int(binding.sizes()["match_config"]) == MATCH_CONFIG_DTYPE.itemsize


def test_init_match_writes_valid_fox_falco_fd_entry_state() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding)
    try:
        config = build_match_config_array(
            num_players=2,
            char_ids=(CHAR_FOX, CHAR_FALCO),
            facing=(1, 0),
            stocks=4,
        )
        out = _compare_bytes()
        binding.init_match(handle, _config_bytes(config))
        binding.write_compare(handle, out)
        row = out.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)

    assert int(row["num_players"]) == 2
    assert int(row["stage_id"]) == MSL_STAGE_FINAL_DESTINATION
    assert row["char_id"][:2].tolist() == [CHAR_FOX, CHAR_FALCO]
    assert row["stocks"][:2].tolist() == [4, 4]
    assert np.all(row["percent"][:2] == np.float32(0.0))
    assert row["action_id"][:2].tolist() == [ACT_ENTRY, ACT_ENTRY]
    assert row["action_frame"][:2].tolist() == [-1, -1]
    assert row["animation_index"][:2].tolist() == [ANIM_NONE, ANIM_NONE]
    assert row["on_ground"][:2].tolist() == [0, 0]
    assert row["facing"][:2].tolist() == [1, 0]
    spawn = _fd_spawn_points()
    np.testing.assert_array_equal(row["pos_x"][:2], spawn[:2, 0])
    np.testing.assert_array_equal(row["pos_y"][:2], spawn[:2, 1])
    assert float(row["shield_hp"][0]) > 0.0
    assert float(row["shield_hp"][1]) > 0.0


def test_init_match_binding_rejects_too_few_config_rows() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding, batch_size=2)
    try:
        config = build_match_config_array(
            num_players=2,
            char_ids=(CHAR_FOX, CHAR_FALCO),
            facing=(1, 0),
            stocks=4,
        )
        with pytest.raises(ValueError):
            binding.init_match(handle, _config_bytes(config))
    finally:
        binding.destroy(handle)


def test_init_match_two_config_rows_initializes_two_batch_rows() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding, batch_size=2)
    try:
        row0 = build_match_config_array(
            num_players=2,
            char_ids=(CHAR_FOX, CHAR_FALCO),
            facing=(1, 0),
            stocks=4,
        )[0]
        row1 = build_match_config_array(
            num_players=2,
            char_ids=(CHAR_FALCO, CHAR_FOX),
            facing=(0, 1),
            stocks=3,
        )[0]
        config = np.zeros(2, dtype=MATCH_CONFIG_DTYPE)
        config[0] = row0
        config[1] = row1
        out = _compare_bytes(rows=2)

        binding.init_match(handle, _config_bytes(config))
        binding.write_compare(handle, out)
        rows = out.view(COMPARE_DTYPE).reshape((2,)).copy()
    finally:
        binding.destroy(handle)

    assert rows[0]["char_id"][:2].tolist() == [CHAR_FOX, CHAR_FALCO]
    assert rows[1]["char_id"][:2].tolist() == [CHAR_FALCO, CHAR_FOX]
    assert rows[0]["stocks"][:2].tolist() == [4, 4]
    assert rows[1]["stocks"][:2].tolist() == [3, 3]
    assert rows[0]["action_id"][:2].tolist() == [ACT_ENTRY, ACT_ENTRY]
    assert rows[1]["action_id"][:2].tolist() == [ACT_ENTRY, ACT_ENTRY]


def test_init_match_is_deterministic_after_same_inputs() -> None:
    binding = _binding_or_skip()
    config = build_match_config_array(
        num_players=2,
        char_ids=(CHAR_FOX, CHAR_FALCO),
        facing=(1, 0),
        stocks=4,
    )
    prev_inp = np.zeros((1, INPUT_DTYPE.itemsize), dtype=np.uint8)
    inp = np.zeros((1, INPUT_DTYPE.itemsize), dtype=np.uint8)

    outputs: list[bytes] = []
    for _ in range(2):
        handle = _init_handle_or_skip(binding)
        try:
            out = _compare_bytes()
            binding.init_match(handle, _config_bytes(config))
            binding.step_input(handle, prev_inp, inp)
            binding.write_compare(handle, out)
            outputs.append(bytes(out.reshape(-1)))
        finally:
            binding.destroy(handle)

    assert outputs[0] == outputs[1]


def test_no_allocations_after_match_init_enters_stepping() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding)
    try:
        config = build_match_config_array(
            num_players=2,
            char_ids=(CHAR_FOX, CHAR_FALCO),
            facing=(1, 0),
            stocks=4,
        )
        prev_inp = np.zeros((1, INPUT_DTYPE.itemsize), dtype=np.uint8)
        inp = np.zeros((1, INPUT_DTYPE.itemsize), dtype=np.uint8)
        out = _compare_bytes()

        binding.alloc_reset()
        binding.init_match(handle, _config_bytes(config))
        binding.step_input(handle, prev_inp, inp)
        binding.write_compare(handle, out)
        stats = binding.alloc_stats()
    finally:
        binding.destroy(handle)

    assert int(stats["calls"]) == 0
    assert int(stats["bytes"]) == 0


def test_modelplay_match_config_builder_smoke() -> None:
    config = build_match_config_array(
        num_players=2,
        char_ids=(CHAR_FALCO, CHAR_FOX),
        facing=(1, 0),
    )
    assert config.dtype == MATCH_CONFIG_DTYPE
    assert int(config[0]["stage_id"]) == MSL_STAGE_FINAL_DESTINATION
    assert config[0]["players"]["char_id"][:2].tolist() == [CHAR_FALCO, CHAR_FOX]


def test_modelplay_sim_session_sim_init_reset_without_dataset(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        "tools.modelplay.sim_env.build_slippi_ai_game",
        lambda state, controllers, *, viewpoint_port: {
            "frame_id": state.frame_id,
            "viewpoint_port": viewpoint_port,
        },
    )
    monkeypatch.setattr("tools.modelplay.sim_env.input_array_to_controllers", lambda input_arr: {})
    try:
        session = SimSession(
            dataset_path=None,
            start_mode="sim-init",
            char_ids=(CHAR_FOX, CHAR_FALCO),
            facing=(1, 0),
        )
    except (MemoryError, RuntimeError) as exc:
        pytest.skip(f"missing local C init artifacts for SimSession sim-init test: {exc}")

    try:
        try:
            env_out = session.reset()
        except RuntimeError as exc:
            pytest.skip(f"missing local C init artifacts for SimSession sim-init reset: {exc}")
        state = session.current_frame_state
    finally:
        session.close()

    assert env_out.needs_reset
    assert sorted(env_out.gamestates) == [1, 2]
    assert state.action_id[:2].tolist() == [ACT_ENTRY, ACT_ENTRY]
    assert state.action_frame[:2].tolist() == [-1, -1]
    assert state.char_id[:2].tolist() == [CHAR_FOX, CHAR_FALCO]
