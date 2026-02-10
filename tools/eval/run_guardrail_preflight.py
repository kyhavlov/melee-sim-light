from __future__ import annotations

"""Fast preflight guardrail runner.

Runs a lock-pack integration test plus guardrail diffs against committed baseline snapshots:
- seed==ref locate diff (new rows must stay 0)
- float top-key diff for pos_x/pos_y (new/gone keys must stay 0)
"""

import argparse
import csv
import json
import subprocess
import sys
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path

from tools.eval.diff_locate import diff_locate_rows
from tools.eval.locate_discrete_mismatches import MismatchRow, _iter_dataset_mismatches
from tools.eval.locate_tsv import parse_locate_tsv
from tools.eval.top_float_offenders import collect_suite_top_float_offenders
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root

GUARDRAIL_FIELDS: tuple[str, ...] = (
    "action_id",
    "hitlag",
    "hitstun",
    "state_flags",
    "instance_id",
    "on_ground",
    "ground_id",
)

FLOAT_FIELDS: tuple[str, ...] = ("pos_x", "pos_y")


@dataclass(frozen=True)
class FieldGuardrailResult:
    field: str
    before_rows: int
    after_rows: int
    new_rows: int
    gone_rows: int
    unchanged: int


@dataclass(frozen=True)
class FloatKeyGuardrailResult:
    field: str
    before_keys: int
    after_keys: int
    new_keys: int
    gone_keys: int


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


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


