from __future__ import annotations

from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tests.replay_buffers_loader import load_replay_buffers


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _dataset(path: str) -> Path:
    root = _root()
    ds_path = root / path
    if not ds_path.exists():
        pytest.skip(f"missing local replay: {path}")
    return ds_path


def _rows(ds_path: Path, rows: list[int]):
    ds = load_replay_buffers(str(ds_path))
    for rec in rows:
        assert int(ds.rows.shape[0]) > rec, f"replay too short for row {rec}"
    return ds.rows


@pytest.mark.integration
def test_dash_catchdash_iasa_qgd_replay_real_lock() -> None:
    # Replay-real lock for the pre-guard Dash_IASA CatchDash bridge in src/locomotion.c.
    #
    # Decomp:
    # - Dash_IASA calls ftCo_800D8A38 before guard-owned paths.
    # - ftCo_800D8A38 requires held L/R + pressed-edge A and enters CatchDash via ftCo_800D8C54.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D8A38,ftCo_800D8C54}
    ds_path = _dataset(
        "replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.slpz"
    )
    samples = _rows(ds_path, [5356, 5357, 5358, 5359])
    p = 0

    target = samples[5358 : 5359]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    assert int(seed["action_id"][0, p]) == 20
    assert int(ref["action_id"][0, p]) == 214
    assert int(target["prev_input_t"][0]["p"]["buttons"][p]) == 0
    assert int(target["input_t"][0]["p"]["buttons"][p]) & 0x0010
    assert int(target["input_t"][0]["p"]["r"][p]) == 255

    for rec in (5356, 5357, 5358, 5359):
        _, ref_row, out_row = _run_one_step_row(ds_path, rec, p)
        for field in ("action_id", "action_frame", "animation_index", "instance_id"):
            assert int(out_row[field][p]) == int(ref_row[field][p]), f"record={rec} field={field}"
        assert int(out_row["on_ground"][p]) == int(ref_row["on_ground"][p]) == 1
        assert int(out_row["facing"][p]) == int(ref_row["facing"][p]) == 0
        assert int(out_row["jumps_left"][p]) == int(ref_row["jumps_left"][p]) == 2
        assert out_row["state_flags"][p].tolist() == ref_row["state_flags"][p].tolist()


@pytest.mark.integration
def test_attackdash_shine_qgd_replay_real_lock() -> None:
    # Replay-real lock for the grounded AttackDash_IASA -> Wait_IASA -> ftCo_800D68C0 reflector
    # subset in src/locomotion.c / src/shine.c.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
    ds_path = _dataset(
        "replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.slpz"
    )
    samples = _rows(ds_path, [9596, 9598, 9599, 9600])
    p = 0

    target = samples[9599 : 9600]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    assert int(seed["action_id"][0, p]) == 50
    assert int(ref["action_id"][0, p]) == 360
    assert int(target["input_t"][0]["p"]["buttons"][p]) == 0x0200
    assert int(target["input_t"][0]["p"]["main_x"][p]) == 0
    assert int(target["input_t"][0]["p"]["main_y"][p]) == -99

    for rec in (9596, 9598, 9600):
        _, ref_row, out_row = _run_one_step_row(ds_path, rec, p)
        for field in ("action_id", "action_frame", "animation_index", "instance_id"):
            assert int(out_row[field][p]) == int(ref_row[field][p]), f"record={rec} field={field}"
        assert int(out_row["on_ground"][p]) == int(ref_row["on_ground"][p]) == 1
        assert int(out_row["facing"][p]) == int(ref_row["facing"][p]) == 1
        assert int(out_row["jumps_left"][p]) == int(ref_row["jumps_left"][p]) == 2
        assert out_row["state_flags"][p].tolist() == ref_row["state_flags"][p].tolist()

    _, ref_row, out_row = _run_one_step_row(ds_path, 9599, p)
    for field in ("action_id", "action_frame", "animation_index", "instance_id"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), f"record=9599 field={field}"
    assert int(out_row["on_ground"][p]) == int(ref_row["on_ground"][p]) == 1
    assert int(out_row["facing"][p]) == int(ref_row["facing"][p]) == 1
    assert int(out_row["jumps_left"][p]) == int(ref_row["jumps_left"][p]) == 2
    # AttackDash_IASA -> Wait_IASA -> ftCo_800D68C0 now preserves the source allow_interrupt bit
    # on the immediate grounded Shine Start destination.
    assert ref_row["state_flags"][p].tolist() == [160, 0, 0, 0, 0]
    assert out_row["state_flags"][p].tolist() == ref_row["state_flags"][p].tolist()


