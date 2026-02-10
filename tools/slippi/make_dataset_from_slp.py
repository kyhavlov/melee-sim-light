from __future__ import annotations

import argparse
from dataclasses import dataclass

import numpy as np

import pyarrow as pa
from peppi_py import _read_slippi

from tools.eval.dataset import SAMPLE_DTYPE, write_dataset
from tools.slippi.hitstun import hitstun_u16_from_misc_as_and_state_flags3
from tools.slippi.rollback import finalized_frame_indices


@dataclass(frozen=True)
class PortStatic:
    team_id: int
    char_id: int  # start character; per-frame character comes from post


def _to_numpy(arr) -> np.ndarray:
    # pyarrow Array.to_numpy defaults to zero_copy_only=True, which can fail depending on chunking/nulls.
    x = arr.to_numpy(zero_copy_only=False)
    if isinstance(x, np.ma.MaskedArray):
        x = x.filled(0)
    if isinstance(x, np.ndarray) and x.dtype.kind == "f":
        x = np.nan_to_num(x, nan=0.0, posinf=0.0, neginf=0.0)
    return x


def _dir_to_facing(direction: np.ndarray) -> np.ndarray:
    # direction is float: -1 (left) or +1 (right); map to 0/1.
    return (direction > 0).astype(np.uint8)


def _airborne_to_on_ground(airborne: np.ndarray | None, n: int) -> np.ndarray:
    if airborne is None:
        return np.zeros(n, dtype=np.uint8)
    # Slippi: 0 grounded, 1 airborne.
    return (airborne == 0).astype(np.uint8)


def _u8_from_float01(x: np.ndarray) -> np.ndarray:
    x = np.clip(x, 0.0, 1.0)
    return np.round(x * 255.0).astype(np.uint8)


def _int8_from_float_axis(x: np.ndarray) -> np.ndarray:
    # Fallback when raw UCF int8 fields are missing: scale processed [-1,1] float.
    x = np.clip(x, -1.0, 1.0)
    return np.round(x * 127.0).astype(np.int8)


def _stick_i8_from_unit_stick(x: np.ndarray) -> np.ndarray:
    # Slippi schema compatibility:
    # - Newer schemas expose raw_analog_* int8 fields in the UCF-clamped range [-80,80].
    # - Older schemas provide pre.joystick / pre.cstick as float in [-1,1].
    # Map [-1,1] -> [-80,80] using the same stick max constant as the sim (MSL_STICK_MAX_I8=80).
    # Source of truth: src/input_axis.h::MSL_STICK_MAX_I8.
    #
    # Use truncation (toward 0) to mirror C float->int casts for deterministic mapping.
    STICK_MAX_I8 = np.float32(80.0)
    x = np.clip(x, -1.0, 1.0)
    return np.trunc(x * STICK_MAX_I8).astype(np.int8)


def _u16_from_float_frames(x: np.ndarray | None, n: int) -> np.ndarray:
    if x is None:
        return np.zeros(n, dtype=np.uint16)
    x = np.clip(x, 0.0, 65535.0)
    return np.floor(x).astype(np.uint16)


def _i16_from_state_age(state_age: np.ndarray | None, n: int) -> np.ndarray:
    if state_age is None:
        return np.zeros(n, dtype=np.int16)
    # Keep it simple: floor the float (can be fractional in some actions).
    x = np.clip(state_age, -32768.0, 32767.0)
    return np.floor(x).astype(np.int16)


def _f32_from_state_age(state_age: np.ndarray | None, n: int) -> np.ndarray:
    if state_age is None:
        return np.zeros(n, dtype=np.float32)
    # Slippi post-frame "state_age" is fp->cur_anim_frame (float). Keep the fractional component.
    # Source pointers:
    # - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm ("send AS frame", loads from 0x894)
    # - refs/melee/src/melee/ft/types.h (Fighter::cur_anim_frame at fp+894)
    return state_age.astype(np.float32)


def _port_name(port_1based: int) -> str:
    if port_1based < 1 or port_1based > 4:
        raise ValueError(f"port must be in 1..4, got {port_1based}")
    return f"P{port_1based}"

_MATCH_FLOW_ACTION_IDS = {
    # Dead*
    0,  # ftCo_MS_DeadDown
    1,  # ftCo_MS_DeadLeft
    2,  # ftCo_MS_DeadRight
    4,  # ftCo_MS_DeadUpStar
    # Rebirth*
    12,  # ftCo_MS_Rebirth
    13,  # ftCo_MS_RebirthWait
    # Entry*
    322,  # ftCo_MS_Entry
    323,  # ftCo_MS_EntryStart
    324,  # ftCo_MS_EntryEnd
}


def _derive_ledge_cooldown(*, action_id_u16: np.ndarray, hitlag_u16: np.ndarray, common: dict) -> np.ndarray:
    """
    Derive fp->x2064_ledgeCooldown (ledge grab cooldown) from replay action history.

    Decomp shape:
    - Decremented each frame under !hitlag.
      refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    - Set to p_ftCommonData->ledge_cooldown on certain cliff releases (notably CliffWait -> Fall).
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A9AC
    """

    n = int(action_id_u16.shape[0])
    out = np.zeros(n, dtype=np.uint8)
    if n == 0:
        return out

    cooldown_frames = int(common.get("ledge_cooldown_frames", 0))
    cooldown_frames = int(np.clip(cooldown_frames, 0, 255))

    # GALE01 action id ranges:
    # - Cliff states: 252..265 (quick/slow variants)
    # - Fall-like states: 29..38 (Fall..DamageFall)
    CLIFF_MIN = 0x00FC
    CLIFF_MAX = 0x0109
    FALL_MIN = 0x001D
    FALL_MAX = 0x0026

    for t in range(1, n):
        cd = int(out[t - 1])
        if int(hitlag_u16[t - 1]) == 0 and cd > 0:
            cd -= 1

        prev_a = int(action_id_u16[t - 1])
        cur_a = int(action_id_u16[t])
        if CLIFF_MIN <= prev_a <= CLIFF_MAX and FALL_MIN <= cur_a <= FALL_MAX:
            cd = cooldown_frames

        out[t] = np.uint8(cd)

    return out


def _derive_match_flow_timer(*, action_id_u16: np.ndarray, port0: int, common: dict) -> np.ndarray:
    """
    Derive a per-frame decomp-shaped countdown for match-flow states.

    This is required for teacher-forced one-step eval because many match-flow motions do not expose
    a useful per-frame counter in Slippi post-frames (action_frame is often -1).

    Causality:
    - Strictly causal w.r.t. the replay: match_flow_timer[t] depends only on action_id[0..t] and
      decomp/ISO-derived constants (no lookahead).

    Convention:
    - match_flow_timer[t] approximates the fighter's internal match-flow countdown timer (fp->x2340),
      computed from ftCommonData constants and elapsed-in-state (run length so far).
    - It is NOT "remaining until the action ends" in general, because some match-flow states can
      exit early via IASA (e.g. RebirthWait) or other transitions.
    - Values are clamped to 255 and are 0 for non-match-flow action_ids.
    - Only populated for match-flow action_ids (Dead*/Rebirth*/Entry*); 0 for other motions.
    """
    a = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    n = int(a.shape[0])
    out = np.zeros(n, dtype=np.uint8)

    dead_timer = int(common["dead_timer_frames"])
    dead_up_star_initial = int(common["dead_up_star_initial_frames"])
    dead_up_star_phase1 = int(common["dead_up_star_phase1_frames"])
    dead_up_star_phase2 = int(common["dead_up_star_phase2_frames"])
    rebirth_timer = int(common["rebirth_timer_frames"])
    rebirth_wait_timer = int(common["rebirth_wait_timer_frames"])
    entry_start_frames = int(common["entry_start_frames"])
    entry_end_frames = int(common["entry_end_frames"])

    dead_up_star_total = max(0, dead_up_star_initial) + max(0, dead_up_star_phase1) + max(0, dead_up_star_phase2)

    # Match start entry delay is per-port and is driven by a Player "unk4C" counter that is set in
    # increments of 5 during match init, then consumed by ftCo_800C61B0.
    # refs/melee/src/melee/gm/gm_16AE.c::fn_8016D8AC (adds 5, calls Player_SetUnk4C)
    # refs/melee/src/melee/pl/player.c::Player_GetUnk4C (read)
    # refs/melee/src/melee/ft/ft_0C31.c::ftCo_800C61B0 (Entry uses unk4C)
    entry_total = 5 * int(port0 + 1)

    prev_ai: int | None = None
    run_len = 0
    for i in range(n):
        ai = int(a[i])
        if prev_ai is not None and ai == prev_ai:
            run_len += 1
        else:
            prev_ai = ai
            run_len = 1

        total: int | None = None
        if ai == 0 or ai == 1 or ai == 2:
            total = dead_timer
        elif ai == 4:
            total = dead_up_star_total
        elif ai == 12:
            total = rebirth_timer
        elif ai == 13:
            total = rebirth_wait_timer
        elif ai == 322:
            total = entry_total
        elif ai == 323:
            # EntryStart enter sets timer=x6BC then immediately decrements it in Anim before Phys,
            # so the first observable frame has (x6BC - 1) remaining.
            total = max(0, entry_start_frames - 1)
        elif ai == 324:
            total = entry_end_frames

        if total is None or ai not in _MATCH_FLOW_ACTION_IDS:
            continue

        t = total - run_len + 1
        if t < 0:
            t = 0
        if t > 255:
            t = 255
        out[i] = np.uint8(t)

    return out


def _team_id_from_start_player(p: dict) -> int:
    t = p.get("team")
    if t is None:
        return 0
    # Slippi start payload commonly encodes team as {color: int, ...}
    c = t.get("color")
    return int(c) if c is not None else 0


