from __future__ import annotations

import copy
from pathlib import Path

import pytest
from peppi_py import _read_slippi

from tools.validation import validate_replay
from tools.validation.slpz import replay_path_for_peppi

ROOT = Path(__file__).resolve().parents[1]
REPLAY = ROOT / "replays/validation/cpu_inputs/exphil_peach_r1.slpz"


@pytest.fixture
def cpu_game():
    if not all(p.is_file() for p in (REPLAY, validate_replay.NATIVE, validate_replay.NATIVE_BINARY)):
        pytest.skip("native CPU replay validation artifacts are unavailable")
    with replay_path_for_peppi(REPLAY) as path:
        return _read_slippi(str(path), False)


def run_game(game, start):
    return validate_replay.load_native().validate_replay(
        game.frames,
        start,
        game.metadata,
        qemu=str(validate_replay.QEMU),
        sysroot=str(validate_replay.SYSROOT),
        binary=str(validate_replay.NATIVE_BINARY),
        data_dir=str(validate_replay.game_data_dir()),
        native=True,
        frames_limit=5000,
        timeout=20.0,
    )


@pytest.mark.parametrize("reverse_players", [False, True])
def test_cpu_processed_input_prefix_is_bit_exact(cpu_game, reverse_players):
    start = copy.deepcopy(cpu_game.start)
    assert [p["type"] for p in start["players"]] == ["Human", "Cpu"]
    if reverse_players:
        start["players"].reverse()
    # The full capture has a historical fnmsubs zero-sign mismatch at frame
    # 5109. This prefix tests input replay without weakening that comparison.
    result = run_game(cpu_game, start)
    assert result["pass"] is True
    assert result["frames"] == 5000
    assert result["mismatch_count"] == 0
    assert result["signed_zero_equal_count"] == 0


@pytest.mark.parametrize("level", [None, 0, 10])
def test_cpu_level_is_required(cpu_game, level):
    start = copy.deepcopy(cpu_game.start)
    start["players"][1]["cpu_level"] = level
    with pytest.raises(ValueError, match="CPU replay player requires a level"):
        run_game(cpu_game, start)


def test_cpu_replay_is_not_exported_as_physical_controller_tape(cpu_game, tmp_path):
    output = tmp_path / "cpu.mslrpb"
    with pytest.raises(ValueError, match="controller-only benchmark tape"):
        validate_replay.load_native().write_benchmark_case(
            cpu_game.frames, cpu_game.start, cpu_game.metadata, str(output)
        )
    assert not output.exists()
