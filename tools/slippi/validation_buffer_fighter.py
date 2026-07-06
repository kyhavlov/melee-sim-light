from __future__ import annotations

import numpy as np

from tools.slippi.validation_buffer_common import _ascontiguousarray, _to_numpy


def _derive_landing_fallspecial_allow_interrupt_seed_lane(*, action_id_u16: np.ndarray, char_id_u8: np.ndarray, origin_allow_by_char: dict[int, dict[int, tuple[int, int]]]) -> np.ndarray:
    """
    Derive LandingFallSpecial `mv.co.landing.allow_interrupt` from replay-prefix action history.

    Decomp ownership:
    - EscapeAir_Coll enters LandingFallSpecial with allow_interrupt=false.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099D70
    - FallSpecial_Coll forwards `mv.co.fallspecial.allow_interrupt`.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096D28
    - The bool is CALLSITE-owned per freefall origin: fox/falco SpecialS/Hi enters pass true,
      marth-style ftMs_SpecialHi passes false. `origin_allow_by_char` carries that per-char
      origin-action identity (owners fx_special_kind lane / special msids - no raw FX ids)
      as (entry_allow, direct_lfs_allow) per origin.
      refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Anim
      refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c
      refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c

    Seed policy:
    - Strictly prefix-causal over visible action ids.
    - Carry the hidden FallSpecial allow bit across a FallSpecial run, and copy it onto the
      subsequent LandingFallSpecial run.
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_landing_fallspecial_allow_interrupt is required; run `make build`') from exc
    return msl_binding.derive_landing_fallspecial_allow_interrupt(_ascontiguousarray(action_id_u16, dtype=np.uint16), _ascontiguousarray(char_id_u8, dtype=np.uint8), origin_allow_by_char)


def _derive_jab_rapid_count_seed_lane(*, action_id_u16: np.ndarray, buttons_released_u16: np.ndarray, buttons_pressed_u16: np.ndarray, button_mask_a: int) -> np.ndarray:
    """Reconstruct fp+0x1A54 for teacher-forced mid-jab seeds.

    The counter is runtime-causal: checkAttack11 resets it on Attack11 entry, then
    ftCo_Attack_800D6A50 increments once per Attack11/12/13 IASA frame when A is pressed or
    released.
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::checkAttack11
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack_800D6A50
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_jab_rapid_count is required; run `make build`') from exc
    return msl_binding.derive_jab_rapid_count(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1), np.asarray(buttons_released_u16, dtype=np.uint16).reshape(-1), np.asarray(buttons_pressed_u16, dtype=np.uint16).reshape(-1), int(button_mask_a))


def _derive_walk_anim_source_vel_seed_lane(*, action_id_u16: np.ndarray, char_id_u8: np.ndarray, facing_dir1_i8: np.ndarray, frame_speed_mul_f32: np.ndarray, walk_divisors_by_char: dict[int, tuple[float, float, float]]) -> np.ndarray:
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
    slow = np.zeros(256, dtype=np.float32)
    middle = np.zeros(256, dtype=np.float32)
    fast = np.zeros(256, dtype=np.float32)
    for cid, divs in walk_divisors_by_char.items():
        slow[int(cid) & 255] = np.float32(float(divs[0]))
        middle[int(cid) & 255] = np.float32(float(divs[1]))
        fast[int(cid) & 255] = np.float32(float(divs[2]))
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_walk_anim_source_vel is required; run `make build`') from exc
    return msl_binding.derive_walk_anim_source_vel(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1), np.asarray(char_id_u8, dtype=np.uint8).reshape(-1), np.asarray(facing_dir1_i8, dtype=np.int8).reshape(-1), np.asarray(frame_speed_mul_f32, dtype=np.float32).reshape(-1), slow, middle, fast)


