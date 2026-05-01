from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path


STAGE_MAGIC = b"MSLSTG01"
STAGE_VERSION = 2
PART_MAGIC = b"MSLPART1"
PART_VERSION = 1
ITEM_ARTICLE_MAGIC = b"MSLITAR1"
ITEM_ARTICLE_VERSION = 2
SCRIPT_MAGIC = b"MSLFTSC1"
SCRIPT_VERSION = 1

ITEM_ARTICLE_CHAR_DOMAIN_GALE01_FIGHTER_KIND = 1
ITEM_ARTICLE_VALUE_U16 = 1
ITEM_ARTICLE_VALUE_U32 = 2
ITEM_ARTICLE_VALUE_F32 = 3

STAGE_BATTLEFIELD = 31
STAGE_FINAL_DESTINATION = 32

STAGE_METADATA_BIN_BY_STAGE_ID = {
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


@dataclass(frozen=True)
class StageSegment:
    line_id: int
    kind_id: int
    flags: int
    hi_flags: int
    lo_flags: int
    x0: float
    y0: float
    x1: float
    y1: float


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


def read_mslstg01_v2(path: Path) -> StageMetadata:
    buf = _require_header(path, STAGE_MAGIC, STAGE_VERSION, 56)
    (
        segment_count,
        stage_point_count,
        spawn_count,
        respawn_count,
        _reserved,
        _reserved2,
        cam_l,
        cam_r,
        cam_t,
        cam_b,
        blast_l,
        blast_r,
        blast_t,
        blast_b,
    ) = struct.unpack_from("<HHHHHHffffffff", buf, 12)
    expected = 56 + segment_count * 24 + stage_point_count * 12 + spawn_count * 8 + respawn_count * 8
    if len(buf) != expected:
        raise ValueError(f"MSLSTG01 size mismatch in {path}: header-derived {expected} != {len(buf)}")
    off = 56
    segments: list[StageSegment] = []
    for _ in range(segment_count):
        line_id, kind_id, flags, hi_flags, lo_flags, x0, y0, x1, y1 = struct.unpack_from("<HBBHHffff", buf, off)
        off += 24
        segments.append(
            StageSegment(
                line_id=int(line_id),
                kind_id=int(kind_id),
                flags=int(flags),
                hi_flags=int(hi_flags),
                lo_flags=int(lo_flags),
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
    )


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
