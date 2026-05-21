from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np

from tools.slippi.make_dataset_from_slp import write_dataset_from_slp
from tools.slippi.slpz import resolve_replay_path
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root
from tools.eval.dataset import HEADER_DTYPE, MAGIC, SAMPLE_DTYPE, SEED_DTYPE


# v19 invalidates datasets generated before FoD hidden-return scheduler timer seed lanes.
# v18 invalidates same-record-size FoD grounded KneeBend severe-airborne DamageFlyRoll seed-lane
# reconstruction.
# v17 invalidates same-record-size datasets generated before sustained airborne DamageFly rows
# reconstructed floor_sweep_prev_pos from callback-visible CollData.cur_pos instead of the older
# public replay row before the seed.
# Record-size checks alone cannot detect this semantic.
_CACHE_VERSION = 19
_SOURCE_INPUT_PY_TREES = ("tools/slippi",)
_SOURCE_INPUT_FILES = ("tools/eval/dataset.py", "bindings/msl_preprocess_native.c")


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


def _python_tree_fingerprint(root: Path, rel: str) -> list[dict[str, Any]]:
    base = root / rel
    if not base.exists():
        return []
    return [
        _file_fingerprint(path, hash_contents=True, display_path=path.relative_to(root))
        for path in sorted(base.rglob("*.py"))
        if path.is_file()
    ]


def _source_input_fingerprints(root: Path) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for rel in _SOURCE_INPUT_FILES:
        path = root / rel
        if path.exists():
            out.append(_file_fingerprint(path, hash_contents=True, display_path=Path(rel)))
    for rel in _SOURCE_INPUT_PY_TREES:
        out.extend(_python_tree_fingerprint(root, rel))
    return sorted(out, key=lambda item: str(item["path"]))


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
    source_inputs: list[dict[str, Any]] | None = None,
    data_inputs: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    if source_inputs is None:
        source_inputs = _source_input_fingerprints(root)
    if data_inputs is None:
        data_inputs = _tree_fingerprint(root, "data")
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
        "data_inputs": data_inputs,
    }


def _build_dataset_task(task: dict[str, Any]) -> str:
    write_dataset_from_slp(
        slp_path=str(task["slp_path"]),
        out_path=str(task["out_path"]),
        ports=[int(p) for p in task["ports"]],
        ucf_enabled=bool(task["ucf_enabled"]),
        ucf_cardinals_1_0_enabled=bool(task["ucf_cardinals_1_0_enabled"]),
    )
    _write_dataset_cache_meta(Path(str(task["dataset_meta_path"])), dict(task["signature"]), rebuilt=True)
    return str(task["out_path"])


def _signature_matches(meta_path: Path, signature: dict[str, Any]) -> bool:
    try:
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
    except Exception:
        return False
    return meta.get("signature") == signature


def _dataset_cache_action(
    *,
    force: bool,
    out_path: Path,
    slp_path: Path,
    dataset_meta_path: Path,
    signature: dict[str, Any],
    trust_legacy_cache: bool,
) -> str:
    if force or not out_path.exists():
        return "rebuild"
    if _signature_matches(dataset_meta_path, signature):
        return "skip"
    if (
        trust_legacy_cache
        and not dataset_meta_path.exists()
        and out_path.stat().st_mtime >= slp_path.stat().st_mtime
        and _dataset_record_size(out_path) == SAMPLE_DTYPE.itemsize
    ):
        return "trust_legacy"
    return "rebuild"


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


def _resolve_worker_count(requested: int, task_count: int) -> int:
    if int(requested) > 0:
        return max(1, int(requested))
    return max(1, min(32, int(os.cpu_count() or 1), int(task_count) if task_count else 1))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--suite", required=True, help="Path to suite JSON (e.g. replays/suites/...)")
    ap.add_argument(
        "--datasets-dir",
        default="datasets",
        help="Directory under repo root to store preprocessed datasets (gitignored)",
    )
    ap.add_argument("--force", action="store_true", help="Rebuild even if dataset is up-to-date")
    ap.add_argument(
        "--trust-legacy-cache",
        action="store_true",
        help=(
            "Opt in to trusting existing no-meta datasets when record_size and replay mtime look current. "
            "Default is to rebuild no-meta datasets so source/data signatures are guaranteed."
        ),
    )
    ap.add_argument(
        "--workers",
        type=int,
        default=0,
        help="Parallel rebuild workers for stale/forced datasets (0 = auto, capped at 32).",
    )
    args = ap.parse_args()

    root = repo_root()
    suite_path = (root / args.suite).resolve()
    suite = load_suite(suite_path)
    source_inputs = _source_input_fingerprints(root)
    data_inputs = _tree_fingerprint(root, "data")

    built = 0
    skipped = 0
    missing = 0
    stale = 0
    trusted_legacy = 0
    build_tasks: list[dict[str, Any]] = []

    for entry in suite.replays:
        slp_path = resolve_replay_path((root / entry.replay).resolve())
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
            source_inputs=source_inputs,
            data_inputs=data_inputs,
        )
        dataset_meta_path = _dataset_cache_meta_path(out_path)
        cache_action = _dataset_cache_action(
            force=bool(args.force),
            out_path=out_path,
            slp_path=slp_path,
            dataset_meta_path=dataset_meta_path,
            signature=signature,
            trust_legacy_cache=bool(args.trust_legacy_cache),
        )
        if cache_action == "skip":
            skipped += 1
            continue
        if cache_action == "trust_legacy":
            _write_dataset_cache_meta(dataset_meta_path, signature, rebuilt=False)
            skipped += 1
            trusted_legacy += 1
            continue
        if not args.force and out_path.exists():
            stale += 1

        build_tasks.append(
            {
                "slp_path": str(slp_path),
                "out_path": str(out_path),
                "ports": ports,
                "ucf_enabled": bool(suite.ucf_enabled),
                "ucf_cardinals_1_0_enabled": bool(suite.ucf_cardinals_1_0_enabled),
                "dataset_meta_path": str(dataset_meta_path),
                "signature": signature,
            }
        )

    workers = _resolve_worker_count(int(args.workers), len(build_tasks))
    if workers == 1 or len(build_tasks) <= 1:
        for task in build_tasks:
            _build_dataset_task(task)
            built += 1
    else:
        with concurrent.futures.ProcessPoolExecutor(max_workers=workers) as executor:
            futures = [executor.submit(_build_dataset_task, task) for task in build_tasks]
            for future in concurrent.futures.as_completed(futures):
                future.result()
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
        "trust_legacy_cache": bool(args.trust_legacy_cache),
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "built": int(built),
        "skipped": int(skipped),
        "trusted_legacy": int(trusted_legacy),
        "stale": int(stale),
        "missing": int(missing),
        "cache_version": _CACHE_VERSION,
        "workers": int(workers),
        "seed_dtype_itemsize": int(SEED_DTYPE.itemsize),
        "sample_dtype_itemsize": int(SAMPLE_DTYPE.itemsize),
    }
    meta_path.write_text(json.dumps(meta, indent=2, sort_keys=True) + "\n")

    print(f"suite: {suite.name}")
    print(
        f"built: {built}  skipped: {skipped}  trusted_legacy: {trusted_legacy}  "
        f"stale: {stale}  missing: {missing}"
    )
    if missing:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
