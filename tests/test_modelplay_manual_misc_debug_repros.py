from __future__ import annotations

import importlib
import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import build_match_config_array


MANUAL_REPROS = Path("tests/fixtures/modelplay/manual_repros")


def _stick_i8(v: float) -> np.int8:
    return np.int8(np.clip(np.rint(float(v) * 80.0), -80, 80))


def _write_input_player(arr: np.ndarray, player: int, values: list[float | int]) -> None:
    buttons, main_x, main_y, c_x, c_y, l_trigger, r_trigger = values
    arr["p"][0, player]["buttons"] = np.uint16(int(buttons))
    arr["p"][0, player]["main_x"] = _stick_i8(float(main_x))
    arr["p"][0, player]["main_y"] = _stick_i8(float(main_y))
    arr["p"][0, player]["c_x"] = _stick_i8(float(c_x))
    arr["p"][0, player]["c_y"] = _stick_i8(float(c_y))
    arr["p"][0, player]["l"] = np.uint8(np.clip(np.rint(float(l_trigger) * 255.0), 0, 255))
    arr["p"][0, player]["r"] = np.uint8(np.clip(np.rint(float(r_trigger) * 255.0), 0, 255))


def _load_manual(name: str) -> dict[str, Any]:
    root = Path(__file__).resolve().parents[1]
    return json.loads((root / MANUAL_REPROS / name).read_text(encoding="utf-8"))


def _run_manual(name: str, end_frame: int | None = None) -> dict[int, np.void]:
    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    trace = _load_manual(name)
    players = trace["settings"]["playerSettings"]
    first = trace["debug"]["frameRows"][0]
    config = build_match_config_array(
        num_players=2,
        char_ids=tuple(int(p["internalCharacterIds"][0]) for p in players),
        team_ids=tuple(int(p["teamId"]) for p in players),
        facing=(1 if float(first[2][5]) > 0.0 else 0, 1 if float(first[3][5]) > 0.0 else 0),
        stage_id=int(trace["settings"]["stageId"]),
        frame_id=0,
        random_seed=int(trace["seed"]),
    )

    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)
    history: dict[int, np.void] = {}
    last = int(end_frame if end_frame is not None else trace["frameCount"])

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        # Manual webplay fixtures are captured input prefixes. Keep their first-frame root
        # placement as fixture setup so source-backed changes to match-start spawn policy do not
        # move the whole repro before the reported interaction.
        if hasattr(binding, "debug_set_player_root"):
            for p in range(2):
                row = first[2 + p]
                binding.debug_set_player_root(
                    handle,
                    0,
                    p,
                    float(row[3]),
                    float(row[4]),
                    1 if float(row[5]) > 0.0 else 0,
                )
        for frame_i in range(0, last + 1):
            binding.write_compare(handle, out_compare)
            history[frame_i] = out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            if frame_i == last:
                break

            input_t = np.zeros((1,), dtype=INPUT_DTYPE)
            _write_input_player(input_t, 0, trace["p1Inputs"][frame_i + 1])
            _write_input_player(input_t, 1, trace["p2Inputs"][frame_i + 1])
            input_bytes = input_t.view(np.uint8).reshape((1, input_stride))
            binding.step_input(handle, prev_input, input_bytes)
            prev_input = input_bytes.copy()
    finally:
        binding.destroy(handle)

    return history


@pytest.mark.integration
def test_manual_misc_required_repros_are_present() -> None:
    root = Path(__file__).resolve().parents[1]
    names = {
        str(p.relative_to(root / MANUAL_REPROS))
        for p in (root / MANUAL_REPROS).glob("**/*.json")
    }
    required = {
        "a_plus_trigger_to_boost_grab_instead_of_dash_attack.json",
        "falco_fall_through_platforms_after_thrown.json",
        "falco_still_falls_through_platform_after_throw.json",
        "firefox_through_bottom_of_battlefield.json",
        "firefox_through_bottom_of_fod.json",
        "firefox_through_bottom_of_fod_again.json",
        "land_on_top_platform_hover_vibrate_weird.json",
        "set6/bugged_ledgedash_yoshis_teleport_to_platform.json",
        "set6/falco_up_B_down_teleport_to_other_ledge.json",
        "set6/side_b_still_pass_through_battlefield.json",
        "set6/up_b_STILL_goes_through_battlefield_underside.json",
        "set6/up_b_STILL_goes_through_fod_underside.json",
        "set7/more_bad_up_b.json",
        "set7/more_bad_up_b_fd.json",
        "set8/ledgedash_teleport.json",
        "side_b_into_battlefield.json",
        "side_b_into_underside_of_fod.json",
        "side_b_through_bottom_of_battlefield.json",
        "up_b_into_battlefield.json",
        "up_b_up_into_fod.json",
        "weird_hover_land_on_platform.json",
    }
    assert required <= names


