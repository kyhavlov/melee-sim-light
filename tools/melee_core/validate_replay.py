from __future__ import annotations

import argparse
import concurrent.futures
import importlib.util
import os
import subprocess
import time
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from types import ModuleType

from peppi_py import _read_slippi

from melee_sim.raw_data import raw_data_dir, validate_raw_data_root
from tools.slippi.slpz import replay_path_for_peppi, resolve_replay_path
from tools.slippi.suite_io import ReplaySuite, display_path_under_repo, load_suite


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build" / "melee_core"
NATIVE = BUILD / "validation" / "_msl_replay_validate.so"
PPC_BINARY = BUILD / "ppc" / "melee-core-ppc"
NATIVE_BINARY = BUILD / "native" / "melee-core-native"
TOOLCHAIN = BUILD / "toolchain" / "root"
QEMU = TOOLCHAIN / "usr" / "bin" / "qemu-ppc-static"
SYSROOT = TOOLCHAIN / "usr" / "powerpc-linux-gnu"
DEFAULT_CHARACTERS = "Fox,Falco"
DEFAULT_STAGES = "32,31,3"
MAX_AUTO_WORKERS = 16

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


@dataclass(frozen=True)
class ReplayOutcome:
    case: ReplayCase
    result: dict[str, object] | None
    error: str | None
    elapsed_seconds: float


@lru_cache(maxsize=1)
def game_data_dir() -> Path:
    path = raw_data_dir(default=ROOT / "data")
    validate_raw_data_root(path, verify_hashes=True)
    return path


def build_validation(*, backend: str, jobs: int = 4) -> None:
    if backend == "native":
        targets = ["validator", "native"]
    elif backend == "ppc":
        targets = ["validation"]
    elif backend == "both":
        targets = ["validation", "native"]
    else:
        raise ValueError(f"unsupported validation backend: {backend}")
    subprocess.run(
        [
            "make",
            "--no-print-directory",
            "--silent",
            "-f",
            "src/melee_core/Makefile",
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
) -> dict[str, object]:
    # Python owns only the replay-loading boundary. The native extension consumes
    # Peppi's Arrow buffers through the Arrow C Data Interface without NumPy or
    # per-frame Python work.
    started = time.perf_counter()
    with replay_path_for_peppi(replay) as peppi_path:
        game = _read_slippi(str(peppi_path), False)
        result = native.validate_replay(
            game.frames,
            game.start,
            game.metadata,
            qemu=str(QEMU),
            sysroot=str(SYSROOT),
            binary=str(NATIVE_BINARY if backend == "native" else PPC_BINARY),
            data_dir=str(game_data_dir()),
            start_frame=start_frame,
            frames_limit=frames,
            timeout=timeout,
            signed_zero_equal=signed_zero_equal,
            native=backend == "native",
        )
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
    if suite.ucf_enabled is not True or suite.ucf_cardinals_1_0_enabled is not True:
        raise ValueError(
            f"{suite_path}: Melee core validation currently requires UCF 0.84 and 1.0 cardinals"
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
) -> ReplayOutcome:
    started = time.perf_counter()
    try:
        result = validate_one(
            native,
            case.replay,
            frames=frames,
            start_frame=start_frame,
            timeout=timeout,
            backend=backend,
            signed_zero_equal=signed_zero_equal,
        )
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
) -> tuple[list[ReplayOutcome], float]:
    # Warm the manifest/hash check before workers fan out. Each C call then starts an isolated
    # scalar runtime process and releases the GIL for the complete stream boundary.
    game_data_dir()
    started = time.perf_counter()
    kwargs = {
        "frames": frames,
        "start_frame": start_frame,
        "timeout": timeout,
        "backend": backend,
        "signed_zero_equal": signed_zero_equal,
    }
    if workers == 1:
        outcomes = [_validate_case(native, case, **kwargs) for case in cases]
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
            futures = [
                executor.submit(_validate_case, native, case, **kwargs) for case in cases
            ]
            # Resolve in manifest order so stdout and regressions remain deterministic.
            outcomes = [future.result() for future in futures]
    return outcomes, time.perf_counter() - started


def _case_scope(case: ReplayCase) -> str:
    if case.stage_id is None:
        return ""
    stage = STAGE_NAMES.get(case.stage_id, f"stage {case.stage_id}")
    players = "/".join(case.characters)
    ports = "/".join(f"P{port}" for port in case.ports)
    return f"{stage} {players} {ports}"


def print_backend_results(
    backend: str,
    outcomes: list[ReplayOutcome],
    *,
    workers: int,
    wall_seconds: float,
    show_timing: bool,
) -> bool:
    print(f"\n[{backend}] workers={workers}")
    passed = 0
    failed = 0
    errors = 0
    compared_frames = 0
    runner_seconds = 0.0
    for outcome in outcomes:
        scope = _case_scope(outcome.case)
        scope_column = f" {scope:<39}" if scope else ""
        if outcome.error is not None:
            errors += 1
            timing = f" {outcome.elapsed_seconds:7.3f}s" if show_timing else ""
            print(f"ERROR {'-':>15}{timing}{scope_column} {outcome.case.display_path}")
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
        if bool(result["pass"]):
            passed += 1
            print(
                f"PASS  {matched:>7,}/{frames:<7,}{timing}{scope_column} "
                f"{outcome.case.display_path}"
            )
        else:
            failed += 1
            print(
                f"FAIL  {matched:>7,}/{frames:<7,}{timing}{scope_column} "
                f"{outcome.case.display_path}"
            )
            print(
                "      "
                f"first={result['first_mismatch_frame']} fields={result['mismatch_count']}"
            )
            for detail in list(result["details"])[:3]:
                print(
                    f"      {detail['field']}: expected={detail['expected']} "
                    f"actual={detail['actual']}"
                )

    aggregate_fps = compared_frames / wall_seconds if wall_seconds > 0.0 else 0.0
    print(
        f"[{backend}] summary: pass={passed} fail={failed} error={errors} "
        f"frames={compared_frames:,} wall={wall_seconds:.3f}s "
        f"aggregate_fps={aggregate_fps:,.0f} runner_cpu={runner_seconds:.3f}s"
    )
    return failed == 0 and errors == 0


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
    args = parser.parse_args()
    if bool(args.replay) == bool(args.suite):
        parser.error("provide either positional replay paths or --suite")
    if args.frames < 0:
        parser.error("--frames must be non-negative")
    if args.timeout is not None and args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.build_jobs <= 0:
        parser.error("--build-jobs must be positive")

    try:
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
                "profile=UCF-0.84+cardinals-1.0"
            )
        else:
            print(f"replays: {len(cases)}")

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
            )
            all_passed = (
                print_backend_results(
                    backend,
                    outcomes,
                    workers=workers,
                    wall_seconds=wall_seconds,
                    show_timing=args.timing,
                )
                and all_passed
            )
        return 0 if all_passed else 1
    except (ImportError, OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
