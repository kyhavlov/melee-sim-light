from __future__ import annotations

from tools.eval.mismatch_taxonomy import (
    FAMILY_META,
    AuditSample,
    ItemSlotRow,
    MismatchEvent,
    PlayerRow,
    _classify_item_slot_row,
    _classify_player_row,
    _family_for_debug_body_contact_residual,
    _split_cross_player_instance_counter_rows,
    _write_audit_samples_tsv,
    _write_family_tsv,
    build_summary,
    build_audit_summary,
)


def _player_row(**overrides) -> PlayerRow:
    base = PlayerRow(
        dataset="d.msl",
        record=10,
        p=0,
        seed_frame=100,
        ref_frame=101,
        seed_action_id=178,
        ref_action_id=181,
        out_action_id=182,
        prev_action_id=20,
        seed_action_frame=0,
        ref_action_frame=1,
        out_action_frame=0,
        on_ground=1,
        hitlag=0,
        hitstun=0,
        fields=("action_id", "action_frame", "animation_index", "instance_id"),
        family_id="F99_misc_other",
    )
    return PlayerRow(**{**base.__dict__, **overrides})


def _event(family_id: str, field: str, *, subject: str = "p0", seed: int = 0, ref: int = 1, out: int = 0) -> MismatchEvent:
    return MismatchEvent(
        family_id=family_id,
        dataset="d.msl",
        record=10,
        subject=subject,
        seed_frame=100,
        ref_frame=101,
        field=field,
        seed=seed,
        ref=ref,
        out=out,
        seed_action_id=178,
        ref_action_id=181,
        out_action_id=182,
        prev_action_id=20,
        seed_action_frame=0,
        ref_action_frame=1,
        out_action_frame=0,
        on_ground=1,
        hitlag=0,
        hitstun=0,
    )


def test_classify_player_row_guard_release_collision_uses_guard_family() -> None:
    row = _player_row()
    got = _classify_player_row(row, {178: "GUARD_ON", 181: "GUARD_SET_OFF", 182: "GUARD_REFLECT", 20: "DASH"})
    assert got == "F01_guard_release_collision"


def test_classify_player_row_capturewait_bridge_uses_capture_family() -> None:
    row = _player_row(
        seed_action_id=227,
        ref_action_id=227,
        out_action_id=227,
        prev_action_id=226,
        fields=("action_frame",),
    )
    got = _classify_player_row(
        row,
        {
            226: "CAPTURE_PULLED_LW",
            227: "CAPTURE_WAIT_LW",
        },
    )
    assert got == "F03_capturewait_bridge"


def test_classify_player_row_damageflyroll_rng_gate_takes_priority() -> None:
    row = _player_row(
        seed_action_id=90,
        ref_action_id=91,
        out_action_id=88,
        prev_action_id=90,
        fields=("action_id", "hitlag", "hitstun", "instance_id"),
    )
    got = _classify_player_row(
        row,
        {
            88: "DAMAGE_FLY_N",
            90: "DAMAGE_FLY_TOP",
            91: "DAMAGE_FLY_ROLL",
        },
    )
    assert got == "F26_damageflyroll_rng_stream_seed_surface"


def test_classify_player_row_keeps_damagefly_grounded_selector_in_f07() -> None:
    row = _player_row(
        seed_action_id=88,
        ref_action_id=183,
        out_action_id=201,
        prev_action_id=88,
        fields=("action_id", "animation_index", "on_ground", "hurtbox_state", "state_flags[3]"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            88: "DAMAGE_FLY_N",
            183: "DOWN_BOUND_U",
            201: "PASSIVE_STAND_B",
        },
    )
    assert got == "F07_knockdown_grounding"


def test_classify_player_row_splits_damagefly_tech_timer_seed_surface() -> None:
    row = _player_row(
        seed_action_id=88,
        ref_action_id=183,
        out_action_id=201,
        prev_action_id=88,
        fields=("action_id", "animation_index", "hurtbox_state", "state_flags[3]"),
        on_ground=0,
        hitlag=0,
    )
    got = _classify_player_row(
        row,
        {
            88: "DAMAGE_FLY_N",
            183: "DOWN_BOUND_U",
            201: "PASSIVE_STAND_B",
        },
    )
    assert got == "F18_damage_tech_timer_seed_surface"


def test_classify_player_row_moves_damagefly_passive_contact_timing_to_damage_transition_owner() -> None:
    row = _player_row(
        seed_action_id=87,
        ref_action_id=199,
        out_action_id=87,
        prev_action_id=87,
        fields=("action_id", "animation_index", "on_ground", "jumps_left", "hurtbox_state"),
    )
    got = _classify_player_row(
        row,
        {
            87: "DAMAGE_FLY_HI",
            199: "PASSIVE",
        },
    )
    assert got == "F27a_damagefly_floor_contact_callback_phase"


def test_classify_player_row_moves_passivewall_contact_timing_to_damage_transition_owner() -> None:
    row = _player_row(
        seed_action_id=27,
        ref_action_id=203,
        out_action_id=27,
        prev_action_id=27,
        fields=("action_id", "animation_index", "hurtbox_state"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            27: "JUMP_AERIAL_F",
            203: "PASSIVE_WALL_JUMP",
        },
    )
    assert got == "F27c_passivewall_contact_callback_phase"


def test_classify_player_row_moves_damage_ground_id_to_floor_line_identity() -> None:
    row = _player_row(
        seed_action_id=86,
        ref_action_id=86,
        out_action_id=86,
        prev_action_id=86,
        fields=("ground_id",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            86: "DAMAGE_AIR_3",
        },
    )
    assert got == "F10m_floor_line_identity"


def test_classify_player_row_does_not_hide_combat_damage_entry_in_mpcoll_residual() -> None:
    row = _player_row(
        seed_action_id=64,
        ref_action_id=90,
        out_action_id=64,
        prev_action_id=64,
        fields=("action_id", "animation_index", "hitlag", "hitstun", "on_ground", "instance_hit_by"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            64: "ATTACK_LW4",
            90: "DAMAGE_FLY_TOP",
        },
    )
    assert got == "F08b_body_contact_geometry_residual"


def test_classify_player_row_routes_grounded_missed_damage_to_body_geometry() -> None:
    row = _player_row(
        seed_action_id=18,
        ref_action_id=76,
        out_action_id=18,
        prev_action_id=18,
        fields=("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "on_ground"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            18: "TURN",
            76: "DAMAGE_HI_2",
        },
    )
    assert got == "F08b_body_contact_geometry_residual"
    meta = FAMILY_META[got]
    assert meta.owner_module == "hsd_pose_collision"
    assert "ftdynamics.c" in "\n".join(meta.refs)


