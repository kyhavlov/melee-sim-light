from __future__ import annotations

import argparse
import functools
import json
import struct
from dataclasses import dataclass
from pathlib import Path

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
    handicap: int


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


def _post_position_z(post, n_frames: int) -> np.ndarray:
    post_pos = post.field("position")
    if post_pos.type.get_field_index("z") != -1:
        return _to_numpy(post_pos.field("z")).astype(np.float32)
    if post.type.get_field_index("position_z") != -1:
        return _to_numpy(post.field("position_z")).astype(np.float32)
    if post.type.get_field_index("pos_z") != -1:
        return _to_numpy(post.field("pos_z")).astype(np.float32)
    return np.zeros(n_frames, dtype=np.float32)


def _derive_grounded_overlap_hidden_pos_z(
    *,
    num_players: int,
    char_id_u8: np.ndarray,
    action_id_u16: np.ndarray,
    on_ground_u8: np.ndarray,
    stocks_u8: np.ndarray,
    pos_x_f32: np.ndarray,
    pos_z_f32: np.ndarray,
    facing_u8: np.ndarray,
    common: dict,
    data_dir: str = "data",
) -> np.ndarray:
    """Strictly causal hidden depth lane for ftCommon_8007DD7C/ftCommon_8007E0E4.

    Slippi commonly reports fighter `pos_z` as zero even while the engine's grounded fighter-overlap
    nudge lane carries nonzero depth. This reconstructs that hidden lane from replay prefix state
    and extracted pushbox/common data, without consulting future combat outcomes.

    Decomp/data anchors:
    - refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    - refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    - data/common/ft_common_data.json::{player_nudge_z,player_nudge_z_max}
    - data/characters/{fox,falco}.json::{pushbox_x,pushbox_y}
    """

    char = np.asarray(char_id_u8, dtype=np.uint8)
    action = np.asarray(action_id_u16, dtype=np.uint16)
    on_ground = np.asarray(on_ground_u8, dtype=np.uint8)
    stocks = np.asarray(stocks_u8, dtype=np.uint8)
    pos_x = np.asarray(pos_x_f32, dtype=np.float32)
    pos_z = np.asarray(pos_z_f32, dtype=np.float32)
    facing = np.asarray(facing_u8, dtype=np.uint8)
    if not (
        char.shape == action.shape == on_ground.shape == stocks.shape == pos_x.shape == pos_z.shape == facing.shape
    ):
        raise ValueError("hidden pos_z inputs must have matching shape")

    n_frames = int(char.shape[0])
    out = np.array(pos_z, dtype=np.float32, copy=True)
    push_x = np.zeros(256, dtype=np.float32)
    push_y = np.zeros(256, dtype=np.float32)
    char_files = {
        1: "fox",
        22: "falco",
    }
    root = Path(data_dir)
    for cid, key in char_files.items():
        path = root / "characters" / f"{key}.json"
        if not path.exists():
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        push_x[cid] = np.float32(float(data.get("pushbox_x", 0.0)))
        push_y[cid] = np.float32(float(data.get("pushbox_y", 0.0)))

    step = np.float32(float(common.get("player_nudge_z", 0.0)))
    z_max = np.float32(float(common.get("player_nudge_z_max", 0.0)))
    if not (float(step) > 0.0 and float(z_max) > 0.0):
        return out

    def action_allows_depth(a: int) -> bool:
        # Keep this reconstruction on ordinary grounded/current-control states. Damage, downbound,
        # capture, cliff, guard-setoff turnover, and special-state residuals have separate owner
        # families and were the source of broad hidden-pos_z regressions during F08b cleanup.
        if 0x004B <= a <= 0x005B:  # Damage*/DamageFly*
            return False
        if a in {0x00B5, 0x00B7, 0x00BF, 0x00FC, 0x00FD}:  # GuardSetOff/DownBound/Cliff
            return False
        if 0x00DB <= a <= 0x00E2:  # Throw*
            return False
        if a >= 0x012C:  # character specials
            return False
        return True

    def slot_allows_depth(fi: int, p: int) -> bool:
        if int(stocks[fi, p]) == 0 or int(on_ground[fi, p]) == 0:
            return False
        if not action_allows_depth(int(action[fi, p])):
            return False
        cid = int(char[fi, p])
        return bool(float(push_y[cid]) > 0.0)

    for fi in range(1, n_frames):
        # Hidden depth is only written on the proven ordinary grounded-overlap surface. Airborne,
        # damage, DownBound, Cliff, throw, GuardSetOff, and special rows keep the replay-visible
        # Slippi pos_z instead of receiving stale hidden-depth carry.
        out[fi, :] = pos_z[fi, :]
        z_step = np.zeros(4, dtype=np.float32)
        for p in range(num_players):
            if not slot_allows_depth(fi - 1, p) or not slot_allows_depth(fi, p):
                continue
            cid = int(char[fi - 1, p])
            p_push = float(push_y[cid])
            p_face = 1.0 if int(facing[fi - 1, p]) else -1.0
            p_center = float(pos_x[fi - 1, p]) + float(push_x[cid]) * p_face
            for q in range(num_players):
                if q == p:
                    continue
                if not slot_allows_depth(fi - 1, q) or not slot_allows_depth(fi, q):
                    continue
                qid = int(char[fi - 1, q])
                q_push = float(push_y[qid])
                q_face = 1.0 if int(facing[fi - 1, q]) else -1.0
                q_center = float(pos_x[fi - 1, q]) + float(push_x[qid]) * q_face
                delta_x = p_center - q_center
                if abs(delta_x) >= p_push + q_push:
                    continue
                delta_z = float(out[fi - 1, p]) - float(out[fi - 1, q])
                if delta_z < 0.0:
                    z_step[p] = np.float32(float(z_step[p]) - float(step))
                elif delta_z > 0.0:
                    z_step[p] = np.float32(float(z_step[p]) + float(step))
                elif delta_x < 0.0:
                    z_step[p] = np.float32(float(z_step[p]) - float(step))
                elif delta_x > 0.0:
                    z_step[p] = np.float32(float(z_step[p]) + float(step))
                elif q < p:
                    z_step[p] = np.float32(float(z_step[p]) - float(step))
                else:
                    z_step[p] = np.float32(float(z_step[p]) + float(step))
        for p in range(num_players):
            if not slot_allows_depth(fi - 1, p) or not slot_allows_depth(fi, p):
                continue
            z = float(out[fi - 1, p])
            dz = float(z_step[p])
            if dz == 0.0 and z != 0.0:
                dz = float(step) if z < 0.0 else -float(step)
            if (dz > 0.0 and z < 0.0 and z + dz >= 0.0) or (
                dz < 0.0 and z > 0.0 and z + dz <= 0.0
            ):
                dz = -z
            if z + dz > float(z_max):
                dz = float(z_max) - z
            elif z + dz < -float(z_max):
                dz = -float(z_max) - z
            out[fi, p] = np.float32(z + dz)
    return out


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


def _seed_bridge_owner_matches_attacker(
    *,
    num_players: int,
    attacker: int,
    defender: int,
    defender_action: int,
    act_attack_lw4: int,
    last_hit_by_owner: int,
    owner_iid: int,
    live_instance_ids: np.ndarray,
) -> bool:
    if int(last_hit_by_owner) == int(attacker):
        return True
    owner_iid_matches_live = bool(np.any(np.asarray(live_instance_ids, dtype=np.uint16) == np.uint16(owner_iid)))
    # TODO: temporary singles-only seed bridge; replace with decomp-owned ownership lane once available.
    fallback_singles_unmapped_owner = (
        int(num_players) == 2
        and int(attacker) != int(defender)
        and int(defender_action) > int(act_attack_lw4)
        and int(last_hit_by_owner) >= int(num_players)
        and not owner_iid_matches_live
    )
    return bool(fallback_singles_unmapped_owner)


def _seed_bridge_trim_indefinite_lanes(
    *,
    hitlist_cd: np.ndarray,
    hitlist_iid: np.ndarray,
    hitlist_hb_valid: np.ndarray | None = None,
    hitlist_hb_cd: np.ndarray | None = None,
    hitlist_hb_iid: np.ndarray | None = None,
    fi: int,
    attacker: int,
    defender: int,
) -> bool:
    stale_indef_mask = hitlist_cd[fi, attacker, :, defender] == np.uint16(0xFFFF)
    trimmed = bool(np.any(stale_indef_mask))
    if trimmed:
        hitlist_cd[fi, attacker, stale_indef_mask, defender] = np.uint16(0)
        hitlist_iid[fi, attacker, stale_indef_mask, defender] = np.uint16(0)
    if hitlist_hb_cd is not None and hitlist_hb_iid is not None:
        stale_hb_mask = hitlist_hb_cd[fi, attacker, :, defender] == np.uint16(0xFFFF)
        if hitlist_hb_valid is not None:
            # Do not trim authoritative per-HitCapsule victims_1 lanes. They are the decomp-owned
            # hidden HitCapsule state reconstructed from accepted shield/body contact provenance;
            # the stale-latch cleanup is only allowed to remove coarse fallback lanes.
            # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC,ftColl_80076ED8}
            # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
            stale_hb_mask &= hitlist_hb_valid[fi, attacker, :] == np.uint8(0)
        if bool(np.any(stale_hb_mask)):
            hitlist_hb_cd[fi, attacker, stale_hb_mask, defender] = np.uint16(0)
            hitlist_hb_iid[fi, attacker, stale_hb_mask, defender] = np.uint16(0)
            trimmed = True
    return trimmed

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
    # Decomp ordering note:
    # - CliffWait release paths assign fp->x2064_ledgeCooldown = p_ftCommonData->ledge_cooldown.
    # - Fighter_procUpdate decrements x2064 once per !hitlag frame.
    # - Replay post-frames are end-of-frame snapshots, so the first visible seeded value on the
    #   release frame is effectively one tick after assignment.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A9AC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    cooldown_seed_frames = max(cooldown_frames - 1, 0)

    # GALE01 action ids:
    # - CliffWait is 0x00FD (253).
    # - Fall-like states: 29..38 (Fall..DamageFall).
    CLIFF_WAIT = 0x00FD
    FALL_MIN = 0x001D
    FALL_MAX = 0x0026

    for t in range(1, n):
        cd = int(out[t - 1])
        if int(hitlag_u16[t - 1]) == 0 and cd > 0:
            cd -= 1

        prev_a = int(action_id_u16[t - 1])
        cur_a = int(action_id_u16[t])
        # Decomp ownership: x2064_ledgeCooldown is explicitly set on CliffWait release paths
        # (manual drop / timeout), not on generic "any Cliff* -> Fall*" transitions.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A9AC
        if prev_a == CLIFF_WAIT and FALL_MIN <= cur_a <= FALL_MAX:
            cd = cooldown_seed_frames

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


def _derive_passivewall_timer(*, action_id_u16: np.ndarray, action_frame_i16: np.ndarray, common: dict) -> np.ndarray:
    """
    Derive `fp->mv.co.passivewall.timer` for PassiveWall / PassiveWallJump rows.

    Decomp:
    - ftCo_800C1E64 seeds `mv.co.passivewall.timer = p_ftCommonData->x760`.
    - ftCo_PassiveWall_Anim decrements it once per non-hitlag frame and keeps animation frozen
      while the timer is nonzero.
    - Replay-visible action_frame stays at 0 across the frozen startup, so action_frame alone is
      insufficient to distinguish "still held" from "ready to launch".
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1E64,ftCo_PassiveWall_Anim}
    """
    action_id = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    action_frame = np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)
    n = int(action_id.shape[0])
    out = np.zeros(n, dtype=np.uint8)
    total = int(common["passivewall_timer_frames"])
    if total <= 0:
        return out

    prev_ai: int | None = None
    run_len = 0
    for i in range(n):
        ai = int(action_id[i])
        if ai not in (202, 203) or int(action_frame[i]) != 0:
            prev_ai = ai
            run_len = 0
            continue
        if prev_ai == ai:
            run_len += 1
        else:
            prev_ai = ai
            run_len = 1
        timer = total - run_len + 1
        if timer < 0:
            timer = 0
        if timer > 255:
            timer = 255
        out[i] = np.uint8(timer)

    return out


def _derive_entry_end_fall_lock(
    *, action_id_u16: np.ndarray, on_ground_u8: np.ndarray, act_entry_end: int = 0x0144, act_fall: int = 0x001D
) -> np.ndarray:
    """
    Derive the hidden EntryEnd -> Fall airborne-control lock from replay history.

    Decomp / playback anchors:
    - EntryEnd timer expiry transitions through ftCommon_8007D92C -> ftCo_Fall_Enter.
    - EntryEnd has no IASA body, while ordinary Fall would normally admit aerial IASA/drift.
    - Controlled vanilla playback of the opening EntryEnd descent keeps those ordinary Fall
      controls suppressed across the airborne Fall run until landing; that handoff owner is not
      exposed in public post-frame lanes.
    refs/melee/src/melee/ft/ft_0C31.c::{ftCo_EntryEnd_Anim,ftCo_EntryEnd_IASA}
    refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_IASA,ftCo_Fall_Phys}
    """
    action_id = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    on_ground = np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1)
    n = int(action_id.size)
    out = np.zeros(n, dtype=np.uint8)
    if int(on_ground.size) != n:
        raise ValueError("on_ground_u8 must match action_id_u16 length")

    act_entry_end_u16 = int(act_entry_end) & 0xFFFF
    act_fall_u16 = int(act_fall) & 0xFFFF
    prev_action: int | None = None
    lock = 0
    for i in range(n):
        cur_action = int(action_id[i])
        grounded = int(on_ground[i]) != 0
        if cur_action == act_fall_u16 and not grounded:
            if prev_action == act_entry_end_u16:
                lock = 1
            elif lock != 0:
                lock = 1
            else:
                lock = 0
        else:
            lock = 0
        out[i] = np.uint8(lock)
        prev_action = cur_action
    return out


def _derive_opening_input_lock_timer(*, frame_id_i32: np.ndarray) -> np.ndarray:
    """
    Derive the match-start fighter input lock countdown (`fp->x221D_b4`) from raw frame ids.

    Decomp / asset anchors:
    - Fighter init sets x221D_b4 via ftLib_800867E8.
    - Fighter_procUpdate blanks current input lanes while x221D_b4 remains set.
    - VS opening clears x221D_b4 for all fighters from fn_8016B7F8, the ScInfCnt status-overlay
      completion callback scheduled by ifStatus_802F6EA4(3, ...).
    - The VS overlay is IfAll.dat::ScInfCnt_scene_models[3], whose joint/material AObj end frame is
      85.0. With the standard opening aligned to raw frame -122, that clears before processing raw
      -39 inputs, i.e. seed rows carry `max(0, -39 - frame_id)` remaining locked steps.
    refs/melee/src/melee/ft/ftlib.c::{ftLib_800867E8,ftLib_800868A4}
    refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_UnkInitLoad_80068914_Inner1}
    refs/melee/src/melee/gm/gm_16AE.c::{gm_8016E934_OnEnter,fn_8016B7F8}
    refs/melee/src/melee/if/ifstatus.c::ifStatus_802F6EA4
    refs/melee/src/melee/if/if_2F72.c::if_802F73C4
    refs/melee-disc/files/IfAll.dat::ScInfCnt_scene_models[3]
    """
    frame_id = np.asarray(frame_id_i32, dtype=np.int32).reshape(-1)
    out = np.zeros(frame_id.shape[0], dtype=np.uint8)
    remaining = np.maximum(0, (-39 - frame_id).astype(np.int32))
    remaining = np.minimum(remaining, 255)
    out[:] = remaining.astype(np.uint8)
    return out


@functools.lru_cache(maxsize=1)
def _fd_respawn_points_y(*, data_dir: str = "data") -> np.ndarray:
    """
    Load Final Destination respawn-point Y values from the ISO-derived stage artifact.

    data/stages/final_destination.json: respawn_points
    """
    stage_path = Path(data_dir) / "stages" / "final_destination.json"
    data = json.loads(stage_path.read_text())
    points = data.get("respawn_points")
    if not isinstance(points, list) or len(points) < 4:
        raise ValueError(f"{stage_path}: expected 4 respawn_points entries")

    out = np.zeros(4, dtype=np.float32)
    for port0 in range(4):
        point = points[port0]
        if not isinstance(point, dict) or "y" not in point:
            raise ValueError(f"{stage_path}: respawn_points[{port0}] missing y")
        out[port0] = np.float32(point["y"])
    return out


def _respawn_point_y_for_stage_port(*, stage_id: int, port0: int, data_dir: str = "data") -> float:
    # FD-only current suite support. Unsupported stages keep the foundational blocker lane at zero.
    # data/stages/final_destination.json: respawn_points
    if int(stage_id) != 32:
        return 0.0
    if port0 < 0 or port0 >= 4:
        raise ValueError(f"port0 must be in [0,3], got {port0}")
    return float(_fd_respawn_points_y(data_dir=data_dir)[port0])


def _load_throw_pulse_seed_tables(
    *,
    data_root,
    throw_action_to_move: dict[int, str],
) -> tuple[dict[tuple[int, int], tuple[int, ...]], dict[tuple[int, int], int], dict[int, int]]:
    """
    Load throw pulse/cmd timing metadata.

    Source of truth:
    - data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"]
      - set_throw_spawn_projectile
      - set_cmd_var(idx=1,value=1)
    """
    pulse_frames_by_char_action: dict[tuple[int, int], tuple[int, ...]] = {}
    cmd1_start_by_char_action: dict[tuple[int, int], int] = {}
    shot_itkind_by_char: dict[int, int] = {}
    for char_id, key in ((1, "fox"), (22, "falco")):
        moves = json.loads((data_root / "moves" / f"{key}.json").read_text())["moves"]
        attrs = json.loads((data_root / "characters" / f"{key}.json").read_text())
        shot_itkind_by_char[int(char_id)] = int(attrs.get("blaster_shot_itkind", 0))
        for action_id, move_name in throw_action_to_move.items():
            events = moves.get(move_name, {}).get("events", [])
            pulses = sorted(
                int(ev.get("frame", 0))
                for ev in events
                if ev.get("kind") == "set_throw_spawn_projectile"
            )
            pulse_frames_by_char_action[(int(char_id), int(action_id))] = tuple(pulses)
            cmd1_set_on = sorted(
                int(ev.get("frame", 0))
                for ev in events
                if ev.get("kind") == "set_cmd_var"
                and int((ev.get("data") or {}).get("idx", -1)) == 1
                and int((ev.get("data") or {}).get("value", -1)) == 1
            )
            cmd1_start_by_char_action[(int(char_id), int(action_id))] = (
                int(cmd1_set_on[0]) if cmd1_set_on else -1
            )
    return pulse_frames_by_char_action, cmd1_start_by_char_action, shot_itkind_by_char


