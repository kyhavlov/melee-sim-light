from __future__ import annotations

"""Helpers for rollout first-break locate TSVs.

The TSV emitted by `tools.eval.locate_rollout_desyncs` uses:
`dataset,record,seed_frame,ref_frame,player,field,subindex,seed,out,ref,`
`streak_start_record,streak_len,seeded_break,cluster_key`.
"""

import csv
import io
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class RolloutLocateRow:
    dataset: str
    record: int
    seed_frame: int
    ref_frame: int
    player: int
    field: str
    subindex: int
    seed: int
    out: int
    ref: int
    streak_start_record: int
    streak_len: int
    seeded_break: bool
    cluster_key: str


ROLLOUT_LOCATE_COLUMNS: tuple[str, ...] = (
    "dataset",
    "record",
    "seed_frame",
    "ref_frame",
    "player",
    "field",
    "subindex",
    "seed",
    "out",
    "ref",
    "streak_start_record",
    "streak_len",
    "seeded_break",
    "cluster_key",
)


@dataclass(frozen=True)
class RolloutClusterSummary:
    cluster_key: str
    field: str
    subindex: int
    seed: int
    out: int
    ref: int
    frequency: int
    impact: int
    seeded_breaks: int
    datasets: tuple[str, ...]
    example: RolloutLocateRow


def cluster_key_for(*, field: str, subindex: int, seed: int, out: int, ref: int) -> str:
    """Stable cross-dataset cluster key for rollout desync ranking."""

    return f"v1|field={field}|subindex={int(subindex)}|seed={int(seed)}|out={int(out)}|ref={int(ref)}"


def parse_seeded_break(value: str) -> bool:
    v = value.strip().lower()
    if v in ("1", "true", "yes"):
        return True
    if v in ("0", "false", "no"):
        return False
    raise ValueError(f"invalid seeded_break value: {value!r}")


def seeded_break_text(value: bool) -> str:
    return "1" if value else "0"


def write_rollout_locate_tsv(path: Path, rows: Iterable[RolloutLocateRow]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f, delimiter="\t", lineterminator="\n")
        writer.writerow(ROLLOUT_LOCATE_COLUMNS)
        for row in rows:
            writer.writerow(row_to_tsv_values(row))


def row_to_tsv_values(row: RolloutLocateRow) -> tuple[object, ...]:
    return (
        row.dataset,
        int(row.record),
        int(row.seed_frame),
        int(row.ref_frame),
        int(row.player),
        row.field,
        int(row.subindex),
        int(row.seed),
        int(row.out),
        int(row.ref),
        int(row.streak_start_record),
        int(row.streak_len),
        seeded_break_text(bool(row.seeded_break)),
        row.cluster_key,
    )


def parse_rollout_locate_tsv(path: Path) -> list[RolloutLocateRow]:
    with path.open("r", encoding="utf-8", newline="") as f:
        return _parse_reader(csv.DictReader(f, delimiter="\t"), source=str(path))


def parse_rollout_locate_tsv_text(text: str, *, source: str = "<memory>") -> list[RolloutLocateRow]:
    stream = io.StringIO(text)
    return _parse_reader(csv.DictReader(stream, delimiter="\t"), source=source)


def _parse_reader(reader: csv.DictReader, *, source: str) -> list[RolloutLocateRow]:
    if reader.fieldnames is None:
        raise ValueError(f"{source}: missing TSV header")
    missing = [name for name in ROLLOUT_LOCATE_COLUMNS if name not in reader.fieldnames]
    if missing:
        raise ValueError(f"{source}: missing columns: {', '.join(missing)}")

    rows: list[RolloutLocateRow] = []
    for raw in reader:
        rows.append(
            RolloutLocateRow(
                dataset=str(raw["dataset"]),
                record=int(raw["record"]),
                seed_frame=int(raw["seed_frame"]),
                ref_frame=int(raw["ref_frame"]),
                player=int(raw["player"]),
                field=str(raw["field"]),
                subindex=int(raw["subindex"]),
                seed=int(raw["seed"]),
                out=int(raw["out"]),
                ref=int(raw["ref"]),
                streak_start_record=int(raw["streak_start_record"]),
                streak_len=int(raw["streak_len"]),
                seeded_break=parse_seeded_break(str(raw["seeded_break"])),
                cluster_key=str(raw["cluster_key"]),
            )
        )
    return rows


def summarize_clusters(rows: Iterable[RolloutLocateRow]) -> list[RolloutClusterSummary]:
    by_key: dict[str, list[RolloutLocateRow]] = defaultdict(list)
    for row in rows:
        by_key[row.cluster_key].append(row)

    summaries: list[RolloutClusterSummary] = []
    for key, group in by_key.items():
        example = group[0]
        dataset_counts = Counter(row.dataset for row in group)
        datasets = tuple(
            ds for ds, _count in sorted(dataset_counts.items(), key=lambda item: (-item[1], item[0]))
        )
        summaries.append(
            RolloutClusterSummary(
                cluster_key=key,
                field=example.field,
                subindex=int(example.subindex),
                seed=int(example.seed),
                out=int(example.out),
                ref=int(example.ref),
                frequency=len(group),
                impact=sum(max(0, int(row.streak_len)) for row in group),
                seeded_breaks=sum(1 for row in group if row.seeded_break),
                datasets=datasets,
                example=example,
            )
        )

    return sorted(
        summaries,
        key=lambda s: (-s.impact, -s.frequency, -s.seeded_breaks, s.cluster_key),
    )


def parse_rollout_key_fields(spec: str) -> tuple[str, ...]:
    fields = tuple(s.strip() for s in spec.split(",") if s.strip())
    if not fields:
        raise ValueError("key field list is empty")
    bad = [name for name in fields if name not in ROLLOUT_LOCATE_COLUMNS]
    if bad:
        allowed = ", ".join(ROLLOUT_LOCATE_COLUMNS)
        raise ValueError(f"unknown key columns: {', '.join(bad)}; allowed: {allowed}")
    return fields


def rollout_row_key(row: RolloutLocateRow, key_fields: tuple[str, ...]) -> tuple[object, ...]:
    return tuple(getattr(row, name) for name in key_fields)

