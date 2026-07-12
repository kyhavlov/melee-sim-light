from __future__ import annotations

from tools.slippi.validation_buffer_common import (  # noqa: F401
    functools,
    json,
    struct,
    dataclass,
    Path,
    SimpleNamespace,
    Any,
    np,
    pa,
    _read_slippi,
    COMPARE_DTYPE,
    INPUT_DTYPE,
    SEED_DTYPE,
    load_action_state_tables,
    hitstun_u16_from_misc_as_and_state_flags3,
    item_article_kind_set,
    item_article_values_by_sim_char,
    STAGE_PLATFORM_TRANSFORM_KIND_HEIGHT,
    dream_whispy_metadata,
    fountain_of_dreams_default_platform_heights,
    fountain_of_dreams_platform_motion_params,
    read_mslstg01_v7,
    stage_metadata_path_for_stage_id,
    yoshi_shyguy_metadata,
    read_mslmso01_v1,
    finalized_frame_indices,
    replay_path_for_peppi,
    read_mslftsc1_v1,
    team_attack_on_from_start,
    MSL_MS_CLASS_ATTACK_AIR,
    FOD_SKIP_ECB_VERTICAL_UNIT,
    FOD_TRANSFORMED_PLATFORM_SKIP_LOOKUP_SLOP,
    FOD_FLOOR_X_END_CLAMP,
    FOD_FLOOR_Y_BIAS,
    FOD_STAGE_LINE_DX_EPSILON,
    _STAGE_KIND_BY_ID,
    _MATCH_FLOW_ACTION_IDS,
    _ascontiguousarray,
    _u8_lut_from_items,
    _u8_lut_from_pairs,
    _path_cache_key,
    _load_json_file_cached,
    _load_bytes_file_cached,
    _read_mslstg01_v7_cached,
    _load_json_file,
    _load_bytes_file,
    _read_mslstg01,
    ValidationReplayBuffers,
    _SampleParts,
    _ManifestPreprocessTables,
    _env_damage_int,
    _load_character_attrs,
    _load_moves_file,
    _load_common_data,
    manifest_registry_chars,
    require_replay_chars_in_manifest,
    _manifest_preprocess_tables,
    _load_u8_character_attr_lut_cached,
    _load_u8_character_attr_lut,
    _load_f32_character_attr_lut_cached,
    _load_f32_character_attr_lut,
    _derive_common_fall_blend_seed,
    _derive_sheik_needle_seed_lanes,
    _derive_sheik_chain_seed_lanes,
    _derive_zelda_twin_state_flags_2218,
    PortStatic,
    _to_numpy,
)
from tools.slippi.validation_buffer_stage import (  # noqa: F401
    _load_stage_segments_for_seed_cached,
    _load_stage_segments_for_seed,
    _motion_state_owner_actions_by_char,
    _move_submotion_ids_for_char,
    _attackair_first_hitbox_phase_by_char_action,
    _stage_ledge_floor_ids,
    _fod_platform_heights_from_frames,
    _fod_platform_height_transform_records,
    _derive_fod_floor_skip_segments,
    _derive_sheik_vanish_floor_skip_segments,
    _fod_platform_heights_with_ground_contact,
    _fod_platform_motion_with_ground_contact,
    _fod_hidden_return_timers,
    _fod_visible_choice_lanes,
    _dir_to_facing,
    _airborne_to_on_ground,
    _post_position_z,
    _derive_grounded_overlap_hidden_pos_z,
    _u8_from_float01,
    _int8_from_float_axis,
    _stick_i8_from_unit_stick,
    _u16_from_float_frames,
    _i16_from_state_age,
    _f32_from_state_age,
)

def _port_name(port_1based: int) -> str:
    if port_1based < 1 or port_1based > 4:
        raise ValueError(f'port must be in 1..4, got {port_1based}')
    return f'P{port_1based}'

def _seed_bridge_owner_matches_attacker(*, num_players: int, attacker: int, defender: int, defender_action: int, act_attack_lw4: int, last_hit_by_owner: int, owner_iid: int, live_instance_ids: np.ndarray) -> bool:
    if int(last_hit_by_owner) == int(attacker):
        return True
    owner_iid_matches_live = bool(np.any(np.asarray(live_instance_ids, dtype=np.uint16) == np.uint16(owner_iid)))
    fallback_singles_unmapped_owner = int(num_players) == 2 and int(attacker) != int(defender) and (int(defender_action) > int(act_attack_lw4)) and (int(last_hit_by_owner) >= int(num_players)) and (not owner_iid_matches_live)
    return bool(fallback_singles_unmapped_owner)

def _seed_bridge_trim_indefinite_lanes(*, hitlist_cd: np.ndarray, hitlist_iid: np.ndarray, hitlist_hb_valid: np.ndarray | None=None, hitlist_hb_cd: np.ndarray | None=None, hitlist_hb_iid: np.ndarray | None=None, fi: int, attacker: int, defender: int) -> bool:
    stale_indef_mask = hitlist_cd[fi, attacker, :, defender] == np.uint16(65535)
    trimmed = bool(np.any(stale_indef_mask))
    if trimmed:
        hitlist_cd[fi, attacker, stale_indef_mask, defender] = np.uint16(0)
        hitlist_iid[fi, attacker, stale_indef_mask, defender] = np.uint16(0)
    if hitlist_hb_cd is not None and hitlist_hb_iid is not None:
        stale_hb_mask = hitlist_hb_cd[fi, attacker, :, defender] == np.uint16(65535)
        if hitlist_hb_valid is not None:
            stale_hb_mask &= hitlist_hb_valid[fi, attacker, :] == np.uint8(0)
        if bool(np.any(stale_hb_mask)):
            hitlist_hb_cd[fi, attacker, stale_hb_mask, defender] = np.uint16(0)
            hitlist_hb_iid[fi, attacker, stale_hb_mask, defender] = np.uint16(0)
            trimmed = True
    return trimmed

