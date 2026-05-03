from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path


STAGE_MAGIC = b"MSLSTG01"
STAGE_VERSION = 7
PART_MAGIC = b"MSLPART1"
PART_VERSION = 1
ITEM_ARTICLE_MAGIC = b"MSLITAR1"
ITEM_ARTICLE_VERSION = 2
STAGE_ITEM_OBJECT_MAGIC = b"MSLSTIO1"
STAGE_ITEM_OBJECT_VERSION = 2
DREAM_WHISPY_MAGIC = b"MSLWHSP1"
DREAM_WHISPY_VERSION = 1
SCRIPT_MAGIC = b"MSLFTSC1"
SCRIPT_VERSION = 1

ITEM_ARTICLE_CHAR_DOMAIN_GALE01_FIGHTER_KIND = 1
ITEM_ARTICLE_VALUE_U16 = 1
ITEM_ARTICLE_VALUE_U32 = 2
ITEM_ARTICLE_VALUE_F32 = 3

STAGE_FOUNTAIN_OF_DREAMS = 2
STAGE_POKEMON_STADIUM = 3
STAGE_YOSHIS_STORY = 8
STAGE_DREAM_LAND_N64 = 28
STAGE_BATTLEFIELD = 31
STAGE_FINAL_DESTINATION = 32
STAGE_PLATFORM_TRANSFORM_KIND_HEIGHT = 1
STAGE_PLATFORM_MOTION_KIND_FOD = 1

STAGE_METADATA_BIN_BY_STAGE_ID = {
    STAGE_FOUNTAIN_OF_DREAMS: "griz.bin",  # Fountain of Dreams / GrIz.dat
    STAGE_POKEMON_STADIUM: "grps.bin",  # Pokemon Stadium base / GrPs.dat
    STAGE_YOSHIS_STORY: "grst.bin",  # Yoshi's Story / GrSt.dat
    STAGE_DREAM_LAND_N64: "grop.bin",  # Dream Land N64 / GrOp.dat
    STAGE_BATTLEFIELD: "grnba.bin",  # Battlefield / GrNBa.dat
    STAGE_FINAL_DESTINATION: "grnla.bin",  # Final Destination / GrNLa.dat
}


@dataclass(frozen=True)
class StageMetadata:
    segment_count: int
    stage_point_count: int
    spawn_count: int
    respawn_count: int
    cam_bounds_world: tuple[float, float, float, float]
    blast_bounds_world: tuple[float, float, float, float]
    segments: tuple["StageSegment", ...]
    stage_points: tuple["StagePoint", ...]
    spawn_points: tuple["StagePoint2", ...]
    respawn_points: tuple["StagePoint2", ...]
    platform_transforms: tuple["StagePlatformTransform", ...]
    platform_motions: tuple["StagePlatformMotion", ...]
    platform_paths: tuple["StagePlatformPathFrame", ...]


@dataclass(frozen=True)
class StageSegment:
    line_id: int
    kind_id: int
    flags: int
    fighter_solid: bool
    hi_flags: int
    lo_flags: int
    prev_id0: int
    next_id0: int
    prev_id1: int
    next_id1: int
    x0: float
    y0: float
    x1: float
    y1: float


@dataclass(frozen=True)
class StagePlatformTransform:
    line_id: int
    kind_id: int
    platform_id: int
    x0: float
    x1: float
    y_const: float
    height_coeff: float


@dataclass(frozen=True)
class StagePlatformMotion:
    kind_id: int
    platform_count: int
    home_height: float
    hidden_target_height: float
    max_height: float
    min_visible_height: float
    up_speed: float
    down_speed: float
    wait_min_frames: float
    wait_max_frames: float
    hidden_wait_min_frames: float
    hidden_wait_max_frames: float
    target_delta_min: float
    target_delta_max: float
    bias_below_home: float
    bias_above_home: float
    hidden_weight: float
    stay_weight: float
    move_weight: float


@dataclass(frozen=True)
class StagePlatformPathFrame:
    line_id: int
    frame: int
    x0: float
    y: float
    x1: float


@dataclass(frozen=True)
class StagePoint:
    x: float
    y: float
    z: float


@dataclass(frozen=True)
class StagePoint2:
    x: float
    y: float


def stage_metadata_bin_name_for_stage_id(stage_id: int) -> str | None:
    return STAGE_METADATA_BIN_BY_STAGE_ID.get(int(stage_id))


