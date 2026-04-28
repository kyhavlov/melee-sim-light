from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


_PRH = "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"


def _run_rollout_rows(
    dataset_path: Path, start_record: int, rows: tuple[int, ...]
) -> dict[int, tuple[np.void, np.void]]:
    import msl_binding

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)

    handle = msl_binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)
    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        msl_binding.reseed_seed_rollout(handle, seed_bytes)
        out: dict[int, tuple[np.void, np.void]] = {}
        for record in range(int(start_record), int(max(rows)) + 1):
            prev_input_bytes[0, :] = samples_u8[record, prev_off : prev_off + input_stride]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            msl_binding.step_input(handle, prev_input_bytes, input_bytes)
            msl_binding.write_compare(handle, out_compare_bytes)
            if record in rows:
                out[record] = (out_view[0].copy(), samples["ref_t1"][record].copy())
        return out
    finally:
        msl_binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    record: int
    port: int
    note: str
    seed_action: int
    ref_action: int
    ref_action_frame: int
    ref_animation: int
    ref_on_ground: int
    ref_shield_hp: float
    ref_vy: float


_CASES = (
    _Case(
        record=11464,
        port=0,
        note="last Guard hold-drain frame before ShieldBreakFly",
        seed_action=179,
        ref_action=179,
        ref_action_frame=-1,
        ref_animation=0xFFFFFFFF,
        ref_on_ground=1,
        ref_shield_hp=0.08003824949264526,
        ref_vy=0.0,
    ),
    _Case(
        record=11465,
        port=0,
        note="Guard hold-drain crosses zero and enters ShieldBreakFly",
        seed_action=179,
        ref_action=205,
        ref_action_frame=1,
        ref_animation=286,
        ref_on_ground=0,
        ref_shield_hp=0.07000000029802322,
        ref_vy=3.129999876022339,
    ),
    _Case(
        record=11560,
        port=0,
        note="ShieldBreakStandU animation end enters Furafura",
        seed_action=209,
        ref_action=211,
        ref_action_frame=0,
        ref_animation=205,
        ref_on_ground=1,
        ref_shield_hp=30.06999969482422,
        ref_vy=0.0,
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"rec{c.record}_p{c.port}")
def test_shield_hold_drain_enters_shieldbreakfly_only_on_depletion(case: _Case) -> None:
    # Replay-real boundary for held-shield depletion:
    # - ftCo_800925A4 drains shield HP during Guard/GuardOn Anim.
    # - When HP crosses below zero, ftCo_80098B20 enters ShieldBreakFly and ftCo_ShieldBreakFly_Phys
    #   runs the same frame through ft_80084EEC, applying gravity to co_attrs.x94.
    # - The preceding row stays ordinary Guard, so this is not a broad low-shield action shortcut.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakFly.c::{
    #   ftCo_80098B20,ftCo_ShieldBreakFly_Phys}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _PRH
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_PRH}")

    seed_row, ref_row, out_row = _run_one_step_row(dataset_path, case.record, case.port)
    p = case.port
    assert int(seed_row["action_id"][p]) == case.seed_action, case.note
    assert int(ref_row["action_id"][p]) == case.ref_action, case.note
    assert int(ref_row["action_frame"][p]) == case.ref_action_frame, case.note
    assert int(ref_row["animation_index"][p]) == case.ref_animation, case.note
    assert int(ref_row["on_ground"][p]) == case.ref_on_ground, case.note
    assert float(ref_row["shield_hp"][p]) == pytest.approx(case.ref_shield_hp, abs=1e-7)

    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "ground_id",
        "jumps_left",
        "hitlag",
        "hitstun",
    ):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"{case.note}: field={field} expected={int(ref_row[field][p])} "
            f"got={int(out_row[field][p])}"
        )
    for field in ("shield_hp", "pos_y", "speed_y_self", "speed_air_x_self"):
        assert float(out_row[field][p]) == pytest.approx(float(ref_row[field][p]), abs=1e-6), (
            f"{case.note}: field={field} expected={float(ref_row[field][p]):.8f} "
            f"got={float(out_row[field][p]):.8f}"
        )
    assert float(out_row["speed_y_self"][p]) == pytest.approx(case.ref_vy, abs=1e-6)


@pytest.mark.integration
def test_shieldbreakfly_lands_into_downu_during_rollout() -> None:
    # Runtime-positive ShieldBreakFly collision boundary:
    # - The break-entry one-step row proves `ftCo_80098B20`/`ftCommon_8007D5D4` jump ownership.
    # - Continuous rollout must then let ShieldBreakFly_Coll route floor contact through
    #   `ft_80082C74(..., ftCo_80098E3C)` into ShieldBreakDownU, not stay airborne/flying until a
    #   downstream BODY hit changes action.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakFly.c::ftCo_ShieldBreakFly_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakDown.c::ftCo_80098E3C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _PRH
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_PRH}")

    p = 0
    rows = _run_rollout_rows(dataset_path, 11456, (11465, 11504, 11510))
    out_entry, ref_entry = rows[11465]
    out_land, ref_land = rows[11504]
    out_late, ref_late = rows[11510]

    assert int(out_entry["action_id"][p]) == int(ref_entry["action_id"][p]) == 205
    assert int(out_entry["jumps_left"][p]) == int(ref_entry["jumps_left"][p]) == 1

    assert int(out_land["action_id"][p]) == int(ref_land["action_id"][p]) == 207
    assert int(out_land["animation_index"][p]) == int(ref_land["animation_index"][p]) == 288
    assert int(out_land["action_frame"][p]) == int(ref_land["action_frame"][p]) == 0
    assert int(out_land["on_ground"][p]) == int(ref_land["on_ground"][p]) == 1
    assert int(out_land["jumps_left"][p]) == int(ref_land["jumps_left"][p]) == 2

    assert int(out_late["action_id"][p]) == int(ref_late["action_id"][p]) == 207
    assert int(out_late["hitlag"][p]) == int(ref_late["hitlag"][p]) == 0
    assert int(out_late["hitstun"][p]) == int(ref_late["hitstun"][p]) == 0
