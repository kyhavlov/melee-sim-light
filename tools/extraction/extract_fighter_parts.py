from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from tools.extraction.known_data_artifacts import PART_MAGIC, PART_VERSION


# External (CSS/Slippi) character ids from the central registry.
from tools.extraction.char_registry import CHARS

CHAR_IDS = {
    **{name: info.external_id for name, info in CHARS.items()},
    # Direct extractor support for validation-domain characters that have not
    # yet entered the production data registry.
    "peach": 12,
    "puff": 16,
}

ANCHOR_IDS = {
    "ecb_joint": 1,
    "laser_spawn_joint": 2,
    "reflector_bone": 3,
    "camera_zoom_target": 4,
    "grab_capture_anchor": 5,
}


def _read_track_parts(path: Path) -> list[tuple[int, int, int]]:
    buf = path.read_bytes()
    if len(buf) < 16 or buf[:8] != b"SSANIMT1":
        raise ValueError(f"{path}: bad SSANIMT1 header")
    version, local_count, _anim_count = struct.unpack_from("<IHH", buf, 8)
    if version != 3:
        raise ValueError(f"{path}: unsupported SSANIMT1 version {version}")
    local_off = 16
    parent_off = local_off + local_count
    flags_off = parent_off + local_count * 2
    out: list[tuple[int, int, int]] = []
    for i in range(local_count):
        part = int(buf[local_off + i])
        parent = int(struct.unpack_from("<h", buf, parent_off + i * 2)[0])
        flags = int(struct.unpack_from("<I", buf, flags_off + i * 4)[0])
        out.append((part, parent, flags))
    return out


def _anchors(attrs: dict) -> list[tuple[int, int, int]]:
    out: list[tuple[int, int, int]] = []
    for part in attrs.get("ecb_joints", []):
        out.append((ANCHOR_IDS["ecb_joint"], int(part), 0))
    key_map = {
        "laser_spawn_joint_part_id": "laser_spawn_joint",
        "reflector_bone_id": "reflector_bone",
        "camera_zoom_target_bone_part_id": "camera_zoom_target",
        "grab_capture_anchor_part_id": "grab_capture_anchor",
    }
    for key, name in key_map.items():
        if key in attrs:
            out.append((ANCHOR_IDS[name], int(attrs[key]), 0))
    return out


def _write(out: Path, *, char_id: int, local_parts: list[tuple[int, int, int]], anchors: list[tuple[int, int, int]]) -> None:
    buf = bytearray()
    buf += PART_MAGIC
    buf += struct.pack("<IHHHH", PART_VERSION, int(char_id), len(local_parts), len(anchors), 0)
    for part, parent, flags in local_parts:
        buf += struct.pack("<HhII", int(part) & 0xFFFF, int(parent), int(flags) & 0xFFFF_FFFF, 0)
    for kind, part, aux in anchors:
        buf += struct.pack("<HHHH", int(kind), int(part) & 0xFFFF, int(aux) & 0xFFFF, 0)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bytes(buf))


def main() -> None:
    ap = argparse.ArgumentParser(description="Pack known fighter part/anchor descriptors as MSLPART1.")
    ap.add_argument("--character", choices=sorted(CHAR_IDS), required=True)
    ap.add_argument("--attrs", type=Path, required=True)
    ap.add_argument("--tracks", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--audit", type=Path, default=None)
    args = ap.parse_args()

    attrs = json.loads(args.attrs.read_text(encoding="utf-8"))
    local_parts = _read_track_parts(args.tracks)
    anchors = _anchors(attrs)
    _write(args.out, char_id=CHAR_IDS[args.character], local_parts=local_parts, anchors=anchors)
    if args.audit is not None:
        payload = {
            "magic": PART_MAGIC.decode("ascii"),
            "version": PART_VERSION,
            "character": args.character,
            "local_parts": [
                {"part": part, "parent": parent, "jobj_flags": flags} for (part, parent, flags) in local_parts
            ],
            "anchors": [{"kind": kind, "part": part, "aux": aux} for (kind, part, aux) in anchors],
        }
        args.audit.parent.mkdir(parents=True, exist_ok=True)
        args.audit.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