def stage_metadata_path_for_stage_id(stage_id: int, data_root: Path | str = Path("data")) -> Path | None:
    name = stage_metadata_bin_name_for_stage_id(stage_id)
    if name is None:
        return None
    return Path(data_root) / "stages" / "bin" / name


@dataclass(frozen=True)
class PartMetadata:
    char_id: int
    local_part_count: int
    anchor_count: int
    parts: tuple["PartRecord", ...]
    anchors: tuple["PartAnchor", ...]


@dataclass(frozen=True)
class PartRecord:
    part_id: int
    parent_part_id: int
    jobj_flags: int


@dataclass(frozen=True)
class PartAnchor:
    kind: int
    part_id: int
    aux: int


@dataclass(frozen=True)
class ItemArticleMetadata:
    record_count: int
    records: tuple["ItemArticleRecord", ...]


@dataclass(frozen=True)
class ItemArticleRecord:
    char_id: int
    char_domain: int
    value_type: int
    field_id: int
    unit_id: int
    u32_value: int
    f32_value: float


@dataclass(frozen=True)
class YoshiShyguyMetadata:
    stage_id: int
    item_kind: int
    timer_min: int
    timer_rand: int
    timer_reset: int
    spawnmany_rarity: int
    spawn_delay_step: int
    fall_accel: float
    fall_speed_max: float
    spawn_left_x: float
    spawn_right_x: float
    state4_speed_mul: float
    jitter_y_amp: float
    vpos: tuple[float, ...]
    speed: tuple[float, ...]
    dyn_y_vel: tuple[float, ...]


@dataclass(frozen=True)
class DreamWhispyMetadata:
    stage_id: int
    wind_speed: float
    right_rect_left: float
    right_rect_right: float
    left_rect_left: float
    left_rect_right: float
    rect_bottom: float
    rect_top: float


@dataclass(frozen=True)
class ScriptTimelineMetadata:
    entry_count: int
    event_count: int
    entries: tuple["ScriptTimelineEntry", ...]
    events: tuple["ScriptTimelineEvent", ...]


@dataclass(frozen=True)
class ScriptTimelineEntry:
    msid: int
    first_event: int
    event_count: int


@dataclass(frozen=True)
class ScriptTimelineEvent:
    frame: int
    kind_id: int
    payload: dict


def _require_header(path: Path, magic: bytes, version: int, min_size: int) -> bytes:
    buf = path.read_bytes()
    if len(buf) < min_size:
        raise ValueError(f"{magic.decode('ascii')}: table too small: {path}")
    if buf[:8] != magic:
        raise ValueError(f"bad {magic.decode('ascii')} magic in {path}: {buf[:8]!r}")
    got = struct.unpack_from("<I", buf, 8)[0]
    if got != version:
        raise ValueError(f"unsupported {magic.decode('ascii')} version in {path}: {got}")
    return buf


