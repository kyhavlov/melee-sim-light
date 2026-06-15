from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _Case:
    record: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(record=3336, note="QGD Ottotto attack family negative control"),
        _Case(record=3337, note="QGD Ottotto attack family target-1"),
        _Case(record=3338, note="QGD Ottotto attack family target"),
        _Case(record=3339, note="QGD Ottotto attack family target+1"),
    ],
)
def test_ottotto_grounded_a_attack_rows_match_replay(case: _Case) -> None:
    # Replay-real lock for the kept Ottotto-only grounded A-attack IASA lane:
    # - ftCo_Ottotto_IASA checks grounded A-attack inputs before jump/dash/turn/walk.
    # - The target row is an Ottotto frame-3 A+forward/down input that should enter AttackS3Lw.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c::ftCo_AttackS3_CheckInput
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_CheckInput
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(case.record)
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    if record == 3338:
        assert int(seed["action_id"][p]) == 245, case.note  # ftCo_MS_Ottotto
        assert int(seed["animation_index"][p]) == 210, case.note  # ftCo_SM_Ottotto
        assert int(ref["action_id"][p]) == 55, case.note  # ftCo_MS_AttackS3Lw
        assert int(ref["animation_index"][p]) == 57, case.note  # ftCo_SM_AttackS3Lw
        assert int(row["input_t"]["p"]["buttons"][0, p]) == 0x0100, case.note  # A edge
        assert int(row["input_t"]["p"]["main_x"][0, p]) == -45, case.note
        assert int(row["input_t"]["p"]["main_y"][0, p]) == -53, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "instance_id",
        "on_ground",
        "facing",
        "jumps_left",
    ):
        got = int(out_row[field][p])
        want = int(ref_row[field][p])
        assert got == want, f"record={record} p={p} field={field} expected={want} got={got}"


@pytest.mark.integration
def test_ottotto_analog_shield_enters_guardon_without_endpoint_gate_feh() -> None:
    # Ottotto analog-shield IASA:
    # ftCo_Ottotto_IASA calls ftCo_80091A4C after attacks and before appeal/jump/dash/turn/walk.
    # Fighter input synthesis maps analog trigger past p_ftCommonData->x10 into the held LR lane, so
    # analog-only shield input must be allowed to reach GuardOn even when the local teeter endpoint
    # helper would otherwise block the guard path. Digital outward LR remains on the platform/drop
    # suppressor in locomotion.c.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800924C0}
    # refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10_Inner1
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/"
        "dream_land_recent/FlippantEnchantedHorse.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 7441
    p = 0
    row = ds.samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == 245  # ftCo_MS_Ottotto
    assert int(row["input_t"]["p"]["buttons"][0, p]) == 0
    assert int(row["input_t"]["p"]["l"][0, p]) > 0
    assert int(ref["action_id"][p]) == 178  # ftCo_MS_GuardOn

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "state_flags",
    ):
        got = out_row[field][p]
        want = ref_row[field][p]
        if field == "state_flags":
            assert got.tolist() == want.tolist(), field
        else:
            assert int(got) == int(want), field


@pytest.mark.integration
def test_ottotto_crouch_iasa_past_endpoint_enters_fall_feh_8688() -> None:
    # Ottotto -> Squat same-proc floor loss:
    # Fighter_8006A360 computes the common xF8 overlap displacement while the source action is
    # still Ottotto. The later ftCo_Ottotto_IASA down-input branch enters Squat, and the same proc's
    # Squat_Coll calls ft_80083F88 -> ft_80082708 -> mpColl_8004B108 against the already-displaced
    # facing endpoint.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::{ftCo_800D5FB0,ftCo_Squat_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/"
        "dream_land_recent/FlippantEnchantedHorse.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 8688
    p = 1
    row = ds.samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == 245  # ftCo_MS_Ottotto
    assert int(seed["action_frame"][p]) == 3
    assert int(row["input_t"]["p"]["main_y"][0, p]) < -64
    assert int(ref["action_id"][p]) == 29  # ftCo_MS_Fall
    assert int(ref["on_ground"][p]) == 0
    assert float(ref["pos_x"][p]) > float(seed["pos_x"][p])

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "pos_x",
        "pos_y",
        "speed_y_self",
    ):
        got = out_row[field][p]
        want = ref_row[field][p]
        if field.startswith("pos_") or field.startswith("speed_"):
            assert float(got) == pytest.approx(float(want), abs=2e-6), field
        else:
            assert int(got) == int(want), field


