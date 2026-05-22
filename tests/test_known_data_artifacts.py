from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
from pathlib import Path

import pytest

from tools.extraction.extract_fighter_parts import ANCHOR_IDS
from tools.extraction.extract_fighter_script_timeline import EVENT_IDS, RUNTIME_OWNER_EVENT_KINDS
from tools.extraction.extract_item_articles import FIELD_SPECS, UNIT_DEGREES, UNIT_FRAMES, UNIT_ITEM_KIND, UNIT_PART_ID
from tools.slippi.item_article_data import (
    SIM_CHAR_TO_GALE01_FIGHTER_KIND,
    item_article_kind_set,
    item_article_values_by_sim_char,
)
from tools.slippi.known_data_artifacts import (
    DREAM_WHISPY_MAGIC,
    DREAM_WHISPY_VERSION,
    ITEM_ARTICLE_CHAR_DOMAIN_GALE01_FIGHTER_KIND,
    ITEM_ARTICLE_MAGIC,
    ITEM_ARTICLE_VALUE_F32,
    ITEM_ARTICLE_VALUE_U16,
    ITEM_ARTICLE_VALUE_U32,
    ITEM_ARTICLE_VERSION,
    PART_MAGIC,
    PART_VERSION,
    SCRIPT_LEGACY_JSON_VERSION,
    SCRIPT_MAGIC,
    SCRIPT_VERSION,
    STAGE_MAGIC,
    STAGE_ITEM_OBJECT_MAGIC,
    STAGE_ITEM_OBJECT_VERSION,
    STAGE_OBJECT_SUPPORT_KIND_YOSHI_SHYGUY,
    STAGE_PLATFORM_MOTION_KIND_FOD,
    STAGE_VERSION,
    read_mslftsc1_v1,
    read_mslwhsp1,
    read_mslitar1,
    read_mslpart1_v1,
    read_mslstg01_v7,
    read_mslstio1_yoshi_shyguy,
    stage_metadata_bin_name_for_stage_id,
    stage_metadata_path_for_stage_id,
    dream_whispy_metadata,
    yoshi_shyguy_metadata,
)
from tools.slippi.make_dataset_from_slp import _load_stage_segments_for_seed, _stage_ledge_floor_ids

SUPPORTED_STAGE_BINS = ("griz.bin", "grps.bin", "grst.bin", "grop.bin", "grnba.bin", "grnla.bin")
SUPPORTED_STAGE_IDS_BY_BIN = {
    "griz.bin": 2,
    "grps.bin": 3,
    "grst.bin": 8,
    "grop.bin": 28,
    "grnba.bin": 31,
    "grnla.bin": 32,
}


def _symlink_data_tree_with_private_dirs(tmp_path: Path, private_dirs: tuple[str, ...]) -> Path:
    data_dir = tmp_path / "data"
    data_dir.mkdir()
    root_data = Path("data").resolve()
    private = set(private_dirs)
    for child in root_data.iterdir():
        if child.name in private:
            continue
        (data_dir / child.name).symlink_to(child, target_is_directory=child.is_dir())
    for name in private:
        (data_dir / name).mkdir(parents=True, exist_ok=True)
    return data_dir


def _copy_stage_bins(data_dir: Path, *, corrupt: str | None = None) -> None:
    root_data = Path("data").resolve()
    (data_dir / "stages" / "bin").mkdir(parents=True, exist_ok=True)
    for name in SUPPORTED_STAGE_BINS:
        buf = bytearray((root_data / "stages" / "bin" / name).read_bytes())
        if name == corrupt:
            buf[8:12] = (STAGE_VERSION - 1).to_bytes(4, "little")
        (data_dir / "stages" / "bin" / name).write_bytes(bytes(buf))


@pytest.mark.integration
def test_stage_metadata_registry_covers_supported_domain() -> None:
    assert stage_metadata_bin_name_for_stage_id(2) == "griz.bin"
    assert stage_metadata_bin_name_for_stage_id(3) == "grps.bin"
    assert stage_metadata_bin_name_for_stage_id(8) == "grst.bin"
    assert stage_metadata_bin_name_for_stage_id(28) == "grop.bin"
    assert stage_metadata_bin_name_for_stage_id(31) == "grnba.bin"
    assert stage_metadata_bin_name_for_stage_id(32) == "grnla.bin"
    assert stage_metadata_bin_name_for_stage_id(4) is None
    assert stage_metadata_path_for_stage_id(2, Path("data")) == Path("data/stages/bin/griz.bin")
    assert stage_metadata_path_for_stage_id(3, Path("data")) == Path("data/stages/bin/grps.bin")
    assert stage_metadata_path_for_stage_id(8, Path("data")) == Path("data/stages/bin/grst.bin")
    assert stage_metadata_path_for_stage_id(28, Path("data")) == Path("data/stages/bin/grop.bin")
    assert stage_metadata_path_for_stage_id(31, Path("data")) == Path("data/stages/bin/grnba.bin")
    assert stage_metadata_path_for_stage_id(32, Path("data")) == Path("data/stages/bin/grnla.bin")
    assert stage_metadata_path_for_stage_id(4, Path("data")) is None


@pytest.mark.integration
def test_stage_ledge_floor_seed_ids_match_mslstg01_supported_legal_stages() -> None:
    # The teacher-forced cliff_ledge_floor_segment_id_u16 seed lane depends on the same generated
    # MSLSTG01 ledge floor ids that runtime CliffCatch/CliffWait ownership consumes. Lock all
    # supported legal stages so left/right derivation cannot drift from extracted stage metadata.
    expected = {
        2: (3, 7),  # Fountain of Dreams
        3: (51, 54),  # frozen Pokemon Stadium; excludes the elevated transformation ledge row
        8: (2, 6),  # Yoshi's Story
        28: (3, 5),  # Dream Land N64
        31: (0, 5),  # Battlefield
        32: (0, 2),  # Final Destination
    }
    for stage_id, expected_ids in expected.items():
        left_id, right_id = _stage_ledge_floor_ids(stage_id=stage_id, data_root=Path("data"))
        assert (left_id, right_id) == expected_ids

        stage_path = stage_metadata_path_for_stage_id(stage_id, Path("data"))
        assert stage_path is not None
        stage = read_mslstg01_v7(stage_path)
        ledge_floors = [
            seg for seg in stage.segments if int(seg.kind_id) == 0 and (int(seg.flags) & 2)
        ]
        assert left_id == min(ledge_floors, key=lambda seg: float(seg.x0)).line_id
        assert right_id == max(ledge_floors, key=lambda seg: float(seg.x1)).line_id


@pytest.mark.integration
def test_stage_metadata_fd_known_rows() -> None:
    stage = read_mslstg01_v7(Path("data/stages/bin/grnla.bin"))
    assert stage.segment_count == 16
    assert stage.stage_point_count == 22
    assert stage.spawn_count == 4
    assert stage.respawn_count == 4
    assert stage.cam_bounds_world == pytest.approx((-170.0, 170.0, 114.0, -80.0))
    assert stage.blast_bounds_world == pytest.approx((-246.0, 246.0, 188.0, -140.0))
    assert [(p.x, p.y) for p in stage.spawn_points] == pytest.approx(
        [(-60.0, 10.0), (60.0, 10.0), (-20.0, 10.0), (20.0, 10.0)]
    )
    assert [(p.x, p.y) for p in stage.respawn_points] == pytest.approx(
        [(16.0, 45.0), (-50.0, 45.0), (50.0, 45.0), (-15.0, 45.0)]
    )
    seg0 = stage.segments[0]
    assert seg0.line_id == 0
    assert seg0.kind_id == 0  # floor
    assert (seg0.flags & 0x3) == 0x2  # ledge
    assert seg0.fighter_solid is True
    assert seg0.hi_flags == 1
    assert seg0.lo_flags == 0x200
    assert (seg0.prev_id0, seg0.next_id0, seg0.prev_id1, seg0.next_id1) == (11, 1, -1, -1)
    assert (seg0.x0, seg0.y0, seg0.x1, seg0.y1) == pytest.approx((-85.5656967, 0.0, -75.0, 0.0))
    assert seg0.ground_friction_mul == pytest.approx(1.0)
    assert (stage.stage_points[1].x, stage.stage_points[1].y, stage.stage_points[1].z) == pytest.approx(
        (0.0, 12.0, 0.0)
    )


@pytest.mark.integration
def test_stage_metadata_segments_match_legacy_fd_json() -> None:
    stage = read_mslstg01_v7(Path("data/stages/bin/grnla.bin"))
    fd = json.loads(Path("data/stages/final_destination.json").read_text(encoding="utf-8"))
    unit_scale = float(fd.get("unit_scale", 1.0))
    json_by_id = {int(seg["i"]): seg for seg in fd["segments"]}
    kind_by_id = {0: "floor", 1: "ceiling", 2: "right_wall", 3: "left_wall", 4: "dynamic"}
    assert len(json_by_id) == len(stage.segments)
    for seg in stage.segments:
        legacy = json_by_id[int(seg.line_id)]
        assert kind_by_id[int(seg.kind_id)] == str(legacy["kind"])
        assert bool(int(seg.flags) & 0x1) == bool(legacy.get("platform"))
        assert bool(int(seg.flags) & 0x2) == bool(legacy.get("ledge"))
        assert int(seg.hi_flags) == int(legacy["hi_flags"])
        assert int(seg.lo_flags) == int(legacy["lo_flags"])
        assert int(seg.prev_id0) == int(legacy["prev_id0"])
        assert int(seg.next_id0) == int(legacy["next_id0"])
        assert int(seg.prev_id1) == int(legacy["prev_id1"])
        assert int(seg.next_id1) == int(legacy["next_id1"])
        assert (seg.x0, seg.y0, seg.x1, seg.y1) == pytest.approx(
            (
                unit_scale * float(legacy["x0"]),
                unit_scale * float(legacy["y0"]),
                unit_scale * float(legacy["x1"]),
                unit_scale * float(legacy["y1"]),
            )
        )


