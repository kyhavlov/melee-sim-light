#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from tools.dolphin.engine_dump_io import read_engine_dump

INTERPRETER_PROBE_WARN_FRAMES = 3


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


@dataclass(frozen=True)
class CaptureResult:
    returncode: int
    out_bin: Path
    stdout_log: Path
    stderr_log: Path
    elapsed_sec: float
    frame_count: int | None
    first_frame: int | None
    last_frame: int | None
    error: str | None = None


def _set_ini_key(lines: list[str], section: str, required: dict[str, str]) -> list[str]:
    out: list[str] = []
    in_section = False
    seen: set[str] = set()
    header = f"[{section}]"
    found = False

    def append_missing() -> None:
        for key, value in required.items():
            if key not in seen:
                out.append(f"{key} = {value}")

    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if in_section:
                append_missing()
                seen.clear()
            in_section = stripped == header
            found = found or in_section
            out.append(line)
            continue
        if in_section and "=" in line:
            key = line.split("=", 1)[0].strip()
            if key in required:
                seen.add(key)
                out.append(f"{key} = {required[key]}")
                continue
        out.append(line)
    if in_section:
        append_missing()
    if not found:
        if out and out[-1].strip():
            out.append("")
        out.append(header)
        for key, value in required.items():
            out.append(f"{key} = {value}")
    return out


def _write_dolphin_ini(user_dir: Path) -> None:
    cfg_dir = user_dir / "Config"
    cfg_dir.mkdir(parents=True, exist_ok=True)
    ini_path = cfg_dir / "Dolphin.ini"
    lines = []
    if ini_path.exists():
        lines = ini_path.read_text().splitlines()
    if not lines:
        lines = ["[Core]"]
    out = _set_ini_key(
        lines,
        "Core",
        {
            "GFXBackend": "Null",
            "CPUCore": "1",
            "EmulationSpeed": "0.000",
            "DSPHLE": "True",
        },
    )
    out = _set_ini_key(out, "DSP", {"Backend": "NullSound"})
    ini_path.write_text("\n".join(out) + "\n")


def _write_playback_txt(
    user_dir: Path,
    *,
    replay: Path,
    start_frame: int,
    end_frame: int,
    dump_path: Path,
    should_resync: bool = True,
) -> Path:
    slippi_dir = user_dir / "Slippi"
    slippi_dir.mkdir(parents=True, exist_ok=True)
    playback_path = slippi_dir / "playback.txt"
    payload = {
        "mode": "normal",
        "replay": str(replay.resolve()),
        "startFrame": int(start_frame),
        "endFrame": int(end_frame),
        "commandId": str(int(time.time() * 1000)),
        "isRealTimeMode": False,
        "shouldResync": bool(should_resync),
        "rollbackDisplayMethod": "off",
        "engineDumpPath": str(dump_path.resolve()),
    }
    playback_path.write_text(json.dumps(payload))
    return playback_path


def _resolve_frame_window(
    *,
    replay: Path,
    start_frame: int | None,
    end_frame: int | None,
) -> tuple[int, int]:
    out_start = start_frame
    out_end = end_frame
    if out_start is None or out_end is None:
        try:
            import peppi_bytes  # type: ignore

            from melee_sim.metrics import canonicalize_slippi_sample_last  # type: ignore
            from melee_sim.replay_io import read_replay_bytes  # type: ignore

            rb = read_replay_bytes(str(replay))
            sample = canonicalize_slippi_sample_last(peppi_bytes.read_slippi_bytes_sample(rb, 0, False))
            frames = sample["frame"]
            if out_start is None:
                out_start = int(frames[0])
            if out_end is None:
                out_end = int(frames[-1])
        except Exception:
            pass
    if out_start is None:
        out_start = -123
    if out_end is None:
        out_end = 999999
    return int(out_start), int(out_end)


def _read_dump_status(path: Path, start_frame: int, end_frame: int) -> tuple[int, int, int, bool]:
    d = read_engine_dump(path)
    frames = [int(fr["frame_index"]) for fr in d.frames]
    if not frames:
        return 0, 0, 0, False
    first_frame = min(frames)
    last_frame = max(frames)
    seen = set(frames)
    complete = all(frame in seen for frame in range(int(start_frame), int(end_frame) + 1))
    return len(frames), first_frame, last_frame, complete


def _terminate_process_group(proc: subprocess.Popen[object]) -> None:
    if proc.poll() is not None:
        return
    try:
        os.killpg(proc.pid, signal.SIGTERM)
        proc.wait(timeout=5.0)
    except Exception:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass


