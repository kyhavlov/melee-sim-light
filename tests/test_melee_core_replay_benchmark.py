from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

import pytest
from peppi_py import _read_slippi

from tools.validation.prepare_replay_benchmark import (
    DEFAULT_CHARACTERS,
    DEFAULT_STAGES,
    _cached_frame_count,
    prepare,
)
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
    assert magic == b"MSLRPB03"
    assert version == 3
    assert input_size == 52
    assert stored_frames == frame_count
    assert output.stat().st_size == header_size + input_size * frame_count
    assert _cached_frame_count(output) == frame_count


@pytest.mark.parametrize("include_human", [False, True])
def test_benchmark_preparation_reports_cpu_exclusions(
    tmp_path: Path, capsys, include_human: bool
) -> None:
    cpu_replay = ROOT / "replays/validation/cpu_inputs/cpu_ice_climbers_l5.slpz"
    if not all(p.is_file() for p in (NATIVE, STARTER_REPLAY, cpu_replay)):
        pytest.skip("benchmark preprocessing artifacts are unavailable")
    cpu_suite = json.loads((ROOT / "replays/suites/cpu_inputs.json").read_text())
    replays = [cpu_suite["replays"][0]]
    if include_human:
        human_suite = json.loads((ROOT / "replays/suites/aggregate_recent.json").read_text())
        replays.insert(0, next(
            entry for entry in human_suite["replays"]
            if entry["replay"] == STARTER_REPLAY.relative_to(ROOT).as_posix()
        ))
    suite = tmp_path / "suite.json"
    suite.write_text(json.dumps({
        "name": "benchmark_inputs", "ucf_enabled": True,
        "ucf_cardinals_1_0_enabled": True, "replays": replays,
    }))
    output = tmp_path / "cases.tsv"
    args = argparse.Namespace(
        suite=suite, output=output, characters=DEFAULT_CHARACTERS,
        stages=DEFAULT_STAGES, force=False,
    )
    if not include_human:
        with pytest.raises(ValueError, match="no controller-only replays"):
            prepare(args)
        assert not output.exists()
        assert not list(tmp_path.rglob("*.mslrpb"))
        return

    prepare(args)
    manifest = output.read_text()
    lines = manifest.splitlines()
    assert lines[0] == "# MSL replay benchmark cases v3"
    assert len(lines) == 2
    tape, replay = lines[1].split("\t")
    assert replay == STARTER_REPLAY.relative_to(ROOT).as_posix()
    assert _cached_frame_count(Path(tape)) is not None
    assert "selected=1 skipped_cpu=1" in capsys.readouterr().out
    prepare(args)
    assert output.read_text() == manifest
    assert "built=0 reused=1" in capsys.readouterr().out