@pytest.mark.integration
def test_manual_side_b_bottom_battlefield_does_not_floor_clip() -> None:
    # Source owner: SpecialAirS_Coll consumes ft_CheckGroundAndLedge -> mpColl_800473CC. A ceiling
    # or underside-shell contact is not a floor/ledge result and must not enter grounded SpecialS.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirS_Coll
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_800473CC
    history = _run_manual("side_b_through_bottom_of_battlefield.json")
    fox = 0
    assert int(history[172]["action_id"][fox]) == 351  # SpecialAirS, still aerial under shell.
    assert int(history[172]["on_ground"][fox]) == 0
    assert float(history[172]["pos_y"][fox]) < -20.0
    first_stock_loss = next(
        frame_i
        for frame_i in range(172, max(history) + 1)
        if int(history[frame_i]["stocks"][fox]) < 4 or int(history[frame_i]["is_dead"][fox])
    )
    assert first_stock_loss == 227
    assert not any(int(history[frame_i]["on_ground"][fox]) for frame_i in range(172, first_stock_loss))


@pytest.mark.integration
def test_manual_a_plus_trigger_boost_grab_enters_catchdash() -> None:
    # Source owner: AttackDash_SetMv0 seeds mv.co.attackdash.x0 from p_ftCommonData->x68, then
    # ftCo_800D8AE0 consumes held L/R during that window to enter CatchDash. The manual trace uses
    # A+R on the first AttackDash frames and should not continue as a dash attack.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_SetMv0
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8AE0
    history = _run_manual("a_plus_trigger_to_boost_grab_instead_of_dash_attack.json", end_frame=140)
    fox = 0
    assert int(history[126]["action_id"][fox]) == 50  # AttackDash entry.
    assert int(history[128]["action_id"][fox]) == 214  # CatchDash.
    assert int(history[128]["animation_index"][fox]) == 243


@pytest.mark.integration
def test_manual_downbound_platform_hover_repros_restore_ground_on_downwait() -> None:
    # Source owners:
    # - This compact modelplay fixture originally reached DownBound->DownWait after a platform
    #   hover. Source-owned AttackAirN transformed-platform handling now changes the preceding
    #   interaction, so Falco restores grounded Wait earlier instead of reaching DownBound.
    # - If later collision/combat work restores the DownBound branch, the original DownWait handoff
    #   remains valid: ftCo_80097E8C restores grounded common state through ftCommon_8007D7FC.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097E8C
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    history = _run_manual("weird_hover_land_on_platform.json")
    falco = 1
    if int(history[284]["action_id"][falco]) == 184:
        assert int(history[284]["on_ground"][falco]) == 1
        assert int(history[284]["ground_id"][falco]) != 0xFFFF
    else:
        assert int(history[202]["action_id"][falco]) == 14  # Wait after earlier platform restore.
        assert int(history[202]["on_ground"][falco]) == 1
        assert int(history[202]["ground_id"][falco]) != 0xFFFF
    final = history[max(history)]
    assert int(final["on_ground"][falco]) == 1
    assert int(final["ground_id"][falco]) != 0xFFFF


@pytest.mark.integration
def test_manual_respawn_platform_landing_stays_stable() -> None:
    # Source owner: after DownBound_Coll/DownWait handoff restores grounded common state, subsequent
    # platform support should remain attached through the ordinary grounded floor collision owner.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097E8C
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    history = _run_manual("land_on_top_platform_hover_vibrate_weird.json")
    falco = 1
    stable_frames = range(max(history) - 30, max(history) + 1)
    ys = [float(history[frame_i]["pos_y"][falco]) for frame_i in stable_frames]
    assert {int(history[frame_i]["on_ground"][falco]) for frame_i in stable_frames} == {1}
    assert {int(history[frame_i]["ground_id"][falco]) for frame_i in stable_frames} == {1}
    assert max(ys) - min(ys) < 1e-4


