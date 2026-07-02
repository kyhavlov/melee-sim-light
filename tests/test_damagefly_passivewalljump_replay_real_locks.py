from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_damagefly_wall_tech_entry_qgd_replay_real_lock() -> None:
    # Replay-real lock for the kept DamageFlyN -> PassiveWallJump wall-tech subset:
    # - DamageFly_Coll tries ftCo_800C1D38 before floor-tech / DownBound callbacks.
    # - ftCo_800C1D38 upgrades to PassiveWallJump when ftCo_800C1E0C is true.
    # - ftCo_800C1E64 clears hitstun ownership and applies ftColl_8007B760(..., x764) on entry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1D38,ftCo_800C1E0C,ftCo_800C1E64}
    # data/common/ft_common_data.json: colanim_passivewall_x1990_frames
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = 0
    negative_control = 8406
    target_minus_1 = 8403
    target = 8404
    target_plus_1 = 8405
    for record in (negative_control, target_minus_1, target, target_plus_1):
        assert int(samples.shape[0]) > record, f"replay too short for lock row: record={record}"

    seed = samples[target]["seed_t"]
    ref = samples[target]["ref_t1"]
    assert int(seed["action_id"][p]) == 88  # DamageFlyN
    assert int(seed["action_frame"][p]) == 12
    assert int(seed["hurtbox_state"][p]) == 0
    assert int(seed["hitstun"][p]) == 21
    assert int(seed["x680"][p]) == 18
    assert int(seed["x684"][p]) == 119
    assert int(seed["x67E"][p]) == 76
    assert int(ref["action_id"][p]) == 203  # PassiveWallJump
    assert int(ref["action_frame"][p]) == 0
    assert int(ref["animation_index"][p]) == 203
    assert int(ref["hurtbox_state"][p]) == 2
    assert int(ref["hitstun"][p]) == 0

    locked_fields = (
        "action_id",
        "action_frame",
        "animation_index",
        "hurtbox_state",
        "hitstun",
        "instance_id",
        "on_ground",
        "facing",
        "jumps_left",
        "state_flags",
    )
    for record in (negative_control, target_minus_1, target, target_plus_1):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
        for field in locked_fields:
            got = out_row[field][p]
            want = ref_row[field][p]
            if field == "state_flags":
                assert list(map(int, got)) == list(map(int, want)), (
                    f"record={record} p={p} field={field} expected={list(map(int, want))} "
                    f"got={list(map(int, got))}"
                )
            else:
                assert int(got) == int(want), (
                    f"record={record} p={p} field={field} expected={int(want)} got={int(got)}"
                )


@pytest.mark.integration
def test_passivewalljump_timer_hold_and_launch_qgd_replay_real_lock() -> None:
    # Replay-real lock for the full PassiveWallJump startup/launch owner:
    # - ftCo_800C1E64 seeds `mv.co.passivewall.timer = p_ftCommonData->x760`.
    # - ftCo_PassiveWall_Anim decrements that hidden timer each frame, keeps animation frozen while
    #   it is nonzero, then applies wall-jump launch velocity when it reaches 0.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1E64,ftCo_PassiveWall_Anim}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    locked_fields = ("action_id", "action_frame", "pos_x", "pos_y", "speed_air_x_self", "speed_y_self")
    for record in range(8405, 8414):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, 0)
        for field in locked_fields:
            got = out_row[field][0]
            want = ref_row[field][0]
            if field.startswith("pos_") or field.startswith("speed_"):
                assert float(got) == pytest.approx(float(want), abs=1e-4), (
                    f"record={record} p=0 field={field} expected={float(want)} got={float(got)}"
                )
            else:
                assert int(got) == int(want), (
                    f"record={record} p=0 field={field} expected={int(want)} got={int(got)}"
                )