@pytest.mark.integration
def test_escapeair_jump_floor_handoff_agg_replay_real_lock() -> None:
    # Replay-real lock for the immediate JumpF -> EscapeAir ledge-floor handoff in src/mpcoll_ground.c.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{ftCo_80099A58,ftCo_EscapeAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    ds_path = _dataset(
        "replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.slpz"
    )
    samples = _rows(ds_path, [2575, 2576, 2577, 2578])
    p = 0

    target = samples[2577 : 2578]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    assert int(seed["action_id"][0, p]) == 25
    assert int(ref["action_id"][0, p]) == 43
    assert int(target["input_t"][0]["p"]["buttons"][p]) == 0x0040

    for rec in (2575, 2576, 2577, 2578):
        _, ref_row, out_row = _run_one_step_row(ds_path, rec, p)
        for field in ("action_id", "action_frame", "animation_index", "instance_id"):
            assert int(out_row[field][p]) == int(ref_row[field][p]), f"record={rec} field={field}"
        assert int(out_row["on_ground"][p]) == int(ref_row["on_ground"][p])
        assert int(out_row["facing"][p]) == int(ref_row["facing"][p])
        assert int(out_row["jumps_left"][p]) == int(ref_row["jumps_left"][p])
        assert out_row["state_flags"][p].tolist() == ref_row["state_flags"][p].tolist()


@pytest.mark.integration
def test_passivewalljump_specialairs_qgd_replay_real_lock() -> None:
    # Replay-real lock for PassiveWallJump IASA using ftCo_SpecialAir_CheckInput in src/blaster.c.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{
    #   inlineA0,ftCo_PassiveWall_Anim,ftCo_PassiveWall_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    ds_path = _dataset(
        "replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.slpz"
    )
    samples = _rows(ds_path, [9259, 9260, 9261, 9262])
    p = 0

    target = samples[9261 : 9262]
    seed = target["seed_t"]
    ref = target["ref_t1"]
    assert int(seed["action_id"][0, p]) == 203
    assert int(ref["action_id"][0, p]) == 350
    assert int(target["input_t"][0]["p"]["buttons"][p]) == 0x0200
    assert int(target["input_t"][0]["p"]["main_x"][p]) == -100

    for rec in (9259, 9260, 9262):
        _, ref_row, out_row = _run_one_step_row(ds_path, rec, p)
        for field in ("action_id", "action_frame", "animation_index", "instance_id", "facing"):
            assert int(out_row[field][p]) == int(ref_row[field][p]), f"record={rec} field={field}"
        assert int(out_row["on_ground"][p]) == int(ref_row["on_ground"][p]) == 0
        assert int(out_row["jumps_left"][p]) == int(ref_row["jumps_left"][p]) == 0
        assert out_row["state_flags"][p].tolist() == ref_row["state_flags"][p].tolist()

    _, ref_row, out_row = _run_one_step_row(ds_path, 9261, p)
    for field in ("action_id", "action_frame", "animation_index", "instance_id", "facing"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), f"record=9261 field={field}"
    assert int(out_row["on_ground"][p]) == int(ref_row["on_ground"][p]) == 0
    assert int(out_row["jumps_left"][p]) == int(ref_row["jumps_left"][p]) == 0
    assert out_row["state_flags"][p].tolist() == ref_row["state_flags"][p].tolist()
