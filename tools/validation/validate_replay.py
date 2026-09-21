from __future__ import annotations

import argparse
import concurrent.futures
import importlib.util
import json
import os
import queue
import re
import subprocess
import threading
import time
from dataclasses import dataclass, replace
from functools import lru_cache
from pathlib import Path
from types import ModuleType

from peppi_py import _read_slippi

from tools.data.raw import raw_dir, validate_raw_dir
from tools.validation.slpz import (
    replay_path_for_peppi,
    resolve_replay_path,
    set_native_unorder_events,
)
from tools.validation.admission import require_admissible
from tools.validation.suite_io import ReplaySuite, display_path_under_repo, load_suite


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build" / "melee_core"
NATIVE = BUILD / "validation" / "_msl_replay_validate.so"
PPC_BINARY = BUILD / "ppc" / "melee-core-ppc"
NATIVE_BINARY = Path(
    os.environ.get("MSL_CORE_NATIVE_BINARY", BUILD / "native" / "melee-core-native")
)
TOOLCHAIN = BUILD / "toolchain" / "root"
QEMU = TOOLCHAIN / "usr" / "bin" / "qemu-ppc-static"
SYSROOT = TOOLCHAIN / "usr" / "powerpc-linux-gnu"
DEFAULT_CHARACTERS = "Mewtwo,Game & Watch,Fox,Falco,Marth,Roy,Captain Falcon,Sheik,Zelda,Jigglypuff,Peach,Luigi,Mario,Dr. Mario,Samus,Ice Climbers,Pikachu,Pichu,Kirby,Donkey Kong,Ganondorf,Yoshi,Bowser,Ness,Link,Young Link"
DEFAULT_STAGES = "32,31,3,2,8,28"
MAX_AUTO_WORKERS = 16
DEFAULT_OUTPUT_LOCKS = ROOT / "replays/suites/melee_core_output_locks.json"

STAGE_NAMES = {
    2: "Fountain",
    3: "Frozen Stadium",
    8: "Yoshi's",
    28: "Dream Land",
    31: "Battlefield",
    32: "FD",
}


@dataclass(frozen=True)
class ReplayCase:
    replay: Path
    display_path: str
    ports: tuple[int, ...] = ()
    stage_id: int | None = None
    characters: tuple[str, ...] = ()
    ucf_cardinals_1_0_enabled: bool = True
    ucf_shield_sdi_enabled: bool = True
    ucf_sdi_enabled: bool = True
    ucf_shield_drop_extended_enabled: bool = True
    ucf_shield_drop_084_enabled: bool = True
    played_on: str | None = None
    fnmsubs_profile: str | None = None


@dataclass(frozen=True)
class ReplayOutcome:
    case: ReplayCase
    result: dict[str, object] | None
    error: str | None
    elapsed_seconds: float


@dataclass(frozen=True)
class ReplayOutputLock:
    replay: str
    expected: dict[str, dict[str, object]]


@dataclass(frozen=True)
class _NativeRunner:
    process: subprocess.Popen[bytes]
    stdin_fd: int
    stdout_fd: int


