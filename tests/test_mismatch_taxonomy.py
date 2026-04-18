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
    assert got == "F06_damageflyroll_rng_gate"


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


def test_classify_player_row_splits_damagefly_contact_timing_to_mpcoll_residual() -> None:
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
    assert got == "F17_mpcoll_ledge_ecb_residual"


def test_classify_player_row_keeps_passivewall_contact_timing_in_mpcoll_residual() -> None:
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
    assert got == "F17_mpcoll_ledge_ecb_residual"


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
        == "F20_speciallw_shine_reflector"
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
        == "F08c_damage_state_transition_adjacency"
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
        == "F08f_body_contact_candidate_filter_residual"
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
        == "F08f_body_contact_candidate_filter_residual"
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
    assert got == "F08c_damage_state_transition_adjacency"


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
    assert got == "F08c_damage_state_transition_adjacency"


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


def test_classify_player_row_splits_landingfallspecial_special_entry_dispatch() -> None:
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
    assert got == "F23_special_common_entry_dispatch"


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


def test_classify_player_row_splits_special_instance_order() -> None:
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
    assert got == "F24_special_adjacent_instance_order"


def test_classify_player_row_splits_per_throw_pulse_from_misc() -> None:
    row = _player_row(
        seed_action_id=222,
        ref_action_id=222,
        out_action_id=222,
        prev_action_id=222,
        fields=("combo_count",),
        on_ground=1,
    )
    got = _classify_player_row(row, {222: "THROW_LW"})
    assert got == "F14b_per_throw_pulse_bookkeeping"


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


def test_classify_item_slot_row_detects_throwhi_item_lane() -> None:
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
    )
    got = _classify_item_slot_row(row, {90: "DAMAGE_FLY_TOP", 221: "THROW_HI", 241: "THROWN_HI"})
    assert got == "F14_throw_item_bookkeeping"


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