def read_mslstg01_v7(path: Path) -> StageMetadata:
    buf = _require_header(path, STAGE_MAGIC, STAGE_VERSION, 64)
    (
        segment_count,
        stage_point_count,
        spawn_count,
        respawn_count,
        platform_transform_count,
        platform_transform_record_bytes,
        platform_motion_count,
        platform_motion_record_bytes,
        platform_path_count,
        platform_path_record_bytes,
        cam_l,
        cam_r,
        cam_t,
        cam_b,
        blast_l,
        blast_r,
        blast_t,
        blast_b,
    ) = struct.unpack_from("<HHHHHHHHHHffffffff", buf, 12)
    expected = (
        64
        + segment_count * 32
        + stage_point_count * 12
        + spawn_count * 8
        + respawn_count * 8
        + platform_transform_count * platform_transform_record_bytes
        + platform_motion_count * platform_motion_record_bytes
        + platform_path_count * platform_path_record_bytes
    )
    if len(buf) != expected:
        raise ValueError(f"MSLSTG01 size mismatch in {path}: header-derived {expected} != {len(buf)}")
    off = 64
    segments: list[StageSegment] = []
    for _ in range(segment_count):
        line_id, kind_id, flags, hi_flags, lo_flags, prev_id0, next_id0, prev_id1, next_id1, x0, y0, x1, y1 = (
            struct.unpack_from("<HBBHHhhhhffff", buf, off)
        )
        off += 32
        segments.append(
            StageSegment(
                line_id=int(line_id),
                kind_id=int(kind_id),
                flags=int(flags),
                fighter_solid=bool(int(flags) & 4),
                hi_flags=int(hi_flags),
                lo_flags=int(lo_flags),
                prev_id0=int(prev_id0),
                next_id0=int(next_id0),
                prev_id1=int(prev_id1),
                next_id1=int(next_id1),
                x0=float(x0),
                y0=float(y0),
                x1=float(x1),
                y1=float(y1),
            )
        )
    stage_points: list[StagePoint] = []
    for _ in range(stage_point_count):
        x, y, z = struct.unpack_from("<fff", buf, off)
        off += 12
        stage_points.append(StagePoint(float(x), float(y), float(z)))
    spawn_points: list[StagePoint2] = []
    for _ in range(spawn_count):
        x, y = struct.unpack_from("<ff", buf, off)
        off += 8
        spawn_points.append(StagePoint2(float(x), float(y)))
    respawn_points: list[StagePoint2] = []
    for _ in range(respawn_count):
        x, y = struct.unpack_from("<ff", buf, off)
        off += 8
        respawn_points.append(StagePoint2(float(x), float(y)))
    platform_transforms: list[StagePlatformTransform] = []
    for _ in range(platform_transform_count):
        if platform_transform_record_bytes != 20:
            raise ValueError(
                f"MSLSTG01 unsupported platform transform record size in {path}: {platform_transform_record_bytes}"
            )
        line_id, kind_id, platform_id, x0, x1, y_const, height_coeff = struct.unpack_from(
            "<HBBffff", buf, off
        )
        off += platform_transform_record_bytes
        platform_transforms.append(
            StagePlatformTransform(
                line_id=int(line_id),
                kind_id=int(kind_id),
                platform_id=int(platform_id),
                x0=float(x0),
                x1=float(x1),
                y_const=float(y_const),
                height_coeff=float(height_coeff),
            )
        )
    platform_motions: list[StagePlatformMotion] = []
    for _ in range(platform_motion_count):
        if platform_motion_record_bytes != 72:
            raise ValueError(
                f"MSLSTG01 unsupported platform motion record size in {path}: {platform_motion_record_bytes}"
            )
        (
            kind_id,
            platform_count,
            _reserved,
            home_height,
            hidden_target_height,
            max_height,
            min_visible_height,
            up_speed,
            down_speed,
            wait_min_frames,
            wait_max_frames,
            hidden_wait_min_frames,
            hidden_wait_max_frames,
            target_delta_min,
            target_delta_max,
            bias_below_home,
            bias_above_home,
            hidden_weight,
            stay_weight,
            move_weight,
        ) = struct.unpack_from("<BBHfffffffffffffffff", buf, off)
        off += platform_motion_record_bytes
        platform_motions.append(
            StagePlatformMotion(
                kind_id=int(kind_id),
                platform_count=int(platform_count),
                home_height=float(home_height),
                hidden_target_height=float(hidden_target_height),
                max_height=float(max_height),
                min_visible_height=float(min_visible_height),
                up_speed=float(up_speed),
                down_speed=float(down_speed),
                wait_min_frames=float(wait_min_frames),
                wait_max_frames=float(wait_max_frames),
                hidden_wait_min_frames=float(hidden_wait_min_frames),
                hidden_wait_max_frames=float(hidden_wait_max_frames),
                target_delta_min=float(target_delta_min),
                target_delta_max=float(target_delta_max),
                bias_below_home=float(bias_below_home),
                bias_above_home=float(bias_above_home),
                hidden_weight=float(hidden_weight),
                stay_weight=float(stay_weight),
                move_weight=float(move_weight),
            )
        )
    platform_paths: list[StagePlatformPathFrame] = []
    for _ in range(platform_path_count):
        if platform_path_record_bytes != 16:
            raise ValueError(
                f"MSLSTG01 unsupported platform path record size in {path}: {platform_path_record_bytes}"
            )
        line_id, frame, x0, y, x1 = struct.unpack_from("<HHfff", buf, off)
        off += platform_path_record_bytes
        platform_paths.append(
            StagePlatformPathFrame(
                line_id=int(line_id),
                frame=int(frame),
                x0=float(x0),
                y=float(y),
                x1=float(x1),
            )
        )
    if off != len(buf):
        raise ValueError(f"MSLSTG01 trailing bytes in {path}: parsed {off} != {len(buf)}")
    return StageMetadata(
        segment_count=int(segment_count),
        stage_point_count=int(stage_point_count),
        spawn_count=int(spawn_count),
        respawn_count=int(respawn_count),
        cam_bounds_world=(float(cam_l), float(cam_r), float(cam_t), float(cam_b)),
        blast_bounds_world=(float(blast_l), float(blast_r), float(blast_t), float(blast_b)),
        segments=tuple(segments),
        stage_points=tuple(stage_points),
        spawn_points=tuple(spawn_points),
        respawn_points=tuple(respawn_points),
        platform_transforms=tuple(platform_transforms),
        platform_motions=tuple(platform_motions),
        platform_paths=tuple(platform_paths),
    )


