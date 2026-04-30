from __future__ import annotations

import json
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
    read_mslstg01_v1,
)
from tools.slippi.make_dataset_from_slp import _load_stage_segments_for_seed


@pytest.mark.integration
def test_stage_metadata_fd_known_rows() -> None:
    stage = read_mslstg01_v1(Path("data/stages/bin/grnla.bin"))
    assert stage.segment_count == 16
    assert stage.stage_point_count == 22
    assert stage.spawn_count == 0
    assert stage.respawn_count == 0
    assert stage.cam_bounds_world == pytest.approx((0.0, 0.0, 0.0, 0.0))
    assert stage.blast_bounds_world == pytest.approx((0.0, 0.0, 0.0, 0.0))
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
    assert _load_stage_segments_for_seed(stage_id=31, data_root=Path("data")) == []


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


@pytest.mark.parametrize(
    ("magic", "version", "reader", "match"),
    [
        (STAGE_MAGIC, STAGE_VERSION, read_mslstg01_v1, "unsupported MSLSTG01 version"),
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
