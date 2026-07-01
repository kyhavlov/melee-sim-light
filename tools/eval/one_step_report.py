from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from tools.eval.validation_profile import ValidationProfile, get_validation_profile, scored_lane_count_for_field


@dataclass
class EvalSummary:
    total_records: int
    total_player_frames: int
    total_state_flags: int
    total_item_slots: int
    mismatches: dict[str, int]
    strict_mismatches: dict[str, int]
    ignored_mismatches: dict[str, int]
    profile_name: str
    float_norm_sum: float
    float_norm_count: int


class Reporter:
    def __init__(self, out_path: Path | None = None, *, echo: bool = True) -> None:
        self._fh = None
        self._echo = bool(echo)
        if out_path is not None:
            out_path.parent.mkdir(parents=True, exist_ok=True)
            self._fh = out_path.open("w", encoding="utf-8")

    def print(self, *args) -> None:
        line = " ".join(str(a) for a in args)
        if self._echo:
            print(line)
        if self._fh is not None:
            self._fh.write(line + "\n")

    def close(self) -> None:
        if self._fh is not None:
            self._fh.close()
            self._fh = None


DISCRETE_FIELDS: tuple[str, ...] = (
    "action_id",
    "action_frame",
    "on_ground",
    "facing",
    "stocks",
    "jumps_left",
    "is_dead",
    "hitlag",
    "hitstun",
    "l_cancel",
    "hurtbox_state",
    "ground_id",
    "animation_index",
    "instance_hit_by",
    "instance_id",
    "last_attack_landed",
    "combo_count",
    "last_hit_by",
    "state_flags",
    "item_exists",
    "item_type",
    "item_state",
    "item_owner",
    "item_instance_id",
)


FLOAT_FIELDS: tuple[str, ...] = (
    "pos_x",
    "pos_y",
    "speed_air_x_self",
    "speed_ground_x_self",
    "speed_y_self",
    "speed_x_attack",
    "speed_y_attack",
    "percent",
    "shield_hp",
    "item_pos_x",
    "item_pos_y",
    "item_vel_x",
    "item_vel_y",
)


def _denom_for_field(
    field: str,
    *,
    total_player_frames: int,
    total_state_flags: int,
    total_item_slots: int,
) -> int:
    if field == "state_flags":
        return total_state_flags
    if field.startswith("item_"):
        return total_item_slots
    return total_player_frames


def _scored_denom_for_field(
    field: str,
    *,
    total_records: int,
    total_player_frames: int,
    total_state_flags: int,
    total_item_slots: int,
    num_players: int,
    profile: ValidationProfile,
) -> int:
    if field == "state_flags":
        per_record = scored_lane_count_for_field(field, players=num_players, profile=profile)
        return int(total_records) * int(per_record)
    return _denom_for_field(
        field,
        total_player_frames=total_player_frames,
        total_state_flags=total_state_flags,
        total_item_slots=total_item_slots,
    )


def emit_summary_from_native(
    *,
    native_summary: dict,
    reporter: Reporter,
    validation_profile: ValidationProfile,
    print_profile: bool,
    num_records: int,
    total_records: int,
    total_player_frames: int,
    total_state_flags: int,
    total_item_slots: int,
    num_players: int,
) -> EvalSummary:
    mismatches = {
        field: int(value)
        for field, value in zip(DISCRETE_FIELDS, native_summary["mismatches"], strict=True)
    }
    strict_mismatches = {
        field: int(value)
        for field, value in zip(DISCRETE_FIELDS, native_summary["strict_mismatches"], strict=True)
    }
    ignored_mismatches = {lane.label: 0 for lane in validation_profile.ignored_lanes}
    for lane in validation_profile.ignored_lanes:
        if lane.field == "state_flags" and lane.subindex == 4 and lane.bitmask == 0x80:
            ignored_mismatches[lane.label] = int(native_summary["ignored_state_flags_4_0x80"])
    float_metrics = native_summary["float_metrics"]
    float_norm_sum = float(native_summary["float_norm_sum"])
    float_norm_count = int(native_summary["float_norm_count"])
    overall_float_norm = float_norm_sum / float_norm_count if float_norm_count > 0 else 0.0

    if print_profile:
        reporter.print(f"validation.profile: {validation_profile.name}")
        for lane in validation_profile.ignored_lanes:
            reporter.print(
                f"validation.profile.ignored: {lane.label} reason={lane.reason} exception={lane.exception}"
            )
    reporter.print(
        f"Records: {num_records}  Players/scored per record: {num_players}  Total player-frames: {total_player_frames}"
    )
    for k, v in mismatches.items():
        denom = _scored_denom_for_field(
            k,
            total_records=total_records,
            total_player_frames=total_player_frames,
            total_state_flags=total_state_flags,
            total_item_slots=total_item_slots,
            num_players=num_players,
            profile=validation_profile,
        )
        reporter.print(f"mismatch.{k}: {v} / {denom} ({v/denom:.6f})")
    for k, v in ignored_mismatches.items():
        denom = total_player_frames if k.startswith("state_flags[") else 0
        if denom > 0:
            reporter.print(f"ignored_mismatch.{k}: {v} / {denom} ({v/denom:.6f})")
    for k in FLOAT_FIELDS:
        m = float_metrics[k]
        reporter.print(
            f"err.{k}: mae={float(m['mae']):.6f} p95={float(m['p95']):.6f} max={float(m['max']):.6f}"
        )

    summary = EvalSummary(
        total_records=total_records,
        total_player_frames=total_player_frames,
        total_state_flags=total_state_flags,
        total_item_slots=total_item_slots,
        mismatches=mismatches,
        strict_mismatches=strict_mismatches,
        ignored_mismatches=ignored_mismatches,
        profile_name=validation_profile.name,
        float_norm_sum=float_norm_sum,
        float_norm_count=float_norm_count,
    )
    mismatches_total, checks_total = discrete_mismatch_total(summary, profile=validation_profile)
    strict_total, strict_checks = strict_discrete_mismatch_total(summary)
    ignored_total = sum(ignored_mismatches.values())
    reporter.print(f"overall.discrete_mismatch: {mismatches_total} / {checks_total}")
    reporter.print(f"overall.strict_discrete_mismatch: {strict_total} / {strict_checks}")
    reporter.print(f"overall.ignored_discrete_mismatch: {ignored_total}")
    reporter.print(f"overall.float_norm_mae_p95: {overall_float_norm:.8f}")
    return summary


def discrete_mismatch_total(summary: EvalSummary, *, profile: ValidationProfile | None = None) -> tuple[int, int]:
    profile = get_validation_profile(summary.profile_name) if profile is None else profile
    num_players = int(summary.total_player_frames // summary.total_records) if summary.total_records > 0 else 0
    total_checks = 0
    total_mismatches = 0
    for k, v in summary.mismatches.items():
        denom = _scored_denom_for_field(
            k,
            total_records=summary.total_records,
            total_player_frames=summary.total_player_frames,
            total_state_flags=summary.total_state_flags,
            total_item_slots=summary.total_item_slots,
            num_players=num_players,
            profile=profile,
        )
        total_checks += denom
        total_mismatches += v
    return total_mismatches, total_checks


def strict_discrete_mismatch_total(summary: EvalSummary) -> tuple[int, int]:
    total_checks = 0
    total_mismatches = 0
    for k, v in summary.strict_mismatches.items():
        denom = _denom_for_field(
            k,
            total_player_frames=summary.total_player_frames,
            total_state_flags=summary.total_state_flags,
            total_item_slots=summary.total_item_slots,
        )
        total_checks += denom
        total_mismatches += v
    return total_mismatches, total_checks
