"""Verify Slippi replays in the simulator and backfill stored fighter state."""

from __future__ import annotations

import argparse
import concurrent.futures
import gzip
import itertools
import json
import os
import shutil
import struct
import sys
import tempfile
import time
from pathlib import Path
from zipfile import BadZipFile, ZIP_DEFLATED, ZipFile, ZipInfo

from peppi_py import _read_slippi

from tools.validation import validate_replay as validation
from tools.validation.admission import (
    capture_issues,
    is_ucf_08,
    replay_events,
    slippi_patches,
    whispy_patch,
)
from tools.validation.slpz import decompress_slpz
from tools.validation.stored import POST_FRAME, STORED_END, STORED_OFFSET, _sizes, expected_stored


REPLAY_SUFFIXES = (".slp", ".slp.gz", ".slpz", ".slpz.gz")
SLIPPI_ICE_CLIMBERS = 14
DEFAULT_COMPRESSION_LEVEL = 5


def _decode_member(archive: ZipFile, member: ZipInfo) -> tuple[str, bytes]:
    raw = archive.read(member)
    name = member.filename
    if name.lower().endswith(".gz"):
        raw = gzip.decompress(raw)
        name = name[:-3]
    if name.lower().endswith(".slpz"):
        raw = decompress_slpz(raw)
        name = name[:-1]
    return name, raw


def _finalized_prefix(raw: bytes) -> bytes:
    """Repair the envelope when the source ends on a finalized frame bookend."""
    try:
        sizes, count = _sizes(raw)
    except ValueError:
        return raw
    pos = 17 + 3 * count
    while pos < len(raw):
        event = raw[pos]
        size = sizes[event]
        if not size or pos + 1 + size > len(raw):
            return raw
        if pos + 1 + size == len(raw) and event == 0x3C and size >= 8:
            frame, finalized = struct.unpack_from(">ii", raw, pos + 1)
            if frame == finalized:
                # Rebuild just the UBJSON envelope. The replay remains unfinished.
                return raw[:11] + struct.pack(">I", len(raw) - 15) + raw[15:] + b"U\x08metadata{}}"
        pos += 1 + size
    return raw


def _last_stock_winner(game, frame: int) -> str | None:
    players = game.start["players"]
    if (len(players) != 2 or game.start["is_teams"] or
            not 0 <= frame < 60 * game.start["timer"]):
        return None
    ports = game.frames.field("ports")
    stocks = {}
    for player in players:
        port = player["port"]
        post = ports.field(port).field("leader").field("post")
        stocks[port] = post.field("stocks")[-1].as_py()
    if None in stocks.values() or list(stocks.values()).count(0) != 1:
        return None
    return next(port for port, count in stocks.items() if count > 0)