def test_classify_player_row_routes_attackairn_attackhi3_pose_surface_to_hsd_pose_collision() -> None:
    # BHH:1599-shaped live-pose row:
    # - seed p0 AttackAirN hits grounded p1 AttackHi3 in vanilla,
    # - sim has no pre-combat BODY selection because extracted hurtcaps differ from live HSD JObj
    #   dynamics pose, so this is upstream collision pose ownership, not post-admission followup.
    row = _player_row(
        dataset="BlondHardHippopotamus.msl",
        record=1599,
        p=1,
        seed_action_id=56,
        ref_action_id=88,
        out_action_id=56,
        prev_action_id=56,
        fields=("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "on_ground"),
        on_ground=1,
        hitlag=0,
        hitstun=0,
    )
    got = _classify_player_row(
        row,
        {
            56: "ATTACK_HI3",
            88: "DAMAGE_FLY_N",
        },
    )
    assert got == "F08b_body_contact_geometry_residual"


def test_debug_body_contact_split_routes_resolved_geometry_residuals_to_named_owners() -> None:
    assert (
        _family_for_debug_body_contact_residual(
            seed_name="WAIT",
            ref_name="WAIT",
            out_name="DAMAGE_FLY_TOP",
            selected_body_count=1,
            body_candidate_count=1,
            filtered_body_candidate_count=1,
            first_msid=52,
        )
        == "F08h_body_selected_false_grounded_attack_pose"
    )
    assert (
        _family_for_debug_body_contact_residual(
            seed_name="WAIT",
            precombat_name="JUMP_AERIAL_F",
            ref_name="WAIT",
            out_name="DAMAGE_FLY_TOP",
            selected_body_count=1,
            body_candidate_count=1,
            filtered_body_candidate_count=1,
            first_msid=72,
        )
        == "F10l_body_selected_false_action_timebase"
    )
    assert (
        _family_for_debug_body_contact_residual(
            seed_name="WAIT",
            ref_name="WAIT",
            out_name="DAMAGE_FLY_TOP",
            selected_body_count=1,
            body_candidate_count=1,
            filtered_body_candidate_count=1,
            first_msid=313,
        )
        == "F28_body_contact_candidate_narrowphase_owner"
    )
    assert (
        _family_for_debug_body_contact_residual(
            seed_name="JUMP_F",
            ref_name="DAMAGE_FLY_N",
            out_name="JUMP_F",
            selected_body_count=0,
            body_candidate_count=0,
            filtered_body_candidate_count=0,
            active_fighter_hitbox_count=1,
        )
        == "F10k_body_no_candidate_action_timing"
    )
    assert (
        _family_for_debug_body_contact_residual(
            seed_name="JUMP_F",
            ref_name="DAMAGE_FLY_N",
            out_name="JUMP_F",
            selected_body_count=0,
            body_candidate_count=0,
            filtered_body_candidate_count=0,
            active_fighter_hitbox_count=0,
            live_nonvictim_item_count=0,
        )
        == "F27b_damage_air_landing_action_callback_phase"
    )
    assert (
        _family_for_debug_body_contact_residual(
            seed_name="JUMP_F",
            ref_name="DAMAGE_FLY_N",
            out_name="JUMP_F",
            selected_body_count=0,
            body_candidate_count=0,
            filtered_body_candidate_count=0,
            active_fighter_hitbox_count=0,
            live_nonvictim_item_count=1,
        )
        == "F28_body_contact_candidate_narrowphase_owner"
    )
    assert (
        _family_for_debug_body_contact_residual(
            seed_name="JUMP_F",
            ref_name="DAMAGE_FLY_N",
            out_name="JUMP_F",
            selected_body_count=0,
            body_candidate_count=2,
            filtered_body_candidate_count=1,
        )
        == "F28_body_contact_candidate_narrowphase_owner"
    )
    assert (
        _family_for_debug_body_contact_residual(
            seed_name="ATTACK_AIR_B",
            ref_name="DAMAGE_AIR_2",
            out_name="ATTACK_AIR_B",
            selected_body_count=0,
            body_candidate_count=5,
            filtered_body_candidate_count=5,
            first_msid=70,
        )
        == "F28_body_contact_candidate_narrowphase_owner"
    )
    assert (
        _family_for_debug_body_contact_residual(
            seed_name="ESCAPE_B",
            ref_name="DAMAGE_N_3",
            out_name="DAMAGE_AIR_3",
            selected_body_count=1,
            body_candidate_count=1,
            filtered_body_candidate_count=1,
        )
        == "F05b_damage_hurt_height_selection_residual"
    )
    assert (
        _family_for_debug_body_contact_residual(
            seed_name="ATTACK_DASH",
            ref_name="REBOUND_STOP",
            out_name="DAMAGE_FLY_TOP",
            selected_body_count=1,
            body_candidate_count=1,
            filtered_body_candidate_count=1,
            first_msid=58,
        )
        == "F08h1_body_selected_false_rebound_clank_residual"
    )
    assert (
        _family_for_debug_body_contact_residual(
            seed_name="KNEE_BEND",
            ref_name="ATTACK_HI4",
            out_name="DAMAGE_N_2",
            selected_body_count=1,
            body_candidate_count=1,
            filtered_body_candidate_count=1,
            first_msid=70,
        )
        == "F10l_body_selected_false_action_timebase"
    )


def test_classify_player_row_splits_prior_damage_exit_from_body_geometry() -> None:
    row = _player_row(
        seed_action_id=76,
        ref_action_id=42,
        out_action_id=76,
        prev_action_id=76,
        fields=("action_id", "animation_index", "on_ground", "hitstun", "instance_id"),
        on_ground=0,
        hitlag=0,
        hitstun=9,
    )
    got = _classify_player_row(
        row,
        {
            42: "LANDING",
            76: "DAMAGE_HI_2",
        },
    )
    assert got == "F27b_damage_air_landing_action_callback_phase"


def test_classify_player_row_keeps_non_damage_down_transition_in_f08c() -> None:
    row = _player_row(
        seed_action_id=183,
        ref_action_id=186,
        out_action_id=186,
        prev_action_id=183,
        fields=("action_frame",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            183: "DOWN_BOUND_U",
            186: "DOWN_STAND_U",
        },
    )
    assert got == "F27d_down_damage_hidden_timer_phase"


def test_classify_player_row_does_not_hide_special_adjacency_in_mpcoll_residual() -> None:
    row = _player_row(
        seed_action_id=90,
        ref_action_id=350,
        out_action_id=90,
        prev_action_id=90,
        fields=("action_id", "animation_index", "jumps_left"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            90: "DAMAGE_FLY_TOP",
            350: "FX_SPECIAL_AIR_S_START",
        },
    )
    assert got == "F23_special_common_entry_dispatch"


def test_classify_player_row_moves_kneebend_specialhihold_tail_to_selector_owner() -> None:
    row = _player_row(
        seed_action_id=24,
        ref_action_id=353,
        out_action_id=24,
        prev_action_id=84,
        fields=("action_id", "animation_index", "instance_id"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            24: "KNEE_BEND",
            84: "DAMAGE_AIR_1",
            353: "FX_SPECIAL_HI_HOLD",
        },
    )
    assert got == "F10a_grounded_selector_transition"


def test_classify_player_row_keeps_landingfallspecial_out_of_common_special_dispatch() -> None:
    row = _player_row(
        seed_action_id=43,
        ref_action_id=360,
        out_action_id=43,
        prev_action_id=43,
        fields=("action_id", "animation_index", "hitlag"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            43: "LANDING_FALL_SPECIAL",
            360: "FX_SPECIAL_LW_START",
        },
    )
    assert got == "F20_speciallw_shine_reflector"


def test_classify_player_row_keeps_active_special_out_of_common_special_dispatch() -> None:
    row = _player_row(
        seed_action_id=369,
        ref_action_id=27,
        out_action_id=354,
        prev_action_id=369,
        fields=("action_id", "animation_index", "action_frame", "instance_id"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            27: "JUMP_AERIAL_F",
            354: "FX_SPECIAL_HI_HOLD_AIR",
            369: "FX_SPECIAL_AIR_LW_TURN",
        },
    )
    assert got == "F20_speciallw_shine_reflector"


def test_classify_player_row_keeps_jumpaerial_special_entry_in_common_dispatch() -> None:
    row = _player_row(
        seed_action_id=27,
        ref_action_id=344,
        out_action_id=65,
        prev_action_id=27,
        fields=("action_id", "animation_index"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            27: "JUMP_AERIAL_F",
            65: "ATTACK_AIR_N",
            344: "FX_SPECIAL_AIR_N_START",
        },
    )
    assert got == "F23_special_common_entry_dispatch"


def test_classify_player_row_hard_moves_specials_hurtbox_tail_to_aerial_stateflag() -> None:
    row = _player_row(
        seed_action_id=350,
        ref_action_id=350,
        out_action_id=350,
        prev_action_id=350,
        fields=("hurtbox_state",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            350: "FX_SPECIAL_AIR_S_START",
        },
    )
    assert got == "F09a_aerial_stateflag_hurtbox_adjacency"


def test_classify_player_row_hard_moves_specials_bookkeeping_tail_to_aerial_bookkeeping() -> None:
    row = _player_row(
        seed_action_id=352,
        ref_action_id=352,
        out_action_id=352,
        prev_action_id=351,
        fields=("combo_count", "last_attack_landed"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            351: "FX_SPECIAL_AIR_S",
            352: "FX_SPECIAL_AIR_S_END",
        },
    )
    assert got == "F09b_aerial_bookkeeping_adjacency"


def test_classify_player_row_hard_moves_specials_contact_tail_to_body_filter() -> None:
    row = _player_row(
        seed_action_id=352,
        ref_action_id=90,
        out_action_id=352,
        prev_action_id=352,
        fields=("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "state_flags[1]"),
        on_ground=0,
        hitlag=0,
    )
    got = _classify_player_row(
        row,
        {
            90: "DAMAGE_FLY_TOP",
            352: "FX_SPECIAL_AIR_S_END",
        },
    )
    assert got == "F28_body_contact_candidate_narrowphase_owner"


def test_classify_player_row_hard_moves_specialhi_holdair_hurtbox_tail_to_aerial_stateflag() -> None:
    row = _player_row(
        seed_action_id=354,
        ref_action_id=354,
        out_action_id=354,
        prev_action_id=354,
        fields=("hurtbox_state",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            354: "FX_SPECIAL_HI_HOLD_AIR",
        },
    )
    assert got == "F09a_aerial_stateflag_hurtbox_adjacency"


def test_classify_player_row_keeps_specialhi_holdair_launch_tail_in_firefox_owner() -> None:
    row = _player_row(
        seed_action_id=354,
        ref_action_id=356,
        out_action_id=356,
        prev_action_id=354,
        fields=("jumps_left",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            354: "FX_SPECIAL_HI_HOLD_AIR",
            356: "FX_SPECIAL_AIR_HI",
        },
    )
    assert got == "F22_specialhi_firefox_firebird"


def test_classify_player_row_moves_specialhi_bound_collision_to_mpcoll_owner() -> None:
    row = _player_row(
        seed_action_id=356,
        ref_action_id=359,
        out_action_id=356,
        prev_action_id=356,
        fields=("action_id", "animation_index", "action_frame"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            356: "FX_SPECIAL_AIR_HI",
            359: "FX_SPECIAL_HI_BOUND",
        },
    )
    assert got == "F10p_specialhi_bound_collision_callback"


def test_classify_player_row_moves_specialhi_landing_ground_id_to_mpcoll() -> None:
    row = _player_row(
        seed_action_id=357,
        ref_action_id=357,
        out_action_id=357,
        prev_action_id=357,
        fields=("ground_id",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            357: "FX_SPECIAL_HI_LANDING",
        },
    )
    assert got == "F10m_floor_line_identity"


def test_classify_player_row_moves_specialhi_damage_tail_to_damage_owner() -> None:
    row = _player_row(
        seed_action_id=359,
        ref_action_id=90,
        out_action_id=90,
        prev_action_id=359,
        fields=("jumps_left",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            90: "DAMAGE_FLY_TOP",
            359: "FX_SPECIAL_HI_BOUND",
        },
    )
    assert got == "F27b_damage_air_landing_action_callback_phase"


def test_classify_player_row_splits_passivewalljump_special_entry_dispatch() -> None:
    row = _player_row(
        seed_action_id=203,
        ref_action_id=365,
        out_action_id=203,
        prev_action_id=203,
        fields=("action_id", "animation_index", "action_frame", "instance_id"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            203: "PASSIVE_WALL_JUMP",
            365: "FX_SPECIAL_AIR_LW_START",
        },
    )
    assert got == "F23_special_common_entry_dispatch"


def test_classify_player_row_moves_pure_grounded_shine_hurtbox_tail_to_stateflag_owner() -> None:
    row = _player_row(
        seed_action_id=360,
        ref_action_id=360,
        out_action_id=360,
        prev_action_id=40,
        fields=("hurtbox_state",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            40: "SQUAT_WAIT",
            360: "FX_SPECIAL_LW_START",
        },
    )
    assert got == "F10d_hurtbox_stateflag_adjacency"


def test_classify_player_row_keeps_prior_special_entry_contact_out_of_common_dispatch() -> None:
    row = _player_row(
        seed_action_id=361,
        ref_action_id=361,
        out_action_id=90,
        prev_action_id=360,
        fields=("action_id", "animation_index", "hitlag", "hitstun", "state_flags[1]"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            90: "DAMAGE_FLY_TOP",
            360: "FX_SPECIAL_LW_START",
            361: "FX_SPECIAL_LW_LOOP",
        },
    )
    assert got == "F20_speciallw_shine_reflector"


def test_classify_player_row_moves_pure_grounded_shine_stateflag_tail_to_stateflag_owner() -> None:
    row = _player_row(
        seed_action_id=50,
        ref_action_id=360,
        out_action_id=360,
        prev_action_id=50,
        fields=("state_flags[0]",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            50: "ATTACK_DASH",
            360: "FX_SPECIAL_LW_START",
        },
    )
    assert got == "F10d_hurtbox_stateflag_adjacency"


def test_classify_player_row_moves_pure_aerial_shine_hurtbox_tail_to_aerial_stateflag() -> None:
    row = _player_row(
        seed_action_id=366,
        ref_action_id=366,
        out_action_id=366,
        prev_action_id=366,
        fields=("hurtbox_state",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            366: "FX_SPECIAL_AIR_LW_LOOP",
        },
    )
    assert got == "F09a_aerial_stateflag_hurtbox_adjacency"


def test_classify_player_row_moves_pure_grounded_shine_hitlag_scalar_to_combat() -> None:
    row = _player_row(
        seed_action_id=39,
        ref_action_id=360,
        out_action_id=360,
        prev_action_id=199,
        fields=("hitlag",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            39: "SQUAT",
            199: "PASSIVE",
            360: "FX_SPECIAL_LW_START",
        },
    )
    assert got == "F10b_grounded_combat_adjacency"


def test_classify_player_row_moves_pure_grounded_shine_source_bookkeeping_to_combat() -> None:
    row = _player_row(
        seed_action_id=360,
        ref_action_id=361,
        out_action_id=361,
        prev_action_id=360,
        fields=("last_hit_by",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            360: "FX_SPECIAL_LW_START",
            361: "FX_SPECIAL_LW_LOOP",
        },
    )
    assert got == "F10b_grounded_combat_adjacency"


def test_classify_player_row_moves_pure_aerial_shine_source_bookkeeping_to_aerial_combat() -> None:
    row = _player_row(
        seed_action_id=25,
        ref_action_id=365,
        out_action_id=365,
        prev_action_id=24,
        fields=("last_attack_landed",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            24: "KNEE_BEND",
            25: "JUMP_F",
            365: "FX_SPECIAL_AIR_LW_START",
        },
    )
    assert got == "F09b_aerial_bookkeeping_adjacency"


def test_classify_player_row_moves_aerial_shine_contact_hitlag_tail_to_aerial_contact() -> None:
    row = _player_row(
        seed_action_id=25,
        ref_action_id=365,
        out_action_id=365,
        prev_action_id=24,
        fields=("hitlag", "state_flags[1]"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            24: "KNEE_BEND",
            25: "JUMP_F",
            365: "FX_SPECIAL_AIR_LW_START",
        },
    )
    assert got == "F29_aerial_contact_hitlag_provenance"


def test_classify_player_row_moves_grounded_shine_contact_bookkeeping_to_combat_owner() -> None:
    row = _player_row(
        seed_action_id=39,
        ref_action_id=360,
        out_action_id=360,
        prev_action_id=39,
        fields=("hitlag", "last_attack_landed", "state_flags[1]"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            39: "SQUAT",
            360: "FX_SPECIAL_LW_START",
        },
    )
    assert got == "F10b_grounded_combat_adjacency"


def test_classify_player_row_moves_grounded_shine_contact_instance_tail_to_combat_owner() -> None:
    row = _player_row(
        seed_action_id=39,
        ref_action_id=360,
        out_action_id=360,
        prev_action_id=39,
        fields=("hitlag", "instance_id", "last_attack_landed", "state_flags[1]"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            39: "SQUAT",
            360: "FX_SPECIAL_LW_START",
        },
    )
    assert got == "F10b_grounded_combat_adjacency"


def test_classify_player_row_moves_missed_damage_from_shine_defender_to_damage_owner() -> None:
    row = _player_row(
        seed_action_id=363,
        ref_action_id=76,
        out_action_id=363,
        prev_action_id=363,
        fields=(
            "action_id",
            "animation_index",
            "hitlag",
            "hitstun",
            "instance_id",
            "on_ground",
            "state_flags[1]",
        ),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            76: "DAMAGE_HI_2",
            363: "FX_SPECIAL_LW_END",
        },
    )
    assert got == "F27b_damage_air_landing_action_callback_phase"


def test_classify_player_row_moves_pure_landingfallspecial_source_bookkeeping_to_combat() -> None:
    row = _player_row(
        seed_action_id=43,
        ref_action_id=43,
        out_action_id=43,
        prev_action_id=43,
        fields=("last_hit_by",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            43: "LANDING_FALL_SPECIAL",
        },
    )
    assert got == "F10b_grounded_combat_adjacency"


def test_classify_player_row_moves_landingfallspecial_collision_bundle_to_landing_owner() -> None:
    row = _player_row(
        seed_action_id=236,
        ref_action_id=43,
        out_action_id=236,
        prev_action_id=236,
        fields=("action_id", "animation_index", "on_ground"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            43: "LANDING_FALL_SPECIAL",
            236: "ESCAPE_AIR",
        },
    )
    assert got == "F13a_common_fallspecial_landing"


def test_classify_player_row_moves_fallspecial_landing_bundle_to_landing_owner() -> None:
    row = _player_row(
        seed_action_id=35,
        ref_action_id=35,
        out_action_id=43,
        prev_action_id=35,
        fields=("action_id", "action_frame", "animation_index", "jumps_left", "on_ground"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            35: "FALL_SPECIAL",
            43: "LANDING_FALL_SPECIAL",
        },
    )
    assert got == "F13a_common_fallspecial_landing"


def test_classify_player_row_moves_pure_landingfallspecial_action_frame_to_landing_owner() -> None:
    row = _player_row(
        seed_action_id=43,
        ref_action_id=43,
        out_action_id=43,
        prev_action_id=35,
        fields=("action_frame",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            35: "FALL_SPECIAL",
            43: "LANDING_FALL_SPECIAL",
        },
    )
    assert got == "F13a_common_fallspecial_landing"


def test_classify_player_row_moves_landingfallspecial_damage_scalar_to_damage_owner() -> None:
    row = _player_row(
        seed_action_id=43,
        ref_action_id=80,
        out_action_id=43,
        prev_action_id=43,
        fields=("action_id", "animation_index", "hitlag", "hitstun", "state_flags[1]"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            43: "LANDING_FALL_SPECIAL",
            80: "DAMAGE_N_3",
        },
    )
    assert got == "F08d_damage_timer_scalar_residual"


def test_classify_player_row_moves_escapeair_damage_transition_to_damage_owner() -> None:
    row = _player_row(
        seed_action_id=84,
        ref_action_id=236,
        out_action_id=84,
        prev_action_id=84,
        fields=("action_id", "animation_index", "state_flags[1]"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            84: "DAMAGE_AIR_1",
            236: "ESCAPE_AIR",
        },
    )
    assert got == "F08d_damage_timer_scalar_residual"


def test_classify_player_row_moves_landingfallspecial_ground_id_tail_to_mpcoll() -> None:
    row = _player_row(
        seed_action_id=43,
        ref_action_id=43,
        out_action_id=43,
        prev_action_id=43,
        fields=("ground_id",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            43: "LANDING_FALL_SPECIAL",
        },
    )
    assert got == "F10m_floor_line_identity"


def test_classify_player_row_moves_landing_hurtbox_tail_to_visible_state_owner() -> None:
    row = _player_row(
        seed_action_id=70,
        ref_action_id=70,
        out_action_id=70,
        prev_action_id=27,
        fields=("hurtbox_state",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            27: "JUMP_AERIAL_F",
            70: "LANDING_AIR_N",
        },
    )
    assert got == "F10d_hurtbox_stateflag_adjacency"


def test_classify_player_row_moves_landing_source_tail_to_combat_bookkeeping() -> None:
    row = _player_row(
        seed_action_id=70,
        ref_action_id=70,
        out_action_id=70,
        prev_action_id=27,
        fields=("last_hit_by",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            27: "JUMP_AERIAL_F",
            70: "LANDING_AIR_N",
        },
    )
    assert got == "F10b_grounded_combat_adjacency"


def test_classify_player_row_moves_ottotto_action_tail_to_grounded_selector() -> None:
    row = _player_row(
        seed_action_id=245,
        ref_action_id=18,
        out_action_id=245,
        prev_action_id=245,
        fields=("action_id", "animation_index", "instance_id"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            18: "TURN",
            245: "OTTOTTO",
        },
    )
    assert got == "F10a_grounded_selector_transition"


def test_classify_player_row_keeps_ottotto_edge_handoff_in_collision_owner() -> None:
    row = _player_row(
        seed_action_id=14,
        ref_action_id=245,
        out_action_id=29,
        prev_action_id=14,
        fields=("action_id", "animation_index", "jumps_left", "on_ground"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            14: "WAIT",
            29: "FALL",
            245: "OTTOTTO",
        },
    )
    assert got == "F10o_ottotto_teeter_edge_handoff"


def test_classify_player_row_splits_common_fall_landing_phase_to_timebase_owner() -> None:
    row = _player_row(
        seed_action_id=29,
        ref_action_id=42,
        out_action_id=29,
        prev_action_id=29,
        fields=("action_id", "action_frame", "animation_index", "instance_id", "jumps_left", "on_ground"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            29: "FALL",
            42: "LANDING",
        },
    )
    assert got == "F10n_common_fall_landing_timebase"


def test_classify_player_row_splits_pure_floor_line_identity_from_cliff_and_damage() -> None:
    cliff = _player_row(
        seed_action_id=257,
        ref_action_id=257,
        out_action_id=257,
        prev_action_id=257,
        fields=("ground_id",),
        on_ground=1,
    )
    assert (
        _classify_player_row(
            cliff,
            {
                257: "CLIFF_ATTACK_QUICK",
            },
        )
        == "F10m_floor_line_identity"
    )

    damage = _player_row(
        seed_action_id=86,
        ref_action_id=86,
        out_action_id=86,
        prev_action_id=86,
        fields=("ground_id",),
        on_ground=0,
    )
    assert _classify_player_row(damage, {86: "DAMAGE_AIR_3"}) == "F10m_floor_line_identity"


def test_classify_player_row_moves_cliff_hurtbox_tail_to_visible_state_owner() -> None:
    row = _player_row(
        seed_action_id=257,
        ref_action_id=257,
        out_action_id=257,
        prev_action_id=253,
        fields=("hurtbox_state",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            253: "CLIFF_WAIT",
            257: "CLIFF_ATTACK_QUICK",
        },
    )
    assert got == "F10d_hurtbox_stateflag_adjacency"


def test_classify_player_row_moves_passivewall_hurtbox_tail_to_visible_state_owner() -> None:
    row = _player_row(
        seed_action_id=203,
        ref_action_id=203,
        out_action_id=203,
        prev_action_id=203,
        fields=("hurtbox_state",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            203: "PASSIVE_WALL_JUMP",
        },
    )
    assert got == "F09a_aerial_stateflag_hurtbox_adjacency"


def test_classify_player_row_moves_escapeair_stateflag_tail_to_aerial_stateflag() -> None:
    row = _player_row(
        seed_action_id=236,
        ref_action_id=35,
        out_action_id=35,
        prev_action_id=236,
        fields=("state_flags[1]",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            35: "FALL_SPECIAL",
            236: "ESCAPE_AIR",
        },
    )
    assert got == "F09a_aerial_stateflag_hurtbox_adjacency"


def test_classify_player_row_splits_specialn_blaster_owner() -> None:
    row = _player_row(
        seed_action_id=345,
        ref_action_id=42,
        out_action_id=346,
        prev_action_id=345,
        fields=("action_id", "animation_index", "on_ground"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            42: "LANDING",
            345: "FX_SPECIAL_AIR_N_LOOP",
            346: "FX_SPECIAL_AIR_N_END",
        },
    )
    assert got == "F19_specialn_blaster_article"


def test_classify_player_row_moves_pure_specialairn_stateflag_to_aerial_stateflag_owner() -> None:
    row = _player_row(
        seed_action_id=345,
        ref_action_id=345,
        out_action_id=345,
        prev_action_id=344,
        fields=("state_flags[1]",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            344: "FX_SPECIAL_AIR_N_START",
            345: "FX_SPECIAL_AIR_N_LOOP",
        },
    )
    assert got == "F09a_aerial_stateflag_hurtbox_adjacency"


def test_classify_player_row_moves_pure_specialairn_hurtbox_to_aerial_stateflag_owner() -> None:
    row = _player_row(
        seed_action_id=345,
        ref_action_id=345,
        out_action_id=345,
        prev_action_id=345,
        fields=("hurtbox_state",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            345: "FX_SPECIAL_AIR_N_LOOP",
        },
    )
    assert got == "F09a_aerial_stateflag_hurtbox_adjacency"


def test_classify_player_row_moves_pure_specialn_source_to_combat_bookkeeping() -> None:
    row = _player_row(
        seed_action_id=342,
        ref_action_id=342,
        out_action_id=342,
        prev_action_id=342,
        fields=("combo_count", "last_attack_landed"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            342: "FX_SPECIAL_N_LOOP",
        },
    )
    assert got == "F10b_grounded_combat_adjacency"


def test_classify_player_row_moves_prev_specialn_landing_ottotto_tail_to_mpcoll() -> None:
    row = _player_row(
        seed_action_id=42,
        ref_action_id=245,
        out_action_id=29,
        prev_action_id=345,
        fields=("action_id", "animation_index", "jumps_left", "on_ground"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            29: "FALL",
            42: "LANDING",
            245: "OTTOTTO",
            345: "FX_SPECIAL_AIR_N_LOOP",
        },
    )
    assert got == "F10o_ottotto_teeter_edge_handoff"


def test_classify_player_row_moves_prev_speciallw_grounded_source_to_combat_bookkeeping() -> None:
    row = _player_row(
        seed_action_id=24,
        ref_action_id=24,
        out_action_id=24,
        prev_action_id=361,
        fields=("last_hit_by",),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            24: "KNEE_BEND",
            361: "FX_SPECIAL_LW_LOOP",
        },
    )
    assert got == "F10b_grounded_combat_adjacency"


