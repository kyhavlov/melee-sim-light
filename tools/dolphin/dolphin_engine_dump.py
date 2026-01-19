#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import time
from pathlib import Path


def _write_dolphin_ini(user_dir: Path) -> None:
    cfg_dir = user_dir / "Config"
    cfg_dir.mkdir(parents=True, exist_ok=True)
    ini_path = cfg_dir / "Dolphin.ini"
    lines = []
    if ini_path.exists():
        lines = ini_path.read_text().splitlines()
    if not lines:
        lines = ["[Core]"]
    if "[Core]" not in lines:
        lines.append("[Core]")
    out = []
    in_core = False
    seen = set()
    for ln in lines:
        if ln.strip().startswith("["):
            if in_core:
                for k, v in (("GFXBackend", "Null"),):
                    if k not in seen:
                        out.append(f"{k} = {v}")
                in_core = False
                seen.clear()
            out.append(ln)
            in_core = ln.strip() == "[Core]"
            continue
        if in_core and "=" in ln:
            k = ln.split("=")[0].strip()
            if k in ("GFXBackend",):
                seen.add(k)
                out.append("GFXBackend = Null")
                continue
        out.append(ln)
    if in_core:
        if "GFXBackend" not in seen:
            out.append("GFXBackend = Null")
    if "[DSP]" not in out:
        out.append("[DSP]")
        out.append("Backend = NullSound")
    else:
        dsp_out = []
        in_dsp = False
        dsp_seen = False
        for ln in out:
            if ln.strip().startswith("["):
                if in_dsp and not dsp_seen:
                    dsp_out.append("Backend = NullSound")
                in_dsp = ln.strip() == "[DSP]"
                dsp_out.append(ln)
                continue
            if in_dsp and "=" in ln:
                k = ln.split("=")[0].strip()
                if k == "Backend":
                    dsp_seen = True
                    dsp_out.append("Backend = NullSound")
                    continue
            dsp_out.append(ln)
        if in_dsp and not dsp_seen:
            dsp_out.append("Backend = NullSound")
        out = dsp_out
    ini_path.write_text("\n".join(out) + "\n")


def _write_playback_txt(
    user_dir: Path,
    *,
    replay: Path,
    start_frame: int,
    end_frame: int,
    dump_path: Path,
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
        "shouldResync": True,
        "rollbackDisplayMethod": "off",
        "engineDumpPath": str(dump_path.resolve()),
    }
    playback_path.write_text(json.dumps(payload))
    return playback_path


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate engine dump via playback dolphin.")
    ap.add_argument("--replay", required=True)
    ap.add_argument("--iso", default=str(Path.cwd() / "SSBM.iso"))
    ap.add_argument("--dolphin", required=True, help="path to playback dolphin-emu")
    ap.add_argument("--user-dir", default=str(Path("/tmp/ish_playback_user")))
    ap.add_argument("--start-frame", type=int, default=None)
    ap.add_argument("--end-frame", type=int, default=None)
    ap.add_argument("--out-bin", required=True)
    ap.add_argument("--timeout", type=float, default=120.0)
    args = ap.parse_args()

    replay = Path(args.replay)
    if not replay.exists():
        raise FileNotFoundError(replay)
    out_bin = Path(args.out_bin)
    if out_bin.exists():
        out_bin.unlink()

    user_dir = Path(args.user_dir)
    user_dir.mkdir(parents=True, exist_ok=True)
    _write_dolphin_ini(user_dir)

    start_frame = args.start_frame
    end_frame = args.end_frame
    if start_frame is None or end_frame is None:
        try:
            import peppi_bytes  # type: ignore

            from melee_sim.metrics import canonicalize_slippi_sample_last  # type: ignore
            from melee_sim.replay_io import read_replay_bytes  # type: ignore

            rb = read_replay_bytes(str(replay))
            sample = canonicalize_slippi_sample_last(peppi_bytes.read_slippi_bytes_sample(rb, 0, False))
            frames = sample["frame"]
            if start_frame is None:
                start_frame = int(frames[0])
            if end_frame is None:
                end_frame = int(frames[-1])
        except Exception:
            pass
    if start_frame is None:
        start_frame = -123
    if end_frame is None:
        end_frame = 999999

    playback_txt = _write_playback_txt(
        user_dir,
        replay=replay,
        start_frame=start_frame,
        end_frame=end_frame,
        dump_path=out_bin,
    )

    dolphin = Path(args.dolphin)
    if not dolphin.exists():
        raise FileNotFoundError(dolphin)

    proc_args = [
        str(dolphin),
        "-e",
        str(Path(args.iso).resolve()),
        "-u",
        str(user_dir.resolve()),
        "--slippi-input",
        str(playback_txt.resolve()),
    ]
    env = os.environ.copy()
    proc = subprocess.Popen(proc_args, env=env)

    t0 = time.monotonic()
    try:
        while time.monotonic() - t0 < args.timeout:
            if out_bin.exists():
                break
            if proc.poll() is not None:
                break
            time.sleep(0.5)
    finally:
        try:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=5.0)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass

    if not out_bin.exists():
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