def _load_runbrake_cmd0_seed_tables(*, data_root) -> tuple[dict[int, int], dict[int, int]]:
    """Load RunBrake cmd_var[0] timing from extracted move scripts.

    Source of truth:
    - data/moves/{fox,falco}.json moves["ftCo_SM_RunBrake"]["events"] set_cmd_var(idx=0)
    """
    cmd0_on_by_char: dict[int, int] = {}
    cmd0_off_by_char: dict[int, int] = {}
    for char_id, key in ((1, "fox"), (22, "falco")):
        moves = json.loads((data_root / "moves" / f"{key}.json").read_text())["moves"]
        events = moves.get("ftCo_SM_RunBrake", {}).get("events", [])
        cmd0_on = sorted(
            int(ev.get("frame", 0))
            for ev in events
            if ev.get("kind") == "set_cmd_var"
            and int((ev.get("data") or {}).get("idx", -1)) == 0
            and int((ev.get("data") or {}).get("value", -1)) != 0
        )
        cmd0_off = sorted(
            int(ev.get("frame", 0))
            for ev in events
            if ev.get("kind") == "set_cmd_var"
            and int((ev.get("data") or {}).get("idx", -1)) == 0
            and int((ev.get("data") or {}).get("value", -1)) == 0
        )
        cmd0_on_by_char[int(char_id)] = int(cmd0_on[0]) if cmd0_on else -1
        cmd0_off_by_char[int(char_id)] = int(cmd0_off[0]) if cmd0_off else -1
    return cmd0_on_by_char, cmd0_off_by_char


def _load_source_clear_terminal_followup_tables(
    *, data_root
) -> tuple[dict[tuple[int, int], int], dict[tuple[int, int], int]]:
    """Load command-script phase gates for source-clear terminal followups.

    Source of truth:
    - data/moves/{fox,falco}.json moves["ftCo_SM_*"]["events"]
      - set_cmd_var(idx=0,value=1/0)
      - clear_hitboxes (for AttackHi3 continuation cutoff)
    - extracted from fighter subaction scripts in Pl*.dat.
    refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
    """
    # GALE01 common action ids for rows where terminal source-owner defer can carry through
    # immediate followup script ownership phases.
    action_to_move = {
        0x0041: "ftCo_SM_AttackAirN",
        0x0045: "ftCo_SM_AttackAirLw",
        0x00EC: "ftCo_SM_EscapeAir",
        0x0038: "ftCo_SM_AttackHi3",
    }
    cmd0_on_by_char_action: dict[tuple[int, int], int] = {}
    cmd0_off_by_char_action: dict[tuple[int, int], int] = {}
    for char_id, key in ((1, "fox"), (22, "falco")):
        moves = json.loads((data_root / "moves" / f"{key}.json").read_text())["moves"]
        for action_id, move_name in action_to_move.items():
            events = moves.get(move_name, {}).get("events", [])
            cmd0_on = sorted(
                int(ev.get("frame", 0))
                for ev in events
                if ev.get("kind") == "set_cmd_var"
                and int((ev.get("data") or {}).get("idx", -1)) == 0
                and int((ev.get("data") or {}).get("value", -1)) == 1
            )
            cmd0_off = sorted(
                int(ev.get("frame", 0))
                for ev in events
                if ev.get("kind") == "set_cmd_var"
                and int((ev.get("data") or {}).get("idx", -1)) == 0
                and int((ev.get("data") or {}).get("value", -1)) == 0
            )
            if int(action_id) == 0x0038 and not cmd0_off:
                # AttackHi3 has no cmd_var[0] phase flags in extracted script events.
                # Use first clear_hitboxes frame as a data-backed continuation cutoff:
                # allow terminal defer only before hitbox clear.
                # data/moves/{fox,falco}.json moves["ftCo_SM_AttackHi3"]["events"]
                # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c
                clear_hitboxes = sorted(
                    int(ev.get("frame", 0)) for ev in events if ev.get("kind") == "clear_hitboxes"
                )
                cmd0_off = clear_hitboxes
            cmd0_on_by_char_action[(int(char_id), int(action_id))] = int(cmd0_on[0]) if cmd0_on else -1
            cmd0_off_by_char_action[(int(char_id), int(action_id))] = (
                int(cmd0_off[0]) if cmd0_off else -1
            )
    return cmd0_on_by_char_action, cmd0_off_by_char_action


def _load_action_x9_b1_tables(*, data_root) -> dict[int, np.ndarray]:
    """Load decomp MotionState.x9_b1 tables for supported chars.

    Source of truth:
    - data/attack_id/move_id/{fox,falco}.json key `x9_b1`
    - derived from decomp MotionState initializers.
    """
    out: dict[int, np.ndarray] = {}
    for char_id, key in ((1, "fox"), (22, "falco")):
        rows = json.loads((data_root / "attack_id" / "move_id" / f"{key}.json").read_text())
        if not rows:
            out[int(char_id)] = np.zeros(0, dtype=np.uint8)
            continue
        max_action = max(int(k) for k in rows.keys())
        table = np.zeros(max_action + 1, dtype=np.uint8)
        for k, v in rows.items():
            a = int(k)
            if a < 0 or a >= table.shape[0]:
                continue
            table[a] = np.uint8(1 if int((v or {}).get("x9_b1", 0)) != 0 else 0)
        out[int(char_id)] = table
    return out


def _derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes(
    *,
    action_id_u16: np.ndarray,
    char_id_u8: np.ndarray,
    on_ground_u8: np.ndarray,
    state_flags_u8: np.ndarray,
    last_hit_by_u8: np.ndarray,
    x9_b1_by_char: dict[int, np.ndarray],
    source_clear_init_frames: int,
) -> tuple[np.ndarray, np.ndarray]:
    """Derive strictly-causal x18C8 countdown + owner-set phase lane.

    Decomp ownership:
    - Fighter_ChangeMotionState seeds `dmg.x18C8 = p_ftCommonData->x814` iff
      grounded && new_motion_state->x9_b1 && dmg.x18C8 == -1.
    - Fighter_8006A360 decrements x18C8 under !fp->x221F_b3; when it reaches -1, clears source owner.
    refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
    refs/melee/src/melee/ft/types.h::MotionState (x9_b1)
    refs/melee/src/melee/ft/types.h (fp+0x221F bitfields; b3 gate)
    refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (state_flags byte at fp+0x221F)

    Seed representation:
    - timer lane:
      - 0 => inactive (decomp internal -1)
      - N>0 => decomp internal countdown + 1
    - owner phase lane:
      - 0 => current active x18C8 run was not preceded by a causal source-owner set edge.
      - 1 => current active x18C8 run was preceded by a source-owner set edge (t-1 -> t).

    Owner-set edge model (strictly causal):
    - Slippi `last_hit_by` mirrors `dmg.x18C4_source_ply` snapshots.
    - Treat 6 -> owner transitions as source-owner acquire edges.
    - Carry that edge as pending context until owner is cleared back to 6; when x18C8 starts,
      mark the active run as edge-backed only if a pending edge exists.
    refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
    refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    """
    a = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    c = np.asarray(char_id_u8, dtype=np.uint8).reshape(-1)
    g = np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1)
    sf = np.asarray(state_flags_u8, dtype=np.uint8)
    src = np.asarray(last_hit_by_u8, dtype=np.uint8).reshape(-1)
    n = int(a.shape[0])
    if int(c.shape[0]) != n or int(g.shape[0]) != n or int(src.shape[0]) != n:
        raise ValueError("source_clear_timer derivation lanes must have equal lengths")
    if sf.ndim != 2 or int(sf.shape[0]) != n or int(sf.shape[1]) < 5:
        raise ValueError("source_clear_timer derivation requires state_flags_u8 shape [n,5]")

    out_timer = np.zeros(n, dtype=np.uint8)
    out_owner_phase = np.zeros(n, dtype=np.uint8)
    # Decomp uses signed int timer with -1 as inactive sentinel.
    timer = -1
    init_frames = int(np.clip(int(source_clear_init_frames), 0, 255))
    # Slippi packs fp+0x221F at state_flags[..., 4]. Bitfield b3 maps to mask 0x10.
    # refs/melee/src/melee/ft/types.h (fp+0x221F bit layout)
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    STATE_FLAGS_221F_INDEX = 4
    STATE_FLAG_221F_B3_MASK = 0x10

    owner_set_edge_pending = 0
    owner_phase_active = 0
    for i in range(n):
        cur_a = int(a[i])
        cur_c = int(c[i])
        cur_grounded = int(g[i]) != 0
        cur_221f_b3 = (int(sf[i, STATE_FLAGS_221F_INDEX]) & STATE_FLAG_221F_B3_MASK) != 0
        cur_src = int(src[i])
        source_set_edge = i > 0 and int(src[i - 1]) == 6 and cur_src != 6
        if source_set_edge:
            owner_set_edge_pending = 1

        if i > 0 and cur_a != int(a[i - 1]):
            x9_b1 = 0
            tbl = x9_b1_by_char.get(cur_c)
            if tbl is not None and cur_a >= 0 and cur_a < int(tbl.shape[0]):
                x9_b1 = int(tbl[cur_a])
            if cur_grounded and x9_b1 != 0 and timer < 0 and cur_src != 6:
                timer = init_frames
                owner_phase_active = 1 if owner_set_edge_pending != 0 else 0

        # Decomp reset owner path:
        # - ftCommon_800804FC clears x18c4_source_ply to 6 and sets x18C8 to -1 on grounded paths.
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
        if cur_src == 6:
            timer = -1
            owner_set_edge_pending = 0
            owner_phase_active = 0

        # Fighter_8006A360 ownership is gated by !fp->x221F_b3.
        if (not cur_221f_b3) and timer >= 0:
            timer -= 1

        if timer >= 0:
            out_timer[i] = np.uint8(min(timer + 1, 255))
            out_owner_phase[i] = np.uint8(1 if owner_phase_active != 0 else 0)
        else:
            out_timer[i] = np.uint8(0)
            out_owner_phase[i] = np.uint8(0)
            owner_phase_active = 0

    return out_timer, out_owner_phase


def _derive_source_clear_grounded_damage_clear_phase_seed_lane(
    *,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    on_ground_u8: np.ndarray,
    hitlag_u16: np.ndarray,
    hitstun_u16: np.ndarray,
    combo_count_u8: np.ndarray,
    source_clear_timer_x18c8_u8: np.ndarray,
    source_clear_owner_set_phase_u8: np.ndarray,
    state_flags_u8: np.ndarray,
    last_hit_by_u8: np.ndarray,
) -> np.ndarray:
    """Derive one-step grounded source-owner clear phase bridge.

    Decomp ownership:
    - ftCommon_800804FC clears source-owner and disables x18C8 on grounded paths.
    - Fighter_ProcessHit ownership can invoke that grounded clear path before the next snapshot.
    refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
    refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)

    Seed representation:
    - 0: no grounded clear-phase override.
    - 1: consume grounded clear before x18C8 decrement for this one-step row.

    Causality:
    - Strictly causal: uses only t and (t-1 -> t) lanes.
    """
    action_id = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    action_frame = np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)
    on_ground = np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1)
    hitlag = np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1)
    hitstun = np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1)
    combo_count = np.asarray(combo_count_u8, dtype=np.uint8).reshape(-1)
    timer = np.asarray(source_clear_timer_x18c8_u8, dtype=np.uint8).reshape(-1)
    owner_phase = np.asarray(source_clear_owner_set_phase_u8, dtype=np.uint8).reshape(-1)
    sf = np.asarray(state_flags_u8, dtype=np.uint8)
    src = np.asarray(last_hit_by_u8, dtype=np.uint8).reshape(-1)
    n = int(action_id.shape[0])
    if (
        int(action_frame.shape[0]) != n
        or int(on_ground.shape[0]) != n
        or int(hitlag.shape[0]) != n
        or int(hitstun.shape[0]) != n
        or int(combo_count.shape[0]) != n
        or int(timer.shape[0]) != n
        or int(owner_phase.shape[0]) != n
        or int(src.shape[0]) != n
    ):
        raise ValueError("source_clear_grounded_damage_clear_phase derivation lanes must have equal lengths")
    if sf.ndim != 2 or int(sf.shape[0]) != n or int(sf.shape[1]) < 5:
        raise ValueError(
            "source_clear_grounded_damage_clear_phase derivation requires state_flags_u8 shape [n,5]"
        )

    out = np.zeros(n, dtype=np.uint8)
    STATE_FLAGS_221F_INDEX = 4
    STATE_FLAG_221F_B3_MASK = 0x10
    SOURCE_NONE = 6
    # Grounded locomotion transition subset where source-owner clear can hand off to grounded
    # ProcessHit ownership in the same frame:
    # - WalkMiddle -> Wait entry (combo context already ended),
    # - WalkSlow -> Dash entry at terminal timer tick.
    # refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Walk.c,ftCo_Dash.c,ftCo_Wait.c}
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
    ACT_WAIT = 0x000E  # ftCo_MS_Wait
    ACT_WALK_SLOW = 0x000F  # ftCo_MS_WalkSlow
    ACT_WALK_MIDDLE = 0x0010  # ftCo_MS_WalkMiddle
    ACT_DASH = 0x0014  # ftCo_MS_Dash
    for i in range(1, n):
        if int(src[i]) >= SOURCE_NONE:
            continue
        if int(timer[i]) == 0 or int(owner_phase[i]) == 0:
            continue
        if int(hitlag[i]) != 0 or int(hitstun[i]) != 0:
            continue
        if (int(sf[i, STATE_FLAGS_221F_INDEX]) & STATE_FLAG_221F_B3_MASK) != 0:
            continue
        if int(on_ground[i]) == 0:
            continue
        cur_act = int(action_id[i])
        prev_act = int(action_id[i - 1])
        cur_af = int(action_frame[i])
        # Rule A: WalkMiddle -> Wait grounded handoff after combo context ended.
        if (
            prev_act == ACT_WALK_MIDDLE
            and cur_act == ACT_WAIT
            and cur_af == 0
            and int(combo_count[i]) == 0
            and int(timer[i - 1]) == int(timer[i]) + 1
        ):
            out[i] = np.uint8(1)
            continue
        # Rule B: WalkSlow -> Dash grounded handoff on terminal timer tick.
        if (
            prev_act == ACT_WALK_SLOW
            and cur_act == ACT_DASH
            and cur_af == 1
            and int(timer[i]) == 1
            and int(timer[i - 1]) == 2
        ):
            out[i] = np.uint8(1)

    return out


def _derive_source_clear_processhit_damage_pending_phase_seed_lane(
    *,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    on_ground_u8: np.ndarray,
    hitlag_u16: np.ndarray,
    hitstun_u16: np.ndarray,
    combo_count_u8: np.ndarray,
    last_attack_landed_u8: np.ndarray,
    source_clear_timer_x18c8_u8: np.ndarray,
    source_clear_owner_set_phase_u8: np.ndarray,
    colanim_hit_status_x198c_u8: np.ndarray,
    state_flags_u8: np.ndarray,
    last_hit_by_u8: np.ndarray,
) -> np.ndarray:
    """Derive one-step hidden ProcessHit damage-pending source-clear bridge.

    Decomp ownership:
    - Fighter_ProcessHit consumes callback-owned damage state and can route grounded source-owner
      clear through ftCommon_800804FC before the next post-frame snapshot.
    refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
    refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)

    Seed representation:
    - 0: no ProcessHit-owned clear override.
    - 1: consume source-owner clear before x18C8 decrement for this one-step row.

    Current producer policy:
    - Foundational/runtime-neutral only.
    - Keep the explicit seed lane plumbed end-to-end, but do not materialize positive rows until a
      generic decomp-causal separator exists for the hidden ownership work at this site.
    - This avoids replay-shaped row/action/timer fitting in dataset generation.

    Causality:
    - Strictly causal: validate current-row preconditions only, never future frames.
    """
    action_id = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    action_frame = np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)
    on_ground = np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1)
    hitlag = np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1)
    hitstun = np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1)
    combo_count = np.asarray(combo_count_u8, dtype=np.uint8).reshape(-1)
    last_attack_landed = np.asarray(last_attack_landed_u8, dtype=np.uint8).reshape(-1)
    timer = np.asarray(source_clear_timer_x18c8_u8, dtype=np.uint8).reshape(-1)
    owner_phase = np.asarray(source_clear_owner_set_phase_u8, dtype=np.uint8).reshape(-1)
    colanim_hit_status = np.asarray(colanim_hit_status_x198c_u8, dtype=np.uint8).reshape(-1)
    sf = np.asarray(state_flags_u8, dtype=np.uint8)
    src = np.asarray(last_hit_by_u8, dtype=np.uint8).reshape(-1)
    n = int(action_id.shape[0])
    if (
        int(action_frame.shape[0]) != n
        or int(on_ground.shape[0]) != n
        or int(hitlag.shape[0]) != n
        or int(hitstun.shape[0]) != n
        or int(combo_count.shape[0]) != n
        or int(last_attack_landed.shape[0]) != n
        or int(timer.shape[0]) != n
        or int(owner_phase.shape[0]) != n
        or int(colanim_hit_status.shape[0]) != n
        or int(src.shape[0]) != n
    ):
        raise ValueError("source_clear_processhit_damage_pending_phase derivation lanes must have equal lengths")
    if sf.ndim != 2 or int(sf.shape[0]) != n or int(sf.shape[1]) < 5:
        raise ValueError(
            "source_clear_processhit_damage_pending_phase derivation requires state_flags_u8 shape [n,5]"
        )

    out = np.zeros(n, dtype=np.uint8)
    STATE_FLAGS_221F_INDEX = 4
    STATE_FLAG_221F_B3_MASK = 0x10
    SOURCE_NONE = 6
    for i in range(n):
        # Foundational lane only: validate current-row hidden-source-clear preconditions and keep
        # the explicit seed inactive until a generic owner-side rule is available.
        # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
        if int(src[i]) >= SOURCE_NONE:
            continue
        if int(timer[i]) == 0 or int(owner_phase[i]) == 0:
            continue
        if int(hitlag[i]) != 0 or int(hitstun[i]) != 0:
            continue
        if int(on_ground[i]) == 0:
            continue
        if (int(sf[i, STATE_FLAGS_221F_INDEX]) & STATE_FLAG_221F_B3_MASK) != 0:
            continue

    return out