@pytest.mark.integration
def test_stage_metadata_battlefield_known_platform_rows() -> None:
    stage = read_mslstg01_v7(Path("data/stages/bin/grnba.bin"))
    assert stage.segment_count == 23
    assert stage.stage_point_count == 22
    assert stage.spawn_count == 4
    assert stage.respawn_count == 4
    assert stage.cam_bounds_world == pytest.approx((-160.0, 160.0, 136.0, -47.200001))
    assert stage.blast_bounds_world == pytest.approx((-224.0, 224.0, 200.0, -108.800003))
    assert (stage.stage_points[1].x, stage.stage_points[1].y, stage.stage_points[1].z) == pytest.approx(
        (0.0, 35.200001, 0.0)
    )
    assert [p.x for p in stage.spawn_points] == pytest.approx([0.0, 0.0, -38.799999, 38.799999])
    assert [p.y for p in stage.spawn_points] == pytest.approx([8.0, 62.400002, 35.200001, 35.200001])
    assert [p.x for p in stage.respawn_points] == pytest.approx([12.800000, -40.0, 40.0, -12.0])
    assert [p.y for p in stage.respawn_points] == pytest.approx([80.0, 80.0, 80.0, 80.0])
    platform_by_id = {int(seg.line_id): seg for seg in stage.segments if int(seg.flags) & 0x1}
    assert sorted(platform_by_id) == [2, 3, 4]
    left = platform_by_id[2]
    assert left.kind_id == 0
    assert left.hi_flags == 1
    assert left.lo_flags == 0x100
    assert (left.x0, left.y0, left.x1, left.y1) == pytest.approx(
        (-57.600002, 27.200001, -20.0, 27.200001)
    )
    top = platform_by_id[3]
    assert (top.x0, top.y0, top.x1, top.y1) == pytest.approx(
        (-18.800001, 54.400002, 18.800001, 54.400002)
    )


@pytest.mark.integration
def test_stage_metadata_remaining_legal_stages_known_rows() -> None:
    fountain = read_mslstg01_v7(Path("data/stages/bin/griz.bin"))
    assert fountain.segment_count == 34
    assert fountain.spawn_count == 4
    assert fountain.respawn_count == 4
    assert fountain.cam_bounds_world == pytest.approx((-123.75, 123.75, 112.5, -84.75))
    assert [(p.x, p.y) for p in fountain.respawn_points] == pytest.approx([(0.0, 63.75)] * 4)
    fountain_platforms = [seg for seg in fountain.segments if int(seg.flags) & 0x1]
    assert [int(seg.line_id) for seg in fountain_platforms[:3]] == [0, 1, 2]
    assert (fountain_platforms[0].x0, fountain_platforms[0].y0, fountain_platforms[0].x1) == pytest.approx(
        (-14.25, 1.125, 14.25)
    )
    fountain_by_id = {int(seg.line_id): seg for seg in fountain.segments}
    assert fountain_by_id[3].lo_flags == 0x0202
    assert fountain_by_id[3].ground_friction_mul == pytest.approx(1.5)

    pokemon = read_mslstg01_v7(Path("data/stages/bin/grps.bin"))
    assert pokemon.segment_count == 136
    assert pokemon.spawn_count == 4
    assert pokemon.respawn_count == 4
    assert pokemon.cam_bounds_world == pytest.approx((-200.0, 200.0, 150.0, -160.0))
    assert pokemon.blast_bounds_world == pytest.approx((-230.0, 230.0, 180.0, -111.0))
    assert [(p.x, p.y) for p in pokemon.respawn_points] == pytest.approx([(0.0, 60.0)] * 4)
    pokemon_platforms = [seg for seg in pokemon.segments if int(seg.flags) & 0x1]
    assert pokemon_platforms[0].line_id == 11
    assert (pokemon_platforms[0].x0, pokemon_platforms[0].y0, pokemon_platforms[0].x1) == pytest.approx(
        (7.75, 26.002, 53.75),
        abs=1e-3,
    )

    yoshis = read_mslstg01_v7(Path("data/stages/bin/grst.bin"))
    assert yoshis.segment_count == 30
    assert yoshis.spawn_count == 4
    assert yoshis.respawn_count == 4
    assert yoshis.cam_bounds_world == pytest.approx((-126.0, 125.3, 118.3, -49.7), abs=1e-4)
    assert [(p.x, p.y) for p in yoshis.respawn_points] == pytest.approx([(0.0, 52.5)] * 4)
    yoshis_platforms = [seg for seg in yoshis.segments if int(seg.flags) & 0x1]
    assert [int(seg.line_id) for seg in yoshis_platforms[:4]] == [0, 1, 4, 5]

    dream = read_mslstg01_v7(Path("data/stages/bin/grop.bin"))
    assert dream.segment_count == 11
    assert dream.spawn_count == 4
    assert dream.respawn_count == 4
    assert dream.cam_bounds_world == pytest.approx((-165.0, 165.0, 190.0, -81.0))
    assert dream.blast_bounds_world == pytest.approx((-255.0, 255.0, 250.0, -123.0))
    assert [p.x for p in dream.respawn_points] == pytest.approx([0.0] * 4)
    assert [p.y for p in dream.respawn_points] == pytest.approx([84.2214966] * 4)
    dream_platforms = [seg for seg in dream.segments if int(seg.flags) & 0x1]
    assert [int(seg.line_id) for seg in dream_platforms] == [0, 1, 2]


@pytest.mark.integration
def test_stage_metadata_preserves_raw_mapline_links_for_supported_stages() -> None:
    # Raw MapLine graph fields are source data used by mpLineGetPrev/Next. These fixture rows cover
    # same-kind chains and cross-kind endpoint links; runtime code may not reconstruct them from
    # normalized endpoints.
    # refs/melee/src/melee/mp/types.h::MapLine
    # refs/melee/src/melee/mp/mplib.c::{mpLineGetPrev,mpLineGetNext}
    cases = [
        ("grnla.bin", 0, (11, 1, -1, -1)),
        ("grnba.bin", 0, (17, 1, -1, -1)),
        ("griz.bin", 3, (23, 4, -1, -1)),
        ("grps.bin", 0, (-1, 1, 52, -1)),
        ("grst.bin", 2, (18, 3, -1, -1)),
        ("grop.bin", 3, (9, 4, -1, -1)),
    ]
    for bin_name, line_id, expected_links in cases:
        stage = read_mslstg01_v7(Path("data/stages/bin") / bin_name)
        seg = next(seg for seg in stage.segments if int(seg.line_id) == line_id)
        assert (int(seg.prev_id0), int(seg.next_id0), int(seg.prev_id1), int(seg.next_id1)) == expected_links


def test_stage_metadata_contains_fod_platform_transform_records() -> None:
    stage = read_mslstg01_v7(Path("data/stages/bin/griz.bin"))
    by_line = {int(rec.line_id): rec for rec in stage.platform_transforms}
    assert sorted(by_line) == [0, 1, 2]
    assert int(by_line[0].platform_id) == 1  # left
    assert (
        float(by_line[0].x0),
        float(by_line[0].x1),
        float(by_line[0].y_const),
        float(by_line[0].height_coeff),
    ) == pytest.approx((-49.5, -21.0, 20.0, 0.75))
    assert int(by_line[1].platform_id) == 0  # right
    assert (
        float(by_line[1].x0),
        float(by_line[1].x1),
        float(by_line[1].y_const),
        float(by_line[1].height_coeff),
    ) == pytest.approx((21.0, 49.5, 28.0, 0.75))
    assert int(by_line[2].kind_id) == 2
    assert (float(by_line[2].x0), float(by_line[2].x1), float(by_line[2].y_const)) == pytest.approx(
        (-14.25, 14.25, 42.75)
    )


def test_stage_metadata_contains_source_backed_fod_motion_params() -> None:
    stage = read_mslstg01_v7(Path("data/stages/bin/griz.bin"))
    motion = next(m for m in stage.platform_motions if m.kind_id == STAGE_PLATFORM_MOTION_KIND_FOD)
    assert motion.platform_count == 2
    assert motion.home_height == pytest.approx(25.0)
    assert motion.target_delta_min > 0.0
    assert motion.target_delta_max > motion.target_delta_min
    assert motion.hidden_weight >= 0.0
    assert motion.move_weight >= 0.0
    assert motion.stay_weight >= 0.0
    assert motion.wait_max_frames >= motion.wait_min_frames

    audit = json.loads(Path("data/stages/bin/griz.json").read_text())
    assert audit["platform_motion"]["fountain_platform"]["home_height"] == pytest.approx(
        motion.home_height
    )


