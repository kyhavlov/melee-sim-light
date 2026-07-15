from __future__ import annotations

import argparse
import importlib.util
import subprocess
from pathlib import Path
from types import ModuleType

from peppi_py import _read_slippi

from tools.slippi.slpz import replay_path_for_peppi, resolve_replay_path


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build" / "melee_core"
NATIVE = BUILD / "validation" / "_msl_replay_validate.so"
BINARY = BUILD / "ppc" / "melee-core-ppc"
TOOLCHAIN = BUILD / "toolchain" / "root"
QEMU = TOOLCHAIN / "usr" / "bin" / "qemu-ppc-static"
SYSROOT = TOOLCHAIN / "usr" / "powerpc-linux-gnu"
GAME_DATA = ROOT / "refs" / "melee-disc" / "files"


def build_validation(*, jobs: int = 2) -> None:
    subprocess.run(
        [
            "make",
            "-f",
            "src/melee_core/Makefile",
            f"-j{max(1, jobs)}",
            "validation",
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
    signed_zero_equal: bool = False,
) -> dict[str, object]:
    # Python owns only the replay-loading boundary. The native extension consumes
    # Peppi's Arrow buffers through the Arrow C Data Interface without NumPy or
    # per-frame Python work.
    with replay_path_for_peppi(replay) as peppi_path:
        game = _read_slippi(str(peppi_path), False)
        return native.validate_replay(
            game.frames,
            game.start,
            game.metadata,
            qemu=str(QEMU),
            sysroot=str(SYSROOT),
            binary=str(BINARY),
            data_dir=str(GAME_DATA),
            start_frame=start_frame,
            frames_limit=frames,
            timeout=timeout,
            signed_zero_equal=signed_zero_equal,
        )


def print_result(replay: Path, result: dict[str, object]) -> bool:
    print(f"replay: {replay}")
    print(
        f"frames: {result['frames']}/{result['available']} "
        f"processed={result['processed']}/{result['total']} "
        f"seed_frame={result['seed_frame']} "
        f"first_ref_frame={result['first_ref_frame']}"
    )
    if result["signed_zero_equal_count"]:
        print(
            "diagnostic: "
            f"signed_zero_equal={result['signed_zero_equal_count']}"
        )
    if result["render_visibility_mismatch_count"]:
        print(
            "diagnostic: "
            "render_visibility_mismatches="
            f"{result['render_visibility_mismatch_count']} "
            "first_frame="
            f"{result['first_render_visibility_mismatch_frame']}"
        )
    if result["pass"]:
        print(f"PASS matched_frames={result['matched_frames']}")
        return True

    print(
        f"FAIL matched_frames={result['matched_frames']} "
        f"first_mismatch_frame={result['first_mismatch_frame']} "
        f"fields={result['mismatch_count']}"
    )
    for detail in result["details"]:
        print(
            f"  {detail['field']}: expected={detail['expected']} "
            f"actual={detail['actual']}"
        )
    if int(result["mismatch_count"]) > len(result["details"]):
        print("  ...")
    return False


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Stream Slippi replays through the PPC Melee core reference."
    )
    parser.add_argument("replay", type=Path, nargs="+")
    parser.add_argument(
        "--frames", type=int, default=0, help="Frames per replay; 0 means all."
    )
    parser.add_argument(
        "--start-frame",
        type=int,
        default=None,
        help="Warm from replay start, then begin comparison at this frame.",
    )
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--no-build", action="store_true", help="Use the existing native and PPC binaries."
    )
    parser.add_argument(
        "--diagnostic-signed-zero-equal",
        action="store_true",
        help="Ignore only +0.0/-0.0 bit differences while finding the next mismatch.",
    )
    args = parser.parse_args()
    if args.frames < 0:
        parser.error("--frames must be non-negative")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    replays = [resolve_replay_path(path.expanduser().resolve()) for path in args.replay]
    for replay in replays:
        if not replay.is_file():
            parser.error(f"replay does not exist: {replay}")
    try:
        if not args.no_build:
            build_validation(jobs=args.jobs)
        native = load_native()
        passed = True
        for replay in replays:
            result = validate_one(
                native,
                replay,
                frames=args.frames,
                start_frame=args.start_frame,
                timeout=args.timeout,
                signed_zero_equal=args.diagnostic_signed_zero_equal,
            )
            passed = print_result(replay, result) and passed
    except (ImportError, OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        parser.error(str(exc))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
