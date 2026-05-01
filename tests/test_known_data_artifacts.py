from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
from pathlib import Path

import pytest

from tools.extraction.extract_fighter_parts import ANCHOR_IDS
from tools.extraction.extract_fighter_script_timeline import EVENT_IDS
from tools.extraction.extract_item_articles import FIELD_SPECS, UNIT_DEGREES, UNIT_FRAMES, UNIT_ITEM_KIND, UNIT_PART_ID
from tools.slippi.item_article_data import (
    SIM_CHAR_TO_GALE01_FIGHTER_KIND,
    item_article_kind_set,
    item_article_values_by_sim_char,
)
from tools.slippi.known_data_artifacts import (
    ITEM_ARTICLE_CHAR_DOMAIN_GALE01_FIGHTER_KIND,
    ITEM_ARTICLE_MAGIC,
    ITEM_ARTICLE_VALUE_F32,
    ITEM_ARTICLE_VALUE_U16,
    ITEM_ARTICLE_VALUE_U32,
    ITEM_ARTICLE_VERSION,
    PART_MAGIC,
    PART_VERSION,
    SCRIPT_MAGIC,
    SCRIPT_VERSION,
    STAGE_MAGIC,
    STAGE_VERSION,
    read_mslftsc1_v1,
    read_mslitar1,
    read_mslpart1_v1,
    read_mslstg01_v2,
    stage_metadata_bin_name_for_stage_id,
    stage_metadata_path_for_stage_id,
)
from tools.slippi.make_dataset_from_slp import _load_stage_segments_for_seed


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


@pytest.mark.integration
def test_stage_metadata_registry_covers_supported_domain() -> None:
    assert stage_metadata_bin_name_for_stage_id(31) == "grnba.bin"
    assert stage_metadata_bin_name_for_stage_id(32) == "grnla.bin"
    assert stage_metadata_bin_name_for_stage_id(8) is None
    assert stage_metadata_path_for_stage_id(31, Path("data")) == Path("data/stages/bin/grnba.bin")
    assert stage_metadata_path_for_stage_id(32, Path("data")) == Path("data/stages/bin/grnla.bin")
    assert stage_metadata_path_for_stage_id(8, Path("data")) is None


@pytest.mark.integration
def test_stage_metadata_fd_known_rows() -> None:
    stage = read_mslstg01_v2(Path("data/stages/bin/grnla.bin"))
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
    assert seg0.flags == 0x2  # ledge
    assert seg0.hi_flags == 1
    assert seg0.lo_flags == 0x200
    assert (seg0.x0, seg0.y0, seg0.x1, seg0.y1) == pytest.approx((-85.5656967, 0.0, -75.0, 0.0))
    assert (stage.stage_points[1].x, stage.stage_points[1].y, stage.stage_points[1].z) == pytest.approx(
        (0.0, 12.0, 0.0)
    )


@pytest.mark.integration
def test_stage_metadata_segments_match_legacy_fd_json() -> None:
    stage = read_mslstg01_v2(Path("data/stages/bin/grnla.bin"))
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
    stage = read_mslstg01_v2(Path("data/stages/bin/grnba.bin"))
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


def test_runtime_stage_collision_rejects_stale_mslstg01(tmp_path: Path) -> None:
    root_data = Path("data").resolve()
    data_dir = _symlink_data_tree_with_private_dirs(tmp_path, ("stages",))
    (data_dir / "stages" / "bin").mkdir(parents=True)
    buf = bytearray((root_data / "stages" / "bin" / "grnla.bin").read_bytes())
    buf[8:12] = (STAGE_VERSION - 1).to_bytes(4, "little")
    (data_dir / "stages" / "bin" / "grnla.bin").write_bytes(bytes(buf))
    (data_dir / "stages" / "bin" / "grnba.bin").write_bytes(
        (root_data / "stages" / "bin" / "grnba.bin").read_bytes()
    )

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


def test_runtime_stage_collision_requires_registered_battlefield_artifact(tmp_path: Path) -> None:
    root_data = Path("data").resolve()
    data_dir = _symlink_data_tree_with_private_dirs(tmp_path, ("stages",))
    (data_dir / "stages" / "bin").mkdir(parents=True)
    (data_dir / "stages" / "bin" / "grnla.bin").write_bytes(
        (root_data / "stages" / "bin" / "grnla.bin").read_bytes()
    )

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