def test_runtime_fod_scheduler_uses_binary_motion_without_audit_json(tmp_path: Path) -> None:
    data_dir = _symlink_data_tree_with_private_dirs(tmp_path, ("stages",))
    _copy_stage_bins(data_dir)

    code = """
import numpy as np
import msl_binding
from tools.modelplay.sim_env import build_match_config_array

handle = msl_binding.init(batch_size=1, num_players=2)
try:
    config = build_match_config_array(stage_id=2, random_seed=0, char_ids=(1, 1))
    msl_binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
    sizes = msl_binding.sizes()
    inp = np.zeros((1, int(sizes["input"])), dtype=np.uint8)
    stage = np.zeros((1, int(sizes["stage_state"])), dtype=np.uint8)
    dtype = np.dtype([
        ("fod_platform_height", ("<f4", (2,))),
        ("fod_platform_height_valid", ("u1", (2,))),
        ("fod_platform_height_source", ("u1", (2,))),
        ("randall_exists", "u1"),
        ("_pad0", "V3"),
        ("randall_x", "<f4"),
        ("randall_y", "<f4"),
    ], align=False)
    msl_binding.step_input(handle, inp, inp)
    msl_binding.debug_write_stage_state(handle, stage)
    row = stage.view(dtype).reshape((1,))[0]
    assert int(row["fod_platform_height_valid"][0]) == 1
    assert int(row["fod_platform_height_valid"][1]) == 1
finally:
    msl_binding.destroy(handle)
"""
    env = os.environ.copy()
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], text=True, capture_output=True, env=env)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_stage_metadata_contains_yoshi_randall_and_rejects_center_raw_platform() -> None:
    stage = read_mslstg01_v7(Path("data/stages/bin/grst.bin"))
    by_line = {int(seg.line_id): seg for seg in stage.segments}
    assert int(by_line[0].flags) & 1
    assert by_line[0].fighter_solid is False
    assert by_line[0].stage_object_support_kind == STAGE_OBJECT_SUPPORT_KIND_YOSHI_SHYGUY
    assert by_line[1].stage_object_support_kind == 0
    assert 1000 in by_line
    assert int(by_line[1000].flags) & 1
    assert by_line[1000].fighter_solid is True
    assert by_line[1000].stage_object_support_kind == 0
    transforms = {int(rec.line_id): rec for rec in stage.platform_transforms}
    assert int(transforms[1000].kind_id) == 3
    path = {(int(rec.line_id), int(rec.frame)): rec for rec in stage.platform_paths}
    assert len([rec for rec in stage.platform_paths if int(rec.line_id) == 1000]) == 1200
    assert path[(1000, 416)].x0 == pytest.approx(89.7526397705, abs=1e-6)
    assert path[(1000, 416)].y == pytest.approx(-33.1844787598, abs=1e-6)
    assert path[(1000, 477)].x0 == pytest.approx(89.3354431152, abs=2e-6)
    assert path[(1000, 1016)].x0 == pytest.approx(-101.9196777344, abs=1e-6)
    assert path[(1000, 1069)].y == pytest.approx(-31.5900421143, abs=1e-6)


def test_stage_item_yoshi_shyguy_known_rows() -> None:
    params = yoshi_shyguy_metadata(Path("data"))
    assert params.stage_id == 8
    assert params.item_kind == 0xD2
    assert params.timer_min == 600
    assert params.timer_rand == 1800
    assert params.timer_reset == 120
    assert params.spawnmany_rarity == 8
    assert params.spawn_delay_step == 25
    assert params.fall_accel == pytest.approx(0.12)
    assert params.fall_speed_max == pytest.approx(2.2)
    assert params.damage_mul == pytest.approx(2.0)
    assert params.spawn_left_x == pytest.approx(-292.0)
    assert params.spawn_right_x == pytest.approx(304.0)
    assert params.state4_speed_mul == pytest.approx(1.5)
    assert params.jitter_y_amp == pytest.approx(3.0)
    assert params.collision_ecb_up == pytest.approx(6.0)
    assert params.collision_ecb_down == pytest.approx(6.0)
    assert params.collision_ecb_right == pytest.approx(8.0)
    assert params.collision_ecb_left == pytest.approx(8.0)
    assert params.collision_ecb_scale == pytest.approx(0.8500000238)
    assert params.damage_threshold == 15
    assert len(params.hurtboxes) == 1
    assert params.hurtboxes[0].bone_id == 0
    assert params.hurtboxes[0].a_offset == pytest.approx((0.0, 0.0, 0.0))
    assert params.hurtboxes[0].b_offset == pytest.approx((0.0, 0.0, 0.0))
    assert params.hurtboxes[0].scale == pytest.approx(5.0)
    assert params.vpos == pytest.approx((30.0, 45.0, 60.0, 75.0, 90.0, 0.0))
    assert params.speed == pytest.approx((0.3, 0.5, 0.75))
    assert len(params.dyn_y_vel) == 128
    assert params.dyn_y_vel[:4] == pytest.approx(
        (0.7028961182, 0.7015228271, 0.6987762451, 0.6946563721),
        abs=1e-7,
    )
    assert params.dyn_y_vel[63:66] == pytest.approx(
        (-0.7028961182, -0.7028961182, -0.7015228271),
        abs=1e-7,
    )


def test_stage_item_dream_whispy_known_rows() -> None:
    params = dream_whispy_metadata(Path("data"))
    assert params.stage_id == 28
    assert params.wind_speed == pytest.approx(0.2)
    assert params.right_rect_left == pytest.approx(-17.0)
    assert params.right_rect_right == pytest.approx(76.0)
    assert params.left_rect_left == pytest.approx(-74.0)
    assert params.left_rect_right == pytest.approx(-18.0)
    assert params.rect_bottom == pytest.approx(-10.0)
    assert params.rect_top == pytest.approx(40.0)


@pytest.mark.integration
def test_fighter_part_metadata_known_anchors() -> None:
    fox = read_mslpart1_v1(Path("data/model_parts/fox.bin"))
    falco = read_mslpart1_v1(Path("data/model_parts/falco.bin"))
    assert fox.char_id == 2
    assert falco.char_id == 20
    assert fox.local_part_count > 30
    assert falco.local_part_count > 30
    assert fox.anchor_count >= 10
    assert falco.anchor_count >= 10
    fox_anchors = {(a.kind, a.part_id) for a in fox.anchors}
    falco_anchors = {(a.kind, a.part_id) for a in falco.anchors}
    assert (ANCHOR_IDS["ecb_joint"], 41) in fox_anchors
    assert (ANCHOR_IDS["laser_spawn_joint"], 67) in fox_anchors
    assert (ANCHOR_IDS["reflector_bone"], 1) in fox_anchors
    assert (ANCHOR_IDS["camera_zoom_target"], 22) in fox_anchors
    assert (ANCHOR_IDS["ecb_joint"], 39) in falco_anchors
    assert (ANCHOR_IDS["laser_spawn_joint"], 61) in falco_anchors


@pytest.mark.integration
def test_runtime_char_part_anchors_match_mslpart1() -> None:
    import msl_binding

    cases = [
        (1, Path("data/model_parts/fox.bin")),
        (22, Path("data/model_parts/falco.bin")),
    ]
    for sim_char, path in cases:
        runtime = msl_binding.char_params_part_anchors(sim_char)
        parts = read_mslpart1_v1(path)
        anchors_by_kind: dict[int, list[int]] = {}
        for anchor in parts.anchors:
            anchors_by_kind.setdefault(anchor.kind, []).append(anchor.part_id)
        assert anchors_by_kind[ANCHOR_IDS["ecb_joint"]] == list(runtime["ecb_joints"])
        assert anchors_by_kind[ANCHOR_IDS["laser_spawn_joint"]] == [
            int(runtime["laser_spawn_joint_part_id"])
        ]
        assert anchors_by_kind[ANCHOR_IDS["reflector_bone"]] == [
            int(runtime["reflector_bone_part_id"])
        ]
        assert anchors_by_kind[ANCHOR_IDS["camera_zoom_target"]] == [
            int(runtime["camera_zoom_target_bone_part_id"])
        ]
        assert anchors_by_kind[ANCHOR_IDS["grab_capture_anchor"]] == [
            int(runtime["grab_capture_anchor_part_id"])
        ]


@pytest.mark.integration
def test_fighter_part_metadata_known_parent_rows() -> None:
    fox = read_mslpart1_v1(Path("data/model_parts/fox.bin"))
    falco = read_mslpart1_v1(Path("data/model_parts/falco.bin"))
    fox_parts = {p.part_id: p for p in fox.parts}
    falco_parts = {p.part_id: p for p in falco.parts}
    assert (fox_parts[67].parent_part_id, fox_parts[67].jobj_flags) == (57, 9)
    assert (fox_parts[71].parent_part_id, fox_parts[71].jobj_flags) == (3, 9)
    assert (falco_parts[61].parent_part_id, falco_parts[61].jobj_flags) == (52, 9)
    assert (falco_parts[65].parent_part_id, falco_parts[65].jobj_flags) == (3, 9)


@pytest.mark.integration
def test_item_article_metadata_known_records_and_manifest() -> None:
    table = read_mslitar1(Path("data/items/articles/fox_falco.bin"))
    assert table.record_count >= 20
    manifest = json.loads(Path("data/items/articles/manifest.json").read_text(encoding="utf-8"))
    fields = {row["name"] for row in manifest["fields"]}
    assert "blaster_shot_itkind" in fields
    assert "side_special_illusion_itkind" in fields
    assert "laser_lifetime_frames" in fields
    assert "illusion_item_state0_damage" in fields
    assert "shield_bounce_extra_degrees" in fields
    assert manifest["char_domain"]["name"] == "GALE01 internal FighterKind enum"

    def rec(char_id: int, field_name: str):
        field_id = FIELD_SPECS[field_name].field_id
        matches = [r for r in table.records if r.char_id == char_id and r.field_id == field_id]
        assert len(matches) == 1
        out = matches[0]
        assert out.char_domain == ITEM_ARTICLE_CHAR_DOMAIN_GALE01_FIGHTER_KIND
        return out

    fox_laser_kind = rec(2, "blaster_shot_itkind")
    assert fox_laser_kind.value_type == ITEM_ARTICLE_VALUE_U16
    assert fox_laser_kind.unit_id == UNIT_ITEM_KIND
    assert fox_laser_kind.u32_value == 54
    falco_laser_kind = rec(20, "blaster_shot_itkind")
    assert falco_laser_kind.u32_value == 55
    fox_illusion_kind = rec(2, "side_special_illusion_itkind")
    assert fox_illusion_kind.value_type == ITEM_ARTICLE_VALUE_U16
    assert fox_illusion_kind.unit_id == UNIT_ITEM_KIND
    assert fox_illusion_kind.u32_value == 56
    falco_illusion_kind = rec(20, "side_special_illusion_itkind")
    assert falco_illusion_kind.value_type == ITEM_ARTICLE_VALUE_U16
    assert falco_illusion_kind.u32_value == 57

    fox_spawn_joint = rec(2, "laser_spawn_joint_part_id")
    assert fox_spawn_joint.unit_id == UNIT_PART_ID
    assert fox_spawn_joint.u32_value == 67
    fox_lifetime = rec(2, "laser_lifetime_frames")
    assert fox_lifetime.value_type == ITEM_ARTICLE_VALUE_U32
    assert fox_lifetime.unit_id == UNIT_FRAMES
    assert fox_lifetime.u32_value == 35
    falco_lifetime = rec(20, "laser_lifetime_frames")
    assert falco_lifetime.u32_value == 100
    fox_damage = rec(2, "laser_damage")
    assert fox_damage.value_type == ITEM_ARTICLE_VALUE_F32
    assert fox_damage.f32_value == pytest.approx(3.0)
    fox_size = rec(2, "laser_size")
    assert fox_size.f32_value == pytest.approx(1.171800017)
    bounce = rec(2, "shield_bounce_extra_degrees")
    assert bounce.unit_id == UNIT_DEGREES
    assert bounce.f32_value == pytest.approx(45.0)


