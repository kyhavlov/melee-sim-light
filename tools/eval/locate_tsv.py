from __future__ import annotations

"""Helpers for reading and filtering locate-discrete-mismatches TSV rows.

The TSV emitted by `tools.eval.locate_discrete_mismatches --format tsv` uses:
`dataset,record,seed_frame,ref_frame,p,field,seed,out,ref`.
"""

import csv
import io
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class LocateRow:
    dataset: str
    record: int
    seed_frame: int
    ref_frame: int
    p: int
    field: str
    seed: int
    out: int
    ref: int


LOCATE_COLUMNS: tuple[str, ...] = (
    "dataset",
    "record",
    "seed_frame",
    "ref_frame",
    "p",
    "field",
    "seed",
    "out",
    "ref",
)


def parse_locate_tsv(path: Path) -> list[LocateRow]:
    with path.open("r", encoding="utf-8", newline="") as f:
        return _parse_reader(csv.DictReader(f, delimiter="\t"), source=str(path))


def parse_locate_tsv_text(text: str, *, source: str = "<memory>") -> list[LocateRow]:
    stream = io.StringIO(text)
    return _parse_reader(csv.DictReader(stream, delimiter="\t"), source=source)


def _parse_reader(reader: csv.DictReader, *, source: str) -> list[LocateRow]:
    rows: list[LocateRow] = []
    if reader.fieldnames is None:
        raise ValueError(f"{source}: missing TSV header")
    missing = [name for name in LOCATE_COLUMNS if name not in reader.fieldnames]
    if missing:
        raise ValueError(f"{source}: missing columns: {', '.join(missing)}")
    for raw in reader:
        rows.append(
            LocateRow(
                dataset=str(raw["dataset"]),
                record=int(raw["record"]),
                seed_frame=int(raw["seed_frame"]),
                ref_frame=int(raw["ref_frame"]),
                p=int(raw["p"]),
                field=str(raw["field"]),
                seed=int(raw["seed"]),
                out=int(raw["out"]),
                ref=int(raw["ref"]),
            )
        )
    return rows


def parse_key_fields(spec: str) -> tuple[str, ...]:
    fields = tuple(s.strip() for s in spec.split(",") if s.strip())
    if not fields:
        raise ValueError("key field list is empty")
    bad = [name for name in fields if name not in LOCATE_COLUMNS]
    if bad:
        allowed = ", ".join(LOCATE_COLUMNS)
        raise ValueError(f"unknown key columns: {', '.join(bad)}; allowed: {allowed}")
    return fields


def row_key(row: LocateRow, key_fields: tuple[str, ...]) -> tuple[object, ...]:
    return tuple(getattr(row, name) for name in key_fields)


def row_matches_filter(row: LocateRow, expr: str) -> bool:
    expr = expr.strip()
    if expr == "":
        return True
    if "=" in expr:
        left, right = expr.split("=", 1)
        left = left.strip()
        right = right.strip()
        if left not in LOCATE_COLUMNS:
            raise ValueError(f"unknown filter column: {left}")
        return right in str(getattr(row, left))

    haystack = "\t".join(
        (
            row.dataset,
            str(row.record),
            str(row.seed_frame),
            str(row.ref_frame),
            str(row.p),
            row.field,
            str(row.seed),
            str(row.out),
            str(row.ref),
        )
    )
    return expr in haystack


def filter_rows(rows: Iterable[LocateRow], expr: str) -> list[LocateRow]:
    if expr.strip() == "":
        return list(rows)
    return [row for row in rows if row_matches_filter(row, expr)]