@pytest.mark.integration
def test_passivewalljump_steady_wall_envelope_priceypartialalbatross_lock() -> None:
    # Replay-real positive for PassiveWallJump after the startup timer has expired:
    # - PassiveWall_Coll calls ft_800831CC once mv.co.passivewall.timer is 0.
    # - ft_800831CC's source path runs the airborne mpColl wall envelope before the shared
    #   walljump/ledge post-consumers, so steady PassiveWallJump can still be clamped by the wall
    #   ECB envelope while ordinary Phys only applies fall/friction.
    # - MSLMSO01 marks PassiveWall{Jump} as COMMON_AIR_WALLJUMP_COLL from that callback identity;
    #   this lock covers the former Yoshi's Story row where sim moved by self velocity only.
    #
    # data/motion_state/owners/{fox,falco}.bin::MSLMSO01 class_bits
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{
    #   ftCo_PassiveWall_Anim,ftCo_PassiveWall_Phys,ftCo_PassiveWall_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_80083090_inline}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80046904}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "replays/validation/aggregate_recent/PriceyPartialAlbatross.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    locked_fields = ("action_id", "action_frame", "pos_x", "pos_y", "speed_air_x_self", "speed_y_self")
    for record in range(907, 912):
        seed, ref_row, out_row = _run_one_step_row(dataset_path, record, 0)
        assert int(seed["action_id"][0]) == 203  # PassiveWallJump
        for field in locked_fields:
            got = out_row[field][0]
            want = ref_row[field][0]
            if field.startswith("pos_") or field.startswith("speed_"):
                assert float(got) == pytest.approx(float(want), abs=1e-5), (
                    f"record={record} p=0 field={field} expected={float(want)} got={float(got)}"
                )
            else:
                assert int(got) == int(want), (
                    f"record={record} p=0 field={field} expected={int(want)} got={int(got)}"
                )


@pytest.mark.integration
def test_passivewall_timer_allows_specialairs_after_hold_distinctcaringcobra_lock() -> None:
    # Replay-real lock for PassiveWall startup hold on the non-jump wall-tech branch:
    # - the startup timer still freezes PassiveWall action_frame at 0,
    # - once the hidden timer reaches 0, grounded/airborne IASA can take the current-frame
    #   SpecialAir path (`350` here) instead of remaining frozen indefinitely.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1E64,ftCo_PassiveWall_Anim,ftCo_PassiveWall_IASA}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "replays/validation/aggregate_recent/DistinctCaringCobra.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    for record in range(4811, 4816):
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, 1)
        assert int(out_row["action_id"][1]) == int(ref_row["action_id"][1]), (
            f"record={record} p=1 expected_action={int(ref_row['action_id'][1])} "
            f"got={int(out_row['action_id'][1])}"
        )
        assert int(out_row["action_frame"][1]) == int(ref_row["action_frame"][1]), (
            f"record={record} p=1 expected_action_frame={int(ref_row['action_frame'][1])} "
            f"got={int(out_row['action_frame'][1])}"
        )


