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
    hitstun_u16_from_misc_as_and_state_flags3,
    item_article_kind_set,
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
    SSBM.iso::IfAll.dat::ScInfCnt_scene_models[3]
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

@functools.lru_cache(maxsize=None)
def _load_specialn_loop_cmd0_windows(*, data_root) -> dict[tuple[int, int], tuple[int, int]]:
    """Load Fox/Falco SpecialN Loop raw cmd_var[0] windows from MSLFTSC1 script data.

    Runtime consumes this command through the live fighter-script cursor and retains a small
    latch-clear tail. Replay seed reconstruction must use the raw script interval only: a B edge
    after the source clear frame does not prove
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

def _derive_source_clear_motion_state_entry_events(
    *,
    seed_u8: np.ndarray,
    prev_input_u8: np.ndarray,
    input_u8: np.ndarray,
    num_players: int,
    ucf_enabled: bool,
    ucf_cardinals_1_0_enabled: bool,
    source_clear_init_frames: int,
    chunk_size: int = 256,
) -> np.ndarray:
    """Observe x18C8 writers by stepping the ordinary runtime once per teacher-forced seed.

    A Slippi post-frame row cannot expose intermediate same-frame MotionStates. In particular, an
    Anim callback can enter grounded Wait (x9_b1), start x18C8, and then an IASA callback can enter
    another state before the row is recorded. The simulator already owns that callback ordering;
    use it directly instead of duplicating transition/action lists in preprocessing.

    This is native batched teacher-forced reconstruction of real hidden source state. Python only
    schedules fixed-size chunks; there is no per-frame Python derivation.
    refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding is required; run `make build`') from exc

    seed = _ascontiguousarray(seed_u8, dtype=np.uint8)
    prev = _ascontiguousarray(prev_input_u8, dtype=np.uint8)
    current = _ascontiguousarray(input_u8, dtype=np.uint8)
    if seed.ndim != 2 or prev.ndim != 2 or current.ndim != 2:
        raise ValueError('runtime source-clear probe inputs must be 2D uint8 arrays')
    if prev.shape[0] != seed.shape[0] or current.shape[0] != seed.shape[0]:
        raise ValueError('runtime source-clear probe row counts differ')
    n = int(seed.shape[0])
    out = np.zeros((n, 4), dtype=np.uint8)
    if n == 0 or int(source_clear_init_frames) <= 0:
        return out

    cap = max(1, min(int(chunk_size), n))
    seed_chunk = np.empty((cap, seed.shape[1]), dtype=np.uint8)
    prev_chunk = np.empty((cap, prev.shape[1]), dtype=np.uint8)
    input_chunk = np.empty((cap, current.shape[1]), dtype=np.uint8)
    handle = msl_binding.init(
        batch_size=cap,
        num_players=int(num_players),
        ucf_enabled=int(bool(ucf_enabled)),
        ucf_cardinals_1_0_enabled=int(bool(ucf_cardinals_1_0_enabled)),
    )
    try:
        for start in range(0, n, cap):
            stop = min(start + cap, n)
            count = stop - start
            seed_chunk[:count] = seed[start:stop]
            prev_chunk[:count] = prev[start:stop]
            input_chunk[:count] = current[start:stop]
            if count < cap:
                seed_chunk[count:] = seed_chunk[count - 1]
                prev_chunk[count:] = prev_chunk[count - 1]
                input_chunk[count:] = input_chunk[count - 1]
            msl_binding.reseed_seed(handle, seed_chunk)
            msl_binding.step_input(handle, prev_chunk, input_chunk)
            timer = msl_binding.validation_source_clear_timer_state(handle)
            out[start:stop] = (
                np.asarray(timer[:count], dtype=np.uint8) == np.uint8(source_clear_init_frames)
            )
    finally:
        msl_binding.destroy(handle)
    return out


def _derive_source_clear_timer_x18c8_seed_lane(*, state_flags_u8: np.ndarray, last_hit_by_u8: np.ndarray, hitlag_u16: np.ndarray, hitstun_u16: np.ndarray, instance_hit_by_u16: np.ndarray, percent_f32: np.ndarray, motion_state_entry_event_u8: np.ndarray, source_clear_init_frames: int) -> np.ndarray:
    """Derive the real hidden x18C8 countdown from its source writers and timer owner.

    Decomp ownership:
    - Fighter_ChangeMotionState seeds `dmg.x18C8 = p_ftCommonData->x814` iff grounded,
      new_motion_state->x9_b1, and dmg.x18C8 == -1. The entry-event lane is observed by stepping
      the ordinary runtime, so hidden same-frame intermediate MotionStates are retained.
    - Every ftColl_8007861C damage-source write resets x18C8 to -1, including same-port hits.
    - Fighter_8006A360 decrements x18C8 under !fp->x221F_b3; at zero it clears source owner.
    refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
    refs/melee/src/melee/ft/ftcoll.c::ftColl_8007861C
    refs/melee/src/melee/ft/types.h::MotionState (x9_b1)
    refs/melee/src/melee/ft/types.h (fp+0x221F bitfields; b3 gate)
    refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (state_flags byte at fp+0x221F)

    Seed representation is direct: 0 means decomp -1, N>0 is the live countdown. Source-port,
    source-instance, and damage/hitlag edges are the replay-visible evidence of ftColl_8007861C's
    otherwise hidden write. The derivation is prefix-causal and does not inspect the reference row.
    """
    try:
        import msl_binding
    except ImportError as exc:
        raise RuntimeError('native msl_binding.derive_source_clear_timer_x18c8_seed_lane is required; run `make build`') from exc
    return msl_binding.derive_source_clear_timer_x18c8_seed_lane(
        _ascontiguousarray(state_flags_u8, dtype=np.uint8),
        _ascontiguousarray(np.asarray(last_hit_by_u8).reshape(-1), dtype=np.uint8),
        _ascontiguousarray(np.asarray(hitlag_u16).reshape(-1), dtype=np.uint16),
        _ascontiguousarray(np.asarray(hitstun_u16).reshape(-1), dtype=np.uint16),
        _ascontiguousarray(np.asarray(instance_hit_by_u16).reshape(-1), dtype=np.uint16),
        _ascontiguousarray(np.asarray(percent_f32).reshape(-1), dtype=np.float32),
        _ascontiguousarray(np.asarray(motion_state_entry_event_u8).reshape(-1), dtype=np.uint8),
        int(source_clear_init_frames),
    )

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
