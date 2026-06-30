from __future__ import annotations

"""Reviewed report-level validation exceptions.

These helpers are display/reporting overlays only. They do not change simulator output, canonical
validation scoring, or replay-real locks.
"""

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping


@dataclass(frozen=True)
class RolloutFirstMismatchException:
    dataset: str
    record: int
    player: int
    field: str
    subindex: int
    seed: int
    out: int
    ref: int
    seeded_break: bool
    category: str
    reason: str


@dataclass(frozen=True)
class RolloutFloatAnnotation:
    dataset: str
    record: int
    player: int
    field: str
    category: str
    reason: str


@dataclass(frozen=True)
class ValidationExceptions:
    rollout_first_mismatch: tuple[RolloutFirstMismatchException, ...] = ()
    rollout_float_annotations: tuple[RolloutFloatAnnotation, ...] = ()


def _dataset_matches(pattern: str, dataset: str) -> bool:
    pattern_path = Path(pattern)
    dataset_path = Path(dataset)
    return (
        pattern == dataset
        or pattern_path.name == dataset_path.name
        or pattern_path.with_suffix("").name == dataset_path.with_suffix("").name
    )


def _require_str(raw: Mapping[str, Any], key: str, *, source: str) -> str:
    value = raw.get(key)
    if not isinstance(value, str) or value.strip() == "":
        raise ValueError(f"{source}: {key} must be a non-empty string")
    return value.strip()


def _require_int(raw: Mapping[str, Any], key: str, *, source: str) -> int:
    try:
        return int(raw[key])
    except Exception as exc:
        raise ValueError(f"{source}: {key} must be an integer") from exc


def _require_bool(raw: Mapping[str, Any], key: str, *, source: str) -> bool:
    value = raw.get(key)
    if isinstance(value, bool):
        return bool(value)
    if isinstance(value, int) and value in (0, 1):
        return bool(value)
    raise ValueError(f"{source}: {key} must be a boolean")


def _parse_first_exception(raw: Mapping[str, Any], *, source: str) -> RolloutFirstMismatchException:
    return RolloutFirstMismatchException(
        dataset=_require_str(raw, "dataset", source=source),
        record=_require_int(raw, "record", source=source),
        player=_require_int(raw, "player", source=source),
        field=_require_str(raw, "field", source=source),
        subindex=_require_int(raw, "subindex", source=source),
        seed=_require_int(raw, "seed", source=source),
        out=_require_int(raw, "out", source=source),
        ref=_require_int(raw, "ref", source=source),
        seeded_break=_require_bool(raw, "seeded_break", source=source),
        category=_require_str(raw, "category", source=source),
        reason=_require_str(raw, "reason", source=source),
    )


def _parse_float_annotation(raw: Mapping[str, Any], *, source: str) -> RolloutFloatAnnotation:
    return RolloutFloatAnnotation(
        dataset=_require_str(raw, "dataset", source=source),
        record=_require_int(raw, "record", source=source),
        player=_require_int(raw, "player", source=source),
        field=_require_str(raw, "field", source=source),
        category=_require_str(raw, "category", source=source),
        reason=_require_str(raw, "reason", source=source),
    )


def _require_mapping(item: Any, *, source: str) -> Mapping[str, Any]:
    if not isinstance(item, Mapping):
        raise ValueError(f"{source}: expected JSON object")
    return item


def load_validation_exceptions(path: Path) -> ValidationExceptions:
    if not path.exists():
        return ValidationExceptions()
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, Mapping):
        raise ValueError(f"{path}: expected JSON object")
    version = int(raw.get("version", 0))
    if version != 1:
        raise ValueError(f"{path}: unsupported version {version}; expected 1")

    first_raw = raw.get("rollout_first_mismatch_exceptions", [])
    if not isinstance(first_raw, list):
        raise ValueError(f"{path}: rollout_first_mismatch_exceptions must be a list")
    float_raw = raw.get("rollout_float_annotations", [])
    if not isinstance(float_raw, list):
        raise ValueError(f"{path}: rollout_float_annotations must be a list")
    return ValidationExceptions(
        rollout_first_mismatch=tuple(
            _parse_first_exception(
                _require_mapping(item, source=f"{path}:rollout_first_mismatch_exceptions[{i}]"),
                source=f"{path}:rollout_first_mismatch_exceptions[{i}]",
            )
            for i, item in enumerate(first_raw)
        ),
        rollout_float_annotations=tuple(
            _parse_float_annotation(
                _require_mapping(item, source=f"{path}:rollout_float_annotations[{i}]"),
                source=f"{path}:rollout_float_annotations[{i}]",
            )
            for i, item in enumerate(float_raw)
        ),
    )


def rollout_first_exception_matches_row(
    exc: RolloutFirstMismatchException, row: Mapping[str, Any]
) -> bool:
    return (
        _dataset_matches(exc.dataset, str(row.get("dataset", "")))
        and int(row.get("record", -1)) == exc.record
        and int(row.get("player", -1)) == exc.player
        and str(row.get("field", "")) == exc.field
        and int(row.get("subindex", -999)) == exc.subindex
        and int(row.get("seed", -999999)) == exc.seed
        and int(row.get("out", -999999)) == exc.out
        and int(row.get("ref", -999999)) == exc.ref
        and bool(row.get("seeded_break", False)) == exc.seeded_break
    )


def match_rollout_first_exceptions(
    *,
    dataset: str,
    rows: tuple[Mapping[str, Any], ...],
    exceptions: ValidationExceptions,
) -> tuple[tuple[RolloutFirstMismatchException, Mapping[str, Any]], tuple[RolloutFirstMismatchException, ...]]:
    accepted: list[tuple[RolloutFirstMismatchException, Mapping[str, Any]]] = []
    stale: list[RolloutFirstMismatchException] = []
    relevant = [
        exc for exc in exceptions.rollout_first_mismatch if _dataset_matches(exc.dataset, dataset)
    ]
    for exc in relevant:
        match = next((row for row in rows if rollout_first_exception_matches_row(exc, row)), None)
        if match is None:
            stale.append(exc)
        else:
            accepted.append((exc, match))
    return tuple(accepted), tuple(stale)


def classify_float_row(
    *,
    dataset: str,
    record: int,
    player: int,
    field: str,
    exceptions: ValidationExceptions,
) -> tuple[str, str]:
    for ann in exceptions.rollout_float_annotations:
        if (
            _dataset_matches(ann.dataset, dataset)
            and ann.record == int(record)
            and ann.player == int(player)
            and ann.field == field
        ):
            return ann.category, ann.reason
    return "unclassified", ""