def _derive_walk_retarget_tick_source_vel_seed_lane(*, action_id_u16: np.ndarray, char_id_u8: np.ndarray, facing_dir1_i8: np.ndarray, anim_frame_f32: np.ndarray, ref_action_frame_i16: np.ndarray, speed_ground_x_self_f32: np.ndarray, walk_anim_source_vel_f32: np.ndarray, walk_divisors_by_char: dict[int, tuple[float, float, float]], walk_max_by_char: dict[int, float], walk_mid_vel_mul: float, walk_fast_vel_mul: float, end_frames: object) -> np.ndarray:
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
    slow = np.zeros(256, dtype=np.float32)
    middle = np.zeros(256, dtype=np.float32)
    fast = np.zeros(256, dtype=np.float32)
    for cid, divs in walk_divisors_by_char.items():
        slow[int(cid) & 255] = np.float32(float(divs[0]))
        middle[int(cid) & 255] = np.float32(float(divs[1]))
        fast[int(cid) & 255] = np.float32(float(divs[2]))
    walk_max = np.zeros(256, dtype=np.float32)
    for cid, value in walk_max_by_char.items():
        walk_max[int(cid) & 255] = np.float32(float(value))
    cycle_width = 16
    end_lut = np.zeros((256, cycle_width), dtype=np.float32)
    for cid, by_msid in end_frames.by_char_id.items():
        for msid, end_frame in by_msid.items():
            if 0 <= int(msid) < cycle_width:
                end_lut[int(cid) & 255, int(msid)] = np.float32(float(end_frame))
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_walk_retarget_tick_source_vel is required; run `make build`') from exc
    return msl_binding.derive_walk_retarget_tick_source_vel(_ascontiguousarray(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)), _ascontiguousarray(np.asarray(char_id_u8, dtype=np.uint8).reshape(-1)), _ascontiguousarray(np.asarray(facing_dir1_i8, dtype=np.int8).reshape(-1)), _ascontiguousarray(np.asarray(anim_frame_f32, dtype=np.float32).reshape(-1)), _ascontiguousarray(np.asarray(ref_action_frame_i16, dtype=np.int16).reshape(-1)), _ascontiguousarray(np.asarray(speed_ground_x_self_f32, dtype=np.float32).reshape(-1)), _ascontiguousarray(np.asarray(walk_anim_source_vel_f32, dtype=np.float32).reshape(-1)), slow, middle, fast, walk_max, end_lut, float(walk_mid_vel_mul), float(walk_fast_vel_mul))


def _derive_run_anim_source_vel_seed_lane(*, action_id_u16: np.ndarray, char_id_u8: np.ndarray, facing_dir1_i8: np.ndarray, frame_speed_mul_f32: np.ndarray, run_scaling_by_char: dict[int, float]) -> np.ndarray:
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
    scaling = np.zeros(256, dtype=np.float32)
    for cid, value in run_scaling_by_char.items():
        scaling[int(cid) & 255] = np.float32(float(value))
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_run_anim_source_vel is required; run `make build`') from exc
    return msl_binding.derive_run_anim_source_vel(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1), np.asarray(char_id_u8, dtype=np.uint8).reshape(-1), np.asarray(facing_dir1_i8, dtype=np.int8).reshape(-1), np.asarray(frame_speed_mul_f32, dtype=np.float32).reshape(-1), scaling)


def _team_id_from_start_player(p: dict) -> int:
    t = p.get('team')
    if t is None:
        return 0
    c = t.get('color')
    return int(c) if c is not None else 0


def _derive_match_flow_pending_rebirth_char_id(*, post_action_id_u16: np.ndarray, post_char_id_u8: np.ndarray, post_stocks_u8: np.ndarray, match_flow_timer_u8: np.ndarray, static_char_id_u8: np.ndarray, team_id_u8: np.ndarray, is_teams: bool, num_players: int) -> np.ndarray:
    """Derive zeroed DeadDown -> Rebirth fighter kind for team-stock respawn rows.

    Source owner:
    - Teams stock-share pending Rebirth runs through gm_16AE.c::fn_8016B918_inline and then
      ftCo_Rebirth entry. Slippi can publish the pending slot as DeadDown/char_id=0/stocks=0
      before Rebirth restores the fighter kind.

    Prefix-causal boundary:
    - A zeroed DeadDown slot is considered pending only after a replay-visible transition into that
      zeroed state while a same-team teammate has more than one stock to share. Terminal eliminated
      slots with no stock-share source, or long-standing zeroed slots without a pending transition,
      remain unseeded.
    """
    n_frames = int(post_action_id_u16.shape[0])
    n_players = int(num_players)
    out = np.zeros((n_frames, 4), dtype=np.uint8)
    if not bool(is_teams) or n_players <= 2:
        return out
    active_char = np.zeros(4, dtype=np.uint8)
    last_visible_char = np.zeros(4, dtype=np.uint8)
    for frame in range(n_frames):
        for slot in range(n_players):
            visible_char = int(post_char_id_u8[frame, slot])
            if visible_char != 0:
                last_visible_char[slot] = np.uint8(visible_char)
            static_char = int(static_char_id_u8[slot])
            is_zeroed_dead = int(post_action_id_u16[frame, slot]) == 0 and int(post_char_id_u8[frame, slot]) == 0 and (int(post_stocks_u8[frame, slot]) == 0) and (int(match_flow_timer_u8[frame, slot]) > 0) and (static_char != 0)
            if not is_zeroed_dead:
                active_char[slot] = 0
                continue
            teammate_has_stock_share = False
            for other in range(n_players):
                if other == slot:
                    continue
                if int(team_id_u8[other]) != int(team_id_u8[slot]):
                    continue
                if int(post_stocks_u8[frame, other]) > 1:
                    teammate_has_stock_share = True
                    break
            if not teammate_has_stock_share:
                active_char[slot] = 0
                continue
            fresh_zero_transition = frame == 0 or int(post_action_id_u16[frame - 1, slot]) != 0 or int(post_char_id_u8[frame - 1, slot]) != 0 or (int(post_stocks_u8[frame - 1, slot]) != 0)
            if fresh_zero_transition:
                active_char[slot] = last_visible_char[slot] if int(last_visible_char[slot]) != 0 else np.uint8(static_char)
            if active_char[slot] != 0:
                out[frame, slot] = active_char[slot]
    return out