@pytest.mark.integration
def test_passivewall_entry_uses_source_wall_anchor_and_clears_kb_distinctcaringcobra_lock() -> None:
    # Replay-real lock for PassiveWall entry placement after DamageFlyTop wall-tech:
    # - ftCo_800C1E64 snapshots the outgoing wall ECB side before the PassiveWall motion change,
    #   then ft_80081F2C runs the target-state 0xA wall projection.
    # - ftCommon_8007E2FC clears attack/self velocities on entry.
    # - The adjacent pre-Hug row proves the clear/projection is not a broad DamageFly shortcut.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E2FC
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081F2C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / "replays/validation/aggregate_recent/DistinctCaringCobra.slpz"
    if not dataset_path.exists():
        pytest.skip("missing aggregate validation dataset: DistinctCaringCobra.slpz")

    pre_record = 4809
    entry_record = 4810
    p = 1

    _seed, ref_pre, out_pre = _run_one_step_row(dataset_path, pre_record, p)
    assert int(ref_pre["action_id"][p]) == 90  # DamageFlyTop
    assert int(out_pre["action_id"][p]) == int(ref_pre["action_id"][p])
    assert float(out_pre["speed_x_attack"][p]) == pytest.approx(float(ref_pre["speed_x_attack"][p]), abs=1e-6)
    assert float(out_pre["speed_y_attack"][p]) == pytest.approx(float(ref_pre["speed_y_attack"][p]), abs=1e-6)

    seed, ref_entry, out_entry = _run_one_step_row(dataset_path, entry_record, p)
    assert int(seed["action_id"][p]) == 90  # DamageFlyTop
    assert int(seed["mpcoll_wall_kind_seed_u8"][p]) == 1
    assert int(seed["mpcoll_wall_id_seed_u16"][p]) == 13
    assert int(ref_entry["action_id"][p]) == 202  # PassiveWall

    for field in ("action_id", "animation_index", "action_frame", "facing", "hitstun"):
        assert int(out_entry[field][p]) == int(ref_entry[field][p]), (
            f"field={field} expected={int(ref_entry[field][p])} got={int(out_entry[field][p])}"
        )
    for field in ("pos_x", "pos_y", "speed_x_attack", "speed_y_attack", "speed_air_x_self", "speed_y_self"):
        assert float(out_entry[field][p]) == pytest.approx(float(ref_entry[field][p]), abs=1e-5), (
            f"field={field} expected={float(ref_entry[field][p])} got={float(out_entry[field][p])}"
        )


@pytest.mark.integration
def test_passivewalljump_entry_uses_mpcoll_wall_contact_his_lock() -> None:
    # Replay-real lock for DamageFlyTop -> PassiveWallJump placement on the FD lower right wall:
    # - DamageFly_Coll's wall pass writes the source wall-side contact before ftCo_800C1E64
    #   snapshots coll->ecb.left for the PassiveWallJump root snap.
    # - Resampling the outgoing animation ECB side after the wall pass is too late and places the
    #   startup hold 0.416 units to the right, which cascades into the later HIS blastzone death.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_800835B0}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80046904}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / "replays/validation/aggregate_recent/HungryImportantSnake.slpz"
    if not dataset_path.exists():
        pytest.skip("missing aggregate validation dataset: HungryImportantSnake.slpz")

    p = 1
    entry_record = 1789
    seed, ref_entry, out_entry = _run_one_step_row(dataset_path, entry_record, p)
    assert int(seed["action_id"][p]) == 90  # DamageFlyTop
    assert int(seed["mpcoll_wall_kind_seed_u8"][p]) == 2
    assert int(seed["mpcoll_wall_id_seed_u16"][p]) == 9
    assert int(ref_entry["action_id"][p]) == 203  # PassiveWallJump

    for field in ("action_id", "animation_index", "action_frame", "facing", "hitstun", "hurtbox_state"):
        assert int(out_entry[field][p]) == int(ref_entry[field][p]), (
            f"field={field} expected={int(ref_entry[field][p])} got={int(out_entry[field][p])}"
        )
    for field in ("pos_x", "pos_y", "speed_x_attack", "speed_y_attack", "speed_air_x_self", "speed_y_self"):
        assert float(out_entry[field][p]) == pytest.approx(float(ref_entry[field][p]), abs=1e-5), (
            f"field={field} expected={float(ref_entry[field][p])} got={float(out_entry[field][p])}"
        )


