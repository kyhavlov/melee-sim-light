from __future__ import annotations

"""Validate one Slippi replay directly from its `.slp` path.

Examples:
  uv run python -m tools.eval.validate_replay --replay /path/to/game.slp --mode one-step
  uv run python -m tools.eval.validate_replay --replay /path/to/game.slp --mode rollout
"""

import argparse
from pathlib import Path

from tools.eval.rollout_metrics import summarize_rollout_payload
from tools.eval.run_longest_rollout_streaks import (
    _parse_csv,
    _parse_players,
    _scan_dataset_streaks,
    _validate_discrete_fields,
)
from tools.eval.run_one_step_eval import Reporter, evaluate_dataset
from tools.eval.validation_profile import get_validation_profile, validation_profile_names
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp


def _parse_ports(value: str | None) -> list[int] | None:
    if value is None or value.strip() == "":
        return None
    ports: list[int] = []
    for part in value.split(","):
        part = part.strip()
        if part == "":
            continue
        try:
            port = int(part)
        except ValueError as exc:
            raise SystemExit(f"error: invalid --ports entry {part!r} (want 1-based port numbers)") from exc
        if port < 1 or port > 4:
            raise SystemExit(f"error: --ports entry {port} is outside 1..4")
        ports.append(port)
    if not ports:
        return None
    return sorted(set(ports))


def _dataset_label(replay: Path) -> Path:
    return replay.with_suffix(".msl")


def _print_one_step(
    *,
    replay: Path,
    ports: list[int] | None,
    ucf_enabled: bool,
    ucf_cardinals_1_0_enabled: bool,
    chunk: int,
    profile: str,
) -> None:
    dataset = build_dataset_from_slp(
        slp_path=str(replay),
        ports=ports,
        ucf_enabled=ucf_enabled,
        ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
    )
    evaluate_dataset(
        dataset_path=_dataset_label(replay),
        dataset=dataset,
        chunk=int(chunk),
        profile=profile,
        ucf_enabled=ucf_enabled,
        ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
        reporter=Reporter(),
    )