@pytest.mark.integration
def test_item_article_tooling_accessors_map_to_sim_char_domain() -> None:
    assert SIM_CHAR_TO_GALE01_FIGHTER_KIND == {1: 2, 22: 20}
    assert item_article_kind_set(Path("data"), "blaster_shot_itkind") == (54, 55)
    assert item_article_kind_set(Path("data"), "side_special_illusion_itkind") == (56, 57)
    by_char = item_article_values_by_sim_char(Path("data"), "laser_lifetime_frames")
    assert by_char[1] == 35
    assert by_char[22] == 100


@pytest.mark.integration
def test_stage_seed_segments_are_loaded_from_mslstg01() -> None:
    segments = _load_stage_segments_for_seed(stage_id=32, data_root=Path("data"))
    assert len(segments) == 16
    assert segments[0]["i"] == 0
    assert segments[0]["kind"] == "floor"
    assert segments[0]["ledge"] is True
    assert segments[0]["hi_flags"] == 1
    assert segments[0]["lo_flags"] == 0x0200
    assert segments[0]["x0"] == pytest.approx(-85.5656967163086)
    assert segments[0]["y0"] == pytest.approx(0.0)
    battlefield = _load_stage_segments_for_seed(stage_id=31, data_root=Path("data"))
    assert len(battlefield) == 23
    platforms = [seg for seg in battlefield if seg["platform"]]
    assert [seg["i"] for seg in platforms] == [2, 3, 4]
    assert platforms[0]["x0"] == pytest.approx(-57.600002)
    assert platforms[0]["y0"] == pytest.approx(27.200001)
    pokemon = _load_stage_segments_for_seed(stage_id=3, data_root=Path("data"))
    assert len(pokemon) == 136
    assert pokemon[0]["i"] == 0
    assert pokemon[0]["kind"] == "floor"
    assert [seg["i"] for seg in pokemon if seg["platform"]][:3] == [11, 17, 18]


def test_runtime_stage_collision_rejects_stale_mslstg01(tmp_path: Path) -> None:
    data_dir = _symlink_data_tree_with_private_dirs(tmp_path, ("stages",))
    _copy_stage_bins(data_dir, corrupt="grnla.bin")

    code = """
import msl_binding
try:
    msl_binding.init(batch_size=1, num_players=2)
except Exception:
    pass
else:
    raise SystemExit(1)
try:
    msl_binding.stage_floor_segment(32, 1)
except Exception:
    raise SystemExit(0)
raise SystemExit(2)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_stage_collision_does_not_require_unused_registered_battlefield_artifact(
    tmp_path: Path,
) -> None:
    root_data = Path("data").resolve()
    data_dir = _symlink_data_tree_with_private_dirs(tmp_path, ("stages",))
    (data_dir / "stages" / "bin").mkdir(parents=True)
    for name in SUPPORTED_STAGE_BINS:
        if name != "grnba.bin":
            (data_dir / "stages" / "bin" / name).write_bytes(
                (root_data / "stages" / "bin" / name).read_bytes()
            )

    code = """
import msl_binding
import numpy as np
from tools.eval.dataset import SEED_DTYPE
try:
    handle = msl_binding.init(batch_size=1, num_players=2)
except Exception:
    raise SystemExit(1)
try:
    msl_binding.stage_match_flow_roles(31)
except Exception:
    pass
else:
    raise SystemExit(2)
seed = np.zeros((1,), dtype=SEED_DTYPE)
seed["stage_id"][0] = np.uint32(31)
seed["num_players"][0] = np.uint8(2)
try:
    msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(msl_binding.sizes()["seed"]))))
except Exception:
    raise SystemExit(0)
