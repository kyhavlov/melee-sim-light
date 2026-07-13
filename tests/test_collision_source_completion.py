from __future__ import annotations

import re
from pathlib import Path

import numpy as np
import pytest

from test_char_common_action_coverage import _mk_inputs, _run, _seed_base
from tools.extraction.known_data_artifacts import read_mslstg01_v7


ROOT = Path(__file__).resolve().parents[1]


def test_capture_hi_floor_contact_runs_installed_hi_to_lw_continuation() -> None:
    seed = _seed_base("fox")
    victim = 1
    seed["grab_owner_port"][0, victim] = np.uint8(0)
    seed["action_id"][0, victim] = np.uint16(0x00DF)  # CapturePulledHi
    seed["animation_index"][0, victim] = np.uint32(251)  # CapturePulledHi submotion
    seed["anim_frame_f32"][0, victim] = np.float32(2.0)
    seed["pos_x"][0, victim] = np.float32(5.0)
    seed["pos_y"][0, victim] = np.float32(0.0)
    seed["on_ground"][0, victim] = np.uint8(0)
    seed["ground_id"][0, victim] = np.uint16(0)
    seed["floor_sweep_prev_pos_x_f32"][0, victim] = np.float32(5.0)
    seed["floor_sweep_prev_pos_y_f32"][0, victim] = np.float32(-0.05)
    seed["floor_sweep_prev_pos_valid_u8"][0, victim] = np.uint8(1)

    out = _run(seed, [_mk_inputs()])[0]

    # ftCo_CapturePulledHi_Coll -> ft_80083C00 -> fn_800DAEEC.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CapturePulledHi_Coll,fn_800DAEEC}
    assert int(out["action_id"][victim]) == 0x00E2  # CapturePulledLw
    assert int(out["on_ground"][victim]) == 1