def test_classify_player_row_keeps_active_specialn_landing_handoff_in_specialn() -> None:
    row = _player_row(
        seed_action_id=345,
        ref_action_id=42,
        out_action_id=346,
        prev_action_id=345,
        fields=("action_id", "animation_index", "instance_id", "jumps_left", "on_ground"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            42: "LANDING",
            345: "FX_SPECIAL_AIR_N_LOOP",
            346: "FX_SPECIAL_AIR_N_END",
        },
    )
    assert got == "F19_specialn_blaster_article"


def test_classify_player_row_moves_specialairn_damage_scalar_to_damage_owner() -> None:
    row = _player_row(
        seed_action_id=345,
        ref_action_id=75,
        out_action_id=75,
        prev_action_id=345,
        fields=("facing", "hitlag"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            75: "DAMAGE_HI_1",
            345: "FX_SPECIAL_AIR_N_LOOP",
        },
    )
    assert got == "F08d_damage_timer_scalar_residual"


def test_classify_player_row_splits_shine_air_ground_owner() -> None:
    row = _player_row(
        seed_action_id=366,
        ref_action_id=361,
        out_action_id=366,
        prev_action_id=365,
        fields=("action_id", "animation_index", "jumps_left", "on_ground"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            361: "FX_SPECIAL_LW_LOOP",
            365: "FX_SPECIAL_AIR_LW_START",
            366: "FX_SPECIAL_AIR_LW_LOOP",
        },
    )
    assert got == "F20_speciallw_shine_reflector"