def _derive_ledge_cooldown(*, action_id_u16: np.ndarray, hitlag_u16: np.ndarray, common: dict) -> np.ndarray:
    """
    Derive fp->x2064_ledgeCooldown (ledge grab cooldown) from replay action history.

    Decomp shape:
    - Decremented each frame under !hitlag.
      refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    - Set to p_ftCommonData->ledge_cooldown on certain cliff releases (notably CliffWait -> Fall)
      and on Damage* entry while the previous cliff-owned x221D_b7 flag is still live.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A9AC
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008E908
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_ledge_cooldown is required; run `make build`') from exc
    return msl_binding.derive_ledge_cooldown(_ascontiguousarray(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)), np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1), int(common.get('ledge_cooldown_frames', 0)))

def _derive_cliff_ledge_floor_segment_id(*, action_id_u16: np.ndarray, facing_u8: np.ndarray, on_ground_u8: np.ndarray, ledge_cooldown_u8: np.ndarray, stage_id: int, data_root: Path) -> np.ndarray:
    """
    Derive the teacher-forced hidden Cliff/CollData floor owner for direct cliff-exit reseeds.

    Decomp shape:
    - Cliff actions own ledge side through `mv.co.cliff.ledge_id`.
    - Immediate cliff exits carry the generated ledge floor owner while `fp->x2064_ledgeCooldown`
      remains live and before a grounded transfer clears the CollData owner.
      refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A9AC
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_cliff_ledge_floor_segment_id is required; run `make build`') from exc
    left_floor, right_floor = _stage_ledge_floor_ids(stage_id=stage_id, data_root=data_root)
    return msl_binding.derive_cliff_ledge_floor_segment_id(_ascontiguousarray(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)), _ascontiguousarray(np.asarray(facing_u8, dtype=np.uint8).reshape(-1)), _ascontiguousarray(np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1)), _ascontiguousarray(np.asarray(ledge_cooldown_u8, dtype=np.uint8).reshape(-1)), int(left_floor), int(right_floor))

def _derive_cliff_option_stick_latch_x8(*, action_id_u16: np.ndarray, main_x_i8: np.ndarray, main_y_i8: np.ndarray, c_x_i8: np.ndarray, c_y_i8: np.ndarray, common: dict) -> np.ndarray:
    """
    Derive the CliffWait climb/drop latch `mv.co.cliff.x8` for teacher-forced reseeds.

    Decomp shape:
    - ftCo_8009A804 initializes x8=0 on CliffWait entry.
    - ftCo_8009AA0C sets x8 when neither main stick nor c-stick is in the cliff option range.
    - ftCo_8009AAFC admits CliffClimb/drop only after x8 is set.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A804
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::{ftCo_8009AA0C,ftCo_8009AAFC}
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_cliff_option_stick_latch_x8 is required; run `make build`') from exc
    return msl_binding.derive_cliff_option_stick_latch_x8(_ascontiguousarray(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)), _ascontiguousarray(np.asarray(main_x_i8, dtype=np.int8).reshape(-1)), _ascontiguousarray(np.asarray(main_y_i8, dtype=np.int8).reshape(-1)), _ascontiguousarray(np.asarray(c_x_i8, dtype=np.int8).reshape(-1)), _ascontiguousarray(np.asarray(c_y_i8, dtype=np.int8).reshape(-1)), float(common['lstick_deadzone_x']), float(common['lstick_deadzone_y']), float(common['cliff_option_stick_threshold']))

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
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_match_flow_timer is required; run `make build`') from exc
    return msl_binding.derive_match_flow_timer(_ascontiguousarray(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)), int(port0), int(common['dead_timer_frames']), int(common['dead_up_star_initial_frames']), int(common['dead_up_star_phase1_frames']), int(common['dead_up_star_phase2_frames']), int(common['dead_up_fall_entry_hold_frames']), int(common['dead_up_fall_lerp_frames']), int(common['dead_up_fall_hitcamera_hold_frames']), int(common['dead_up_fall_phase3_frames']), int(common['dead_up_fall_phase4_frames']), int(common['rebirth_timer_frames']), int(common['rebirth_wait_timer_frames']), int(common['entry_start_frames']), int(common['entry_end_frames']))

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
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_passivewall_timer is required; run `make build`') from exc
    return msl_binding.derive_passivewall_timer(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1), np.asarray(action_frame_i16, dtype=np.int16).reshape(-1), int(common['passivewall_timer_frames']))

def _derive_walljump_used_seed_lanes(*, char_id_u8: np.ndarray, action_id_u16: np.ndarray, action_frame_i16: np.ndarray, on_ground_u8: np.ndarray, jumps_left_u8: np.ndarray, max_jumps_lut_u8: np.ndarray, buttons_pressed_u16: np.ndarray, stick_y_f32: np.ndarray, button_mask_xy: int, tap_jump_threshold: float) -> tuple[np.ndarray, np.ndarray]:
    """Derive `x1969_walljumpUsed` and the active PassiveWall entry exponent.

    Ordinary walljumps copy the current count into `mv.co.passivewall.vel_y_exponent`, then
    saturating-increment the count. Wall-tech entry uses exponent zero without incrementing, and
    `ftCommon_8007D6A4` resets the count on grounding, while Fighter_UnkInitReset_80067C98 resets it
    before Rebirth. The native scan is prefix-causal and uses generated collision-callback owners
    to distinguish every ordinary walljump producer from the four wall-tech producers. A grounded
    ProcessHit that returns airborne in the same replay row is recovered from the paired source
    x1968 write exposed by Slippi's jumps-left lane after excluding both aerial-jump input gates.

    refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4
    refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitReset_80067C98
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1D38,ftCo_800C1E64}
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_walljump_used_seed_lanes is required; run `make build`') from exc
    return msl_binding.derive_walljump_used_seed_lanes(np.asarray(char_id_u8, dtype=np.uint8).reshape(-1), np.asarray(action_id_u16, dtype=np.uint16).reshape(-1), np.asarray(action_frame_i16, dtype=np.int16).reshape(-1), np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1), np.asarray(jumps_left_u8, dtype=np.uint8).reshape(-1), np.asarray(max_jumps_lut_u8, dtype=np.uint8).reshape(-1), np.asarray(buttons_pressed_u16, dtype=np.uint16).reshape(-1), np.asarray(stick_y_f32, dtype=np.float32).reshape(-1), int(button_mask_xy), float(tap_jump_threshold))

