from __future__ import annotations

import argparse
from pathlib import Path

from melee_sim.raw_data import raw_data_dir, validate_raw_data_root


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate extracted raw SSBM game data.")
    root = parser.add_mutually_exclusive_group()
    root.add_argument(
        "--data-dir",
        type=Path,
        default=None,
        help="MSL data root; defaults to MSL_DATA_DIR or ./data",
    )
    root.add_argument(
        "--raw-dir",
        type=Path,
        default=None,
        help="raw DAT directory directly",
    )
    parser.add_argument(
        "--size-only",
        action="store_true",
        help="validate manifest and file sizes without recomputing file hashes",
    )
    args = parser.parse_args(argv)
    selected = (
        args.raw_dir.expanduser().resolve()
        if args.raw_dir is not None
        else raw_data_dir(args.data_dir)
    )
    validate_raw_data_root(selected, verify_hashes=not args.size_only)
    print(f"raw game data valid: {selected}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