def test_classify_player_row_splits_firefox_from_common_fallspecial() -> None:
    firefox = _player_row(
        seed_action_id=358,
        ref_action_id=357,
        out_action_id=357,
        prev_action_id=358,
        fields=("jumps_left",),
        on_ground=0,
    )
    assert (
        _classify_player_row(
            firefox,
            {
                357: "FX_SPECIAL_HI_LANDING",
                358: "FX_SPECIAL_HI_FALL",
            },
        )
        == "F22_specialhi_firefox_firebird"
    )

    common = _player_row(
        seed_action_id=236,
        ref_action_id=43,
        out_action_id=236,
        prev_action_id=25,
        fields=("action_id", "animation_index", "on_ground"),
        on_ground=0,
    )
    assert (
        _classify_player_row(
            common,
            {
                25: "JUMP_F",
                43: "LANDING_FALL_SPECIAL",
                236: "ESCAPE_AIR",
            },
        )
        == "F13a_common_fallspecial_landing"
    )


def test_classify_player_row_moves_firefox_bound_collision_timing_to_collision_owner() -> None:
    row = _player_row(
        seed_action_id=356,
        ref_action_id=359,
        out_action_id=356,
        prev_action_id=356,
        fields=("action_id", "action_frame", "animation_index"),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            356: "FX_SPECIAL_AIR_HI",
            359: "FX_SPECIAL_HI_BOUND",
        },
    )
    assert got == "F10p_specialhi_bound_collision_callback"


