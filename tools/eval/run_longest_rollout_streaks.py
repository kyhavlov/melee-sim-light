from __future__ import annotations

"""Longest in-sync rollout streaks over a preprocessed replay suite.

This is a diagnostic evaluator: it rolls forward sequentially through each `.msl` dataset and
records the longest contiguous streak of exact matches on a discrete-only field set. On a
desync at record j, it reseeds at record j and retries once; if it still mismatches, it advances
to j+1 (guard against infinite loops).

Example:
  uv run python -m tools.eval.run_longest_rollout_streaks \\
    --suite replays/suites/fox_falco_fd_ucf084_recent.json \\
    --datasets-dir datasets \\
    --fields action_id,animation_index,on_ground,hitlag,hitstun,state_flags \\
    --out reports/triage/rollout_streaks.json
"""

import argparse
import importlib
import json
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, Dataset, read_dataset
from tools.eval.discrete_compare_lanes import compile_discrete_compare_lanes, first_mismatch_field
from tools.eval.validation_profile import ValidationProfile, get_validation_profile, validation_profile_names
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


_STANDARD_ROLLOUT_FIELDS = ("action_id", "animation_index", "on_ground", "hitlag", "hitstun", "state_flags")


def _load_binding():
    # Built by `uv pip install -e python` (or similar).
    return importlib.import_module("msl_binding")


def _parse_csv(s: str) -> tuple[str, ...]:
    return tuple(x.strip() for x in s.split(",") if x.strip() != "")


def _parse_players(players_csv: str | None, *, num_players: int) -> tuple[int, ...]:
    if players_csv is None or players_csv.strip() == "":
        return tuple(range(num_players))
    out: list[int] = []
    for part in _parse_csv(players_csv):
        try:
            p = int(part)
        except ValueError as e:
            raise SystemExit(f"error: invalid --players entry {part!r} (want integers)") from e
        if p < 0 or p >= num_players:
            raise SystemExit(f"error: --players includes {p}, but dataset has num_players={num_players}")
        out.append(p)
    # Deterministic order; drop dups.
    return tuple(sorted(set(out)))


def _validate_discrete_fields(fields: tuple[str, ...]) -> tuple[str, ...]:
    if not fields:
        raise SystemExit("error: --fields is empty")
    missing = [f for f in fields if f not in COMPARE_DTYPE.fields]
    if missing:
        raise SystemExit(f"error: unknown compare fields: {', '.join(missing)}")

    floaty = []
    for f in fields:
        base = COMPARE_DTYPE.fields[f][0]
        # Items is a nested struct with floats; treat as non-discrete for this evaluator.
        if base.fields is not None:
            floaty.append(f)
            continue
        if np.issubdtype(base, np.floating):
            floaty.append(f)
            continue
    if floaty:
        raise SystemExit(
            "error: discrete-only evaluator; these fields are non-discrete or structured: "
            + ", ".join(floaty)
        )
    return fields


def _first_mismatch_standard_rollout(
    *,
    out_row: np.void,
    ref_row: np.void,
    players: tuple[int, ...],
    profile_name: str,
) -> str | None:
    for field in ("action_id", "animation_index", "on_ground", "hitlag", "hitstun"):
        out_v = out_row[field]
        ref_v = ref_row[field]
        for p in players:
            if int(out_v[p]) != int(ref_v[p]):
                return field

    out_sf = out_row["state_flags"]
    ref_sf = ref_row["state_flags"]
    if profile_name == "rl1_gameplay":
        for p in players:
            for sub in range(4):
                if int(out_sf[p, sub]) != int(ref_sf[p, sub]):
                    return "state_flags"
            if ((int(out_sf[p, 4]) ^ int(ref_sf[p, 4])) & 0x7F) != 0:
                return "state_flags"
        return None

    for p in players:
        for sub in range(5):
            if int(out_sf[p, sub]) != int(ref_sf[p, sub]):
                return "state_flags"
    return None


def _first_ignored_standard_rollout(
    *,
    out_row: np.void,
    ref_row: np.void,
    players: tuple[int, ...],
    profile_name: str,
) -> str | None:
    if profile_name != "rl1_gameplay":
        return None
    out_sf = out_row["state_flags"]
    ref_sf = ref_row["state_flags"]
    for p in players:
        if ((int(out_sf[p, 4]) ^ int(ref_sf[p, 4])) & 0x80) != 0:
            return "state_flags[4]&0x80"
    return None


