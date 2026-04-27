from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np

from tools.slippi.make_dataset_from_slp import write_dataset_from_slp
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root
from tools.eval.dataset import HEADER_DTYPE, MAGIC, SAMPLE_DTYPE, SEED_DTYPE


_CACHE_VERSION = 2
_SOURCE_INPUTS = (
    "tools/eval/dataset.py",
    "tools/slippi/make_dataset_from_slp.py",
    "tools/slippi/seed_history.py",
    "tools/slippi/combat_history.py",
)


def _hash_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _file_fingerprint(path: Path, *, hash_contents: bool, display_path: Path | None = None) -> dict[str, Any]:
    st = path.stat()
    out: dict[str, Any] = {
        "path": str(display_path if display_path is not None else path),
        "size": int(st.st_size),
        "mtime_ns": int(st.st_mtime_ns),
    }
    if hash_contents:
        out["sha256"] = _hash_file(path)
    return out


def _tree_fingerprint(root: Path, rel: str) -> list[dict[str, Any]]:
    base = root / rel
    if not base.exists():
        return []
    return [
        _file_fingerprint(path, hash_contents=True, display_path=path.relative_to(root))
        for path in sorted(base.rglob("*"))
        if path.is_file()
    ]


def _dataset_cache_meta_path(out_path: Path) -> Path:
    return out_path.with_name(out_path.name + ".meta.json")


def _dataset_record_size(path: Path) -> int | None:
    try:
        with path.open("rb") as f:
            header_bytes = f.read(HEADER_DTYPE.itemsize)
        if len(header_bytes) != HEADER_DTYPE.itemsize:
            return None
        header = np.frombuffer(header_bytes, dtype=HEADER_DTYPE, count=1)[0]
        if bytes(header["magic"]) != MAGIC:
            return None
        return int(header["record_size"])
    except Exception:
        return None


def _cache_signature(
    *,
    root: Path,
    suite_path: Path,
    suite_name: str,
    replay_rel: str,
    slp_path: Path,
    ports: list[int],
    ucf_enabled: bool,
    ucf_cardinals_1_0_enabled: bool,
) -> dict[str, Any]:
    source_inputs = [
        _file_fingerprint(root / rel, hash_contents=True, display_path=Path(rel))
        for rel in _SOURCE_INPUTS
        if (root / rel).exists()
    ]
    return {
        "cache_version": _CACHE_VERSION,
        "suite": str(suite_path.relative_to(root)),
        "suite_name": suite_name,
        "replay": replay_rel,
        "replay_file": _file_fingerprint(slp_path, hash_contents=False, display_path=slp_path.relative_to(root)),
        "ports": [int(p) for p in ports],
        "ucf_enabled": bool(ucf_enabled),
        "ucf_cardinals_1_0_enabled": bool(ucf_cardinals_1_0_enabled),
        "seed_dtype_itemsize": int(SEED_DTYPE.itemsize),
        "sample_dtype_itemsize": int(SAMPLE_DTYPE.itemsize),
        "sample_dtype_descr": repr(SAMPLE_DTYPE.descr),
        "source_inputs": source_inputs,
        "data_inputs": _tree_fingerprint(root, "data"),
    }


def _signature_matches(meta_path: Path, signature: dict[str, Any]) -> bool:
    try:
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
    except Exception:
        return False
    return meta.get("signature") == signature


def _write_dataset_cache_meta(meta_path: Path, signature: dict[str, Any], *, rebuilt: bool) -> None:
    meta_path.write_text(
        json.dumps(
            {
                "cache_version": _CACHE_VERSION,
                "rebuilt": bool(rebuilt),
                "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                "signature": signature,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


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
    stale = 0
    legacy_skipped = 0

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

        ports = list(entry.ports)
        signature = _cache_signature(
            root=root,
            suite_path=suite_path,
            suite_name=suite.name,
            replay_rel=entry.replay,
            slp_path=slp_path,
            ports=ports,
            ucf_enabled=bool(suite.ucf_enabled),
            ucf_cardinals_1_0_enabled=bool(suite.ucf_cardinals_1_0_enabled),
        )
        dataset_meta_path = _dataset_cache_meta_path(out_path)
        if not args.force and out_path.exists():
            if _signature_matches(dataset_meta_path, signature):
                skipped += 1
                continue
            if (
                not dataset_meta_path.exists()
                and out_path.stat().st_mtime >= slp_path.stat().st_mtime
                and _dataset_record_size(out_path) == SAMPLE_DTYPE.itemsize
            ):
                # Legacy cache entry from before per-dataset signatures. Keep it, but stamp it so
                # future schema/data/tooling changes can invalidate it precisely.
                _write_dataset_cache_meta(dataset_meta_path, signature, rebuilt=False)
                skipped += 1
                legacy_skipped += 1
                continue
            stale += 1

        write_dataset_from_slp(
            slp_path=str(slp_path),
            out_path=str(out_path),
            ports=ports,
            ucf_enabled=bool(suite.ucf_enabled),
            ucf_cardinals_1_0_enabled=bool(suite.ucf_cardinals_1_0_enabled),
        )
        _write_dataset_cache_meta(dataset_meta_path, signature, rebuilt=True)
        built += 1

    # Write a persistent stamp so validation reports can prove whether `--force` was used.
    #
    # This matters whenever the dataset schema changes (record_size mismatch), because `make validate`
    # does not implicitly rebuild cached datasets.
    meta_path = (root / args.datasets_dir / suite.name / ".preprocess_meta.json").resolve()
    meta_path.parent.mkdir(parents=True, exist_ok=True)
    meta = {
        "suite": str(args.suite),
        "suite_name": suite.name,
        "datasets_dir": str(args.datasets_dir),
        "force_used": bool(args.force),
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "built": int(built),
        "skipped": int(skipped),
        "legacy_skipped": int(legacy_skipped),
        "stale": int(stale),
        "missing": int(missing),
        "cache_version": _CACHE_VERSION,
        "seed_dtype_itemsize": int(SEED_DTYPE.itemsize),
        "sample_dtype_itemsize": int(SAMPLE_DTYPE.itemsize),
    }
    meta_path.write_text(json.dumps(meta, indent=2, sort_keys=True) + "\n")

    print(f"suite: {suite.name}")
    print(
        f"built: {built}  skipped: {skipped}  legacy_skipped: {legacy_skipped}  "
        f"stale: {stale}  missing: {missing}"
    )
    if missing:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
