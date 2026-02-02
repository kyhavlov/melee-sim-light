from __future__ import annotations

from collections import Counter

import tools.eval.run_longest_rollout_streaks as streaks


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

    out = streaks._scan_rollout_streaks(
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

    out = streaks._scan_rollout_streaks(
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

    out = streaks._scan_rollout_streaks(
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

