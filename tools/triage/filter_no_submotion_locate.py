from __future__ import annotations

"""Filter locate_discrete_mismatches TSV rows to no-submotion seed snapshots.

Usage:
  uv run python -m tools.triage.filter_no_submotion_locate --locate-tsv <locate.tsv>
  uv run python -m tools.triage.filter_no_submotion_locate --locate-tsv a.tsv --locate-tsv b.tsv

Defaults:
  - writes outputs under reports/triage/<utc-timestamp>/
  - emits no_submotion_rows.tsv and no_submotion_top.tsv

Notes:
  - offline data processing only (TSV + dataset files); no msl_binding/sim runtime import.
"""

import argparse
import csv
from collections import defaultdict
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path

from tools.eval.dataset import read_dataset
from tools.eval.locate_tsv import LocateRow, parse_locate_tsv


@dataclass(frozen=True)
class AnnotatedRow:
    dataset: str
    record: int
    seed_frame: int
    ref_frame: int
    p: int
    field: str
    seed: int
    out: int
    ref: int
    seed_action_id: int
    seed_action_frame: int
    seed_animation_index: int
    seed_anim_frame_f32: float
    seed_hitlag: int
    seed_hitstun: int
    seed_on_ground: int
    seed_ground_id: int
    ref_action_id: int
    ref_action_frame: int
    ref_animation_index: int
    ref_hitlag: int
    ref_hitstun: int
    ref_on_ground: int
    ref_ground_id: int


def _default_out_dir() -> Path:
    ts = datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
    return Path("reports/triage") / ts


def _annotate(rows: list[LocateRow], *, require_seed_action_frame_minus1: bool) -> list[AnnotatedRow]:
    ds_cache: dict[str, object] = {}
    out: list[AnnotatedRow] = []

    for row in rows:
        dataset = row.dataset
        if dataset not in ds_cache:
            ds_cache[dataset] = read_dataset(dataset).samples
        samples = ds_cache[dataset]

        sample = samples[row.record]
        seed = sample["seed_t"]
        ref = sample["ref_t1"]

        p = row.p
        seed_anim = int(seed["animation_index"][p])
        if seed_anim != 0xFFFFFFFF:
            continue
        seed_action_frame = int(seed["action_frame"][p])
        if require_seed_action_frame_minus1 and seed_action_frame != -1:
            continue

        out.append(
            AnnotatedRow(
                dataset=dataset,
                record=row.record,
                seed_frame=row.seed_frame,
                ref_frame=row.ref_frame,
                p=p,
                field=row.field,
                seed=row.seed,
                out=row.out,
                ref=row.ref,
                seed_action_id=int(seed["action_id"][p]),
                seed_action_frame=seed_action_frame,
                seed_animation_index=seed_anim,
                seed_anim_frame_f32=float(seed["anim_frame_f32"][p]),
                seed_hitlag=int(seed["hitlag"][p]),
                seed_hitstun=int(seed["hitstun"][p]),
                seed_on_ground=int(seed["on_ground"][p]),
                seed_ground_id=int(seed["ground_id"][p]),
                ref_action_id=int(ref["action_id"][p]),
                ref_action_frame=int(ref["action_frame"][p]),
                ref_animation_index=int(ref["animation_index"][p]),
                ref_hitlag=int(ref["hitlag"][p]),
                ref_hitstun=int(ref["hitstun"][p]),
                ref_on_ground=int(ref["on_ground"][p]),
                ref_ground_id=int(ref["ground_id"][p]),
            )
        )

    out.sort(
        key=lambda r: (
            r.dataset,
            r.record,
            r.p,
            r.field,
        )
    )
    return out


def _write_rows(path: Path, rows: list[AnnotatedRow]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(list(AnnotatedRow.__dataclass_fields__.keys()))
        for r in rows:
            w.writerow(
                [
                    r.dataset,
                    r.record,
                    r.seed_frame,
                    r.ref_frame,
                    r.p,
                    r.field,
                    r.seed,
                    r.out,
                    r.ref,
                    r.seed_action_id,
                    r.seed_action_frame,
                    r.seed_animation_index,
                    f"{r.seed_anim_frame_f32:.6f}",
                    r.seed_hitlag,
                    r.seed_hitstun,
                    r.seed_on_ground,
                    r.seed_ground_id,
                    r.ref_action_id,
                    r.ref_action_frame,
                    r.ref_animation_index,
                    r.ref_hitlag,
                    r.ref_hitstun,
                    r.ref_on_ground,
                    r.ref_ground_id,
                ]
            )


def _write_top(path: Path, rows: list[AnnotatedRow], *, top: int) -> None:
    agg: dict[tuple[str, int, int], list[AnnotatedRow]] = defaultdict(list)
    for r in rows:
        agg[(r.dataset, r.record, r.p)].append(r)

    ranked = sorted(
        agg.items(),
        key=lambda it: (-len(it[1]), it[0][0], it[0][1], it[0][2]),
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(
            [
                "dataset",
                "record",
                "p",
                "mismatch_rows",
                "mismatch_fields",
                "seed_action_id",
                "seed_action_frame",
                "seed_animation_index",
                "seed_hitlag",
                "seed_hitstun",
                "ref_action_id",
                "ref_action_frame",
                "ref_animation_index",
                "ref_hitlag",
                "ref_hitstun",
                "fields",
            ]
        )
        for (dataset, record, p), lst in ranked[: max(1, int(top))]:
            first = lst[0]
            fields = ",".join(sorted({r.field for r in lst}))
            w.writerow(
                [
                    dataset,
                    record,
                    p,
                    len(lst),
                    len({r.field for r in lst}),
                    first.seed_action_id,
                    first.seed_action_frame,
                    first.seed_animation_index,
                    first.seed_hitlag,
                    first.seed_hitstun,
                    first.ref_action_id,
                    first.ref_action_frame,
                    first.ref_animation_index,
                    first.ref_hitlag,
                    first.ref_hitstun,
                    fields,
                ]
            )


def main() -> None:
    ap = argparse.ArgumentParser(
        description=(
            "Filter locate_discrete_mismatches TSV rows to no-submotion seed snapshots "
            "(seed.animation_index==0xFFFFFFFF) and emit joined seed/ref metadata."
        )
    )
    ap.add_argument(
        "--locate-tsv",
        action="append",
        required=True,
        help="Path to a locate_discrete_mismatches TSV (repeatable).",
    )
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output directory (default: reports/triage/<utc-timestamp>).",
    )
    ap.add_argument(
        "--require-seed-action-frame-minus1",
        action="store_true",
        help="Keep only rows where seed.action_frame == -1.",
    )
    ap.add_argument(
        "--top",
        type=int,
        default=25,
        help="Number of top record/port clusters to emit in no_submotion_top.tsv.",
    )
    args = ap.parse_args()

    locate_paths = [Path(p).resolve() for p in args.locate_tsv]
    all_rows: list[LocateRow] = []
    for p in locate_paths:
        all_rows.extend(parse_locate_tsv(p))

    annotated = _annotate(
        all_rows,
        require_seed_action_frame_minus1=bool(args.require_seed_action_frame_minus1),
    )

    out_dir = args.out_dir if args.out_dir is not None else _default_out_dir()
    out_dir = out_dir.resolve()
    rows_path = out_dir / "no_submotion_rows.tsv"
    top_path = out_dir / "no_submotion_top.tsv"

    _write_rows(rows_path, annotated)
    _write_top(top_path, annotated, top=int(args.top))

    print(f"rows={len(annotated)}")
    print(f"rows_tsv={rows_path}")
    print(f"top_tsv={top_path}")


if __name__ == "__main__":
    main()
