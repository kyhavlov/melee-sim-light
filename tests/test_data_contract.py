from __future__ import annotations

import json
import struct
from pathlib import Path

import pytest

from tools.extraction.extract_fighter_anims import validate_ssanimt1_file


def _read_tracks_msids(path: Path) -> list[int]:
    with path.open("rb") as f:
        magic = f.read(8)
        if magic != b"SSANIMT1":
            raise ValueError(f"bad tracks magic: {magic!r}")
        (version,) = struct.unpack("<I", f.read(4))
        if version != 3:
            raise ValueError(f"unsupported tracks version: {version}")
        local_count, anim_count = struct.unpack("<HH", f.read(4))
        f.read(local_count)  # local_parts
        f.read(2 * local_count)  # local_parent
        f.read(4 * local_count)  # local_flags

        msids: list[int] = []
        for _ in range(anim_count):
            (msid,) = struct.unpack("<H", f.read(2))
            f.read(4)  # end_frame
            f.read(1)  # aobj_loop
            f.read(1)  # uses_root_motion
            msids.append(int(msid))
            for _lp in range(local_count):
                part_u8 = f.read(1)
                if not part_u8:
                    raise ValueError("unexpected EOF in tracks parts")
                (n_tracks,) = struct.unpack("<B", f.read(1))
                for _t in range(n_tracks):
                    hdr = f.read(8)
                    if len(hdr) != 8:
                        raise ValueError("unexpected EOF in tracks header")
                    (_obj_type, _frac_value, _frac_slope, _pad, _startframe, length) = struct.unpack("<BBBBHH", hdr)
                    f.read(int(length))
        return msids


@pytest.mark.integration
def test_data_contract_is_self_consistent_if_present() -> None:
    # This is intentionally not a builder test: it validates *existing* local artifacts fast,
    # and skips entirely if they aren't present.
    stage_path = Path("data/stages/final_destination.json")
    common_path = Path("data/common/ft_common_data.json")

    if not stage_path.exists() or not common_path.exists():
        pytest.skip("no local data/ artifacts present")

    stage = json.loads(stage_path.read_text())
    assert stage.get("stage_dat") == "GrNLa.dat"
    assert int(stage.get("line_count", 0)) > 0
    assert isinstance(stage.get("segments"), list) and len(stage["segments"]) > 0

    common = json.loads(common_path.read_text())
    for k in (
        "air_motion_kb_mul",
        "asdi_step_mul",
        "attack_angle_threshold_radians",
        "cliff_drop_stick_threshold",
        "crouch_stick_threshold",
        "guard_special_enable_frames",
        "ottotto_walk_stick_x_threshold",
    ):
        assert k in common

    for ch in ("fox", "falco"):
        moves_path = Path("data/moves") / f"{ch}.json"
        tracks_path = Path("data/anims") / f"{ch}.tracks.bin"
        msids_path = Path("data/special_msids") / f"{ch}.json"

        if not moves_path.exists() or not tracks_path.exists() or not msids_path.exists():
            pytest.skip("missing local moves/anims/special_msids artifacts")

        moves = json.loads(moves_path.read_text())
        assert moves.get("character") == ch
        assert isinstance(moves.get("moves"), dict) and len(moves["moves"]) > 0

        referenced_msids: set[int] = set()
        for mv in moves["moves"].values():
            referenced_msids.add(int(mv["submotion_id"]))
        for k in (moves.get("specials_by_msid") or {}).keys():
            referenced_msids.add(int(k))

        tracks_msids = set(_read_tracks_msids(tracks_path))
        missing = sorted(referenced_msids - tracks_msids)
        assert not missing, f"msids referenced by moves missing from tracks.bin: {missing[:20]}"
        validate_ssanimt1_file(tracks_path)
