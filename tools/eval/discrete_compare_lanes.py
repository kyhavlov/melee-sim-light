from __future__ import annotations

"""Precompiled discrete compare lanes for rollout triage tools."""

from dataclasses import dataclass

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE
from tools.eval.validation_profile import ValidationProfile


@dataclass(frozen=True)
class DiscreteCompareLane:
    field: str
    player: int
    subindex: int


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

    def include_lane(field: str, subindex: int) -> bool:
        ignored = profile.is_ignored(field, subindex) if profile is not None else False
        return ignored if ignored_only else not ignored

    for field in fields:
        field_dtype = dtype.fields[field][0]
        shape = field_dtype.shape
        if shape == ():
            if include_lane(field, -1):
                lanes.append(DiscreteCompareLane(field=field, player=-1, subindex=-1))
        elif len(shape) == 1:
            if include_lane(field, -1):
                lanes.extend(DiscreteCompareLane(field=field, player=int(p), subindex=-1) for p in players)
        else:
            sub_count = int(np.prod(shape[1:]))
            for p in players:
                lanes.extend(
                    DiscreteCompareLane(field=field, player=int(p), subindex=sub) for sub in range(sub_count)
                    if include_lane(field, sub)
                )
    return tuple(lanes)


def _lane_value(row: np.void, lane: DiscreteCompareLane) -> int:
    value = row[lane.field]
    if lane.player < 0:
        return int(np.asarray(value).item())
    if lane.subindex < 0:
        return int(np.asarray(value[lane.player]).item())
    return int(np.asarray(value[lane.player]).flat[lane.subindex])


def first_mismatch_field(
    *,
    out_row: np.void,
    ref_row: np.void,
    lanes: tuple[DiscreteCompareLane, ...],
    label_subindex: bool = False,
) -> str | None:
    for lane in lanes:
        if _lane_value(out_row, lane) != _lane_value(ref_row, lane):
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
        out_v = _lane_value(out_row, lane)
        ref_v = _lane_value(ref_row, lane)
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