def test_classify_player_row_moves_steady_bound_jump_bookkeeping_to_aerial_state_owner() -> None:
    row = _player_row(
        seed_action_id=359,
        ref_action_id=359,
        out_action_id=359,
        prev_action_id=359,
        fields=("jumps_left",),
        on_ground=0,
    )
    got = _classify_player_row(row, {359: "FX_SPECIAL_HI_BOUND"})
    assert got == "F09a_aerial_stateflag_hurtbox_adjacency"


def test_classify_player_row_moves_grounded_kneebend_specialhihold_tail_to_selector_owner() -> None:
    row = _player_row(
        seed_action_id=24,
        ref_action_id=353,
        out_action_id=24,
        prev_action_id=84,
        fields=("action_id", "animation_index", "instance_id"),
        on_ground=1,
    )
    got = _classify_player_row(
        row,
        {
            24: "KNEE_BEND",
            84: "DAMAGE_AIR_1",
            353: "FX_SPECIAL_HI_HOLD",
        },
    )
    assert got == "F10a_grounded_selector_transition"


def test_classify_player_row_moves_direct_special_entry_instance_order_to_generic_owner() -> None:
    row = _player_row(
        seed_action_id=27,
        ref_action_id=344,
        out_action_id=344,
        prev_action_id=27,
        fields=("instance_id",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            27: "JUMP_AERIAL_F",
            344: "FX_SPECIAL_AIR_N_START",
        },
    )
    assert got == "F12b_adjacent_instance_counter_order"