def _regenerate_member(archive, member, native, pool, timeout, relaxed_floats,
                       include_finalized_prefixes=False):
    try:
        name, raw = _decode_member(archive, member)
        repaired_envelope = False
        if include_finalized_prefixes and 15 + int.from_bytes(raw[11:15], "big") > len(raw):
            repaired = _finalized_prefix(raw)
            repaired_envelope = repaired is not raw
            raw = repaired
        # Peppi's file API need only see one member per worker. Keep its
        # temporary file outside the input archive and remove it after parsing.
        with tempfile.NamedTemporaryFile(suffix=".slp") as source:
            source.write(raw)
            source.flush()
            try:
                game = _read_slippi(source.name, False)
            except OSError as exc:
                return None, {"status": "parse_failed", "error": str(exc)}
        if 15 + int.from_bytes(raw[11:15], "big") > len(raw):
            return None, {"status": "unsupported", "reasons": ["incomplete-history"]}
        sizes, _ = _sizes(raw)
        post_size = sizes[POST_FRAME]
        if post_size < STORED_END - 1 and post_size not in (0x50, STORED_OFFSET - 1):
            return None, {"status": "unsupported", "reasons": ["post-frame-layout"]}
        try:
            codes, bookend = replay_events(raw)
        except ValueError as exc:
            return None, {"status": "parse_failed", "error": str(exc)}
        missing_end = (
            game.end is None and bookend is not None and len(game.frames) > 0
            and game.frames.field("id")[-1].as_py() == bookend[0]
        )
        finalized_end = missing_end and bookend[0] == bookend[1]
        finalized_through = None
        if missing_end and include_finalized_prefixes and bookend[1] < bookend[0]:
            finalized_through = bookend[1]
        allow_missing_end = finalized_end or finalized_through is not None
        legacy = is_ucf_08(codes)
        played_on = game.metadata.get("playedOn")
        if (played_on is None and (legacy or allow_missing_end) and
                game.start.get("scene", {}).get("major") == 8):
            # The netplay scene still identifies the profile without metadata.
            played_on = "network"
        issues = capture_issues(game, raw, played_on=played_on, codes=codes, allow_ucf_08=True,
                                allow_missing_game_end=allow_missing_end,
                                finalized_through=finalized_through)
        # Transform can publish uninitialized L-cancel data. The native comparison
        # checks the recorded byte on every frame, including the transform.
        issues = [issue for issue in issues if issue != "uninitialized-transform-lcancel"]
        if issues:
            return None, {"status": "unsupported", "reasons": issues}
        winner = None
        if finalized_end:
            if not repaired_envelope:
                winner = _last_stock_winner(game, bookend[0])
            if winner is None and not include_finalized_prefixes:
                return None, {"status": "unsupported", "reasons": ["missing-game-end"]}
        try:
            whispy = whispy_patch(codes)
        except ValueError as exc:
            return None, {"status": "unsupported", "reasons": [str(exc)]}
        metadata = dict(game.metadata, playedOn=played_on)
        last_frame = game.metadata.get("lastFrame")
        if finalized_through is not None and last_frame not in (None, finalized_through):
            end = 15 + int.from_bytes(raw[11:15], "big")
            key = b"U\x09lastFramel"
            previous = key + struct.pack(">i", last_frame)
            trailer = raw[end:]
            if trailer.count(previous) != 1:
                return None, {"status": "unsupported", "reasons": ["ambiguous-last-frame-metadata"]}
            raw = raw[:end] + trailer.replace(previous, key + struct.pack(">i", finalized_through))
        stored = b""
        if post_size >= STORED_END - 1:
            stored = expected_stored(raw, None, validation.ROOT,
                                     finalized_through=finalized_through)
    except (BadZipFile, EOFError, OSError, RuntimeError, ValueError) as exc:
        return None, {"status": "error", "error": str(exc)}

    runner = pool.acquire()
    try:
        options = dict(
            qemu=str(validation.QEMU),
            sysroot=str(validation.SYSROOT),
            binary=str(validation.NATIVE_BINARY),
            data_dir=str(validation.game_data_dir()),
            timeout=timeout,
            native=True,
            ucf_cardinals_1_0_enabled=not legacy,
            ucf_shield_sdi_enabled=not legacy,
            ucf_sdi_enabled=not legacy,
            ucf_shield_drop_extended_enabled=not legacy,
            ucf_shield_drop_084_enabled=not legacy,
            runner_stdin=runner.stdin_fd,
            runner_stdout=runner.stdout_fd,
            slippi_patches=slippi_patches(codes),
            stored_expected=stored,
            regenerate_slp=b"" if stored and finalized_through is None else raw,
            whispy_dead_fighter_fix=whispy,
        )
        if finalized_through is not None:
            options["finalized_through"] = finalized_through
        result = native.validate_replay(
            game.frames, game.start, metadata, relaxed_floats=relaxed_floats and not stored, **options
        )
        split_fma = False
        if (not stored and (not result["pass"] or result["tolerated_values"]) and
                any(player["character"] == SLIPPI_ICE_CLIMBERS for player in game.start["players"])):
            # Older Dolphin builds can split a double multiply-add in Nana's
            # position blend. Require an exact match across the entire replay.
            try:
                alternate = native.validate_replay(
                    game.frames, game.start, metadata, jit_arithmetic=1, relaxed_floats=False, **options
                )
            except RuntimeError:
                pool.discard(runner)
                runner = None
            else:
                if alternate["pass"]:
                    result = alternate
                    split_fma = True
    except RuntimeError as exc:
        detail = pool.discard(runner)
        runner = None
        return None, {"status": "error", "error": f"{exc}: {detail}" if detail else str(exc)}
    except ValueError as exc:
        return None, {"status": "unsupported", "reasons": [str(exc)]}
    finally:
        if runner is not None:
            pool.release(runner)
    if not result["pass"]:
        return None, {
            "status": "diverged",
            "first_mismatch_frame": result["first_mismatch_frame"],
            "details": result["details"][:1],
            "frames": result["frames"],
        }
    report = {
        "status": "already_recorded" if stored else "regenerated",
        "frames": result["frames"],
        "version": game.start["slippi"]["version"],
    }
    if split_fma:
        report["jit_arithmetic"] = "split-fma"
    if game.end is None:
        report["game_end"] = "missing"
        if winner is not None:
            report["winner"] = winner
        else:
            report["complete"] = False
        if finalized_through is not None:
            report["finalized_through"] = finalized_through
            report["discarded_tail_frames"] = bookend[0] - finalized_through
    if result["tolerated_values"]:
        report["status"] = "regenerated_with_tolerance"
        report["tolerated_values"] = result["tolerated_values"]
        for field in ("tolerated_laser_angles", "tolerated_shadow_ball_angles",
                      "tolerated_needle_anchors", "tolerated_cape_states"):
            if result[field]:
                report[field] = result[field]
    output = raw if stored and finalized_through is None else result["regenerated_slp"]
    return (name, output), report


