from __future__ import annotations

from collections import Counter
from typing import Any, Mapping


def validate_rollout_payload(payload: Mapping[str, Any], *, label: str = "input") -> None:
    if not isinstance(payload, Mapping):
        raise ValueError(
            f"{label}: invalid rollout JSON (expected object). "
            "Expected output from tools.eval.run_longest_rollout_streaks."
        )
    missing = [k for k in ("per_dataset", "fields") if k not in payload]
    if missing:
        raise ValueError(
            f"{label}: invalid rollout JSON; missing keys: {', '.join(missing)}. "
            "Expected output from tools.eval.run_longest_rollout_streaks."
        )
    if not isinstance(payload.get("per_dataset"), list):
        raise ValueError(
            f"{label}: invalid rollout JSON; 'per_dataset' must be a list. "
            "Expected output from tools.eval.run_longest_rollout_streaks."
        )
    if not isinstance(payload.get("fields"), list):
        raise ValueError(
            f"{label}: invalid rollout JSON; 'fields' must be a list. "
            "Expected output from tools.eval.run_longest_rollout_streaks."
        )


def _to_int_counter(raw: Mapping[Any, Any]) -> Counter[int]:
    out: Counter[int] = Counter()
    for k, v in raw.items():
        try:
            kk = int(k)
            vv = int(v)
        except Exception:
            continue
        if vv > 0:
            out[kk] += vv
    return out


def _quantile_from_hist(hist: Mapping[int, int], q: float) -> int:
    if not hist:
        return 0
    if q <= 0.0:
        return min(hist)
    if q >= 1.0:
        return max(hist)
    total = sum(int(v) for v in hist.values())
    if total <= 0:
        return 0
    # ceil(q * total) rank over sorted streak lengths.
    target = int(q * total)
    if target * 1.0 < q * total:
        target += 1
    if target <= 0:
        target = 1
    run = 0
    for streak_len in sorted(int(k) for k in hist.keys()):
        run += int(hist[streak_len])
        if run >= target:
            return streak_len
    return max(int(k) for k in hist.keys())


def _summarize_dataset_row(row: Mapping[str, Any]) -> dict[str, Any]:
    hist = _to_int_counter(row.get("streak_histogram", {}))
    first_mm = Counter({str(k): int(v) for k, v in dict(row.get("first_mismatch_field_counts", {})).items()})
    first_mm_seeded = Counter(
        {str(k): int(v) for k, v in dict(row.get("first_mismatch_field_counts_seeded", {})).items()}
    )
    return {
        "dataset": str(row.get("dataset", "")),
        "num_records": int(row.get("num_records", 0)),
        "max_records_used": int(row.get("max_records_used", 0)),
        "best_len": int(row.get("best_len", 0)),
        "best_start_record": int(row.get("best_start_record", 0)),
        "best_end_record_excl": int(row.get("best_end_record_excl", 0)),
        "total_streaks": int(sum(hist.values())),
        "median_streak_len": _quantile_from_hist(hist, 0.5),
        "p90_streak_len": _quantile_from_hist(hist, 0.9),
        "p95_streak_len": _quantile_from_hist(hist, 0.95),
        "max_streak_len": max(hist.keys()) if hist else 0,
        "streak_histogram": {int(k): int(v) for k, v in sorted(hist.items())},
        "first_mismatch_field_counts": dict(sorted(first_mm.items())),
        "first_mismatch_field_counts_seeded": dict(sorted(first_mm_seeded.items())),
        "first_mismatch_total": int(sum(first_mm.values())),
        "first_mismatch_seeded_total": int(sum(first_mm_seeded.values())),
    }


def summarize_rollout_payload(payload: Mapping[str, Any]) -> dict[str, Any]:
    validate_rollout_payload(payload, label="rollout")
    dataset_rows = [
        _summarize_dataset_row(row)
        for row in list(payload.get("per_dataset", []))
        if isinstance(row, Mapping)
    ]

    suite_hist: Counter[int] = Counter()
    suite_first_mm: Counter[str] = Counter()
    suite_first_mm_seeded: Counter[str] = Counter()
    for row in dataset_rows:
        suite_hist.update({int(k): int(v) for k, v in row["streak_histogram"].items()})
        suite_first_mm.update(Counter(row["first_mismatch_field_counts"]))
        suite_first_mm_seeded.update(Counter(row["first_mismatch_field_counts_seeded"]))

    suite = {
        "dataset_count": len(dataset_rows),
        "total_streaks": int(sum(suite_hist.values())),
        "median_streak_len": _quantile_from_hist(suite_hist, 0.5),
        "p90_streak_len": _quantile_from_hist(suite_hist, 0.9),
        "p95_streak_len": _quantile_from_hist(suite_hist, 0.95),
        "max_streak_len": max(suite_hist.keys()) if suite_hist else 0,
        "max_best_len": max((int(row["best_len"]) for row in dataset_rows), default=0),
        "first_mismatch_field_counts": dict(sorted(suite_first_mm.items())),
        "first_mismatch_field_counts_seeded": dict(sorted(suite_first_mm_seeded.items())),
        "first_mismatch_total": int(sum(suite_first_mm.values())),
        "first_mismatch_seeded_total": int(sum(suite_first_mm_seeded.values())),
    }

    return {
        "suite": str(payload.get("suite", "")),
        "suite_path": str(payload.get("suite_path", "")),
        "fields": list(payload.get("fields", [])),
        "datasets_dir": str(payload.get("datasets_dir", "")),
        "dataset_summaries": sorted(dataset_rows, key=lambda r: str(r["dataset"])),
        "suite_summary": suite,
    }