def test_classify_player_row_keeps_specialn_loop_restart_instance_order_in_f24() -> None:
    row = _player_row(
        seed_action_id=345,
        ref_action_id=345,
        out_action_id=345,
        prev_action_id=345,
        seed_action_frame=11,
        ref_action_frame=0,
        out_action_frame=0,
        fields=("instance_id",),
        on_ground=0,
    )
    got = _classify_player_row(
        row,
        {
            345: "FX_SPECIAL_AIR_N_LOOP",
        },
    )
    assert got == "F24_special_adjacent_instance_order"


def test_classify_player_row_moves_pure_throw_score_to_grounded_combat_bookkeeping() -> None:
    row = _player_row(
        seed_action_id=222,
        ref_action_id=222,
        out_action_id=222,
        prev_action_id=222,
        fields=("combo_count",),
        on_ground=1,
    )
    got = _classify_player_row(row, {222: "THROW_LW"})
    assert got == "F10b_grounded_combat_adjacency"


def test_classify_player_row_moves_action_aligned_throw_contact_tail_to_grounded_combat() -> None:
    row = _player_row(
        seed_action_id=222,
        ref_action_id=222,
        out_action_id=222,
        prev_action_id=222,
        fields=("hitlag", "state_flags[1]"),
        on_ground=1,
    )
    got = _classify_player_row(row, {222: "THROW_LW"})
    assert got == "F10b_grounded_combat_adjacency"


def test_classify_player_row_moves_thrownlw_instance_hit_tail_to_grounded_combat() -> None:
    row = _player_row(
        seed_action_id=242,
        ref_action_id=242,
        out_action_id=242,
        prev_action_id=242,
        fields=("instance_hit_by", "state_flags[3]"),
        on_ground=1,
    )
    got = _classify_player_row(row, {242: "THROWN_LW"})
    assert got == "F10b_grounded_combat_adjacency"


def test_classify_player_row_splits_turn_hidden_microphase_from_grounded_selector() -> None:
    row = _player_row(
        seed_action_id=18,
        ref_action_id=20,
        out_action_id=18,
        prev_action_id=18,
        fields=("action_id", "action_frame", "animation_index", "facing", "instance_id"),
    )
    got = _classify_player_row(row, {18: "TURN", 20: "DASH"})
    assert got == "F10i_turn_hidden_microphase"


def test_classify_player_row_splits_turnrun_exit_microphase_from_grounded_selector() -> None:
    row = _player_row(
        seed_action_id=19,
        ref_action_id=39,
        out_action_id=21,
        prev_action_id=19,
        fields=("action_id", "action_frame", "animation_index", "instance_id"),
    )
    got = _classify_player_row(row, {19: "TURN_RUN", 21: "RUN", 39: "SQUAT"})
    assert got == "F10j_turnrun_exit_microphase"


def test_classify_player_row_splits_attack_instance_counter_from_grounded_instance() -> None:
    row = _player_row(
        seed_action_id=39,
        ref_action_id=57,
        out_action_id=57,
        prev_action_id=39,
        fields=("instance_id",),
    )
    got = _classify_player_row(row, {39: "SQUAT", 57: "ATTACK_LW3"})
    assert got == "F12c_attack_instance_counter_order"


def test_classify_player_row_splits_squat_escape_instance_counter_from_grounded_instance() -> None:
    row = _player_row(
        seed_action_id=234,
        ref_action_id=15,
        out_action_id=15,
        prev_action_id=234,
        fields=("instance_id",),
    )
    got = _classify_player_row(row, {15: "WALK_SLOW", 234: "ESCAPE_B"})
    assert got == "F12d_squat_escape_instance_counter_order"


def test_split_cross_player_instance_counter_rows_uses_peer_adjacent_family() -> None:
    local = _player_row(
        p=0,
        fields=("instance_id",),
        family_id="F12a_grounded_instance_counter_order",
        seed_action_id=20,
        ref_action_id=24,
        out_action_id=24,
    )
    peer = _player_row(
        p=1,
        fields=("instance_id",),
        family_id="F12b_adjacent_instance_counter_order",
        seed_action_id=70,
        ref_action_id=20,
        out_action_id=20,
    )
    events = [_event("F12a_grounded_instance_counter_order", "instance_id", subject="p0")]
    rows = {("d.msl", 10, 0): local, ("d.msl", 10, 1): peer}

    new_events, new_rows = _split_cross_player_instance_counter_rows(events, rows)

    assert new_rows[("d.msl", 10, 0)].family_id == "F12e_cross_player_instance_counter_order"
    assert new_events[0].family_id == "F12e_cross_player_instance_counter_order"


def test_classify_item_slot_row_moves_throw_laser_article_to_specialn_owner() -> None:
    row = ItemSlotRow(
        dataset="d.msl",
        record=10,
        slot=2,
        seed_frame=100,
        ref_frame=101,
        player_actions=(221, 241),
        ref_actions=(221, 90),
        out_actions=(221, 90),
        fields=("item_exists", "item_owner", "item_instance_id"),
        family_id="F99_misc_other",
        seed_item_type=54,
        ref_item_type=54,
        out_item_type=0,
    )
    got = _classify_item_slot_row(row, {90: "DAMAGE_FLY_TOP", 221: "THROW_HI", 241: "THROWN_HI"})
    assert got == "F19_specialn_blaster_article"