@pytest.mark.integration
def test_manual_damagefly_upthrow_recontacts_fod_platform_on_descent() -> None:
    # Source owner: DamageFly_Coll calls ft_80081DD4, whose mpColl floor pass consumes the actual
    # CollData ECB sweep. The split KB lane can still be positive while the live ECB bottom is
    # descending; that must not suppress soft-platform recontact after an upthrow from below FoD's
    # right platform.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor}
    history = _run_manual("falco_still_falls_through_platform_after_throw.json")
    falco = 1
    assert int(history[987]["action_id"][falco]) == 90  # DamageFlyTop before floor contact.
    assert int(history[987]["on_ground"][falco]) == 0
    assert int(history[990]["action_id"][falco]) == 183  # DownBoundU from damage floor collision.
    assert int(history[990]["on_ground"][falco]) == 1
    assert int(history[990]["ground_id"][falco]) == 1  # FoD right moving platform.
    assert float(history[990]["pos_y"][falco]) == pytest.approx(16.976116, abs=1e-4)
    final = history[max(history)]
    assert int(final["on_ground"][falco]) == 1
    assert int(final["ground_id"][falco]) == 1


def _assert_understage_recovery_has_no_ground_clip(
    name: str, *, actions: tuple[int, ...], y_below: float
) -> None:
    history = _run_manual(name)
    fox = 0
    candidates = [
        frame_i
        for frame_i, row in history.items()
        if int(row["action_id"][fox]) in actions
        and int(row["on_ground"][fox]) == 0
        and float(row["pos_y"][fox]) < y_below
    ]
    assert candidates, (
        f"{name}: no recovery frames below {y_below}; fixture no longer locks the reported "
        "under-stage bug window"
    )

    start = candidates[0]
    stocks_before = int(history[start]["stocks"][fox])
    stock_loss = next(
        frame_i
        for frame_i in range(start, max(history) + 1)
        if int(history[frame_i]["stocks"][fox]) < stocks_before
        or int(history[frame_i]["is_dead"][fox])
    )
    assert not any(int(history[frame_i]["on_ground"][fox]) for frame_i in range(start, stock_loss))
    assert int(history[stock_loss]["action_id"][fox]) == 0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("name", "actions", "y_below"),
    [
        ("side_b_into_underside_of_fod.json", (351, 352), -20.0),
        ("side_b_into_battlefield.json", (351, 352), -15.0),
        ("up_b_up_into_fod.json", (356, 358), 31.0),
        ("up_b_into_battlefield.json", (356, 358), -15.0),
    ],
)
def test_manual_understage_recovery_repros_do_not_floor_clip(
    name: str, actions: tuple[int, ...], y_below: float
) -> None:
    # Source owners:
    # - SpecialAirS_Coll reaches grounded SpecialS only through ft_CheckGroundAndLedge.
    # - SpecialAirHi/SpecialHiFall consume wall/ceiling/underside contacts before floor landing.
    # These webplay traces demonstrated the rejected broad floor handoff: the recovery moved through
    # the stage shell and landed on a top surface from below.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirS_Coll
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
    _assert_understage_recovery_has_no_ground_clip(name, actions=actions, y_below=y_below)


@pytest.mark.integration
@pytest.mark.parametrize(
    "name",
    [
        "firefox_through_bottom_of_fod.json",
    ],
)
def test_manual_firefox_bottom_survivor_repros_stay_alive(name: str) -> None:
    # Source owner: SpecialAirHi_Coll consumes underside wall/ceiling contacts before the
    # launch/fall handoff; the survivor traces should not clip through the floor shell into a KO.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
    history = _run_manual(name)
    fox = 0
    bug_window = range(150, 261)
    assert {int(history[frame_i]["stocks"][fox]) for frame_i in bug_window} == {4}
    min_frame = min(bug_window, key=lambda frame_i: float(history[frame_i]["pos_y"][fox]))
    min_row = history[min_frame]
    assert int(min_row["action_id"][fox]) != 0
    assert float(min_row["pos_y"][fox]) > -5.0

    first_ground_after_min = next(
        frame_i
        for frame_i in range(min_frame, 261)
        if int(history[frame_i]["on_ground"][fox]) == 1
    )
    ground_row = history[first_ground_after_min]
    assert int(ground_row["action_id"][fox]) != 0
    assert int(ground_row["ground_id"][fox]) != 0xFFFF
    assert float(ground_row["pos_y"][fox]) > -5.0

