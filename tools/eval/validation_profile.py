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
    bitmask: int | None
    reason: str
    exception: str

    @property
    def label(self) -> str:
        base = f"{self.field}[{self.subindex}]" if self.subindex >= 0 else self.field
        if self.bitmask is None:
            return base
        return f"{base}&0x{self.bitmask:02X}"


@dataclass(frozen=True)
class ValidationProfile:
    name: str
    ignored_lanes: tuple[IgnoredLane, ...]

    def is_ignored(self, field: str, subindex: int) -> bool:
        return any(
            lane.field == field and lane.subindex == subindex and lane.bitmask is None
            for lane in self.ignored_lanes
        )

    def ignored_bitmask_for(self, field: str, subindex: int) -> int:
        mask = 0
        for lane in self.ignored_lanes:
            if lane.field == field and lane.subindex == subindex and lane.bitmask is not None:
                mask |= int(lane.bitmask)
        return mask & 0xFFFFFFFF

    def ignored_label_for(self, field: str, subindex: int) -> str | None:
        for lane in self.ignored_lanes:
            if lane.field == field and lane.subindex == subindex and lane.bitmask is None:
                return lane.label
        return None

    def scored_value(self, field: str, subindex: int, value: int) -> int:
        if self.is_ignored(field, subindex):
            return 0
        ignored_mask = self.ignored_bitmask_for(field, subindex)
        if ignored_mask == 0:
            return int(value)
        return int(value) & (~ignored_mask & 0xFFFFFFFF)

    def ignored_value(self, field: str, subindex: int, value: int) -> int:
        if self.is_ignored(field, subindex):
            return int(value)
        ignored_mask = self.ignored_bitmask_for(field, subindex)
        if ignored_mask == 0:
            return 0
        return int(value) & ignored_mask

    def scored_values_differ(self, field: str, subindex: int, out_value: int, ref_value: int) -> bool:
        if self.is_ignored(field, subindex):
            return False
        return self.scored_value(field, subindex, out_value) != self.scored_value(
            field, subindex, ref_value
        )

    def ignored_values_differ(self, field: str, subindex: int, out_value: int, ref_value: int) -> bool:
        if self.is_ignored(field, subindex):
            return int(out_value) != int(ref_value)
        ignored_mask = self.ignored_bitmask_for(field, subindex)
        if ignored_mask == 0:
            return False
        return (int(out_value) & ignored_mask) != (int(ref_value) & ignored_mask)


STRICT_PROFILE = ValidationProfile(name="strict", ignored_lanes=())

RL1_GAMEPLAY_PROFILE = ValidationProfile(
    name="rl1_gameplay",
    ignored_lanes=(
        IgnoredLane(
            field="state_flags",
            subindex=4,
            bitmask=0x80,
            reason=(
                "Slippi fp+0x221F camera-subject / magnifying-glass visibility bit. The current "
                "RL1 target consumes gameplay state, collision, damage, hitlag, shield, item, "
                "source, identity lanes, and the other fp+0x221F gameplay-adjacent bits, but not "
                "camera-box rendering visibility."
            ),
            exception=(
                "Only bit 0x80 of the raw compare byte is ignored for validation totals/rollout "
                "first-break ranking. Other state_flags[4] bits, including x221F_b1/x221F_b3, "
                "remain scored."
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
