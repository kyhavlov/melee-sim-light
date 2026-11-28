"""Verify Slippi replays in the simulator and backfill stored fighter state."""

from __future__ import annotations

import argparse
import concurrent.futures
import gzip
import itertools
import json
import multiprocessing
import os
import re
import shutil
import struct
import sys
import tempfile
import time
from contextlib import closing
from multiprocessing.connection import wait
from pathlib import Path
from zipfile import BadZipFile, ZIP_DEFLATED, ZipFile, ZipInfo

from peppi_py import _read_slippi

from tools.validation import validate_replay as validation
from tools.validation.admission import (
    PATCH_FD_BACKGROUND_FROZEN,
    REPLAY_PLATFORMS,
    capture_issues,
    has_ucf_084_cardinals,
    is_ucf_08,
    replay_events,
    slippi_patches,
    stadium_is_frozen,
    whispy_patch,
)
from tools.validation.deflate_zip import DeflateZipFile, deflate_member
from tools.validation.reconstruct_cstick import CStickRecovery
from tools.validation.slpz import decompress_slpz
from tools.validation.stored import POST_FRAME, STORED_END, STORED_OFFSET, _sizes, expected_stored


REPLAY_SUFFIXES = (".slp", ".slp.gz", ".slpz", ".slpz.gz")
SLIPPI_FINAL_DESTINATION = 32
SLIPPI_ICE_CLIMBERS = 14
SLIPPI_DAMAGE_FLY_ROLL = "91"
DEFAULT_COMPRESSION_LEVEL = 5
WORKERS_PER_PROCESS = 8


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


def _finalized_prefix(raw: bytes, *, through: int | None = None) -> bytes:
    """Repair an interrupted envelope using its last complete bookend."""
    try:
        sizes, count = _sizes(raw)
    except ValueError:
        return raw
    unclosed = int.from_bytes(raw[11:15], "big") == 0
    pos = 17 + 3 * count
    end = 0
    while pos < len(raw):
        event = raw[pos]
        size = sizes[event]
        next_pos = pos + 1 + size
        if not size:
            return raw
        if next_pos > len(raw):
            break
        if event == 0x3C and size >= 8:
            frame, finalized = struct.unpack_from(">ii", raw, pos + 1)
            if through is not None and -123 < through <= finalized <= frame:
                raw = raw[:pos + 5] + struct.pack(">i", through) + raw[pos + 9:next_pos]
                end = next_pos
                break
            if (unclosed and -123 < finalized <= frame) or (next_pos == len(raw) and frame == finalized):
                end = next_pos
        elif unclosed and event == 0x39 and end == pos:
            end = next_pos
        pos = next_pos
    if not end:
        return raw
    return raw[:11] + struct.pack(">I", end - 15) + raw[15:end] + b"U\x08metadata{}}"


def _collapse_duplicate_endings(raw: bytes, endings: list[int], size: int) -> bytes:
    """Keep the final copy of a Game End repeated during rollback."""
    last = endings[-1]
    if any(raw[start:start + size] != raw[last:last + size] for start in endings[:-1]):
        raise ValueError("conflicting Game End events")
    parts = []
    position = 15
    for start in endings[:-1]:
        parts.append(raw[position:start])
        position = start + size
    parts.append(raw[position:])
    length = int.from_bytes(raw[11:15], "big") - size * (len(endings) - 1)
    return raw[:11] + struct.pack(">I", length) + b"".join(parts)


def _read_replay_events(raw: bytes):
    try:
        return raw, *replay_events(raw)
    except ValueError:
        declared_end = 15 + int.from_bytes(raw[11:15], "big")
        metadata_start = raw.find(b"U\x08metadata{", 15)
        if not 15 <= metadata_start < declared_end:
            raise
        repaired = raw[:11] + struct.pack(">I", metadata_start - 15) + raw[15:]
        codes, bookend, endings = replay_events(repaired)
        if not endings or bookend is None or bookend[0] != bookend[1]:
            raise
        # The declared event stream ran into the metadata after Game End.
        return repaired, codes, bookend, endings