def test_classify_item_slot_row_moves_pure_blaster_gun_xda8_to_instance_owner() -> None:
    row = ItemSlotRow(
        dataset="BlondHardHippopotamus.msl",
        record=94,
        slot=0,
        seed_frame=-29,
        ref_frame=-28,
        player_actions=(25, 18),
        ref_actions=(344, 20),
        out_actions=(344, 20),
        fields=("item_instance_id",),
        family_id="F99_misc_other",
        seed_item_type=0,
        ref_item_type=74,
        out_item_type=74,
    )
    got = _classify_item_slot_row(row, {18: "WAIT", 20: "DASH", 25: "JUMP_F", 344: "FX_SPECIAL_AIR_N_START"})
    assert got == "F12b_adjacent_instance_counter_order"


def test_classify_item_slot_row_moves_guard_context_pure_blaster_gun_xda8_to_instance_owner() -> None:
    row = ItemSlotRow(
        dataset="AttachedGoodNaturedGuanaco.msl",
        record=124,
        slot=0,
        seed_frame=0,
        ref_frame=1,
        player_actions=(25, 179),
        ref_actions=(344, 234),
        out_actions=(344, 234),
        fields=("item_instance_id",),
        family_id="F99_misc_other",
        seed_item_type=0,
        ref_item_type=75,
        out_item_type=75,
    )
    got = _classify_item_slot_row(
        row,
        {
            25: "JUMP_F",
            179: "GUARD",
            234: "ESCAPE_B",
            344: "FX_SPECIAL_AIR_N_START",
        },
    )
    assert got == "F12b_adjacent_instance_counter_order"


def test_classify_item_slot_row_moves_rebirth_gun_spawn_fallout_to_match_flow() -> None:
    row = ItemSlotRow(
        dataset="TubbyCurlyHerring.msl",
        record=3062,
        slot=0,
        seed_frame=0,
        ref_frame=1,
        player_actions=(13, 25),
        ref_actions=(344, 25),
        out_actions=(13, 25),
        fields=("item_exists", "item_type", "item_owner", "item_state", "item_instance_id"),
        family_id="F99_misc_other",
        seed_item_type=0,
        ref_item_type=75,
        out_item_type=0,
    )
    got = _classify_item_slot_row(
        row,
        {
            13: "REBIRTH_WAIT",
            25: "JUMP_F",
            344: "FX_SPECIAL_AIR_N_START",
        },
    )
    assert got == "F04_match_flow_rebirth"


def test_classify_item_slot_row_keeps_dead_context_blaster_rows_in_item_identity() -> None:
    row = ItemSlotRow(
        dataset="d.msl",
        record=20,
        slot=0,
        seed_frame=0,
        ref_frame=1,
        player_actions=(0, 14),
        ref_actions=(0, 14),
        out_actions=(0, 14),
        fields=("item_exists", "item_type", "item_owner"),
        family_id="F99_misc_other",
        seed_item_type=75,
        ref_item_type=0,
        out_item_type=75,
    )
    got = _classify_item_slot_row(row, {0: "DEAD_DOWN", 14: "WAIT"})
    assert got == "F16b_blaster_article_identity"


def test_classify_item_slot_row_moves_false_aerial_blaster_entry_to_action_owner() -> None:
    row = ItemSlotRow(
        dataset="DistinctCaringCobra.msl",
        record=546,
        slot=0,
        seed_frame=0,
        ref_frame=1,
        player_actions=(69, 25),
        ref_actions=(69, 85),
        out_actions=(69, 85),
        fields=("item_exists", "item_type", "item_owner", "item_state", "item_instance_id"),
        family_id="F99_misc_other",
        seed_item_type=0,
        ref_item_type=0,
        out_item_type=75,
    )
    got = _classify_item_slot_row(
        row,
        {
            25: "JUMP_F",
            69: "ATTACK_AIR_B",
            85: "DAMAGE_AIR_2",
        },
    )
    assert got == "F09c_aerial_action_entry_adjacency"


def test_classify_item_slot_row_moves_specialn_landing_slot_echo_to_action_owner() -> None:
    for dataset, record, player_actions, ref_actions, out_actions, prev_actions, action_names in (
        (
            "HungryImportantSnake.msl",
            6603,
            (345, 25),
            (42, 25),
            (42, 25),
            (),
            {
                25: "JUMP_F",
                42: "LANDING",
                345: "FX_SPECIAL_AIR_N_LOOP",
            },
        ),
        (
            "PutridJoyousOryx.msl",
            2235,
            (345, 38),
            (42, 38),
            (42, 38),
            (),
            {
                38: "DAMAGE_FALL",
                42: "LANDING",
                345: "FX_SPECIAL_AIR_N_LOOP",
            },
        ),
        (
            "HungryImportantSnake.msl",
            6604,
            (42, 25),
            (42, 25),
            (42, 25),
            (345, 25),
            {
                25: "JUMP_F",
                42: "LANDING",
                345: "FX_SPECIAL_AIR_N_LOOP",
            },
        ),
    ):
        row = ItemSlotRow(
            dataset=dataset,
            record=record,
            slot=1,
            seed_frame=0,
            ref_frame=1,
            player_actions=player_actions,
            ref_actions=ref_actions,
            out_actions=out_actions,
            prev_actions=prev_actions,
            fields=("item_instance_id",),
            family_id="F99_misc_other",
            seed_item_type=54,
            ref_item_type=54,
            out_item_type=54,
        )
        got = _classify_item_slot_row(row, action_names)
        assert got == "F09c_aerial_action_entry_adjacency"


def test_classify_item_slot_row_keeps_non_specialn_laser_instance_echo_in_body_lifetime() -> None:
    row = ItemSlotRow(
        dataset="TreasuredBackKangaroo.msl",
        record=6197,
        slot=0,
        seed_frame=0,
        ref_frame=1,
        player_actions=(199, 14),
        ref_actions=(199, 14),
        out_actions=(199, 14),
        fields=("item_instance_id",),
        family_id="F99_misc_other",
        seed_item_type=54,
        ref_item_type=54,
        out_item_type=54,
    )
    got = _classify_item_slot_row(row, {14: "WAIT", 199: "PASSIVE"})
    assert got == "F16d_item_body_lifetime"


def test_classify_item_slot_row_moves_laser_player_damage_divergence_to_body_filter() -> None:
    row = ItemSlotRow(
        dataset="PositiveRevolvingHyena.msl",
        record=8137,
        slot=1,
        seed_frame=8014,
        ref_frame=8015,
        player_actions=(29, 24),
        ref_actions=(29, 25),
        out_actions=(84, 25),
        fields=("item_exists", "item_type", "item_owner", "item_instance_id"),
        family_id="F99_misc_other",
        seed_item_type=55,
        ref_item_type=55,
        out_item_type=0,
    )
    got = _classify_item_slot_row(row, {24: "KNEE_BEND", 25: "JUMP_F", 29: "FALL", 84: "DAMAGE_AIR_1"})
    assert got == "F28_body_contact_candidate_narrowphase_owner"


def test_classify_item_slot_row_moves_specialn_gun_identity_before_guard_owner() -> None:
    row = ItemSlotRow(
        dataset="MotionlessAggressiveJay.msl",
        record=5587,
        slot=1,
        seed_frame=5464,
        ref_frame=5465,
        player_actions=(345, 182),
        ref_actions=(345, 181),
        out_actions=(345, 182),
        fields=("item_exists", "item_type", "item_owner", "item_state", "item_instance_id"),
        family_id="F99_misc_other",
        seed_item_type=75,
        ref_item_type=0,
        out_item_type=75,
    )
    got = _classify_item_slot_row(
        row,
        {181: "GUARD_SET_OFF", 182: "GUARD_REFLECT", 345: "FX_SPECIAL_AIR_N_LOOP"},
    )
    assert got == "F19_specialn_blaster_article"