def _fill_items_fixed(frames: pa.StructArray, n_frames: int) -> np.ndarray:
    """
    Convert Slippi frame items (list<struct<...>>) into a fixed-length [n_frames, 15]
    array matching the dataset's ITEM dtype, with a stable ordering.

    Ordering: sort by (instance_id, spawn_id/id, type).
    """
    out = np.zeros((n_frames, 15), dtype=SAMPLE_DTYPE["seed_t"]["items"].base)
    out["owner"] = np.int8(-1)
    # Decomp defaults for cleared/unowned items:
    # - it->xD88_attackID = 1 (FtMoveId_Default, "do not stale")
    # - it->xD8C_attack_instance = 0
    # refs/melee/src/melee/it/it_2725.c::it_8027B1F4
    if "attack_id" in out.dtype.names:
        out["attack_id"] = np.uint16(1)
    if "attack_instance" in out.dtype.names:
        out["attack_instance"] = np.uint16(0)

    if "item" not in {f.name for f in frames.type}:
        return out

    items_list = frames.field("item")

    # Offline preprocessing: simplest correct approach is converting to Python lists.
    items_py = items_list.to_pylist()
    for fi, lst in enumerate(items_py):
        if not lst:
            continue
        lst = sorted(lst, key=lambda it: (int(it["instance_id"]), int(it["id"]), int(it["type"])))
        for slot, it in enumerate(lst[:15]):
            out[fi, slot]["exists"] = np.uint8(1)
            out[fi, slot]["state"] = np.uint8(int(it["state"]))
            out[fi, slot]["type"] = np.uint16(int(it["type"]))
            out[fi, slot]["owner"] = np.int8(int(it.get("owner", -1)))
            out[fi, slot]["instance_id"] = np.uint16(int(it["instance_id"]))
            out[fi, slot]["direction"] = np.float32(float(it["direction"]))
            out[fi, slot]["vel_x"] = np.float32(float(it["velocity"]["x"]))
            out[fi, slot]["vel_y"] = np.float32(float(it["velocity"]["y"]))
            out[fi, slot]["pos_x"] = np.float32(float(it["position"]["x"]))
            out[fi, slot]["pos_y"] = np.float32(float(it["position"]["y"]))
            out[fi, slot]["damage"] = np.uint16(int(it["damage"]))
            out[fi, slot]["timer"] = np.float32(float(it["timer"]))
            out[fi, slot]["spawn_id"] = np.uint32(int(it["id"]))
            misc = it.get("misc") or {}
            out[fi, slot]["misc0"] = np.uint8(int(misc.get("0", 0)))
            out[fi, slot]["misc1"] = np.uint8(int(misc.get("1", 0)))
            out[fi, slot]["misc2"] = np.uint8(int(misc.get("2", 0)))
            out[fi, slot]["misc3"] = np.uint8(int(misc.get("3", 0)))

    return out


def _derive_item_attack_fields(
    items_fixed: np.ndarray,
    *,
    fighter_attack_id: np.ndarray,
    fighter_attack_instance: np.ndarray,
    num_players: int,
) -> None:
    """Derive per-item (attack_id, attack_instance) strictly causally (prefix-invariant).

    Decomp shape:
    - Items spawned from fighters copy fp->x2068_attackID / fp->x206C_attack_instance at spawn.
      refs/melee/src/melee/it/it_2725.c::it_8027B070
    - Reflected items can change owner/instance identity without despawning, but the item's staling
      identity remains spawn-latched for lasers in v1 (do not overwrite these fields on reflect).
      Slippi records item.instance_id from item->xDA8_short (SendItemInfo.s reads 0xDA8), and the
      reflect path updates xDA8_short from the reflecting fighter snapshot:
        refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464 (writes item reflect snapshot fields)
        refs/melee/src/melee/it/item.c::Item_80269F14 (applies reflect; updates owner + xDA8_short)
        refs/slippi-ssbm-asm/Recording/SendItemInfo.s (lhz r3,0xDA8(REG_ItemData))
    - Item hits use these fields for stale multiplier + stale queue update.
      refs/melee/src/melee/it/itcoll.c::it_80272460
      refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
    """
    if "attack_id" not in items_fixed.dtype.names or "attack_instance" not in items_fixed.dtype.names:
        return

    # Live mapping for active items keyed by the stable item identity:
    # (spawn_id, type) == (Slippi item.id, item.type).
    #
    # IMPORTANT: Slippi `item.instance_id` is item->xDA8_short, which can change on reflect
    # (and other owner transfers) without the underlying item despawning. Using instance_id as
    # part of the map key would spuriously treat a reflected item as a new item and would break
    # one-step reseed expectations.
    active: dict[tuple[int, int], tuple[int, int]] = {}
    default_attack_id = 1  # FtMoveId_Default (do not stale)

    n_frames = int(items_fixed.shape[0])
    for fi in range(n_frames):
        keys_this_frame: set[tuple[int, int]] = set()

        for slot in range(15):
            if int(items_fixed[fi, slot]["exists"]) == 0:
                continue

            key = (
                int(items_fixed[fi, slot]["spawn_id"]),
                int(items_fixed[fi, slot]["type"]),
            )
            keys_this_frame.add(key)

            owner = int(items_fixed[fi, slot]["owner"])

            v = active.get(key)
            if v is None:
                if 0 <= owner < int(num_players):
                    aid = int(fighter_attack_id[fi, owner])
                    ainst = int(fighter_attack_instance[fi, owner])
                else:
                    aid = default_attack_id
                    ainst = 0
                v = (aid, ainst)
                active[key] = v

            items_fixed[fi, slot]["attack_id"] = np.uint16(v[0])
            items_fixed[fi, slot]["attack_instance"] = np.uint16(v[1])

        # Drop inactive keys to keep the active map bounded.
        if active:
            for k in list(active.keys()):
                if k not in keys_this_frame:
                    del active[k]


def write_dataset_from_slp(
    *,
    slp_path: str,
    out_path: str,
    ports: list[int] | None = None,
    ucf_enabled: bool = True,
    ucf_cardinals_1_0_enabled: bool = False,
) -> None:
    """
    Build a dataset from a single .slp by reseeding with post(i-1),
    applying inputs from pre(i), and comparing to post(i).

    ports: optional list of 1-based ports to include (e.g. [1,2]).
    """
    class Args:
        pass

    a = Args()
    a.slp = slp_path
    a.out = out_path
    a.ports = None if ports is None else ",".join(str(p) for p in ports)
    a.ucf_enabled = bool(ucf_enabled)
    a.ucf_cardinals_1_0_enabled = bool(ucf_cardinals_1_0_enabled)

    _main_impl(a)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--slp", required=True, help="Path to .slp file")
    ap.add_argument("--out", required=True, help="Output .msl dataset path")
    ap.add_argument(
        "--ports",
        default=None,
        help="Comma-separated 1-based ports to include (e.g. '1,2' for singles). "
        "If omitted, uses all HUMAN ports from game start.",
    )
    ap.add_argument("--ucf-enabled", action="store_true", default=True)
    ap.add_argument("--no-ucf-enabled", dest="ucf_enabled", action="store_false")
    ap.add_argument("--ucf-cardinals-1-0-enabled", action="store_true", default=False)
    ap.add_argument(
        "--no-ucf-cardinals-1-0-enabled", dest="ucf_cardinals_1_0_enabled", action="store_false"
    )
    args = ap.parse_args()
    _main_impl(args)

