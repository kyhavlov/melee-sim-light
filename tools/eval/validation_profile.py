from __future__ import annotations

"""Validation scoring profiles.

Profiles affect report scoring only. They do not change simulator output, compare dtypes, dataset
schemas, or replay-real locks.
"""

from dataclasses import dataclass

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE


@dataclass(frozen=True)
class IgnoredLane:
    field: str
    subindex: int
    reason: str
    exception: str

    @property
    def label(self) -> str:
        return f"{self.field}[{self.subindex}]" if self.subindex >= 0 else self.field


@dataclass(frozen=True)
class ValidationProfile:
    name: str
    ignored_lanes: tuple[IgnoredLane, ...]

    def is_ignored(self, field: str, subindex: int) -> bool:
        return any(lane.field == field and lane.subindex == subindex for lane in self.ignored_lanes)

    def ignored_label_for(self, field: str, subindex: int) -> str | None:
        for lane in self.ignored_lanes:
            if lane.field == field and lane.subindex == subindex:
                return lane.label
        return None


STRICT_PROFILE = ValidationProfile(name="strict", ignored_lanes=())

RL1_GAMEPLAY_PROFILE = ValidationProfile(
    name="rl1_gameplay",
    ignored_lanes=(
        IgnoredLane(
            field="state_flags",
            subindex=4,
            reason=(
                "Slippi fp+0x221F camera-subject visibility byte. The current RL1 target consumes "
                "gameplay state, collision, damage, hitlag, shield, item, source, and identity "
                "lanes, but not magnifying-glass/camera-box rendering."
            ),
            exception=(
                "fp+0x221F bit3 gates source-clear timer derivation during dataset construction; "
                "that semantic seed lane remains scored separately. Only the raw compare byte is "
                "ignored for validation totals/rollout first-break ranking."
            ),
        ),
    ),
)

_PROFILES = {
    STRICT_PROFILE.name: STRICT_PROFILE,
    RL1_GAMEPLAY_PROFILE.name: RL1_GAMEPLAY_PROFILE,
}


def validation_profile_names() -> tuple[str, ...]:
    return tuple(sorted(_PROFILES))


def get_validation_profile(name: str | ValidationProfile | None) -> ValidationProfile:
    if isinstance(name, ValidationProfile):
        return name
    key = "rl1_gameplay" if name is None or str(name).strip() == "" else str(name).strip()
    try:
        return _PROFILES[key]
    except KeyError as e:
        choices = ", ".join(validation_profile_names())
        raise ValueError(f"unknown validation profile {key!r}; choices: {choices}") from e


def discrete_field_lane_count(field: str, *, players: int, dtype: np.dtype = COMPARE_DTYPE) -> int:
    field_dtype = dtype.fields[field][0]
    shape = field_dtype.shape
    if shape == ():
        return 1
    if len(shape) == 1:
        return int(players)
    return int(players) * int(np.prod(shape[1:]))


def ignored_lane_count_for_field(
    field: str,
    *,
    players: int,
    profile: ValidationProfile,
    dtype: np.dtype = COMPARE_DTYPE,
) -> int:
    field_dtype = dtype.fields[field][0]
    shape = field_dtype.shape
    if shape == ():
        return 1 if profile.is_ignored(field, -1) else 0
    if len(shape) == 1:
        return int(players) if profile.is_ignored(field, -1) else 0
    sub_count = int(np.prod(shape[1:]))
    ignored_subs = sum(1 for sub in range(sub_count) if profile.is_ignored(field, sub))
    return int(players) * int(ignored_subs)


def scored_lane_count_for_field(
    field: str,
    *,
    players: int,
    profile: ValidationProfile,
    dtype: np.dtype = COMPARE_DTYPE,
) -> int:
    return discrete_field_lane_count(field, players=players, dtype=dtype) - ignored_lane_count_for_field(
        field, players=players, profile=profile, dtype=dtype
    )