def _derive_attackdash_x0_seed_lane(*, action_id_u16: np.ndarray, action_frame_i16: np.ndarray, misc_as_f32: np.ndarray, act_attack_dash: int, attackdash_x0_init_frames: int) -> np.ndarray:
    """
    Derive `fp->mv.co.attackdash.x0` for teacher-forced AttackDash reseeds.

    Source owner:
    - `ftCo_AttackDash.c::doEnter` clears `mv.co.attackdash.x0`.
    - `ftCo_AttackDash_SetMv0` seeds `mv.co.attackdash.x0` from `p_ftCommonData->x68`.
    - `ftCo_800D8AE0` consumes/decrements that countdown at the start of AttackDash IASA and
      enters CatchDash while L/R is held and x0 is nonzero.

    Slippi exposes fp+0x2340 as `misc_as`, but current public rows often serialize zero for this
    short AttackDash motion-var lane. Reconstruct the entry countdown from extracted common data
    and replay-visible AttackDash age, while preserving a nonzero exposed misc_as value if present.
    This initializes real hidden source state for one-step reseed only. Free-running runtime still
    needs the live `ftCo_AttackDash_SetMv0` callback timing before analog-only boost-grab can be
    admitted safely.

    refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::doEnter
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_SetMv0
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8AE0
    refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    data/common/ft_common_data.json::attackdash_x0_init_frames
    """
    action_id = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    action_frame = np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)
    misc_as = np.asarray(misc_as_f32, dtype=np.float32).reshape(-1)
    out = np.zeros(action_id.shape[0], dtype=np.int16)
    attackdash_mask = action_id == np.uint16(act_attack_dash)
    if not np.any(attackdash_mask):
        return out
    out[attackdash_mask] = np.clip(misc_as[attackdash_mask].astype(np.int32), np.iinfo(np.int16).min, np.iinfo(np.int16).max).astype(np.int16)
    init = int(attackdash_x0_init_frames)
    if init <= 0:
        return out
    ages = action_frame.astype(np.int32)
    reconstructed = init - np.maximum(ages - 1, 0)
    reconstructed = np.clip(reconstructed, 0, init).astype(np.int16)
    reconstruct_mask = attackdash_mask & (out == 0) & (reconstructed > 0)
    out[reconstruct_mask] = reconstructed[reconstruct_mask]
    return out

