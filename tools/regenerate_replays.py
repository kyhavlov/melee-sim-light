"""Verify Slippi replays in the simulator and backfill stored fighter state."""

from __future__ import annotations

import argparse
import concurrent.futures
import gzip
import itertools
import json
import os
import shutil
import sys
import tempfile
import time
from pathlib import Path
from zipfile import BadZipFile, ZIP_DEFLATED, ZipFile, ZipInfo

from peppi_py import _read_slippi

from tools.validation import validate_replay as validation
from tools.validation.admission import (
    capture_issues,
    gecko_codes,
    is_ucf_08,
    slippi_patches,
    whispy_patch,
)
from tools.validation.slpz import decompress_slpz
from tools.validation.stored import POST_FRAME, STORED_END, STORED_OFFSET, _sizes, expected_stored


REPLAY_SUFFIXES = (".slp", ".slp.gz", ".slpz", ".slpz.gz")


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


def _regenerate_member(archive, member, native, pool, timeout, relaxed_floats):
    try:
        name, raw = _decode_member(archive, member)
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
        codes = gecko_codes(raw)
        legacy = is_ucf_08(codes)
        played_on = game.metadata.get("playedOn")
        if played_on is None and legacy and game.start.get("scene", {}).get("major") == 8:
            # This profile owns old Slippi online recordings, including
            # anonymized captures whose UBJSON metadata was stripped.
            played_on = "network"
        issues = capture_issues(game, raw, played_on=played_on, codes=codes, allow_ucf_08=True)
        # Transform can publish uninitialized L-cancel data. The native comparison
        # checks the recorded byte on every frame, including the transform.
        issues = [issue for issue in issues if issue != "uninitialized-transform-lcancel"]
        if issues:
            return None, {"status": "unsupported", "reasons": issues}
        try:
            whispy = whispy_patch(codes)
        except ValueError as exc:
            return None, {"status": "unsupported", "reasons": [str(exc)]}
        metadata = dict(game.metadata, playedOn=played_on)
        stored = expected_stored(raw, None, validation.ROOT) if post_size >= STORED_END - 1 else b""
    except (BadZipFile, EOFError, OSError, RuntimeError, ValueError) as exc:
        return None, {"status": "error", "error": str(exc)}

    runner = pool.acquire()
    try:
        result = native.validate_replay(
            game.frames,
            game.start,
            metadata,
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
            regenerate_slp=b"" if stored else raw,
            relaxed_floats=relaxed_floats and not stored,
            whispy_dead_fighter_fix=whispy,
        )
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
    if result["tolerated_values"]:
        report["status"] = "regenerated_with_tolerance"
        report["tolerated_values"] = result["tolerated_values"]
        for field in ("tolerated_laser_angles", "tolerated_shadow_ball_angles",
                      "tolerated_needle_anchors", "tolerated_cape_states"):
            if result[field]:
                report[field] = result[field]
    return (name, raw if stored else result["regenerated_slp"]), report


def regenerate_zip(
    source: Path, destination: Path, *, workers: int, cores: tuple[int, ...],
    start: int = 0, limit: int | None = None, timeout: float = 60, relaxed_floats: bool = False,
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
            pending.name, mode="w", compression=ZIP_DEFLATED, compresslevel=1
        ) as output, concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
            jobs = {}

            def submit(member):
                future = executor.submit(
                    _regenerate_member, archive, member, native, pool, timeout, relaxed_floats
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
    parser.add_argument("--disable-strict-exactness", action="store_true",
                        help="allow the documented regeneration tolerances")
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
                             relaxed_floats=args.disable_strict_exactness)
    print(json.dumps(summary))
    return int(summary["outcomes"].get("error", 0) != 0)


if __name__ == "__main__":
    sys.exit(main())