def _collect_suite_seedref_locate_rows(
    *,
    suite: Path,
    datasets_dir: str,
    chunk: int,
) -> dict[str, list[MismatchRow]]:
    root = repo_root()
    suite_obj = load_suite((root / suite).resolve())

    out: dict[str, list[MismatchRow]] = {f: [] for f in GUARDRAIL_FIELDS}
    for entry in suite_obj.replays:
        ds = dataset_path_for_suite_replay(
            suite_name=suite_obj.name,
            replay_rel_path=entry.replay,
            datasets_dir=datasets_dir,
        )
        rows = _iter_dataset_mismatches(
            dataset_path=ds,
            fields=GUARDRAIL_FIELDS,
            chunk=chunk,
            ucf_enabled=bool(suite_obj.ucf_enabled),
            ucf_cardinals_1_0_enabled=bool(suite_obj.ucf_cardinals_1_0_enabled),
            max_rows=None,
            only_seed_equals_ref=True,
        )

        for row in rows:
            base_field = row.field.split("[", 1)[0]
            if base_field not in out:
                continue
            out[base_field].append(
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
        out[field].sort(key=lambda r: (r.dataset, r.record, r.p, r.field, r.seed_frame, r.ref_frame))
    return out


def _run_lock_pack() -> None:
    cmd = [sys.executable, "-m", "pytest", "-m", "integration", "tests/test_hard_row_lock_pack.py", "-q"]
    print("[preflight] running lock pack:", " ".join(cmd))
    subprocess.run(cmd, check=True)


def _read_float_keyset(path: Path) -> dict[str, set[tuple[str, int, int]]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    out: dict[str, set[tuple[str, int, int]]] = {}
    for field in FLOAT_FIELDS:
        rows = payload["keys"][field]
        out[field] = {
            (str(r["dataset"]), int(r["record"]), int(r["p"]))
            for r in rows
        }
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description="Run lock-pack + guardrail diff preflight checks.")
    ap.add_argument("--suite", type=Path, default=Path("replays/suites/fox_falco_fd_ucf084_recent.json"))
    ap.add_argument("--datasets-dir", default="datasets")
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--float-top", type=int, default=20)
    ap.add_argument(
        "--baseline-dir",
        type=Path,
        default=Path("tests/fixtures/guardrails/current_main"),
        help="Directory containing seedref_<field>.tsv and float_top20_posx_posy.json",
    )
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path("reports/triage") / f"{_timestamp()}_guardrail_preflight",
    )
    ap.add_argument("--skip-lock-pack", action="store_true")
    args = ap.parse_args()

    root = repo_root()
    out_dir = (root / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    baseline_dir = (root / args.baseline_dir).resolve()

    if not args.skip_lock_pack:
        _run_lock_pack()

    print("[preflight] collecting seed==ref locate rows")
    current_rows = _collect_suite_seedref_locate_rows(
        suite=args.suite,
        datasets_dir=str(args.datasets_dir),
        chunk=int(args.chunk),
    )

    field_results: list[FieldGuardrailResult] = []
    for field in GUARDRAIL_FIELDS:
        current_tsv = out_dir / f"current_seedref_{field}.tsv"
        _write_locate_tsv(current_tsv, current_rows[field])

        baseline_tsv = baseline_dir / f"seedref_{field}.tsv"
        if not baseline_tsv.exists():
            raise SystemExit(f"missing baseline TSV: {baseline_tsv}")

        before_rows = parse_locate_tsv(baseline_tsv)
        after_rows = parse_locate_tsv(current_tsv)
        report = diff_locate_rows(
            before_rows=before_rows,
            after_rows=after_rows,
            key_fields=("dataset", "record", "p", "field"),
        )
        r = FieldGuardrailResult(
            field=field,
            before_rows=int(report.before_count),
            after_rows=int(report.after_count),
            new_rows=len(report.new_rows),
            gone_rows=len(report.gone_rows),
            unchanged=int(report.unchanged),
        )
        field_results.append(r)
        print(
            f"[preflight] seed==ref {field}: before={r.before_rows} after={r.after_rows} "
            f"new={r.new_rows} gone={r.gone_rows}"
        )

    print("[preflight] collecting float top offenders")
    float_payload = collect_suite_top_float_offenders(
        suite=args.suite,
        datasets_dir=str(args.datasets_dir),
        fields=FLOAT_FIELDS,
        chunk=int(args.chunk),
        top=int(args.float_top),
    )
    current_float_json = out_dir / "current_float_top20_posx_posy.json"
    current_float_json.write_text(json.dumps(float_payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    baseline_float_json = baseline_dir / "float_top20_posx_posy.json"
    if not baseline_float_json.exists():
        raise SystemExit(f"missing baseline float snapshot: {baseline_float_json}")

    before_keyset = _read_float_keyset(baseline_float_json)
    after_keyset = _read_float_keyset(current_float_json)

    float_results: list[FloatKeyGuardrailResult] = []
    for field in FLOAT_FIELDS:
        new_keys = sorted(after_keyset[field] - before_keyset[field])
        gone_keys = sorted(before_keyset[field] - after_keyset[field])
        r = FloatKeyGuardrailResult(
            field=field,
            before_keys=len(before_keyset[field]),
            after_keys=len(after_keyset[field]),
            new_keys=len(new_keys),
            gone_keys=len(gone_keys),
        )
        float_results.append(r)
        print(
            f"[preflight] float keys {field}: before={r.before_keys} after={r.after_keys} "
            f"new={r.new_keys} gone={r.gone_keys}"
        )

    failures: list[str] = []
    for r in field_results:
        if r.new_rows != 0:
            failures.append(f"seed==ref {r.field} has new rows: {r.new_rows}")
    for r in float_results:
        if r.new_keys != 0 or r.gone_keys != 0:
            failures.append(
                f"float keyset drift on {r.field}: new={r.new_keys} gone={r.gone_keys}"
            )

    summary = {
        "suite": str(args.suite.as_posix()),
        "datasets_dir": str(args.datasets_dir),
        "chunk": int(args.chunk),
        "baseline_dir": str(_dataset_rel(root, baseline_dir)),
        "out_dir": str(_dataset_rel(root, out_dir)),
        "seed_ref": [asdict(r) for r in field_results],
        "float_keys": [asdict(r) for r in float_results],
        "failures": failures,
    }
    summary_path = out_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if failures:
        print("[preflight] FAILED")
        for f in failures:
            print(" -", f)
        raise SystemExit(1)

    print("[preflight] PASS")
    print(f"[preflight] summary: {summary_path}")


if __name__ == "__main__":
    main()