def capture_engine_dump(
    *,
    replay: str | Path,
    dolphin: str | Path,
    iso: str | Path,
    user_dir: str | Path,
    out_bin: str | Path,
    start_frame: int | None = None,
    end_frame: int | None = None,
    timeout: float = 600.0,
    should_resync: bool = True,
    collision_probe_path: str | Path | None = None,
    collision_probe_frame_start: int | None = None,
    collision_probe_frame_end: int | None = None,
    damagefall_probe_path: str | Path | None = None,
    damagefall_probe_frame_start: int | None = None,
    damagefall_probe_frame_end: int | None = None,
    fall_floor_probe_path: str | Path | None = None,
    fall_floor_probe_frame_start: int | None = None,
    fall_floor_probe_frame_end: int | None = None,
    probe_interpreter_frame_start: int | None = None,
    probe_interpreter_frame_end: int | None = None,
    throw_release_probe_path: str | Path | None = None,
    throw_laser_event_probe_path: str | Path | None = None,
    laser_shield_reflect_event_probe_path: str | Path | None = None,
) -> CaptureResult:
    replay = Path(replay)
    if not replay.exists():
        raise FileNotFoundError(replay)
    out_bin = Path(out_bin)
    if out_bin.exists():
        out_bin.unlink()
    out_bin.parent.mkdir(parents=True, exist_ok=True)
    stdout_log = out_bin.with_suffix(out_bin.suffix + ".stdout.log")
    stderr_log = out_bin.with_suffix(out_bin.suffix + ".stderr.log")
    for log_path in (stdout_log, stderr_log):
        if log_path.exists():
            log_path.unlink()

    user_dir = Path(user_dir)
    user_dir.mkdir(parents=True, exist_ok=True)
    _write_dolphin_ini(user_dir)

    resolved_start, resolved_end = _resolve_frame_window(
        replay=replay, start_frame=start_frame, end_frame=end_frame
    )
    if (probe_interpreter_frame_start is None) != (probe_interpreter_frame_end is None):
        raise ValueError(
            "probe_interpreter_frame_start and probe_interpreter_frame_end must be provided together"
        )

    playback_txt = _write_playback_txt(
        user_dir,
        replay=replay,
        start_frame=resolved_start,
        end_frame=resolved_end,
        dump_path=out_bin,
        should_resync=should_resync,
    )

    dolphin = Path(dolphin)
    if not dolphin.exists():
        raise FileNotFoundError(dolphin)
    iso = Path(iso)
    if not iso.exists():
        raise FileNotFoundError(iso)

    proc_args = [
        str(dolphin),
        "-e",
        str(iso.resolve()),
        "-u",
        str(user_dir.resolve()),
        "--slippi-input",
        str(playback_txt.resolve()),
    ]
    env = os.environ.copy()
    probe_windows: list[tuple[int, int]] = []

    def add_probe_window(
        probe_path: str | Path | None,
        probe_start: int | None = None,
        probe_end: int | None = None,
    ) -> None:
        if probe_path is None:
            return
        default_start = (
            resolved_start
            if probe_interpreter_frame_start is None
            else int(probe_interpreter_frame_start)
        )
        default_end = (
            resolved_end
            if probe_interpreter_frame_end is None
            else int(probe_interpreter_frame_end)
        )
        window_start = default_start if probe_start is None else int(probe_start)
        window_end = default_end if probe_end is None else int(probe_end)
        probe_windows.append((window_start, window_end))

    add_probe_window(
        collision_probe_path,
        collision_probe_frame_start,
        collision_probe_frame_end,
    )
    add_probe_window(
        damagefall_probe_path,
        damagefall_probe_frame_start,
        damagefall_probe_frame_end,
    )
    add_probe_window(
        fall_floor_probe_path,
        fall_floor_probe_frame_start,
        fall_floor_probe_frame_end,
    )
    add_probe_window(throw_release_probe_path)
    add_probe_window(throw_laser_event_probe_path)
    add_probe_window(laser_shield_reflect_event_probe_path)
    if probe_windows:
        interpreter_start = min(start for start, _end in probe_windows)
        interpreter_end = max(end for _start, end in probe_windows)
        interpreter_frame_count = interpreter_end - interpreter_start + 1
        if interpreter_frame_count > INTERPRETER_PROBE_WARN_FRAMES:
            print(
                "WARNING: Dolphin interpreter probes are extremely slow. "
                f"This run requested {interpreter_frame_count} consecutive interpreter frames "
                f"({interpreter_start}..{interpreter_end}). Keep interpreter windows to the "
                "exact target frames; do not use broad context windows unless you deliberately "
                "accept a very slow run.",
                file=sys.stderr,
            )
        env["MSL_PROBE_INTERPRETER_FRAME_START"] = str(interpreter_start)
        env["MSL_PROBE_INTERPRETER_FRAME_END"] = str(interpreter_end)

    if collision_probe_path is not None:
        env["MSL_COLLISION_PROBE_PATH"] = str(Path(collision_probe_path).resolve())
        env["MSL_COLLISION_PROBE_FRAME_START"] = str(
            resolved_start if collision_probe_frame_start is None else int(collision_probe_frame_start)
        )
        env["MSL_COLLISION_PROBE_FRAME_END"] = str(
            resolved_end if collision_probe_frame_end is None else int(collision_probe_frame_end)
        )
    if damagefall_probe_path is not None:
        env["MSL_DAMAGEFALL_PROBE_PATH"] = str(Path(damagefall_probe_path).resolve())
        env["MSL_DAMAGEFALL_PROBE_FRAME_START"] = str(
            resolved_start if damagefall_probe_frame_start is None else int(damagefall_probe_frame_start)
        )
        env["MSL_DAMAGEFALL_PROBE_FRAME_END"] = str(
            resolved_end if damagefall_probe_frame_end is None else int(damagefall_probe_frame_end)
        )
    if fall_floor_probe_path is not None:
        env["MSL_FALL_FLOOR_PROBE_PATH"] = str(Path(fall_floor_probe_path).resolve())
        env["MSL_FALL_FLOOR_PROBE_FRAME_START"] = str(
            resolved_start if fall_floor_probe_frame_start is None else int(fall_floor_probe_frame_start)
        )
        env["MSL_FALL_FLOOR_PROBE_FRAME_END"] = str(
            resolved_end if fall_floor_probe_frame_end is None else int(fall_floor_probe_frame_end)
        )
    if throw_release_probe_path is not None:
        env["MSL_THROW_RELEASE_PROBE_PATH"] = str(
            Path(throw_release_probe_path).resolve()
        )
    if throw_laser_event_probe_path is not None:
        env["MSL_THROW_LASER_EVENT_PROBE_PATH"] = str(
            Path(throw_laser_event_probe_path).resolve()
        )
    if laser_shield_reflect_event_probe_path is not None:
        env["MSL_LASER_SHIELD_REFLECT_EVENT_PROBE_PATH"] = str(
            Path(laser_shield_reflect_event_probe_path).resolve()
        )
    t0 = time.monotonic()
    rc = 1
    error: str | None = None
    frame_count: int | None = None
    first_frame: int | None = None
    last_frame: int | None = None
    try:
        stdout_log.parent.mkdir(parents=True, exist_ok=True)
        with stdout_log.open("wb") as stdout_f, stderr_log.open("wb") as stderr_f:
            proc = subprocess.Popen(
                proc_args,
                env=env,
                stdout=stdout_f,
                stderr=stderr_f,
                start_new_session=True,
            )
            while time.monotonic() - t0 < timeout:
                proc_rc = proc.poll()
                if out_bin.exists() and out_bin.stat().st_size > 0:
                    try:
                        count, first, last, complete = _read_dump_status(
                            out_bin, resolved_start, resolved_end
                        )
                        frame_count = count
                        first_frame = first
                        last_frame = last
                        if complete:
                            rc = 0
                            break
                    except Exception as exc:
                        error = f"dump parse pending: {exc}"
                        if proc_rc is not None:
                            break
                elif proc_rc is not None:
                    rc = int(proc_rc or 1)
                    break
                time.sleep(0.25)
            else:
                error = f"timeout after {float(timeout):.1f}s"
            if proc.poll() is None:
                _terminate_process_group(proc)
            elif rc != 0:
                rc = int(proc.returncode or 1)
    except Exception as exc:
        error = str(exc)
        rc = 1

    elapsed = time.monotonic() - t0
    if rc == 0:
        error = None
    elif out_bin.exists() and out_bin.stat().st_size > 0 and frame_count is None:
        try:
            frame_count, first_frame, last_frame, _complete = _read_dump_status(
                out_bin, resolved_start, resolved_end
            )
        except Exception as exc:
            error = error or str(exc)
    elif not out_bin.exists():
        error = error or "dump file was not created"
    elif out_bin.stat().st_size <= 0:
        error = error or "dump file is empty"

    return CaptureResult(
        returncode=int(rc),
        out_bin=out_bin,
        stdout_log=stdout_log,
        stderr_log=stderr_log,
        elapsed_sec=float(elapsed),
        frame_count=frame_count,
        first_frame=first_frame,
        last_frame=last_frame,
        error=error,
    )


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate engine dump via playback dolphin CLI only.")
    ap.add_argument("--replay", required=True, type=Path)
    ap.add_argument("--iso", default=str(Path.cwd() / "SSBM.iso"))
    ap.add_argument("--dolphin", required=True, help="path to playback dolphin-emu")
    ap.add_argument(
        "--user-dir",
        default=str(Path("reports/triage") / f"{_timestamp()}_dolphin_user"),
        help="Dolphin user dir for playback config/temporary state",
    )
    ap.add_argument("--start-frame", type=int, default=None)
    ap.add_argument("--end-frame", type=int, default=None)
    ap.add_argument("--out-bin", required=True)
    ap.add_argument("--timeout", type=float, default=600.0)
    ap.add_argument(
        "--no-resync",
        action="store_true",
        help="set playback shouldResync=false for rollout-style patched-input probes",
    )
    ap.add_argument(
        "--collision-probe",
        type=Path,
        default=None,
        help="optional JSONL path for pre-collision primitive probes; uses a bounded interpreter CPU window",
    )
    ap.add_argument("--collision-probe-frame-start", type=int, default=None)
    ap.add_argument("--collision-probe-frame-end", type=int, default=None)
    ap.add_argument(
        "--damagefall-probe",
        type=Path,
        default=None,
        help="optional JSONL path for DamageFall IASA/Fall_Enter events; uses a bounded interpreter CPU window",
    )
    ap.add_argument("--damagefall-probe-frame-start", type=int, default=None)
    ap.add_argument("--damagefall-probe-frame-end", type=int, default=None)
    ap.add_argument(
        "--fall-floor-probe",
        type=Path,
        default=None,
        help="optional JSONL path for Fall/FallSpecial mpColl floor publication events; uses a bounded interpreter CPU window",
    )
    ap.add_argument("--fall-floor-probe-frame-start", type=int, default=None)
    ap.add_argument("--fall-floor-probe-frame-end", type=int, default=None)
    ap.add_argument(
        "--probe-interpreter-frame-start",
        type=int,
        default=None,
        help="first frame where interpreter CPU mode may be enabled; keep this window tiny because interpreter mode is very slow",
    )
    ap.add_argument(
        "--probe-interpreter-frame-end",
        type=int,
        default=None,
        help="last frame where interpreter CPU mode may be enabled; keep this window tiny because interpreter mode is very slow",
    )
    ap.add_argument(
        "--throw-release-probe",
        type=Path,
        default=None,
        help="optional JSONL path for ftCo_800DDDE4 throw-release position publication events; uses a bounded interpreter CPU window",
    )
    ap.add_argument(
        "--throw-laser-event-probe",
        type=Path,
        default=None,
        help="optional JSONL path for throw-laser item spawn/body/damage/delete events; uses a bounded interpreter CPU window",
    )
    ap.add_argument(
        "--laser-shield-reflect-event-probe",
        type=Path,
        default=None,
        help="optional JSONL path for laser shield/reflect branch events; uses a bounded interpreter CPU window",
    )
    args = ap.parse_args()

    result = capture_engine_dump(
        replay=args.replay,
        dolphin=args.dolphin,
        iso=args.iso,
        user_dir=args.user_dir,
        out_bin=args.out_bin,
        start_frame=args.start_frame,
        end_frame=args.end_frame,
        timeout=float(args.timeout),
        should_resync=not args.no_resync,
        collision_probe_path=args.collision_probe,
        collision_probe_frame_start=args.collision_probe_frame_start,
        collision_probe_frame_end=args.collision_probe_frame_end,
        damagefall_probe_path=args.damagefall_probe,
        damagefall_probe_frame_start=args.damagefall_probe_frame_start,
        damagefall_probe_frame_end=args.damagefall_probe_frame_end,
        fall_floor_probe_path=args.fall_floor_probe,
        fall_floor_probe_frame_start=args.fall_floor_probe_frame_start,
        fall_floor_probe_frame_end=args.fall_floor_probe_frame_end,
        probe_interpreter_frame_start=args.probe_interpreter_frame_start,
        probe_interpreter_frame_end=args.probe_interpreter_frame_end,
        throw_release_probe_path=args.throw_release_probe,
        throw_laser_event_probe_path=args.throw_laser_event_probe,
        laser_shield_reflect_event_probe_path=args.laser_shield_reflect_event_probe,
    )
    if result.returncode != 0:
        coverage = ""
        if result.frame_count is not None:
            coverage = (
                f" frames={result.frame_count} first={result.first_frame} last={result.last_frame}"
            )
        print(
            "engine dump capture failed: "
            f"{result.out_bin} elapsed={result.elapsed_sec:.1f}s{coverage} "
            f"stdout={result.stdout_log} stderr={result.stderr_log} error={result.error}"
        )
    else:
        print(
            f"wrote {result.out_bin.resolve()} "
            f"frames={result.frame_count} elapsed={result.elapsed_sec:.1f}s"
        )
        print(f"stdout={result.stdout_log.resolve()}")
        print(f"stderr={result.stderr_log.resolve()}")
    return int(result.returncode)


if __name__ == "__main__":
    raise SystemExit(main())