def _main_impl(args) -> None:
    import json
    from pathlib import Path

    from tools.slippi.combat_history import derive_combat_hitlist_seed_fields
    from tools.slippi.anim_timebase import derive_frame_speed_mul_f32, load_end_frame_tables
    from tools.slippi.seed_history import (
        apply_deadzone,
        compute_fighter_button_timers,
        compute_fighter_stick_input_counters,
        compute_fighter_trigger_input_counters,
        compute_lr_press_timer_x67f,
        derive_instance_id_counter,
        derive_instance_id_x2073,
        derive_colanim_internals,
        derive_downwait_timer,
        derive_damage_jump_buffer_x14,
        derive_damage_post_hitlag_cb_kind,
        derive_grab_owner_port_2p,
        derive_guard_reflect_timer_x14,
        derive_guard_reflect_timer_x18,
        derive_guard_release_lockout_and_lightshield,
        derive_guard_tilt_state,
        derive_dash_x4,
        derive_shine_release_state,
        derive_run_x0,
        derive_ecb_lock_timer,
        load_shield_tilt_table_meta,
        compute_tilt_timer_axis_pre_post,
        compute_tilt_timer_y_pre_post_with_fall_fast,
        compute_x672_trigger_timer_pre_post,
        derive_ucf_pad_buffer_state,
        derive_kneebend_internals,
        derive_turn_internals,
        stick_i8_to_unit,
        ucf_process_stick_i8,
    )

    game = _read_slippi(args.slp, False)
    frames_all = game.frames
    if frames_all is None or len(frames_all) == 0:
        raise ValueError("Replay has no frames")

    ucf_enabled = bool(getattr(args, "ucf_enabled", True))
    ucf_cardinals_1_0_enabled = bool(getattr(args, "ucf_cardinals_1_0_enabled", False))

    # Decide which source ports to include.
    if args.ports is not None:
        src_ports = [int(x.strip()) for x in args.ports.split(",") if x.strip()]
        if any(p < 1 or p > 4 for p in src_ports):
            raise ValueError(f"--ports must be in 1..4, got {src_ports}")
        src_ports = sorted(src_ports)
    else:
        # Default: use HUMAN ports from game start (common for RL suites).
        players = list(game.start.get("players", []))
        src_ports = []
        for p in players:
            if str(p.get("type")) != "Human":
                continue
            port = str(p.get("port"))
            if not port.startswith("P"):
                continue
            src_ports.append(int(port[1:]))
        src_ports = sorted(src_ports)

    if len(src_ports) not in (2, 4):
        raise ValueError(f"Expected 2 or 4 selected ports, got {src_ports}")

    src_port_names = [_port_name(p) for p in src_ports]
    num_players = len(src_port_names)

    # Map static info by 1-based port. Character may change in-game; per-frame char id comes from post.character.
    static_by_port: dict[int, PortStatic] = {}
    ratios_by_port: dict[int, tuple[float, float, float]] = {}
    dmg_flags_by_port: dict[int, tuple[int, int]] = {}
    for p in game.start.get("players", []):
        port = str(p.get("port", ""))
        if not port.startswith("P"):
            continue
        port_1based = int(port[1:])
        static_by_port[port_1based] = PortStatic(
            team_id=_team_id_from_start_player(p),
            char_id=int(p.get("character", 0)),
        )
        ratios_by_port[port_1based] = (
            float(p.get("offense_ratio", 1.0)),
            float(p.get("defense_ratio", 1.0)),
            float(p.get("model_scale", 1.0)),
        )
        # fp+0x2225/fp+0x2224 gate bits used by ftColl_80079AB0.
        #
        # Decomp trail:
        # - PlayerInitData.xC_b7 (mn/types.h) feeds Player_SetMoreFlagsBit2:
        #   refs/melee/src/melee/gm/gm_16AE.c::fn_8016D8AC
        # - Fighter_UnkInitLoad_80068914 seeds fp->x2225_b7 from Player_GetMoreFlagsBit2:
        #   refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitLoad_80068914
        #
        # Slippi start `player.bitfield` is the raw PlayerInitData 0x0C byte; xC_b7 is the LSB.
        raw_xc = int(p.get("bitfield", 0)) & 0xFF
        dmg_x2225_b7 = 1 if (raw_xc & 0x01) else 0
        # x2224_b2 is not exposed by Slippi post-frames; for stock/percent matches (x2225_b7==0)
        # it is never set in decomp (only setter is in stamina KO handling).
        dmg_x2224_b2 = 0
        dmg_flags_by_port[port_1based] = (dmg_x2225_b7, dmg_x2224_b2)

    frame_ids_all = _to_numpy(frames_all.field("id"))
    keep = finalized_frame_indices(frame_ids_all)
    frames = frames_all.take(pa.array(keep))
    frame_ids = _to_numpy(frames.field("id")).astype(np.int32)
    n_frames = int(len(frames))
    if n_frames < 2:
        raise ValueError("Not enough frames for one-step dataset")

    # Per-frame seed for determinism (global RNG seed recorded by Slippi).
    frame_pre_random_seed = _to_numpy(frames.field("start").field("random_seed")).astype(np.uint32)

    # We build samples for indices i=1..n_frames-1:
    # seed_t  := post(i-1)
    # input_t := pre(i)
    # prev_input_t := pre(i-1)
    # ref_t1  := post(i)
    n_samples = n_frames - 1
    samples = np.zeros(n_samples, dtype=SAMPLE_DTYPE)
    # Seed defaults for new internal fields.
    samples["seed_t"]["combo_victim_port"][:] = np.uint8(0xFF)
    samples["seed_t"]["grab_owner_port"][:] = np.uint8(0xFF)

    stage_id = int(game.start.get("stage", 0))
    is_teams = int(bool(game.start.get("is_teams", False)))

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    lstick_deadzone_x = float(common["lstick_deadzone_x"])
    lstick_deadzone_y = float(common["lstick_deadzone_y"])
    lstick_tilt_x_thresh = float(common["lstick_tilt_x_thresh"])
    lstick_tilt_y_thresh = float(common["lstick_tilt_y_thresh"])
    dash_flick_abs = float(common["dash_flick_abs"])
    dash_flick_tilt_max_frames = int(common["dash_flick_tilt_max_frames"])
    tap_jump_threshold = float(common["tap_jump_threshold"])
    tap_jump_release_threshold = float(common["tap_jump_release_threshold"])
    tap_jump_tilt_max_frames = int(common["tap_jump_tilt_max_frames"])
    fastfall_stick_threshold = float(common["fastfall_stick_threshold"])
    fastfall_tilt_max_frames = int(common["fastfall_tilt_max_frames"])
    guard_stick_lerp_x44c = float(common["guard_stick_lerp_x44c"])
    lcancel_window_frames = int(common["lcancel_window_frames"])
    lcancel_lag_div = float(common["lcancel_lag_div"])
    landing_fall_special_lag_frames = float(common["landing_fall_special_lag_frames"])

    data_root = Path("data")
    end_frames = load_end_frame_tables(data_root)
    char_landing_air_lag_frames: dict[int, dict[str, int]] = {}
    for cid in (1, 22):
        key = "fox" if cid == 1 else "falco"
        attrs = json.loads((data_root / "characters" / f"{key}.json").read_text())
        char_landing_air_lag_frames[int(cid)] = {
            "airn": int(attrs["landing_airn_lag_frames"]),
            "airf": int(attrs["landing_airf_lag_frames"]),
            "airb": int(attrs["landing_airb_lag_frames"]),
            "airhi": int(attrs["landing_airhi_lag_frames"]),
            "airlw": int(attrs["landing_airlw_lag_frames"]),
        }

    # Guard-tilt table metadata (neutral frame + max frame) for decomp-shaped mv.co.guard.x8.
    shield_meta = load_shield_tilt_table_meta()
    neutral_lut = np.zeros(256, dtype=np.uint16)
    frame_max_lut = np.zeros(256, dtype=np.uint16)
    for cid, (neutral, frame_max) in shield_meta.items():
        neutral_lut[np.uint8(cid)] = np.uint16(int(neutral) & 0xFFFF)
        frame_max_lut[np.uint8(cid)] = np.uint16(int(frame_max) & 0xFFFF)

    # Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
    act_turn = 0x0012
    act_turn_run = 0x0013
    act_dash = 0x0014
    act_run = 0x0015
    act_run_direct = 0x0016
    act_kneebend = 0x0018
    act_jump_f = 0x0019
    act_jump_b = 0x001A
    act_jump_aerial_f = 0x001B
    act_jump_aerial_b = 0x001C
    act_fall = 0x001D
    act_fall_f = 0x001E
    act_fall_b = 0x001F
    act_fall_aerial = 0x0020
    act_fall_aerial_f = 0x0021
    act_fall_aerial_b = 0x0022
    act_fall_special = 0x0023
    act_fall_special_f = 0x0024
    act_fall_special_b = 0x0025
    act_damage_fall = 0x0026
    act_landing_fall_special = 0x002B
    act_damage_hi_1 = 0x004B
    act_damage_hi_2 = 0x004C
    act_damage_hi_3 = 0x004D
    act_damage_n_1 = 0x004E
    act_damage_n_2 = 0x004F
    act_damage_n_3 = 0x0050
    act_damage_lw_1 = 0x0051
    act_damage_lw_2 = 0x0052
    act_damage_lw_3 = 0x0053
    act_damage_air_1 = 0x0054
    act_damage_air_2 = 0x0055
    act_damage_air_3 = 0x0056
    act_damage_fly_hi = 0x0057
    act_damage_fly_n = 0x0058
    act_damage_fly_lw = 0x0059
    act_damage_fly_top = 0x005A
    act_damage_fly_roll = 0x005B
    act_attack_air_n = 0x0041
    act_attack_air_f = 0x0042
    act_attack_air_b = 0x0043
    act_attack_air_hi = 0x0044
    act_attack_air_lw = 0x0045
    act_guard_on = 0x00B2
    act_guard = 0x00B3
    act_guard_off = 0x00B4
    act_guard_set_off = 0x00B5
    act_guard_reflect = 0x00B6
    act_throw_f = 0x00DB
    act_throw_b = 0x00DC
    act_throw_hi = 0x00DD
    act_throw_lw = 0x00DE
    act_cliff_catch = 0x00FC
    act_cliff_wait = 0x00FD
    act_down_bound_u = 0x00B7
    act_down_wait_u = 0x00B8
    act_down_bound_d = 0x00BF
    act_down_wait_d = 0x00C0
    act_escape_air = 0x00EC
    button_mask_xy = 0x0400 | 0x0800  # HSD_PAD_XY / src/buttons.h::MSL_BUTTON_XY
    button_mask_lr = 0x0040 | 0x0020  # HSD_PAD_L|HSD_PAD_R / src/buttons.h::MSL_BUTTON_{L,R}
    button_mask_z = 0x0010  # HSD_PAD_Z / src/buttons.h::MSL_BUTTON_Z
    button_mask_a = 0x0100  # HSD_PAD_A / src/buttons.h::MSL_BUTTON_A
    button_mask_b = 0x0200  # HSD_PAD_B / src/buttons.h::MSL_BUTTON_B
    button_mask_dpad_up = 0x0008  # HSD_PAD_DPADUP / refs/melee/src/common_structs.h
    button_mask_dpad_down = 0x0004  # HSD_PAD_DPADDOWN / refs/melee/src/common_structs.h

    # Character id mapping follows Slippi post-frame `character` (GALE01):
    # - Fox   = 1
    # - Falco = 22
    turn_frames_lut = np.zeros(256, dtype=np.uint8)
    turn_frames_lut[np.uint8(1)] = np.uint8(
        json.loads(Path("data/characters/fox.json").read_text())["turn_frames"]
    )
    turn_frames_lut[np.uint8(22)] = np.uint8(
        json.loads(Path("data/characters/falco.json").read_text())["turn_frames"]
    )
    reflector_release_lag_lut = np.zeros(256, dtype=np.uint8)
    reflector_release_lag_lut[np.uint8(1)] = np.uint8(
        json.loads(Path("data/characters/fox.json").read_text())["reflector_release_lag_frames"]
    )
    reflector_release_lag_lut[np.uint8(22)] = np.uint8(
        json.loads(Path("data/characters/falco.json").read_text())["reflector_release_lag_frames"]
    )
    # Frame ids and seeds (seed from frame i-1, ref from frame i).
    samples["seed_t"]["frame_id"] = frame_ids[:-1]
    samples["ref_t1"]["frame_id"] = frame_ids[1:]
    samples["seed_t"]["frame_pre_random_seed"] = frame_pre_random_seed[:-1]
    samples["ref_t1"]["frame_pre_random_seed"] = frame_pre_random_seed[1:]

    samples["seed_t"]["stage_id"] = stage_id
    samples["seed_t"]["num_players"] = num_players
    samples["seed_t"]["is_teams"] = is_teams
    samples["seed_t"]["match_damage_ratio"] = np.float32(float(game.start.get("damage_ratio", 1.0)))

    # Staling seed schema (PP#4):
    # - Derive stale queue state strictly causally from replay history so one-step reseed can
    #   apply staling multiplier deterministically.
    from tools.slippi.staling_history import derive_staling_history

    hist = derive_staling_history(frames, src_ports=src_ports)
    samples["seed_t"]["attack_id"][:, :num_players] = hist.attack_id[:-1, :]
    samples["seed_t"]["attack_instance"][:, :num_players] = hist.attack_instance[:-1, :]
    samples["seed_t"]["stale_queue_index"][:, :num_players] = hist.stale_queue_index[:-1, :]
    samples["seed_t"]["stale_move_id"][:, :num_players, :] = hist.stale_move_id[:-1, :, :]
    samples["seed_t"]["stale_attack_instance"][:, :num_players, :] = hist.stale_attack_instance[:-1, :, :]

    samples["ref_t1"]["stage_id"] = stage_id
    samples["ref_t1"]["num_players"] = num_players
    samples["ref_t1"]["is_teams"] = is_teams

    # Static team ids from game start (slot order follows src_ports list).
    for slot, port_1based in enumerate(src_ports):
        st = static_by_port.get(port_1based, PortStatic(team_id=0, char_id=0))
        samples["seed_t"]["team_id"][:, slot] = np.uint8(st.team_id)
        samples["ref_t1"]["team_id"][:, slot] = np.uint8(st.team_id)
        atk, df, scl = ratios_by_port.get(port_1based, (1.0, 1.0, 1.0))
        samples["seed_t"]["attack_ratio"][:, slot] = np.float32(atk)
        samples["seed_t"]["defense_ratio"][:, slot] = np.float32(df)
        # Fighter model scale (decomp: fp->x34_scale.y) comes from game-start settings (player.model_scale).
        samples["seed_t"]["fighter_scale_y"][:, slot] = np.float32(scl)

    # Fill inputs and per-port post state.
    ports_struct = frames.field("ports")
    available_ports = set(f.name for f in ports_struct.type)
    for slot, port_name in enumerate(src_port_names):
        if port_name not in available_ports:
            raise ValueError(f"Replay missing port {port_name}; available ports: {sorted(available_ports)}")
        leader = ports_struct.field(port_name).field("leader")
        pre = leader.field("pre")
        post = leader.field("post")

        # --- Pre-frame inputs (prev and current)
        pre_buttons_physical = _to_numpy(pre.field("buttons_physical")).astype(np.uint16)
        pre_main_x = _to_numpy(pre.field("raw_analog_x")).astype(np.int8)
        pre_main_y = _to_numpy(pre.field("raw_analog_y")).astype(np.int8)
        if pre.type.get_field_index("raw_analog_cstick_x") != -1:
            pre_c_x = _to_numpy(pre.field("raw_analog_cstick_x")).astype(np.int8)
            pre_c_y = _to_numpy(pre.field("raw_analog_cstick_y")).astype(np.int8)
        elif pre.type.get_field_index("cstick") != -1:
            # Older schemas: use pre.cstick float (no raw int8 fields).
            pre_c_x = _stick_i8_from_unit_stick(_to_numpy(pre.field("cstick").field("x")).astype(np.float32))
            pre_c_y = _stick_i8_from_unit_stick(_to_numpy(pre.field("cstick").field("y")).astype(np.float32))
        else:
            pre_c_x = np.zeros(n_frames, dtype=np.int8)
            pre_c_y = np.zeros(n_frames, dtype=np.int8)
        pre_l = _u8_from_float01(_to_numpy(pre.field("triggers_physical").field("l")).astype(np.float32))
        pre_r = _u8_from_float01(_to_numpy(pre.field("triggers_physical").field("r")).astype(np.float32))

        # i=0..n_samples-1 corresponds to frame index (i+1) for current input and i for prev.
        samples["prev_input_t"]["p"]["buttons"][:, slot] = pre_buttons_physical[:-1]
        samples["input_t"]["p"]["buttons"][:, slot] = pre_buttons_physical[1:]
        samples["prev_input_t"]["p"]["main_x"][:, slot] = pre_main_x[:-1]
        samples["input_t"]["p"]["main_x"][:, slot] = pre_main_x[1:]
        samples["prev_input_t"]["p"]["main_y"][:, slot] = pre_main_y[:-1]
        samples["input_t"]["p"]["main_y"][:, slot] = pre_main_y[1:]
        samples["prev_input_t"]["p"]["c_x"][:, slot] = pre_c_x[:-1]
        samples["input_t"]["p"]["c_x"][:, slot] = pre_c_x[1:]
        samples["prev_input_t"]["p"]["c_y"][:, slot] = pre_c_y[:-1]
        samples["input_t"]["p"]["c_y"][:, slot] = pre_c_y[1:]
        samples["prev_input_t"]["p"]["l"][:, slot] = pre_l[:-1]
        samples["input_t"]["p"]["l"][:, slot] = pre_l[1:]
        samples["prev_input_t"]["p"]["r"][:, slot] = pre_r[:-1]
        samples["input_t"]["p"]["r"][:, slot] = pre_r[1:]

        # --- Post-frame state (seed/ref)
        post_char = _to_numpy(post.field("character")).astype(np.uint8)
        post_state = _to_numpy(post.field("state")).astype(np.uint16)
        post_pos = post.field("position")
        post_pos_x = _to_numpy(post_pos.field("x")).astype(np.float32)
        post_pos_y = _to_numpy(post_pos.field("y")).astype(np.float32)
        # Slippi Z: prefer position.z when present, otherwise fall back to 0 (older schemas are 2D-only).
        if post_pos.type.get_field_index("z") != -1:
            post_pos_z = _to_numpy(post_pos.field("z")).astype(np.float32)
        elif post.type.get_field_index("position_z") != -1:
            post_pos_z = _to_numpy(post.field("position_z")).astype(np.float32)
        elif post.type.get_field_index("pos_z") != -1:
            post_pos_z = _to_numpy(post.field("pos_z")).astype(np.float32)
        else:
            post_pos_z = np.zeros(n_frames, dtype=np.float32)
        post_dir = _dir_to_facing(_to_numpy(post.field("direction")).astype(np.float32))
        post_percent = _to_numpy(post.field("percent")).astype(np.float32)
        post_shield = _to_numpy(post.field("shield")).astype(np.float32)
        post_stocks = _to_numpy(post.field("stocks")).astype(np.uint8)
        post_jumps = _to_numpy(post.field("jumps")).astype(np.uint8)
        post_airborne = _to_numpy(post.field("airborne")).astype(np.uint8)
        post_on_ground = _airborne_to_on_ground(post_airborne, n_frames)
        post_hitlag = _u16_from_float_frames(_to_numpy(post.field("hitlag")).astype(np.float32), n_frames)
        post_misc_as = _to_numpy(post.field("misc_as")).astype(np.float32)
        post_state_age_f32 = _to_numpy(post.field("state_age")).astype(np.float32)
        post_state_age = _i16_from_state_age(post_state_age_f32, n_frames)
        post_anim_frame_f32 = _f32_from_state_age(post_state_age_f32, n_frames)

        hurtbox_state = _to_numpy(post.field("hurtbox_state")).astype(np.uint8)
        l_cancel = _to_numpy(post.field("l_cancel")).astype(np.uint8)
        ground_id = _to_numpy(post.field("ground")).astype(np.uint16)
        animation_index = _to_numpy(post.field("animation_index")).astype(np.uint32)
        instance_hit_by = _to_numpy(post.field("last_hit_by_instance")).astype(np.uint16)
        instance_id = _to_numpy(post.field("instance_id")).astype(np.uint16)
        last_attack_landed = _to_numpy(post.field("last_attack_landed")).astype(np.uint8)
        combo_count = _to_numpy(post.field("combo_count")).astype(np.uint8)
        last_hit_by = _to_numpy(post.field("last_hit_by")).astype(np.uint8)

        sf = post.field("state_flags")
        state_flags = np.stack(
            [
                _to_numpy(sf.field("0")).astype(np.uint8),
                _to_numpy(sf.field("1")).astype(np.uint8),
                _to_numpy(sf.field("2")).astype(np.uint8),
                _to_numpy(sf.field("3")).astype(np.uint8),
                _to_numpy(sf.field("4")).astype(np.uint8),
            ],
            axis=1,
        )  # [n_frames, 5]
        post_hitstun = hitstun_u16_from_misc_as_and_state_flags3(
            misc_as_f32=post_misc_as, state_flags3_u8=state_flags[:, 3], n=n_frames
        )

        vel = post.field("velocities")
        speed_air_x_self = _to_numpy(vel.field("self_x_air")).astype(np.float32)
        speed_y_self = _to_numpy(vel.field("self_y")).astype(np.float32)
        speed_x_attack = _to_numpy(vel.field("knockback_x")).astype(np.float32)
        speed_y_attack = _to_numpy(vel.field("knockback_y")).astype(np.float32)
        speed_ground_x_self = _to_numpy(vel.field("self_x_ground")).astype(np.float32)

        # Seed uses post at (i), ref uses post at (i+1).
        samples["seed_t"]["char_id"][:, slot] = post_char[:-1]
        samples["ref_t1"]["char_id"][:, slot] = post_char[1:]

        samples["seed_t"]["action_id"][:, slot] = post_state[:-1]
        samples["ref_t1"]["action_id"][:, slot] = post_state[1:]
        samples["seed_t"]["action_frame"][:, slot] = post_state_age[:-1]
        samples["ref_t1"]["action_frame"][:, slot] = post_state_age[1:]
        port0 = int(src_ports[slot]) - 1
        samples["seed_t"]["match_flow_timer"][:, slot] = _derive_match_flow_timer(
            action_id_u16=post_state, port0=port0, common=common
        )[:-1]
        samples["seed_t"]["downwait_timer"][:, slot] = derive_downwait_timer(
            action_id_u16=post_state,
            down_wait_frames=int(common["down_wait_frames"]),
            act_down_wait_u=act_down_wait_u,
            act_down_wait_d=act_down_wait_d,
        )[:-1]
        samples["seed_t"]["anim_frame_f32"][:, slot] = post_anim_frame_f32[:-1]

        samples["seed_t"]["pos_x"][:, slot] = post_pos_x[:-1]
        samples["ref_t1"]["pos_x"][:, slot] = post_pos_x[1:]
        samples["seed_t"]["pos_y"][:, slot] = post_pos_y[:-1]
        samples["ref_t1"]["pos_y"][:, slot] = post_pos_y[1:]
        samples["seed_t"]["pos_z"][:, slot] = post_pos_z[:-1]
        samples["seed_t"]["speed_air_x_self"][:, slot] = speed_air_x_self[:-1]
        samples["ref_t1"]["speed_air_x_self"][:, slot] = speed_air_x_self[1:]
        samples["seed_t"]["speed_ground_x_self"][:, slot] = speed_ground_x_self[:-1]
        samples["ref_t1"]["speed_ground_x_self"][:, slot] = speed_ground_x_self[1:]
        samples["seed_t"]["speed_y_self"][:, slot] = speed_y_self[:-1]
        samples["ref_t1"]["speed_y_self"][:, slot] = speed_y_self[1:]
        samples["seed_t"]["speed_x_attack"][:, slot] = speed_x_attack[:-1]
        samples["ref_t1"]["speed_x_attack"][:, slot] = speed_x_attack[1:]
        samples["seed_t"]["speed_y_attack"][:, slot] = speed_y_attack[:-1]
        samples["ref_t1"]["speed_y_attack"][:, slot] = speed_y_attack[1:]

        samples["seed_t"]["facing"][:, slot] = post_dir[:-1]
        samples["ref_t1"]["facing"][:, slot] = post_dir[1:]
        samples["seed_t"]["on_ground"][:, slot] = post_on_ground[:-1]
        samples["ref_t1"]["on_ground"][:, slot] = post_on_ground[1:]

        samples["seed_t"]["percent"][:, slot] = post_percent[:-1]
        samples["ref_t1"]["percent"][:, slot] = post_percent[1:]
        port_1based = int(src_ports[slot])
        dmg_x2225_b7, dmg_x2224_b2 = dmg_flags_by_port.get(port_1based, (0, 0))
        samples["seed_t"]["dmg_x2225_b7"][:, slot] = np.uint8(dmg_x2225_b7)
        samples["seed_t"]["dmg_x2224_b2"][:, slot] = np.uint8(dmg_x2224_b2)
        samples["seed_t"]["shield_hp"][:, slot] = post_shield[:-1]
        samples["ref_t1"]["shield_hp"][:, slot] = post_shield[1:]
        samples["seed_t"]["stocks"][:, slot] = post_stocks[:-1]
        samples["ref_t1"]["stocks"][:, slot] = post_stocks[1:]
        samples["seed_t"]["jumps_left"][:, slot] = post_jumps[:-1]
        samples["ref_t1"]["jumps_left"][:, slot] = post_jumps[1:]

        samples["seed_t"]["hitlag"][:, slot] = post_hitlag[:-1]
        samples["ref_t1"]["hitlag"][:, slot] = post_hitlag[1:]
        samples["seed_t"]["hitstun"][:, slot] = post_hitstun[:-1]
        samples["ref_t1"]["hitstun"][:, slot] = post_hitstun[1:]

        samples["seed_t"]["l_cancel"][:, slot] = l_cancel[:-1]
        samples["ref_t1"]["l_cancel"][:, slot] = l_cancel[1:]
        samples["seed_t"]["hurtbox_state"][:, slot] = hurtbox_state[:-1]
        samples["ref_t1"]["hurtbox_state"][:, slot] = hurtbox_state[1:]
        colanim_x198c, colanim_x1990, colanim_x1994, colanim_x2221_b0 = derive_colanim_internals(
            action_id_u16=post_state,
            action_frame_i16=post_state_age,
            hitlag_u16=post_hitlag,
            hitstun_u16=post_hitstun,
            hurtbox_state_u8=hurtbox_state,
            colanim_throw_x1994_frames=int(common["colanim_throw_x1994_frames"]),
            colanim_cliff_x1990_frames=int(common["colanim_cliff_x1990_frames"]),
            colanim_damage_x1994_frames=int(common["colanim_damage_x1994_frames"]),
            throw_actions=(act_throw_f, act_throw_b, act_throw_hi, act_throw_lw),
            cliff_actions=(act_cliff_catch, act_cliff_wait),
            damage_actions=(
                act_damage_hi_1,
                act_damage_hi_2,
                act_damage_hi_3,
                act_damage_n_1,
                act_damage_n_2,
                act_damage_n_3,
                act_damage_lw_1,
                act_damage_lw_2,
                act_damage_lw_3,
                act_damage_air_1,
                act_damage_air_2,
                act_damage_air_3,
                act_damage_fly_hi,
                act_damage_fly_n,
                act_damage_fly_lw,
                act_damage_fly_top,
                act_damage_fly_roll,
                act_damage_fall,
            ),
        )
        samples["seed_t"]["colanim_hit_status_x198c"][:, slot] = colanim_x198c[:-1]
        samples["seed_t"]["colanim_lock_x2221_b0"][:, slot] = colanim_x2221_b0[:-1]
        samples["seed_t"]["colanim_timer_x1990"][:, slot] = colanim_x1990[:-1]
        samples["seed_t"]["colanim_timer_x1994"][:, slot] = colanim_x1994[:-1]
        samples["seed_t"]["ground_id"][:, slot] = ground_id[:-1]
        samples["ref_t1"]["ground_id"][:, slot] = ground_id[1:]
        samples["seed_t"]["animation_index"][:, slot] = animation_index[:-1]
        samples["ref_t1"]["animation_index"][:, slot] = animation_index[1:]
        samples["seed_t"]["instance_hit_by"][:, slot] = instance_hit_by[:-1]
        samples["ref_t1"]["instance_hit_by"][:, slot] = instance_hit_by[1:]
        samples["seed_t"]["instance_id"][:, slot] = instance_id[:-1]
        samples["ref_t1"]["instance_id"][:, slot] = instance_id[1:]
        # Seed fp+0x2073 compare byte used by ft_800895E0 to gate instance_id bumps.
        # Derived strictly causally from replay history in tools/slippi/seed_history.py.
        samples["seed_t"]["instance_id_x2073"][:, slot] = derive_instance_id_x2073(
            char_id_u8=post_char,
            action_id_u16=post_state,
            action_frame_i16=post_state_age,
            data_dir="data",
        )[:-1]
        samples["seed_t"]["last_attack_landed"][:, slot] = last_attack_landed[:-1]
        samples["ref_t1"]["last_attack_landed"][:, slot] = last_attack_landed[1:]
        samples["seed_t"]["combo_count"][:, slot] = combo_count[:-1]
        samples["ref_t1"]["combo_count"][:, slot] = combo_count[1:]
        samples["seed_t"]["last_hit_by"][:, slot] = last_hit_by[:-1]
        samples["ref_t1"]["last_hit_by"][:, slot] = last_hit_by[1:]

        samples["seed_t"]["state_flags"][:, slot, :] = state_flags[:-1, :]
        samples["ref_t1"]["state_flags"][:, slot, :] = state_flags[1:, :]

        # -----------------------------
        # Multi-frame seeded internals:
        # - x670/x671 tilt timers (dash flick / tap jump gates)
        # - TURN countdown + flip latch
        # -----------------------------
        main_x_proc, main_y_proc = ucf_process_stick_i8(
            pre_main_x,
            pre_main_y,
            ucf_enabled=ucf_enabled,
            ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
        )
        c_x_proc, c_y_proc = ucf_process_stick_i8(
            pre_c_x,
            pre_c_y,
            ucf_enabled=ucf_enabled,
            ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
        )
        stick_x = apply_deadzone(stick_i8_to_unit(main_x_proc), lstick_deadzone_x)
        stick_y = apply_deadzone(stick_i8_to_unit(main_y_proc), lstick_deadzone_y)
        cstick_y = apply_deadzone(stick_i8_to_unit(c_y_proc), lstick_deadzone_y)

        # Guard (shield) tilt state (mv.co.guard.x8 + mv.co.guard.x4) is seeded so shield bubble
        # placement becomes stateful (tilt smoothing/inertia) under teacher-forced one-step eval.
        neutral_frame = neutral_lut[post_char]
        frame_max = frame_max_lut[post_char]
        guard_tilt_x8_post, guard_tilt_x4_post = derive_guard_tilt_state(
            stick_x,
            stick_y,
            facing=post_dir,
            action_id=post_state,
            action_frame=post_state_age,
            neutral_frame=neutral_frame,
            frame_max=frame_max,
            guard_stick_lerp_x44c=guard_stick_lerp_x44c,
            act_guard_on=act_guard_on,
            act_guard=act_guard,
            act_guard_reflect=act_guard_reflect,
        )
        samples["seed_t"]["guard_tilt_x8"][:, slot] = guard_tilt_x8_post[:-1]
        samples["seed_t"]["guard_tilt_x4"][:, slot] = guard_tilt_x4_post[:-1]

        prev_buttons = np.concatenate(([np.uint16(0)], pre_buttons_physical[:-1]))
        buttons_pressed = pre_buttons_physical & ~prev_buttons

        trigger_unit = np.maximum(pre_l, pre_r).astype(np.float32) / np.float32(255.0)
        trigger_unit = np.where(
            (pre_buttons_physical & np.uint16(button_mask_lr)) != 0, np.float32(1.0), trigger_unit
        )

        # Guard release lockout (mv.co.guard.xC/x10) + lightshield latch (fp->lightshield_amount).
        # Derived strictly causally from replay history to support teacher-forced one-step reseed.
        guard_release_latched_xc, guard_x10, lightshield_amount = derive_guard_release_lockout_and_lightshield(
            action_id=post_state,
            shield_hp=post_shield,
            hitlag=post_hitlag,
            buttons_held=pre_buttons_physical,
            button_mask_lr=int(button_mask_lr),
            trigger_unit=trigger_unit,
            trigger_deadzone=float(common["trigger_deadzone"]),
            guard_x10_init_frames=int(common["guard_x10_init_frames"]),
            act_guard_on=act_guard_on,
            act_guard=act_guard,
            act_guard_reflect=act_guard_reflect,
            act_guard_set_off=act_guard_set_off,
        )
        samples["seed_t"]["guard_release_latched_xc"][:, slot] = guard_release_latched_xc[:-1]
        samples["seed_t"]["guard_x10"][:, slot] = guard_x10[:-1]
        samples["seed_t"]["lightshield_amount"][:, slot] = lightshield_amount[:-1]

        # x67F input-history timer:
        # - resets on x668 LR-lane edge (digital LR, trigger lane, Z-mapped LR lane),
        # - otherwise increments and saturates at 0xFF.
        # refs/melee/src/melee/ft/fighter.c:1868-1890
        # refs/melee/src/melee/ft/fighter.c:2078-2086
        lr_press_timer = compute_lr_press_timer_x67f(
            buttons=pre_buttons_physical,
            trigger_unit=trigger_unit,
            hitlag_frames=post_hitlag,
            trigger_deadzone=float(common["trigger_deadzone"]),
            button_mask_lr=button_mask_lr,
            button_mask_z=button_mask_z,
            start_timer=0xFF,
        )

        # Seed fp->frame_speed_mul (float) for deterministic anim timebase stepping.
        #
        # Slippi does not expose frame_speed_mul directly; derive it strictly causally from the replay
        # prefix plus decomp-backed landing formulas where state_age resets on entry.
        frame_speed_mul = derive_frame_speed_mul_f32(
            state_age_f32=post_state_age_f32,
            action_id=post_state,
            hitlag=post_hitlag,
            char_id=post_char,
            animation_index=animation_index,
            lr_press_timer=lr_press_timer,
            shield_hp=post_shield,
            lightshield_amount=lightshield_amount,
            common_shield_hit_damage_mul=float(common["shield_hit_damage_mul"]),
            common_shield_hit_damage_base=float(common["shield_hit_damage_base"]),
            common_shield_hit_lightshield_min=float(common["shield_hit_lightshield_min"]),
            common_shield_hit_lightshield_max=float(common["shield_hit_lightshield_max"]),
            common_shield_stun_mul=float(common["shield_stun_mul"]),
            common_shield_stun_base=float(common["shield_stun_base"]),
            common_shield_stun_lightshield_min=float(common["shield_stun_lightshield_min"]),
            common_shield_stun_lightshield_max=float(common["shield_stun_lightshield_max"]),
            end_frames=end_frames,
            common_lcancel_window_frames=lcancel_window_frames,
            common_lcancel_lag_div=lcancel_lag_div,
            common_landing_fall_special_lag_frames=landing_fall_special_lag_frames,
            char_landing_air_lag_frames=char_landing_air_lag_frames,
        )
        # Seed fp->frame_speed_mul (float) for deterministic timebase stepping.
        #
        # Decomp shape:
        # - In HSD_AObjInterpretAnim, curr_frame advances by framerate (fp->frame_speed_mul) and the
        #   resulting curr_frame is what Slippi records as post-frame `state_age`.
        #   refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
        #   refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        #
        # So the stable-segment delta(state_age[t] - state_age[t-1]) is the rate that should be
        # applied on the *next* one-step tick when reseeding at post-frame t.
        samples["seed_t"]["frame_speed_mul_f32"][:, slot] = frame_speed_mul[:-1]

        # Action-entry overrides (decomp):
        # - Dash: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c:55-71
        # - Jump: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c:101-150
        # - JumpAerial: refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c:140-180
        is_dash = post_state == np.uint16(act_dash)
        dash_entry = is_dash & ~np.concatenate(([False], is_dash[:-1]))
        is_jump = (
            (post_state == np.uint16(act_jump_f))
            | (post_state == np.uint16(act_jump_b))
            | (post_state == np.uint16(act_jump_aerial_f))
            | (post_state == np.uint16(act_jump_aerial_b))
        )
        # Jump entry is per-motion-state, not "any jump group":
        # treat JumpF->JumpB, JumpF->JumpAerialF, etc as fresh entries.
        # This matches the action-entry override behavior (x671=0xFE) and keeps the derivation causal.
        prev_state = np.concatenate(([post_state[0]], post_state[:-1]))
        jump_entry = is_jump & (post_state != prev_state)
        fastfall_ok = (
            is_jump
            | (post_state == np.uint16(act_fall))
            | (post_state == np.uint16(act_fall_f))
            | (post_state == np.uint16(act_fall_b))
            | (post_state == np.uint16(act_fall_aerial))
            | (post_state == np.uint16(act_fall_aerial_f))
            | (post_state == np.uint16(act_fall_aerial_b))
            | (post_state == np.uint16(act_fall_special))
            | (post_state == np.uint16(act_fall_special_f))
            | (post_state == np.uint16(act_fall_special_b))
            | (post_state == np.uint16(act_damage_fall))
            | (post_state == np.uint16(act_attack_air_n))
            | (post_state == np.uint16(act_attack_air_f))
            | (post_state == np.uint16(act_attack_air_b))
            | (post_state == np.uint16(act_attack_air_hi))
            | (post_state == np.uint16(act_attack_air_lw))
            | (post_state == np.uint16(act_escape_air))
        )
        tilt_timer_x_pre, tilt_timer_x_post = compute_tilt_timer_axis_pre_post(
            stick_x, tilt_thresh=lstick_tilt_x_thresh, override_post_mask=dash_entry, override_post_value=0xFE
        )
        tilt_timer_y_pre, tilt_timer_y_post, fall_fast_post = compute_tilt_timer_y_pre_post_with_fall_fast(
            stick_y,
            tilt_thresh=lstick_tilt_y_thresh,
            jump_entry=jump_entry,
            fastfall_ok=fastfall_ok,
            speed_y_self_post=speed_y_self,
            on_ground_post=(post_on_ground != 0),
            fastfall_stick_threshold=fastfall_stick_threshold,
            fastfall_tilt_max_frames=fastfall_tilt_max_frames,
        )

        # UCF 0.84 pad buffer state (strictly causal).
        #
        # Source tie-down + ordering note:
        # - UCF gates sdrop-up on `player->input.stick_y_hold_time < 2` (offset 0x671):
        #   refs/ucf/include/melee/asm/player.h
        # - The decomp per-frame update for fp->x671_timer_lstick_tilt_y is:
        #   refs/melee/src/melee/ft/fighter.c:1963-2008
        # - UCF's injection applies cardinals before check_sdrop_up (refs/ucf/src/pad_buffer/pad_buffer.cpp),
        #   but we haven't proven whether Melee updates stick_y_hold_time using pre/post-injection stick.
        #   If shielddrop behavior is off later, revisit this ordering first.
        #
        # We model stick_y_hold_time with the x671-style timer after the per-frame input update,
        # before action-entry overrides (`tilt_timer_y_pre`).
        padbuf_index, padbuf_sdrop_up, padbuf_x, padbuf_y = derive_ucf_pad_buffer_state(
            pre_main_x,
            pre_main_y,
            stick_y_hold_time=tilt_timer_y_pre,
            ucf_enabled=ucf_enabled,
            ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
            lstick_deadzone_x=float(lstick_deadzone_x),
            lstick_deadzone_y=float(lstick_deadzone_y),
        )
        samples["seed_t"]["ucf_padbuf_index"][:, slot] = padbuf_index[:-1]
        samples["seed_t"]["ucf_padbuf_sdrop_up_frames"][:, slot] = padbuf_sdrop_up[:-1]
        samples["seed_t"]["ucf_padbuf_stick_x"][:, slot, :] = padbuf_x[:-1, :]
        samples["seed_t"]["ucf_padbuf_stick_y"][:, slot, :] = padbuf_y[:-1, :]

        samples["seed_t"]["tilt_timer_x"][:, slot] = tilt_timer_x_post[:-1]
        samples["seed_t"]["tilt_timer_y"][:, slot] = tilt_timer_y_post[:-1]
        samples["seed_t"]["fall_fast"][:, slot] = fall_fast_post[:-1]
        run_x0 = derive_run_x0(
            action_id=post_state,
            hitlag_u16=post_hitlag,
            run_x0_init_x430=float(common["run_x0_init_x430"]),
            act_run=act_run,
            act_run_direct=act_run_direct,
            act_turn_run=act_turn_run,
        )
        samples["seed_t"]["run_x0"][:, slot] = run_x0[:-1]
        dash_x4 = derive_dash_x4(
            action_id_u16=post_state,
            action_frame_i16=post_state_age,
            act_dash=act_dash,
            act_turn=act_turn,
        )
        samples["seed_t"]["dash_x4"][:, slot] = dash_x4[:-1]
        shine_release_lag, shine_is_release = derive_shine_release_state(
            action_id_u16=post_state,
            action_frame_i16=post_state_age,
            buttons_held_u16=pre_buttons_physical,
            hitlag_u16=post_hitlag,
            release_lag_init_u8=reflector_release_lag_lut[post_char],
            button_mask_b=button_mask_b,
        )
        samples["seed_t"]["shine_release_lag"][:, slot] = shine_release_lag[:-1]
        samples["seed_t"]["shine_is_release"][:, slot] = shine_is_release[:-1]
        # Decomp: ftCommon_8007D5D4 sets fp->ecb_lock=10 on ground->air and Fighter_procMap ticks it.
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
        # refs/melee/src/melee/ft/fighter.c::Fighter_procMap
        ecb_lock_timer = derive_ecb_lock_timer(
            on_ground_u8=post_on_ground,
            action_id_u16=post_state,
            lock_frames_ground_to_air=10,
        )
        samples["seed_t"]["ecb_lock_timer"][:, slot] = ecb_lock_timer[:-1]
        damage_jump_buffer_x14 = derive_damage_jump_buffer_x14(
            action_id=post_state,
            hitstun_u16=post_hitstun,
            buttons_pressed=buttons_pressed,
            stick_y_unit=stick_y,
            tilt_timer_y=tilt_timer_y_pre,
            tap_jump_threshold=tap_jump_threshold,
            tap_jump_tilt_max_frames=tap_jump_tilt_max_frames,
            button_mask_xy=button_mask_xy,
            damage_actions=(
                act_damage_hi_1,
                act_damage_hi_2,
                act_damage_hi_3,
                act_damage_n_1,
                act_damage_n_2,
                act_damage_n_3,
                act_damage_lw_1,
                act_damage_lw_2,
                act_damage_lw_3,
                act_damage_air_1,
                act_damage_air_2,
                act_damage_air_3,
                act_damage_fly_hi,
                act_damage_fly_n,
                act_damage_fly_lw,
                act_damage_fly_top,
                act_damage_fly_roll,
                act_damage_fall,
            ),
        )
        samples["seed_t"]["damage_jump_buffer_x14"][:, slot] = damage_jump_buffer_x14[:-1]
        damage_post_hitlag_cb_kind = derive_damage_post_hitlag_cb_kind(
            action_id=post_state,
            hitstun_u16=post_hitstun,
            damage_actions=(
                act_damage_fly_hi,
                act_damage_fly_n,
                act_damage_fly_lw,
                act_damage_fly_top,
                act_damage_fly_roll,
                act_damage_fall,
            ),
        )
        samples["seed_t"]["damage_post_hitlag_cb_kind"][:, slot] = damage_post_hitlag_cb_kind[:-1]
        ledge_cooldown = _derive_ledge_cooldown(action_id_u16=post_state, hitlag_u16=post_hitlag, common=common)
        samples["seed_t"]["ledge_cooldown"][:, slot] = ledge_cooldown[:-1]
        samples["seed_t"]["lr_press_timer"][:, slot] = lr_press_timer[:-1]

        # x672 input-history timer (analog trigger hold timer) is seeded to support
        # GuardReflect/powershield logic.
        # Decomp update: refs/melee/src/melee/ft/fighter.c:2020-2050.
        # Decomp override on GuardReflect entry: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:746-804.
        is_guard_reflect = post_state == np.uint16(act_guard_reflect)
        guard_reflect_entry = is_guard_reflect & ~np.concatenate(([False], is_guard_reflect[:-1]))
        _, x672_post = compute_x672_trigger_timer_pre_post(
            trigger_unit=trigger_unit,
            trigger_min=float(common["powershield_reflect_trigger_min"]),
            guard_reflect_entry=guard_reflect_entry,
            start_timer_post=0xFE,
        )
        samples["seed_t"]["x672_input_timer"][:, slot] = x672_post[:-1]

        # GuardReflect reflect timer (mv.co.guard.x14) as a strictly-causal internal countdown.
        # Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50 and ::ftCo_80093BC0.
        guard_reflect_timer_x14 = derive_guard_reflect_timer_x14(
            action_id_u16=post_state,
            hitlag_u16=post_hitlag,
            act_guard_reflect=act_guard_reflect,
            reflect_frames_x2a4=int(common["powershield_reflect_frames"]),
        )
        samples["seed_t"]["guard_reflect_timer_x14"][:, slot] = guard_reflect_timer_x14[:-1]
        guard_reflect_timer_x18 = derive_guard_reflect_timer_x18(
            action_id_u16=post_state,
            hitlag_u16=post_hitlag,
            act_guard_reflect=act_guard_reflect,
            reflect_total_frames_x2b4=int(common["powershield_reflect_total_frames"]),
        )
        samples["seed_t"]["guard_reflect_timer_x18"][:, slot] = guard_reflect_timer_x18[:-1]

        # Fighter per-frame input counters block.
        # Decomp: refs/melee/src/melee/ft/fighter.c:1897-2094 (lb helper: refs/melee/src/melee/lb/lb_00CE.c:163-225).
        x673, x674, x676_x, x677_y, x679_x, x67A_y = compute_fighter_stick_input_counters(
            stick_x_unit=stick_x,
            stick_y_unit=stick_y,
            tilt_thresh_x=lstick_tilt_x_thresh,
            tilt_thresh_y=lstick_tilt_y_thresh,
            start_timer=0xFE,
        )
        samples["seed_t"]["x673"][:, slot] = x673[:-1]
        samples["seed_t"]["x674"][:, slot] = x674[:-1]
        samples["seed_t"]["x676_x"][:, slot] = x676_x[:-1]
        samples["seed_t"]["x677_y"][:, slot] = x677_y[:-1]
        samples["seed_t"]["x679_x"][:, slot] = x679_x[:-1]
        samples["seed_t"]["x67A_y"][:, slot] = x67A_y[:-1]

        x675, x67B, x678 = compute_fighter_trigger_input_counters(
            trigger_unit=trigger_unit,
            trigger_min=float(common["powershield_reflect_trigger_min"]),
            start_timer=0xFE,
        )
        samples["seed_t"]["x675"][:, slot] = x675[:-1]
        samples["seed_t"]["x67B"][:, slot] = x67B[:-1]
        samples["seed_t"]["x678"][:, slot] = x678[:-1]

        x67C, x67D, x67E, x680, x681, x682, x683, x684 = compute_fighter_button_timers(
            buttons_pressed=buttons_pressed,
            mask_a=button_mask_a,
            mask_b=button_mask_b,
            mask_xy=button_mask_xy,
            mask_dpad_up=button_mask_dpad_up,
            mask_dpad_down=button_mask_dpad_down,
            mask_lr=button_mask_lr,
            start_timer=0xFF,
        )
        samples["seed_t"]["x67C"][:, slot] = x67C[:-1]
        samples["seed_t"]["x67D"][:, slot] = x67D[:-1]
        samples["seed_t"]["x67E"][:, slot] = x67E[:-1]
        samples["seed_t"]["x680"][:, slot] = x680[:-1]
        samples["seed_t"]["x681"][:, slot] = x681[:-1]
        samples["seed_t"]["x682"][:, slot] = x682[:-1]
        samples["seed_t"]["x683"][:, slot] = x683[:-1]
        samples["seed_t"]["x684"][:, slot] = x684[:-1]

        # KneeBend internals (jump_input source + short-hop latch) must be seeded to avoid
        # mid-KneeBend reseed guessing in the simulator.
        # Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c:16-28 and :44-56.
        kb_jump_in, kb_short = derive_kneebend_internals(
            action_id=post_state,
            buttons=pre_buttons_physical,
            buttons_pressed=buttons_pressed,
            stick_y_unit=stick_y,
            cstick_y_unit=cstick_y,
            tilt_timer_y=tilt_timer_y_pre,
            tap_jump_threshold=tap_jump_threshold,
            tap_jump_tilt_max_frames=tap_jump_tilt_max_frames,
            tap_jump_release_threshold=tap_jump_release_threshold,
            act_kneebend=act_kneebend,
            button_mask_xy=button_mask_xy,
        )
        samples["seed_t"]["kneebend_jump_input"][:, slot] = kb_jump_in[:-1]
        samples["seed_t"]["kneebend_is_short_hop"][:, slot] = kb_short[:-1]

        # TURN internals are only meaningful in TURN frames; otherwise seed 0.
        turn_frames = turn_frames_lut[post_char]
        turn_frames_to_turn, turn_has_turned, turn_x8 = derive_turn_internals(
            action_id=post_state,
            facing=post_dir,
            stick_x_unit=stick_x,
            tilt_timer_x=tilt_timer_x_pre,
            dash_flick_abs=dash_flick_abs,
            dash_flick_tilt_max_frames=dash_flick_tilt_max_frames,
            turn_frames=turn_frames,
            act_turn=act_turn,
            act_turn_run=act_turn_run,
        )

        samples["seed_t"]["turn_frames_to_turn"][:, slot] = turn_frames_to_turn[:-1]
        samples["seed_t"]["turn_has_turned"][:, slot] = turn_has_turned[:-1]
        samples["seed_t"]["turn_x8"][:, slot] = turn_x8[:-1]

    # Items are global per frame.
    items_fixed = _fill_items_fixed(frames, n_frames)
    _derive_item_attack_fields(
        items_fixed,
        fighter_attack_id=hist.attack_id,
        fighter_attack_instance=hist.attack_instance,
        num_players=num_players,
    )
    samples["seed_t"]["items"] = items_fixed[:-1]
    samples["ref_t1"]["items"] = items_fixed[1:]

    # is_dead in compare is derived from stocks in the evaluator too, but fill it here for completeness.
    samples["ref_t1"]["is_dead"] = (samples["ref_t1"]["stocks"] == 0).astype(np.uint8)

    # -----------------------------
    # Combat rehit latch internals (strictly causal)
    # -----------------------------
    #
    # Teacher-forced one-step eval reseeds from replay post-frames, which wipes rollout history.
    # Seed the combat rehit suppression state so BODY-only hitlag+attribution mutations don't
    # turn into "hit every frame" artifacts.
    #
    # IMPORTANT: Derivation is strictly causal and does not use replay outcomes like hitlag/hitstun
    # to infer hits; it uses extracted hitbox/hurtcap data + current-frame inputs.
    #
    # Build full-frame per-slot arrays (n_frames, MAX_PLAYERS) from the already-populated sample
    # arrays and the original per-frame pre/post buffers.
    #
    # NOTE: we keep unused slots (p>=num_players) zeroed.
    post_team_id = np.zeros((n_frames, 4), dtype=np.uint8)
    post_char_id = np.zeros((n_frames, 4), dtype=np.uint8)
    post_action_id = np.zeros((n_frames, 4), dtype=np.uint16)
    post_action_frame = np.zeros((n_frames, 4), dtype=np.int16)
    post_animation_index = np.zeros((n_frames, 4), dtype=np.uint32)
    post_facing = np.zeros((n_frames, 4), dtype=np.uint8)
    post_on_ground = np.zeros((n_frames, 4), dtype=np.uint8)
    post_pos_x = np.zeros((n_frames, 4), dtype=np.float32)
    post_pos_y = np.zeros((n_frames, 4), dtype=np.float32)
    post_scale_y = np.ones((n_frames, 4), dtype=np.float32)
    post_guard_tilt_x8 = np.zeros((n_frames, 4), dtype=np.uint16)
    post_guard_tilt_x4 = np.zeros((n_frames, 4), dtype=np.float32)
    post_stocks = np.zeros((n_frames, 4), dtype=np.uint8)
    post_shield_hp = np.zeros((n_frames, 4), dtype=np.float32)
    post_hurtbox_state = np.zeros((n_frames, 4), dtype=np.uint8)
    post_instance_hit_by = np.zeros((n_frames, 4), dtype=np.uint16)
    post_instance_id = np.zeros((n_frames, 4), dtype=np.uint16)
    post_hitlag = np.zeros((n_frames, 4), dtype=np.uint16)
    post_hitstun = np.zeros((n_frames, 4), dtype=np.uint16)
    post_state_flags = np.zeros((n_frames, 4, 5), dtype=np.uint8)
    post_last_hit_by = np.full((n_frames, 4), 0xFF, dtype=np.uint8)
    pre_buttons = np.zeros((n_frames, 4), dtype=np.uint16)
    pre_l = np.zeros((n_frames, 4), dtype=np.uint8)
    pre_r = np.zeros((n_frames, 4), dtype=np.uint8)

    # Re-read the per-slot per-frame buffers from the Slippi payload again, but only for the
    # fields needed by the strictly-causal combat history derivation. This keeps the logic local
    # and avoids reverse-mapping from the (n_samples) packed sample arrays.
    ports_struct = frames.field("ports")
    for slot, port_name in enumerate(src_port_names):
        leader = ports_struct.field(port_name).field("leader")
        pre = leader.field("pre")
        post = leader.field("post")

        pre_buttons[:, slot] = _to_numpy(pre.field("buttons_physical")).astype(np.uint16)
        pre_l[:, slot] = _u8_from_float01(_to_numpy(pre.field("triggers_physical").field("l")).astype(np.float32))
        pre_r[:, slot] = _u8_from_float01(_to_numpy(pre.field("triggers_physical").field("r")).astype(np.float32))

        post_team_id[:, slot] = samples["seed_t"]["team_id"][0, slot]
        post_char_id[:, slot] = _to_numpy(post.field("character")).astype(np.uint8)
        post_action_id[:, slot] = _to_numpy(post.field("state")).astype(np.uint16)
        post_action_frame[:, slot] = _i16_from_state_age(_to_numpy(post.field("state_age")).astype(np.float32), n_frames)
        post_animation_index[:, slot] = _to_numpy(post.field("animation_index")).astype(np.uint32)
        post_on_ground[:, slot] = _airborne_to_on_ground(_to_numpy(post.field("airborne")).astype(np.uint8), n_frames)
        post_pos_x[:, slot] = _to_numpy(post.field("position").field("x")).astype(np.float32)
        post_pos_y[:, slot] = _to_numpy(post.field("position").field("y")).astype(np.float32)
        post_stocks[:, slot] = _to_numpy(post.field("stocks")).astype(np.uint8)
        post_shield_hp[:, slot] = _to_numpy(post.field("shield")).astype(np.float32)
        post_hurtbox_state[:, slot] = _to_numpy(post.field("hurtbox_state")).astype(np.uint8)
        post_instance_hit_by[:, slot] = _to_numpy(post.field("last_hit_by_instance")).astype(np.uint16)
        post_instance_id[:, slot] = _to_numpy(post.field("instance_id")).astype(np.uint16)
        post_hitlag[:, slot] = _u16_from_float_frames(_to_numpy(post.field("hitlag")).astype(np.float32), n_frames)
        post_last_hit_by[:, slot] = _to_numpy(post.field("last_hit_by")).astype(np.uint8)
        sf = post.field("state_flags")
        post_state_flags[:, slot, :] = np.stack(
            [
                _to_numpy(sf.field("0")).astype(np.uint8),
                _to_numpy(sf.field("1")).astype(np.uint8),
                _to_numpy(sf.field("2")).astype(np.uint8),
                _to_numpy(sf.field("3")).astype(np.uint8),
                _to_numpy(sf.field("4")).astype(np.uint8),
            ],
            axis=1,
        )
        post_hitstun[:, slot] = hitstun_u16_from_misc_as_and_state_flags3(
            misc_as_f32=_to_numpy(post.field("misc_as")).astype(np.float32),
            state_flags3_u8=post_state_flags[:, slot, 3],
            n=n_frames,
        )

    # Seed bridge: plAttack_80037B08 global next-id counter (unk_804D6480).
    #
    # Slippi does not expose this internal directly. Seed it strictly causally from replay-visible
    # fighter/item instance_id history so one-step reseed starts from a counter that preserves
    # prior-frame id churn (instead of only current-frame max(live ids)).
    # refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
    counter_post = derive_instance_id_counter(
        fighter_instance_id_u16_2d=post_instance_id[:, :num_players],
        item_instance_id_u16_2d=items_fixed["instance_id"],
    )
    samples["seed_t"]["instance_id_counter"] = counter_post[:-1]

    # Grab/throw victim attachment owner identity (slot indices; 2p-only for v1 suite).
    if int(num_players) == 2:
        grab_owner = derive_grab_owner_port_2p(action_id_u16_2p=post_action_id[:, :2])
        samples["seed_t"]["grab_owner_port"][:, :2] = grab_owner[:-1, :]

    # Use already-derived replay-causal seed fields for shield bubble placement:
    # - facing (post-frame)
    # - guard tilt state (mv.co.guard.x8/x4)
    #
    # These are populated per-sample for frames [0..n_frames-2]. Extend to [0..n_frames-1] by
    # repeating the last available state; the last frame is not used by seed_t assignment anyway.
    if n_frames >= 2:
        post_facing[:-1, :] = samples["seed_t"]["facing"][:, :]
        post_facing[-1, :] = post_facing[-2, :]
        post_guard_tilt_x8[:-1, :] = samples["seed_t"]["guard_tilt_x8"][:, :]
        post_guard_tilt_x8[-1, :] = post_guard_tilt_x8[-2, :]
        post_guard_tilt_x4[:-1, :] = samples["seed_t"]["guard_tilt_x4"][:, :]
        post_guard_tilt_x4[-1, :] = post_guard_tilt_x4[-2, :]

    hitlist_cd, hitlist_iid = derive_combat_hitlist_seed_fields(
        num_players=num_players,
        is_teams=bool(is_teams),
        team_id=post_team_id,
        char_id=post_char_id,
        action_id=post_action_id,
        action_frame=post_action_frame,
        animation_index=post_animation_index,
        facing=post_facing,
        on_ground=post_on_ground,
        pos_x=post_pos_x,
        pos_y=post_pos_y,
        fighter_scale_y=post_scale_y,
        guard_tilt_x8=post_guard_tilt_x8,
        guard_tilt_x4=post_guard_tilt_x4,
        stocks=post_stocks,
        shield_hp=post_shield_hp,
        hurtbox_state=post_hurtbox_state,
        hitlag=post_hitlag,
        last_hit_by=post_last_hit_by,
        instance_hit_by=post_instance_hit_by,
        instance_id=post_instance_id,
        input_buttons=pre_buttons,
        input_l=pre_l,
        input_r=pre_r,
        data_root="data",
    )

    # Seed-bridge stale-latch cleanup (strictly causal; replay-visible lanes only).
    #
    # Runtime C previously trimmed stale hitlist entries when attribution disagreed and the victim
    # was neutral (hitlag/hitstun zero, non-guard-family). Keep this ownership repair in seed
    # materialization so sim runtime remains decomp-shaped.
    #
    # Source lanes:
    # - instance_id / last_hit_by_instance / hitlag / hitstun / action_id from Slippi post-frame
    #   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    # - Hitlist ownership container:
    #   refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    guard_family = {
        int(act_guard_on),
        int(act_guard),
        int(act_guard_set_off),
        int(act_guard_reflect),
        int(act_guard_off),
    }
    for fi in range(n_frames):
        for attacker in range(num_players):
            attacker_iid = int(post_instance_id[fi, attacker])
            for defender in range(num_players):
                if int(post_instance_hit_by[fi, defender]) == attacker_iid:
                    continue
                hitlag_pre = int(post_hitlag[fi, defender])
                if hitlag_pre > 0:
                    hitlag_pre -= 1
                hitstun_pre = int(post_hitstun[fi, defender])
                if hitstun_pre > 0:
                    hitstun_pre -= 1
                if hitlag_pre != 0 or hitstun_pre != 0:
                    continue
                if int(post_action_id[fi, defender]) in guard_family:
                    continue
                # Keep LandingFallSpecial suppression stable: this state can carry transient
                # post-landing overlap lanes where clearing suppression synthesizes false BODY hits.
                # refs/melee/src/melee/ft/chara/ftCommon/forward.h (ftCo_MS_LandingFallSpecial)
                if int(post_action_id[fi, defender]) == int(act_landing_fall_special):
                    continue
                # Attribution corroboration: only trim stale suppression for attacker/defender pairs
                # where replay-visible ownership points to this attacker port.
                #
                # Slippi post-frame `last_hit_by` mirrors fighter->x2088 and is replay-visible:
                # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
                if int(post_last_hit_by[fi, defender]) != attacker:
                    continue
                hitlist_cd[fi, attacker, :, defender] = np.uint16(0)
                hitlist_iid[fi, attacker, :, defender] = np.uint16(0)

    samples["seed_t"]["combat_hitlist_cd"] = hitlist_cd[:-1]
    samples["seed_t"]["combat_hitlist_victim_iid"] = hitlist_iid[:-1]

    # -----------------------------
    # Combo victim + combo timer internals (strictly causal)
    # -----------------------------
    #
    # These seed fields support decomp-shaped ftColl_800763C0/ftColl_800764DC combo tracking in the
    # simulator core by reconstructing the missing fp->x2094/x2098 state from replay prefix history.
    from tools.slippi.combo_history import derive_combo_seed_fields

    combo_victim_port, combo_victim_iid, combo_timer = derive_combo_seed_fields(
        num_players=num_players,
        src_ports=list(src_ports),
        hitlag=post_hitlag,
        state_flags=post_state_flags,
        instance_id=post_instance_id,
        last_hit_by=post_last_hit_by,
        instance_hit_by=post_instance_hit_by,
        data_root="data",
    )
    samples["seed_t"]["combo_victim_port"][:, :num_players] = combo_victim_port[:-1, :num_players]
    samples["seed_t"]["combo_victim_instance_id"][:, :num_players] = combo_victim_iid[:-1, :num_players]
    samples["seed_t"]["combo_timer_x2098"][:, :num_players] = combo_timer[:-1, :num_players]

    write_dataset(args.out, num_players=num_players, samples=samples)
    print(f"Wrote {n_samples} samples to {args.out} from {args.slp}")


if __name__ == "__main__":
    main()