def _derive_walljump_phase_seed_lanes(*, action_id_u16: np.ndarray, action_frame_i16: np.ndarray, walljump_setup_x_delta_threshold_f32: np.ndarray, pos_x_f32: np.ndarray, pos_y_f32: np.ndarray, raw_main_x_i8: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Derive the hidden `ftWallJump_8008169C` input phase for one-step reseeds.

    Slippi does not expose `fp->wall_jump_input_timer`, `fp->x2110_walljumpWallSide`, or CollData's
    persisted wall-hug side. Keep this seed lane restricted to common airborne walljump callbacks
    in supported side-wall/underside neighborhoods. This is a teacher-forced one-step seed for the
    hidden timer/side only: runtime still requires the current stick-away input and x670 freshness
    before entering PassiveWallJump.

    The reconstruction is prefix-causal in sample space: for seed row `i`, it uses only root
    movement, action state, and raw input visible at or before that row's one-step input. It does
    not read the next post-frame reference state. The terminal output row is unused because
    Validation buffers store `walljump_*[:-1]`.
    - setup is reconstructed from replay-prefix root movement into the side-wall neighborhood when
      `data/characters/*.json::can_walljump` is true, using the per-character
      `walljump_setup_x_delta_threshold`. This mirrors `ftWallJump_8008169C`'s `fp->can_walljump`
      guard and `ABS(fp->pos_delta.x - wall_speed.x) > fp->co_attrs.x148` setup branch. Supported
      legal-stage side walls in the current suite are static for this owner, so wall speed is zero.
    - once setup starts, the hidden timer carries causally across same-side common-air wall rows,
      but preprocessing serializes it only for rows where the ftWallJump stick-away admission branch
      can consume the timer. Runtime still requires current WallHug/seeded-Hug and x670 freshness.

    refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_800835B0}
    data/characters/*.json::{can_walljump,walljump_setup_x_delta_threshold}
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_walljump_phase_seed_lanes is required; run `make build`') from exc
    return msl_binding.derive_walljump_phase_seed_lanes(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1), np.asarray(action_frame_i16, dtype=np.int16).reshape(-1), np.asarray(walljump_setup_x_delta_threshold_f32, dtype=np.float32).reshape(-1), np.asarray(pos_x_f32, dtype=np.float32).reshape(-1), np.asarray(pos_y_f32, dtype=np.float32).reshape(-1), np.asarray(raw_main_x_i8, dtype=np.int8).reshape(-1))

def _derive_mpcoll_wall_seed_lanes(*, action_id_u16: np.ndarray, action_frame_i16: np.ndarray, hitlag_u16: np.ndarray, hitstun_u16: np.ndarray, pos_x_f32: np.ndarray, pos_y_f32: np.ndarray, stage_id_u32: int, stage_segments: list[dict]) -> tuple[np.ndarray, np.ndarray]:
    """Derive one-step CollData wall side/index seed lanes from replay-prefix position.

    Decomp owner:
    - mpColl owns persisted `CollData.{left,right}_facing_wall.index` and writes
      `Collide_*WallHug`.
    - DamageFly_Coll and DownDamage_Coll then consume those env flags to enter PassiveWall /
      PassiveWallJump before other damage-collision followups.

    Public Slippi post-frames do not expose the persisted wall index. This reconstruction is
    prefix-causal and seed-only: it uses only current replay-visible action/position/hitstun plus
    extracted FD wall segments. Normal rollouts keep these lanes zero and carry `wall_kind/wall_id`
    through runtime mpColl.

    The scope is intentionally narrow to airborne damage-collision rows already outside a concrete
    FD wall surface. It is not a generic wall proximity/contact heuristic.

    refs/melee/src/melee/lb/types.h::CollData
    refs/melee/src/melee/mp/mplib.c::{mpLib_8004E398_LeftWall,mpLib_8004E684_RightWall}
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Coll
    data/stages/final_destination.json
    """
    line_id = np.array([int(seg['i']) for seg in stage_segments], dtype=np.uint16)
    kind_id = np.array([3 if seg.get('kind') == 'left_wall' else 2 if seg.get('kind') == 'right_wall' else 0 for seg in stage_segments], dtype=np.uint8)
    x0 = np.array([float(seg['x0']) for seg in stage_segments], dtype=np.float32)
    y0 = np.array([float(seg['y0']) for seg in stage_segments], dtype=np.float32)
    x1 = np.array([float(seg['x1']) for seg in stage_segments], dtype=np.float32)
    y1 = np.array([float(seg['y1']) for seg in stage_segments], dtype=np.float32)
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_mpcoll_wall_seed_lanes is required; run `make build`') from exc
    return msl_binding.derive_mpcoll_wall_seed_lanes(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1), np.asarray(action_frame_i16, dtype=np.int16).reshape(-1), np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1), np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1), np.asarray(pos_x_f32, dtype=np.float32).reshape(-1), np.asarray(pos_y_f32, dtype=np.float32).reshape(-1), int(stage_id_u32), line_id, kind_id, x0, y0, x1, y1)

def _derive_entry_end_fall_lock(*, action_id_u16: np.ndarray, on_ground_u8: np.ndarray, act_entry_end: int=324, act_fall: int=29) -> np.ndarray:
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
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_entry_end_fall_lock is required; run `make build`') from exc
    return msl_binding.derive_entry_end_fall_lock(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1), np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1), int(act_entry_end), int(act_fall))

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

@functools.lru_cache(maxsize=8)
def _stage_respawn_points_y(*, stage_id: int, data_dir: str='data') -> np.ndarray | None:
    """
    Load respawn-point Y values from the ISO-derived MSLSTG01 stage artifact.

    data/stages/bin/*.bin::MSLSTG01 respawn_points
    """
    stage_path = stage_metadata_path_for_stage_id(int(stage_id), Path(data_dir))
    if stage_path is None:
        return None
    stage = _read_mslstg01(stage_path)
    if len(stage.respawn_points) < 4:
        raise ValueError(f'{stage_path}: expected 4 respawn_points entries')
    out = np.zeros(4, dtype=np.float32)
    for port0 in range(4):
        out[port0] = np.float32(stage.respawn_points[port0].y)
    return out

def _respawn_point_y_for_stage_port(*, stage_id: int, port0: int, data_dir: str='data') -> float:
    if port0 < 0 or port0 >= 4:
        raise ValueError(f'port0 must be in [0,3], got {port0}')
    points = _stage_respawn_points_y(stage_id=int(stage_id), data_dir=data_dir)
    if points is None:
        return 0.0
    return float(points[port0])

def _load_throw_pulse_seed_tables(*, data_root, throw_action_to_move: dict[int, str]) -> tuple[dict[tuple[int, int], tuple[int, ...]], dict[tuple[int, int], int], dict[int, int]]:
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
    for char_id, key in ((1, 'fox'), (22, 'falco')):
        moves = _load_moves_file(data_root, key)['moves']
        shot_itkind_by_char[int(char_id)] = int(item_article_values_by_sim_char(data_root, 'blaster_shot_itkind').get(int(char_id), 0))
        for action_id, move_name in throw_action_to_move.items():
            events = moves.get(move_name, {}).get('events', [])
            pulses = sorted((int(ev.get('frame', 0)) for ev in events if ev.get('kind') == 'set_throw_spawn_projectile'))
            pulse_frames_by_char_action[int(char_id), int(action_id)] = tuple(pulses)
            cmd1_set_on = sorted((int(ev.get('frame', 0)) for ev in events if ev.get('kind') == 'set_cmd_var' and int((ev.get('data') or {}).get('idx', -1)) == 1 and (int((ev.get('data') or {}).get('value', -1)) == 1)))
            cmd1_start_by_char_action[int(char_id), int(action_id)] = int(cmd1_set_on[0]) if cmd1_set_on else -1
    return (pulse_frames_by_char_action, cmd1_start_by_char_action, shot_itkind_by_char)

@functools.lru_cache(maxsize=None)
def _load_specialn_loop_cmd0_windows(*, data_root) -> dict[tuple[int, int], tuple[int, int]]:
    """Load Fox/Falco SpecialN Loop raw cmd_var[0] windows from MSLFTSC1 script data.

    Runtime uses move_tables_special_cmd0_active_at_frame() for the live IASA latch check, but
    that helper intentionally includes a small latch-clear tail. Replay seed reconstruction must
    use the raw script interval only: a B edge after the source clear frame does not prove
    `mv.fx.SpecialN.isBlasterLoop` was live when Loop_Anim reached anim-end.

    Source: refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
      ftFx_SpecialNLoop_Anim,ftFx_SpecialNLoop_IASA,ftFx_SpecialAirNLoop_Anim,
      ftFx_SpecialAirNLoop_IASA}
    Script data: data/scripts/{fox,falco}.bin (MSLFTSC1 set_cmd_var idx=0).
    """
    windows: dict[tuple[int, int], tuple[int, int]] = {}
    for char_id, key in ((1, 'fox'), (22, 'falco')):
        table = read_mslftsc1_v1(data_root / 'scripts' / f'{key}.bin')
        manifest = _load_json_file(data_root / 'scripts' / f'{key}_manifest.json')
        kind_names = {int(row['id']): str(row['name']) for row in manifest.get('event_kinds', [])}
        for msid in (296, 299):
            entry = next((entry for entry in table.entries if int(entry.msid) == int(msid)), None)
            if entry is None:
                continue
            events = table.events[entry.first_event:entry.first_event + entry.event_count]
            on_frame = -1
            off_frame = -1
            for ev in events:
                if kind_names.get(int(ev.kind_id)) != 'set_cmd_var':
                    continue
                if int(ev.payload.get('idx', -1)) != 0:
                    continue
                value = int(ev.payload.get('value', 0))
                if value != 0 and on_frame < 0:
                    on_frame = int(ev.frame)
                elif value == 0 and on_frame >= 0 and (off_frame < 0):
                    off_frame = int(ev.frame)
            if on_frame >= 0 and off_frame >= 0:
                windows[int(char_id), int(msid)] = (int(on_frame), int(off_frame))
    return windows

@functools.lru_cache(maxsize=None)
def _load_runbrake_cmd0_seed_tables(*, data_root) -> tuple[dict[int, int], dict[int, int]]:
    """Load RunBrake cmd_var[0] timing from extracted move scripts.

    Source of truth:
    - data/moves/{fox,falco}.json moves["ftCo_SM_RunBrake"]["events"] set_cmd_var(idx=0)
    """
    cmd0_on_by_char: dict[int, int] = {}
    cmd0_off_by_char: dict[int, int] = {}
    from tools.extraction.char_registry import CHARS as _REGISTRY_CHARS_RB
    for char_id, key in ((info.internal_id, info.name) for info in _REGISTRY_CHARS_RB.values()):
        moves = _load_moves_file(data_root, key)['moves']
        events = moves.get('ftCo_SM_RunBrake', {}).get('events', [])
        cmd0_on = sorted((int(ev.get('frame', 0)) for ev in events if ev.get('kind') == 'set_cmd_var' and int((ev.get('data') or {}).get('idx', -1)) == 0 and (int((ev.get('data') or {}).get('value', -1)) != 0)))
        cmd0_off = sorted((int(ev.get('frame', 0)) for ev in events if ev.get('kind') == 'set_cmd_var' and int((ev.get('data') or {}).get('idx', -1)) == 0 and (int((ev.get('data') or {}).get('value', -1)) == 0)))
        cmd0_on_by_char[int(char_id)] = int(cmd0_on[0]) if cmd0_on else -1
        cmd0_off_by_char[int(char_id)] = int(cmd0_off[0]) if cmd0_off else -1
    return (cmd0_on_by_char, cmd0_off_by_char)

@functools.lru_cache(maxsize=None)
def _load_source_clear_terminal_followup_tables(*, data_root) -> tuple[dict[tuple[int, int], int], dict[tuple[int, int], int]]:
    """Load command-script phase gates for source-clear terminal followups.

    Source of truth:
    - data/moves/{fox,falco}.json moves["ftCo_SM_*"]["events"]
      - set_cmd_var(idx=0,value=1/0)
      - clear_hitboxes (for AttackHi3 continuation cutoff)
    - extracted from fighter subaction scripts in Pl*.dat.
    refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
    """
    action_to_move = {65: 'ftCo_SM_AttackAirN', 69: 'ftCo_SM_AttackAirLw', 236: 'ftCo_SM_EscapeAir', 56: 'ftCo_SM_AttackHi3'}
    cmd0_on_by_char_action: dict[tuple[int, int], int] = {}
    cmd0_off_by_char_action: dict[tuple[int, int], int] = {}
    from tools.extraction.char_registry import CHARS as _REGISTRY_CHARS_CA
    for char_id, key in ((info.internal_id, info.name) for info in _REGISTRY_CHARS_CA.values()):
        moves = _load_moves_file(data_root, key)['moves']
        for action_id, move_name in action_to_move.items():
            events = moves.get(move_name, {}).get('events', [])
            cmd0_on = sorted((int(ev.get('frame', 0)) for ev in events if ev.get('kind') == 'set_cmd_var' and int((ev.get('data') or {}).get('idx', -1)) == 0 and (int((ev.get('data') or {}).get('value', -1)) == 1)))
            cmd0_off = sorted((int(ev.get('frame', 0)) for ev in events if ev.get('kind') == 'set_cmd_var' and int((ev.get('data') or {}).get('idx', -1)) == 0 and (int((ev.get('data') or {}).get('value', -1)) == 0)))
            if int(action_id) == 56 and (not cmd0_off):
                clear_hitboxes = sorted((int(ev.get('frame', 0)) for ev in events if ev.get('kind') == 'clear_hitboxes'))
                cmd0_off = clear_hitboxes
            cmd0_on_by_char_action[int(char_id), int(action_id)] = int(cmd0_on[0]) if cmd0_on else -1
            cmd0_off_by_char_action[int(char_id), int(action_id)] = int(cmd0_off[0]) if cmd0_off else -1
    return (cmd0_on_by_char_action, cmd0_off_by_char_action)

@functools.lru_cache(maxsize=None)
def _load_action_x9_b1_tables(*, data_root) -> dict[int, np.ndarray]:
    """Load decomp MotionState.x9_b1 tables for supported chars from MSLACID1 v3.

    Source of truth:
    - data/attack_id/move_id/{fox,falco}.bin `motion_state_word` table.
    - Derived from decomp MotionState initializers.
    - refs/melee/src/melee/ft/types.h::MotionState
    - refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    """
    return {char_id: table.x9_b1 for char_id, table in load_action_state_tables(str(data_root)).items()}

def _derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes(*, action_id_u16: np.ndarray, char_id_u8: np.ndarray, on_ground_u8: np.ndarray, state_flags_u8: np.ndarray, last_hit_by_u8: np.ndarray, x9_b1_by_char: dict[int, np.ndarray], source_clear_init_frames: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
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
    max_actions = max((int(tbl.shape[0]) for tbl in x9_b1_by_char.values()), default=0)
    x9_lut = np.zeros((256, max_actions), dtype=np.uint8)
    for cid, tbl in x9_b1_by_char.items():
        arr = np.asarray(tbl, dtype=np.uint8).reshape(-1)
        x9_lut[int(cid) & 255, :int(arr.shape[0])] = arr
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes is required; run `make build`') from exc
    return msl_binding.derive_source_clear_timer_x18c8_and_owner_phase_seed_lanes(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1), np.asarray(char_id_u8, dtype=np.uint8).reshape(-1), np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1), _ascontiguousarray(state_flags_u8, dtype=np.uint8), np.asarray(last_hit_by_u8, dtype=np.uint8).reshape(-1), x9_lut, int(source_clear_init_frames))

def _derive_source_clear_grounded_damage_clear_phase_seed_lane(*, action_id_u16: np.ndarray, action_frame_i16: np.ndarray, on_ground_u8: np.ndarray, hitlag_u16: np.ndarray, hitstun_u16: np.ndarray, combo_count_u8: np.ndarray, source_clear_timer_x18c8_u8: np.ndarray, source_clear_owner_set_phase_u8: np.ndarray, state_flags_u8: np.ndarray, last_hit_by_u8: np.ndarray) -> np.ndarray:
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
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_source_clear_grounded_damage_clear_phase_seed_lane is required; run `make build`') from exc
    return msl_binding.derive_source_clear_grounded_damage_clear_phase_seed_lane(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1), np.asarray(action_frame_i16, dtype=np.int16).reshape(-1), np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1), np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1), np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1), np.asarray(combo_count_u8, dtype=np.uint8).reshape(-1), np.asarray(source_clear_timer_x18c8_u8, dtype=np.uint8).reshape(-1), np.asarray(source_clear_owner_set_phase_u8, dtype=np.uint8).reshape(-1), _ascontiguousarray(state_flags_u8, dtype=np.uint8), np.asarray(last_hit_by_u8, dtype=np.uint8).reshape(-1))

def _derive_source_clear_processhit_damage_pending_phase_seed_lane(*, action_id_u16: np.ndarray, action_frame_i16: np.ndarray, on_ground_u8: np.ndarray, hitlag_u16: np.ndarray, hitstun_u16: np.ndarray, combo_count_u8: np.ndarray, last_attack_landed_u8: np.ndarray, source_clear_timer_x18c8_u8: np.ndarray, source_clear_owner_set_phase_u8: np.ndarray, colanim_hit_status_x198c_u8: np.ndarray, state_flags_u8: np.ndarray, last_hit_by_u8: np.ndarray) -> np.ndarray:
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
    - This avoids replay-shaped row/action/timer fitting in validation-buffer generation.

    Causality:
    - Strictly causal: validate current-row preconditions only, never future frames.
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_source_clear_processhit_damage_pending_phase_seed_lane is required; run `make build`') from exc
    return msl_binding.derive_source_clear_processhit_damage_pending_phase_seed_lane(_ascontiguousarray(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)), _ascontiguousarray(state_flags_u8, dtype=np.uint8))

def _derive_phantom_damage_pending_seed_lanes(*, percent_f32: np.ndarray, hitlag_u16: np.ndarray, action_id_u16: np.ndarray, instance_hit_by_u16: np.ndarray, instance_id_u16: np.ndarray, num_players: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Derive hidden fighter phantom/tip-log damage pending at one-step reseed boundaries.

    Decomp ownership:
    - ftColl_80076ED8 stores phantom/tip-log damage into `fp->dmg.x1898` and starts hitlag through
      the `x1840/x18a0` branch.
    - Fighter_ProcessHit sets `x189C_unk_num_frames = hitlag`, then ftColl_8007BE3C applies x1898
      to percent/stale/combo when x189C expires.

    Seed policy:
    - Carry active x189C countdown rows where a later post-frame exposes that delayed x1898 percent
      addition before a knockback/body damage path supersedes it. The countdown is fighter damage
      state and can survive an intervening action change such as Guard -> GuardSetOff.
    - Store the current-row source fighter by matching `instance_hit_by` against live fighter
      instance ids; rows without a live source stay unseeded. The output field is named
      `phantom_damage_source_port`, but its value is a local simulator slot or 0xFF, not raw
      Slippi/controller source-port domain.

    This is intentionally a hidden-state seed lane, not a gameplay row branch. Runtime rollouts
    produce the same lane directly when a modeled phantom contact occurs.
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_phantom_damage_pending_seed_lanes is required; run `make build`') from exc
    return msl_binding.derive_phantom_damage_pending_seed_lanes(np.asarray(percent_f32, dtype=np.float32), np.asarray(hitlag_u16, dtype=np.uint16), np.asarray(action_id_u16, dtype=np.uint16), np.asarray(instance_hit_by_u16, dtype=np.uint16), np.asarray(instance_id_u16, dtype=np.uint16), int(num_players))

def _derive_fighter_8006cda4_pre_gate_consume_count_seed_lane(*, action_id_u16: np.ndarray, action_frame_i16: np.ndarray, ref_action_id_u16: np.ndarray, on_ground_u8: np.ndarray, hitlag_u16: np.ndarray, hitstun_u16: np.ndarray, state_flags_u8: np.ndarray, last_hit_by_u8: np.ndarray, all_source_port0_u8: np.ndarray, all_action_id_u16: np.ndarray, all_action_frame_i16: np.ndarray, all_ref_action_id_u16: np.ndarray | None=None, all_on_ground_u8: np.ndarray | None=None, all_hitlag_u16: np.ndarray | None=None, all_hitstun_u16: np.ndarray | None=None, all_last_hit_by_u8: np.ndarray | None=None, all_ref_last_hit_by_u8: np.ndarray | None=None, frame_pre_random_seed_u32: np.ndarray, damagefly_roll_prob: float, victim_port: int, num_players: int, allow_grounded_kneebend: bool=False) -> np.ndarray:
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
    - 3: consume all three decomp-visible pre-gate HSD_Randi calls before the DamageFlyRoll gate.
    - 4: source-proven zero-consume gate; admit the gate without a pre-gate stream advance.

    Current producer policy:
    - Materialize the replay-real families that are currently separable by strict-causal current-row
      context without replay-keyed lookup:
      * AttackAirB airborne carry -> one consume
      * ThrownF grounded-hitlag carry with x221A_b3 latched -> two consumes
      * DamageFlyTop airborne carry while the live source owner is still in the steady AttackAirB
        window -> two consumes
      * DamageFlyTop AttackAirB early/steady rows where the replay-visible RNG outcome proves the
        hidden held-item/x197C stream phase -> explicit zero-consume marker or one to three
        consumes
      * Catch-family severe-airborne damage entry rows where the replay-visible RNG outcome proves
        the hidden held-item/x197C stream phase -> explicit zero-consume marker or one to three
        consumes
      * FoD grounded KneeBend severe-airborne damage entry rows, currently scoped to the grIzumi
        validation owner where the same hidden stream phase is replay-visible without causing
        non-FoD rollout reshaping.

    Causality:
    - Runtime remains causal: it consumes only this explicit stream-phase lane.
    - Validation-buffer derivation uses `ref_t1.action_id` only to infer hidden Fighter_8006CDA4 stream phase
      where Slippi does not expose the consume-critical held-item/x197C internals.
    """
    if all_ref_action_id_u16 is None:
        all_ref_action_id_u16 = np.asarray(all_action_id_u16, dtype=np.uint16)
    if all_on_ground_u8 is None:
        all_on_ground_u8 = np.broadcast_to(np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1, 1), np.asarray(all_action_id_u16).shape)
    if all_hitlag_u16 is None:
        all_hitlag_u16 = np.broadcast_to(np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1, 1), np.asarray(all_action_id_u16).shape)
    if all_hitstun_u16 is None:
        all_hitstun_u16 = np.broadcast_to(np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1, 1), np.asarray(all_action_id_u16).shape)
    if all_last_hit_by_u8 is None:
        all_last_hit_by_u8 = np.broadcast_to(np.asarray(last_hit_by_u8, dtype=np.uint8).reshape(-1, 1), np.asarray(all_action_id_u16).shape)
    if all_ref_last_hit_by_u8 is None:
        all_ref_last_hit_by_u8 = all_last_hit_by_u8
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_fighter_8006cda4_pre_gate_consume_count is required; run `make build`') from exc
    return msl_binding.derive_fighter_8006cda4_pre_gate_consume_count(_ascontiguousarray(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)), _ascontiguousarray(np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)), _ascontiguousarray(np.asarray(ref_action_id_u16, dtype=np.uint16).reshape(-1)), _ascontiguousarray(np.asarray(on_ground_u8, dtype=np.uint8).reshape(-1)), _ascontiguousarray(np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1)), _ascontiguousarray(np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1)), _ascontiguousarray(state_flags_u8, dtype=np.uint8), _ascontiguousarray(np.asarray(last_hit_by_u8, dtype=np.uint8).reshape(-1)), _ascontiguousarray(all_source_port0_u8, dtype=np.uint8), _ascontiguousarray(all_action_id_u16, dtype=np.uint16), _ascontiguousarray(all_action_frame_i16, dtype=np.int16), _ascontiguousarray(all_ref_action_id_u16, dtype=np.uint16), _ascontiguousarray(all_on_ground_u8, dtype=np.uint8), _ascontiguousarray(all_hitlag_u16, dtype=np.uint16), _ascontiguousarray(all_hitstun_u16, dtype=np.uint16), _ascontiguousarray(all_last_hit_by_u8, dtype=np.uint8), _ascontiguousarray(all_ref_last_hit_by_u8, dtype=np.uint8), _ascontiguousarray(np.asarray(frame_pre_random_seed_u32, dtype=np.uint32).reshape(-1)), float(damagefly_roll_prob), int(victim_port), int(num_players), int(bool(allow_grounded_kneebend)))

def _derive_source_clear_terminal_phase_seed_lane(*, char_id_u8: np.ndarray, action_id_u16: np.ndarray, action_frame_i16: np.ndarray, hitlag_u16: np.ndarray, hitstun_u16: np.ndarray, combo_count_u8: np.ndarray, last_attack_landed_u8: np.ndarray, source_clear_timer_x18c8_u8: np.ndarray, source_clear_owner_set_phase_u8: np.ndarray, state_flags_u8: np.ndarray, last_hit_by_u8: np.ndarray, terminal_followup_cmd0_on_by_char_action: dict[tuple[int, int], int], terminal_followup_cmd0_off_by_char_action: dict[tuple[int, int], int]) -> np.ndarray:
    """Derive one-step terminal phase lane for source-owner clear/parking.

    Decomp ownership:
    - Fighter_8006A360 owns x18C8 countdown + terminal source-owner clear in proc-prio-1.
      Some terminal callback contexts park the source owner while retiring the countdown.
    - Slippi `last_hit_by` mirrors `dmg.x18C4_source_ply` snapshots.
    refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm

    Seed representation:
    - 0: default terminal-clear behavior at `source_clear_timer_x18c8 == 1`.
    - 1: park source owner and retire the countdown on this row.

    Producer policy (narrow, replay-causal):
    - only on terminal timer rows (`t == 1`) under !x221F_b3,
    - only when the active timer run is backed by a causal source-owner set phase edge,
    - only from present/past lanes (`t` and `t-1`), never future frames,
    - only for source/provenance classes accepted by the native seed-bridge predicate,
    - only in stable ongoing ownership context:
      - no active hitlag/hitstun at `t` (runtime already has explicit hitstun defer),
      - prior row continuity (`timer 2->1`, same owner, same action progression),
      - active combo provenance (`combo_count > 0 && last_attack_landed > 0`).

    This keeps derivation strict-causal for one-step reseed. Validation-inferred action/frame/flag
    distinctions are seed/provenance bridge inputs, not free-running gameplay predicates.
    refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
    """
    max_action = max([*(int(action) for _, action in terminal_followup_cmd0_on_by_char_action.keys()), *(int(action) for _, action in terminal_followup_cmd0_off_by_char_action.keys()), 0])
    cmd0_on = np.full((256, max_action + 1), -1, dtype=np.int16)
    cmd0_off = np.full((256, max_action + 1), -1, dtype=np.int16)
    for (cid, action), frame in terminal_followup_cmd0_on_by_char_action.items():
        cmd0_on[int(cid) & 255, int(action)] = np.int16(int(frame))
    for (cid, action), frame in terminal_followup_cmd0_off_by_char_action.items():
        cmd0_off[int(cid) & 255, int(action)] = np.int16(int(frame))
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_source_clear_terminal_phase_seed_lane is required; run `make build`') from exc
    return msl_binding.derive_source_clear_terminal_phase_seed_lane(_ascontiguousarray(np.asarray(char_id_u8, dtype=np.uint8).reshape(-1)), _ascontiguousarray(np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)), _ascontiguousarray(np.asarray(action_frame_i16, dtype=np.int16).reshape(-1)), _ascontiguousarray(np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1)), _ascontiguousarray(np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1)), _ascontiguousarray(np.asarray(combo_count_u8, dtype=np.uint8).reshape(-1)), _ascontiguousarray(np.asarray(last_attack_landed_u8, dtype=np.uint8).reshape(-1)), _ascontiguousarray(np.asarray(source_clear_timer_x18c8_u8, dtype=np.uint8).reshape(-1)), _ascontiguousarray(np.asarray(source_clear_owner_set_phase_u8, dtype=np.uint8).reshape(-1)), _ascontiguousarray(state_flags_u8, dtype=np.uint8), _ascontiguousarray(np.asarray(last_hit_by_u8, dtype=np.uint8).reshape(-1)), cmd0_on, cmd0_off)