def fountain_of_dreams_default_platform_heights(
    data_root: Path | str = Path("data"),
) -> tuple[float, float]:
    """Return source-backed FoD platform initial heights by Slippi platform id.

    Platform ids follow Slippi `fod_platform` events: 0=right, 1=left. The defaults come from the
    generated MSLSTG01 v7 transform records rather than seed-generation local constants.
    refs/melee/src/melee/gr/grizumi.c::{grIzumi_801CC358,grIzumi_801CCBDC}
    data/stages/bin/griz.bin::MSLSTG01 platform_transforms
    """
    stage_path = stage_metadata_path_for_stage_id(STAGE_FOUNTAIN_OF_DREAMS, data_root)
    if stage_path is None:
        raise ValueError("missing Fountain of Dreams stage metadata path")
    stage = read_mslstg01_v7(stage_path)
    heights: list[float | None] = [None, None]
    for rec in stage.platform_transforms:
        if rec.kind_id != STAGE_PLATFORM_TRANSFORM_KIND_HEIGHT:
            continue
        if 0 <= rec.platform_id < 2:
            heights[int(rec.platform_id)] = float(rec.y_const)
    if heights[0] is None or heights[1] is None:
        raise ValueError(f"{stage_path}: missing FoD platform height defaults")
    return (float(heights[0]), float(heights[1]))


def fountain_of_dreams_platform_motion_params(
    data_root: Path | str = Path("data"),
) -> dict[str, float]:
    """Return generated GrIz platform scheduler constants from `yakumono_param`.

    refs/melee/src/melee/gr/grizumi.c::{FountainParams,grIzumi_801CC358}
    data/stages/bin/griz.bin::MSLSTG01 platform_motions
    """
    stage_path = stage_metadata_path_for_stage_id(STAGE_FOUNTAIN_OF_DREAMS, data_root)
    if stage_path is None:
        raise ValueError("missing Fountain of Dreams stage metadata path")
    stage = read_mslstg01_v7(stage_path)
    for motion in stage.platform_motions:
        if motion.kind_id == STAGE_PLATFORM_MOTION_KIND_FOD:
            return {
                "home_height": motion.home_height,
                "hidden_target_height": motion.hidden_target_height,
                "max_height": motion.max_height,
                "min_visible_height": motion.min_visible_height,
                "up_speed": motion.up_speed,
                "down_speed": motion.down_speed,
                "wait_min_frames": motion.wait_min_frames,
                "wait_max_frames": motion.wait_max_frames,
                "hidden_wait_min_frames": motion.hidden_wait_min_frames,
                "hidden_wait_max_frames": motion.hidden_wait_max_frames,
                "target_delta_min": motion.target_delta_min,
                "target_delta_max": motion.target_delta_max,
                "bias_below_home": motion.bias_below_home,
                "bias_above_home": motion.bias_above_home,
                "hidden_weight": motion.hidden_weight,
                "stay_weight": motion.stay_weight,
                "move_weight": motion.move_weight,
            }
    raise ValueError(f"{stage_path}: missing FoD platform motion params")


