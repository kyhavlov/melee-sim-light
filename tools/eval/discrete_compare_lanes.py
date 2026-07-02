from __future__ import annotations

"""Precompiled discrete compare lanes for rollout triage tools."""

from dataclasses import dataclass

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tools.eval.validation_profile import ValidationProfile


@dataclass(frozen=True)
class DiscreteCompareLane:
    field: str
    player: int
    subindex: int
    bitmask: int | None = None

    @property
    def label(self) -> str:
        base = f"{self.field}[{self.subindex}]" if self.subindex >= 0 else self.field
        if self.bitmask is None:
            return base
        return f"{base}&0x{self.bitmask:02X}"


@dataclass(frozen=True)
class DiscreteMismatch:
    player: int
    field: str
    subindex: int
    seed: int
    out: int
    ref: int


def compile_discrete_compare_lanes(
    fields: tuple[str, ...],
    players: tuple[int, ...],
    *,
    dtype: np.dtype = COMPARE_DTYPE,
    profile: ValidationProfile | None = None,
    ignored_only: bool = False,
) -> tuple[DiscreteCompareLane, ...]:
    lanes: list[DiscreteCompareLane] = []

    def lane_specs(field: str, subindex: int) -> tuple[int | None, ...]:
        if profile is None:
            return (None,) if not ignored_only else ()
        if ignored_only:
            if profile.is_ignored(field, subindex):
                return (None,)
            ignored_mask = profile.ignored_bitmask_for(field, subindex)
            return (ignored_mask,) if ignored_mask != 0 else ()
        if profile.is_ignored(field, subindex):
            return ()
        ignored_mask = profile.ignored_bitmask_for(field, subindex)
        return ((~ignored_mask) & 0xFFFFFFFF,) if ignored_mask != 0 else (None,)

    for field in fields:
        field_dtype = dtype.fields[field][0]
        shape = field_dtype.shape
        if shape == ():
            for bitmask in lane_specs(field, -1):
                lanes.append(DiscreteCompareLane(field=field, player=-1, subindex=-1, bitmask=bitmask))
        elif len(shape) == 1:
            for bitmask in lane_specs(field, -1):
                lanes.extend(
                    DiscreteCompareLane(field=field, player=int(p), subindex=-1, bitmask=bitmask)
                    for p in players
                )
        else:
            sub_count = int(np.prod(shape[1:]))
            for p in players:
                for sub in range(sub_count):
                    for bitmask in lane_specs(field, sub):
                        lanes.append(
                            DiscreteCompareLane(
                                field=field, player=int(p), subindex=sub, bitmask=bitmask
                            )
                        )
    return tuple(lanes)


def _lane_value(row: np.void, lane: DiscreteCompareLane) -> int:
    value = row[lane.field]
    if lane.player < 0:
        return int(np.asarray(value).item())
    if lane.subindex < 0:
        return int(np.asarray(value[lane.player]).item())
    return int(np.asarray(value[lane.player]).flat[lane.subindex])


def _lane_compare_value(row: np.void, lane: DiscreteCompareLane) -> int:
    value = _lane_value(row, lane)
    if lane.bitmask is None:
        return value
    return value & int(lane.bitmask)


def first_mismatch_field(
    *,
    out_row: np.void,
    ref_row: np.void,
    lanes: tuple[DiscreteCompareLane, ...],
    label_subindex: bool = False,
) -> str | None:
    for lane in lanes:
        if _lane_compare_value(out_row, lane) != _lane_compare_value(ref_row, lane):
            if label_subindex and lane.bitmask is not None:
                return lane.label
            if label_subindex and lane.subindex >= 0:
                return f"{lane.field}[{lane.subindex}]"
            return lane.field
    return None


def first_mismatch_values(
    *,
    seed_row: np.void,
    out_row: np.void,
    ref_row: np.void,
    lanes: tuple[DiscreteCompareLane, ...],
) -> DiscreteMismatch | None:
    for lane in lanes:
        out_v = _lane_compare_value(out_row, lane)
        ref_v = _lane_compare_value(ref_row, lane)
        if out_v != ref_v:
            return DiscreteMismatch(
                player=lane.player,
                field=lane.field,
                subindex=lane.subindex,
                seed=_lane_value(seed_row, lane),
                out=out_v,
                ref=ref_v,
            )
    return None