def diff_rollout_summaries(before: Mapping[str, Any], after: Mapping[str, Any]) -> dict[str, Any]:
    validate_rollout_payload(before, label="before")
    validate_rollout_payload(after, label="after")
    b = summarize_rollout_payload(before)
    a = summarize_rollout_payload(after)

    b_suite = dict(b["suite_summary"])
    a_suite = dict(a["suite_summary"])

    suite_delta = {
        "total_streaks": int(a_suite["total_streaks"]) - int(b_suite["total_streaks"]),
        "median_streak_len": int(a_suite["median_streak_len"]) - int(b_suite["median_streak_len"]),
        "p90_streak_len": int(a_suite["p90_streak_len"]) - int(b_suite["p90_streak_len"]),
        "p95_streak_len": int(a_suite["p95_streak_len"]) - int(b_suite["p95_streak_len"]),
        "max_streak_len": int(a_suite["max_streak_len"]) - int(b_suite["max_streak_len"]),
        "max_best_len": int(a_suite["max_best_len"]) - int(b_suite["max_best_len"]),
        "first_mismatch_total": int(a_suite["first_mismatch_total"]) - int(b_suite["first_mismatch_total"]),
        "first_mismatch_seeded_total": int(a_suite["first_mismatch_seeded_total"])
        - int(b_suite["first_mismatch_seeded_total"]),
    }

    b_field = Counter({str(k): int(v) for k, v in b_suite["first_mismatch_field_counts"].items()})
    a_field = Counter({str(k): int(v) for k, v in a_suite["first_mismatch_field_counts"].items()})
    b_field_seed = Counter({str(k): int(v) for k, v in b_suite["first_mismatch_field_counts_seeded"].items()})
    a_field_seed = Counter({str(k): int(v) for k, v in a_suite["first_mismatch_field_counts_seeded"].items()})

    by_dataset_before = {str(r["dataset"]): r for r in b["dataset_summaries"]}
    by_dataset_after = {str(r["dataset"]): r for r in a["dataset_summaries"]}
    keys_before = set(by_dataset_before.keys())
    keys_after = set(by_dataset_after.keys())
    shared = sorted(keys_before & keys_after)

    per_dataset_delta = []
    for key in shared:
        rb = by_dataset_before[key]
        ra = by_dataset_after[key]
        per_dataset_delta.append(
            {
                "dataset": key,
                "best_len": int(ra["best_len"]) - int(rb["best_len"]),
                "median_streak_len": int(ra["median_streak_len"]) - int(rb["median_streak_len"]),
                "p90_streak_len": int(ra["p90_streak_len"]) - int(rb["p90_streak_len"]),
                "p95_streak_len": int(ra["p95_streak_len"]) - int(rb["p95_streak_len"]),
                "max_streak_len": int(ra["max_streak_len"]) - int(rb["max_streak_len"]),
                "first_mismatch_total": int(ra["first_mismatch_total"]) - int(rb["first_mismatch_total"]),
                "first_mismatch_seeded_total": int(ra["first_mismatch_seeded_total"])
                - int(rb["first_mismatch_seeded_total"]),
            }
        )

    per_dataset_delta.sort(key=lambda r: (r["p90_streak_len"], r["median_streak_len"], r["best_len"]), reverse=True)

    all_field_keys = sorted(set(b_field.keys()) | set(a_field.keys()))
    all_field_seed_keys = sorted(set(b_field_seed.keys()) | set(a_field_seed.keys()))
    field_delta = {k: int(a_field.get(k, 0)) - int(b_field.get(k, 0)) for k in all_field_keys}
    field_seed_delta = {
        k: int(a_field_seed.get(k, 0)) - int(b_field_seed.get(k, 0)) for k in all_field_seed_keys
    }

    return {
        "before_suite": b["suite"],
        "after_suite": a["suite"],
        "before_fields": list(b["fields"]),
        "after_fields": list(a["fields"]),
        "suite_before": b_suite,
        "suite_after": a_suite,
        "suite_delta": suite_delta,
        "suite_first_mismatch_field_delta": field_delta,
        "suite_first_mismatch_field_seeded_delta": field_seed_delta,
        "dataset_new": sorted(keys_after - keys_before),
        "dataset_gone": sorted(keys_before - keys_after),
        "dataset_shared": shared,
        "per_dataset_delta": per_dataset_delta,
    }
