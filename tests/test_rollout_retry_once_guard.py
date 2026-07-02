from __future__ import annotations

from collections import Counter
from dataclasses import dataclass


@dataclass(frozen=True)
class _ScanResult:
    best_len: int
    best_start_record: int
    best_end_record_excl: int
    streak_histogram: Counter[int]
    first_mismatch_field_counts: Counter[str]
    first_mismatch_field_counts_seeded: Counter[str]
    ignored_first_mismatch_field_counts: Counter[str]
    ignored_first_mismatch_field_counts_seeded: Counter[str]


@dataclass(frozen=True)
class _AttemptResult:
    scored_field: str | None = None
    ignored_first_field: str | None = None


def _normalize_attempt_result(value: str | _AttemptResult | None) -> _AttemptResult:
    if isinstance(value, _AttemptResult):
        return value
    return _AttemptResult(scored_field=value)


def _scan_rollout_streaks(*, n: int, reseed_at, attempt_from_current, attempt_seeded_at_record):
    best_len = 0
    best_start = 0
    best_end_excl = 0
    cur_start = 0
    cur_len = 0
    hist: Counter[int] = Counter()
    mismatch_fields: Counter[str] = Counter()
    mismatch_fields_seeded: Counter[str] = Counter()
    ignored_first_fields: Counter[str] = Counter()
    ignored_first_fields_seeded: Counter[str] = Counter()

    needs_seed = True
    j = 0
    while j < n:
        if needs_seed:
            reseed_at(cur_start)
            needs_seed = False

        first = _normalize_attempt_result(attempt_from_current(j))
        if first.ignored_first_field is not None:
            ignored_first_fields[first.ignored_first_field] += 1
        if first.scored_field is None:
            cur_len += 1
            if cur_len > best_len:
                best_len = cur_len
                best_start = cur_start
                best_end_excl = cur_start + cur_len
            j += 1
            continue

        if cur_len:
            hist[cur_len] += 1
        mismatch_fields[first.scored_field] += 1

        cur_start = j
        cur_len = 0
        seeded = _normalize_attempt_result(attempt_seeded_at_record(j))
        if seeded.ignored_first_field is not None:
            ignored_first_fields_seeded[seeded.ignored_first_field] += 1
        if seeded.scored_field is None:
            cur_len = 1
            if cur_len > best_len:
                best_len = cur_len
                best_start = cur_start
                best_end_excl = cur_start + cur_len
            j += 1
            continue

        mismatch_fields_seeded[seeded.scored_field] += 1
        cur_start = j + 1
        cur_len = 0
        j += 1
        needs_seed = True

    if cur_len:
        hist[cur_len] += 1
    return _ScanResult(
        best_len=best_len,
        best_start_record=best_start,
        best_end_record_excl=best_end_excl,
        streak_histogram=hist,
        first_mismatch_field_counts=mismatch_fields,
        first_mismatch_field_counts_seeded=mismatch_fields_seeded,
        ignored_first_mismatch_field_counts=ignored_first_fields,
        ignored_first_mismatch_field_counts_seeded=ignored_first_fields_seeded,
    )


def test_retry_once_then_advance_never_loops() -> None:
    n = 8
    reseeds: list[int] = []
    attempts: list[int] = []
    retries: list[int] = []

    def reseed_at(j: int) -> None:
        reseeds.append(j)

    def attempt_from_current(j: int) -> str | None:
        attempts.append(j)
        return "action_id"

    def attempt_seeded_at_record(j: int) -> str | None:
        retries.append(j)
        return "action_id"

    out = _scan_rollout_streaks(
        n=n,
        reseed_at=reseed_at,
        attempt_from_current=attempt_from_current,
        attempt_seeded_at_record=attempt_seeded_at_record,
    )

    assert attempts == list(range(n))
    assert retries == list(range(n))
    assert reseeds == list(range(n))
    assert out.best_len == 0
    assert out.best_start_record == 0
    assert out.best_end_record_excl == 0
    assert out.streak_histogram == Counter()
    assert out.first_mismatch_field_counts == Counter({"action_id": n})
    assert out.first_mismatch_field_counts_seeded == Counter({"action_id": n})


def test_reseed_at_same_record_on_mismatch_then_continue_on_retry_success() -> None:
    n = 4
    reseeds: list[int] = []
    attempts: list[int] = []
    retries: list[int] = []

    def reseed_at(j: int) -> None:
        reseeds.append(j)

    def attempt_from_current(j: int) -> str | None:
        attempts.append(j)
        return None if j != 1 else "hitstun"

    def attempt_seeded_at_record(j: int) -> str | None:
        retries.append(j)
        return None

    out = _scan_rollout_streaks(
        n=n,
        reseed_at=reseed_at,
        attempt_from_current=attempt_from_current,
        attempt_seeded_at_record=attempt_seeded_at_record,
    )

    assert retries == [1]
    assert attempts == [0, 1, 2, 3]
    assert reseeds == [0]
    assert out.best_len == 3
    assert out.best_start_record == 1
    assert out.best_end_record_excl == 4
    assert out.streak_histogram == Counter({1: 1, 3: 1})
    assert out.first_mismatch_field_counts == Counter({"hitstun": 1})
    assert out.first_mismatch_field_counts_seeded == Counter()


def test_retry_failure_advances_to_next_record_and_reseeds() -> None:
    n = 3
    reseeds: list[int] = []
    attempts: list[int] = []
    retries: list[int] = []

    def reseed_at(j: int) -> None:
        reseeds.append(j)

    def attempt_from_current(j: int) -> str | None:
        attempts.append(j)
        return None if j == 0 else "state_flags"

    def attempt_seeded_at_record(j: int) -> str | None:
        retries.append(j)
        return "state_flags"

    out = _scan_rollout_streaks(
        n=n,
        reseed_at=reseed_at,
        attempt_from_current=attempt_from_current,
        attempt_seeded_at_record=attempt_seeded_at_record,
    )

    assert retries == [1, 2]
    assert attempts == [0, 1, 2]
    assert reseeds == [0, 2]
    assert out.best_len == 1
    assert out.best_start_record == 0
    assert out.best_end_record_excl == 1
    assert out.streak_histogram == Counter({1: 1})
    assert out.first_mismatch_field_counts == Counter({"state_flags": 2})
    assert out.first_mismatch_field_counts_seeded == Counter({"state_flags": 2})
