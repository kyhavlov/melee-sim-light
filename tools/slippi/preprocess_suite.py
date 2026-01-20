from __future__ import annotations

import argparse
from pathlib import Path

from tools.slippi.make_dataset_from_slp import write_dataset_from_slp
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--suite", required=True, help="Path to suite JSON (e.g. replays/suites/...)")
    ap.add_argument(
        "--datasets-dir",
        default="datasets",
        help="Directory under repo root to store preprocessed datasets (gitignored)",
    )
    ap.add_argument("--force", action="store_true", help="Rebuild even if dataset is up-to-date")
    args = ap.parse_args()

    root = repo_root()
    suite_path = (root / args.suite).resolve()
    suite = load_suite(suite_path)

    built = 0
    skipped = 0
    missing = 0

    for entry in suite.replays:
        slp_path = (root / entry.replay).resolve()
        if not slp_path.exists():
            print(f"missing replay: {entry.replay}")
            missing += 1
            continue

        out_path = dataset_path_for_suite_replay(
            suite_name=suite.name,
            replay_rel_path=entry.replay,
            datasets_dir=args.datasets_dir,
        )
        out_path.parent.mkdir(parents=True, exist_ok=True)

        if not args.force and out_path.exists():
            if out_path.stat().st_mtime >= slp_path.stat().st_mtime:
                skipped += 1
                continue

        ports = list(entry.ports)
        write_dataset_from_slp(
            slp_path=str(slp_path),
            out_path=str(out_path),
            ports=ports,
            ucf_enabled=bool(suite.ucf_enabled),
            ucf_cardinals_1_0_enabled=bool(suite.ucf_cardinals_1_0_enabled),
        )
        built += 1

    print(f"suite: {suite.name}")
    print(f"built: {built}  skipped: {skipped}  missing: {missing}")
    if missing:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