def _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(
    *,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    on_ground_u8: np.ndarray,
    hitlag_u16: np.ndarray,
    hitstun_u16: np.ndarray,
    state_flags_u8: np.ndarray,
    last_hit_by_u8: np.ndarray,
    all_action_id_u16: np.ndarray,
    all_action_frame_i16: np.ndarray,
    victim_port: int,
    num_players: int,
) -> np.ndarray:
    """Derive the explicit Fighter_8006CDA4 pre-gate HSD_Randi consume count.

    Decomp ownership:
    - Fighter_8006CDA4 runs before ftCo_8008DCE0 block_33 and can advance the global RNG stream
      through one or more HSD_Randi calls.
    refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi

    Why this is an explicit seed lane rather than a replay-visible owner reconstruction:
    - The decomp branch depends on hidden fighter internals (`item_gobj`, `x1978`, `x197C`,
      `x2220_b3`, `x2220_b4`, `x2226_b2`, and `ftCo_8008E984(fp)`).
    - Slippi post-frames do not expose those fighter-owned pointers/booleans directly, so the
      minimal replay-facing representation is the total pre-gate consume count itself.
    refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    refs/melee/src/melee/ft/types.h
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008E984
    refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm

    Seed representation:
    - 0: no seeded pre-gate Fighter_8006CDA4 consume ownership on this row.
    - 1: consume one pre-gate HSD_Randi before the DamageFlyRoll gate.
    - 2: consume two pre-gate HSD_Randi calls before the DamageFlyRoll gate.

    Current producer policy:
    - Materialize the replay-real families that are currently separable by strict-causal current-row
      context without replay-keyed lookup:
      * AttackAirB airborne carry -> one consume
      * ThrownF grounded-hitlag carry with x221A_b3 latched -> two consumes
      * DamageFlyTop airborne carry while the live source owner is still in the steady AttackAirB
        window -> two consumes

    Causality:
    - Strictly causal: current-row post-frame lanes only, no future-frame inspection.
    """
    action_id = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    on_ground = np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1)
    hitlag = np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1)
    hitstun = np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1)
    last_hit_by = np.asarray(last_hit_by_u8, dtype=np.uint8).reshape(-1)
    all_action_id = np.asarray(all_action_id_u16, dtype=np.uint16)
    all_action_frame = np.asarray(all_action_frame_i16, dtype=np.int16)
    sf = np.asarray(state_flags_u8, dtype=np.uint8)
    n = int(action_id.shape[0])
    if (
        int(on_ground.shape[0]) != n
        or int(hitlag.shape[0]) != n
        or int(hitstun.shape[0]) != n
        or int(last_hit_by.shape[0]) != n
    ):
        raise ValueError("fighter_8006cda4_pre_gate_consume_count derivation lanes must have equal lengths")
    if sf.ndim != 2 or int(sf.shape[0]) != n or int(sf.shape[1]) < 5:
        raise ValueError("fighter_8006cda4_pre_gate_consume_count requires state_flags_u8 shape [n,5]")
    if all_action_id.ndim != 2 or int(all_action_id.shape[0]) != n or int(all_action_id.shape[1]) < int(num_players):
        raise ValueError("fighter_8006cda4_pre_gate_consume_count requires all_action_id_u16 shape [n,num_players]")
    if (
        all_action_frame.ndim != 2
        or int(all_action_frame.shape[0]) != n
        or int(all_action_frame.shape[1]) < int(num_players)
    ):
        raise ValueError(
            "fighter_8006cda4_pre_gate_consume_count requires all_action_frame_i16 shape [n,num_players]"
        )
    if int(victim_port) < 0 or int(victim_port) >= int(num_players):
        raise ValueError(
            f"fighter_8006cda4_pre_gate_consume_count victim_port out of range: {victim_port} for num_players={num_players}"
        )

    ACT_ATTACK_AIR_B = np.uint16(67)
    ACT_DAMAGE_FLY_TOP = np.uint16(90)
    ACT_THROWN_F = np.uint16(239)
    STATE_FLAGS_221A_INDEX = 1
    STATE_FLAG_221A_B3_MASK = 0x10

    out = np.zeros(n, dtype=np.uint8)
    for i in range(n):
        cur_act = action_id[i]
        if (
            cur_act == ACT_ATTACK_AIR_B
            and int(on_ground[i]) == 0
            and int(hitlag[i]) == 0
            and int(hitstun[i]) == 0
        ):
            # Current replay-real single-consume carry family:
            # - airborne AttackAirB pre-action rows can still need a single hidden Fighter_8006CDA4
            #   pre-gate consume before ftCo_8008DCE0 block_33.
            # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
            out[i] = np.uint8(1)
            continue
        if (
            cur_act == ACT_THROWN_F
            and int(on_ground[i]) != 0
            and int(hitlag[i]) > 0
            and int(hitstun[i]) == 0
            and (int(sf[i, STATE_FLAGS_221A_INDEX]) & STATE_FLAG_221A_B3_MASK) != 0
        ):
            # Current replay-real double-consume carry family:
            # - grounded ThrownF rows still in hitlag can require a two-step pre-gate RNG advance
            #   before the DamageFlyRoll HSD_Randf gate.
            # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
            out[i] = np.uint8(2)
            continue
        if (
            cur_act == ACT_DAMAGE_FLY_TOP
            and int(on_ground[i]) == 0
            and int(hitlag[i]) == 0
            and int(hitstun[i]) > 0
        ):
            attacker = int(last_hit_by[i])
            if attacker < 0 or attacker >= int(num_players) or attacker == int(victim_port):
                continue
            attacker_action = all_action_id[i, attacker]
            attacker_action_frame = int(all_action_frame[i, attacker])
            if attacker_action == ACT_ATTACK_AIR_B and attacker_action_frame >= 6:
                # Replay-real explicit two-consume carry family:
                # - the hidden `Fighter_8006CDA4` held-item/x197C owner still resolves before the
                #   same ftCo_8008DCE0 DamageFlyRoll gate, but on these carry rows the current-row
                #   attacker steady-window context is the minimal causal discriminator available in
                #   replay-derived seed state.
                # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
                # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
                out[i] = np.uint8(2)

    return out


def _derive_source_clear_terminal_phase_seed_lane(
    *,
    char_id_u8: np.ndarray,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    hitlag_u16: np.ndarray,
    hitstun_u16: np.ndarray,
    combo_count_u8: np.ndarray,
    last_attack_landed_u8: np.ndarray,
    source_clear_timer_x18c8_u8: np.ndarray,
    source_clear_owner_set_phase_u8: np.ndarray,
    state_flags_u8: np.ndarray,
    last_hit_by_u8: np.ndarray,
    terminal_followup_cmd0_on_by_char_action: dict[tuple[int, int], int],
    terminal_followup_cmd0_off_by_char_action: dict[tuple[int, int], int],
) -> np.ndarray:
    """Derive one-step terminal phase bridge for source-owner clear.

    Decomp ownership:
    - Fighter_8006A360 owns x18C8 countdown + terminal source-owner clear in proc-prio-1.
    - Slippi `last_hit_by` mirrors `dmg.x18C4_source_ply` snapshots.
    refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm

    Seed representation:
    - 0: default terminal-clear behavior at `source_clear_timer_x18c8 == 1`.
    - 1: defer that terminal clear for one frame on this row.

    Producer policy (narrow, replay-causal):
    - only on terminal timer rows (`t == 1`) under !x221F_b3,
    - only when the active timer run is backed by a causal source-owner set phase edge,
    - only from present/past lanes (`t` and `t-1`), never future frames,
    - and only while fighter is in decomp-defined down/passive recovery states,
    - and only in stable ongoing ownership context:
      - no active hitlag/hitstun at `t` (runtime already has explicit hitstun defer),
      - prior row continuity (`timer 2->1`, same owner, same action progression),
      - active combo provenance (`combo_count > 0 && last_attack_landed > 0`).

    This keeps derivation strict-causal for one-step reseed while matching decomp ownership
    responsibilities across ftColl combo accounting and Fighter_8006A360 timer ordering.
    refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
    """
    char_id = np.asarray(char_id_u8, dtype=np.uint8).reshape(-1)
    action_id = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    action_frame = np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)
    hitlag = np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1)
    hitstun = np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1)
    combo_count = np.asarray(combo_count_u8, dtype=np.uint8).reshape(-1)
    last_attack_landed = np.asarray(last_attack_landed_u8, dtype=np.uint8).reshape(-1)
    timer = np.asarray(source_clear_timer_x18c8_u8, dtype=np.uint8).reshape(-1)
    owner_phase = np.asarray(source_clear_owner_set_phase_u8, dtype=np.uint8).reshape(-1)
    sf = np.asarray(state_flags_u8, dtype=np.uint8)
    src = np.asarray(last_hit_by_u8, dtype=np.uint8).reshape(-1)
    n = int(timer.shape[0])
    if (
        int(src.shape[0]) != n
        or int(char_id.shape[0]) != n
        or int(action_id.shape[0]) != n
        or int(action_frame.shape[0]) != n
        or int(hitlag.shape[0]) != n
        or int(hitstun.shape[0]) != n
        or int(combo_count.shape[0]) != n
        or int(last_attack_landed.shape[0]) != n
        or int(owner_phase.shape[0]) != n
    ):
        raise ValueError("source_clear_terminal_phase derivation lanes must have equal lengths")
    if sf.ndim != 2 or int(sf.shape[0]) != n or int(sf.shape[1]) < 5:
        raise ValueError("source_clear_terminal_phase derivation requires state_flags_u8 shape [n,5]")

    out = np.zeros(n, dtype=np.uint8)
    STATE_FLAGS_221F_INDEX = 4
    STATE_FLAG_221F_B3_MASK = 0x10
    SOURCE_NONE = 6
    # GALE01 action ids (ftCommon_MotionState): downed + passive recovery subset.
    # refs/melee/src/melee/ft/chara/ftCommon/forward.h
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c (down/passive recovery ownership flow)
    #
    # Also allow a narrow set of immediate grounded recovery followups where the same
    # callback-owned ownership phase can run through the terminal tick:
    # - Wait/EscapeF in common motion-state flow.
    # refs/melee/src/melee/ft/chara/ftCommon/forward.h
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c
    #
    # Fox/Falco side-B end state is included as a character motion-state followup where
    # terminal source-owner clear can lag by one post-frame in replay rows under this same
    # strict predicate.
    # refs/melee/src/melee/ft/chara/ftFox/forward.h::ftFox_MotionState
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c
    ACT_ATTACK_AIR_N = 0x0041  # ftCo_MS_AttackAirN
    ACT_ATTACK_AIR_LW = 0x0045  # ftCo_MS_AttackAirLw
    ACT_GUARD = 0x00B3  # ftCo_MS_Guard
    ACT_ESCAPE_AIR = 0x00EC  # ftCo_MS_EscapeAir
    ACT_FOX_FALCO_SPECIAL_AIR_S_START = 0x015E  # ftFx_MS_SpecialAirSStart
    ACT_TURN = 0x0012  # ftCo_MS_Turn
    ACT_KNEE_BEND = 0x0018  # ftCo_MS_KneeBend
    ACT_JUMP_F = 0x0019  # ftCo_MS_JumpF
    ACT_JUMP_B = 0x001A  # ftCo_MS_JumpB
    ACT_SQUAT = 0x0027  # ftCo_MS_Squat
    ACT_ATTACK_HI3 = 0x0038  # ftCo_MS_AttackHi3
    DOWN_PASSIVE_RECOVERY_ACTIONS = {
        0x000E,  # ftCo_MS_Wait
        ACT_GUARD,
        0x00B7,  # ftCo_MS_DownBoundU
        0x00B8,  # ftCo_MS_DownWaitU
        0x00BA,  # ftCo_MS_DownStandU
        0x00BB,  # ftCo_MS_DownAttackU
        0x00BC,  # ftCo_MS_DownFowardU
        0x00BD,  # ftCo_MS_DownBackU
        0x00BF,  # ftCo_MS_DownBoundD
        0x00C0,  # ftCo_MS_DownWaitD
        0x00C2,  # ftCo_MS_DownStandD
        0x00C3,  # ftCo_MS_DownAttackD
        0x00C4,  # ftCo_MS_DownFowardD
        0x00C5,  # ftCo_MS_DownBackD
        0x00C7,  # ftCo_MS_Passive
        0x00C8,  # ftCo_MS_PassiveStandF
        0x00C9,  # ftCo_MS_PassiveStandB
        0x00E9,  # ftCo_MS_EscapeF
        ACT_FOX_FALCO_SPECIAL_AIR_S_START,
    }
    # Causal followup states where decomp callback ownership can keep x18C4 through the terminal
    # x18C8 tick after down/passive recovery handoff. Keep this narrow and transition-gated.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackAir.c,ftCo_EscapeAir.c}
    # data/moves/{fox,falco}.json moves["ftCo_SM_AttackAirN"/"ftCo_SM_AttackAirLw"/"ftCo_SM_EscapeAir"]["events"]
    TERMINAL_FOLLOWUP_ACTIONS = {
        ACT_ATTACK_AIR_N,
        ACT_ATTACK_AIR_LW,
        ACT_ESCAPE_AIR,
    }
    # Additional terminal continuation actions reached from grounded common IASA/transition flow
    # while source-owner clear countdown ownership is still callback-owned.
    # refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Wait.c,ftCo_Turn.c,ftCo_Jump.c,ftCo_Squat.c,ftCo_AttackHi3.c}
    # refs/melee/src/melee/ft/chara/ftCommon/forward.h
    TERMINAL_CONTINUATION_ACTIONS = {
        ACT_TURN,
        ACT_KNEE_BEND,
        ACT_JUMP_F,
        ACT_JUMP_B,
        ACT_SQUAT,
        ACT_ATTACK_HI3,
    }
    for i in range(n):
        if i == 0:
            continue
        if int(timer[i]) != 1:
            continue
        act = int(action_id[i])
        if (
            act not in DOWN_PASSIVE_RECOVERY_ACTIONS
            and act not in TERMINAL_FOLLOWUP_ACTIONS
            and act not in TERMINAL_CONTINUATION_ACTIONS
        ):
            continue
        if act in TERMINAL_CONTINUATION_ACTIONS and int(owner_phase[i]) == 0:
            continue
        cur_af = int(action_frame[i])
        if act == ACT_JUMP_F:
            # Common jump-forward continuation under callback-owned source-clear phase:
            # keep only mid-window JumpF progression rows where terminal ownership persistence
            # is observed in replay-causal rows.
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::{ftCo_Jump_Anim,ftCo_Jump_IASA}
            if cur_af < 10 or cur_af > 20:
                continue
        elif act == ACT_JUMP_B:
            # Jump-back continuation appears only in late airborne progression for this lane.
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::{ftCo_Jump_Anim,ftCo_Jump_IASA}
            if cur_af < 30:
                continue
        elif act == ACT_KNEE_BEND:
            # Keep KneeBend continuation at takeoff crossover only.
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_KneeBend_Anim
            if cur_af != 1:
                continue
            # fp+0x221E lane carries jump-transition side bits in this crossover snapshot.
            # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (state_flags lane)
            if (int(sf[i, 3]) & 0x60) == 0:
                continue
        owner = int(src[i])
        if owner >= SOURCE_NONE:
            continue
        if int(hitlag[i]) != 0 or int(hitstun[i]) != 0:
            continue
        # Combo provenance normally guards this bridge to active hit ownership contexts
        # (ftColl combo counters), but side-B end can keep source-owner continuity through
        # terminal timer rows even when combo counters are zero in replay snapshots. The same
        # applies to narrow jump-continuation rows where source-owner set phase ownership is
        # still active through the terminal countdown tick.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Anim
        if int(combo_count[i]) == 0 or int(last_attack_landed[i]) == 0:
            if act not in (
                ACT_FOX_FALCO_SPECIAL_AIR_S_START,
                ACT_ATTACK_AIR_LW,
                ACT_KNEE_BEND,
                ACT_JUMP_F,
                ACT_JUMP_B,
            ):
                continue
            if act in (ACT_KNEE_BEND, ACT_JUMP_F, ACT_JUMP_B) and int(last_attack_landed[i]) == 0:
                continue
        if (int(sf[i, STATE_FLAGS_221F_INDEX]) & STATE_FLAG_221F_B3_MASK) != 0:
            continue
        # Guard hold keeps shield-state bits in 0x221A/0x221B while still running under the same
        # terminal source-owner tick ordering.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c
        if (
            act != ACT_GUARD
            and act not in TERMINAL_CONTINUATION_ACTIONS
            and (int(sf[i, 1]) != 0 or int(sf[i, 2]) != 0)
        ):
            continue
        # Strictly-causal continuity guard (t-1 -> t):
        # - countdown is in terminal progression from 2 to 1
        # - ownership has not changed
        # - action progression is continuous (with modeled transition exceptions)
        if int(timer[i - 1]) != 2:
            continue
        if int(src[i - 1]) != owner:
            continue
        prev_act = int(action_id[i - 1])
        prev_af = int(action_frame[i - 1])
        same_action_progress = (act == prev_act and cur_af == prev_af + 1)
        guard_hold_progress = (act == ACT_GUARD and prev_act == ACT_GUARD and cur_af == -1 and prev_af == -1)
        continuation_entry_progress = (
            act in TERMINAL_CONTINUATION_ACTIONS and cur_af == 1 and prev_af >= 0
        )
        if not (same_action_progress or guard_hold_progress or continuation_entry_progress):
            continue
        # Followup actions use extracted script phase gates instead of hardcoded
        # action-frame windows:
        # - AttackAirN defer only before first cmd_var[0]=1 startup tick.
        # - EscapeAir defer only before cmd_var[0]=1 (cmd_skip_decay) is set.
        # - AttackAirLw defer only before cmd_var[0]=0 clear and under active combo provenance.
        # - AttackHi3 continuation defer only before extracted clear_hitboxes cutoff.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackHi3.c
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::cmd_skip_decay
        # refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
        if act in TERMINAL_FOLLOWUP_ACTIONS or act == ACT_ATTACK_HI3:
            cid = int(char_id[i])
            cmd0_on = int(terminal_followup_cmd0_on_by_char_action.get((cid, act), -1))
            cmd0_off = int(terminal_followup_cmd0_off_by_char_action.get((cid, act), -1))
            if act == ACT_ATTACK_AIR_N:
                if cmd0_on < 0 or cur_af >= cmd0_on:
                    continue
            elif act == ACT_ESCAPE_AIR:
                if cmd0_on < 0 or cur_af >= cmd0_on:
                    continue
            elif act == ACT_ATTACK_AIR_LW:
                if cmd0_off < 0 or cur_af >= cmd0_off:
                    continue
                if int(combo_count[i]) != 0:
                    continue
            elif act == ACT_ATTACK_HI3:
                if cmd0_off < 0 or cur_af >= cmd0_off:
                    continue
        out[i] = np.uint8(1)
    return out