@pytest.mark.integration
def test_passivewalljump_current_stick_entry_uses_outgoing_ecb_side_ppa_lock() -> None:
    # Replay-real negative for overusing the replay-seeded wall-contact anchor:
    # - this DamageFlyTop -> PassiveWallJump row has no mpColl wall seed and x67E is expired,
    #   so ftCo_800C1E0C reaches PassiveWallJump from current tap-jump stick instead of the
    #   rewind-wall branch,
    # - ftCo_800C1E64 still snapshots the outgoing ECB side, but the lite wall_contact_x from the
    #   same frame is not the source rewind contact used by the HIS/DCC positives.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1E0C,ftCo_800C1E64}
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_800835B0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / "replays/validation/aggregate_recent/PriceyPartialAlbatross.slpz"
    if not dataset_path.exists():
        pytest.skip("missing aggregate validation dataset: PriceyPartialAlbatross.slpz")

    p = 0
    record = 902
    seed, ref_entry, out_entry = _run_one_step_row(dataset_path, record, p)
    assert int(seed["action_id"][p]) == 90  # DamageFlyTop
    assert int(seed["mpcoll_wall_kind_seed_u8"][p]) == 0
    assert int(seed["mpcoll_wall_id_seed_u16"][p]) == 0xFFFF
    assert int(seed["x67E"][p]) == 0xFF
    assert int(ref_entry["action_id"][p]) == 203  # PassiveWallJump

    for field in ("action_id", "animation_index", "action_frame", "facing", "hitstun", "hurtbox_state"):
        assert int(out_entry[field][p]) == int(ref_entry[field][p]), (
            f"field={field} expected={int(ref_entry[field][p])} got={int(out_entry[field][p])}"
        )
    for field in ("pos_y", "speed_x_attack", "speed_y_attack", "speed_air_x_self", "speed_y_self"):
        assert float(out_entry[field][p]) == pytest.approx(float(ref_entry[field][p]), abs=1e-5), (
            f"field={field} expected={float(ref_entry[field][p])} got={float(out_entry[field][p])}"
        )
    assert abs(float(out_entry["pos_x"][p]) - float(ref_entry["pos_x"][p])) < 0.5


@pytest.mark.integration
def test_passivewalljump_terminal_x1990_clears_hurtbox_state_hvg_lock() -> None:
    # Replay-real lock for PassiveWallJump x198C/x1990 terminal ownership:
    # - ftCo_800C1E64 starts x1990 via ftColl_8007B760(..., p_ftCommonData->x764) on entry.
    # - Fighter_8006A360 decrements x1990 every frame even while mv.co.passivewall.timer freezes
    #   action_frame; the terminal x1990=1 row clears x198C before the post-frame snapshot.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / "replays/validation/aggregate_recent/HilariousVillainousGiraffe.slpz"
    if not dataset_path.exists():
        pytest.skip("missing aggregate validation dataset: HilariousVillainousGiraffe.slpz")

    p = 0
    pre_record = 4680
    terminal_record = 4681
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > terminal_record

    pre_seed = samples[pre_record]["seed_t"]
    assert int(pre_seed["action_id"][p]) == 203  # PassiveWallJump
    assert int(pre_seed["action_frame"][p]) == 7
    assert int(pre_seed["colanim_timer_x1990"][p]) == 2
    _, pre_ref, pre_out = _run_one_step_row(dataset_path, pre_record, p)
    assert int(pre_out["hurtbox_state"][p]) == int(pre_ref["hurtbox_state"][p]) == 2

    seed = samples[terminal_record]["seed_t"]
    ref = samples[terminal_record]["ref_t1"]
    assert int(seed["action_id"][p]) == 203  # PassiveWallJump
    assert int(seed["action_frame"][p]) == 8
    assert int(seed["colanim_timer_x1990"][p]) == 1
    assert int(seed["colanim_hit_status_x198c"][p]) == 2
    assert int(ref["action_id"][p]) == 203
    assert int(ref["action_frame"][p]) == 9
    assert int(ref["hurtbox_state"][p]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, terminal_record, p)
    for field in ("action_id", "action_frame", "animation_index", "hurtbox_state"):
        assert int(out_row[field][p]) == int(ref_row[field][p]), (
            f"field={field} expected={int(ref_row[field][p])} got={int(out_row[field][p])}"
        )