def _trim_last_frame(raw: bytes, last_frame: int | None, through: int) -> bytes:
    if last_frame in (None, through):
        return raw
    end = 15 + int.from_bytes(raw[11:15], "big")
    key = b"U\x09lastFramel"
    previous = key + struct.pack(">i", last_frame)
    trailer = raw[end:]
    if trailer.count(previous) != 1:
        raise ValueError("ambiguous-last-frame-metadata")
    return raw[:end] + trailer.replace(previous, key + struct.pack(">i", through))


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
                       include_finalized_prefixes):
    try:
        name, raw = _decode_member(archive, member)
    except (BadZipFile, EOFError, OSError, RuntimeError, ValueError) as exc:
        return None, {"status": "error", "error": str(exc)}
    return _regenerate_replay(name, raw, native, pool, timeout, relaxed_floats,
                              include_finalized_prefixes)


def _regenerate_replay(name, raw, native, pool, timeout, relaxed_floats,
                       include_finalized_prefixes):
    try:
        repaired_envelope = False
        declared_length = int.from_bytes(raw[11:15], "big")
        if include_finalized_prefixes and (not declared_length or 15 + declared_length > len(raw)):
            repaired = _finalized_prefix(raw)
            repaired_envelope = repaired is not raw
            raw = repaired
            if repaired_envelope and not declared_length:
                relaxed_floats = False
        try:
            repaired, codes, bookend, endings = _read_replay_events(raw)
            if repaired is not raw:
                raw = repaired
                repaired_envelope = True
                relaxed_floats = False
            sizes, _ = _sizes(raw)
            if len(endings) > 1:
                raw = _collapse_duplicate_endings(raw, endings, sizes[0x39] + 1)
            # Peppi stops at the first Game End. Give it the canonical ending.
            with tempfile.NamedTemporaryFile(suffix=".slp") as source:
                source.write(raw)
                source.flush()
                game = _read_slippi(source.name, False)
        except (OSError, ValueError) as exc:
            return None, {"status": "parse_failed", "error": str(exc)}
        except BaseException as exc:
            # PyO3 wraps parser assertions in a BaseException subclass.
            if (type(exc).__module__, type(exc).__name__) != ("pyo3_runtime", "PanicException"):
                raise
            return None, {"status": "parse_failed", "error": str(exc)}
        post_size = sizes[POST_FRAME]
        if post_size < STORED_END - 1 and post_size not in (0x50, STORED_OFFSET - 1):
            return None, {"status": "unsupported", "reasons": ["post-frame-layout"]}
        terminal_bookend = (
            bookend is not None and len(game.frames) > 0
            and game.frames.field("id")[-1].as_py() == bookend[0]
        )
        missing_end = game.end is None and terminal_bookend
        finalized_end = terminal_bookend and bookend[0] == bookend[1]
        finalized_through = None
        if (missing_end and include_finalized_prefixes) or (game.end is not None and finalized_end):
            last_published = int(game.frames.field("id").to_numpy(zero_copy_only=False).max())
            if bookend[1] < last_published:
                # Rollback can leave speculative frames ahead of the last bookend.
                finalized_through = bookend[1]
        allow_missing_end = missing_end and (finalized_end or finalized_through is not None)
        legacy = is_ucf_08(codes)
        online = game.start.get("scene", {}).get("major") == 8
        # Slippi's 0x40-byte Pre-Frame payload includes raw main-stick Y but
        # predates raw C-stick X/Y (Slippi ASM c852afa).
        missing_cstick = online and sizes[0x37] == 0x40 and has_ucf_084_cardinals(codes)
        played_on = game.metadata.get("playedOn")
        if played_on is None and online:
            # The netplay scene still identifies the profile without metadata.
            played_on = "network"
        issues = capture_issues(game, raw, played_on=played_on, codes=codes, allow_ucf_08=True,
                                allow_missing_cstick=missing_cstick,
                                allow_missing_game_end=allow_missing_end,
                                finalized_through=finalized_through)
        # Transform can publish uninitialized L-cancel data. The native comparison
        # checks the recorded byte on every frame, including the transform.
        issues = [issue for issue in issues if issue != "uninitialized-transform-lcancel"]
        # A known platform can still lack arithmetic provenance for the strict
        # corpus. Regeneration may use a profile only if the full match is exact.
        exact_arithmetic_retry = (issues == ["unestablished-arithmetic"] and
                                  played_on in REPLAY_PLATFORMS)
        if issues and not exact_arithmetic_retry:
            return None, {"status": "unsupported", "reasons": issues}
        if exact_arithmetic_retry:
            relaxed_floats = False
        winner = None
        if missing_end and finalized_end:
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
        if finalized_through is not None:
            try:
                raw = _trim_last_frame(raw, last_frame, finalized_through)
            except ValueError as exc:
                return None, {"status": "unsupported", "reasons": [str(exc)]}
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
            binary=str(pool.binary),
            data_dir=str(pool.data_dir),
            timeout=timeout,
            native=True,
            stadium_transformations=(game.start["stage"] == 3 and not stadium_is_frozen(game.start, codes)),
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

        def compare(**profile):
            settings = options | profile

            def run(frames):
                return native.validate_replay(frames, game.start, metadata, **settings)

            if missing_cstick:
                ports = [player["port"] for player in game.start["players"]]
                return CStickRecovery(game.frames, ports).validate(run)
            return run(game.frames), 0

        result, recovered_snaps = compare(relaxed_floats=relaxed_floats and not stored)
        if missing_cstick and not result["pass"] and result["tolerated_values"]:
            # A tolerated displacement can hide the input reconstruction
            # needed to reproduce a collision on the same frame.
            alternate, snaps = compare(relaxed_floats=False)
            if alternate["pass"]:
                result, recovered_snaps = alternate, snaps
        retry_retail = False
        retail_fnmsubs = False
        frame_start_rng = False
        animated_fd = False
        if not result["pass"] and online:
            first = next(iter(result["details"]), {})
            field = first.get("field", "").partition("[")[0].removeprefix("follower_")
            roll_mismatch = (field == "action_id" and
                             SLIPPI_DAMAGE_FLY_ROLL in (first["expected"], first["actual"]))
            retry_retail = field in ("speed_x_attack", "speed_y_attack")
            if retry_retail:
                # Netplay captures can use the retail zero sign on cancellation.
                signed_zero = {first["expected"], first["actual"]} == {
                    "0 (0x00000000)", "-0 (0x80000000)"
                }
                alternate, snaps = compare(fnmsubs_profile="retail",
                                           relaxed_floats=relaxed_floats and not stored and signed_zero)
                if alternate["pass"]:
                    result, recovered_snaps = alternate, snaps
                    retail_fnmsubs = True
            elif not stored and roll_mismatch:
                # Some netplay captures need the full RNG stream from the frame
                # start. The extra fighter reseed can count earlier draws twice.
                alternate, snaps = compare(frame_start_rng=True, relaxed_floats=False)
                if alternate["pass"]:
                    result, recovered_snaps = alternate, snaps
                    frame_start_rng = True
            patches = options["slippi_patches"]
            if (not result["pass"] and not stored and
                    game.start["stage"] == SLIPPI_FINAL_DESTINATION and
                    patches & PATCH_FD_BACKGROUND_FROZEN and
                    (field == "animation_index" or roll_mismatch)):
                # Historical netplay Gecko lists can carry the freeze write
                # while the capture uses the animated background's RNG.
                alternate, snaps = compare(slippi_patches=patches & ~PATCH_FD_BACKGROUND_FROZEN,
                                           relaxed_floats=False)
                if alternate["pass"]:
                    result, recovered_snaps = alternate, snaps
                    animated_fd = True
        split_fma = False
        has_ice_climbers = any(player["character"] == SLIPPI_ICE_CLIMBERS
                               for player in game.start["players"])
        if not stored and has_ice_climbers and (not result["pass"] or result["tolerated_values"]):
            # Older Dolphin builds can split a double multiply-add in Nana's
            # position blend. Require an exact match across the entire replay.
            try:
                split_retail = False
                alternate, snaps = compare(jit_arithmetic=1, relaxed_floats=False)
                if retry_retail and not alternate["pass"]:
                    alternate, snaps = compare(fnmsubs_profile="retail", jit_arithmetic=1,
                                               relaxed_floats=False)
                    split_retail = True
            except RuntimeError:
                pool.discard(runner)
                runner = None
            else:
                if alternate["pass"]:
                    result, recovered_snaps = alternate, snaps
                    split_fma = True
                    retail_fnmsubs = split_retail
        stock_share_rng = False
        recorded_follower_inputs = False
        if (runner is not None and not stored and not result["pass"] and
                has_ice_climbers and game.start["is_teams"]):
            alternate, snaps = compare(stock_share_rng=True, relaxed_floats=False)
            if alternate["pass"]:
                result, recovered_snaps = alternate, snaps
                stock_share_rng = True
            else:
                alternate, snaps = compare(recorded_follower_inputs=True, relaxed_floats=False)
                if alternate["pass"]:
                    result, recovered_snaps = alternate, snaps
                    recorded_follower_inputs = True
    except RuntimeError as exc:
        detail = pool.discard(runner)
        runner = None
        return None, {"status": "error", "error": f"{exc}: {detail}" if detail else str(exc)}
    except ValueError as exc:
        loss = re.fullmatch(r"active finalized frame (-?\d+) has no (P[1-4]) pre/post row",
                            str(exc))
        game_end = game.end or {}
        if loss and include_finalized_prefixes and (
            (repaired_envelope and not declared_length and missing_end)
            or (finalized_end and game_end.get("method") == "NoContest"
                and any(player["port"] == game_end.get("lras_initiator")
                        for player in game.start["players"]))
        ):
            frame, port = int(loss[1]), loss[2]
            fighter = game.frames.field("ports").field(port).field("leader")
            ids = game.frames.field("id").to_numpy(zero_copy_only=False)
            present = (fighter.field("pre").is_valid().to_numpy(zero_copy_only=False) |
                       fighter.field("post").is_valid().to_numpy(zero_copy_only=False))
            if frame > -122 and not (present & (ids >= frame)).any():
                prefix = _finalized_prefix(raw, through=frame - 1)
                if prefix is not raw:
                    if game_end:
                        prefix_end = 15 + int.from_bytes(prefix[11:15], "big")
                        source_end = 15 + int.from_bytes(raw[11:15], "big")
                        try:
                            prefix = _trim_last_frame(prefix[:prefix_end] + raw[source_end:],
                                                      last_frame, frame - 1)
                        except ValueError as metadata_error:
                            return None, {"status": "unsupported", "reasons": [str(metadata_error)]}
                    pool.release(runner)
                    runner = None
                    output, report = _regenerate_replay(name, prefix, native, pool, timeout,
                                                        False, include_finalized_prefixes)
                    if output is not None:
                        report.pop("winner", None)
                        report.update(repaired_envelope=True, complete=False, finalized_through=frame - 1,
                                      discarded_tail_frames=int(ids.max()) - frame + 1)
                        return output, report
        return None, {"status": "unsupported", "reasons": [str(exc)]}
    finally:
        if runner is not None:
            pool.release(runner)
    if not result["pass"]:
        if exact_arithmetic_retry:
            return None, {"status": "unsupported", "reasons": ["unestablished-arithmetic"],
                          "details": result["details"][:1]}
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
    if retail_fnmsubs:
        report["fnmsubs_profile"] = "retail"
    if frame_start_rng:
        report["rng_profile"] = "frame-start"
    elif stock_share_rng:
        report["rng_profile"] = "stock-share"
    if recorded_follower_inputs:
        report["recorded_follower_inputs"] = True
    if animated_fd:
        report["fd_background_profile"] = "animated"
    if missing_cstick:
        report["reconstructed_cstick_snaps"] = recovered_snaps
    if len(endings) > 1:
        report["discarded_duplicate_game_ends"] = len(endings) - 1
    if repaired_envelope:
        report["repaired_envelope"] = True
    if game.end is None:
        report["game_end"] = "missing"
        if winner is not None:
            report["winner"] = winner
        else:
            report["complete"] = False
    if finalized_through is not None:
        report["finalized_through"] = finalized_through
        report["discarded_tail_frames"] = last_published - finalized_through
    if result["tolerated_values"]:
        report["status"] = "regenerated_with_tolerance"
        report["tolerated_values"] = result["tolerated_values"]
        for field in ("tolerated_laser_angles", "tolerated_shadow_ball_angles",
                      "tolerated_needle_anchors", "tolerated_cape_states"):
            if result[field]:
                report[field] = result[field]
    output = raw if stored and finalized_through is None else result["regenerated_slp"]
    return (name, output), report