def test_classify_item_slot_row_moves_specialn_new_shot_identity_before_guard_owner() -> None:
    row = ItemSlotRow(
        dataset="MotionlessAggressiveJay.msl",
        record=6336,
        slot=1,
        seed_frame=6213,
        ref_frame=6214,
        player_actions=(345, 182),
        ref_actions=(345, 182),
        out_actions=(345, 182),
        fields=("item_owner", "item_instance_id"),
        family_id="F99_misc_other",
        seed_item_type=0,
        ref_item_type=55,
        out_item_type=55,
    )
    got = _classify_item_slot_row(
        row,
        {182: "GUARD_REFLECT", 345: "FX_SPECIAL_AIR_N_LOOP"},
    )
    assert got == "F19_specialn_blaster_article"


def test_classify_item_slot_row_moves_speciallw_laser_lifetime_to_shine_owner() -> None:
    row = ItemSlotRow(
        dataset="PriceyPartialAlbatross.msl",
        record=6414,
        slot=1,
        seed_frame=6291,
        ref_frame=6292,
        player_actions=(367, 24),
        ref_actions=(367, 24),
        out_actions=(367, 24),
        fields=("item_exists", "item_type", "item_owner", "item_instance_id"),
        family_id="F99_misc_other",
        seed_item_type=55,
        ref_item_type=0,
        out_item_type=55,
    )
    got = _classify_item_slot_row(row, {24: "KNEE_BEND", 367: "FX_SPECIAL_AIR_LW_HIT"})
    assert got == "F20_speciallw_shine_reflector"


def test_classify_item_slot_row_moves_guard_action_divergence_to_guard_owner() -> None:
    row = ItemSlotRow(
        dataset="MotionlessAggressiveJay.msl",
        record=702,
        slot=0,
        seed_frame=579,
        ref_frame=580,
        player_actions=(20, 182),
        ref_actions=(20, 182),
        out_actions=(20, 181),
        fields=("item_exists", "item_type", "item_owner", "item_instance_id"),
        family_id="F99_misc_other",
        seed_item_type=55,
        ref_item_type=55,
        out_item_type=0,
    )
    got = _classify_item_slot_row(
        row,
        {
            20: "DASH",
            181: "GUARD_SET_OFF",
            182: "GUARD_REFLECT",
        },
    )
    assert got == "F01_guard_release_collision"


def test_classify_item_slot_row_moves_same_guard_action_lifetime_to_guard_owner() -> None:
    row = ItemSlotRow(
        dataset="DistinctCaringCobra.msl",
        record=2905,
        slot=0,
        seed_frame=2782,
        ref_frame=2783,
        player_actions=(182, 20),
        ref_actions=(182, 20),
        out_actions=(182, 20),
        fields=("item_exists", "item_type", "item_owner", "item_instance_id"),
        family_id="F99_misc_other",
        seed_item_type=55,
        ref_item_type=0,
        out_item_type=55,
    )
    got = _classify_item_slot_row(row, {20: "DASH", 182: "GUARD_REFLECT"})
    assert got == "F01_guard_release_collision"


def test_build_summary_aggregates_family_counts_and_nearby_controls() -> None:
    player_rows = {
        ("d.msl", 9, 0): _player_row(record=9, seed_frame=99, ref_frame=100, fields=(), family_id="F99_misc_other"),
        ("d.msl", 10, 0): _player_row(),
        ("d.msl", 11, 0): _player_row(record=11, seed_frame=101, ref_frame=102, fields=(), family_id="F99_misc_other"),
    }
    item_rows = {}
    events = [
        _event("F01_guard_release_collision", "action_id", seed=178, ref=181, out=182),
        _event("F01_guard_release_collision", "instance_id", seed=100, ref=101, out=100),
        _event("F12_instance_id_transition_only", "instance_id", seed=200, ref=201, out=200),
    ]
    summary = build_summary(
        events,
        player_rows,
        item_rows,
        action_names={20: "DASH", 178: "GUARD_ON", 181: "GUARD_SET_OFF", 182: "GUARD_REFLECT"},
        top_n=5,
    )

    assert summary["total_mismatches"] == 3
    assert summary["family_count"] == 2
    assert summary["top_families"][0]["family_id"] == "F01_guard_release_collision"
    assert summary["top_families"][0]["count"] == 2
    assert summary["top_families"][0]["primary_fields"][0] == {"field": "action_id", "count": 1}
    assert summary["top_families"][0]["counterexample_rows"]
    assert summary["top_families"][0]["refs"] == list(FAMILY_META["F01_guard_release_collision"].refs)


def test_build_audit_summary_reports_precision_on_synthetic_rows() -> None:
    player_rows = {
        ("d.msl", 10, 0): _player_row(family_id="F01_guard_release_collision"),
        ("d.msl", 20, 0): _player_row(
            record=20,
            family_id="F99_misc_other",
            seed_action_id=50,
            ref_action_id=50,
            out_action_id=50,
            fields=("action_id",),
        ),
    }
    item_rows = {}
    summary, samples = build_audit_summary(
        player_rows,
        item_rows,
        action_names={20: "DASH", 50: "ATTACK_DASH", 178: "GUARD_ON", 181: "GUARD_SET_OFF", 182: "GUARD_REFLECT"},
        sample_n=25,
    )

    assert len(samples) == 2
    by_family = {row["family_id"]: row for row in summary["families"]}
    assert by_family["F01_guard_release_collision"]["precision_estimate"] == 1.0
    assert by_family["F99_misc_other"]["precision_estimate"] == 0.0
    assert by_family["F99_misc_other"]["mismatch_examples"]


def test_family_tsv_includes_action_frame_buckets(tmp_path) -> None:
    out = tmp_path / "family.tsv"
    _write_family_tsv(
        out,
        [
            _event(
                "F01_guard_release_collision",
                "action_id",
                seed=178,
                ref=181,
                out=182,
            )
        ],
        {20: "DASH", 178: "GUARD_ON", 181: "GUARD_SET_OFF", 182: "GUARD_REFLECT"},
    )
    text = out.read_text(encoding="utf-8")
    lines = text.splitlines()
    assert "seed_action_frame_bucket" in lines[0]
    cols = lines[1].split("\t")
    assert cols[18:24] == ["0", "0", "1", "1", "0", "0"]


def test_audit_samples_tsv_includes_reason_and_buckets(tmp_path) -> None:
    out = tmp_path / "audit.tsv"
    _write_audit_samples_tsv(
        out,
        [
            AuditSample(
                family_id="F01_guard_release_collision",
                dataset="d.msl",
                record=10,
                subject="p0",
                matched=True,
                audit_reason="guard family with collision/identity field bundle",
                field_bundle=("action_id", "instance_id"),
                seed_action_id=178,
                ref_action_id=181,
                out_action_id=182,
                seed_action_frame_bucket="<0",
                ref_action_frame_bucket="1",
                out_action_frame_bucket="0",
            )
        ],
        {178: "GUARD_ON", 181: "GUARD_SET_OFF", 182: "GUARD_REFLECT"},
    )
    text = out.read_text(encoding="utf-8")
    assert "audit_reason" in text.splitlines()[0]
    assert "guard family with collision/identity field bundle" in text
    assert "<0" in text