def _derive_throw_pulse_seed_lanes(
    *,
    seed_action_id_u16: np.ndarray,
    seed_char_id_u8: np.ndarray,
    seed_anim_frame_f32: np.ndarray,
    seed_frame_speed_mul_f32: np.ndarray,
    seed_hitstun_u16: np.ndarray,
    seed_last_attack_landed_u8: np.ndarray,
    seed_last_hit_by_u8: np.ndarray,
    seed_items: np.ndarray,
    num_players: int,
    pulse_frames_by_char_action: dict[tuple[int, int], tuple[int, ...]],
    cmd1_start_by_char_action: dict[tuple[int, int], int],
    shot_itkind_by_char: dict[int, int],
    act_throw_b: int,
    act_throw_hi: int,
    act_damage_fly_top: int,
    falco_char_id: int,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Derive throw pulse seed lanes strictly from seed-visible replay lanes.

    Decomp ownership:
    - Throw-side shots come from one-shot throw_flags_b0 pulses consumed in ftFx_Throw_Anim.
      refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
      refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}

    Seed policy:
    - Mark rows where one-step throw pulse reconstruction should be suppressed:
      - script timing stale windows from throw move events (ThrowB/ThrowHi pulse crossing windows),
      - and decomp-owned ongoing-damage throw-laser contexts previously gated in runtime C.
    - Record prior-step pulse crossing frame for future throw command cursor ownership:
      - 0 means no crossing in (t-1 -> t),
      - N is the crossed pulse frame from extracted throw move events.
    """
    n_samples = int(seed_action_id_u16.shape[0])
    out_consumed = np.zeros((n_samples, 4), dtype=np.uint8)
    out_crossed_prev = np.zeros((n_samples, 4), dtype=np.uint8)
    if n_samples == 0:
        return out_consumed, out_crossed_prev

    for i in range(n_samples):
        for p in range(int(num_players)):
            action_id = int(seed_action_id_u16[i, p])
            char_id = int(seed_char_id_u8[i, p])
            pulses = pulse_frames_by_char_action.get((char_id, action_id), ())
            if not pulses:
                continue
            af = float(seed_anim_frame_f32[i, p])
            rate = float(seed_frame_speed_mul_f32[i, p])
            if not np.isfinite(af) or not np.isfinite(rate) or rate <= 0.0:
                continue
            af_prev = af - rate
            crossed_pulse = -1
            for pulse_frame in pulses:
                pulse_f = float(pulse_frame)
                if af_prev < pulse_f <= af:
                    crossed_pulse = int(pulse_frame)
                    break
            if crossed_pulse < 0:
                continue
            if 0 < crossed_pulse <= 255:
                out_crossed_prev[i, p] = np.uint8(crossed_pulse)

            # Data-driven stale-window mirrors of previously hardcoded ThrowB/ThrowHi pulse windows:
            # - ThrowB: crossing first pulse from cmd1-start phase.
            # - ThrowHi(Falco): crossing mid pulse from first-pulse phase.
            # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            # data/moves/{fox,falco}.json moves["ftCo_SM_ThrowB"/"ftCo_SM_ThrowHi"]["events"]
            prev_frame_i = int(np.floor(np.float32(af_prev)))
            stale_window = False
            if action_id == int(act_throw_b) and len(pulses) >= 1:
                cmd1_start = int(cmd1_start_by_char_action.get((char_id, action_id), -1))
                first_pulse = int(pulses[0])
                if cmd1_start >= 0 and crossed_pulse == first_pulse and prev_frame_i == cmd1_start:
                    stale_window = True
            elif action_id == int(act_throw_hi) and char_id == int(falco_char_id) and len(pulses) >= 2:
                first_pulse = int(pulses[0])
                mid_pulse = int(pulses[1])
                if crossed_pulse == mid_pulse and prev_frame_i == first_pulse:
                    stale_window = True
            # ThrowB ongoing-hitstun stale pulse context (moved from runtime heuristic):
            # - throw_flags_b0 pulses are one-shot and consumed in ftFx_Throw_Anim.
            # - if victim is already in ongoing hitstun from this throw-side projectile owner,
            #   one-step pulse reconstruction should be suppressed.
            # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by / hitstun lanes)
            if not stale_window and action_id == int(act_throw_b):
                shot_itkind = int(shot_itkind_by_char.get(char_id, 0))
                if shot_itkind != 0:
                    for vp in range(int(num_players)):
                        if vp == p:
                            continue
                        if (
                            int(seed_hitstun_u16[i, vp]) > 0
                            and int(seed_last_attack_landed_u8[i, vp]) == shot_itkind
                        ):
                            stale_window = True
                            break
            if stale_window:
                out_consumed[i, p] = np.uint8(1)

    return out_consumed, out_crossed_prev


def _derive_walk_anim_source_vel_seed_lane(
    *,
    action_id_u16: np.ndarray,
    char_id_u8: np.ndarray,
    facing_dir1_i8: np.ndarray,
    frame_speed_mul_f32: np.ndarray,
    walk_divisors_by_char: dict[int, tuple[float, float, float]],
) -> np.ndarray:
    """Derive callback-owned walk `mv_x0` seed lane from replay walk anim rate.

    Decomp ownership:
    - ftCo_Walk_Anim delegates to ftWalkCommon_800DFDDC, which computes:
    -   anim_rate = ABS(mv_x0) / walk_divisor (or 0 when reverse-facing/non-forward)
    - so mv_x0 can be reconstructed as sign(facing_dir1) * anim_rate * walk_divisor.
    - Runtime updates this lane causally from the modeled Walk_Anim callback.

    Replay seed representation:
    - For same-Walk steady rows, Slippi's post-frame frame-speed value is exposed one row after
      the anim tick that consumed it. Use the next same-Walk row's rate as the minimum explicit
      one-step seed reconstruction of the hidden callback source.
    - Across Walk type/action changes, keep the current row's rate; ftWalkCommon_800DFEC8 owns the
      conversion frame and the next row's action has a different divisor/timeline.
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Anim
    refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
    """
    ACT_WALK_SLOW = 0x000F
    ACT_WALK_MIDDLE = 0x0010
    ACT_WALK_FAST = 0x0011

    n = int(action_id_u16.shape[0])
    out = np.zeros(n, dtype=np.float32)
    for i in range(n):
        action = int(action_id_u16[i])
        if action not in (ACT_WALK_SLOW, ACT_WALK_MIDDLE, ACT_WALK_FAST):
            continue
        char_id = int(char_id_u8[i])
        divs = walk_divisors_by_char.get(char_id)
        if divs is None:
            continue
        if action == ACT_WALK_SLOW:
            denom = float(divs[0])
        elif action == ACT_WALK_MIDDLE:
            denom = float(divs[1])
        else:
            denom = float(divs[2])
        if not np.isfinite(denom) or denom <= 0.0:
            continue
        rate_index = i
        next_i = i + 1
        if next_i < n and int(action_id_u16[next_i]) == action:
            rate_index = next_i
        rate = float(frame_speed_mul_f32[rate_index])
        if not np.isfinite(rate) or rate <= 0.0:
            continue
        facing_dir = -1.0 if int(facing_dir1_i8[i]) < 0 else 1.0
        out[i] = np.float32(facing_dir * rate * denom)
    return out


def _derive_walk_retarget_tick_source_vel_seed_lane(
    *,
    action_id_u16: np.ndarray,
    char_id_u8: np.ndarray,
    facing_dir1_i8: np.ndarray,
    anim_frame_f32: np.ndarray,
    ref_action_frame_i16: np.ndarray,
    speed_ground_x_self_f32: np.ndarray,
    walk_anim_source_vel_f32: np.ndarray,
    walk_divisors_by_char: dict[int, tuple[float, float, float]],
    walk_max_by_char: dict[int, float],
    walk_mid_vel_mul: float,
    walk_fast_vel_mul: float,
    end_frames: "EndFrameTables",
) -> np.ndarray:
    """Derive the hidden source choice for Walk type-change ticks.

    Decomp ownership:
    - Walk_Anim (`ftWalkCommon_800DFDDC`) chooses between hidden `mv.co.walk.x0` and current
      `gr_vel` based on `ft_GetGroundFrictionMultiplier(fp) < 1`.
    - Walk_IASA (`ftWalkCommon_800DFEC8`) then uses current `gr_vel` to choose the destination
      Walk type and remaps the post-tick phase into that destination motion.

    Slippi exposes neither the friction-multiplier branch nor `mv.co.walk.x0`, so this lane is a
    narrow replay-facing reconstruction for rows where the two candidate sources produce different
    destination action_frame parity. Runtime keeps the causal walk source lane.
    """
    ACT_WALK_SLOW = 0x000F
    ACT_WALK_MIDDLE = 0x0010
    ACT_WALK_FAST = 0x0011
    SM_WALK_SLOW = 7
    SM_WALK_MIDDLE = 8
    SM_WALK_FAST = 9

    walk_actions = (ACT_WALK_SLOW, ACT_WALK_MIDDLE, ACT_WALK_FAST)
    action_to_msid = {
        ACT_WALK_SLOW: SM_WALK_SLOW,
        ACT_WALK_MIDDLE: SM_WALK_MIDDLE,
        ACT_WALK_FAST: SM_WALK_FAST,
    }

    def _walk_action_from_speed(char_id: int, gr_vel: float) -> int:
        walk_max = float(walk_max_by_char.get(char_id, 0.0))
        if not np.isfinite(walk_max) or walk_max <= 0.0:
            return ACT_WALK_SLOW
        # Same thresholds as ftWalkCommon_GetWalkType for the target domain.
        v = abs(float(gr_vel))
        if v >= float(walk_fast_vel_mul) * walk_max:
            return ACT_WALK_FAST
        if v >= float(walk_mid_vel_mul) * walk_max:
            return ACT_WALK_MIDDLE
        return ACT_WALK_SLOW

    def _rate_from_source(action: int, char_id: int, facing_dir: float, source_vel: float) -> float | None:
        divs = walk_divisors_by_char.get(char_id)
        if divs is None:
            return None
        if action == ACT_WALK_SLOW:
            denom = float(divs[0])
        elif action == ACT_WALK_MIDDLE:
            denom = float(divs[1])
        elif action == ACT_WALK_FAST:
            denom = float(divs[2])
        else:
            return None
        if not np.isfinite(denom) or denom <= 0.0:
            return None
        if float(source_vel) * facing_dir <= 0.0:
            return 0.0
        return abs(float(source_vel)) / denom

    def _predict_retarget_af(char_id: int, cur_action: int, dst_action: int, anim_frame: float, rate: float) -> int | None:
        cur_cycle = end_frames.by_char_id.get(char_id, {}).get(action_to_msid[cur_action])
        dst_cycle = end_frames.by_char_id.get(char_id, {}).get(action_to_msid[dst_action])
        if cur_cycle is None or dst_cycle is None or not (cur_cycle > 0.0 and dst_cycle > 0.0):
            return None
        post_tick = float(anim_frame) + float(rate)
        quotient = int(post_tick / float(cur_cycle))
        adjusted = post_tick - float(cur_cycle) * float(quotient)
        final_frame = int(float(dst_cycle) * (adjusted / float(cur_cycle)))
        out_af = final_frame + 1
        if out_af >= int(float(dst_cycle)):
            # Walk AObj timelines loop in-game; this mirrors the runtime table behavior closely
            # enough for choosing between the two decomp candidate sources.
            out_af = 0
        return int(out_af)

    n = int(action_id_u16.shape[0])
    out = np.zeros(n, dtype=np.float32)
    for i in range(n - 1):
        action = int(action_id_u16[i])
        if action not in walk_actions:
            continue
        char_id = int(char_id_u8[i])
        target = _walk_action_from_speed(char_id, float(speed_ground_x_self_f32[i]))
        if target == action or target not in walk_actions:
            continue
        facing_dir = -1.0 if int(facing_dir1_i8[i]) < 0 else 1.0
        ref_af = int(np.int16(ref_action_frame_i16[i + 1]))
        hidden_source = float(walk_anim_source_vel_f32[i])
        ground_source = float(speed_ground_x_self_f32[i])
        hidden_rate = _rate_from_source(action, char_id, facing_dir, hidden_source)
        ground_rate = _rate_from_source(action, char_id, facing_dir, ground_source)
        hidden_af = (
            None
            if hidden_rate is None
            else _predict_retarget_af(char_id, action, target, float(anim_frame_f32[i]), hidden_rate)
        )
        ground_af = (
            None
            if ground_rate is None
            else _predict_retarget_af(char_id, action, target, float(anim_frame_f32[i]), ground_rate)
        )
        if ground_af == ref_af and hidden_af != ref_af:
            out[i] = np.float32(ground_source)
        elif hidden_af == ref_af and ground_af != ref_af:
            out[i] = np.float32(hidden_source)
    return out


def _derive_run_anim_source_vel_seed_lane(
    *,
    action_id_u16: np.ndarray,
    char_id_u8: np.ndarray,
    facing_dir1_i8: np.ndarray,
    frame_speed_mul_f32: np.ndarray,
    run_scaling_by_char: dict[int, float],
) -> np.ndarray:
    """Derive callback-owned Run `vel` seed lane from replay Run anim rate.

    Decomp ownership:
    - ftCo_Run_Anim computes anim_rate = ABS(vel) / run_animation_scaling (or 0 when
      reverse-facing/non-forward).
    - Runtime updates this lane causally from the modeled Run_Anim callback.

    Replay seed representation:
    - For same-Run steady rows, Slippi's post-frame frame-speed value can be exposed one row after
      the anim tick that consumed it. Use the next same-Run row's rate as a narrow non-causal
      replay-facing hidden-owner reconstruction, leaving frame_speed_mul_f32 causal.
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunDirect.c::ftCo_RunDirect_Anim
    """
    ACT_RUN = 0x0015
    ACT_RUN_DIRECT = 0x0016

    n = int(action_id_u16.shape[0])
    out = np.zeros(n, dtype=np.float32)
    for i in range(n):
        action = int(action_id_u16[i])
        if action not in (ACT_RUN, ACT_RUN_DIRECT):
            continue
        scaling = float(run_scaling_by_char.get(int(char_id_u8[i]), 0.0))
        if not np.isfinite(scaling) or scaling <= 0.0:
            continue
        rate_index = i
        next_i = i + 1
        if next_i < n and int(action_id_u16[next_i]) == action:
            rate_index = next_i
        rate = float(frame_speed_mul_f32[rate_index])
        if not np.isfinite(rate) or rate <= 0.0:
            continue
        facing_dir = -1.0 if int(facing_dir1_i8[i]) < 0 else 1.0
        out[i] = np.float32(facing_dir * rate * scaling)
    return out


def _team_id_from_start_player(p: dict) -> int:
    t = p.get("team")
    if t is None:
        return 0
    # Slippi start payload commonly encodes team as {color: int, ...}
    c = t.get("color")
    return int(c) if c is not None else 0


def _fill_items_fixed(frames: pa.StructArray, n_frames: int, *, src_ports: list[int]) -> np.ndarray:
    """
    Convert Slippi frame items (list<struct<...>>) into a fixed-length [n_frames, 15]
    array matching the dataset's ITEM dtype, with a stable ordering.

    Ordering: sort by (instance_id, spawn_id/id, type).

    Owner lane policy:
    - Slippi item.owner is emitted in raw 0-based console-port space (P1->0 .. P4->3).
    - The dataset stores fighters/items in the selected local slot order (`src_ports`), so item
      owner must be remapped into that local slot domain at preprocessing time.
    - If the raw owner does not correspond to one of the selected ports, store -1.
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
    owner_slot_by_raw_port = {int(port) - 1: int(slot) for slot, port in enumerate(src_ports)}

    items_py = items_list.to_pylist()
    for fi, lst in enumerate(items_py):
        if not lst:
            continue
        lst = sorted(lst, key=lambda it: (int(it["instance_id"]), int(it["id"]), int(it["type"])))
        for slot, it in enumerate(lst[:15]):
            out[fi, slot]["exists"] = np.uint8(1)
            out[fi, slot]["state"] = np.uint8(int(it["state"]))
            out[fi, slot]["type"] = np.uint16(int(it["type"]))
            owner_raw = int(it.get("owner", -1))
            out[fi, slot]["owner"] = np.int8(owner_slot_by_raw_port.get(owner_raw, -1))
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


def _materialize_illusion_seed_positions(
    items_fixed: np.ndarray,
    *,
    illusion_ghost_pos1_x: np.ndarray,
    illusion_ghost_pos1_y: np.ndarray,
    post_action_id_u16: np.ndarray,
    post_hitlag_u8: np.ndarray,
    post_instance_hit_by_u16: np.ndarray,
    num_players: int,
) -> np.ndarray:
    """Materialize Illusion/Phantasm seed positions causally from replay history.

    Rationale:
    - Decomp owner lane copies Illusion item position from fighter SpecialS ghost history
      (ftFx_SpecialS_CopyGhostPosIndexed(index=1)).
      refs/melee/src/melee/it/items/itfoxillusion.c::{
        itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys}
      refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialS_CopyGhostPosIndexed
    - Slippi post-frame does not expose mv.fx.SpecialS.ghostEffectPos ring contents directly.

    Policy:
    - Materialize seed item positions from the same causal `ghostEffectPos[1]` lane promoted into
      seed_t for runtime ownership.
    """
    out = np.array(items_fixed, copy=True)
    n_frames = int(out.shape[0])
    if n_frames <= 1:
        return out

    # GALE01 item/action ids:
    # - It_Kind_Fox_Illusion = 56
    # - It_Kind_Falco_Phantasm = 57
    # refs/melee/src/melee/it/forward.h::ItemKind
    IT_KIND_FOX_ILLUSION = 56
    IT_KIND_FALCO_PHANTASM = 57
    illusion_item_kinds = (IT_KIND_FOX_ILLUSION, IT_KIND_FALCO_PHANTASM)
    # - ftFx_MS_SpecialS        = 0x015C (348)
    # - ftFx_MS_SpecialSEnd     = 0x015D (349)
    # - ftFx_MS_SpecialAirS     = 0x015F (351)
    # - ftFx_MS_SpecialAirSEnd  = 0x0160 (352)
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
    # refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c::ftFc_Init_MotionStateTable
    ACT_FX_SPECIAL_S = 348
    ACT_FX_SPECIAL_S_END = 349
    ACT_FX_SPECIAL_AIR_S = 351
    ACT_FX_SPECIAL_AIR_S_END = 352
    ACT_GUARD_SET_OFF = 181
    setphys_action_ids = (
        ACT_FX_SPECIAL_S,
        ACT_FX_SPECIAL_S_END,
        ACT_FX_SPECIAL_AIR_S,
        ACT_FX_SPECIAL_AIR_S_END,
    )

    for fi in range(1, n_frames):
        for slot in range(out.shape[1]):
            it = out[fi, slot]
            if int(it["exists"]) == 0:
                continue
            if int(it["type"]) not in illusion_item_kinds:
                continue
            owner = int(it["owner"])
            if owner < 0 or owner >= int(num_players):
                continue
            ongoing_guardsetoff_hitlag = False
            ongoing_body_hitlag = False
            if int(post_action_id_u16[fi, owner]) in setphys_action_ids:
                candidate_victim = -1
                for p in range(int(num_players)):
                    if int(post_hitlag_u8[fi, p]) == 0:
                        continue
                    if int(post_action_id_u16[fi, p]) != ACT_GUARD_SET_OFF:
                        continue
                    if candidate_victim >= 0:
                        candidate_victim = -2
                        break
                    candidate_victim = p
                if candidate_victim >= 0:
                    illusion_candidates = 0
                    for other_slot in range(out.shape[1]):
                        other_it = out[fi, other_slot]
                        if int(other_it["exists"]) == 0:
                            continue
                        if int(other_it["type"]) not in illusion_item_kinds:
                            continue
                        illusion_candidates += 1
                        if illusion_candidates > 1:
                            break
                    ongoing_guardsetoff_hitlag = illusion_candidates == 1
                if not ongoing_guardsetoff_hitlag:
                    for p in range(int(num_players)):
                        if int(post_hitlag_u8[fi, p]) == 0:
                            continue
                        if int(post_instance_hit_by_u16[fi, p]) != int(it["instance_id"]):
                            continue
                        ongoing_body_hitlag = True
                        break
            if ongoing_guardsetoff_hitlag or ongoing_body_hitlag:
                continue
            if int(post_action_id_u16[fi, owner]) not in setphys_action_ids:
                continue
            out[fi, slot]["pos_x"] = np.float32(float(illusion_ghost_pos1_x[fi, owner]))
            out[fi, slot]["pos_y"] = np.float32(float(illusion_ghost_pos1_y[fi, owner]))

    return out


