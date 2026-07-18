from __future__ import annotations

import argparse
import hashlib
import os
import struct
from pathlib import Path

from peppi_py import _read_slippi

from tools.validation.validate_replay import (
    DEFAULT_CHARACTERS,
    DEFAULT_STAGES,
    ROOT,
    _parse_characters,
    _parse_stages,
    load_native,
    load_suite_cases,
    ReplayCase,
)
from tools.validation.slpz import replay_path_for_peppi


FORMAT_VERSION = 1
DEFAULT_SUITE = ROOT / "replays/suites/melee_core_aggregate.json"
DEFAULT_OUTPUT = ROOT / "build/melee_core/benchmark/cases.tsv"


def _cache_key(case: ReplayCase) -> str:
    replay = case.replay
    stat = replay.stat()
    identity = "\0".join(
        (
            str(FORMAT_VERSION),
            str(replay.resolve()),
            str(stat.st_size),
            str(stat.st_mtime_ns),
            str(case.ucf_cardinals_1_0_enabled),
            str(case.ucf_shield_sdi_enabled),
            str(case.ucf_sdi_enabled),
            str(case.played_on),
        )
    )
    return hashlib.sha256(identity.encode()).hexdigest()[:24]


def _cached_frame_count(path: Path) -> int | None:
    try:
        with path.open("rb") as stream:
            header = stream.read(24)
        magic, version, header_size, input_size, frame_count = struct.unpack("<8sIIII", header)
    except (OSError, struct.error):
        return None
    if (
        magic != b"MSLRPB01"
        or version != FORMAT_VERSION
        or header_size < 24
        or input_size != 52
        or path.stat().st_size != header_size + input_size * frame_count
    ):
        return None
    return frame_count


def prepare(args: argparse.Namespace) -> None:
    output = args.output.resolve()
    case_dir = output.parent / "cases"
    case_dir.mkdir(parents=True, exist_ok=True)
    _, cases = load_suite_cases(
        args.suite.resolve(),
        characters=_parse_characters(args.characters),
        stages=_parse_stages(args.stages),
    )
    native = load_native()
    rows: list[str] = []
    total_frames = 0
    built = 0
    reused = 0
    for case in cases:
        case_path = case_dir / f"{_cache_key(case)}.mslrpb"
        frame_count = None if args.force else _cached_frame_count(case_path)
        if frame_count is None:
            temporary = case_path.with_suffix(f".tmp.{os.getpid()}")
            try:
                with replay_path_for_peppi(case.replay) as peppi_path:
                    game = _read_slippi(str(peppi_path), False)
                    metadata = game.metadata
                    if case.played_on is not None:
                        metadata = dict(metadata)
                        metadata["playedOn"] = case.played_on
                    frame_count = native.write_benchmark_case(
                        game.frames,
                        game.start,
                        metadata,
                        str(temporary),
                        ucf_cardinals_1_0_enabled=case.ucf_cardinals_1_0_enabled,
                        ucf_shield_sdi_enabled=case.ucf_shield_sdi_enabled,
                        ucf_sdi_enabled=case.ucf_sdi_enabled,
                    )
                os.replace(temporary, case_path)
            finally:
                temporary.unlink(missing_ok=True)
            built += 1
        else:
            reused += 1
        total_frames += frame_count
        rows.append(f"{case_path}\t{case.display_path}\n")

    temporary_manifest = output.with_suffix(f".tmp.{os.getpid()}")
    try:
        temporary_manifest.write_text(
            "# MSL replay benchmark cases v1\n" + "".join(rows)
        )
        os.replace(temporary_manifest, output)
    finally:
        temporary_manifest.unlink(missing_ok=True)
    print(
        f"benchmark cases: selected={len(cases)} frames={total_frames:,} "
        f"built={built} reused={reused} manifest={output.relative_to(ROOT)}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Predecode replay inputs for the native melee-core benchmark"
    )
    parser.add_argument("--suite", type=Path, default=DEFAULT_SUITE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--characters", default=DEFAULT_CHARACTERS)
    parser.add_argument("--stages", default=DEFAULT_STAGES)
    parser.add_argument("--force", action="store_true")
    prepare(parser.parse_args())


if __name__ == "__main__":
    main()