def regenerate_zip(
    source: Path, destination: Path, *, workers: int, cores: tuple[int, ...],
    start: int = 0, limit: int | None = None, timeout: float = 60, relaxed_floats: bool = False,
    include_finalized_prefixes: bool = False, compression_level: int = DEFAULT_COMPRESSION_LEVEL,
) -> dict:
    if source.resolve() == destination.resolve() or destination.exists():
        raise FileExistsError(destination)
    native = validation.load_native()
    validation.game_data_dir()
    started = time.monotonic()
    with ZipFile(source) as archive, tempfile.NamedTemporaryFile(
        dir=destination.parent, suffix=".zip"
    ) as pending, tempfile.TemporaryFile() as manifest:
        members = (member for member in archive.infolist()
                   if not member.is_dir() and member.filename.lower().endswith(REPLAY_SUFFIXES))
        selection = iter(itertools.islice(members, start, None if limit is None else start + limit))
        counts: dict[str, int] = {}
        frames = 0
        output_names: set[str] = set()
        with validation._NativeRunnerPool(workers, cores) as pool, ZipFile(
            pending.name, mode="w", compression=ZIP_DEFLATED, compresslevel=compression_level
        ) as output, concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
            jobs = {}

            def submit(member):
                future = executor.submit(
                    _regenerate_member, archive, member, native, pool, timeout, relaxed_floats,
                    include_finalized_prefixes,
                )
                jobs[future] = member

            for member in itertools.islice(selection, 2 * workers):
                submit(member)
            while jobs:
                ready, _ = concurrent.futures.wait(jobs, return_when=concurrent.futures.FIRST_COMPLETED)
                for future in ready:
                    member = jobs.pop(future)
                    replay, report = future.result()
                    if replay is not None:
                        name, raw = replay
                        if name in output_names:
                            report = {"status": "error", "error": f"duplicate output member {name}"}
                        else:
                            output.writestr(name, raw)
                            output_names.add(name)
                    status = report["status"]
                    counts[status] = counts.get(status, 0) + 1
                    frames += report.get("frames", 0)
                    report["member"] = member.filename
                    manifest.write((json.dumps(report) + "\n").encode())
                    next_member = next(selection, None)
                    if next_member is not None:
                        submit(next_member)
                done = sum(counts.values())
                if done // 100 != (done - len(ready)) // 100:
                    print(f"{done} replays {counts}", file=sys.stderr, flush=True)
            if not counts:
                raise ValueError("no replay members in the selected archive range")
            manifest.seek(0)
            with output.open("regeneration.jsonl", "w") as dest:
                shutil.copyfileobj(manifest, dest)
        # Publish only a complete archive and preserve an existing destination.
        os.link(pending.name, destination)
    return {
        "outcomes": counts,
        "compared_frames": frames,
        "seconds": round(time.monotonic() - started, 3),
        "output_bytes": destination.stat().st_size,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="ZIP archive containing Slippi replays")
    parser.add_argument("-o", "--output", type=Path,
                        help="output ZIP (default: ARCHIVE.regenerated.zip)")
    parser.add_argument("--start", type=int, default=0, help="skip this many replay members")
    parser.add_argument("--limit", type=int, help="maximum replay members to process")
    parser.add_argument("--workers", type=int, help="number of persistent simulation workers")
    parser.add_argument("--cores", help="comma-separated logical CPUs, one per native worker (Linux)")
    parser.add_argument("--timeout", type=float, default=60, help="timeout per simulation in seconds")
    parser.add_argument("--compression-level", type=int, choices=range(1, 10),
                        default=DEFAULT_COMPRESSION_LEVEL, metavar="LEVEL",
                        help="ZIP Deflate compression level from 1 to 9 (default: %(default)s)")
    parser.add_argument("--disable-strict-exactness", action="store_true",
                        help="allow the documented regeneration tolerances")
    parser.add_argument("--include-finalized-prefixes", action="store_true",
                        help="also regenerate incomplete replays through their last confirmed frame")
    parser.add_argument("--no-build", action="store_true", help="reuse the native runner and validator")
    args = parser.parse_args()
    cores = ()
    if args.cores:
        try:
            cores = tuple(int(core) for core in args.cores.split(","))
        except ValueError:
            parser.error("--cores must be comma-separated CPU numbers")
        if (not hasattr(os, "sched_getaffinity") or not shutil.which("taskset") or
                len(set(cores)) != len(cores) or not set(cores) <= os.sched_getaffinity(0)):
            parser.error("--cores requires distinct available Linux CPU numbers and taskset")
        if args.workers is not None and args.workers != len(cores):
            parser.error("--workers must equal the number of pinned cores")
    workers = args.workers if args.workers is not None else len(cores) or min(16, os.cpu_count() or 1)
    if workers < 1 or args.start < 0 or (args.limit is not None and args.limit < 1) or args.timeout <= 0:
        parser.error("workers, limit and timeout must be positive and start must be non-negative")
    destination = args.output or args.archive.with_suffix(".regenerated.zip")
    if not args.no_build:
        validation.build_validation(backend="native")
    summary = regenerate_zip(args.archive, destination, workers=workers, cores=cores,
                             start=args.start, limit=args.limit, timeout=args.timeout,
                             relaxed_floats=args.disable_strict_exactness,
                             include_finalized_prefixes=args.include_finalized_prefixes,
                             compression_level=args.compression_level)
    print(json.dumps(summary))
    return int(summary["outcomes"].get("error", 0) != 0)


if __name__ == "__main__":
    sys.exit(main())