@dataclass(frozen=True)
class DatasetStreaks:
    dataset: str
    num_records: int
    max_records_used: int
    players: tuple[int, ...]
    fields: tuple[str, ...]
    best_len: int
    best_start_record: int
    best_end_record_excl: int
    best_start_seed_frame_id: int | None
    best_end_ref_frame_id_inclusive: int | None
    streak_histogram: dict[int, int]
    first_mismatch_field_counts: dict[str, int]
    first_mismatch_field_counts_seeded: dict[str, int]
    ignored_first_mismatch_field_counts: dict[str, int]
    ignored_first_mismatch_field_counts_seeded: dict[str, int]
    profile_name: str


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


def _scan_rollout_streaks(
    *,
    n: int,
    reseed_at,
    attempt_from_current,
    attempt_seeded_at_record,
) -> _ScanResult:
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

        attempt = _normalize_attempt_result(attempt_from_current(j))
        if attempt.ignored_first_field is not None:
            ignored_first_fields[attempt.ignored_first_field] += 1
        if attempt.scored_field is None:
            cur_len += 1
            if cur_len > best_len:
                best_len = cur_len
                best_start = cur_start
                best_end_excl = cur_start + cur_len
            j += 1
            continue

        if cur_len > 0:
            hist[cur_len] += 1
        mismatch_fields[attempt.scored_field] += 1

        # Start a new streak at the same record j (reseed-at-j), retry once.
        cur_start = j
        cur_len = 0

        retry = _normalize_attempt_result(attempt_seeded_at_record(j))
        if retry.ignored_first_field is not None:
            ignored_first_fields_seeded[retry.ignored_first_field] += 1
        if retry.scored_field is None:
            cur_len = 1
            if cur_len > best_len:
                best_len = cur_len
                best_start = cur_start
                best_end_excl = cur_start + cur_len
            j += 1
            continue

        mismatch_fields_seeded[retry.scored_field] += 1

        # Guard: if it mismatches even when seeded-at-j, advance to j+1.
        cur_start = j + 1
        cur_len = 0
        j += 1
        needs_seed = True

    if cur_len > 0:
        hist[cur_len] += 1

    return _ScanResult(
        best_len=int(best_len),
        best_start_record=int(best_start),
        best_end_record_excl=int(best_end_excl),
        streak_histogram=hist,
        first_mismatch_field_counts=mismatch_fields,
        first_mismatch_field_counts_seeded=mismatch_fields_seeded,
        ignored_first_mismatch_field_counts=ignored_first_fields,
        ignored_first_mismatch_field_counts_seeded=ignored_first_fields_seeded,
    )