def test_runtime_stage_collision_reads_mslstg01_segments(tmp_path: Path) -> None:
    root_data = Path("data").resolve()
    data_dir = _symlink_data_tree_with_private_dirs(tmp_path, ("stages",))
    (data_dir / "stages" / "bin").mkdir(parents=True)
    stage = read_mslstg01_v2(root_data / "stages" / "bin" / "grnla.bin")
    record_i, floor_seg = next(
        (i, seg) for i, seg in enumerate(stage.segments) if int(seg.kind_id) == 0 and int(seg.line_id) == 1
    )
    buf = bytearray((root_data / "stages" / "bin" / "grnla.bin").read_bytes())
    # Segment record layout: <HBBHHffff>, records start after the 56-byte MSLSTG01 header.
    rec_off = 56 + record_i * 24
    struct.pack_into("<f", buf, rec_off + 12, 5.0)
    struct.pack_into("<f", buf, rec_off + 20, 5.0)
    (data_dir / "stages" / "bin" / "grnla.bin").write_bytes(bytes(buf))
    (data_dir / "stages" / "bin" / "grnba.bin").write_bytes(
        (root_data / "stages" / "bin" / "grnba.bin").read_bytes()
    )

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
raise SystemExit(0)
"""
    proc = subprocess.run([sys.executable, "-c", code], text=True, capture_output=True)
    assert proc.returncode == 0, proc.stderr + proc.stdout


def test_runtime_stage_collision_reads_match_flow_roles_from_mslstg01(tmp_path: Path) -> None:
    root_data = Path("data").resolve()
    data_dir = _symlink_data_tree_with_private_dirs(tmp_path, ("stages",))
    (data_dir / "stages" / "bin").mkdir(parents=True)
    (data_dir / "stages" / "bin" / "grnla.bin").write_bytes(
        (root_data / "stages" / "bin" / "grnla.bin").read_bytes()
    )
    (data_dir / "stages" / "bin" / "grnba.bin").write_bytes(
        (root_data / "stages" / "bin" / "grnba.bin").read_bytes()
    )

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


def _allow_interrupt_window(events: list[dict]) -> tuple[int, int] | None:
    for ev in events:
        if ev.get("kind") == "allow_interrupt":
            return (int(ev["frame"]), 32767)
    return None


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
        for action_id, move_name in attackair:
            events = _move_events(moves, move_name)
            cmd0 = _cmd0_window(events, open_end=False)
            allow = _allow_interrupt_window(events)
            for frame in range(0, 80):
                assert msl_binding.move_tables_debug_query(
                    "attackair_cmd0", char_id, action_id, float(frame), 0.0
                ) == _active(cmd0, float(frame))
                assert msl_binding.move_tables_debug_query(
                    "attackair_allow_interrupt", char_id, action_id, float(frame), 0.0
                ) == _active(allow, float(frame))

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
            cmd0 = _cmd0_window(_special_events(moves, msid), open_end=True)
            # Runtime intentionally keeps a two-frame latch-clear tail after the extracted clear frame.
            if cmd0 is not None and cmd0[1] < 32767:
                cmd0 = (cmd0[0], cmd0[1] + 2)
            sfx_ranges = [
                (int(ev["frame"]), int(ev.get("data", {}).get("random_range", 0)))
                for ev in _special_events(moves, msid)
                if ev.get("kind") == "pseudo_random_sfx"
            ]
            for frame in range(0, 100):
                assert msl_binding.move_tables_debug_query(
                    "special_cmd0", char_id, msid, float(frame), 0.0
                ) == _active(cmd0, float(frame))
                expected_ranges = tuple(
                    random_range
                    for pulse_frame, random_range in sfx_ranges
                    if (frame - 1 < pulse_frame <= frame)
                    or (pulse_frame == 0 and frame - 1 == 0 and frame > 0)
                )
                assert msl_binding.move_tables_special_pseudo_random_sfx_ranges_crossed(
                    char_id, msid, float(frame - 1), float(frame), 16
                ) == expected_ranges


@pytest.mark.parametrize(
    ("magic", "version", "reader", "match"),
    [
        (STAGE_MAGIC, STAGE_VERSION, read_mslstg01_v2, "unsupported MSLSTG01 version"),
        (PART_MAGIC, PART_VERSION, read_mslpart1_v1, "unsupported MSLPART1 version"),
        (ITEM_ARTICLE_MAGIC, ITEM_ARTICLE_VERSION, read_mslitar1, "unsupported MSLITAR1 version"),
        (SCRIPT_MAGIC, SCRIPT_VERSION, read_mslftsc1_v1, "unsupported MSLFTSC1 version"),
    ],
)
def test_known_data_artifact_readers_reject_stale_versions(tmp_path: Path, magic, version, reader, match) -> None:
    path = tmp_path / "stale.bin"
    buf = bytearray(64)
    buf[:8] = magic
    struct.pack_into("<I", buf, 8, version - 1)
    path.write_bytes(bytes(buf))
    with pytest.raises(ValueError, match=match):
        reader(path)


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