class _NativeRunnerPool:
    def __init__(self, count: int) -> None:
        self._available: queue.LifoQueue[_NativeRunner] = queue.LifoQueue()
        self._runners: list[_NativeRunner] = []
        self._lock = threading.Lock()
        try:
            for _ in range(count):
                runner = self._spawn()
                self._runners.append(runner)
                self._available.put(runner)
        except Exception:
            self.close()
            raise

    def _spawn(self) -> _NativeRunner:
        process = subprocess.Popen(
            [str(NATIVE_BINARY), str(game_data_dir()), "--server"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )
        assert process.stdin is not None and process.stdout is not None
        return _NativeRunner(
            process=process,
            stdin_fd=process.stdin.fileno(),
            stdout_fd=process.stdout.fileno(),
        )

    def acquire(self) -> _NativeRunner:
        return self._available.get()

    def release(self, runner: _NativeRunner) -> None:
        self._available.put(runner)

    def discard(self, runner: _NativeRunner) -> None:
        # A case that failed mid-session leaves the runner's streams at an
        # undefined position (a partially written job, unread output), so the
        # next job's header would be read as frame payload. Retire the process
        # and hand the pool a fresh one instead of poisoning later cases.
        with self._lock:
            self._runners.remove(runner)
        for stream in (runner.process.stdin, runner.process.stdout, runner.process.stderr):
            if stream is not None and not stream.closed:
                stream.close()
        runner.process.kill()
        runner.process.wait()
        replacement = self._spawn()
        with self._lock:
            self._runners.append(replacement)
        self._available.put(replacement)

    def close(self) -> None:
        errors: list[str] = []
        for runner in self._runners:
            if runner.process.stdin is not None and not runner.process.stdin.closed:
                runner.process.stdin.close()
        for runner in self._runners:
            try:
                returncode = runner.process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                runner.process.kill()
                returncode = runner.process.wait()
            stderr = b""
            if runner.process.stderr is not None:
                stderr = runner.process.stderr.read()
                runner.process.stderr.close()
            if runner.process.stdout is not None:
                runner.process.stdout.close()
            if returncode != 0:
                detail = stderr.decode(errors="replace").strip()
                errors.append(detail or f"native validation worker exited {returncode}")
        self._runners.clear()
        if errors:
            raise RuntimeError("; ".join(errors))

    def __enter__(self) -> _NativeRunnerPool:
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        try:
            self.close()
        except RuntimeError:
            if exc is None:
                raise


@lru_cache(maxsize=1)
def game_data_dir() -> Path:
    path = raw_dir(ROOT / "data")
    validate_raw_dir(path, verify_hashes=True)
    return path


def load_output_locks(path: Path) -> dict[str, ReplayOutputLock]:
    data = json.loads(path.read_text())
    if data.get("version") != 1 or data.get("algorithm") != "fnv1a64":
        raise ValueError(f"{path}: unsupported output-lock manifest")
    if data.get("wire") != {"name": "MslCoreCompare", "size": 1302}:
        raise ValueError(f"{path}: output-lock wire contract does not match MslCoreCompare")
    locks: dict[str, ReplayOutputLock] = {}
    for entry in data.get("locks", []):
        replay = str(entry.get("replay", ""))
        expected = entry.get("expected")
        replay_path = Path(replay)
        if not replay or replay_path.is_absolute() or ".." in replay_path.parts:
            raise ValueError(f"{path}: output-lock replay must be repo-relative")
        if not resolve_replay_path(ROOT / replay_path).is_file():
            raise ValueError(f"{path}: output-lock replay does not exist: {replay}")
        if not isinstance(expected, dict) or not expected:
            raise ValueError(f"{path}: output lock has no backend snapshot: {replay}")
        snapshots: dict[str, dict[str, object]] = {}
        for backend, snapshot in expected.items():
            if backend not in {"native", "ppc"} or not isinstance(snapshot, dict):
                raise ValueError(f"{path}: invalid output-lock backend for {replay}")
            if set(snapshot) != {"frames", "actual_output_fingerprint"}:
                raise ValueError(f"{path}: invalid {backend} output lock for {replay}")
            frames = snapshot["frames"]
            fingerprint = snapshot["actual_output_fingerprint"]
            if not isinstance(frames, int) or frames <= 0 or not isinstance(fingerprint, str) or not re.fullmatch(r"[0-9a-f]{16}", fingerprint):
                raise ValueError(f"{path}: malformed {backend} output lock for {replay}")
            snapshots[backend] = snapshot
        if replay in locks:
            raise ValueError(f"{path}: duplicate output-lock replay: {replay}")
        locks[replay] = ReplayOutputLock(replay=replay, expected=snapshots)
    return locks


def write_output_locks(
    path: Path,
    locks: dict[str, ReplayOutputLock],
    backend: str,
    outcomes: list[ReplayOutcome],
) -> dict[str, ReplayOutputLock]:
    updated = dict(locks)
    for outcome in outcomes:
        if outcome.error is not None or outcome.result is None:
            raise ValueError(f"cannot lock failed replay: {outcome.case.display_path}")
        result = outcome.result
        if not result.get("admitted") or result.get("diagnostic") or not result["pass"]:
            raise ValueError(f"cannot lock non-strict or ineligible replay: {outcome.case.display_path}")
        if not _has_full_replay_coverage(result):
            raise ValueError(f"cannot lock partial replay: {outcome.case.display_path}")
        replay = outcome.case.display_path
        current = updated.get(replay)
        expected = dict(current.expected) if current is not None else {}
        expected[backend] = {
            "frames": int(result["frames"]),
            "actual_output_fingerprint": str(result["actual_output_fingerprint"]),
        }
        updated[replay] = ReplayOutputLock(replay=replay, expected=expected)
    payload = {
        "version": 1,
        "algorithm": "fnv1a64",
        "wire": {"name": "MslCoreCompare", "size": 1302},
        "locks": [
            {"replay": lock.replay, "expected": lock.expected}
            for lock in sorted(updated.values(), key=lambda lock: lock.replay)
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=False) + "\n")
    return updated


def _has_full_replay_coverage(result: dict[str, object]) -> bool:
    return int(result["frames"]) == int(result["total"])


def build_validation(*, backend: str, jobs: int = 4) -> None:
    if backend == "native":
        targets = ["validator", "native"]
    elif backend == "ppc":
        targets = ["validator", "ppc"]
    elif backend == "both":
        targets = ["validator", "ppc", "native"]
    else:
        raise ValueError(f"unsupported validation backend: {backend}")
    subprocess.run(
        [
            "make",
            "--no-print-directory",
            "--silent",
            "-f",
            "Makefile",
            f"-j{max(1, jobs)}",
            *targets,
        ],
        cwd=ROOT,
        check=True,
    )


def load_native() -> ModuleType:
    if not NATIVE.is_file():
        raise FileNotFoundError(f"native validator is missing: {NATIVE}")
    spec = importlib.util.spec_from_file_location("_msl_replay_validate", NATIVE)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load native validator: {NATIVE}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    set_native_unorder_events(module.slpz_unorder_events)
    return module


def validate_one(
    native: ModuleType,
    replay: Path,
    *,
    frames: int,
    start_frame: int | None,
    timeout: float,
    backend: str = "ppc",
    signed_zero_equal: bool = False,
    ucf_cardinals_1_0_enabled: bool = True,
    ucf_shield_sdi_enabled: bool = True,
    ucf_sdi_enabled: bool = True,
    ucf_shield_drop_extended_enabled: bool = True,
    ucf_shield_drop_084_enabled: bool = True,
    played_on: str | None = None,
    fnmsubs_profile: str | None = None,
    runner: _NativeRunner | None = None,
    diagnostic: bool = False,
) -> dict[str, object]:
    # Python owns only the replay-loading boundary. The native extension consumes
    # Peppi's Arrow buffers through the Arrow C Data Interface without NumPy or
    # per-frame Python work.
    started = time.perf_counter()
    with replay_path_for_peppi(replay) as peppi_path:
        game = _read_slippi(str(peppi_path), False)
        if not diagnostic:
            require_admissible(game, peppi_path, played_on=played_on, fnmsubs_profile=fnmsubs_profile)
        metadata = game.metadata
        if played_on is not None:
            metadata = dict(metadata)
            metadata["playedOn"] = played_on
        result = native.validate_replay(
            game.frames,
            game.start,
            metadata,
            qemu=str(QEMU),
            sysroot=str(SYSROOT),
            binary=str(NATIVE_BINARY if backend == "native" else PPC_BINARY),
            data_dir=str(game_data_dir()),
            start_frame=start_frame,
            frames_limit=frames,
            timeout=timeout,
            signed_zero_equal=signed_zero_equal,
            fnmsubs_profile=fnmsubs_profile,
            native=backend == "native",
            ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
            ucf_shield_sdi_enabled=ucf_shield_sdi_enabled,
            ucf_sdi_enabled=ucf_sdi_enabled,
            ucf_shield_drop_extended_enabled=ucf_shield_drop_extended_enabled,
            ucf_shield_drop_084_enabled=ucf_shield_drop_084_enabled,
            runner_stdin=runner.stdin_fd if runner is not None else -1,
            runner_stdout=runner.stdout_fd if runner is not None else -1,
        )
    result["admitted"] = not diagnostic
    result["diagnostic"] = diagnostic or signed_zero_equal or frames != 0 or start_frame is not None
    result["fnmsubs_profile"] = fnmsubs_profile or "metadata"
    result["end_to_end_seconds"] = time.perf_counter() - started
    return result


def _parse_characters(value: str) -> frozenset[str]:
    characters = frozenset(part.strip().casefold() for part in value.split(",") if part.strip())
    if not characters:
        raise ValueError("character scope must not be empty")
    return characters


def _parse_stages(value: str) -> frozenset[int]:
    try:
        stages = frozenset(int(part.strip()) for part in value.split(",") if part.strip())
    except ValueError as exc:
        raise ValueError(f"invalid stage scope: {value!r}") from exc
    if not stages:
        raise ValueError("stage scope must not be empty")
    return stages


def load_suite_cases(
    suite_path: Path,
    *,
    characters: frozenset[str],
    stages: frozenset[int],
    root: Path = ROOT,
) -> tuple[ReplaySuite, list[ReplayCase]]:
    suite = load_suite(suite_path)
    if suite.ucf_enabled is not True or suite.ucf_cardinals_1_0_enabled is None:
        raise ValueError(
            f"{suite_path}: Melee core validation requires an explicit UCF/cardinal profile"
        )

    cases: list[ReplayCase] = []
    for entry in suite.replays:
        if entry.stage_id is None or entry.characters is None:
            raise ValueError(f"{suite_path}: replay is missing stage/character metadata: {entry.replay}")
        entry_characters: list[str] = []
        for port in entry.ports:
            character = entry.characters.get(str(port))
            if character is None:
                raise ValueError(f"{suite_path}: replay is missing P{port} character: {entry.replay}")
            entry_characters.append(character)
        if int(entry.stage_id) not in stages or not all(
            character.casefold() in characters for character in entry_characters
        ):
            continue

        unresolved = Path(entry.replay)
        if not unresolved.is_absolute():
            unresolved = root / unresolved
        replay = resolve_replay_path(unresolved)
        if not replay.is_file():
            raise FileNotFoundError(f"suite replay does not exist: {replay}")
        cases.append(
            ReplayCase(
                replay=replay,
                display_path=display_path_under_repo(unresolved, root),
                ports=entry.ports,
                stage_id=int(entry.stage_id),
                characters=tuple(entry_characters),
                ucf_cardinals_1_0_enabled=(
                    entry.ucf_cardinals_1_0_enabled
                    if entry.ucf_cardinals_1_0_enabled is not None
                    else suite.ucf_cardinals_1_0_enabled
                ),
                ucf_shield_sdi_enabled=(
                    entry.ucf_shield_sdi_enabled
                    if entry.ucf_shield_sdi_enabled is not None
                    else (
                        suite.ucf_shield_sdi_enabled
                        if suite.ucf_shield_sdi_enabled is not None
                        else True
                    )
                ),
                ucf_sdi_enabled=(
                    entry.ucf_sdi_enabled
                    if entry.ucf_sdi_enabled is not None
                    else (
                        suite.ucf_sdi_enabled
                        if suite.ucf_sdi_enabled is not None
                        else True
                    )
                ),
                ucf_shield_drop_extended_enabled=(
                    entry.ucf_shield_drop_extended_enabled
                    if entry.ucf_shield_drop_extended_enabled is not None
                    else (
                        suite.ucf_shield_drop_extended_enabled
                        if suite.ucf_shield_drop_extended_enabled is not None
                        else True
                    )
                ),
                ucf_shield_drop_084_enabled=(
                    entry.ucf_shield_drop_084_enabled
                    if entry.ucf_shield_drop_084_enabled is not None
                    else (
                        suite.ucf_shield_drop_084_enabled
                        if suite.ucf_shield_drop_084_enabled is not None
                        else True
                    )
                ),
                played_on=entry.played_on,
                fnmsubs_profile=entry.fnmsubs_profile,
            )
        )
    if not cases:
        raise ValueError(f"{suite_path}: no replays match the requested character/stage scope")
    return suite, cases


def _manual_cases(replays: list[Path]) -> list[ReplayCase]:
    cases: list[ReplayCase] = []
    for replay_arg in replays:
        unresolved = replay_arg.expanduser()
        if not unresolved.is_absolute():
            unresolved = ROOT / unresolved
        replay = resolve_replay_path(unresolved)
        if not replay.is_file():
            raise FileNotFoundError(f"replay does not exist: {replay}")
        cases.append(
            ReplayCase(
                replay=replay,
                display_path=display_path_under_repo(unresolved, ROOT),
            )
        )
    return cases


def _resolve_worker_count(requested: int, task_count: int) -> int:
    if requested < 0:
        raise ValueError("workers must be non-negative")
    if requested > 0:
        return min(requested, task_count)
    return max(1, min(MAX_AUTO_WORKERS, int(os.cpu_count() or 1), task_count))


def _validate_case(
    native: ModuleType,
    case: ReplayCase,
    *,
    frames: int,
    start_frame: int | None,
    timeout: float,
    backend: str,
    signed_zero_equal: bool,
    runner_pool: _NativeRunnerPool | None = None,
    diagnostic: bool = False,
) -> ReplayOutcome:
    started = time.perf_counter()
    try:
        runner = runner_pool.acquire() if runner_pool is not None else None
        try:
            result = validate_one(
                native,
                case.replay,
                frames=frames,
                start_frame=start_frame,
                timeout=timeout,
                backend=backend,
                signed_zero_equal=signed_zero_equal,
                ucf_cardinals_1_0_enabled=case.ucf_cardinals_1_0_enabled,
                ucf_shield_sdi_enabled=case.ucf_shield_sdi_enabled,
                ucf_sdi_enabled=case.ucf_sdi_enabled,
                ucf_shield_drop_extended_enabled=(
                    case.ucf_shield_drop_extended_enabled
                ),
                ucf_shield_drop_084_enabled=case.ucf_shield_drop_084_enabled,
                played_on=case.played_on,
                fnmsubs_profile=case.fnmsubs_profile,
                runner=runner,
                diagnostic=diagnostic,
            )
        except Exception:
            if runner is not None and runner_pool is not None:
                runner_pool.discard(runner)
                runner = None
            raise
        finally:
            if runner is not None and runner_pool is not None:
                runner_pool.release(runner)
        return ReplayOutcome(case, result, None, time.perf_counter() - started)
    except Exception as exc:
        return ReplayOutcome(
            case,
            None,
            f"{type(exc).__name__}: {exc}",
            time.perf_counter() - started,
        )


def run_cases(
    native: ModuleType,
    cases: list[ReplayCase],
    *,
    workers: int,
    frames: int,
    start_frame: int | None,
    timeout: float,
    backend: str,
    signed_zero_equal: bool,
    diagnostic: bool = False,
) -> tuple[list[ReplayOutcome], float]:
    # Warm the manifest/hash check before workers fan out. Native workers keep
    # immutable GameData and resettable match storage alive across jobs; PPC
    # remains an isolated QEMU oracle process per replay.
    game_data_dir()
    started = time.perf_counter()
    kwargs = {
        "frames": frames,
        "start_frame": start_frame,
        "timeout": timeout,
        "backend": backend,
        "signed_zero_equal": signed_zero_equal,
        "diagnostic": diagnostic,
    }
    pool = _NativeRunnerPool(workers) if backend == "native" else None
    try:
        kwargs["runner_pool"] = pool
        if workers == 1:
            outcomes = [_validate_case(native, case, **kwargs) for case in cases]
        else:
            with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
                futures = [
                    executor.submit(_validate_case, native, case, **kwargs)
                    for case in cases
                ]
                # Resolve in manifest order so stdout and regressions remain deterministic.
                outcomes = [future.result() for future in futures]
    finally:
        if pool is not None:
            pool.close()
    return outcomes, time.perf_counter() - started


def _case_scope(case: ReplayCase) -> str:
    profile = f"fnmsubs={case.fnmsubs_profile}" if case.fnmsubs_profile else ""
    if case.stage_id is None:
        return profile
    stage = STAGE_NAMES.get(case.stage_id, f"stage {case.stage_id}")
    players = "/".join(case.characters)
    ports = "/".join(f"P{port}" for port in case.ports)
    return f"{stage} {players} {ports} {profile}".rstrip()


def _result_status(
    backend: str,
    outcome: ReplayOutcome,
    output_locks: dict[str, ReplayOutputLock] | None = None,
    require_output_lock: bool = False,
) -> str:
    assert outcome.result is not None
    result = outcome.result
    if not result["pass"]:
        return "fail"
    if result.get("diagnostic"):
        return "diagnostic"
    if not result.get("admitted") or not _has_full_replay_coverage(result):
        return "fail"
    locks = output_locks or {}
    lock = locks.get(outcome.case.display_path) or locks.get(
        display_path_under_repo(outcome.case.replay, ROOT))
    expected = lock.expected.get(backend) if lock is not None else None
    if expected is None and require_output_lock:
        return "unlocked"
    if expected is not None and (
        result["frames"] != expected["frames"] or
        result["actual_output_fingerprint"] != expected["actual_output_fingerprint"]
    ):
        return "output-drift"
    return "pass"


def _print_mismatch(result: dict[str, object]) -> None:
    print(
        "      "
        f"mismatch_rows={int(result['mismatched_frames']):,} "
        f"prefix={int(result['exact_prefix_frames']):,} "
        f"suffix={int(result['strict_suffix_frames']):,} "
        f"first={result['first_mismatch_frame']} "
        f"last={result['last_mismatch_frame']} "
        f"fields={int(result['mismatch_count']):,} "
        f"fingerprint={result['mismatch_fingerprint']}"
    )
    for detail in list(result["details"])[:3]:
        print(
            f"      frame={detail['frame']} {detail['field']}: "
            f"expected={detail['expected']} actual={detail['actual']}"
        )
    summaries = list(result["mismatch_fields"])
    if summaries:
        fields = ", ".join(
            f"{field['field']}={int(field['count']):,}"
            f"@{field['first_frame']}..{field['last_frame']}"
            for field in summaries[:4]
        )
        if len(summaries) > 4:
            fields += f", +{len(summaries) - 4} fields"
        print(f"      mismatch_fields: {fields}")


def print_backend_results(
    backend: str,
    outcomes: list[ReplayOutcome],
    *,
    workers: int,
    wall_seconds: float,
    show_timing: bool,
    output_locks: dict[str, ReplayOutputLock] | None = None,
    require_output_lock: bool = False,
) -> bool:
    print(f"\n[{backend}] workers={workers}")
    counts = {"pass": 0, "diagnostic": 0, "fail": 0, "error": 0}
    compared_frames = 0
    runner_seconds = 0.0
    for outcome in outcomes:
        scope = _case_scope(outcome.case)
        scope_column = f" {scope:<39}" if scope else ""
        if outcome.error is not None:
            counts["error"] += 1
            print(f"{'ERROR':<10} {'-':>15}{scope_column} {outcome.case.display_path}")
            print(f"      {outcome.error}")
            continue
        assert outcome.result is not None
        result = outcome.result
        frames = int(result["frames"])
        matched = int(result["matched_frames"])
        compared_frames += frames
        runner = float(result["runner_seconds"])
        runner_seconds += runner
        timing = ""
        if show_timing:
            fps = frames / runner if runner > 0.0 else 0.0
            timing = f" {runner:7.3f}s {fps:9,.0f} fps"
        status = _result_status(backend, outcome, output_locks, require_output_lock)
        key = status if status in ("pass", "diagnostic") else "fail"
        counts[key] += 1
        print(f"{key.upper():<10} {matched:>7,}/{frames:<7,}{timing}{scope_column} "
              f"{outcome.case.display_path}")
        if status == "unlocked":
            print("      missing required full-output lock")
        elif status == "output-drift":
            print(f"      output drift: actual={result['actual_output_fingerprint']}")
        if key == "fail":
            _print_mismatch(result)
        if result["render_visibility_mismatch_count"]:
            print(f"      render_visibility_diagnostic={result['render_visibility_mismatch_count']:,} "
                  f"first={result['first_render_visibility_mismatch_frame']} "
                  "field=state_flags[*][4]&0x80")
    aggregate_fps = compared_frames / wall_seconds if wall_seconds > 0.0 else 0.0
    print(f"[{backend}] summary: pass={counts['pass']} diagnostic={counts['diagnostic']} "
          f"fail={counts['fail']} error={counts['error']} "
          f"frames={compared_frames:,} wall={wall_seconds:.3f}s "
          f"aggregate_fps={aggregate_fps:,.0f} runner_cpu={runner_seconds:.3f}s")
    return counts["fail"] == 0 and counts["error"] == 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Stream Slippi replays through parallel Melee core scalar runtimes."
    )
    parser.add_argument("replay", type=Path, nargs="*")
    parser.add_argument(
        "--suite",
        type=Path,
        default=None,
        help="canonical replay suite; mutually exclusive with positional replay paths",
    )
    parser.add_argument(
        "--characters",
        default=DEFAULT_CHARACTERS,
        help=f"suite character scope (default: {DEFAULT_CHARACTERS})",
    )
    parser.add_argument(
        "--stages",
        default=DEFAULT_STAGES,
        help=f"suite stage-id scope (default: {DEFAULT_STAGES})",
    )
    parser.add_argument(
        "--frames", type=int, default=0, help="Frames per replay; 0 means all."
    )
    parser.add_argument(
        "--start-frame",
        type=int,
        default=None,
        help="Warm from replay start, then begin comparison at this frame.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=None,
        help="Per-replay timeout; defaults to 8s native and 30s PPC.",
    )
    parser.add_argument(
        "--fnmsubs-profile", choices=("retail", "dolphin-legacy"),
        help="Explicit recording arithmetic profile; omission uses replay metadata defaults.",
    )
    parser.add_argument("--backend", choices=("ppc", "native", "both"), default="native")
    parser.add_argument(
        "--timing", action="store_true", help="Print per-replay runner timing."
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=0,
        help=f"Parallel replay runners (0 = auto, capped at {MAX_AUTO_WORKERS}).",
    )
    parser.add_argument("--build-jobs", type=int, default=4)
    parser.add_argument(
        "--no-build", action="store_true", help="Use the existing native and PPC binaries."
    )
    parser.add_argument(
        "--diagnostic-signed-zero-equal",
        action="store_true",
        help="Ignore only +0.0/-0.0 bit differences while finding the next mismatch.",
    )
    parser.add_argument(
        "--diagnostic", action="store_true",
        help="Inspect an ineligible capture; manual paths only, never suite acceptance or output locks.",
    )
    parser.add_argument(
        "--output-locks",
        type=Path,
        default=DEFAULT_OUTPUT_LOCKS,
        help="Exact full-simulator-output locks for behavior-neutral refactors.",
    )
    parser.add_argument(
        "--write-output-locks",
        action="store_true",
        help="Write full replay output locks from this run.",
    )
    args = parser.parse_args()
    if bool(args.replay) == bool(args.suite):
        parser.error("provide either positional replay paths or --suite")
    if args.frames < 0:
        parser.error("--frames must be non-negative")
    if args.timeout is not None and args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.build_jobs <= 0:
        parser.error("--build-jobs must be positive")

    if args.suite and (args.diagnostic or args.diagnostic_signed_zero_equal or args.frames or args.start_frame is not None):
        parser.error("suite acceptance requires complete strict admitted replays; use manual paths for diagnostics")
    if args.write_output_locks and (args.diagnostic or args.diagnostic_signed_zero_equal or args.frames or args.start_frame is not None):
        parser.error("output locks require complete strict admitted replays")

    try:
        output_lock_path = args.output_locks.expanduser()
        if not output_lock_path.is_absolute():
            output_lock_path = ROOT / output_lock_path
        if output_lock_path.is_file():
            output_locks = load_output_locks(output_lock_path)
        elif args.write_output_locks:
            output_locks = {}
        else:
            raise ValueError(f"output-lock manifest does not exist: {output_lock_path}")
        suite: ReplaySuite | None = None
        if args.suite is not None:
            suite_path = args.suite.expanduser()
            if not suite_path.is_absolute():
                suite_path = ROOT / suite_path
            characters = _parse_characters(args.characters)
            stages = _parse_stages(args.stages)
            suite, cases = load_suite_cases(
                suite_path,
                characters=characters,
                stages=stages,
            )
        else:
            stages = frozenset()
            cases = _manual_cases(args.replay)
        if args.fnmsubs_profile is not None:
            cases = [replace(case, fnmsubs_profile=args.fnmsubs_profile) for case in cases]
        if args.write_output_locks and suite is not None and suite.name == "melee_core_aggregate":
            selected = {entry.replay for entry in suite.replays}
            output_locks = {key: lock for key, lock in output_locks.items() if key in selected}
        workers = _resolve_worker_count(args.workers, len(cases))

        if not args.no_build:
            build_validation(backend=args.backend, jobs=args.build_jobs)
        native = load_native()

        if suite is not None:
            print(
                f"suite: {suite.name} selected={len(cases)}/{len(suite.replays)} "
                f"source={display_path_under_repo(args.suite, ROOT)}"
            )
            print(
                f"scope: characters={','.join(sorted(args.characters.split(',')))} "
                f"stages={','.join(str(stage) for stage in sorted(stages))} "
                "profile=UCF-0.84+manifest-cardinals"
            )
        else:
            print(f"replays: {len(cases)}")
        print(
            f"output_locks: entries={len(output_locks)} "
            f"source={display_path_under_repo(output_lock_path, ROOT)}"
        )

        all_passed = True
        backends = ("native", "ppc") if args.backend == "both" else (args.backend,)
        for backend in backends:
            timeout = args.timeout if args.timeout is not None else (8.0 if backend == "native" else 30.0)
            outcomes, wall_seconds = run_cases(
                native,
                cases,
                workers=workers,
                frames=args.frames,
                start_frame=args.start_frame,
                timeout=timeout,
                backend=backend,
                signed_zero_equal=args.diagnostic_signed_zero_equal,
                diagnostic=args.diagnostic,
            )
            if args.write_output_locks:
                output_locks = write_output_locks(
                    output_lock_path, output_locks, backend, outcomes
                )
            all_passed = (
                print_backend_results(
                    backend,
                    outcomes,
                    workers=workers,
                    wall_seconds=wall_seconds,
                    show_timing=args.timing,
                    output_locks=output_locks,
                    require_output_lock=(
                        suite is not None
                        and suite.name == "melee_core_aggregate"
                        and args.frames == 0
                        and args.start_frame is None
                    ),
                )
                and all_passed
            )
        return 0 if all_passed else 1
    except (ImportError, OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