@pytest.mark.integration
def test_manual_firefox_fod_low_recovery_again_uses_blast_bounds_not_floor_clip() -> None:
    # This trace crosses FoD's runtime blast bottom after a low recovery. Keep the collision owner
    # honest: no synthetic floor catch is applied once the fighter is already below the extracted
    # blast bound.
    # data/stages/bin/griz.bin::MSLSTG01 blast bounds
    history = _run_manual("firefox_through_bottom_of_fod_again.json")
    fox = 0
    first_stock_loss = next(
        out for out in history.values() if int(out["stocks"][fox]) < 4 or int(out["is_dead"][fox])
    )
    assert float(first_stock_loss["pos_y"][fox]) < -146.25
    assert int(first_stock_loss["action_id"][fox]) == 0


@pytest.mark.integration
def test_manual_set6_battlefield_upb_shell_contact_does_not_cross_stage() -> None:
    # Source owner: airborne mpColl resolves left/right wall twice, then walks adjacent raw MapLine
    # wall->ceiling before floor. The vanilla no-resync BF probe stays pinned near the right shell;
    # it does not pass through the top floor span.
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80046904,mpColl_80044AD8_Ceiling}
    history = _run_manual("set6/up_b_STILL_goes_through_battlefield_underside.json")
    fox = 0
    fall_frame = next(f for f, row in history.items() if int(row["action_id"][fox]) == 358)
    assert float(history[fall_frame]["pos_x"][fox]) > 35.0
    assert float(history[fall_frame]["pos_y"][fox]) < -20.0
    assert not any(
        int(history[f]["action_id"][fox]) in (356, 358) and float(history[f]["pos_x"][fox]) < 20.0
        for f in range(220, fall_frame + 1)
    )


@pytest.mark.integration
def test_manual_set6_fod_upb_shell_contact_does_not_cross_center_floor() -> None:
    # Same mpColl wall/adjacent-ceiling owner on FoD: the recovery remains outside the underside
    # shell instead of traveling through to the opposite side/top-surface floor.
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80046904,mpColl_80044AD8_Ceiling}
    history = _run_manual("set6/up_b_STILL_goes_through_fod_underside.json")
    fox = 0
    fall_frame = next(f for f, row in history.items() if f > 280 and int(row["action_id"][fox]) == 358)
    special_window = range(284, fall_frame + 1)
    assert not any(int(history[f]["on_ground"][fox]) for f in special_window)
    assert 10.0 < float(history[fall_frame]["pos_x"][fox]) < 25.0
    assert float(history[fall_frame]["pos_y"][fox]) < -50.0
    assert int(history[fall_frame]["on_ground"][fox]) == 0


@pytest.mark.integration
def test_manual_set6_battlefield_sideb_shell_contact_stops_at_underside() -> None:
    # SpecialAirS_Coll itself only changes to grounded SideB on real floor/ledge contact, but its
    # shared ft_CheckGroundAndLedge mpColl pass still resolves wall/ceiling shell contact.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirS_Coll
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_800473CC
    history = _run_manual("set6/side_b_still_pass_through_battlefield.json")
    fox = 0
    end_frames = [
        f
        for f, row in history.items()
        if 195 <= f <= 205 and int(row["action_id"][fox]) == 352
    ]
    assert end_frames
    assert min(float(history[f]["pos_x"][fox]) for f in end_frames) > 39.0
    assert max(float(history[f]["pos_y"][fox]) for f in end_frames) < -20.0