def test_active_fighter_floor_projection_crosses_stadium_lip_to_main_floor() -> None:
    stage = read_mslstg01_v7(ROOT / "data/stages/bin/grps.bin")
    hard_floors = [
        seg
        for seg in stage.segments
        if int(seg.kind_id) == 0 and bool(seg.fighter_solid) and not (int(seg.flags) & 1)
    ]
    main = max(hard_floors, key=lambda seg: abs(float(seg.x1) - float(seg.x0)))
    right = (float(main.x1), float(main.y1))
    right_lip = min(
        (
            seg
            for seg in hard_floors
            if int(seg.line_id) != int(main.line_id)
            and ((float(seg.x0), float(seg.y0)) == right or (float(seg.x1), float(seg.y1)) == right)
        ),
        key=lambda seg: abs(float(seg.x1) - float(seg.x0)),
    )

    seed = _seed_base("fox")
    seed["stage_id"][0] = np.uint32(3)  # frozen Pokemon Stadium
    seed["action_id"][0, 0] = np.uint16(0x002B)  # LandingFallSpecial
    seed["animation_index"][0, 0] = np.uint32(36)
    seed["anim_frame_f32"][0, 0] = np.float32(5.0)
    seed["pos_x"][0, 0] = np.float32(right[0] + 0.5)
    seed["pos_y"][0, 0] = np.float32(right[1] + 0.0001)
    seed["facing"][0, 0] = np.uint8(0)
    seed["facing_dir1"][0, 0] = np.int8(-1)
    seed["speed_ground_x_self"][0, 0] = np.float32(-1.0)
    seed["speed_air_x_self"][0, 0] = np.float32(-1.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["ground_id"][0, 0] = np.uint16(right_lip.line_id)

    out = _run(seed, [_mk_inputs()])[0]

    # Frozen Stadium's generated active floor graph connects the lip to the main floor even though
    # the raw transformation topology has another primary link.
    # data/stages/bin/grps.bin::MSLSTG01 fighter_solid + floor endpoint links
    assert int(out["action_id"][0]) == 0x002B
    assert int(out["ground_id"][0]) == int(main.line_id)
    assert int(out["on_ground"][0]) == 1


def _read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_source_collision_cutover_has_no_legacy_coordinator_or_reject_taxonomy() -> None:
    deleted = (
        "src/mpcoll_ground.c",
        "src/mpcoll_ground.h",
        "src/mpcoll_floor.c",
        "src/mpcoll_floor.h",
        "src/mpcoll_floor_callbacks.c",
        "src/mpcoll_wall_ceil.c",
        "src/mpcoll_wall_ceil.h",
    )
    for path in deleted:
        assert not (ROOT / path).exists(), path

    sources = "\n".join(
        _read(path)
        for path in (
            "src/mp_coll.c",
            "src/mp_lib.c",
            "src/mpcoll_source_ground.c",
            "src/mpcoll_source_air.c",
        )
    )
    assert "MSL_MPCOLL_REJECT_" not in sources
    setup = _read("setup.py")
    for path in ("mp_coll.c", "mp_lib.c", "mpcoll_source_ground.c", "mpcoll_source_air.c"):
        assert path in setup


def _line_y(seg: dict, x: float) -> float:
    dx = float(seg["x1"]) - float(seg["x0"])
    if abs(dx) < 1e-7:
        return float(seg["y0"])
    t = (x - float(seg["x0"])) / dx
    return float(seg["y0"]) + t * (float(seg["y1"]) - float(seg["y0"]))


def test_stage_item_line_hit_point_shares_item_collision_semantics() -> None:
    # The hit-point helper is the same item stage-line owner as the bool helper used by lasers and
    # Sheik Needles. Cover endpoint, sloped floor, wall, ceiling, and the horizontal-floor direction
    # rejection so it cannot drift into an independent raw-intersection predicate.
    # refs/melee/src/melee/it/itgroundcoll.c::it_8026E9A4
    import msl_binding

    endpoint = msl_binding.stage_item_line_hit(31, -60.0, 5.0, -60.0, -5.0)
    assert endpoint == pytest.approx({"x": -60.0, "y": 0.0}, abs=1e-5)

    horizontal_floor_upward_miss = msl_binding.stage_item_line_hit(31, -64.0, -5.0, -64.0, 5.0)
    assert horizontal_floor_upward_miss is None

    sloped = msl_binding.stage_floor_segment(8, 2)
    sx = (float(sloped["x0"]) + float(sloped["x1"])) * 0.5
    sy = _line_y(sloped, sx)
    sloped_hit = msl_binding.stage_item_line_hit(8, sx, sy + 6.0, sx, sy - 6.0)
    assert sloped_hit == pytest.approx({"x": sx, "y": sy}, abs=1e-5)

    wall = msl_binding.stage_left_wall_segment(28, 9)
    wy = (float(wall["y0"]) + float(wall["y1"])) * 0.5
    wx = _line_y({"x0": wall["y0"], "y0": wall["x0"], "x1": wall["y1"], "y1": wall["x1"]}, wy)
    wall_hit = msl_binding.stage_item_line_hit(28, wx - 8.0, wy, wx + 8.0, wy)
    assert wall_hit == pytest.approx({"x": wx, "y": wy}, abs=1e-5)

    ceiling = msl_binding.stage_ceiling_segment(28, 6)
    cy = float(ceiling["y0"])
    ceiling_hit = msl_binding.stage_item_line_hit(28, 0.0, cy - 8.0, 0.0, cy + 8.0)
    assert ceiling_hit == pytest.approx({"x": 0.0, "y": cy}, abs=1e-5)

    adjacent_miss = msl_binding.stage_item_line_hit(28, 0.0, cy + 8.0, 0.0, cy + 16.0)
    assert adjacent_miss is None


def test_collision_closed_doc_has_no_unfinished_rows() -> None:
    doc = _read("agent_docs/systems/collision.md")
    progress = _read("agent_docs/systems/PROGRESS.md")

    assert "| Collision | CLOSED | [collision.md](collision.md) |" in progress
    for token in ("WRONG/NEEDS WORK", "| BLOCKED |", "TODO"):
        assert token not in doc
    assert "trace111" in doc
    assert "FallSpecial destination collision" in doc


def test_mpcoll_source_comments_do_not_claim_retained_approximation_debt() -> None:
    audited_sources = (
        "src/mp_coll.c",
        "src/mp_lib.c",
        "src/mpcoll_source_ground.c",
        "src/mpcoll_source_air.c",
        "src/mpcoll_env.c",
        "src/locomotion.c",
        "src/fighter_callbacks.c",
        "src/damage_terminal_owner.h",
        "src/motion_state_owners.h",
        "src/stage_collision.c",
    )
    text = "\n".join(_read(path) for path in audited_sources)
    edited_collision_stage_comments = "\n".join(
        (_read("src/fighter_callbacks.c"), _read("src/state.h"))
    )
    stale_collision_stage_phrases = (
        "does not yet implement mpColl substeps",
        "does not yet substep collision",
        "Approximate the collision-stage",
        "We approximate a single collision",
    )
    for phrase in stale_collision_stage_phrases:
        assert phrase not in edited_collision_stage_comments, phrase

def test_cliff_floor_handoff_has_no_stage_or_dataset_special_case() -> None:
    source = _read("src/mpcoll_source_air.c") + _read("src/ledge.c")
    for token in ("dataset", ".slp", ".slpz", "record_id", "replay row"):
        assert token not in source


def test_phase6_moving_surface_owner_is_packet_driven_not_replay_or_stage_shortcut() -> None:
    stage_c = _read("src/stage_collision.c")
    moving_owner = stage_c[
        stage_c.index("uint8_t stage_collision_floor_line_moving_surface_state"):
        stage_c.index("const MslStageCeilingGraph* stage_collision_get_ceiling_graph")
    ]

    assert "stage_collision_floor_line_moving_surface_state_impl(batch, bi, line, 0u, &surface)" in moving_owner
    assert "stage_collision_floor_line_moving_surface_state_impl(batch, bi, line, 1u, out)" in moving_owner
    assert "stage_collision_platform_path_world_line" in moving_owner
    assert "stage_collision_fod_height_platform_line_state" in moving_owner
    assert "stage_fod_platform_velocity" in moving_owner

    for token in (
        "trace",
        "dataset",
        ".slpz",
        ".slp",
        "replay record",
        "hardcoded record",
        "stage_id ==",
        "stage_id !=",
        "MSL_STAGE_ID_FOUNTAIN_OF_DREAMS",
        "MSL_STAGE_ID_YOSHIS_STORY",
        "ledge band",
        "source band",
    ):
        assert token not in moving_owner, token

    static_floor_filter = stage_c[
        stage_c.index("static inline uint8_t stage_static_floor_line_query_active"):
        stage_c.index("static inline uint8_t stage_static_line_query_active")
    ]
    assert "MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT" not in static_floor_filter
    assert "MSL_STAGE_PLATFORM_TRANSFORM_RANDALL" not in static_floor_filter
    assert "MSL_STAGE_PLATFORM_TRANSFORM_STATIC_Y" in static_floor_filter


def test_phase2_mpcoll_substrate_is_not_routed_through_item_or_special_only_sources() -> None:
    # Phase 2 owns common CollData/mpColl substrate, not item-only collision routing or bespoke
    # special-action callbacks. Keep this as a source-scope guard so future helper plumbing cannot
    # satisfy the Phase 2 tests by entering item/special owners.
    item_sources = "\n".join(
        _read(path)
        for path in (
            "src/items.c",
            "src/item_reflect.h",
            "src/item_common_params.c",
            "src/item_article_params.c",
            "src/stage_item_params.c",
        )
    )
    for token in (
        "mpcoll_colldata_copy",
        "mpcoll_check_bounding",
        "mpcoll_end_static_events",
        "stage_collision_static_query",
        "debug_copy_colldata",
    ):
        assert token not in item_sources, token

    special_only_sources = "\n".join(
        _read(path)
        for path in (
            "src/shine.c",
            "src/specialhi_pose.h",
            "src/special_msids.c",
        )
    )
    for token in (
        "mpcoll_colldata_copy",
        "mpcoll_check_bounding",
        "mpcoll_end_static_events",
        "stage_collision_static_query",
        "debug_copy_colldata",
    ):
        assert token not in special_only_sources, token


def test_specialhi_jobj_ecb_owner_excludes_unrotated_followups() -> None:
    ecb_pose_c = _read("src/mpcoll_ecb_pose.c")
    helper_match = re.search(
        r"uint8_t mpcoll_ground_specialhi_uses_jobj_ecb"
        r"\([^)]*\) \{(?P<body>.*?)\n\}",
        ecb_pose_c,
        flags=re.DOTALL,
    )
    assert helper_match is not None
    helper_body = helper_match.group("body")

    # Only the launch states write and consume XRotN. Landing/Fall/Bound enter with motion-change
    # flags=0 and load their own unrotated animation pose.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFox_SpecialHi_RotateModel,ftFx_SpecialHiFall_AirToGround,
    #   ftFx_SpecialHiLanding_GroundToAir,ftFx_SpecialHiBound_Enter}
    for kind in ("MSL_FX_KIND_SPECIAL_HI", "MSL_FX_KIND_SPECIAL_AIR_HI"):
        assert kind in helper_body
    for kind in (
        "MSL_FX_KIND_SPECIAL_HI_LANDING",
        "MSL_FX_KIND_SPECIAL_HI_FALL",
        "MSL_FX_KIND_SPECIAL_HI_BOUND",
    ):
        assert kind not in helper_body
    assert "mpColl_LoadECB_JObj" in helper_body
