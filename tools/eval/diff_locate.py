from __future__ import annotations

"""Diff two legacy locate TSV outputs."""

import argparse
import json
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from tools.eval.locate_tsv import LocateRow, filter_rows, parse_key_fields, parse_locate_tsv, row_key


@dataclass(frozen=True)
class LocateDiff:
    before_count: int
    after_count: int
    duplicate_before: int
    duplicate_after: int
    unchanged: int
    new_rows: tuple[LocateRow, ...]
    gone_rows: tuple[LocateRow, ...]
    delta_seed_ref_out: Counter[tuple[int, int, int]]
    delta_ref_out: Counter[tuple[int, int]]


def _index_by_key(rows: list[LocateRow], key_fields: tuple[str, ...]) -> tuple[dict[tuple[object, ...], LocateRow], int]:
    out: dict[tuple[object, ...], LocateRow] = {}
    dup = 0
    for row in rows:
        key = row_key(row, key_fields)
        if key in out:
            dup += 1
            continue
        out[key] = row
    return out, dup


def _transition_counters(rows: list[LocateRow]) -> tuple[Counter[tuple[int, int, int]], Counter[tuple[int, int]]]:
    sro: Counter[tuple[int, int, int]] = Counter()
    ro: Counter[tuple[int, int]] = Counter()
    for row in rows:
        sro[(row.seed, row.ref, row.out)] += 1
        ro[(row.ref, row.out)] += 1
    return sro, ro


def diff_locate_rows(
    *,
    before_rows: list[LocateRow],
    after_rows: list[LocateRow],
    key_fields: tuple[str, ...],
) -> LocateDiff:
    before_idx, dup_before = _index_by_key(before_rows, key_fields)
    after_idx, dup_after = _index_by_key(after_rows, key_fields)

    before_keys = set(before_idx.keys())
    after_keys = set(after_idx.keys())

    new_keys = sorted(after_keys - before_keys)
    gone_keys = sorted(before_keys - after_keys)
    unchanged = len(before_keys & after_keys)

    new_rows = tuple(after_idx[k] for k in new_keys)
    gone_rows = tuple(before_idx[k] for k in gone_keys)

    before_unique = list(before_idx.values())
    after_unique = list(after_idx.values())
    before_sro, before_ro = _transition_counters(before_unique)
    after_sro, after_ro = _transition_counters(after_unique)

    all_sro = set(before_sro.keys()) | set(after_sro.keys())
    all_ro = set(before_ro.keys()) | set(after_ro.keys())

    delta_sro: Counter[tuple[int, int, int]] = Counter()
    for key in all_sro:
        delta = after_sro[key] - before_sro[key]
        if delta != 0:
            delta_sro[key] = delta

    delta_ro: Counter[tuple[int, int]] = Counter()
    for key in all_ro:
        delta = after_ro[key] - before_ro[key]
        if delta != 0:
            delta_ro[key] = delta

    return LocateDiff(
        before_count=len(before_idx),
        after_count=len(after_idx),
        duplicate_before=dup_before,
        duplicate_after=dup_after,
        unchanged=unchanged,
        new_rows=new_rows,
        gone_rows=gone_rows,
        delta_seed_ref_out=delta_sro,
        delta_ref_out=delta_ro,
    )


def _print_rows(title: str, rows: tuple[LocateRow, ...], limit: int) -> None:
    print(title)
    if not rows:
        print("  (none)")
        return
    for row in rows[:limit]:
        print(
            f"  {Path(row.dataset).name} rec={row.record} p={row.p} field={row.field} "
            f"seed/ref/out={row.seed}/{row.ref}/{row.out} frames={row.seed_frame}->{row.ref_frame}"
        )
    if len(rows) > limit:
        print(f"  ... ({len(rows) - limit} more)")


def _print_delta(
    title: str,
    items: list[tuple[tuple[int, ...], int]],
    *,
    top: int,
    kind: str,
) -> None:
    print(title)
    if not items:
        print("  (none)")
        return
    for key, delta in items[:top]:
        if kind == "sro":
            seed, ref, out = key
            print(f"  delta={delta:+d} seed/ref/out={seed}/{ref}/{out}")
        else:
            ref, out = key
            print(f"  delta={delta:+d} ref/out={ref}->{out}")