def _selected_members(archive, start, stop):
    members = (member for member in archive.infolist()
               if not member.is_dir() and member.filename.lower().endswith(REPLAY_SUFFIXES))
    return itertools.islice(members, start, stop)


def _process_members(archive, start, stop, workers, cores, settings):
    binary, data_dir, timeout, relaxed_floats, include_finalized_prefixes, compression_level = settings
    native = validation.load_native()
    members = _selected_members(archive, start, stop)
    with validation._NativeRunnerPool(workers, cores, binary=binary, data_dir=data_dir) as pool, \
            concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        def process(member):
            replay, report = _regenerate_member(
                archive, member, native, pool, timeout, relaxed_floats,
                include_finalized_prefixes,
            )
            if replay is None:
                return None, b"", report
            name, raw = replay
            info, compressed = deflate_member(name, raw, compression_level)
            return info, compressed, report

        jobs = {executor.submit(process, member): member.filename
                for member in itertools.islice(members, 2 * workers)}
        while jobs:
            ready, _ = concurrent.futures.wait(jobs, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in ready:
                name = jobs.pop(future)
                yield name, *future.result()
                member = next(members, None)
                if member is not None:
                    jobs[executor.submit(process, member)] = member.filename


def _archive_identity(archive):
    info = os.fstat(archive.fp.fileno())
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns


def _send_members(connection, source, identity, start, stop, workers, cores, settings):
    with connection, ZipFile(source) as archive, closing(
        _process_members(archive, start, stop, workers, cores, settings)
    ) as results:
        if _archive_identity(archive) != identity:
            raise RuntimeError("source archive changed while starting replay workers")
        for name, info, compressed, report in results:
            connection.send((name, info, report))
            if info is not None:
                connection.send_bytes(compressed)


def _parallel_members(source, identity, start, count, workers, cores, settings):
    # Each process has its own Python interpreter and ZIP reader. The output
    # receives bounded streams of compressed members through the pipes.
    context = multiprocessing.get_context("spawn")
    readers = {}
    try:
        for index in range(0, workers, WORKERS_PER_PROCESS):
            end = min(index + WORKERS_PER_PROCESS, workers)
            first = start + count * index // workers
            stop = start + count * end // workers
            reader, writer = context.Pipe(duplex=False)
            with writer:
                process = context.Process(
                    target=_send_members,
                    args=(writer, source, identity, first, stop, end - index, cores[index:end], settings),
                )
                try:
                    process.start()
                except BaseException:
                    reader.close()
                    raise
            readers[reader] = process

        while readers:
            for reader in wait(readers):
                try:
                    name, info, report = reader.recv()
                except EOFError:
                    process = readers.pop(reader)
                    reader.close()
                    process.join()
                    exitcode = process.exitcode
                    process.close()
                    if exitcode:
                        raise RuntimeError(f"replay worker process exited with status {exitcode}")
                    continue
                compressed = reader.recv_bytes() if info is not None else b""
                yield name, info, compressed, report
    finally:
        for reader in readers:
            reader.close()
        for process in readers.values():
            process.join(timeout=1)
            if process.is_alive():
                process.terminate()
                process.join()
            process.close()


def regenerate_zip(
    source: Path, destination: Path, *, workers: int, cores: tuple[int, ...],
    start: int = 0, limit: int | None = None, timeout: float = 60, relaxed_floats: bool = True,
    include_finalized_prefixes: bool = True, compression_level: int = DEFAULT_COMPRESSION_LEVEL,
    binary: Path | None = None,
) -> dict:
    if source.resolve() == destination.resolve() or destination.exists():
        raise FileExistsError(destination)
    data_dir = validation.game_data_dir()
    started = time.monotonic()
    with ZipFile(source) as archive, tempfile.NamedTemporaryFile(
        dir=destination.parent, suffix=".zip"
    ) as pending, tempfile.TemporaryFile() as manifest:
        identity = _archive_identity(archive)
        stop = None if limit is None else start + limit
        count = sum(1 for _ in _selected_members(archive, start, stop))
        if not count:
            raise ValueError("no replay members in the selected archive range")
        workers = min(workers, count)
        settings = binary, data_dir, timeout, relaxed_floats, include_finalized_prefixes, compression_level
        results = (_process_members(archive, start, start + count, workers, cores[:workers], settings)
                   if workers <= WORKERS_PER_PROCESS else
                   _parallel_members(source, identity, start, count, workers, cores, settings))
        counts: dict[str, int] = {}
        frames = 0
        output_names: set[str] = set()
        with closing(results), DeflateZipFile(
            pending.name, mode="w", compression=ZIP_DEFLATED, compresslevel=compression_level
        ) as output:
            for done, (member, info, compressed, report) in enumerate(results, 1):
                if info is not None:
                    name = info.filename
                    if name in output_names:
                        report = {"status": "error", "error": f"duplicate output member {name}"}
                    else:
                        output.write_deflated(info, compressed)
                        output_names.add(name)
                status = report["status"]
                counts[status] = counts.get(status, 0) + 1
                frames += report.get("frames", 0)
                report["member"] = member
                manifest.write((json.dumps(report) + "\n").encode())
                if done % 100 == 0:
                    print(f"{done} replays {counts}", file=sys.stderr, flush=True)
            if sum(counts.values()) != count:
                raise RuntimeError("replay workers did not return every selected member")
            manifest.seek(0)
            with output.open("regeneration.jsonl", "w") as dest:
                shutil.copyfileobj(manifest, dest)
        # Publish only a complete archive and preserve an existing destination.
        if _archive_identity(archive) != identity:
            raise RuntimeError("source archive changed during replay regeneration")
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
                        help="output ZIP (default: ARCHIVE.regenerated.zip next to the input)")
    parser.add_argument("--start", type=int, default=0, help="skip this many replay members")
    parser.add_argument("--limit", type=int, help="maximum replay members to process")
    parser.add_argument("--workers", type=int, help="number of simulation workers (default: up to 16)")
    parser.add_argument("--cores", help="comma-separated logical CPUs, one per native worker (Linux)")
    parser.add_argument("--timeout", type=float, default=60, help="timeout per simulation in seconds")
    parser.add_argument("--compression-level", type=int, choices=range(1, 10),
                        default=DEFAULT_COMPRESSION_LEVEL, metavar="LEVEL",
                        help="ZIP Deflate compression level from 1 to 9 (default: %(default)s)")
    parser.add_argument("--strict-exactness", action="store_true",
                        help="require exact matches for all compared fields")
    parser.add_argument("--complete-only", action="store_true",
                        help="skip incomplete recordings")
    parser.add_argument("--no-build", action="store_true", help="reuse the native runner and validator")
    args = parser.parse_args()
    available_cpus = os.sched_getaffinity(0) if hasattr(os, "sched_getaffinity") else None
    cores = ()
    if args.cores:
        try:
            cores = tuple(int(core) for core in args.cores.split(","))
        except ValueError:
            parser.error("--cores must be comma-separated CPU numbers")
        if (available_cpus is None or not shutil.which("taskset") or
                len(set(cores)) != len(cores) or not set(cores) <= available_cpus):
            parser.error("--cores requires distinct available Linux CPU numbers and taskset")
        if args.workers is not None and args.workers != len(cores):
            parser.error("--workers must equal the number of pinned cores")
    cpu_count = len(available_cpus) if available_cpus is not None else os.cpu_count() or 1
    workers = args.workers if args.workers is not None else len(cores) or min(16, cpu_count)
    if workers < 1 or args.start < 0 or (args.limit is not None and args.limit < 1) or args.timeout <= 0:
        parser.error("workers, limit and timeout must be positive and start must be non-negative")
    destination = args.output or args.archive.with_suffix(".regenerated.zip")
    if not args.no_build:
        validation.build_validation(backend="native-release")
    binary = (validation.NATIVE_BINARY if "MSL_CORE_NATIVE_BINARY" in os.environ else
              validation.NATIVE_RELEASE_BINARY)
    summary = regenerate_zip(args.archive, destination, workers=workers, cores=cores,
                             start=args.start, limit=args.limit, timeout=args.timeout,
                             relaxed_floats=not args.strict_exactness,
                             include_finalized_prefixes=not args.complete_only,
                             compression_level=args.compression_level, binary=binary)
    print(json.dumps(summary))
    return int(summary["outcomes"].get("error", 0) != 0)


if __name__ == "__main__":
    sys.exit(main())