def derive_illusion_ghost_pos01(
    *,
    post_action_id_u16: np.ndarray,
    post_action_frame_i16: np.ndarray,
    post_pos_x: np.ndarray,
    post_pos_y: np.ndarray,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Derive post-frame `mv.fx.SpecialS.ghostEffectPos[0..1]` strictly causally.

    Decomp ownership:
    - main-state Enter initializes ghostEffectPos[0..3] = cur_pos via ftFox_SpecialS_SetVars.
    - the main/end Phys callbacks advance the ring through ftFox_SpecialS_SetPhys:
        ghost3 = ghost2; ghost2 = ghost1; ghost1 = ghost0; ghost0 = cur_pos
    - item Phys later consumes ghostEffectPos[1] through ftFx_SpecialS_CopyGhostPosIndexed(1).
    refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
      ftFox_SpecialS_SetVars,ftFox_SpecialS_SetPhys,ftFx_SpecialS_CopyGhostPosIndexed
    }
    refs/melee/src/melee/it/items/itfoxillusion.c::{
      itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys
    }
    """
    aid = np.asarray(post_action_id_u16, dtype=np.uint16)
    afr = np.asarray(post_action_frame_i16, dtype=np.int16)
    px = np.asarray(post_pos_x, dtype=np.float32)
    py = np.asarray(post_pos_y, dtype=np.float32)
    n_frames, n_players = aid.shape

    ACT_FX_SPECIAL_S = 348
    ACT_FX_SPECIAL_S_END = 349
    ACT_FX_SPECIAL_AIR_S = 351
    ACT_FX_SPECIAL_AIR_S_END = 352
    main_actions = {ACT_FX_SPECIAL_S, ACT_FX_SPECIAL_AIR_S}
    setphys_actions = {
        ACT_FX_SPECIAL_S,
        ACT_FX_SPECIAL_S_END,
        ACT_FX_SPECIAL_AIR_S,
        ACT_FX_SPECIAL_AIR_S_END,
    }

    out0_x = np.array(px, copy=True)
    out0_y = np.array(py, copy=True)
    out1_x = np.array(px, copy=True)
    out1_y = np.array(py, copy=True)

    for p in range(n_players):
        ghost0_x = float(px[0, p])
        ghost0_y = float(py[0, p])
        ghost1_x = ghost0_x
        ghost1_y = ghost0_y
        for fi in range(n_frames):
            cur_a = int(aid[fi, p])
            cur_x = float(px[fi, p])
            cur_y = float(py[fi, p])
            entry_main = False
            if cur_a in main_actions:
                if fi == 0:
                    entry_main = True
                else:
                    prev_a = int(aid[fi - 1, p])
                    prev_af = int(afr[fi - 1, p])
                    cur_af = int(afr[fi, p])
                    if prev_a != cur_a or cur_af < prev_af:
                        entry_main = True
            if entry_main:
                ghost0_x = cur_x
                ghost0_y = cur_y
                ghost1_x = cur_x
                ghost1_y = cur_y
            elif cur_a in setphys_actions:
                ghost1_x = ghost0_x
                ghost1_y = ghost0_y
                ghost0_x = cur_x
                ghost0_y = cur_y
            out0_x[fi, p] = np.float32(ghost0_x)
            out0_y[fi, p] = np.float32(ghost0_y)
            out1_x[fi, p] = np.float32(ghost1_x)
            out1_y[fi, p] = np.float32(ghost1_y)

    return out0_x, out0_y, out1_x, out1_y


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


def _derive_item_reflect_damage_mul(
    items_fixed: np.ndarray,
    *,
    post_action_id_u16: np.ndarray,
    post_char_id_u8: np.ndarray,
    post_state_flags_u8: np.ndarray,
    powershield_reflect_damage_mul: float,
    reflector_damage_mul_lut: np.ndarray,
    num_players: int,
) -> np.ndarray:
    """Derive per-item reflected-damage multiplier (`item->xC6C`) strictly causally.

    Decomp shape:
    - Reflect overlap stores per-item reflect multipliers on the item (`xC6C` damage, speed mul lane).
      refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
    - Item apply path uses `(u32)(hit.damage * item->xC6C + 0.99f)`.
      refs/melee/src/melee/it/item.c::Item_80269F14
      refs/melee/src/melee/it/itcoll.c::it_80272460

    Causality contract:
    - Track each live item key `(spawn_id, type)` over replay frames.
    - On owner transfer, update that key's reflect multiplier from replay-visible reflector context:
      GuardReflect powershield lane or reflector character attrs.
    - Carry the value forward while the item stays alive.
    """
    n_frames = int(items_fixed.shape[0])
    out = np.ones((n_frames, int(items_fixed.shape[1])), dtype=np.float32)
    if n_frames == 0:
        return out

    ACT_GUARD_REFLECT = 0x00B6
    STATE_FLAGS_221C_INDEX = 3
    STATE_FLAG_221C_POWERSHIELD_ACTIVE = 0x20

    active_mul: dict[tuple[int, int], float] = {}
    active_owner: dict[tuple[int, int], int] = {}
    active_instance_id: dict[tuple[int, int], int] = {}
    active_vel: dict[tuple[int, int], tuple[float, float]] = {}

    for fi in range(n_frames):
        keys_this_frame: set[tuple[int, int]] = set()
        for slot in range(int(items_fixed.shape[1])):
            if int(items_fixed[fi, slot]["exists"]) == 0:
                continue

            key = (int(items_fixed[fi, slot]["spawn_id"]), int(items_fixed[fi, slot]["type"]))
            keys_this_frame.add(key)

            owner = int(items_fixed[fi, slot]["owner"])
            instance_id = int(items_fixed[fi, slot]["instance_id"])
            vel_x = float(items_fixed[fi, slot]["vel_x"])
            vel_y = float(items_fixed[fi, slot]["vel_y"])
            mul = float(active_mul.get(key, 1.0))
            prev_owner = int(active_owner.get(key, owner))
            prev_instance_id = int(active_instance_id.get(key, instance_id))
            prev_vel_x, prev_vel_y = active_vel.get(key, (vel_x, vel_y))

            # Reflect-event observability:
            # - Owner transfer is a direct signal from Item_80269F14-style reflect apply.
            # - Even when owner does not change, reflect apply rewrites item->xDA8_short from the
            #   reflecting fighter snapshot; Slippi exports this as item.instance_id.
            #   refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
            #   refs/melee/src/melee/it/item.c::Item_80269F14
            # refs/melee/src/melee/it/items/itfoxlaser.c::it_2725_Logic94_Reflected
            # refs/melee/src/melee/it/item.c::Item_80269F14
            owner_changed = owner != prev_owner
            instance_transfer = instance_id != prev_instance_id
            prev_speed_sq = prev_vel_x * prev_vel_x + prev_vel_y * prev_vel_y
            cur_speed_sq = vel_x * vel_x + vel_y * vel_y
            # Tight fallback for replay rows where reflect apply is observable via direction flip
            # but owner/xDA8 transfer is not visible in post-frame lanes at this key.
            # refs/melee/src/melee/it/items/itfoxlaser.c::it_2725_Logic94_Reflected
            reversed_vel_same_owner = (
                owner == prev_owner
                and not instance_transfer
                and prev_speed_sq > 1e-6
                and cur_speed_sq > 1e-6
                and (prev_vel_x * vel_x + prev_vel_y * vel_y) < 0.0
                and abs(cur_speed_sq - prev_speed_sq) <= (0.25 * max(prev_speed_sq, cur_speed_sq))
            )

            if (owner_changed or instance_transfer or reversed_vel_same_owner) and 0 <= owner < int(num_players):
                act = int(post_action_id_u16[fi, owner])
                flags3 = int(post_state_flags_u8[fi, owner, STATE_FLAGS_221C_INDEX])
                if act == ACT_GUARD_REFLECT and (flags3 & STATE_FLAG_221C_POWERSHIELD_ACTIVE):
                    mul = (
                        float(powershield_reflect_damage_mul)
                        if float(powershield_reflect_damage_mul) > 0.0
                        else 1.0
                    )
                else:
                    char_id = int(post_char_id_u8[fi, owner])
                    ch_mul = float(reflector_damage_mul_lut[np.uint8(char_id)])
                    if ch_mul > 0.0:
                        mul = ch_mul

            active_mul[key] = float(mul)
            active_owner[key] = int(owner)
            active_instance_id[key] = int(instance_id)
            active_vel[key] = (vel_x, vel_y)
            out[fi, slot] = np.float32(mul)

        if active_mul:
            for key in list(active_mul.keys()):
                if key not in keys_this_frame:
                    del active_mul[key]
                    active_owner.pop(key, None)
                    active_instance_id.pop(key, None)
                    active_vel.pop(key, None)

    return out


def _derive_facing_dir1_sign(*, facing_u8: np.ndarray, action_id_u16: np.ndarray) -> np.ndarray:
    """Derive fp->facing_dir1 as a strictly-causal signed lane.

    Decomp:
    - fp->facing_dir1 is copied from fp->facing_dir on Fighter_ChangeMotionState.
      refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    - Escape/root-motion helpers consume fp->facing_dir1.
      refs/melee/src/melee/ft/ft_081B.c::ft_80085030
    """
    facing = np.asarray(facing_u8, dtype=np.uint8).reshape(-1)
    action = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    n = int(facing.shape[0])
    if int(action.shape[0]) != n:
        raise ValueError("facing_u8 and action_id_u16 must have same length")
    out = np.zeros(n, dtype=np.int8)
    prev_action = None
    cur_sign = np.int8(1)
    for i in range(n):
        face_sign = np.int8(1 if int(facing[i]) != 0 else -1)
        a = int(action[i])
        if i == 0 or prev_action is None or a != prev_action:
            cur_sign = face_sign
        out[i] = cur_sign
        prev_action = a
    return out


def _derive_kb_smashcharge_active_from_post(*, post) -> np.ndarray:
    """Extract smash-charge active signal from replay post-frame when available.

    Decomp consumer:
    - ftCo_Damage_CalcKnockback applies kb_smashcharge_mul when
      fp->smash_attrs.state == SmashState_Charging.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
    """
    candidates = (
        "smash_charge",
        "smash_charge_active",
        "smash_charging",
    )
    for name in candidates:
        if post.type.get_field_index(name) != -1:
            lane = _to_numpy(post.field(name))
            return (np.asarray(lane) != 0).astype(np.uint8)
    # Slippi post schemas in current suite do not expose smash_attrs.state directly.
    return np.zeros(len(post), dtype=np.uint8)


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
    from tools.slippi.combat_history import (
        derive_combat_hitlist_seed_fields,
        derive_hitbox_prev_center_seed_fields,
    )
    from tools.slippi.damage_history import derive_damage_time_since_hit_x18ac
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
        derive_camera_box_visible_x221f_b0,
        derive_camera_target_point_inside_stage_cam_bounds,
        derive_camera_target_world,
        derive_rebirth_camera_anchor_y,
        derive_capture_grab_hidden_post,
        derive_grab_mash_stick_sign_post,
        derive_grab_owner_port_2p,
        derive_seed_prev_action_post,
        derive_guard_reflect_timer_x14,
        derive_guard_reflect_timer_x18,
        derive_guard_release_lockout_and_lightshield,
        derive_guard_setoff_hitlag_damage_min,
        derive_guard_setoff_hitlag_exit_phase,
        derive_guard_setoff_post_hitlag_owner,
        derive_guard_tilt_state,
        derive_dash_x4,
        derive_runbrake_cmd0,
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
            handicap=int(p.get("handicap", 9)),
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
    grab_mash_stick_threshold = float(common["grab_mash_stick_threshold"])
    fastfall_stick_threshold = float(common["fastfall_stick_threshold"])
    fastfall_tilt_max_frames = int(common["fastfall_tilt_max_frames"])
    guard_stick_lerp_x44c = float(common["guard_stick_lerp_x44c"])
    lcancel_window_frames = int(common["lcancel_window_frames"])
    lcancel_lag_div = float(common["lcancel_lag_div"])
    landing_fall_special_lag_frames = float(common["landing_fall_special_lag_frames"])

    data_root = Path("data")
    end_frames = load_end_frame_tables(data_root)
    char_landing_air_lag_frames: dict[int, dict[str, int]] = {}
    char_walk_divisors: dict[int, tuple[float, float, float]] = {}
    char_walk_max: dict[int, float] = {}
    char_run_scaling: dict[int, float] = {}
    char_gr_friction: dict[int, float] = {}
    char_active_shield_hit_int_damage: dict[int, dict[int, dict[int, int]]] = {}

    def _get_env_dmg_local(dmg: float) -> int:
        if float(dmg) == 0.0:
            return 0
        i = int(dmg)
        return i if i != 0 else 1

    def _stale_multiplier_from_seed_queue(queue_index: int, queue_move_ids: np.ndarray, move_id: int) -> float:
        if move_id in (0xFFFF, 1):
            return 1.0
        qi = int(queue_index) if 0 <= int(queue_index) < 10 else 0
        pos = qi - 1 if qi != 0 else 9
        mult = 1.0
        for i in range(9):
            mid = int(queue_move_ids[pos])
            if mid == 0:
                return mult
            if mid == move_id:
                mult -= float(stale_weights[i])
            pos = pos - 1 if pos != 0 else 9
        return mult

    stale_weights_buf = (data_root / "staling" / "weights.bin").read_bytes()
    stale_weight_count = int(struct.unpack_from("<H", stale_weights_buf, 12)[0])
    stale_weights = struct.unpack_from("<" + "f" * stale_weight_count, stale_weights_buf, 20)

    for cid in (1, 22):
        key = "fox" if cid == 1 else "falco"
        attrs = json.loads((data_root / "characters" / f"{key}.json").read_text())
        move_data = json.loads((data_root / "moves" / f"{key}.json").read_text())["moves"]
        char_landing_air_lag_frames[int(cid)] = {
            "airn": int(attrs["landing_airn_lag_frames"]),
            "airf": int(attrs["landing_airf_lag_frames"]),
            "airb": int(attrs["landing_airb_lag_frames"]),
            "airhi": int(attrs["landing_airhi_lag_frames"]),
            "airlw": int(attrs["landing_airlw_lag_frames"]),
        }
        char_walk_divisors[int(cid)] = (
            float(attrs["slow_walk_max"]),
            float(attrs["mid_walk_point"]),
            float(attrs["fast_walk_min"]),
        )
        char_walk_max[int(cid)] = float(attrs["walk_max_vel"])
        char_run_scaling[int(cid)] = float(attrs["run_animation_scaling"])
        char_gr_friction[int(cid)] = float(attrs["gr_friction"])
        active_int_damage_by_anim: dict[int, dict[int, int]] = {}
        for move in move_data.values():
            submotion_id = int(move.get("submotion_id", -1))
            if submotion_id < 0:
                continue
            events = sorted(move.get("events", []), key=lambda ev: (int(ev.get("frame", 0)), ev.get("kind", "")))
            events_by_frame: dict[int, list[dict]] = {}
            max_frame = 0
            for ev in events:
                frame = int(ev.get("frame", 0))
                events_by_frame.setdefault(frame, []).append(ev)
                max_frame = max(max_frame, frame)
            active_by_hitbox: dict[int, int] = {}
            frame_damage: dict[int, int] = {}
            for frame in range(0, max_frame + 2):
                for ev in events_by_frame.get(frame, []):
                    kind = ev.get("kind")
                    if kind == "create_hitbox":
                        hb = ev.get("data", {}).get("hitbox", {})
                        hb_id = int(hb.get("hitbox_id", 0))
                        active_by_hitbox[hb_id] = _get_env_dmg_local(float(hb.get("damage", 0.0)))
                    elif kind == "set_hitbox_damage":
                        hb_id = int(ev.get("data", {}).get("idx", 0))
                        if hb_id in active_by_hitbox:
                            active_by_hitbox[hb_id] = _get_env_dmg_local(
                                float(ev.get("data", {}).get("damage", 0.0))
                            )
                    elif kind == "remove_hitbox":
                        active_by_hitbox.pop(int(ev.get("data", {}).get("idx", 0)), None)
                    elif kind == "clear_hitboxes":
                        active_by_hitbox.clear()
                frame_damage[frame] = max(active_by_hitbox.values(), default=0)
            active_int_damage_by_anim[submotion_id] = frame_damage
        char_active_shield_hit_int_damage[int(cid)] = active_int_damage_by_anim

    # Guard-tilt table metadata (neutral frame + max frame) for decomp-shaped mv.co.guard.x8.
    shield_meta = load_shield_tilt_table_meta()
    neutral_lut = np.zeros(256, dtype=np.uint16)
    frame_max_lut = np.zeros(256, dtype=np.uint16)
    for cid, (neutral, frame_max) in shield_meta.items():
        neutral_lut[np.uint8(cid)] = np.uint16(int(neutral) & 0xFFFF)
        frame_max_lut[np.uint8(cid)] = np.uint16(int(frame_max) & 0xFFFF)

    # Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
    act_wait = 0x000E
    act_walk_slow = 0x000F
    act_walk_middle = 0x0010
    act_walk_fast = 0x0011
    act_turn = 0x0012
    act_turn_run = 0x0013
    act_dash = 0x0014
    act_run = 0x0015
    act_run_direct = 0x0016
    act_run_brake = 0x0017
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
    act_attack_11 = 0x002C
    act_attack_12 = 0x002D
    act_attack_13 = 0x002E
    act_attack_dash = 0x0032
    act_attack_air_n = 0x0041
    act_attack_air_f = 0x0042
    act_attack_air_b = 0x0043
    act_attack_air_hi = 0x0044
    act_attack_air_lw = 0x0045
    act_attack_lw4 = 0x0040
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
    throw_action_to_move = {
        int(act_throw_f): "ftCo_SM_ThrowF",
        int(act_throw_b): "ftCo_SM_ThrowB",
        int(act_throw_hi): "ftCo_SM_ThrowHi",
        int(act_throw_lw): "ftCo_SM_ThrowLw",
    }
    (
        throw_pulse_frames_by_char_action,
        throw_cmd1_start_by_char_action,
        throw_shot_itkind_by_char,
    ) = _load_throw_pulse_seed_tables(
        data_root=data_root,
        throw_action_to_move=throw_action_to_move,
    )
    runbrake_cmd0_on_by_char, runbrake_cmd0_off_by_char = _load_runbrake_cmd0_seed_tables(
        data_root=data_root
    )
    (
        source_clear_followup_cmd0_on_by_char_action,
        source_clear_followup_cmd0_off_by_char_action,
    ) = _load_source_clear_terminal_followup_tables(
        data_root=data_root,
    )
    action_x9_b1_by_char = _load_action_x9_b1_tables(data_root=data_root)
    # GALE01 p_ftCommonData->x814 initializes fp->dmg.x18C8 in Fighter_ChangeMotionState.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    source_clear_x18c8_init_frames = 60
    char_falco = 22
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
    reflector_damage_mul_lut = np.ones(256, dtype=np.float32)
    reflector_damage_mul_lut[np.uint8(1)] = np.float32(
        json.loads(Path("data/characters/fox.json").read_text())["reflector_damage_mul"]
    )
    reflector_damage_mul_lut[np.uint8(22)] = np.float32(
        json.loads(Path("data/characters/falco.json").read_text())["reflector_damage_mul"]
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
        st = static_by_port.get(port_1based, PortStatic(team_id=0, char_id=0, handicap=9))
        samples["seed_t"]["team_id"][:, slot] = np.uint8(st.team_id)
        samples["ref_t1"]["team_id"][:, slot] = np.uint8(st.team_id)
        samples["seed_t"]["handicap"][:, slot] = np.uint8(st.handicap)
        atk, df, scl = ratios_by_port.get(port_1based, (1.0, 1.0, 1.0))
        samples["seed_t"]["attack_ratio"][:, slot] = np.float32(atk)
        samples["seed_t"]["defense_ratio"][:, slot] = np.float32(df)
        # Fighter model scale (decomp: fp->x34_scale.y) comes from game-start settings (player.model_scale).
        samples["seed_t"]["fighter_scale_y"][:, slot] = np.float32(scl)

    # Fill inputs and per-port post state.
    post_action_id_u16 = np.zeros((n_frames, 4), dtype=np.uint16)
    post_state_age_all = np.zeros((n_frames, 4), dtype=np.int16)
    post_char_id_u8 = np.zeros((n_frames, 4), dtype=np.uint8)
    post_pos_x_all = np.zeros((n_frames, 4), dtype=np.float32)
    post_pos_y_all = np.zeros((n_frames, 4), dtype=np.float32)
    post_percent_all = np.zeros((n_frames, 4), dtype=np.float32)
    frame_speed_mul_all = np.zeros((n_frames, 4), dtype=np.float32)
    post_hitlag_u16_all = np.zeros((n_frames, 4), dtype=np.uint16)
    post_shield_f32_all = np.zeros((n_frames, 4), dtype=np.float32)
    post_animation_index_u32_all = np.zeros((n_frames, 4), dtype=np.uint32)
    lightshield_amount_all = np.zeros((n_frames, 4), dtype=np.float32)
    post_instance_hit_by_u16_all = np.zeros((n_frames, 4), dtype=np.uint16)
    post_state_flags_u8 = np.zeros((n_frames, 4, 5), dtype=np.uint8)
    post_turn_has_turned_u8 = np.zeros((n_frames, 4), dtype=np.uint8)
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
        post_char_id_u8[:, slot] = post_char
        post_action_id_u16[:, slot] = post_state
        post_pos_x_all[:, slot] = post_pos_x
        post_pos_y_all[:, slot] = post_pos_y
        # Slippi Z: prefer position.z when present, otherwise fall back to 0 (older schemas are 2D-only).
        post_pos_z = _post_position_z(post, n_frames)
        post_dir = _dir_to_facing(_to_numpy(post.field("direction")).astype(np.float32))
        post_percent = _to_numpy(post.field("percent")).astype(np.float32)
        post_percent_all[:, slot] = post_percent
        post_shield = _to_numpy(post.field("shield")).astype(np.float32)
        post_shield_f32_all[:, slot] = post_shield
        post_stocks = _to_numpy(post.field("stocks")).astype(np.uint8)
        post_jumps = _to_numpy(post.field("jumps")).astype(np.uint8)
        post_airborne = _to_numpy(post.field("airborne")).astype(np.uint8)
        post_on_ground = _airborne_to_on_ground(post_airborne, n_frames)
        fighter_scale_y = np.full(n_frames, np.float32(scl), dtype=np.float32)
        post_hitlag = _u16_from_float_frames(_to_numpy(post.field("hitlag")).astype(np.float32), n_frames)
        post_hitlag_u16_all[:, slot] = post_hitlag
        post_misc_as = _to_numpy(post.field("misc_as")).astype(np.float32)
        post_state_age_f32 = _to_numpy(post.field("state_age")).astype(np.float32)
        post_state_age = _i16_from_state_age(post_state_age_f32, n_frames)
        post_state_age_all[:, slot] = post_state_age
        post_anim_frame_f32 = _f32_from_state_age(post_state_age_f32, n_frames)

        hurtbox_state = _to_numpy(post.field("hurtbox_state")).astype(np.uint8)
        l_cancel = _to_numpy(post.field("l_cancel")).astype(np.uint8)
        ground_id = _to_numpy(post.field("ground")).astype(np.uint16)
        animation_index = _to_numpy(post.field("animation_index")).astype(np.uint32)
        post_animation_index_u32_all[:, slot] = animation_index
        instance_hit_by = _to_numpy(post.field("last_hit_by_instance")).astype(np.uint16)
        post_instance_hit_by_u16_all[:, slot] = instance_hit_by
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
        post_state_flags_u8[:, slot, :] = state_flags

        vel = post.field("velocities")
        speed_air_x_self = _to_numpy(vel.field("self_x_air")).astype(np.float32)
        speed_y_self = _to_numpy(vel.field("self_y")).astype(np.float32)
        speed_x_attack = _to_numpy(vel.field("knockback_x")).astype(np.float32)
        speed_y_attack = _to_numpy(vel.field("knockback_y")).astype(np.float32)
        speed_ground_x_self = _to_numpy(vel.field("self_x_ground")).astype(np.float32)
        post_instance_id_slot = _to_numpy(post.field("instance_id")).astype(np.uint16)

        # Seed uses post at (i), ref uses post at (i+1).
        samples["seed_t"]["char_id"][:, slot] = post_char[:-1]
        samples["ref_t1"]["char_id"][:, slot] = post_char[1:]

        samples["seed_t"]["action_id"][:, slot] = post_state[:-1]
        samples["ref_t1"]["action_id"][:, slot] = post_state[1:]
        samples["seed_t"]["action_frame"][:, slot] = post_state_age[:-1]
        samples["ref_t1"]["action_frame"][:, slot] = post_state_age[1:]
        seed_prev_action_id, seed_prev_action_frame = derive_seed_prev_action_post(
            post_action_id_u16=post_state,
            post_action_frame_i16=post_state_age,
        )
        samples["seed_t"]["seed_prev_action_id"][:, slot] = seed_prev_action_id
        samples["seed_t"]["seed_prev_action_frame"][:, slot] = seed_prev_action_frame
        # Throw pulse-consume seed lane is filled after item materialization from full seed_t arrays.
        samples["seed_t"]["throw_pulse_consumed"][:, slot] = 0
        samples["seed_t"]["throw_pulse_crossed_prev_frame"][:, slot] = 0
        samples["seed_t"]["source_clear_owner_set_phase"][:, slot] = 0
        samples["seed_t"]["source_clear_processhit_damage_pending_phase"][:, slot] = 0
        samples["seed_t"]["fighter_8006cda4_pre_gate_consume_count"][:, slot] = 0
        samples["seed_t"]["source_clear_grounded_damage_clear_phase"][:, slot] = 0
        samples["seed_t"]["source_clear_terminal_phase"][:, slot] = 0
        # fp+0x2340 AttackDash lane (decomp-backed targeted ownership seed):
        # - mv.co.attackdash.x0 is consumed by ftCo_800D8AE0 during AttackDash IASA.
        # - Slippi emits fp+0x2340 as `misc_as`; AttackDash treats this lane as signed int.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
        # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_800D8AE0
        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        post_attackdash_x0 = np.zeros(n_frames, dtype=np.int16)
        attackdash_mask = post_state == np.uint16(act_attack_dash)
        post_attackdash_x0[attackdash_mask] = np.clip(
            post_misc_as[attackdash_mask].astype(np.int32),
            np.iinfo(np.int16).min,
            np.iinfo(np.int16).max,
        ).astype(np.int16)
        samples["seed_t"]["attackdash_x0"][:, slot] = post_attackdash_x0[:-1]
        # fp+0x2340 Attack1 lane (decomp-backed targeted ownership seed):
        # - mv.co.attack1.x0 is latched intent consumed by checkAttack12/checkAttack13.
        # - Slippi emits fp+0x2340 as `misc_as`; this lane is a bool in Attack11/Attack12/Attack13.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{checkAttack12,checkAttack13}
        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        post_jab_x0 = np.zeros(n_frames, dtype=np.uint8)
        jab_mask = (
            (post_state == np.uint16(act_attack_11))
            | (post_state == np.uint16(act_attack_12))
            | (post_state == np.uint16(act_attack_13))
        )
        post_jab_x0[jab_mask] = (post_misc_as[jab_mask] > 0.0).astype(np.uint8)
        samples["seed_t"]["jab_x0"][:, slot] = post_jab_x0[:-1]
        port0 = int(src_ports[slot]) - 1
        samples["seed_t"]["match_flow_timer"][:, slot] = _derive_match_flow_timer(
            action_id_u16=post_state, port0=port0, common=common
        )[:-1]
        samples["seed_t"]["opening_input_lock_timer"][:, slot] = _derive_opening_input_lock_timer(
            frame_id_i32=frame_ids
        )[:-1]
        samples["seed_t"]["entry_end_fall_lock"][:, slot] = _derive_entry_end_fall_lock(
            action_id_u16=post_state, on_ground_u8=post_on_ground
        )[:-1]
        samples["seed_t"]["camera_box_visible_x221f_b0"][:, slot] = derive_camera_box_visible_x221f_b0(
            state_flags_u8=state_flags
        )[:-1]
        # Rebirth camera subject anchor Y (`fp->mv.co.common.x8`) is a hidden match-flow lane owned
        # by ftCo_Rebirth_Cam. On Final Destination it comes from the stage respawn-point Y rather
        # than the fighter's replay-visible cur_pos.y.
        # refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
        # data/stages/final_destination.json: respawn_points
        samples["seed_t"]["rebirth_camera_anchor_y_f32"][:, slot] = derive_rebirth_camera_anchor_y(
            action_id_u16=post_state,
            stage_id_u32=int(stage_id),
            respawn_point_y=_respawn_point_y_for_stage_port(stage_id=int(stage_id), port0=port0),
        )[:-1]
        (
            camera_target_world_x,
            camera_target_world_y,
            camera_target_world_z,
            camera_box_radius,
        ) = derive_camera_target_world(
            char_id_u8=post_char,
            animation_index_u32=animation_index,
            anim_frame_f32=post_anim_frame_f32,
            fighter_scale_y_f32=fighter_scale_y,
            facing_u8=post_dir,
            pos_x_f32=post_pos_x,
            pos_y_f32=post_pos_y,
            pos_z_f32=post_pos_z,
        )
        samples["seed_t"]["camera_target_world_x_f32"][:, slot] = camera_target_world_x[:-1]
        samples["seed_t"]["camera_target_world_y_f32"][:, slot] = camera_target_world_y[:-1]
        samples["seed_t"]["camera_target_world_z_f32"][:, slot] = camera_target_world_z[:-1]
        samples["seed_t"]["camera_box_radius_f32"][:, slot] = camera_box_radius[:-1]
        samples["seed_t"]["camera_target_point_inside_stage_cam_bounds_u8"][:, slot] = (
            derive_camera_target_point_inside_stage_cam_bounds(
                stage_id_u32=int(stage_id),
                camera_target_world_x_f32=camera_target_world_x,
                camera_target_world_y_f32=camera_target_world_y,
                camera_box_radius_f32=camera_box_radius,
            )[:-1]
        )
        samples["seed_t"]["downwait_timer"][:, slot] = derive_downwait_timer(
            action_id_u16=post_state,
            down_wait_frames=int(common["down_wait_frames"]),
            act_down_wait_u=act_down_wait_u,
            act_down_wait_d=act_down_wait_d,
        )[:-1]
        samples["seed_t"]["passivewall_timer"][:, slot] = _derive_passivewall_timer(
            action_id_u16=post_state,
            action_frame_i16=post_state_age,
            common=common,
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
        # fp->facing_dir1 seeded lane (signed).
        facing_dir1_post = _derive_facing_dir1_sign(facing_u8=post_dir, action_id_u16=post_state)
        samples["seed_t"]["facing_dir1"][:, slot] = facing_dir1_post[:-1]
        # Ground friction multiplier lane used by grounded-KB decay.
        # Decomp source is ft_GetGroundFrictionMultiplier(fp); Slippi currently exposes no direct
        # post-frame lane for this value in-suite, so seed explicit default identity.
        samples["seed_t"]["ground_friction_mul"][:, slot] = np.float32(1.0)
        # Smash-charge gate lane (fp->smash_attrs.state == Charging) when present in schema.
        samples["seed_t"]["kb_smashcharge_active"][:, slot] = _derive_kb_smashcharge_active_from_post(
            post=post
        )[:-1]
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
        damage_time_since_hit_x18ac = derive_damage_time_since_hit_x18ac(
            action_id_u16=post_state,
            hitlag_u16=post_hitlag,
            hitstun_u16=post_hitstun,
            state_flags_u8=state_flags,
        )
        samples["seed_t"]["damage_time_since_hit_x18ac"][:, slot] = damage_time_since_hit_x18ac[:-1]
        source_clear_timer_x18c8, source_clear_owner_set_phase = (
            _derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes(
                action_id_u16=post_state,
                char_id_u8=post_char,
                on_ground_u8=post_on_ground,
                state_flags_u8=state_flags,
                last_hit_by_u8=last_hit_by,
                x9_b1_by_char=action_x9_b1_by_char,
                source_clear_init_frames=source_clear_x18c8_init_frames,
            )
        )
        samples["seed_t"]["source_clear_timer_x18c8"][:, slot] = source_clear_timer_x18c8[:-1]
        samples["seed_t"]["source_clear_owner_set_phase"][:, slot] = source_clear_owner_set_phase[:-1]
        samples["seed_t"]["source_clear_grounded_damage_clear_phase"][:, slot] = (
            _derive_source_clear_grounded_damage_clear_phase_seed_lane(
                action_id_u16=post_state,
                action_frame_i16=post_state_age,
                on_ground_u8=post_on_ground,
                hitlag_u16=post_hitlag,
                hitstun_u16=post_hitstun,
                combo_count_u8=combo_count,
                source_clear_timer_x18c8_u8=source_clear_timer_x18c8,
                source_clear_owner_set_phase_u8=source_clear_owner_set_phase,
                state_flags_u8=state_flags,
                last_hit_by_u8=last_hit_by,
            )[:-1]
        )
        samples["seed_t"]["source_clear_terminal_phase"][:, slot] = (
            _derive_source_clear_terminal_phase_seed_lane(
                char_id_u8=post_char,
                action_id_u16=post_state,
                action_frame_i16=post_state_age,
                hitlag_u16=post_hitlag,
                hitstun_u16=post_hitstun,
                combo_count_u8=combo_count,
                last_attack_landed_u8=last_attack_landed,
                source_clear_timer_x18c8_u8=source_clear_timer_x18c8,
                source_clear_owner_set_phase_u8=source_clear_owner_set_phase,
                state_flags_u8=state_flags,
                last_hit_by_u8=last_hit_by,
                terminal_followup_cmd0_on_by_char_action=source_clear_followup_cmd0_on_by_char_action,
                terminal_followup_cmd0_off_by_char_action=source_clear_followup_cmd0_off_by_char_action,
            )[:-1]
        )

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
        lightshield_amount_all[:, slot] = lightshield_amount
        guard_setoff_hitlag_damage_min = derive_guard_setoff_hitlag_damage_min(
            action_id=post_state,
            action_frame_i16=post_state_age,
            hitlag=post_hitlag,
            hitlag_dmg_mul=float(common["hitlag_dmg_mul"]),
            hitlag_base=float(common["hitlag_base"]),
            act_guard_set_off=act_guard_set_off,
        )
        samples["seed_t"]["guard_setoff_hitlag_damage_min"][:, slot] = guard_setoff_hitlag_damage_min[:-1]
        guard_setoff_hitlag_exit_phase = derive_guard_setoff_hitlag_exit_phase(
            action_id=post_state,
            hitlag=post_hitlag,
            act_guard_set_off=act_guard_set_off,
        )
        samples["seed_t"]["guard_setoff_hitlag_exit_phase_u8"][:, slot] = guard_setoff_hitlag_exit_phase[:-1]
        samples["seed_t"]["guard_setoff_post_hitlag_owner_u8"][:, slot] = derive_guard_setoff_post_hitlag_owner(
            action_id=post_state,
            guard_setoff_hitlag_exit_phase_u8=guard_setoff_hitlag_exit_phase,
            state_flags_221c_u8=state_flags[:, 3],
            act_guard_set_off=act_guard_set_off,
        )[:-1]

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
        frame_speed_mul_all[:, slot] = frame_speed_mul
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
        # Walk callback-owned source velocity (`mv_x0` in ftWalkCommon_800DFDDC) reconstructed
        # from the seeded walk anim-rate lane.
        samples["seed_t"]["walk_anim_source_vel_f32"][:, slot] = _derive_walk_anim_source_vel_seed_lane(
            action_id_u16=post_state,
            char_id_u8=post_char,
            facing_dir1_i8=facing_dir1_post,
            frame_speed_mul_f32=frame_speed_mul,
            walk_divisors_by_char=char_walk_divisors,
        )[:-1]
        samples["seed_t"]["walk_retarget_tick_source_vel_f32"][:, slot] = (
            _derive_walk_retarget_tick_source_vel_seed_lane(
                action_id_u16=post_state,
                char_id_u8=post_char,
                facing_dir1_i8=facing_dir1_post,
                anim_frame_f32=post_anim_frame_f32,
                ref_action_frame_i16=post_state_age,
                speed_ground_x_self_f32=speed_ground_x_self,
                walk_anim_source_vel_f32=samples["seed_t"]["walk_anim_source_vel_f32"][:, slot],
                walk_divisors_by_char=char_walk_divisors,
                walk_max_by_char=char_walk_max,
                walk_mid_vel_mul=float(common["walk_mid_vel_mul"]),
                walk_fast_vel_mul=float(common["walk_fast_vel_mul"]),
                end_frames=end_frames,
            )[:-1]
        )
        samples["seed_t"]["run_anim_source_vel_f32"][:, slot] = _derive_run_anim_source_vel_seed_lane(
            action_id_u16=post_state,
            char_id_u8=post_char,
            facing_dir1_i8=facing_dir1_post,
            frame_speed_mul_f32=frame_speed_mul,
            run_scaling_by_char=char_run_scaling,
        )[:-1]
        turn_kneebend_face = np.zeros(n_frames - 1, dtype=np.uint8)
        turn_kneebend_hidden_face = (
            (post_state[:-1] == np.uint16(act_turn))
            & (post_state[1:] == np.uint16(act_kneebend))
            & (post_state_age[:-1] == np.int16(1))
            & (post_dir[1:] != post_dir[:-1])
        )
        # 0 = no override; 1 = left; 2 = right. This is intentionally non-causal and Turn-only:
        # the replay-visible next row is the first place Slippi exposes the hidden Turn jump-facing
        # owner for first-tick Turn->KneeBend entries.
        turn_kneebend_face[turn_kneebend_hidden_face] = (post_dir[1:][turn_kneebend_hidden_face] + 1).astype(
            np.uint8
        )
        samples["seed_t"]["turn_kneebend_facing_override_u8"][:, slot] = turn_kneebend_face

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
        # Fastfall ownership at immediate hitlag-exit rows (decomp-shaped reseed lane):
        # - Hitlag is decremented first in Fighter_8006A1BC, then Fighter_8006A360 runs the
        #   non-hitlag callback/physics lane where ftCommon_CheckFallFast ownership applies.
        # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_CheckFallFast
        samples["seed_t"]["fall_fast_hitlag_exit_owner"][:, slot] = (
            (post_hitlag[:-1] == np.uint16(1)) & fastfall_ok[:-1]
        ).astype(np.uint8)
        run_x0 = derive_run_x0(
            action_id=post_state,
            hitlag_u16=post_hitlag,
            run_x0_init_x430=float(common["run_x0_init_x430"]),
            act_run=act_run,
            act_run_direct=act_run_direct,
            act_turn_run=act_turn_run,
        )
        samples["seed_t"]["run_x0"][:, slot] = run_x0[:-1]
        runbrake_cmd0 = derive_runbrake_cmd0(
            action_id_u16=post_state,
            anim_frame_f32=post_anim_frame_f32,
            char_id_u8=post_char,
            cmd0_on_by_char=runbrake_cmd0_on_by_char,
            cmd0_off_by_char=runbrake_cmd0_off_by_char,
            act_run_brake=act_run_brake,
        )
        samples["seed_t"]["runbrake_cmd0"][:, slot] = runbrake_cmd0[:-1]
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
        x673, x674, x676_x, x2228_b7, x677_y, x679_x, x67A_y = compute_fighter_stick_input_counters(
            stick_x_unit=stick_x,
            stick_y_unit=stick_y,
            tilt_thresh_x=lstick_tilt_x_thresh,
            tilt_thresh_y=lstick_tilt_y_thresh,
            start_timer=0xFE,
        )
        samples["seed_t"]["x673"][:, slot] = x673[:-1]
        samples["seed_t"]["x674"][:, slot] = x674[:-1]
        samples["seed_t"]["x676_x"][:, slot] = x676_x[:-1]
        samples["seed_t"]["x2228_b7"][:, slot] = x2228_b7[:-1]
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
            hitlag_frames=post_hitlag,
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
            action_frame_i16=post_state_age,
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
        post_turn_has_turned_u8[:, slot] = turn_has_turned

    # GuardSetOff frozen-hitlag frame-speed ownership (strictly causal):
    # - ftColl_80076CBC writes the defender's hidden x19A4 from the current shield-hit max int
    #   damage before ftCo_80092F2C enters GuardSetOff.
    # - ftCo_80092F2C then derives `fp->frame_speed_mul` from x19A4 and the current
    #   lightshield_amount; Fighter_8006A360 freezes state_age while hitlag is active.
    # - Slippi does not expose x19A4 directly, so reconstruct the GuardSetOff entry rate from
    #   current/past replay-visible state: active attacker hitbox damage from extracted move data,
    #   stale queue state, shield HP drop when it reveals the hidden lightshield lane, and the
    #   decomp common-params formula. This preserves frame_speed_mul_f32 as a prefix-causal seed.
    #
    # Decomp/data anchors:
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # - refs/melee/src/melee/ft/ft_0881.c::ft_80089228
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    # - data/moves/{fox,falco}.json create_hitbox damage timelines
    shield_hit_mul = float(common["shield_hit_damage_mul"])
    shield_hit_base = float(common["shield_hit_damage_base"])
    shield_hit_ls_min = float(common["shield_hit_lightshield_min"])
    shield_hit_ls_max = float(common["shield_hit_lightshield_max"])
    shield_stun_mul = float(common["shield_stun_mul"])
    shield_stun_base = float(common["shield_stun_base"])
    shield_stun_ls_min = float(common["shield_stun_lightshield_min"])
    shield_stun_ls_max = float(common["shield_stun_lightshield_max"])

    def _guardsetoff_infer_hidden_light(i: int, defender: int, int_dmg: int) -> tuple[bool, float]:
        light = float(lightshield_amount_all[i, defender])
        if light > 0.0:
            return True, min(max(light, 0.0), 1.0)
        if i <= 0 or int_dmg <= 0 or shield_hit_mul <= 0.0:
            return False, min(max(light, 0.0), 1.0)
        shield_drop = float(post_shield_f32_all[i - 1, defender]) - float(post_shield_f32_all[i, defender])
        if shield_drop <= 0.0:
            return False, min(max(light, 0.0), 1.0)
        hit_den = shield_hit_mul * float(int_dmg)
        if hit_den <= 0.0:
            return False, min(max(light, 0.0), 1.0)
        hit_light_term = 1.0 - ((shield_drop - shield_hit_base) / hit_den)
        if not np.isfinite(hit_light_term):
            return False, min(max(light, 0.0), 1.0)
        if shield_hit_ls_max == shield_hit_ls_min:
            return False, min(max(light, 0.0), 1.0)
        inferred = (hit_light_term - shield_hit_ls_min) / (shield_hit_ls_max - shield_hit_ls_min)
        if inferred < -0.001 or inferred > 1.001:
            return False, min(max(light, 0.0), 1.0)
        return True, min(max(float(inferred), 0.0), 1.0)

    def _guardsetoff_active_int_damage(i: int, defender: int) -> int:
        best = 0
        for attacker in range(num_players):
            if attacker == defender or int(post_hitlag_u16_all[i, attacker]) == 0:
                continue
            anim_idx = int(post_animation_index_u32_all[i, attacker])
            anim_frame = int(post_state_age_all[i, attacker])
            if anim_frame < 0:
                anim_frame = 0
            int_dmg = (
                char_active_shield_hit_int_damage.get(int(post_char_id_u8[i, attacker]), {})
                .get(anim_idx, {})
                .get(anim_frame, 0)
            )
            if int_dmg <= 0:
                continue
            move_id = int(hist.attack_id[i, attacker])
            stale_mult = _stale_multiplier_from_seed_queue(
                int(hist.stale_queue_index[i, attacker]),
                hist.stale_move_id[i, attacker],
                move_id,
            )
            int_dmg = _get_env_dmg_local(float(int_dmg) * stale_mult)
            if int_dmg > best:
                best = int_dmg
        return best

    for defender in range(num_players):
        carry_rate = np.float32(0.0)
        for i in range(n_frames):
            if int(post_action_id_u16[i, defender]) != int(act_guard_set_off):
                carry_rate = np.float32(0.0)
                continue
            cur_hl = int(post_hitlag_u16_all[i, defender])
            prev_action = int(post_action_id_u16[i - 1, defender]) if i > 0 else -1
            prev_hl = int(post_hitlag_u16_all[i - 1, defender]) if i > 0 else 0
            prev_af = int(post_state_age_all[i - 1, defender]) if i > 0 else 0
            cur_af = int(post_state_age_all[i, defender])
            segment_entry = (
                i == 0
                or prev_action != int(act_guard_set_off)
                or cur_hl > prev_hl
                or cur_af < prev_af
            )
            if segment_entry and cur_hl > 0:
                int_dmg = _guardsetoff_active_int_damage(i, defender)
                if int_dmg > 0:
                    inferred_light_ok, hidden_light = _guardsetoff_infer_hidden_light(i, defender, int_dmg)
                    powershield_active = (int(post_state_flags_u8[i, defender, 3]) & 0x20) != 0
                    if not (powershield_active or inferred_light_ok):
                        carry_rate = np.float32(0.0)
                        continue
                    stun_light_term = hidden_light * (shield_stun_ls_max - shield_stun_ls_min) + shield_stun_ls_min
                    stun_frames = shield_stun_mul * (float(int_dmg) * (1.0 - stun_light_term)) + shield_stun_base
                    # GuardSetOff uses ftCo_SM_GuardDamage as its submotion.
                    end_frame = end_frames.by_char_id.get(int(post_char_id_u8[i, defender]), {}).get(40)
                    if end_frame is not None and stun_frames > 0.0:
                        carry_rate = np.float32((np.float32(end_frame) + np.float32(0.1)) / np.float32(stun_frames))
            if cur_hl > 0 and float(carry_rate) > 0.0:
                frame_speed_mul_all[i, defender] = carry_rate
        samples["seed_t"]["frame_speed_mul_f32"][:, defender] = frame_speed_mul_all[:-1, defender]

    # GuardSetOff hidden exit-rate reconstruction (intentionally non-causal, explicit lane):
    # Some GuardSetOff last-hitlag rows do not contain enough current/past replay-visible state to
    # reconstruct the exact ftCo_80092F2C x19A4/lightshield-owned rate. The first same-segment
    # non-hitlag GuardSetOff post-frame exposes that hidden rate after hitlag exits, so carry it in a
    # GuardSetOff-specific seed lane instead of weakening the general frame_speed_mul_f32 contract.
    #
    # Runtime consumes this only for GuardSetOff phase-2 rows; 0.0 means no explicit override.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_GuardSetOff_Anim}
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    guard_setoff_exit_frame_speed = np.zeros((n_frames, 4), dtype=np.float32)
    for defender in range(num_players):
        for i in range(0, max(0, n_frames - 1)):
            if (
                int(post_action_id_u16[i, defender]) == int(act_guard_set_off)
                and int(post_hitlag_u16_all[i, defender]) == 1
                and int(post_action_id_u16[i + 1, defender]) == int(act_guard_set_off)
                and int(post_hitlag_u16_all[i + 1, defender]) == 0
            ):
                rate = np.float32(frame_speed_mul_all[i + 1, defender])
                if float(rate) > 0.0 and np.isfinite(float(rate)):
                    guard_setoff_exit_frame_speed[i, defender] = rate
        samples["seed_t"]["guard_setoff_exit_frame_speed_mul_f32"][:, defender] = (
            guard_setoff_exit_frame_speed[:-1, defender]
        )

    # Owner-indexed ProcessHit source-clear bridge depends on current source-owner seed-visible
    # motion state from the other port, so derive it after all per-port seed arrays are populated.
    for slot in range(num_players):
        samples["seed_t"]["source_clear_processhit_damage_pending_phase"][:, slot] = (
            _derive_source_clear_processhit_damage_pending_phase_seed_lane(
                action_id_u16=samples["seed_t"]["action_id"][:, slot],
                action_frame_i16=samples["seed_t"]["action_frame"][:, slot],
                on_ground_u8=samples["seed_t"]["on_ground"][:, slot],
                hitlag_u16=samples["seed_t"]["hitlag"][:, slot],
                hitstun_u16=samples["seed_t"]["hitstun"][:, slot],
                combo_count_u8=samples["seed_t"]["combo_count"][:, slot],
                last_attack_landed_u8=samples["seed_t"]["last_attack_landed"][:, slot],
                source_clear_timer_x18c8_u8=samples["seed_t"]["source_clear_timer_x18c8"][:, slot],
                source_clear_owner_set_phase_u8=samples["seed_t"]["source_clear_owner_set_phase"][
                    :, slot
                ],
                colanim_hit_status_x198c_u8=samples["seed_t"]["colanim_hit_status_x198c"][:, slot],
                state_flags_u8=samples["seed_t"]["state_flags"][:, slot, :],
                last_hit_by_u8=samples["seed_t"]["last_hit_by"][:, slot],
            )
        )
        samples["seed_t"]["fighter_8006cda4_pre_gate_consume_count"][:, slot] = (
            _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(
                action_id_u16=samples["seed_t"]["action_id"][:, slot],
                action_frame_i16=samples["seed_t"]["action_frame"][:, slot],
                on_ground_u8=samples["seed_t"]["on_ground"][:, slot],
                hitlag_u16=samples["seed_t"]["hitlag"][:, slot],
                hitstun_u16=samples["seed_t"]["hitstun"][:, slot],
                state_flags_u8=samples["seed_t"]["state_flags"][:, slot, :],
                last_hit_by_u8=samples["seed_t"]["last_hit_by"][:, slot],
                all_action_id_u16=samples["seed_t"]["action_id"][:, :num_players],
                all_action_frame_i16=samples["seed_t"]["action_frame"][:, :num_players],
                victim_port=slot,
                num_players=num_players,
            )
        )

    illusion_ghost_pos0_x, illusion_ghost_pos0_y, illusion_ghost_pos1_x, illusion_ghost_pos1_y = (
        derive_illusion_ghost_pos01(
        post_action_id_u16=post_action_id_u16,
        post_action_frame_i16=post_state_age_all,
        post_pos_x=post_pos_x_all,
        post_pos_y=post_pos_y_all,
        )
    )
    samples["seed_t"]["illusion_ghost_pos0_x"][:, :num_players] = illusion_ghost_pos0_x[
        :-1, :num_players
    ]
    samples["seed_t"]["illusion_ghost_pos0_y"][:, :num_players] = illusion_ghost_pos0_y[
        :-1, :num_players
    ]
    samples["seed_t"]["illusion_ghost_pos1_x"][:, :num_players] = illusion_ghost_pos1_x[:-1, :num_players]
    samples["seed_t"]["illusion_ghost_pos1_y"][:, :num_players] = illusion_ghost_pos1_y[:-1, :num_players]
    # Grounded attacker-on-shield knockback scalar (`fp->xF4_ground_attacker_shield_kb_vel`).
    #
    # Decomp:
    # - On shield hit, ftColl_80076CBC stores `attacker.dmg.x1928 = defender.lightshield_amount * int_dmg`
    #   and a sign in `attacker.dmg.x192C` from relative X positions.
    # - Fighter_ProcessHit_8006D1EC then shapes grounded attacker shield KB as:
    #     eval = x1928 * x3E0 + x3E4
    #     xF4_ground_attacker_shield_kb_vel = +/-eval
    # - While hitlag is active the main Fighter_procUpdate integration block is skipped, so this
    #   scalar carries unchanged through the frozen shield-hit segment.
    # - On each later grounded frame, Fighter_procUpdate decays the scalar through
    #   `ftCommon_8007CE4C(gr_friction * x3EC)` before projecting it onto the floor tangent.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_procUpdate}
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007CE4C,ftCommon_8007E2A4}
    seed_action_id = samples["seed_t"]["action_id"]
    seed_attack_id = samples["seed_t"]["attack_id"]
    seed_anim_frame_f32 = samples["seed_t"]["anim_frame_f32"]
    seed_animation_index = samples["seed_t"]["animation_index"]
    seed_on_ground = samples["seed_t"]["on_ground"]
    seed_hitlag = samples["seed_t"]["hitlag"]
    seed_pos_x = samples["seed_t"]["pos_x"]
    seed_char_id = samples["seed_t"]["char_id"]
    seed_ground_friction_mul = samples["seed_t"]["ground_friction_mul"]
    seed_lightshield_amount = samples["seed_t"]["lightshield_amount"]
    seed_guard_setoff_hitlag_damage_min = samples["seed_t"]["guard_setoff_hitlag_damage_min"]
    seed_stale_queue_index = samples["seed_t"]["stale_queue_index"]
    seed_stale_move_id = samples["seed_t"]["stale_move_id"]
    attacker_shield_ground_kb_vel = np.zeros((n_samples, 4), dtype=np.float32)
    shield_kb_mul = float(common["shield_attacker_ground_kb_mul"])
    shield_kb_base = float(common["shield_attacker_ground_kb_base"])
    shield_kb_friction_mul = float(common["shield_attacker_ground_friction_mul"])
    for attacker in range(num_players):
        kb = np.float32(0.0)
        for i in range(n_samples):
            if int(seed_on_ground[i, attacker]) == 0:
                kb = np.float32(0.0)
                attacker_shield_ground_kb_vel[i, attacker] = kb
                continue

            onset_kb: np.float32 | None = None
            if int(seed_hitlag[i, attacker]) > 0:
                for defender in range(num_players):
                    if defender == attacker:
                        continue
                    if int(seed_hitlag[i, defender]) == 0:
                        continue
                    defender_action = int(seed_action_id[i, defender])
                    if defender_action not in (act_guard_set_off, act_guard_reflect):
                        continue
                    prev_attacker_hitlag = int(seed_hitlag[i - 1, attacker]) if i > 0 else 0
                    prev_defender_hitlag = int(seed_hitlag[i - 1, defender]) if i > 0 else 0
                    prev_defender_dmg = int(seed_guard_setoff_hitlag_damage_min[i - 1, defender]) if i > 0 else 0
                    if prev_attacker_hitlag != 0 and prev_defender_hitlag != 0 and prev_defender_dmg > 0:
                        continue

                    int_dmg = 0
                    anim_idx = int(seed_animation_index[i, attacker])
                    anim_frame = int(np.floor(float(seed_anim_frame_f32[i, attacker])))
                    if anim_frame < 0:
                        anim_frame = 0
                    int_dmg = (
                        char_active_shield_hit_int_damage.get(int(seed_char_id[i, attacker]), {})
                        .get(anim_idx, {})
                        .get(anim_frame, 0)
                    )
                    if int_dmg > 0:
                        move_id = int(seed_attack_id[i, attacker])
                        stale_mult = _stale_multiplier_from_seed_queue(
                            int(seed_stale_queue_index[i, attacker]),
                            seed_stale_move_id[i, attacker],
                            move_id,
                        )
                        int_dmg = _get_env_dmg_local(float(int_dmg) * stale_mult)
                    if int_dmg <= 0:
                        int_dmg = int(seed_guard_setoff_hitlag_damage_min[i, defender])
                    if int_dmg <= 0:
                        continue

                    eval_kb = (
                        float(seed_lightshield_amount[i, defender]) * float(int_dmg) * shield_kb_mul
                        + shield_kb_base
                    )
                    onset_kb = np.float32(
                        -eval_kb if float(seed_pos_x[i, defender]) > float(seed_pos_x[i, attacker]) else eval_kb
                    )
                    break

            if onset_kb is not None:
                kb = onset_kb

            attacker_shield_ground_kb_vel[i, attacker] = kb

            cur_hitlag = int(seed_hitlag[i, attacker])
            if cur_hitlag > 1 or kb == np.float32(0.0):
                continue

            gr_friction = char_gr_friction.get(int(seed_char_id[i, attacker]), 0.0)
            friction = float(seed_ground_friction_mul[i, attacker]) * gr_friction * shield_kb_friction_mul
            if friction <= 0.0 or abs(friction) >= abs(float(kb)):
                kb = np.float32(0.0)
            elif kb < 0.0:
                kb = np.float32(float(kb) + friction)
            else:
                kb = np.float32(float(kb) - friction)

    samples["seed_t"]["attacker_shield_ground_kb_vel"] = attacker_shield_ground_kb_vel

    # Items are global per frame.
    items_fixed = _fill_items_fixed(frames, n_frames, src_ports=src_ports)
    _derive_item_attack_fields(
        items_fixed,
        fighter_attack_id=hist.attack_id,
        fighter_attack_instance=hist.attack_instance,
        num_players=num_players,
    )
    items_seed = _materialize_illusion_seed_positions(
        items_fixed,
        illusion_ghost_pos1_x=illusion_ghost_pos1_x,
        illusion_ghost_pos1_y=illusion_ghost_pos1_y,
        post_action_id_u16=post_action_id_u16,
        post_hitlag_u8=post_hitlag_u16_all,
        post_instance_hit_by_u16=post_instance_hit_by_u16_all,
        num_players=num_players,
    )
    item_reflect_damage_mul = _derive_item_reflect_damage_mul(
        items_fixed,
        post_action_id_u16=post_action_id_u16,
        post_char_id_u8=post_char_id_u8,
        post_state_flags_u8=post_state_flags_u8,
        powershield_reflect_damage_mul=float(common["powershield_reflect_damage_mul"]),
        reflector_damage_mul_lut=reflector_damage_mul_lut,
        num_players=num_players,
    )
    samples["seed_t"]["item_reflect_damage_mul"] = item_reflect_damage_mul[:-1]
    samples["seed_t"]["items"] = items_seed[:-1]
    # Throw pulse-consume seed lane (causal producer):
    # - runtime consumes this lane in src/items.c throw-side pulse reconstruction suppressor.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    throw_pulse_consumed, throw_pulse_crossed_prev = _derive_throw_pulse_seed_lanes(
        seed_action_id_u16=samples["seed_t"]["action_id"],
        seed_char_id_u8=samples["seed_t"]["char_id"],
        seed_anim_frame_f32=samples["seed_t"]["anim_frame_f32"],
        seed_frame_speed_mul_f32=samples["seed_t"]["frame_speed_mul_f32"],
        seed_hitstun_u16=samples["seed_t"]["hitstun"],
        seed_last_attack_landed_u8=samples["seed_t"]["last_attack_landed"],
        seed_last_hit_by_u8=samples["seed_t"]["last_hit_by"],
        seed_items=samples["seed_t"]["items"],
        num_players=num_players,
        pulse_frames_by_char_action=throw_pulse_frames_by_char_action,
        cmd1_start_by_char_action=throw_cmd1_start_by_char_action,
        shot_itkind_by_char=throw_shot_itkind_by_char,
        act_throw_b=int(act_throw_b),
        act_throw_hi=int(act_throw_hi),
        act_damage_fly_top=int(act_damage_fly_top),
        falco_char_id=int(char_falco),
    )
    samples["seed_t"]["throw_pulse_consumed"] = throw_pulse_consumed
    samples["seed_t"]["throw_pulse_crossed_prev_frame"] = throw_pulse_crossed_prev
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
    post_anim_frame = np.zeros((n_frames, 4), dtype=np.float32)
    post_animation_index = np.zeros((n_frames, 4), dtype=np.uint32)
    post_facing = np.zeros((n_frames, 4), dtype=np.uint8)
    post_on_ground = np.zeros((n_frames, 4), dtype=np.uint8)
    post_pos_x = np.zeros((n_frames, 4), dtype=np.float32)
    post_pos_y = np.zeros((n_frames, 4), dtype=np.float32)
    post_pos_z_2d = np.zeros((n_frames, 4), dtype=np.float32)
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
    pre_main_x_2d = np.zeros((n_frames, 4), dtype=np.int8)
    pre_main_y_2d = np.zeros((n_frames, 4), dtype=np.int8)
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
        pre_main_x_2d[:, slot] = _to_numpy(pre.field("raw_analog_x")).astype(np.int8)
        pre_main_y_2d[:, slot] = _to_numpy(pre.field("raw_analog_y")).astype(np.int8)
        pre_l[:, slot] = _u8_from_float01(_to_numpy(pre.field("triggers_physical").field("l")).astype(np.float32))
        pre_r[:, slot] = _u8_from_float01(_to_numpy(pre.field("triggers_physical").field("r")).astype(np.float32))

        post_team_id[:, slot] = samples["seed_t"]["team_id"][0, slot]
        post_char_id[:, slot] = _to_numpy(post.field("character")).astype(np.uint8)
        post_action_id[:, slot] = _to_numpy(post.field("state")).astype(np.uint16)
        post_state_age_f32 = _to_numpy(post.field("state_age")).astype(np.float32)
        post_action_frame[:, slot] = _i16_from_state_age(post_state_age_f32, n_frames)
        post_anim_frame[:, slot] = _f32_from_state_age(post_state_age_f32, n_frames)
        post_animation_index[:, slot] = _to_numpy(post.field("animation_index")).astype(np.uint32)
        post_facing[:, slot] = _dir_to_facing(_to_numpy(post.field("direction")).astype(np.float32))
        post_on_ground[:, slot] = _airborne_to_on_ground(_to_numpy(post.field("airborne")).astype(np.uint8), n_frames)
        post_pos_x[:, slot] = _to_numpy(post.field("position").field("x")).astype(np.float32)
        post_pos_y[:, slot] = _to_numpy(post.field("position").field("y")).astype(np.float32)
        post_pos_z_2d[:, slot] = _post_position_z(post, n_frames)
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

    hidden_pos_z = _derive_grounded_overlap_hidden_pos_z(
        num_players=num_players,
        char_id_u8=post_char_id,
        action_id_u16=post_action_id,
        on_ground_u8=post_on_ground,
        stocks_u8=post_stocks,
        pos_x_f32=post_pos_x,
        pos_z_f32=post_pos_z_2d,
        facing_u8=post_facing,
        common=common,
        data_dir="data",
    )
    samples["seed_t"]["pos_z"] = hidden_pos_z[:-1]

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

    # Same-frame fighter-proc order lane for plAttack_80037B08.
    #
    # The counter itself is causal above, but simultaneous fighter motion-state entries share one
    # global counter and Slippi exposes only post-frame ids, not HSD proc order. Seed the exact
    # per-entry id for the grounded locomotion/motion-entry owner only, and only when at least two
    # fighters both changed action and instance_id, or when a single fighter entry observes a ref id
    # beyond the seeded next counter (hidden same-frame item/fighter consumer before this fighter's
    # proc). Adjacent combat, damage, landing, special, grab, and item rows must stay outside this
    # lane; their counter-order work belongs to their own owner families.
    # refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState)
    # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::{ft_800895E0,ft_80089824}
    # refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
    entry_changed = post_action_id[1:, :] != post_action_id[:-1, :]
    instance_changed = post_instance_id[1:, :] != post_instance_id[:-1, :]
    has_ref_instance = post_instance_id[1:, :] != np.uint16(0)
    same_frame_fighter_entries_all = entry_changed & instance_changed & has_ref_instance
    grounded_motion_owner_actions = np.isin(
        post_action_id[1:, :],
        np.array([act_wait, act_walk_slow, act_walk_middle, act_walk_fast, act_turn, act_turn_run, act_dash, act_run, act_run_direct, act_kneebend], dtype=np.uint16),
    ) & np.isin(
        post_action_id[:-1, :],
        np.array([act_wait, act_walk_slow, act_walk_middle, act_walk_fast, act_turn, act_turn_run, act_dash, act_run, act_run_direct, act_kneebend], dtype=np.uint16),
    )
    same_frame_fighter_entries = same_frame_fighter_entries_all & grounded_motion_owner_actions
    multi_entry_frame = np.sum(same_frame_fighter_entries_all[:, :num_players], axis=1) >= 2
    hidden_prior_consumer = same_frame_fighter_entries & (
        post_instance_id[1:, :] != counter_post[:-1, None]
    )
    motion_entry_iid_override = np.zeros((n_frames - 1, 4), dtype=np.uint16)
    motion_entry_override_mask = same_frame_fighter_entries & (
        multi_entry_frame[:, None] | hidden_prior_consumer
    )
    motion_entry_iid_override[motion_entry_override_mask] = post_instance_id[1:, :][
        motion_entry_override_mask
    ]
    samples["seed_t"]["motion_entry_instance_id_override_u16"][:, :] = motion_entry_iid_override

    # Grab/throw victim attachment owner identity (slot indices; 2p-only for v1 suite).
    if int(num_players) == 2:
        grab_owner = derive_grab_owner_port_2p(action_id_u16_2p=post_action_id[:, :2])
        samples["seed_t"]["grab_owner_port"][:, :2] = grab_owner[:-1, :]

    pre_stick_x_unit_2d = np.zeros((n_frames, 4), dtype=np.float32)
    pre_stick_y_unit_2d = np.zeros((n_frames, 4), dtype=np.float32)
    grab_mash_x_sign_post = np.zeros((n_frames, 4), dtype=np.int8)
    grab_mash_y_sign_post = np.zeros((n_frames, 4), dtype=np.int8)
    for slot in range(num_players):
        main_x_proc, main_y_proc = ucf_process_stick_i8(
            pre_main_x_2d[:, slot],
            pre_main_y_2d[:, slot],
            ucf_enabled=ucf_enabled,
            ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
        )
        stick_x = apply_deadzone(stick_i8_to_unit(main_x_proc), lstick_deadzone_x)
        stick_y = apply_deadzone(stick_i8_to_unit(main_y_proc), lstick_deadzone_y)
        pre_stick_x_unit_2d[:, slot] = stick_x
        pre_stick_y_unit_2d[:, slot] = stick_y
        mash_x, mash_y = derive_grab_mash_stick_sign_post(
            stick_x_unit=stick_x,
            stick_y_unit=stick_y,
            grab_mash_stick_threshold=grab_mash_stick_threshold,
        )
        grab_mash_x_sign_post[:, slot] = mash_x
        grab_mash_y_sign_post[:, slot] = mash_y
        samples["seed_t"]["grab_mash_stick_x_sign"][:, slot] = mash_x[:-1]
        samples["seed_t"]["grab_mash_stick_y_sign"][:, slot] = mash_y[:-1]

    if int(num_players) == 2:
        for slot, port_1based in enumerate(src_ports):
            st = static_by_port.get(port_1based, PortStatic(team_id=0, char_id=0, handicap=9))
            (
                capture_grab_timer,
                capture_wait_counter,
                capture_wait_anim_timer,
                capture_wait_jump_latch,
                capture_breakout_pending,
            ) = derive_capture_grab_hidden_post(
                action_id_u16=post_action_id[:, slot],
                action_frame_i16=post_action_frame[:, slot],
                grab_owner_port_u8=grab_owner[:, slot],
                percent_f32=post_percent_all[:, slot],
                buttons_held_u16=pre_buttons[:, slot],
                stick_x_unit=pre_stick_x_unit_2d[:, slot],
                stick_y_unit=pre_stick_y_unit_2d[:, slot],
                frame_speed_mul_f32=frame_speed_mul_all[:, slot],
                grab_mash_stick_x_sign_post=grab_mash_x_sign_post[:, slot],
                grab_mash_stick_y_sign_post=grab_mash_y_sign_post[:, slot],
                slot_index=slot,
                handicap=st.handicap,
                capture_grab_timer_base=float(common["capture_grab_timer_base"]),
                capture_grab_timer_handicap_mul=float(common["capture_grab_timer_handicap_mul"]),
                capture_grab_timer_handicap_base=float(common["capture_grab_timer_handicap_base"]),
                capture_grab_timer_slot_mul=float(common["capture_grab_timer_slot_mul"]),
                capture_grab_timer_slot_base=float(common["capture_grab_timer_slot_base"]),
                capture_grab_timer_percent_mul=float(common["capture_grab_timer_percent_mul"]),
                capture_wait_grab_timer_decrement=float(common["capture_wait_grab_timer_decrement"]),
                capture_wait_grab_mash_damage=float(common["capture_wait_grab_mash_damage"]),
                capture_wait_anim_rate_hold_frames=float(common["capture_wait_anim_rate_hold_frames"]),
                capture_wait_jump_latch_window_frames=float(
                    common["capture_wait_jump_latch_window_frames"]
                ),
                grab_mash_stick_threshold=grab_mash_stick_threshold,
            )
            samples["seed_t"]["capture_grab_timer_f32"][:, slot] = capture_grab_timer[:-1]
            samples["seed_t"]["capture_wait_counter_f32"][:, slot] = capture_wait_counter[:-1]
            samples["seed_t"]["capture_wait_anim_rate_timer_f32"][:, slot] = (
                capture_wait_anim_timer[:-1]
            )
            samples["seed_t"]["capture_wait_jump_latch_u8"][:, slot] = (
                capture_wait_jump_latch[:-1]
            )
            samples["seed_t"]["capture_breakout_pending_u8"][:, slot] = (
                capture_breakout_pending[:-1]
            )

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

    hitlist_cd, hitlist_iid, hitlist_hb_valid, hitlist_hb_cd, hitlist_hb_iid = derive_combat_hitlist_seed_fields(
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
        percent=post_percent_all,
        shield_hp=post_shield_hp,
        hurtbox_state=post_hurtbox_state,
        hitlag=post_hitlag,
        last_hit_by=post_last_hit_by,
        instance_hit_by=post_instance_hit_by,
        instance_id=post_instance_id,
        input_buttons=pre_buttons,
        input_l=pre_l,
        input_r=pre_r,
        turn_has_turned=post_turn_has_turned_u8,
        anim_frame_f32=post_anim_frame,
        frame_speed_mul_f32=frame_speed_mul_all,
        include_per_hitbox=True,
        include_replay_only_shield_admission=True,
        include_replay_only_body_admission=True,
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
                # Attribution corroboration: only trim stale suppression for attacker/defender pairs
                # where replay-visible ownership points to this attacker port.
                #
                # Slippi post-frame ownership lanes:
                # - `last_hit_by` mirrors fighter->x2088 (port index):
                #   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
                # - `last_hit_by_instance` mirrors fighter->x18EC (instance id):
                #   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
                #
                # For the singles (2p) target domain, allow a narrow fallback when `last_hit_by`
                # is unmapped and `last_hit_by_instance` does not map to any live fighter iid this
                # frame (stale owner lane). In 2p, the non-defender slot is the only valid attacker.
                #
                # Keep this fallback out of early/common grounded action space (<= AttackLw4), where
                # seeded suppression frequently represents valid same-victim rehit blocking.
                # refs/melee/src/melee/ft/chara/ftCommon/forward.h (ftCommon_MotionState)
                # src/action_ids.h::MSL_ACT_ATTACK_LW4 (0x0040)
                defender_action = int(post_action_id[fi, defender])
                last_hit_by_owner = int(post_last_hit_by[fi, defender])
                owner_iid = int(post_instance_hit_by[fi, defender])
                if not _seed_bridge_owner_matches_attacker(
                    num_players=num_players,
                    attacker=attacker,
                    defender=defender,
                    defender_action=defender_action,
                    act_attack_lw4=int(act_attack_lw4),
                    last_hit_by_owner=last_hit_by_owner,
                    owner_iid=owner_iid,
                    live_instance_ids=post_instance_id[fi, :num_players],
                ):
                    continue
                hitlag_pre = int(post_hitlag[fi, defender])
                if hitlag_pre > 0:
                    hitlag_pre -= 1
                hitstun_pre = int(post_hitstun[fi, defender])
                if hitstun_pre > 0:
                    hitstun_pre -= 1
                # Neutral non-guard stale suppression (existing bridge policy).
                neutral_non_guard = (
                    hitlag_pre == 0
                    and hitstun_pre == 0
                    and defender_action not in guard_family
                    and defender_action != int(act_landing_fall_special)
                )

                # DamageFlyTop stale suppression (grounded-attack ownership lane):
                # - Decomp ownership for suppression gate is lbColl_8000ACFC victim presence, not
                #   defender hitstun state itself.
                # - When replay-visible attribution disagrees on instance identity for this attacker
                #   while defender remains in DamageFlyTop, stale dense-seeded suppression can block
                #   first valid grounded re-contacts in one-step reseed.
                # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
                # refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
                # Grounded attack action-id range (ftCo_MS_Attack11..ftCo_MS_AttackLw4) is
                # contiguous in GALE01:
                # refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCommon_MotionState
                attacker_action = int(post_action_id[fi, attacker])
                damage_state_bridge = (
                    hitlag_pre == 0
                    and defender_action == int(act_damage_fly_top)
                    and int(act_attack_11) <= attacker_action <= int(act_attack_lw4)
                )

                if not (neutral_non_guard or damage_state_bridge):
                    continue
                # Only trim stale indefinite suppression lanes (0xFFFF). Finite cooldown lanes are
                # replay-causal and should decay naturally.
                #
                # Hitlist cooldown ownership:
                # - 0xFFFF is the "indefinite" sentinel used by hitlist victim suppression.
                #   refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
                if not _seed_bridge_trim_indefinite_lanes(
                    hitlist_cd=hitlist_cd,
                    hitlist_iid=hitlist_iid,
                    hitlist_hb_valid=hitlist_hb_valid,
                    hitlist_hb_cd=hitlist_hb_cd,
                    hitlist_hb_iid=hitlist_hb_iid,
                    fi=fi,
                    attacker=attacker,
                    defender=defender,
                ):
                    continue

    samples["seed_t"]["combat_hitlist_cd"] = hitlist_cd[:-1]
    samples["seed_t"]["combat_hitlist_victim_iid"] = hitlist_iid[:-1]
    samples["seed_t"]["combat_hitlist_hb_valid"] = hitlist_hb_valid[:-1]
    samples["seed_t"]["combat_hitlist_hb_cd"] = hitlist_hb_cd[:-1]
    samples["seed_t"]["combat_hitlist_hb_victim_iid"] = hitlist_hb_iid[:-1]

    (
        hitbox_prev_valid,
        hitbox_prev_x,
        hitbox_prev_y,
        hitbox_prev_z,
    ) = derive_hitbox_prev_center_seed_fields(
        num_players=num_players,
        char_id=post_char_id,
        animation_index=post_animation_index,
        action_frame=post_action_frame,
        anim_frame_f32=post_anim_frame,
        pos_x=post_pos_x,
        pos_y=post_pos_y,
        pos_z=hidden_pos_z,
        facing=post_facing,
        fighter_scale_y=post_scale_y,
        data_root="data",
    )
    samples["seed_t"]["combat_hitbox_prev_valid"] = hitbox_prev_valid[:-1]
    samples["seed_t"]["combat_hitbox_prev_x"] = hitbox_prev_x[:-1]
    samples["seed_t"]["combat_hitbox_prev_y"] = hitbox_prev_y[:-1]
    samples["seed_t"]["combat_hitbox_prev_z"] = hitbox_prev_z[:-1]

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