def _scan_dataset_streaks(
    *,
    dataset_path: Path,
    ds: Dataset,
    fields: tuple[str, ...],
    players: tuple[int, ...],
    max_records: int,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
    profile: str | ValidationProfile | None = None,
) -> DatasetStreaks:
    validation_profile = get_validation_profile(profile)
    samples = ds.samples
    num_records_total = int(samples.shape[0])
    num_players = int(ds.header["num_players"])

    n = num_records_total
    if max_records > 0:
        n = min(n, int(max_records))

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    init_kwargs = {"batch_size": 1, "num_players": num_players}
    if ucf_enabled is not None:
        init_kwargs["ucf_enabled"] = int(bool(ucf_enabled))
    if ucf_cardinals_1_0_enabled is not None:
        init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(ucf_cardinals_1_0_enabled))
    handle = binding.init(**init_kwargs)

    # Preallocated buffers (bytes) that C reads/writes.
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    # Efficient per-record byte slicing without per-step `.tobytes()` allocations.
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(num_records_total, sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    ref = samples["ref_t1"]
    seed = samples["seed_t"]
    compare_lanes = compile_discrete_compare_lanes(fields, players, profile=validation_profile)
    ignored_lanes = compile_discrete_compare_lanes(
        fields, players, profile=validation_profile, ignored_only=True
    )
    use_standard_rollout_compare = (
        tuple(fields) == _STANDARD_ROLLOUT_FIELDS
        and validation_profile.name in ("rl1_gameplay", "strict")
    )

    def reseed_at(j: int) -> None:
        seed_bytes[0, :] = samples_u8[j, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)

    def step_and_compare(j: int) -> _AttemptResult:
        seed_bytes[0, :] = samples_u8[j, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
        binding.step_input_replay_frame_rng(handle, seed_bytes, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        if use_standard_rollout_compare:
            scored = _first_mismatch_standard_rollout(
                out_row=out_view[0],
                ref_row=ref[j],
                players=players,
                profile_name=validation_profile.name,
            )
            ignored = _first_ignored_standard_rollout(
                out_row=out_view[0],
                ref_row=ref[j],
                players=players,
                profile_name=validation_profile.name,
            )
        else:
            scored = first_mismatch_field(
                out_row=out_view[0],
                ref_row=ref[j],
                lanes=compare_lanes,
            )
            ignored = first_mismatch_field(
                out_row=out_view[0], ref_row=ref[j], lanes=ignored_lanes, label_subindex=True
            )
        return _AttemptResult(scored_field=scored, ignored_first_field=ignored)

    def step_seeded_and_compare(j: int) -> _AttemptResult:
        reseed_at(j)
        return step_and_compare(j)

    try:
        scan = _scan_rollout_streaks(
            n=n,
            reseed_at=reseed_at,
            attempt_from_current=step_and_compare,
            attempt_seeded_at_record=step_seeded_and_compare,
        )
    finally:
        try:
            binding.destroy(handle)
        except Exception:
            pass

    start_seed_frame = None
    end_ref_frame_incl = None
    if scan.best_len > 0:
        try:
            start_seed_frame = int(seed["frame_id"][scan.best_start_record])
        except Exception:
            start_seed_frame = None
        try:
            end_ref_frame_incl = int(ref["frame_id"][scan.best_end_record_excl - 1])
        except Exception:
            end_ref_frame_incl = None

    return DatasetStreaks(
        dataset=str(dataset_path),
        num_records=num_records_total,
        max_records_used=n,
        players=players,
        fields=fields,
        best_len=int(scan.best_len),
        best_start_record=int(scan.best_start_record),
        best_end_record_excl=int(scan.best_end_record_excl),
        best_start_seed_frame_id=start_seed_frame,
        best_end_ref_frame_id_inclusive=end_ref_frame_incl,
        streak_histogram=dict(sorted(scan.streak_histogram.items())),
        first_mismatch_field_counts=dict(sorted(scan.first_mismatch_field_counts.items())),
        first_mismatch_field_counts_seeded=dict(sorted(scan.first_mismatch_field_counts_seeded.items())),
        ignored_first_mismatch_field_counts=dict(sorted(scan.ignored_first_mismatch_field_counts.items())),
        ignored_first_mismatch_field_counts_seeded=dict(
            sorted(scan.ignored_first_mismatch_field_counts_seeded.items())
        ),
        profile_name=validation_profile.name,
    )


def _print_dataset_summary(*, root: Path, s: DatasetStreaks) -> None:
    rel = str(Path(s.dataset).resolve().relative_to(root))
    print(f"== {rel} ==")
    print(
        "best_len:",
        s.best_len,
        "best_records:",
        f"[{s.best_start_record},{s.best_end_record_excl})",
        f"(end_excl; len={s.best_len})",
    )
    if s.best_start_seed_frame_id is not None and s.best_end_ref_frame_id_inclusive is not None:
        print(
            "best_frames:",
            "start_seed_frame_id=",
            s.best_start_seed_frame_id,
            "end_ref_frame_id_inclusive=",
            s.best_end_ref_frame_id_inclusive,
        )
    print("records_used:", f"{s.max_records_used}/{s.num_records}", "players:", ",".join(map(str, s.players)))
    print("fields:", ",".join(s.fields))
    print("profile:", s.profile_name)

    if s.streak_histogram:
        pairs = sorted(s.streak_histogram.items(), key=lambda kv: (-kv[1], -kv[0]))[:12]
        preview = " ".join(f"{k}:{v}" for k, v in pairs)
        total_streaks = sum(s.streak_histogram.values())
        print("streak_hist_top:", preview, f"(top12 by count; total_streaks={total_streaks})")
    else:
        print("streak_hist_top: (none)")

    if s.first_mismatch_field_counts:
        pairs = sorted(s.first_mismatch_field_counts.items(), key=lambda kv: (-kv[1], kv[0]))[:12]
        preview = " ".join(f"{k}:{v}" for k, v in pairs)
        print("first_mismatch_field_top:", preview)
    else:
        print("first_mismatch_field_top: (none)")

    if s.first_mismatch_field_counts_seeded:
        pairs = sorted(s.first_mismatch_field_counts_seeded.items(), key=lambda kv: (-kv[1], kv[0]))[:12]
        preview = " ".join(f"{k}:{v}" for k, v in pairs)
        print("seeded_mismatch_field_top:", preview)
    else:
        print("seeded_mismatch_field_top: (none)")

    if s.ignored_first_mismatch_field_counts:
        pairs = sorted(s.ignored_first_mismatch_field_counts.items(), key=lambda kv: (-kv[1], kv[0]))[:12]
        preview = " ".join(f"{k}:{v}" for k, v in pairs)
        print("ignored_first_mismatch_field_top:", preview)
    else:
        print("ignored_first_mismatch_field_top: (none)")

    if s.ignored_first_mismatch_field_counts_seeded:
        pairs = sorted(s.ignored_first_mismatch_field_counts_seeded.items(), key=lambda kv: (-kv[1], kv[0]))[:12]
        preview = " ".join(f"{k}:{v}" for k, v in pairs)
        print("ignored_seeded_mismatch_field_top:", preview)
    else:
        print("ignored_seeded_mismatch_field_top: (none)")


def main() -> None:
    ap = argparse.ArgumentParser(description="Compute longest in-sync rollout streaks over a suite.")
    ap.add_argument("--suite", required=True, help="Suite JSON path under repo root.")
    ap.add_argument("--datasets-dir", default="datasets", help="Datasets directory under repo root.")
    ap.add_argument(
        "--fields",
        default="action_id,animation_index,on_ground,hitlag,hitstun,state_flags",
        help="Comma-separated discrete compare fields (default: %(default)s).",
    )
    ap.add_argument(
        "--profile",
        default="rl1_gameplay",
        choices=validation_profile_names(),
        help="Validation scoring profile. strict scores every compare lane; rl1_gameplay ignores RL1-irrelevant lanes.",
    )
    ap.add_argument(
        "--players",
        default=None,
        help="Comma-separated player indices to compare (default: all players in dataset).",
    )
    ap.add_argument("--out", type=Path, default=None, help="Optional JSON output path.")
    ap.add_argument("--max-records", type=int, default=0, help="Optional cap (0 = no cap).")
    args = ap.parse_args()

    root = repo_root()
    suite_path = (root / args.suite).resolve()
    suite = load_suite(suite_path)

    fields = _validate_discrete_fields(_parse_csv(str(args.fields)))
    validation_profile = get_validation_profile(args.profile)

    missing: list[str] = []
    dataset_paths: list[Path] = []
    for entry in suite.replays:
        ds_path = dataset_path_for_suite_replay(
            suite_name=suite.name,
            replay_rel_path=entry.replay,
            datasets_dir=str(args.datasets_dir),
        )
        if not ds_path.exists():
            missing.append(str(ds_path.relative_to(root)))
        else:
            dataset_paths.append(ds_path)

    if missing:
        print(f"Missing {len(missing)} preprocessed dataset files for suite {suite.name}:")
        for p in missing:
            print(f"  {p}")
        print(
            "This legacy triage command still reads dataset files; normal validation now reads "
            "suite replays directly. Port this command to replay-derived rows before using it."
        )
        raise SystemExit(2)

    print(
        f"suite: {suite.name}  datasets: {len(dataset_paths)}  "
        f"ucf_enabled: {suite.ucf_enabled}  ucf_cardinals_1_0_enabled: {suite.ucf_cardinals_1_0_enabled}"
    )

    results: list[DatasetStreaks] = []
    for ds_path in dataset_paths:
        ds = read_dataset(str(ds_path))
        num_players = int(ds.header["num_players"])
        players = _parse_players(args.players, num_players=num_players)
        print()
        s = _scan_dataset_streaks(
            dataset_path=ds_path,
            ds=ds,
            fields=fields,
            players=players,
            max_records=int(args.max_records),
            ucf_enabled=suite.ucf_enabled,
            ucf_cardinals_1_0_enabled=suite.ucf_cardinals_1_0_enabled,
            profile=validation_profile,
        )
        _print_dataset_summary(root=root, s=s)
        results.append(s)

    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "suite": suite.name,
            "suite_path": str(Path(args.suite)),
            "datasets_dir": str(args.datasets_dir),
            "ucf_enabled": bool(suite.ucf_enabled),
            "ucf_cardinals_1_0_enabled": bool(suite.ucf_cardinals_1_0_enabled),
            "fields": list(fields),
            "profile": validation_profile.name,
            "ignored_lanes": [lane.label for lane in validation_profile.ignored_lanes],
            "players_csv": None if args.players is None else str(args.players),
            "max_records": int(args.max_records),
            "per_dataset": [
                {
                    "dataset": str(Path(s.dataset).resolve().relative_to(root)),
                    "num_records": s.num_records,
                    "max_records_used": s.max_records_used,
                    "players": list(s.players),
                    "best_len": s.best_len,
                    "best_start_record": s.best_start_record,
                    "best_end_record_excl": s.best_end_record_excl,
                    "best_start_seed_frame_id": s.best_start_seed_frame_id,
                    "best_end_ref_frame_id_inclusive": s.best_end_ref_frame_id_inclusive,
                    "streak_histogram": s.streak_histogram,
                    "first_mismatch_field_counts": s.first_mismatch_field_counts,
                    "first_mismatch_field_counts_seeded": s.first_mismatch_field_counts_seeded,
                    "ignored_first_mismatch_field_counts": s.ignored_first_mismatch_field_counts,
                    "ignored_first_mismatch_field_counts_seeded": s.ignored_first_mismatch_field_counts_seeded,
                }
                for s in results
            ],
        }
        args.out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print()
        print(f"wrote: {args.out.resolve().relative_to(root)}")


if __name__ == "__main__":
    main()
