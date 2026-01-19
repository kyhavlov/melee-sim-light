#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def _load_suite(path: Path) -> list[dict]:
    raw = json.loads(path.read_text())
    if isinstance(raw, dict) and "replays" in raw and isinstance(raw["replays"], list):
        return raw["replays"]
    raise ValueError(f"invalid suite JSON: {path}")


def _char_to_paths(name: str) -> tuple[Path, Path]:
    key = name.strip().lower().replace(" ", "_")
    moves = Path("data/moves") / f"{key}.json"
    attrs = Path("data/characters") / f"{key}.json"
    return moves, attrs


def _run(cmd: list[str], env: dict[str, str] | None = None) -> int:
    proc = subprocess.run(cmd, env=env)
    return proc.returncode


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate engine dumps for a replay suite and run validator.")
    ap.add_argument("--suite", type=Path, default=Path("replays/suites/fox_falco_fd_ucf084_recent.json"))
    ap.add_argument("--out-dir", type=Path, default=Path("/tmp/engine_dumps"))
    ap.add_argument("--dolphin", type=Path, default=Path("refs/Ishiiruka/build/Binaries/dolphin-emu-nogui"))
    ap.add_argument("--iso", type=Path, default=Path("SSBM.iso"))
    ap.add_argument("--user-dir-base", type=Path, default=Path("/tmp/ish_playback_user"))
    ap.add_argument("--skip-existing", action="store_true")
    ap.add_argument("--dump-only", action="store_true")
    ap.add_argument("--validate-only", action="store_true")
    ap.add_argument("--ucf", type=int, default=1)
    args = ap.parse_args()

    items = _load_suite(args.suite)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PYTHONPATH"] = str(Path("src").resolve())

    results: list[dict] = []

    for it in items:
        replay = Path(it["replay"])
        if not replay.exists():
            print(f"missing replay: {replay}", file=sys.stderr)
            return 1
        stem = replay.stem
        dump_path = args.out_dir / f"{stem}.bin"
        user_dir = args.user_dir_base / stem

        if not args.validate_only:
            if args.skip_existing and dump_path.exists():
                print(f"skip dump (exists): {dump_path}")
            else:
                cmd = [
                    "uv",
                    "run",
                    "python",
                    "scripts/dolphin_engine_dump.py",
                    "--replay",
                    str(replay),
                    "--dolphin",
                    str(args.dolphin),
                    "--iso",
                    str(args.iso),
                    "--user-dir",
                    str(user_dir),
                    "--out-bin",
                    str(dump_path),
                ]
                print("dump:", " ".join(cmd))
                rc = _run(cmd, env=env)
                if rc != 0:
                    print(f"dump failed for {replay} (rc={rc})", file=sys.stderr)
                    return rc

        if args.dump_only:
            continue

        chars = it.get("characters", {})
        p1 = chars.get("1", "Fox")
        p2 = chars.get("2", "Falco")
        p1_moves, p1_attrs = _char_to_paths(p1)
        p2_moves, p2_attrs = _char_to_paths(p2)
        stage_path = Path("data/stages/final_destination.json")

        cmd = [
            "cargo",
            "run",
            "-p",
            "ssbm_sim",
            "--bin",
            "engine_dump_validate",
            "--",
            "--dump",
            str(dump_path),
            "--stage",
            str(stage_path),
            "--p1-moves",
            str(p1_moves),
            "--p2-moves",
            str(p2_moves),
            "--p1-attrs",
            str(p1_attrs),
            "--p2-attrs",
            str(p2_attrs),
            "--ucf",
            str(int(args.ucf)),
        ]
        print("validate:", " ".join(cmd))
        venv = env.copy()
        venv.setdefault("RUSTFLAGS", "-Awarnings")
        proc = subprocess.run(cmd, capture_output=True, text=True, env=venv)
        out = (proc.stdout or "") + (proc.stderr or "")
        status = "ok" if proc.returncode == 0 else "fail"
        mismatch = None
        if proc.returncode != 0:
            for line in out.splitlines():
                if "mismatch:" in line and "frame" in line:
                    mismatch = line.strip()
                    break
            if mismatch is None and out.strip():
                mismatch = out.strip().splitlines()[-1]
        results.append(
            {
                "replay": str(replay),
                "dump": str(dump_path),
                "status": status,
                "mismatch": mismatch,
            }
        )
        if proc.returncode != 0 and mismatch:
            print(mismatch)

    if results:
        print("\nSummary:")
        for r in results:
            if r["status"] == "ok":
                print(f"- {Path(r['replay']).name}: ok")
            else:
                print(f"- {Path(r['replay']).name}: fail ({r['mismatch']})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