@pytest.mark.integration
def test_ottotto_crouch_iasa_entry_frame_nudge_enters_fall_wws_8411() -> None:
    # Ottotto -> Squat same-proc floor loss on the first Ottotto callback frame:
    # Fighter_procUpdate applies the frame-start xF8 overlap nudge before Squat_Coll's
    # ft_80083F88 -> ft_80082708 floor check. On this Dream Land platform row, the unnudged root is still
    # exactly at the facing endpoint, but the source nudge crosses it and the callback-visible root
    # Y remains the just-published Ottotto entry root rather than the raw endpoint Y.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::{ftCo_800D5FB0,ftCo_Squat_Coll}
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/marth/replays/validation/marth/WellWornSmallGoshawk.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 8411
    p = 0
    row = ds.samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == 245  # ftCo_MS_Ottotto
    assert int(seed["action_frame"][p]) == 0
    assert int(row["input_t"]["p"]["main_y"][0, p]) < -64
    assert int(ref["action_id"][p]) == 29  # ftCo_MS_Fall
    assert int(ref["on_ground"][p]) == 0
    assert float(ref["pos_x"][p]) < float(seed["pos_x"][p])
    assert float(ref["pos_y"][p]) == pytest.approx(float(seed["pos_y"][p]), abs=2e-6)

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "pos_x",
        "pos_y",
        "speed_y_self",
    ):
        got = out_row[field][p]
        want = ref_row[field][p]
        if field.startswith("pos_") or field.startswith("speed_"):
            assert float(got) == pytest.approx(float(want), abs=2e-6), field
        else:
            assert int(got) == int(want), field


@pytest.mark.integration
def test_ottotto_crouch_iasa_carried_floor_sweep_stays_squat_wws_2145() -> None:
    # Adjacent negative for the WWS Ottotto entry-frame owner. This row has the same down-input
    # Ottotto -> Squat IASA transition, but the replay seed carries a non-current
    # floor_sweep_prev_pos.x from the prior source collision step. The same-proc Squat_Coll path
    # must not recompute and reapply a fresh xF8 nudge here; vanilla publishes Squat for this
    # callback, then the following Squat callback can lose the floor from the current-root sweep.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_Ottotto_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::{ftCo_800D5FB0,ftCo_Squat_Coll}
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "datasets/marth/replays/validation/marth/WellWornSmallGoshawk.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 2145
    p = 0
    row = ds.samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == 245  # ftCo_MS_Ottotto
    assert int(seed["action_frame"][p]) == 0
    assert int(row["input_t"]["p"]["main_y"][0, p]) < -64
    assert int(ref["action_id"][p]) == 39  # ftCo_MS_Squat
    assert int(ref["on_ground"][p]) == 1
    assert float(seed["floor_sweep_prev_pos_x_f32"][p]) != pytest.approx(
        float(seed["pos_x"][p]), abs=1e-5
    )

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "pos_x",
        "pos_y",
    ):
        got = out_row[field][p]
        want = ref_row[field][p]
        if field.startswith("pos_"):
            assert float(got) == pytest.approx(float(want), abs=2e-6), field
        else:
            assert int(got) == int(want), field


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "player", "expected_action"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "LawfulInsistentMeerkat.msl",
            4822,
            1,
            39,
        ),
        (
            "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            3338,
            1,
            55,
        ),
    ],
)
def test_ottotto_crouch_endpoint_owner_does_not_affect_adjacent_controls(
    dataset_rel: str, record: int, player: int, expected_action: int
) -> None:
    # Negatives for the FEH Ottotto -> Squat floor-loss owner:
    # - LIM crouches on the same facing endpoint but does not overrun it, so source stays Squat.
    # - QGD uses the same Ottotto IASA callback but enters AttackS3Lw before crouch/floor-loss.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref, out = _run_one_step_row(dataset_path, record, player)
    assert int(seed["action_id"][player]) == 245  # ftCo_MS_Ottotto
    assert int(ref["action_id"][player]) == expected_action
    assert int(out["action_id"][player]) == int(ref["action_id"][player])
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player])