def main() -> None:
    epilog = (
        "Example:\n"
        "  uv run python -m tools.eval.diff_locate --before reports/triage/before.tsv "
        "--after reports/triage/after.tsv --key dataset,record,p,field --show new,gone,delta\n\n"
        "Note: locate TSV columns are seed,out,ref; this tool reports seed,ref,out."
    )
    ap = argparse.ArgumentParser(
        description="Diff two locate TSV outputs.",
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=epilog,
    )
    ap.add_argument("--before", type=Path, required=True, help="Before TSV path.")
    ap.add_argument("--after", type=Path, required=True, help="After TSV path.")
    ap.add_argument("--key", default="dataset,record,p,field", help="Comma-separated key columns.")
    ap.add_argument(
        "--show",
        default="new,gone,delta",
        help="Comma-separated sections to show: new,gone,delta",
    )
    ap.add_argument("--filter", default="", help="Optional string filter (e.g. field=state_flags[2]).")
    ap.add_argument("--top", type=int, default=20, help="Top-N delta transitions to print.")
    ap.add_argument("--limit", type=int, default=20, help="Max row lines for new/gone sections.")
    ap.add_argument("--json-out", type=Path, default=None, help="Optional JSON summary output path.")
    args = ap.parse_args()

    try:
        key_fields = parse_key_fields(str(args.key))
    except ValueError as e:
        raise SystemExit(f"invalid --key: {e}") from e
    show = {s.strip() for s in str(args.show).split(",") if s.strip()}

    before_rows = filter_rows(parse_locate_tsv(args.before), str(args.filter))
    after_rows = filter_rows(parse_locate_tsv(args.after), str(args.filter))
    report = diff_locate_rows(before_rows=before_rows, after_rows=after_rows, key_fields=key_fields)

    print(f"key={','.join(key_fields)} filter={args.filter!r}")
    print(
        f"rows(before)={len(before_rows)} rows(after)={len(after_rows)} "
        f"dedup(before)={report.before_count} dedup(after)={report.after_count}"
    )
    print(
        f"new={len(report.new_rows)} gone={len(report.gone_rows)} unchanged={report.unchanged} "
        f"dup(before)={report.duplicate_before} dup(after)={report.duplicate_after}"
    )

    if "new" in show:
        _print_rows("== new rows ==", report.new_rows, int(args.limit))
    if "gone" in show:
        _print_rows("== gone rows ==", report.gone_rows, int(args.limit))
    if "delta" in show:
        inc_sro = sorted(
            ((k, v) for (k, v) in report.delta_seed_ref_out.items() if v > 0),
            key=lambda it: (-it[1], it[0]),
        )
        dec_sro = sorted(
            ((k, v) for (k, v) in report.delta_seed_ref_out.items() if v < 0),
            key=lambda it: (it[1], it[0]),
        )
        inc_ro = sorted(
            ((k, v) for (k, v) in report.delta_ref_out.items() if v > 0),
            key=lambda it: (-it[1], it[0]),
        )
        dec_ro = sorted(
            ((k, v) for (k, v) in report.delta_ref_out.items() if v < 0),
            key=lambda it: (it[1], it[0]),
        )
        _print_delta("== increased seed/ref/out ==", inc_sro, top=int(args.top), kind="sro")
        _print_delta("== decreased seed/ref/out ==", dec_sro, top=int(args.top), kind="sro")
        _print_delta("== increased ref->out ==", inc_ro, top=int(args.top), kind="ro")
        _print_delta("== decreased ref->out ==", dec_ro, top=int(args.top), kind="ro")

    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        payload: dict[str, Any] = {
            "before": str(args.before),
            "after": str(args.after),
            "key": list(key_fields),
            "filter": str(args.filter),
            "counts": {
                "rows_before": len(before_rows),
                "rows_after": len(after_rows),
                "dedup_before": report.before_count,
                "dedup_after": report.after_count,
                "new": len(report.new_rows),
                "gone": len(report.gone_rows),
                "unchanged": report.unchanged,
                "duplicate_before": report.duplicate_before,
                "duplicate_after": report.duplicate_after,
            },
            "top": {
                "increased_seed_ref_out": [
                    {"seed": k[0], "ref": k[1], "out": k[2], "delta": int(v)}
                    for k, v in sorted(
                        ((k, v) for (k, v) in report.delta_seed_ref_out.items() if v > 0),
                        key=lambda it: (-it[1], it[0]),
                    )[: int(args.top)]
                ],
                "decreased_seed_ref_out": [
                    {"seed": k[0], "ref": k[1], "out": k[2], "delta": int(v)}
                    for k, v in sorted(
                        ((k, v) for (k, v) in report.delta_seed_ref_out.items() if v < 0),
                        key=lambda it: (it[1], it[0]),
                    )[: int(args.top)]
                ],
                "increased_ref_out": [
                    {"ref": k[0], "out": k[1], "delta": int(v)}
                    for k, v in sorted(
                        ((k, v) for (k, v) in report.delta_ref_out.items() if v > 0),
                        key=lambda it: (-it[1], it[0]),
                    )[: int(args.top)]
                ],
                "decreased_ref_out": [
                    {"ref": k[0], "out": k[1], "delta": int(v)}
                    for k, v in sorted(
                        ((k, v) for (k, v) in report.delta_ref_out.items() if v < 0),
                        key=lambda it: (it[1], it[0]),
                    )[: int(args.top)]
                ],
            },
        }
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