def _streak_payload(s) -> dict:
    return {
        "dataset": str(s.dataset),
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


def _print_rollout(
    *,
    replay: Path,
    ports: list[int] | None,
    ucf_enabled: bool,
    ucf_cardinals_1_0_enabled: bool,
    fields: tuple[str, ...],
    players_csv: str | None,
    max_records: int,
    profile: str,
) -> None:
    dataset = build_dataset_from_slp(
        slp_path=str(replay),
        ports=ports,
        ucf_enabled=ucf_enabled,
        ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
    )
    num_players = int(dataset.header["num_players"])
    players = _parse_players(players_csv, num_players=num_players)
    label = _dataset_label(replay)
    streaks = _scan_dataset_streaks(
        dataset_path=label,
        ds=dataset,
        fields=fields,
        players=players,
        max_records=int(max_records),
        ucf_enabled=ucf_enabled,
        ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
        profile=profile,
    )
    payload = {
        "suite": "single_replay",
        "suite_path": str(replay),
        "datasets_dir": "",
        "ucf_enabled": bool(ucf_enabled),
        "ucf_cardinals_1_0_enabled": bool(ucf_cardinals_1_0_enabled),
        "fields": list(fields),
        "profile": profile,
        "ignored_lanes": [lane.label for lane in get_validation_profile(profile).ignored_lanes],
        "players_csv": None if players_csv is None else str(players_csv),
        "max_records": int(max_records),
        "per_dataset": [_streak_payload(streaks)],
    }
    summary = summarize_rollout_payload(payload)
    suite_summary = summary["suite_summary"]

    print(
        f"suite: single_replay  datasets: 1  "
        f"ucf_enabled: {ucf_enabled}  ucf_cardinals_1_0_enabled: {ucf_cardinals_1_0_enabled}"
    )
    print(f"fields: {','.join(str(x) for x in summary['fields'])}")
    print(f"validation.profile: {profile}")
    for lane in get_validation_profile(profile).ignored_lanes:
        print(f"validation.profile.ignored: {lane.label} reason={lane.reason} exception={lane.exception}")

    for row in summary["dataset_summaries"]:
        print()
        print(f"== {row['dataset']} ==")
        print(f"rollout.best_len: {row['best_len']}")
        print(f"rollout.streak_count: {row['total_streaks']}")
        print(f"rollout.streak_len.median: {row['median_streak_len']}")
        print(f"rollout.streak_len.p90: {row['p90_streak_len']}")
        print(f"rollout.streak_len.p95: {row['p95_streak_len']}")
        print(f"rollout.streak_len.max: {row['max_streak_len']}")
        print(f"rollout.first_mismatch_total: {row['first_mismatch_total']}")
        print(f"rollout.first_mismatch_seeded_total: {row['first_mismatch_seeded_total']}")
        print(f"rollout.first_mismatch_non_seeded_total: {row['first_mismatch_non_seeded_total']}")
        ignored = dict(row.get("ignored_first_mismatch_field_counts", {}))
        if ignored:
            print(
                "rollout.ignored_first_mismatch_top:",
                " ".join(f"{k}:{v}" for k, v in sorted(ignored.items(), key=lambda kv: (-kv[1], kv[0]))[:8]),
            )

    print()
    print("== suite summary ==")
    print(f"overall.rollout.streak_count: {suite_summary['total_streaks']}")
    print(f"overall.rollout.streak_len.median: {suite_summary['median_streak_len']}")
    print(f"overall.rollout.streak_len.p90: {suite_summary['p90_streak_len']}")
    print(f"overall.rollout.streak_len.p95: {suite_summary['p95_streak_len']}")
    print(f"overall.rollout.streak_len.max: {suite_summary['max_streak_len']}")
    print(f"overall.rollout.best_len.max: {suite_summary['max_best_len']}")
    print(f"overall.rollout.first_mismatch_total: {suite_summary['first_mismatch_total']}")
    print(f"overall.rollout.first_mismatch_seeded_total: {suite_summary['first_mismatch_seeded_total']}")
    print(f"overall.rollout.first_mismatch_non_seeded_total: {suite_summary['first_mismatch_non_seeded_total']}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Validate one .slp replay without writing validation reports.")
    ap.add_argument("--replay", required=True, type=Path, help="Path to the .slp replay.")
    ap.add_argument("--mode", choices=("one-step", "rollout", "both"), default="one-step")
    ap.add_argument("--ports", default=None, help="Comma-separated 1-based ports; default uses human ports.")
    ap.add_argument("--chunk", type=int, default=4096, help="One-step batch size.")
    ap.add_argument(
        "--fields",
        default="action_id,animation_index,on_ground,hitlag,hitstun,state_flags",
        help="Rollout discrete fields.",
    )
    ap.add_argument("--players", default=None, help="Rollout 0-based player indices; default all selected players.")
    ap.add_argument("--max-records", type=int, default=0, help="Rollout record cap; 0 means no cap.")
    ap.add_argument("--profile", default="rl1_gameplay", choices=validation_profile_names())
    ap.add_argument("--ucf-enabled", action="store_true", default=True)
    ap.add_argument("--no-ucf-enabled", dest="ucf_enabled", action="store_false")
    ap.add_argument("--ucf-cardinals-1-0-enabled", action="store_true", default=False)
    ap.add_argument(
        "--no-ucf-cardinals-1-0-enabled",
        dest="ucf_cardinals_1_0_enabled",
        action="store_false",
    )
    args = ap.parse_args()

    replay = args.replay.expanduser().resolve()
    if not replay.exists():
        raise SystemExit(f"error: replay does not exist: {replay}")
    ports = _parse_ports(args.ports)
    fields = _validate_discrete_fields(_parse_csv(str(args.fields)))

    if args.mode in ("one-step", "both"):
        _print_one_step(
            replay=replay,
            ports=ports,
            ucf_enabled=bool(args.ucf_enabled),
            ucf_cardinals_1_0_enabled=bool(args.ucf_cardinals_1_0_enabled),
            chunk=int(args.chunk),
            profile=str(args.profile),
        )
    if args.mode == "both":
        print()
    if args.mode in ("rollout", "both"):
        _print_rollout(
            replay=replay,
            ports=ports,
            ucf_enabled=bool(args.ucf_enabled),
            ucf_cardinals_1_0_enabled=bool(args.ucf_cardinals_1_0_enabled),
            fields=fields,
            players_csv=args.players,
            max_records=int(args.max_records),
            profile=str(args.profile),
        )


if __name__ == "__main__":
    main()
