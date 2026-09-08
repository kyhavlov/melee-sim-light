from __future__ import annotations

import struct
from pathlib import Path

import pytest
from peppi_py import _read_slippi

from tools.validation.prepare_replay_benchmark import _cached_frame_count
from tools.validation.validate_replay import NATIVE, load_native
from tools.validation.slpz import replay_path_for_peppi


ROOT = Path(__file__).resolve().parents[1]
STARTER_REPLAY = (
    ROOT
    / "replays"
    / "validation"
    / "aggregate_recent"
    / "Game_20260514T181413.slpz"
)


def test_native_preprocessor_writes_packed_benchmark_case(tmp_path: Path) -> None:
    if not NATIVE.is_file() or not STARTER_REPLAY.is_file():
        pytest.skip("melee core benchmark preprocessing artifacts are unavailable")

    output = tmp_path / "starter.mslrpb"
    with replay_path_for_peppi(STARTER_REPLAY) as replay_path:
        game = _read_slippi(str(replay_path), False)
        frame_count = load_native().write_benchmark_case(
            game.frames,
            game.start,
            game.metadata,
            str(output),
            ucf_cardinals_1_0_enabled=True,
            ucf_shield_sdi_enabled=True,
            ucf_sdi_enabled=True,
        )

    magic, version, header_size, input_size, stored_frames = struct.unpack(
        "<8sIIII", output.read_bytes()[:24]
    )
    assert magic == b"MSLRPB02"
    assert version == 2
    assert input_size == 52
    assert stored_frames == frame_count
    assert output.stat().st_size == header_size + input_size * frame_count
    assert _cached_frame_count(output) == frame_count