def read_mslpart1_v1(path: Path) -> PartMetadata:
    buf = _require_header(path, PART_MAGIC, PART_VERSION, 20)
    char_id, local_part_count, anchor_count, _reserved = struct.unpack_from("<HHHH", buf, 12)
    expected = 20 + local_part_count * 12 + anchor_count * 8
    if len(buf) != expected:
        raise ValueError(f"MSLPART1 size mismatch in {path}: header-derived {expected} != {len(buf)}")
    off = 20
    parts: list[PartRecord] = []
    for _ in range(local_part_count):
        part, parent, flags, _reserved = struct.unpack_from("<HhII", buf, off)
        off += 12
        parts.append(PartRecord(part_id=int(part), parent_part_id=int(parent), jobj_flags=int(flags)))
    anchors: list[PartAnchor] = []
    for _ in range(anchor_count):
        kind, part, aux, _reserved = struct.unpack_from("<HHHH", buf, off)
        off += 8
        anchors.append(PartAnchor(kind=int(kind), part_id=int(part), aux=int(aux)))
    return PartMetadata(
        char_id=int(char_id),
        local_part_count=int(local_part_count),
        anchor_count=int(anchor_count),
        parts=tuple(parts),
        anchors=tuple(anchors),
    )


def read_mslitar1(path: Path) -> ItemArticleMetadata:
    buf = _require_header(path, ITEM_ARTICLE_MAGIC, ITEM_ARTICLE_VERSION, 16)
    (record_count,) = struct.unpack_from("<I", buf, 12)
    expected = 16 + record_count * 24
    if len(buf) != expected:
        raise ValueError(f"MSLITAR1 size mismatch in {path}: header-derived {expected} != {len(buf)}")
    records: list[ItemArticleRecord] = []
    off = 16
    for _ in range(record_count):
        char_id, char_domain, value_type, field_id, unit_id, u32_value, f32_value, _r0, _r1 = struct.unpack_from(
            "<HBBHHIfII", buf, off
        )
        off += 24
        records.append(
            ItemArticleRecord(
                char_id=int(char_id),
                char_domain=int(char_domain),
                value_type=int(value_type),
                field_id=int(field_id),
                unit_id=int(unit_id),
                u32_value=int(u32_value),
                f32_value=float(f32_value),
            )
        )
    return ItemArticleMetadata(record_count=int(record_count), records=tuple(records))


def read_mslstio1_yoshi_shyguy(path: Path) -> YoshiShyguyMetadata:
    buf = _require_header(path, STAGE_ITEM_OBJECT_MAGIC, STAGE_ITEM_OBJECT_VERSION, 56)
    (
        stage_id,
        item_kind,
        vpos_count,
        speed_count,
        dyn_y_count,
        timer_min,
        timer_rand,
        timer_reset,
        spawnmany_rarity,
        spawn_delay_step,
        fall_accel,
        fall_speed_max,
        spawn_left_x,
        spawn_right_x,
        state4_speed_mul,
        jitter_y_amp,
    ) = struct.unpack_from("<HHHHHHHHHHffffff", buf, 12)
    expected = 56 + int(vpos_count) * 4 + int(speed_count) * 4 + int(dyn_y_count) * 4
    if len(buf) != expected:
        raise ValueError(f"MSLSTIO1 size mismatch in {path}: header-derived {expected} != {len(buf)}")
    off = 56
    vpos = struct.unpack_from("<" + "f" * int(vpos_count), buf, off)
    off += int(vpos_count) * 4
    speed = struct.unpack_from("<" + "f" * int(speed_count), buf, off)
    off += int(speed_count) * 4
    dyn_y_vel = struct.unpack_from("<" + "f" * int(dyn_y_count), buf, off)
    off += int(dyn_y_count) * 4
    if off != len(buf):
        raise ValueError(f"MSLSTIO1 trailing bytes in {path}: parsed {off} != {len(buf)}")
    return YoshiShyguyMetadata(
        stage_id=int(stage_id),
        item_kind=int(item_kind),
        timer_min=int(timer_min),
        timer_rand=int(timer_rand),
        timer_reset=int(timer_reset),
        spawnmany_rarity=int(spawnmany_rarity),
        spawn_delay_step=int(spawn_delay_step),
        fall_accel=float(fall_accel),
        fall_speed_max=float(fall_speed_max),
        spawn_left_x=float(spawn_left_x),
        spawn_right_x=float(spawn_right_x),
        state4_speed_mul=float(state4_speed_mul),
        jitter_y_amp=float(jitter_y_amp),
        vpos=tuple(float(x) for x in vpos),
        speed=tuple(float(x) for x in speed),
        dyn_y_vel=tuple(float(x) for x in dyn_y_vel),
    )