def _derive_match_flow_pending_rebirth_state_flags_2218(*, post_action_id_u16: np.ndarray, post_char_id_u8: np.ndarray, post_stocks_u8: np.ndarray, post_state_flags_u8: np.ndarray, match_flow_timer_u8: np.ndarray, static_char_id_u8: np.ndarray, team_id_u8: np.ndarray, is_teams: bool, num_players: int) -> np.ndarray:
    """Derive hidden fp+0x2218 for team-stock pending Rebirth rows.

    Slippi publishes the waiting stock-share slot as zeroed DeadDown, but gm_16AE still polls a
    live player entity. Fighter_UnkInitReset_80067C98 and Fighter_ChangeMotionState clear
    reflecting/x2218_b6/b7 but do not clear x2218_b1 or x2218_b5, so preserve only those two bits
    from the last visible fighter row through the zeroed wait slot.
    """
    n_frames = int(post_action_id_u16.shape[0])
    n_players = int(num_players)
    out = np.zeros((n_frames, 4), dtype=np.uint8)
    if not bool(is_teams) or n_players <= 2:
        return out
    active_flags = np.zeros(4, dtype=np.uint8)
    last_visible_flags = np.zeros(4, dtype=np.uint8)
    preserve_mask = np.uint8(0x40 | 0x04)
    for frame in range(n_frames):
        for slot in range(n_players):
            if int(post_char_id_u8[frame, slot]) != 0:
                last_visible_flags[slot] = np.uint8(int(post_state_flags_u8[frame, slot, 0]) & int(preserve_mask))
            static_char = int(static_char_id_u8[slot])
            is_zeroed_dead = int(post_action_id_u16[frame, slot]) == 0 and int(post_char_id_u8[frame, slot]) == 0 and (int(post_stocks_u8[frame, slot]) == 0) and (int(match_flow_timer_u8[frame, slot]) > 0) and (static_char != 0)
            if not is_zeroed_dead:
                active_flags[slot] = 0
                continue
            teammate_has_stock_share = False
            for other in range(n_players):
                if other == slot:
                    continue
                if int(team_id_u8[other]) != int(team_id_u8[slot]):
                    continue
                if int(post_stocks_u8[frame, other]) > 1:
                    teammate_has_stock_share = True
                    break
            if not teammate_has_stock_share:
                active_flags[slot] = 0
                continue
            fresh_zero_transition = frame == 0 or int(post_action_id_u16[frame - 1, slot]) != 0 or int(post_char_id_u8[frame - 1, slot]) != 0 or (int(post_stocks_u8[frame - 1, slot]) != 0)
            if fresh_zero_transition:
                active_flags[slot] = last_visible_flags[slot]
            out[frame, slot] = active_flags[slot]
    return out


