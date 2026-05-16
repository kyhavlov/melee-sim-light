from __future__ import annotations

import argparse
from pathlib import Path

from tools.slippi.slpz import compress_path
from tools.slippi.suite_io import load_suite, repo_root


def _suite_paths(root: Path, args) -> list[Path]:
    if args.all_validation_suites:
        return sorted((root / "replays" / "suites").glob("*.json"))
    if not args.suite:
        raise SystemExit("pass --suite or --all-validation-suites")
    return [(root / suite).resolve() for suite in args.suite]


def _rewrite_suite_replays(path: Path, replacements: dict[str, str]) -> bool:
    if not replacements:
        return False
    text = path.read_text(encoding="utf-8")
    updated = text
    for old, new in replacements.items():
        updated = updated.replace(old, new)
    if updated != text:
        path.write_text(updated, encoding="utf-8")
        return True
    return False


def main() -> None:
    ap = argparse.ArgumentParser(description="Convert suite replay storage between .slp and .slpz.")
    ap.add_argument("--suite", action="append", default=[], help="Suite JSON path. May be repeated.")
    ap.add_argument(
        "--all-validation-suites",
        action="store_true",
        help="Convert every replay referenced by replays/suites/*.json.",
    )
    ap.add_argument("--rewrite-suites", action="store_true", help="Rewrite suite replay paths to .slpz.")
    ap.add_argument("--delete-source", action="store_true", help="Delete .slp files after successful compression.")
    ap.add_argument("--force", action="store_true", help="Overwrite existing .slpz files.")
    ap.add_argument("--level", type=int, default=3, help="zstd compression level.")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = repo_root()
    suite_paths = _suite_paths(root, args)
    replay_to_suites: dict[str, list[Path]] = {}
    for suite_path in suite_paths:
        suite = load_suite(suite_path)
        for entry in suite.replays:
            if Path(entry.replay).suffix != ".slp":
                continue
            if args.all_validation_suites and not entry.replay.startswith("replays/validation/"):
                continue
            replay_to_suites.setdefault(entry.replay, []).append(suite_path)

    converted: dict[str, str] = {}
    skipped = 0
    missing = 0
    for replay_rel in sorted(replay_to_suites):
        src = root / replay_rel
        dst = src.with_suffix(".slpz")
        compressed_rel = dst.relative_to(root).as_posix()
        if not src.exists():
            if dst.exists():
                converted[replay_rel] = compressed_rel
                skipped += 1
                continue
            print(f"missing: {replay_rel}")
            missing += 1
            continue
        if args.dry_run:
            print(f"would compress: {replay_rel} -> {compressed_rel}")
            converted[replay_rel] = compressed_rel
            continue
        compress_path(src, dst, force=bool(args.force), level=int(args.level))
        if bool(args.delete_source):
            src.unlink()
        converted[replay_rel] = compressed_rel
        print(f"compressed: {replay_rel} -> {compressed_rel}")

    rewritten = 0
    if bool(args.rewrite_suites):
        for suite_path in suite_paths:
            if args.dry_run:
                if any(old in suite_path.read_text(encoding="utf-8") for old in converted):
                    print(f"would rewrite: {suite_path.relative_to(root)}")
                    rewritten += 1
                continue
            if _rewrite_suite_replays(suite_path, converted):
                print(f"rewrote: {suite_path.relative_to(root)}")
                rewritten += 1

    print(f"converted: {len(converted)}  skipped: {skipped}  missing: {missing}  rewritten_suites: {rewritten}")
    if missing:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