@pytest.mark.integration
def test_common_air_walljump_hidden_phase_seed_qgd_replay_real_lock() -> None:
    # Replay-real positive/negative controls for the common-air walljump hidden phase seed:
    # - Slippi exposes neither `fp->wall_jump_input_timer` nor `fp->x2110_walljumpWallSide`.
    # - The seed lane reconstructs only the late FD wall/underside hidden timer phase; runtime still
    #   requires ftWallJump stick-away and x670 freshness before entering PassiveWallJump.
    # - Earlier generic wall-hug rows keep sentinel seed state and must not be admitted broadly.
    # refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    # refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_800835B0}
    # data/stages/final_destination.json
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = 0
    target = 9218
    for record in (627, 635, target):
        assert int(samples.shape[0]) > record, f"replay too short for lock row: record={record}"

    seed = samples[target]["seed_t"]
    ref = samples[target]["ref_t1"]
    assert int(seed["action_id"][p]) == 27  # JumpAerialF
    assert int(seed["action_frame"][p]) == 17
    assert int(seed["walljump_input_timer"][p]) == 9
    assert int(seed["walljump_wall_side_i8"][p]) == -1
    assert int(ref["action_id"][p]) == 203  # PassiveWallJump

    _, ref_row, out_row = _run_one_step_row(dataset_path, target, p)
    assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p]) == 203
    assert int(out_row["animation_index"][p]) == int(ref_row["animation_index"][p]) == 203

    for record in (627, 635):
        seed = samples[record]["seed_t"]
        assert int(seed["action_id"][p]) == 29  # Fall
        assert int(seed["walljump_input_timer"][p]) == 254
        assert int(seed["walljump_wall_side_i8"][p]) == 0
        _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
        assert int(ref_row["action_id"][p]) != 203
        assert int(out_row["action_id"][p]) == int(ref_row["action_id"][p])


@pytest.mark.integration
def test_common_air_walljump_setup_carry_consumes_fresh_stick_away_feh_lock() -> None:
    # Replay-real lock for source `ftWallJump_8008169C` setup/carry consumption on Dream Land:
    # - Slippi does not expose `wall_jump_input_timer` / `x2110_walljumpWallSide`.
    # - The native seed lane reconstructs the hidden setup from prefix root movement, but only
    #   serializes it on the row where the stick-away admission branch can consume it.
    # - FEH 8797/8798 are neutral-control rows; 8799 is the fresh right-wall stick-away consumer.
    # refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # data/characters/falco.json::walljump_setup_x_delta_threshold
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / "replays/validation/dream_land_recent/FlippantEnchantedHorse.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = load_replay_buffers(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    samples = ds.rows
    p = 1
    for record in (8797, 8798, 8799):
        assert int(samples.shape[0]) > record, f"replay too short for lock row: record={record}"

    for record in (8797, 8798):
        seed = samples[record]["seed_t"]
        ref = samples[record]["ref_t1"]
        assert int(seed["action_id"][p]) == 27  # JumpAerialF
        assert int(seed["walljump_input_timer"][p]) == 254
        assert int(seed["walljump_wall_side_i8"][p]) == 0
        assert int(ref["action_id"][p]) == 27

    seed = samples[8799]["seed_t"]
    ref = samples[8799]["ref_t1"]
    assert int(seed["action_id"][p]) == 27
    assert int(seed["action_frame"][p]) == 16
    assert int(seed["walljump_input_timer"][p]) == 2
    assert int(seed["walljump_wall_side_i8"][p]) == -1
    assert int(samples[8799]["prev_input_t"]["p"][p]["main_x"]) == 0
    assert int(samples[8799]["input_t"]["p"][p]["main_x"]) == 80
    assert int(ref["action_id"][p]) == 203  # PassiveWallJump

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    row = samples[8799:8800]
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, int(sizes["seed"])
    )
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, int(sizes["input"])
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, int(sizes["input"])
    )
    out_bytes = np.empty((1, int(sizes["compare"])), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)
    out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 203
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p]) == 203