def _derive_facing_dir1_sign(*, facing_u8: np.ndarray, action_id_u16: np.ndarray) -> np.ndarray:
    """Derive fp->facing_dir1 as a strictly-causal signed lane.

    Decomp:
    - fp->facing_dir1 is copied from fp->facing_dir on Fighter_ChangeMotionState.
      refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    - Escape/root-motion helpers consume fp->facing_dir1.
      refs/melee/src/melee/ft/ft_081B.c::ft_80085030
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_facing_dir1_sign is required; run `make build`') from exc
    return msl_binding.derive_facing_dir1_sign(np.asarray(facing_u8, dtype=np.uint8).reshape(-1), np.asarray(action_id_u16, dtype=np.uint16).reshape(-1))


def _derive_specialhi_rotate_model_seed_lane(*, action_id_u16: np.ndarray, facing_u8: np.ndarray, pos_x_f32: np.ndarray, pos_y_f32: np.ndarray, speed_air_x_self_f32: np.ndarray, speed_y_self_f32: np.ndarray, stage_id_u32: int, stage_segments: list[dict], act_fx_special_hi: int, act_fx_special_air_hi: int, act_fx_special_hi_landing: int, act_fx_special_hi_fall: int, act_fx_special_hi_bound: int) -> tuple[np.ndarray, np.ndarray]:
    """Reconstruct the hidden Firefox/Firebird rotateModel over continuous launch episodes.

    Decomp:
    - ftFx_SpecialAirHi_Enter writes `mv.fx.SpecialHi.rotateModel` from launch self_vel/facing.
    - ftFx_SpecialAirHi_Phys consumes that stored angle for reverse acceleration.
    - ftFx_SpecialAirHi_Coll can rewrite facing and recompute rotateModel from current self_vel.
    - SpecialHiLanding/Fall/Bound callbacks do not rewrite FtPart_XRotN, so the live JObj pose can
      persist into those followups until another motion state owns the model.
    refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
      ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Phys,
      ftFx_SpecialAirHi_Coll,ftFx_SpecialHiLanding_Anim,ftFx_SpecialHiFall_Anim,
      ftFx_SpecialHiBound_Enter}
    """
    kind_id = np.array([3 if seg.get('kind') == 'left_wall' else 2 if seg.get('kind') == 'right_wall' else 0 for seg in stage_segments], dtype=np.uint8)
    x0 = np.array([float(seg['x0']) for seg in stage_segments], dtype=np.float32)
    y0 = np.array([float(seg['y0']) for seg in stage_segments], dtype=np.float32)
    x1 = np.array([float(seg['x1']) for seg in stage_segments], dtype=np.float32)
    y1 = np.array([float(seg['y1']) for seg in stage_segments], dtype=np.float32)
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_specialhi_rotate_model_seed_lane is required; run `make build`') from exc
    return msl_binding.derive_specialhi_rotate_model_seed_lane(_ascontiguousarray(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)), _ascontiguousarray(np.asarray(facing_u8, dtype=np.uint8).reshape(-1)), _ascontiguousarray(np.asarray(pos_x_f32, dtype=np.float32).reshape(-1)), _ascontiguousarray(np.asarray(pos_y_f32, dtype=np.float32).reshape(-1)), _ascontiguousarray(np.asarray(speed_air_x_self_f32, dtype=np.float32).reshape(-1)), _ascontiguousarray(np.asarray(speed_y_self_f32, dtype=np.float32).reshape(-1)), int(stage_id_u32), kind_id, x0, y0, x1, y1, int(act_fx_special_hi), int(act_fx_special_air_hi), int(act_fx_special_hi_landing), int(act_fx_special_hi_fall), int(act_fx_special_hi_bound))


def _derive_kb_smashcharge_active_from_post(*, post) -> np.ndarray:
    """Extract smash-charge active signal from replay post-frame when available.

    Decomp consumer:
    - ftCo_Damage_CalcKnockback applies kb_smashcharge_mul when
      fp->smash_attrs.state == SmashState_Charging.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
    """
    candidates = ('smash_charge', 'smash_charge_active', 'smash_charging')
    for name in candidates:
        if post.type.get_field_index(name) != -1:
            lane = _to_numpy(post.field(name))
            return (np.asarray(lane) != 0).astype(np.uint8)
    return np.zeros(len(post), dtype=np.uint8)


def _derive_smash_charge_seed_lanes(*, char_id_u8: np.ndarray, action_id_u16: np.ndarray, anim_frame_f32: np.ndarray, frame_speed_mul_f32: np.ndarray, on_ground_u8: np.ndarray, hitlag_u16: np.ndarray, hitstun_u16: np.ndarray, buttons_held_u16: np.ndarray, button_mask_a: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Derive hidden fp->smash_attrs lanes from prefix-visible grounded-smash charge history.

    The source owner is the start_smash_charge script event plus ftCo_800DF0D0/ftCo_800DEEB8.
    Keep the sequential state machine in native preprocessing: this runs over every replay frame
    and feeds one-step reseeds, so Python should not own the per-frame loop.
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_smash_charge_seed_lanes is required; run `make build`') from exc
    return msl_binding.derive_smash_charge_seed_lanes(np.asarray(char_id_u8, dtype=np.uint8), np.asarray(action_id_u16, dtype=np.uint16), np.asarray(anim_frame_f32, dtype=np.float32), np.asarray(frame_speed_mul_f32, dtype=np.float32), np.asarray(on_ground_u8, dtype=np.uint8), np.asarray(hitlag_u16, dtype=np.uint16), np.asarray(hitstun_u16, dtype=np.uint16), np.asarray(buttons_held_u16, dtype=np.uint16), int(button_mask_a))