def yoshi_shyguy_metadata(data_root: Path | str = Path("data")) -> YoshiShyguyMetadata:
    """Return generated Yoshi's Story Shy Guy stage-owned item data.

    Source data:
    - `_iso/GrSt.dat::yakumono_param`
    - `_iso/GrSt.dat::itemdata` Heiho Article attrs and child-JObj FObjDesc
    refs/melee/src/melee/gr/grstory.c::{reset_shyguy_timer,grStory_801E3418}
    refs/melee/src/melee/it/items/itheiho.c::{it_802D8618,itHeiho_UnkMotion*_Phys,it_802D98C4}
    """
    return read_mslstio1_yoshi_shyguy(Path(data_root) / "stage_items" / "yoshi_shyguy.bin")


def read_mslwhsp1(path: Path) -> DreamWhispyMetadata:
    buf = _require_header(path, DREAM_WHISPY_MAGIC, DREAM_WHISPY_VERSION, 44)
    if len(buf) != 44:
        raise ValueError(f"MSLWHSP1 size mismatch in {path}: expected 44 != {len(buf)}")
    (
        stage_id,
        _reserved,
        wind_speed,
        right_rect_left,
        right_rect_right,
        left_rect_left,
        left_rect_right,
        rect_bottom,
        rect_top,
    ) = struct.unpack_from("<HHfffffff", buf, 12)
    return DreamWhispyMetadata(
        stage_id=int(stage_id),
        wind_speed=float(wind_speed),
        right_rect_left=float(right_rect_left),
        right_rect_right=float(right_rect_right),
        left_rect_left=float(left_rect_left),
        left_rect_right=float(left_rect_right),
        rect_bottom=float(rect_bottom),
        rect_top=float(rect_top),
    )


def dream_whispy_metadata(data_root: Path | str = Path("data")) -> DreamWhispyMetadata:
    """Return generated Dream Land Whispy wind data.

    Source data:
    - `_iso/GrOp.dat::yakumono_param`
    refs/melee/src/melee/gr/groldpupupu.c::{grOldPupupu_802113E0,fn_802112F4}
    refs/melee/src/melee/ft/ftcoll.c::ftColl_GetWindOffsetVec
    """
    return read_mslwhsp1(Path(data_root) / "stage_items" / "dream_whispy.bin")


def read_mslftsc1_v1(path: Path) -> ScriptTimelineMetadata:
    buf = _require_header(path, SCRIPT_MAGIC, SCRIPT_VERSION, 28)
    entry_count, event_count, index_off, event_off = struct.unpack_from("<IIII", buf, 12)
    if index_off != 28:
        raise ValueError(f"MSLFTSC1 bad index offset in {path}: {index_off}")
    expected_min = index_off + entry_count * 12
    if event_off != expected_min:
        raise ValueError(f"MSLFTSC1 bad event offset in {path}: {event_off} != {expected_min}")
    if len(buf) < event_off:
        raise ValueError(f"MSLFTSC1 truncated index in {path}")
    entries: list[ScriptTimelineEntry] = []
    for i in range(entry_count):
        msid, _reserved, first_event, count = struct.unpack_from("<HHiI", buf, index_off + i * 12)
        entries.append(ScriptTimelineEntry(msid=int(msid), first_event=int(first_event), event_count=int(count)))

    off = event_off
    events: list[ScriptTimelineEvent] = []
    for _ in range(event_count):
        if off + 8 > len(buf):
            raise ValueError(f"MSLFTSC1 truncated event header in {path}")
        frame, kind_id, payload_len = struct.unpack_from("<HHI", buf, off)
        off += 8 + int(payload_len)
        payload_start = off - int(payload_len)
        payload = json.loads(buf[payload_start:off].decode("utf-8")) if payload_len else {}
        events.append(ScriptTimelineEvent(frame=int(frame), kind_id=int(kind_id), payload=dict(payload)))
    if off != len(buf):
        raise ValueError(f"MSLFTSC1 trailing bytes in {path}: parsed {off} != {len(buf)}")
    return ScriptTimelineMetadata(
        entry_count=int(entry_count),
        event_count=int(event_count),
        entries=tuple(entries),
        events=tuple(events),
    )


def read_symbol_manifest(path: Path, *, magic: str, version: int) -> dict[int, str]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("magic") != magic or int(payload.get("version", -1)) != version:
        raise ValueError(f"bad {magic} manifest header: {path}")
    return {int(row["id"]): str(row["symbol"]) for row in payload["symbols"]}
