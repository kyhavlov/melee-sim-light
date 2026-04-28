from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _run_rollout_window_rows_with_trace,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_rollout_with_seed_mutation(
    dataset_path: Path,
    *,
    start_record: int,
    end_record: int,
    mutate_seed,
) -> dict[int, tuple[np.void, np.void]]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > end_record

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_rows = samples[start_record : start_record + 1]["seed_t"].copy()
    mutate_seed(seed_rows[0])
    seed_bytes = np.frombuffer(seed_rows.tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    out: dict[int, tuple[np.void, np.void]] = {}
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for rec in range(start_record, end_record + 1):
            row = samples[rec : rec + 1]
            prev_input_bytes = np.frombuffer(
                row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).copy().reshape(1, input_stride)
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out[rec] = (
                samples[rec : rec + 1]["ref_t1"][0],
                out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(),
            )
    finally:
        binding.destroy(handle)
    return out


@pytest.mark.integration
def test_dash_attackdash_entry_phys_applies_same_frame_root_velocity() -> None:
    # Dash_IASA enters AttackDash before Phys in source, so the entry frame must already expose
    # `ftCo_AttackDash_Phys -> ft_80085030` root-motion velocity.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{doEnter,ftCo_AttackDash_Phys}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80085030
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _, ref_row, out_row = _run_one_step_row(dataset_path, 2399, 1)
    for field in ("action_id", "action_frame", "animation_index", "on_ground"):
        assert int(out_row[field][1]) == int(ref_row[field][1])
    assert float(out_row["pos_x"][1]) == pytest.approx(float(ref_row["pos_x"][1]), abs=1.0e-6)
    assert float(out_row["speed_ground_x_self"][1]) == pytest.approx(
        float(ref_row["speed_ground_x_self"][1]), abs=1.0e-6
    )
    assert float(out_row["speed_air_x_self"][1]) == pytest.approx(
        float(ref_row["speed_air_x_self"][1]), abs=2.0e-6
    )


@pytest.mark.integration
def test_run_attackdash_entry_phys_applies_same_frame_root_velocity() -> None:
    # Run/RunDirect IASA uses the same AttackDash_SetMv0 entry path as Dash, before current-frame
    # AttackDash Phys. PRH 9954 p1 is a Run->AttackDash entry whose current-frame velocity is needed
    # for the downstream DownBound BODY contact.
    # refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Run.c,ftCo_RunDirect.c}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
    #   ftCo_AttackDash_SetMv0,ftCo_AttackDash_Phys}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    _, ref_row, out_row = _run_one_step_row(dataset_path, 9954, 1)
    assert int(out_row["action_id"][1]) == 50  # AttackDash
    for field in ("action_id", "action_frame", "animation_index", "on_ground"):
        assert int(out_row[field][1]) == int(ref_row[field][1])
    assert float(out_row["pos_x"][1]) == pytest.approx(float(ref_row["pos_x"][1]), abs=1.0e-6)
    assert float(out_row["speed_ground_x_self"][1]) == pytest.approx(
        float(ref_row["speed_ground_x_self"][1]), abs=1.0e-6
    )
    assert float(out_row["speed_air_x_self"][1]) == pytest.approx(
        float(ref_row["speed_air_x_self"][1]), abs=1.0e-6
    )


@pytest.mark.integration
def test_dash_attackdash_entry_phys_rollout_and_adjacent_dash_control() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed_2398 = ds.samples[2398]["seed_t"]
    assert int(seed_2398["action_id"][1]) == 20  # Dash
    assert int(ds.samples[2398]["ref_t1"]["action_id"][1]) == 20  # stays Dash without A press

    rows = _run_rollout_window_rows_with_trace(
        dataset_path,
        start_record=2276,
        window_records=(2398, 2399, 2400),
        rng_damage_fly_roll_gate=None,
        trace_path=root / "reports/triage/dash_attackdash_entry_phys_rollout.tsv",
    )
    for rec, (ref_row, out_row, _site1_count) in rows.items():
        for field in ("action_id", "action_frame", "animation_index", "on_ground"):
            assert int(out_row[field][1]) == int(ref_row[field][1]), f"{rec} {field}"
        assert float(out_row["pos_x"][1]) == pytest.approx(float(ref_row["pos_x"][1]), abs=1.0e-5)
        assert float(out_row["speed_ground_x_self"][1]) == pytest.approx(
            float(ref_row["speed_ground_x_self"][1]), abs=1.0e-6
        )


@pytest.mark.integration
def test_airborne_downbound_x1994_seed_carries_to_grounded_attackdash_pose_boundary() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[2400]["seed_t"]
    defender = 0
    attacker = 1
    assert int(seed["action_id"][defender]) == 191  # DownBound/DownBack family row
    assert int(seed["on_ground"][defender]) == 0
    assert int(seed["hurtbox_state"][defender]) == 0
    assert int(seed["colanim_hit_status_x198c"][defender]) == 1
    assert int(seed["colanim_timer_x1994"][defender]) > 0
    assert int(seed["action_id"][attacker]) == 50  # AttackDash

    # Fighter_8006A360 owns x1994 independently of the post-frame ground bit. The airborne
    # snapshot floors before AttackDash hb1 reaches the downed fighter; the hidden x1994 lane then
    # selects the post-Anim DownBound collision pose, avoiding the replay-false high-hurtcap BODY
    # overlap without suppressing real DownDamage contacts.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B7A4,ftColl_8007B868,ftColl_80076ED8}
    rows = _run_rollout_with_seed_mutation(
        dataset_path, start_record=2400, end_record=2402, mutate_seed=lambda _seed: None
    )
    ref_row, out_row = rows[2402]
    for p in (defender, attacker):
        for field in ("action_id", "action_frame", "animation_index", "on_ground", "hitlag", "hitstun"):
            assert int(out_row[field][p]) == int(ref_row[field][p]), f"{field} p={p}"
    assert float(out_row["pos_x"][defender]) == pytest.approx(
        float(ref_row["pos_x"][defender]), abs=1.0e-6
    )

    def _clear_x1994(seed_row: np.void) -> None:
        seed_row["colanim_timer_x1994"][defender] = np.uint16(0)

    poisoned = _run_rollout_with_seed_mutation(
        dataset_path, start_record=2400, end_record=2402, mutate_seed=_clear_x1994
    )
    _ref_poisoned, out_poisoned = poisoned[2402]
    assert int(out_poisoned["action_id"][defender]) == 90  # DamageFlyTop
    assert int(out_poisoned["hitlag"][defender]) > 0
    assert int(out_poisoned["hitlag"][attacker]) > 0


@pytest.mark.integration
def test_grounded_downbound_x1994_keeps_current_pose_for_body_contact() -> None:
    # The x1994 post-Anim pose bridge is a floor-contact handoff: an airborne DownBound snapshot
    # can land before collision and then needs the post-Anim collision pose. Already-grounded
    # DownBound rows are not in that same callback handoff and must keep the current collision pose
    # so real downed BODY contacts still land.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
    #   ftCo_DownBound_Anim,ftCo_DownBound_Coll}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[1555]["seed_t"]
    defender = 1
    attacker = 0
    assert int(seed["action_id"][defender]) == 191  # DownBoundD
    assert int(seed["on_ground"][defender]) == 1
    assert int(seed["hurtbox_state"][defender]) == 0
    assert int(seed["colanim_hit_status_x198c"][defender]) == 1
    assert int(seed["colanim_timer_x1994"][defender]) > 0
    assert int(seed["action_id"][attacker]) == 57  # AttackLw3

    _, ref_row, out_row = _run_one_step_row(dataset_path, 1555, defender)
    for p in (defender, attacker):
        for field in ("action_id", "animation_index", "on_ground", "hitlag", "hitstun"):
            assert int(out_row[field][p]) == int(ref_row[field][p]), f"{field} p={p}"
    assert int(out_row["instance_hit_by"][defender]) == int(ref_row["instance_hit_by"][defender])
    assert int(out_row["last_attack_landed"][attacker]) == int(ref_row["last_attack_landed"][attacker])


@pytest.mark.integration
def test_run_attackdash_entry_phys_downbound_contact_rollout_boundary() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[9954]["seed_t"]
    defender = 0
    attacker = 1
    assert int(seed["action_id"][defender]) == 191  # DownBoundD
    assert int(seed["action_id"][attacker]) == 21  # Run

    rows = _run_rollout_with_seed_mutation(
        dataset_path, start_record=9954, end_record=9958, mutate_seed=lambda _seed: None
    )
    for rec, (ref_row, out_row) in rows.items():
        for p in (defender, attacker):
            for field in ("action_id", "action_frame", "animation_index", "on_ground", "hitlag", "hitstun"):
                assert int(out_row[field][p]) == int(ref_row[field][p]), f"{rec} {field} p={p}"
            assert float(out_row["pos_x"][p]) == pytest.approx(
                float(ref_row["pos_x"][p]), abs=1.0e-5
            )
