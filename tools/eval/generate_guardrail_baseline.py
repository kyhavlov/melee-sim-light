from __future__ import annotations

"""Generate guardrail baseline snapshots for preflight checks."""

import argparse
import csv
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

from tools.eval.locate_discrete_mismatches import MismatchRow, _iter_dataset_mismatches
from tools.eval.run_guardrail_preflight import FLOAT_FIELDS, GUARDRAIL_FIELDS
from tools.eval.top_float_offenders import collect_suite_top_float_offenders
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


def _git_sha(root: Path) -> str:
    try:
        proc = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=str(root),
            check=True,
            capture_output=True,
            text=True,
        )
    except (subprocess.SubprocessError, OSError):
        return "unknown"
    return proc.stdout.strip() or "unknown"


def _dataset_rel(root: Path, p: Path) -> str:
    rp = p.resolve()
    try:
        return str(rp.relative_to(root).as_posix())
    except ValueError:
        return str(rp.as_posix())


def _write_locate_tsv(path: Path, rows: list[MismatchRow]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["dataset", "record", "seed_frame", "ref_frame", "p", "field", "seed", "out", "ref"])
        for r in rows:
            w.writerow(
                [
                    r.dataset,
                    int(r.record),
                    int(r.seed_frame),
                    int(r.ref_frame),
                    int(r.p),
                    r.field,
                    int(r.seed),
                    int(r.out),
                    int(r.ref),
                ]
            )


def main() -> None:
    ap = argparse.ArgumentParser(description="Generate guardrail baseline fixtures.")
    ap.add_argument("--suite", type=Path, default=Path("replays/suites/fox_falco_fd_ucf084_recent.json"))
    ap.add_argument("--datasets-dir", default="datasets")
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--float-top", type=int, default=20)
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path("tests/fixtures/guardrails/current_main"),
    )
    args = ap.parse_args()

    root = repo_root()
    out_dir = (root / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    suite_obj = load_suite((root / args.suite).resolve())
    rows_by_base: dict[str, list[MismatchRow]] = {f: [] for f in GUARDRAIL_FIELDS}

    for entry in suite_obj.replays:
        ds = dataset_path_for_suite_replay(
            suite_name=suite_obj.name,
            replay_rel_path=entry.replay,
            datasets_dir=str(args.datasets_dir),
        )
        rows = _iter_dataset_mismatches(
            dataset_path=ds,
            fields=GUARDRAIL_FIELDS,
            chunk=int(args.chunk),
            ucf_enabled=bool(suite_obj.ucf_enabled),
            ucf_cardinals_1_0_enabled=bool(suite_obj.ucf_cardinals_1_0_enabled),
            max_rows=None,
            only_seed_equals_ref=True,
        )
        for row in rows:
            base = row.field.split("[", 1)[0]
            rows_by_base[base].append(
                MismatchRow(
                    dataset=_dataset_rel(root, Path(row.dataset)),
                    record=row.record,
                    seed_frame=row.seed_frame,
                    ref_frame=row.ref_frame,
                    p=row.p,
                    field=row.field,
                    seed=row.seed,
                    out=row.out,
                    ref=row.ref,
                )
            )

    for field in GUARDRAIL_FIELDS:
        rows = rows_by_base[field]
        rows.sort(key=lambda r: (r.dataset, r.record, r.p, r.field, r.seed_frame, r.ref_frame))
        path = out_dir / f"seedref_{field}.tsv"
        _write_locate_tsv(path, rows)
        print(f"wrote {path} rows={len(rows)}")

    float_payload = collect_suite_top_float_offenders(
        suite=args.suite,
        datasets_dir=str(args.datasets_dir),
        fields=FLOAT_FIELDS,
        chunk=int(args.chunk),
        top=int(args.float_top),
    )
    float_path = out_dir / "float_top20_posx_posy.json"
    float_path.write_text(json.dumps(float_payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {float_path}")

    meta = {
        "suite_name": suite_obj.name,
        "suite_path": str(args.suite.as_posix()),
        "datasets_dir": str(args.datasets_dir),
        "chunk": int(args.chunk),
        "float_top": int(args.float_top),
        "seed_ref_fields": list(GUARDRAIL_FIELDS),
        "float_fields": list(FLOAT_FIELDS),
        "git_sha": _git_sha(root),
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
    }
    meta_path = out_dir / "meta.json"
    meta_path.write_text(json.dumps(meta, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {meta_path}")


if __name__ == "__main__":
    main()