raise SystemExit(3)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_reseed_rejects_unsupported_stage_id() -> None:
    code = """
import msl_binding
import numpy as np
from tools.eval.dataset import SEED_DTYPE
handle = msl_binding.init(batch_size=1, num_players=2)
seed = np.zeros((1,), dtype=SEED_DTYPE)
seed["stage_id"][0] = np.uint32(4)
seed["num_players"][0] = np.uint8(2)
try:
    msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(msl_binding.sizes()["seed"]))))
except Exception:
    raise SystemExit(0)
raise SystemExit(1)
"""
    proc = subprocess.run([sys.executable, "-c", code], text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_stage_collision_reads_mslstg01_segments(tmp_path: Path) -> None:
    root_data = Path("data").resolve()
    data_dir = _symlink_data_tree_with_private_dirs(tmp_path, ("stages",))
    (data_dir / "stages" / "bin").mkdir(parents=True)
    stage = read_mslstg01_v7(root_data / "stages" / "bin" / "grnla.bin")
    record_i, floor_seg = next(
        (i, seg) for i, seg in enumerate(stage.segments) if int(seg.kind_id) == 0 and int(seg.line_id) == 1
    )
    buf = bytearray((root_data / "stages" / "bin" / "grnla.bin").read_bytes())
    # Segment record layout: <HBBHHhhhhfffff>, records start after the 64-byte MSLSTG01 header.
    rec_off = 64 + record_i * 36
    struct.pack_into("<f", buf, rec_off + 20, 5.0)
    struct.pack_into("<f", buf, rec_off + 28, 5.0)
    _copy_stage_bins(data_dir)
    (data_dir / "stages" / "bin" / "grnla.bin").write_bytes(bytes(buf))

    code = f"""
import msl_binding
seg = msl_binding.stage_floor_segment(32, {int(floor_seg.line_id)})
if seg is None:
    raise SystemExit(2)
if abs(float(seg["y0"]) - 5.0) > 1e-6 or abs(float(seg["y1"]) - 5.0) > 1e-6:
    raise SystemExit(1)
raise SystemExit(0)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_stage_collision_reads_battlefield_platform_segments() -> None:
    code = """
import msl_binding
msl_binding.init(batch_size=1, num_players=2)
seg = msl_binding.stage_floor_segment(31, 2)
if seg is None:
    raise SystemExit(2)
if abs(float(seg["y0"]) - 27.200000762939453) > 1e-5:
    raise SystemExit(1)
if int(seg["is_platform"]) != 1:
    raise SystemExit(3)
fighter_seg = msl_binding.stage_fighter_floor_segment(31, 2)
if fighter_seg is not None:
    raise SystemExit(4)
main = msl_binding.stage_fighter_floor_segment(31, 0)
if main is None:
    raise SystemExit(5)
raise SystemExit(0)
"""
    proc = subprocess.run([sys.executable, "-c", code], text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_stage_collision_reads_match_flow_roles_from_mslstg01(tmp_path: Path) -> None:
    data_dir = _symlink_data_tree_with_private_dirs(tmp_path, ("stages",))
    _copy_stage_bins(data_dir)

    code = """
import msl_binding
msl_binding.init(batch_size=1, num_players=2)
roles = msl_binding.stage_match_flow_roles(32)
if roles is None:
    raise SystemExit(2)
if abs(float(roles["respawn_points"][1][0]) - -50.0) > 1e-6:
    raise SystemExit(1)
if abs(float(roles["cam_bounds"][2]) - 114.0) > 1e-6:
    raise SystemExit(3)
raise SystemExit(0)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_stage_collision_reads_battlefield_match_flow_roles() -> None:
    code = """
import msl_binding
msl_binding.init(batch_size=1, num_players=2)
roles = msl_binding.stage_match_flow_roles(31)
if roles is None:
    raise SystemExit(2)
if abs(float(roles["respawn_points"][2][0]) - 40.0) > 1e-5:
    raise SystemExit(1)
if abs(float(roles["cam_bounds"][3]) - -47.20000076293945) > 1e-5:
    raise SystemExit(3)
raise SystemExit(0)
"""
    proc = subprocess.run([sys.executable, "-c", code], text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_stage_collision_reads_remaining_stage_roles_and_platforms() -> None:
    code = """
import msl_binding
msl_binding.init(batch_size=1, num_players=2)
cases = [
    (2, 0, 1.125, 1, 63.75),
    (3, 11, 26.002, 1, 60.0),
    (8, 1, 23.45, 1, 52.5),
    (28, 2, 51.42530059814453, 1, 84.22149658203125),
]
for stage_id, line_id, y0, is_platform, respawn_y in cases:
    seg = msl_binding.stage_floor_segment(stage_id, line_id)
    if seg is None:
        raise SystemExit(10 + stage_id)
    if abs(float(seg["y0"]) - y0) > 1e-4:
        raise SystemExit(30 + stage_id)
    if int(seg["is_platform"]) != is_platform:
        raise SystemExit(50 + stage_id)
    roles = msl_binding.stage_match_flow_roles(stage_id)
    if roles is None:
        raise SystemExit(70 + stage_id)
    if abs(float(roles["respawn_points"][0][1]) - respawn_y) > 1e-4:
        raise SystemExit(90 + stage_id)
raise SystemExit(0)
"""
    proc = subprocess.run([sys.executable, "-c", code], text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_frozen_ps_preserves_raw_links_but_uses_fighter_solid_mask() -> None:
    # GrPs raw MapLine links include transformation topology. The current runtime domain is frozen
    # Stadium, so raw ids remain inspectable while fighter collision is governed by the generated
    # active/fighter-solid mask in MSLSTG01 rather than a runtime line-id allowlist.
    # refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
    # refs/melee/src/melee/gr/grpstadium.c::{grStadium_OnInit,grStadium_801D10F0}
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        line_52 = msl_binding.stage_floor_segment(3, 52)
        assert line_52 is not None
        assert int(line_52["raw_next_id"]) == 55
        assert int(line_52["fighter_solid"]) == 1
        assert int(line_52["next"]) == int(msl_binding.stage_floor_segment(3, 34)["line_index"])
        assert int(msl_binding.stage_floor_segment(3, 55)["fighter_solid"]) == 0
        assert msl_binding.stage_fighter_floor_segment(3, 55) is None
    finally:
        msl_binding.destroy(handle)

    stage = read_mslstg01_v7(Path("data/stages/bin/grps.bin"))
    seg_by_id = {int(seg.line_id): seg for seg in stage.segments}
    assert seg_by_id[35].fighter_solid is True
    assert seg_by_id[81].kind_id == 2  # right_wall
    assert seg_by_id[81].fighter_solid is False


def test_runtime_stage_lookup_caches_match_mslstg01_for_supported_stages() -> None:
    import msl_binding

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        for bin_name, stage_id in SUPPORTED_STAGE_IDS_BY_BIN.items():
            stage = read_mslstg01_v7(Path("data/stages/bin") / bin_name)
            transforms = {int(rec.line_id): rec for rec in stage.platform_transforms}

            floors = sorted((seg for seg in stage.segments if int(seg.kind_id) == 0), key=lambda s: int(s.line_id))
            fighter_floors = [
                seg for seg in floors if not (int(seg.flags) & 0x1) and bool(seg.fighter_solid)
            ]
            ceilings = sorted((seg for seg in stage.segments if int(seg.kind_id) == 1), key=lambda s: int(s.line_id))
            right_walls = sorted((seg for seg in stage.segments if int(seg.kind_id) == 2), key=lambda s: int(s.line_id))
            left_walls = sorted((seg for seg in stage.segments if int(seg.kind_id) == 3), key=lambda s: int(s.line_id))

            for expected_idx, seg in enumerate(floors):
                line_id = int(seg.line_id)
                runtime = msl_binding.stage_floor_segment(stage_id, line_id)
                assert runtime is not None, (stage_id, "floor", line_id)
                assert int(runtime["line_index"]) == expected_idx
                rec = transforms.get(line_id)
                expected_kind = 0 if rec is None else int(rec.kind_id)
                expected_platform_id = 0 if rec is None else int(rec.platform_id)
                assert int(runtime["platform_transform_kind"]) == expected_kind
                assert int(runtime["platform_transform_id"]) == expected_platform_id
                assert float(runtime["ground_friction_mul"]) == pytest.approx(
                    float(seg.ground_friction_mul)
                )

            fighter_index_by_line = {
                int(seg.line_id): i for i, seg in enumerate(fighter_floors)
            }
            for seg in floors:
                line_id = int(seg.line_id)
                runtime = msl_binding.stage_fighter_floor_segment(stage_id, line_id)
                if line_id not in fighter_index_by_line:
                    assert runtime is None, (stage_id, "fighter_floor", line_id)
                else:
                    assert runtime is not None, (stage_id, "fighter_floor", line_id)
                    assert int(runtime["line_index"]) == fighter_index_by_line[line_id]

            for expected_idx, seg in enumerate(ceilings):
                line_id = int(seg.line_id)
                runtime = msl_binding.stage_ceiling_segment(stage_id, line_id)
                assert runtime is not None, (stage_id, "ceiling", line_id)
                assert int(runtime["line_index"]) == expected_idx

            for expected_idx, seg in enumerate(left_walls):
                line_id = int(seg.line_id)
                runtime = msl_binding.stage_left_wall_segment(stage_id, line_id)
                assert runtime is not None, (stage_id, "left_wall", line_id)
                assert int(runtime["line_index"]) == expected_idx

            for expected_idx, seg in enumerate(right_walls):
                line_id = int(seg.line_id)
                runtime = msl_binding.stage_right_wall_segment(stage_id, line_id)
                assert runtime is not None, (stage_id, "right_wall", line_id)
                assert int(runtime["line_index"]) == expected_idx
    finally:
        msl_binding.destroy(handle)


def test_runtime_move_tables_reject_stale_mslftsc1(tmp_path: Path) -> None:
    root_data = Path("data").resolve()
    data_dir = _symlink_data_tree_with_private_dirs(tmp_path, ("scripts",))
    for name in ("fox.bin", "falco.bin"):
        buf = bytearray((root_data / "scripts" / name).read_bytes())
        if name == "fox.bin":
            buf[8:12] = (SCRIPT_VERSION - 1).to_bytes(4, "little")
        (data_dir / "scripts" / name).write_bytes(bytes(buf))

    code = """
import msl_binding
try:
    msl_binding.init(batch_size=1, num_players=2)
except Exception:
    raise SystemExit(0)
raise SystemExit(1)
"""
    env = dict(os.environ)
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


@pytest.mark.integration
def test_script_timeline_known_decoded_events() -> None:
    fox = read_mslftsc1_v1(Path("data/scripts/fox.bin"))
    falco = read_mslftsc1_v1(Path("data/scripts/falco.bin"))
    assert fox.entry_count >= 60
    assert falco.entry_count >= 60
    assert fox.event_count > 100
    assert falco.event_count > 100
    manifest = json.loads(Path("data/scripts/fox_manifest.json").read_text(encoding="utf-8"))
    kinds = {row["name"] for row in manifest["event_kinds"]}
    assert "create_hitbox" in kinds
    assert "allow_interrupt" in kinds
    assert "set_jab_rapid" in kinds
    assert "start_smash_charge" in kinds
    attackairn = next(e for e in fox.entries if e.msid == 68)
    attackairn_events = fox.events[attackairn.first_event : attackairn.first_event + attackairn.event_count]
    assert any(e.frame == 42 and e.kind_id == EVENT_IDS["allow_interrupt"] and e.payload == {} for e in attackairn_events)
    attack12 = next(e for e in fox.entries if e.msid == 47)
    attack12_events = fox.events[attack12.first_event : attack12.first_event + attack12.event_count]
    assert any(
        e.frame == 6 and e.kind_id == EVENT_IDS["set_jab_rapid"] and e.payload == {"state": 1}
        for e in attack12_events
    )
    turnrun = next(e for e in fox.entries if e.msid == 11)
    turnrun_events = fox.events[turnrun.first_event : turnrun.first_event + turnrun.event_count]
    assert any(
        e.frame == 9 and e.kind_id == EVENT_IDS["set_cmd_var"] and e.payload == {"idx": 1, "value": 1}
        for e in turnrun_events
    )
    lipstick = next(e for e in fox.entries if e.msid == 128)
    lipstick_events = fox.events[lipstick.first_event : lipstick.first_event + lipstick.event_count]
    assert any(
        e.frame == 5 and e.kind_id == EVENT_IDS["set_cmd_var"] and e.payload == {"idx": 0, "value": 286}
        for e in lipstick_events
    )


def test_script_timeline_manifests_do_not_drop_runtime_owner_events() -> None:
    for char_name in ("fox", "falco"):
        manifest = json.loads(Path(f"data/scripts/{char_name}_manifest.json").read_text(encoding="utf-8"))
        dropped = set(manifest.get("unknown_event_counts", {}))
        dropped.update(manifest.get("unsupported_event_counts", {}))
        assert dropped.isdisjoint(RUNTIME_OWNER_EVENT_KINDS), (char_name, sorted(dropped))


@pytest.mark.integration
def test_script_timeline_key_events_match_existing_move_json() -> None:
    cases = [
        ("ftCo_SM_AttackAirN", "allow_interrupt"),
        ("ftCo_SM_AttackAirN", "create_hitbox"),
        ("ftCo_SM_AttackAirN", "clear_hitboxes"),
        ("ftCo_SM_ThrowHi", "set_throw_spawn_projectile"),
        ("ftCo_SM_Attack12", "set_jab_rapid"),
        ("ftCo_SM_AttackS4", "start_smash_charge"),
        ("ftCo_SM_EscapeAir", "set_hit_status"),
        ("ftCo_SM_AttackHi4", "set_all_hurt_state"),
        ("ftCo_SM_AttackHi4", "set_hurt_state"),
        ("ftCo_SM_AttackS4", "set_state_flags_221c_u16_y"),
    ]

    for char_name in ("fox", "falco"):
        moves = json.loads(Path(f"data/moves/{char_name}.json").read_text(encoding="utf-8"))
        table = read_mslftsc1_v1(Path(f"data/scripts/{char_name}.bin"))
        by_msid = {entry.msid: table.events[entry.first_event : entry.first_event + entry.event_count] for entry in table.entries}
        for move_name, event_kind in cases:
            move = moves["moves"][move_name]
            msid = int(move["submotion_id"])
            expected = [
                (int(ev["frame"]), EVENT_IDS[event_kind], dict(ev.get("data", {})))
                for ev in move["events"]
                if ev.get("kind") == event_kind
            ]
            got = [
                (int(ev.frame), int(ev.kind_id), dict(ev.payload))
                for ev in by_msid[msid]
                if int(ev.kind_id) == EVENT_IDS[event_kind]
            ]
            assert got == expected, (char_name, move_name, event_kind)


def _move_events(moves: dict, move_name: str) -> list[dict]:
    return list(moves.get("moves", {}).get(move_name, {}).get("events", []))


def _special_events(moves: dict, msid: int) -> list[dict]:
    return list(moves.get("specials_by_msid", {}).get(str(msid), {}).get("events", []))


def _cmd_var_window(events: list[dict], idx: int, *, open_end: bool) -> tuple[int, int] | None:
    on = None
    off = None
    for ev in events:
        if ev.get("kind") != "set_cmd_var":
            continue
        data = dict(ev.get("data", {}))
        if int(data.get("idx", -1)) != idx:
            continue
        frame = int(ev["frame"])
        value = int(data.get("value", 0))
        if value != 0 and on is None:
            on = frame
        elif value == 0 and on is not None and off is None:
            off = frame
    if on is None:
        return None
    if off is None:
        off = 32767 if open_end else None
    if off is None:
        return None
    return (on, off)


def _cmd0_window(events: list[dict], *, open_end: bool) -> tuple[int, int] | None:
    return _cmd_var_window(events, 0, open_end=open_end)


def _cmd_var_value1_pulses(events: list[dict], idx: int) -> list[int]:
    return [
        int(ev["frame"])
        for ev in events
        if ev.get("kind") == "set_cmd_var"
        and int(ev.get("data", {}).get("idx", -1)) == idx
        and int(ev.get("data", {}).get("value", 0)) == 1
    ]


def _hurtcap_bone_part_ids(char_name: str) -> list[int]:
    path = Path("data/hurtcaps") / f"{char_name}.bin"
    buf = path.read_bytes()
    if buf[:8] != b"MSLHURT1":
        raise AssertionError(f"{path}: bad magic")
    version, cap_count, reserved = struct.unpack_from("<IHH", buf, 8)
    if int(version) != 1 or int(reserved) != 0:
        raise AssertionError(f"{path}: bad header")
    out = []
    off = 16
    for _ in range(int(cap_count)):
        out.append(int(struct.unpack_from("<H", buf, off)[0]))
        off += 34
    return out


def _script_owner_expected_timelines(events: list[dict], cap_bones: list[int]) -> tuple[list[int], list[int], list[int], list[int]]:
    hit_status = []
    hurt_masks = []
    airborne = []
    flags_221c_y = []
    cur_hit = 0
    cur_hurt = [0 for _ in cap_bones]
    cur_flags = 0
    bone_to_cap = {}
    for i, bone in enumerate(cap_bones):
        bone_to_cap.setdefault(bone, i)
    events_by_frame: dict[int, list[dict]] = {}
    for ev in events:
        events_by_frame.setdefault(int(ev.get("frame", 0)), []).append(ev)
    for frame in range(0, 240):
        air = -1
        for ev in events_by_frame.get(frame, []):
            kind = ev.get("kind")
            data = dict(ev.get("data", {}))
            if kind == "set_hit_status":
                cur_hit = int(data.get("state", 0)) & 0xFF
            elif kind == "set_all_hurt_state":
                cur_hurt = [int(data.get("state", 0)) for _ in cap_bones]
            elif kind == "set_hurt_state":
                cap_i = bone_to_cap.get(int(data.get("bone_idx", -1)))
                if cap_i is not None:
                    cur_hurt[cap_i] = int(data.get("state", 0))
            elif kind == "set_airborne_state":
                state = int(data.get("state", -1))
                if 0 <= state <= 2:
                    air = state
            elif kind == "set_state_flags_221c_u16_y":
                cur_flags = int(data.get("flags", 0)) & 0x7
        mask = 0
        for cap_i, state in enumerate(cur_hurt):
            if state == 0:
                mask |= 1 << cap_i
        hit_status.append(cur_hit)
        hurt_masks.append(mask)
        airborne.append(air)
        flags_221c_y.append(cur_flags)
    return hit_status, hurt_masks, airborne, flags_221c_y


def _allow_interrupt_window(events: list[dict]) -> tuple[int, int] | None:
    for ev in events:
        if ev.get("kind") == "allow_interrupt":
            return (int(ev["frame"]), 32767)
    return None


def _second_create_hitbox_window(events: list[dict]) -> tuple[int, int] | None:
    first = None
    second = None
    clear = None
    for ev in events:
        frame = int(ev["frame"])
        if ev.get("kind") == "create_hitbox":
            if first is None:
                first = frame
            elif second is None and frame != first:
                second = frame
        elif ev.get("kind") == "clear_hitboxes" and second is not None and frame >= second:
            clear = frame
            break
    if second is None:
        return None
    return (second, clear if clear is not None else 32767)


def _throw_flags_start(events: list[dict], *, hit_idx: int | None = None) -> int | None:
    frames = []
    for ev in events:
        if ev.get("kind") != "set_throw_flags":
            continue
        if hit_idx is not None and int(dict(ev.get("data", {})).get("hit_idx", -1)) != hit_idx:
            continue
        frames.append(int(ev["frame"]))
    return min(frames) if frames else None


def _jab_combo_window(events: list[dict]) -> tuple[int, int] | None:
    for ev in events:
        if ev.get("kind") == "set_jab_combo" and int(dict(ev.get("data", {})).get("disabled", 1)) == 0:
            return (int(ev["frame"]), 32767)
    return None


def _jab_rapid_window(events: list[dict]) -> tuple[int, int] | None:
    on = None
    off = None
    for ev in events:
        if ev.get("kind") != "set_jab_rapid":
            continue
        frame = int(ev["frame"])
        state = int(dict(ev.get("data", {})).get("state", 0))
        if state != 0 and on is None:
            on = frame
        elif state == 0 and on is not None and off is None:
            off = frame
    if on is None:
        return None
    return (on, off if off is not None else 32767)


def _active(window: tuple[int, int] | None, frame: float) -> int:
    if window is None:
        return 0
    return int(float(window[0]) <= frame < float(window[1]))


def _throw_cmd1_window(events: list[dict]) -> tuple[int, int] | None:
    on = None
    off = None
    for ev in events:
        if ev.get("kind") != "set_cmd_var":
            continue
        data = dict(ev.get("data", {}))
        if int(data.get("idx", -1)) != 1:
            continue
        frame = int(ev["frame"])
        value = int(data.get("value", 0))
        if value == 1 and on is None:
            on = frame
        elif on is not None and off is None:
            off = frame
    if on is None:
        return None
    return (on, off if off is not None else 32767)


def _throw_projectile_pulses(events: list[dict]) -> list[int]:
    return [int(ev["frame"]) for ev in events if ev.get("kind") == "set_throw_spawn_projectile"]


def _throw_hitboxes(events: list[dict]) -> dict[int, tuple]:
    out = {}
    for ev in events:
        if ev.get("kind") != "set_throw_hitbox":
            continue
        data = dict(ev.get("data", {}))
        out[int(data["idx"])] = (
            float(data["damage"]),
            int(data["angle"]),
            int(data["kbg"]),
            int(data["wsk"]),
            int(data["bkb"]),
            int(data["element"]),
            int(data["sfx_kind"]),
            int(data["sfx_severity"]),
        )
    return out


def _crossed_frames(frames: list[int], prev_frame: int, cur_frame: int) -> list[int]:
    return [frame for frame in frames if prev_frame < frame <= cur_frame]


@pytest.mark.integration
def test_runtime_move_tables_mslftsc1_matches_legacy_json_queries() -> None:
    import msl_binding

    attackair = [
        (0x0041, "ftCo_SM_AttackAirN"),
        (0x0042, "ftCo_SM_AttackAirF"),
        (0x0043, "ftCo_SM_AttackAirB"),
        (0x0044, "ftCo_SM_AttackAirHi"),
        (0x0045, "ftCo_SM_AttackAirLw"),
    ]
    grounded = [
        (0x002C, "ftCo_SM_Attack11"),
        (0x002D, "ftCo_SM_Attack12"),
        (0x002E, "ftCo_SM_Attack13"),
        (0x0032, "ftCo_SM_AttackDash"),
        (0x0035, "ftCo_SM_AttackS3"),
        (0x0038, "ftCo_SM_AttackHi3"),
        (0x0039, "ftCo_SM_AttackLw3"),
        (0x003C, "ftCo_SM_AttackS4"),
        (0x003F, "ftCo_SM_AttackHi4"),
        (0x0040, "ftCo_SM_AttackLw4"),
    ]
    throws = [
        (0x00DB, "ftCo_SM_ThrowF"),
        (0x00DC, "ftCo_SM_ThrowB"),
        (0x00DD, "ftCo_SM_ThrowHi"),
        (0x00DE, "ftCo_SM_ThrowLw"),
    ]
    cases = [("fox", 1), ("falco", 22)]
    for char_name, char_id in cases:
        moves = json.loads(Path(f"data/moves/{char_name}.json").read_text(encoding="utf-8"))
        cap_bones = _hurtcap_bone_part_ids(char_name)
        for action_id, move_name in attackair:
            events = _move_events(moves, move_name)
            cmd0 = _cmd0_window(events, open_end=False)
            allow = _allow_interrupt_window(events)
            second_create = _second_create_hitbox_window(events)
            for frame in range(0, 80):
                assert msl_binding.move_tables_debug_query(
                    "attackair_cmd0", char_id, action_id, float(frame), 0.0
                ) == _active(cmd0, float(frame))
                assert msl_binding.move_tables_debug_query(
                    "attackair_allow_interrupt", char_id, action_id, float(frame), 0.0
                ) == _active(allow, float(frame))
                assert msl_binding.move_tables_debug_query(
                    "attackair_second_create_hitbox_phase", char_id, action_id, float(frame), 0.0
                ) == _active(second_create, float(frame))

        for action_id, move_name in grounded:
            events = _move_events(moves, move_name)
            allow = _allow_interrupt_window(events)
            for frame in range(0, 80):
                assert msl_binding.move_tables_debug_query(
                    "grounded_attack_allow_interrupt", char_id, action_id, float(frame), 0.0
                ) == _active(allow, float(frame))

        for action_id, move_name in ((0x003C, "ftCo_SM_AttackS4"), (0x003F, "ftCo_SM_AttackHi4"), (0x0040, "ftCo_SM_AttackLw4")):
            events = _move_events(moves, move_name)
            charge = next((ev for ev in events if ev.get("kind") == "start_smash_charge"), None)
            expected_frame = int(charge["frame"]) if charge else None
            expected_hold = int(charge["data"]["hold_frames"]) if charge else 0
            expected_mul = float(charge["data"]["damage_mul"]) if charge else 1.0
            if charge:
                # GALE01 opcode 56 uses ftAction_804D82A0 (`.float 0.003906`) via fmuls, not an
                # exact 1/256 scale. Locking the source literal here prevents both the JSON and
                # runtime table from drifting together back to exact fixed-point division.
                source_scale = struct.unpack("<f", struct.pack("<f", 0.003906))[0]
                source_mul = struct.unpack(
                    "<f",
                    struct.pack("<f", float(350) * source_scale),
                )[0]
                assert expected_mul == pytest.approx(source_mul, abs=1.0e-8)
            assert msl_binding.move_tables_debug_query(
                "grounded_smash_charge_damage_mul", char_id, action_id, 0.0, 0.0
            ) == pytest.approx(expected_mul)
            for frame in range(0, 80):
                got_ok, got_hold = msl_binding.move_tables_debug_query(
                    "grounded_smash_charge_crossed", char_id, action_id, float(frame - 1), float(frame)
                )
                assert (got_ok, got_hold) == (
                    int(expected_frame is not None and frame - 1 < expected_frame <= frame),
                    expected_hold if expected_frame is not None and frame - 1 < expected_frame <= frame else 0,
                )

        escape_n_allow = _allow_interrupt_window(_move_events(moves, "ftCo_SM_EscapeN"))
        escape_air_cmd0 = _cmd0_window(_move_events(moves, "ftCo_SM_EscapeAir"), open_end=True)
        dash_cmd0 = _cmd0_window(_move_events(moves, "ftCo_SM_Dash"), open_end=True)
        runbrake_cmd0 = _cmd0_window(_move_events(moves, "ftCo_SM_RunBrake"), open_end=True)
        turnrun_cmd1 = _cmd_var_window(_move_events(moves, "ftCo_SM_TurnRun"), 1, open_end=True)
        catch_attack = _move_events(moves, "ftCo_SM_CatchAttack")
        catch_on = min(
            [
                int(ev["frame"])
                for ev in catch_attack
                if ev.get("kind") == "create_hitbox"
                and ev.get("data", {}).get("hitbox", {}).get("only_hit_grabbed")
            ],
            default=None,
        )
        catch_off = min(
            [int(ev["frame"]) for ev in catch_attack if ev.get("kind") == "clear_hitboxes" and catch_on is not None and int(ev["frame"]) >= catch_on],
            default=(catch_on + 1 if catch_on is not None else None),
        )
        for frame in range(0, 90):
            assert msl_binding.move_tables_debug_query(
                "escape_allow_interrupt", char_id, 0x00EB, float(frame), 0.0
            ) == _active(escape_n_allow, float(frame))
            assert msl_binding.move_tables_debug_query(
                "escapeair_cmd0", char_id, 0, float(frame), 0.0
            ) == _active(escape_air_cmd0, float(frame))
            assert msl_binding.move_tables_debug_query("dash_cmd0", char_id, 0, float(frame), 0.0) == _active(
                dash_cmd0, float(frame)
            )
            assert msl_binding.move_tables_debug_query(
                "runbrake_cmd0", char_id, 0, float(frame), 0.0
            ) == _active(runbrake_cmd0, float(frame))
            assert msl_binding.move_tables_debug_query(
                "turnrun_cmd1", char_id, 0, float(frame), 0.0
            ) == _active(turnrun_cmd1, float(frame))
            assert msl_binding.move_tables_debug_query(
                "catchattack_grabbed_hit", char_id, 0, float(frame), 0.0
            ) == _active((catch_on, catch_off) if catch_on is not None and catch_off is not None else None, float(frame))

        for action_id, move_name in ((0x002C, "ftCo_SM_Attack11"), (0x002D, "ftCo_SM_Attack12")):
            events = _move_events(moves, move_name)
            combo = _jab_combo_window(events)
            rapid = _jab_rapid_window(events)
            for frame in range(0, 80):
                assert msl_binding.move_tables_debug_query(
                    "jab_combo", char_id, action_id, float(frame), 0.0
                ) == _active(combo, float(frame))
                assert msl_binding.move_tables_debug_query(
                    "jab_rapid", char_id, action_id, float(frame), 0.0
                ) == _active(rapid, float(frame))

        attack100_frames = [
            int(ev["frame"])
            for ev in _move_events(moves, "ftCo_SM_Attack100Loop")
            if ev.get("kind") == "set_throw_flags" and int(ev.get("data", {}).get("hit_idx", -1)) == 0
        ]
        escapef_frame = _throw_flags_start(_move_events(moves, "ftCo_SM_EscapeF"), hit_idx=0)
        catch_frame = _throw_flags_start(_move_events(moves, "ftCo_SM_Catch"), hit_idx=None)
        catchdash_frame = _throw_flags_start(_move_events(moves, "ftCo_SM_CatchDash"), hit_idx=None)
        for frame in range(0, 90):
            assert msl_binding.move_tables_debug_query(
                "attack100_loop_end", char_id, 0, float(frame - 1), float(frame)
            ) == int(any(frame - 1 < pulse <= frame for pulse in attack100_frames))
            assert msl_binding.move_tables_debug_query(
                "escapef_flip", char_id, 0, float(frame - 1), float(frame)
            ) == int(escapef_frame is not None and frame - 1 < escapef_frame <= frame)
            assert msl_binding.move_tables_debug_query(
                "catchpull_enter_wait", char_id, 0x00D5, float(frame), 0.0
            ) == int(catch_frame is not None and frame >= catch_frame)
            assert msl_binding.move_tables_debug_query(
                "catchpull_enter_wait", char_id, 0x00D7, float(frame), 0.0
            ) == int(catchdash_frame is not None and frame >= catchdash_frame)

        for action_id, move_name in throws:
            events = _move_events(moves, move_name)
            release_frame = _throw_flags_start(events, hit_idx=0)
            flip_frame = _throw_flags_start(events, hit_idx=1)
            cmd1 = _throw_cmd1_window(events)
            projectile_pulses = _throw_projectile_pulses(events)
            hitboxes = _throw_hitboxes(events)
            assert msl_binding.move_tables_throw_has_release(char_id, action_id) == int(
                release_frame is not None
            )
            assert msl_binding.move_tables_throw_release_frame(char_id, action_id) == (
                int(release_frame is not None),
                float(release_frame or 0),
            )
            for hit_idx in range(0, 4):
                assert msl_binding.move_tables_throw_hitbox_params(
                    char_id, action_id, hit_idx
                ) == hitboxes.get(hit_idx)
            assert msl_binding.move_tables_throw_projectile_first_pulse_frame(char_id, action_id) == (
                int(bool(projectile_pulses)),
                min(projectile_pulses) if projectile_pulses else -1,
            )
            assert msl_binding.move_tables_throw_projectile_last_pulse_frame(char_id, action_id) == (
                int(bool(projectile_pulses)),
                max(projectile_pulses) if projectile_pulses else -1,
            )
            for pulse_i, pulse_frame in enumerate(projectile_pulses, start=1):
                assert msl_binding.move_tables_throw_projectile_pulse_ordinal(
                    char_id, action_id, pulse_frame
                ) == (1, pulse_i)
            assert msl_binding.move_tables_throw_projectile_pulse_ordinal(
                char_id, action_id, 99
            ) == (0, -1)
            for frame in range(0, 90):
                released = int(release_frame is not None and frame >= release_frame)
                assert msl_binding.move_tables_throw_release_hit_idx(
                    char_id, action_id, float(frame)
                ) == (released, 0 if released else -1)
                assert msl_binding.move_tables_throw_cmd1_active(
                    char_id, action_id, float(frame)
                ) == _active(cmd1, float(frame))
                crossed = _crossed_frames(projectile_pulses, frame - 1, frame)
                expected_pulse = crossed[0] if crossed else -1
                assert msl_binding.move_tables_throw_should_spawn_projectile(
                    char_id, action_id, float(frame - 1), float(frame)
                ) == int(bool(crossed))
                assert msl_binding.move_tables_throw_crossed_projectile_pulse_frame(
                    char_id, action_id, float(frame - 1), float(frame)
                ) == (int(bool(crossed)), expected_pulse)
                assert msl_binding.move_tables_throw_should_flip_facing(
                    char_id, action_id, float(frame - 1), float(frame)
                ) == int(flip_frame is not None and frame - 1 < flip_frame <= frame)

        special_msids = sorted(int(k) for k in moves["specials_by_msid"].keys())
        for msid in special_msids:
            special_events = _special_events(moves, msid)
            cmd0 = _cmd0_window(special_events, open_end=True)
            # Runtime intentionally keeps a two-frame latch-clear tail after the extracted clear frame.
            if cmd0 is not None and cmd0[1] < 32767:
                cmd0 = (cmd0[0], cmd0[1] + 2)
            cmd2_pulses = _cmd_var_value1_pulses(special_events, 2)
            sfx_ranges = [
                (int(ev["frame"]), int(ev.get("data", {}).get("random_range", 0)))
                for ev in special_events
                if ev.get("kind") == "pseudo_random_sfx"
            ]
            for frame in range(0, 100):
                assert msl_binding.move_tables_debug_query(
                    "special_cmd0", char_id, msid, float(frame), 0.0
                ) == _active(cmd0, float(frame))
                crossed_cmd2 = _crossed_frames(cmd2_pulses, frame - 1, frame)
                expected_cmd2_pulse = crossed_cmd2[0] if crossed_cmd2 else -1
                assert msl_binding.move_tables_debug_query(
                    "special_cmd2_pulse", char_id, msid, float(frame - 1), float(frame)
                ) == (int(bool(crossed_cmd2)), expected_cmd2_pulse)
                expected_ranges = tuple(
                    random_range
                    for pulse_frame, random_range in sfx_ranges
                    if (frame - 1 < pulse_frame <= frame)
                    or (pulse_frame == 0 and frame - 1 == 0 and frame > 0)
                )
                assert msl_binding.move_tables_special_pseudo_random_sfx_ranges_crossed(
                    char_id, msid, float(frame - 1), float(frame), 16
                ) == expected_ranges
            if cmd2_pulses:
                assert msl_binding.move_tables_debug_query(
                    "special_cmd2_pulse", char_id, msid, 10.0, 0.0
                ) == (0, -1)

        script_owner_cases: dict[int, list[dict]] = {}
        relevant_kinds = {
            "set_hit_status",
            "set_all_hurt_state",
            "set_hurt_state",
            "set_airborne_state",
            "set_state_flags_221c_u16_y",
        }
        for move in moves.get("moves", {}).values():
            events = list(move.get("events", []))
            if any(ev.get("kind") in relevant_kinds for ev in events):
                script_owner_cases[int(move["submotion_id"])] = events
        for msid_s, special in moves.get("specials_by_msid", {}).items():
            events = list(special.get("events", []))
            if any(ev.get("kind") in relevant_kinds for ev in events):
                script_owner_cases[int(msid_s)] = events
        for msid, events in sorted(script_owner_cases.items()):
            hit_status, hurt_masks, airborne, flags_221c_y = _script_owner_expected_timelines(
                events, cap_bones
            )
            sample_frames = {0, 1, 238, 239, 240}
            for ev in events:
                if ev.get("kind") in relevant_kinds:
                    frame = int(ev.get("frame", 0))
                    sample_frames.update({max(0, frame - 1), frame, min(240, frame + 1)})
            cap_count = len(cap_bones)
            cap_mask = (1 << cap_count) - 1
            for frame in sorted(sample_frames):
                clamp = min(frame, 239)
                assert msl_binding.move_tables_debug_query(
                    "hit_status", char_id, msid, float(frame), 0.0
                ) == (1, hit_status[clamp])
                assert msl_binding.move_tables_debug_query(
                    "hurtbox_can_hit_mask", char_id, msid, float(frame), float(cap_count)
                ) == (1, hurt_masks[clamp] & cap_mask)
                assert msl_binding.move_tables_debug_query(
                    "state_flags_221c_y", char_id, msid, float(frame), 0.0
                ) == (1, flags_221c_y[clamp])
                expected_air = airborne[frame] if frame < 240 else -1
                assert msl_binding.move_tables_debug_query(
                    "airborne_state_event", char_id, msid, float(frame), 0.0
                ) == (int(expected_air >= 0), expected_air)


@pytest.mark.parametrize(
    ("magic", "version", "reader", "match"),
    [
        (STAGE_MAGIC, STAGE_VERSION, read_mslstg01_v7, "unsupported MSLSTG01 version"),
        (PART_MAGIC, PART_VERSION, read_mslpart1_v1, "unsupported MSLPART1 version"),
        (ITEM_ARTICLE_MAGIC, ITEM_ARTICLE_VERSION, read_mslitar1, "unsupported MSLITAR1 version"),
        (
            STAGE_ITEM_OBJECT_MAGIC,
            STAGE_ITEM_OBJECT_VERSION,
            read_mslstio1_yoshi_shyguy,
            "unsupported MSLSTIO1 version",
        ),
        (DREAM_WHISPY_MAGIC, DREAM_WHISPY_VERSION, read_mslwhsp1, "unsupported MSLWHSP1 version"),
        (SCRIPT_MAGIC, SCRIPT_LEGACY_JSON_VERSION, read_mslftsc1_v1, "unsupported MSLFTSC1 version"),
    ],
)
def test_known_data_artifact_readers_reject_stale_versions(tmp_path: Path, magic, version, reader, match) -> None:
    path = tmp_path / "stale.bin"
    buf = bytearray(84 if magic == STAGE_ITEM_OBJECT_MAGIC else 64)
    buf[:8] = magic
    struct.pack_into("<I", buf, 8, version - 1)
    path.write_bytes(bytes(buf))
    with pytest.raises(ValueError, match=match):
        reader(path)


def test_mslstio1_rejects_zero_hurtbox_contract(tmp_path: Path) -> None:
    path = tmp_path / "zero_hurtboxes.bin"
    buf = bytearray(84)
    buf[:8] = STAGE_ITEM_OBJECT_MAGIC
    struct.pack_into("<I", buf, 8, STAGE_ITEM_OBJECT_VERSION)
    struct.pack_into("<HHH", buf, 16, 6, 3, 128)
    struct.pack_into("<H", buf, 82, 0)
    path.write_bytes(bytes(buf))
    with pytest.raises(ValueError, match="invalid hurtbox_count"):
        read_mslstio1_yoshi_shyguy(path)

    from tools.extraction.extract_stage_item_objects import _write_bin

    with pytest.raises(ValueError, match="unexpected Shy Guy table dimensions"):
        _write_bin(
            tmp_path / "zero_hurtboxes_writer.bin",
            {
                "stage_id": 8,
                "item_kind": 0xD2,
                "timer_min": 1,
                "timer_rand": 1,
                "timer_reset": 120,
                "spawnmany_rarity": 1,
                "spawn_delay_step": 25,
                "fall_accel": 0.1,
                "fall_speed_max": 1.0,
                "damage_mul": 2.0,
                "spawn_left_x": -292.0,
                "spawn_right_x": 304.0,
                "state4_speed_mul": 1.5,
                "jitter_y_amp": 3.0,
                "damage_threshold": 15,
                "collision_ecb": {
                    "up": 6.0,
                    "down": 6.0,
                    "right": 8.0,
                    "left": 8.0,
                    "scale": 0.85,
                },
                "hurtboxes": [],
                "vpos": [0.0] * 6,
                "speed": [1.0] * 3,
                "dyn_y_vel": [0.0] * 128,
            },
        )


@pytest.mark.integration
def test_known_data_artifact_extractors_regenerate_stable_outputs(tmp_path: Path) -> None:
    subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.extraction.extract_stage_metadata",
            "--dat",
            "_iso/GrNLa.dat",
            "--out",
            str(tmp_path / "grnla.bin"),
            "--audit",
            str(tmp_path / "grnla.json"),
        ],
        check=True,
    )
    assert (tmp_path / "grnla.bin").read_bytes() == Path("data/stages/bin/grnla.bin").read_bytes()

    subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.extraction.extract_fighter_script_timeline",
            "--moves",
            "data/moves/fox.json",
            "--character",
            "fox",
            "--iso_dir",
            "_iso",
            "--melee_decomp",
            "refs/melee",
            "--special_msids_dir",
            "data/special_msids",
            "--out",
            str(tmp_path / "fox_scripts.bin"),
            "--manifest",
            str(tmp_path / "fox_scripts.json"),
        ],
        check=True,
    )
    assert (tmp_path / "fox_scripts.bin").read_bytes() == Path("data/scripts/fox.bin").read_bytes()

    subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.extraction.extract_fighter_parts",
            "--character",
            "fox",
            "--attrs",
            "data/characters/fox.json",
            "--tracks",
            "data/anims/fox.tracks.bin",
            "--out",
            str(tmp_path / "fox_parts.bin"),
        ],
        check=True,
    )
    assert (tmp_path / "fox_parts.bin").read_bytes() == Path("data/model_parts/fox.bin").read_bytes()

    subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.extraction.extract_item_articles",
            "--attrs-dir",
            "data/characters",
            "--item-common",
            "data/items/item_common.json",
            "--out",
            str(tmp_path / "articles.bin"),
        ],
        check=True,
    )
    assert (tmp_path / "articles.bin").read_bytes() == Path("data/items/articles/fox_falco.bin").read_bytes()

    subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.extraction.extract_stage_item_objects",
            "--grst",
            "_iso/GrSt.dat",
            "--grop",
            "_iso/GrOp.dat",
            "--out",
            str(tmp_path / "yoshi_shyguy.bin"),
            "--audit",
            str(tmp_path / "yoshi_shyguy.json"),
            "--dream-out",
            str(tmp_path / "dream_whispy.bin"),
            "--dream-audit",
            str(tmp_path / "dream_whispy.json"),
        ],
        check=True,
    )
    assert (tmp_path / "yoshi_shyguy.bin").read_bytes() == Path(
        "data/stage_items/yoshi_shyguy.bin"
    ).read_bytes()
    assert (tmp_path / "dream_whispy.bin").read_bytes() == Path(
        "data/stage_items/dream_whispy.bin"
    ).read_bytes()