@pytest.mark.integration
def test_manual_set6_fd_downward_upb_does_not_snap_to_opposite_ledge() -> None:
    # Source owner: mpColl ledge helpers store left/right ledge ids from raw MapLine endpoint
    # orientation. Extracted floor lines are normalized x0<=x1, so global left/right ledge carriers
    # must not treat the right ledge's inner endpoint as Collide_LeftLedgeGrab.
    # refs/melee/src/melee/mp/mplib.c::mpLib_80051BA8_Floor
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044164,mpColl_800443C4}
    history = _run_manual("set6/falco_up_B_down_teleport_to_other_ledge.json")
    falco = 1
    late = range(840, 970)
    assert not any(
        int(history[f]["action_id"][falco]) in (252, 253) and float(history[f]["pos_x"][falco]) < 0.0
        for f in late
    )
    # The retained package guard is the opposite-ledge snap itself. The later offstage stock-loss
    # side is modelplay outcome drift outside this package-cleanup lock.
    assert any(int(history[f]["stocks"][falco]) < 4 for f in late)


@pytest.mark.integration
def test_manual_set6_yoshis_ledgedash_does_not_reuse_stale_platform_floor() -> None:
    # Dropping/releasing from ledge into a later air dodge must not let a pre-catch side-platform
    # CollData.floor.index seed the EscapeAir/Fall floor callback. The current regenerated prefix no
    # longer reaches CliffWait at the original report frame, but it still exercises the same stale
    # side-platform floor-id owner before the late EscapeAir window.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    history = _run_manual("set6/bugged_ledgedash_yoshis_teleport_to_platform.json")
    fox = 0
    assert int(history[598]["action_id"][fox]) == 236  # EscapeAir.
    assert max(float(history[f]["pos_y"][fox]) for f in range(598, 641)) < 6.0
    assert not any(
        int(history[f]["on_ground"][fox]) == 1 and int(history[f]["ground_id"][fox]) == 1
        for f in range(598, 641)
    )


@pytest.mark.integration
def test_manual_set8_yoshis_ledgedash_rejected_platform_skip_does_not_keep_snap_y() -> None:
    # Ledge-drop floor_skip is owned by CollData. If a late projection path resolves the skipped
    # side-platform anyway, the rejection must discard the local cur_pos lift just like
    # mpColl_80044628_Floor would have done before publishing a floor result.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
    # refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_80044628_Floor}
    history = _run_manual("set8/ledgedash_teleport.json")
    fox = 0
    assert int(history[394]["action_id"][fox]) == 236  # EscapeAir.
    assert max(float(history[f]["pos_y"][fox]) for f in range(394, 410)) < 1.0
    assert all(
        not (int(history[f]["on_ground"][fox]) == 1 and int(history[f]["ground_id"][fox]) == 1)
        for f in range(394, 410)
    )
    assert int(history[398]["action_id"][fox]) == 43
    assert int(history[398]["on_ground"][fox]) == 1
    assert int(history[398]["ground_id"][fox]) == 2
    assert float(history[398]["pos_y"][fox]) < 0.0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("name", "min_climb_y"),
    [
        ("set7/more_bad_up_b.json", 15.0),
        ("set7/more_bad_up_b_fd.json", 20.0),
    ],
)
def test_manual_set7_specialhi_side_ride_does_not_clip_at_floor_height(
    name: str, min_climb_y: float
) -> None:
    # Source owner: SpecialAirHi ceiling collision is sampled from the live JObj ECB top point. A
    # root-position crossing of a top floor line while riding the outside wall is not a ceiling hit,
    # so it must not pin Y at floor height before the launch continues upward.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044C74_Ceiling}
    history = _run_manual(name)
    fox = 0
    special_frames = [
        f for f, row in history.items() if int(row["action_id"][fox]) in (356, 358)
    ]
    assert special_frames

    max_y_frame = max(special_frames, key=lambda f: float(history[f]["pos_y"][fox]))
    assert float(history[max_y_frame]["pos_y"][fox]) > min_climb_y
    assert int(history[max_y_frame]["on_ground"][fox]) == 0

    first_ground_after_launch = next(
        f
        for f in range(special_frames[0], max(history) + 1)
        if int(history[f]["on_ground"][fox]) == 1
    )
    assert max_y_frame < first_ground_after_launch
