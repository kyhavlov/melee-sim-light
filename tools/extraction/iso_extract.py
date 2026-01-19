from __future__ import annotations

import argparse
from pathlib import Path

from melee_sim.iso import extract_file, find_files, list_files


def main() -> None:
    ap = argparse.ArgumentParser(description="Extract files from a GameCube ISO (FST-based).")
    ap.add_argument("--iso", type=Path, required=True, help="path to SSBM.iso")
    ap.add_argument("--list", action="store_true", help="list files and exit")
    ap.add_argument("--glob", type=str, default=None, help="glob pattern over ISO paths (e.g. '*GrIz.dat')")
    ap.add_argument("--out-dir", type=Path, default=Path("_iso"), help="output directory")
    ap.add_argument("--limit", type=int, default=0, help="limit number of matches (0 = no limit)")
    args = ap.parse_args()

    files = list_files(args.iso)
    if args.list:
        for x in files:
            print(f"{x.path}\t{hex(x.offset)}\t{x.size}")
        return

    if not args.glob:
        raise SystemExit("--glob is required unless --list is used")

    matches = find_files(files, args.glob)
    if args.limit and args.limit > 0:
        matches = matches[: args.limit]
    if not matches:
        raise SystemExit(f"no matches for {args.glob!r}")

    for m in matches:
        out = args.out_dir / m.path
        extract_file(args.iso, m, out)
        print(f"wrote {out} ({m.size} bytes)")


if __name__ == "__main__":
    main()
