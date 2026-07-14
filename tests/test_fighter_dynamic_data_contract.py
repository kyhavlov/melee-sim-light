from __future__ import annotations

import os
import struct
import tempfile
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _parse_ssdynn01(path: Path) -> dict[str, object]:
    buf = path.read_bytes()
    if len(buf) < 16:
        raise ValueError("SSDYNN01: truncated header")
    if buf[:8] != b"SSDYNN01":
        raise ValueError(f"SSDYNN01: bad magic {buf[:8]!r}")
    version, set_count, total_nodes = struct.unpack_from("<IHH", buf, 8)
    if version != 9:
        raise ValueError(f"SSDYNN01: bad version {version}")
    off = 16
    sets: list[dict[str, object]] = []
    for _ in range(int(set_count)):
        if off + 16 > len(buf):
            raise ValueError("SSDYNN01: truncated set header")
        root_part, node_count, px, py, pz = struct.unpack_from("<HH3f", buf, off)
        off += 16
        nodes: list[dict[str, object]] = []
        for _node_i in range(int(node_count)):
            if off + 100 > len(buf):
                raise ValueError("SSDYNN01: truncated node")
            part, _pad = struct.unpack_from("<HH", buf, off)
            off += 4
            constants = struct.unpack_from("<15f", buf, off)
            off += 15 * 4
            rest_rot = struct.unpack_from("<3f", buf, off)
            rest_pos = struct.unpack_from("<3f", buf, off + 12)
            rest_scl = struct.unpack_from("<3f", buf, off + 24)
            off += 9 * 4
            nodes.append(
                {
                    "part": int(part),
                    "constants": constants,
                    "rest_rot": rest_rot,
                    "rest_pos": rest_pos,
                    "rest_scl": rest_scl,
                }
            )
        sets.append(
            {
                "root_part": int(root_part),
                "node_count": int(node_count),
                "pos": (float(px), float(py), float(pz)),
                "nodes": nodes,
            }
        )
    collision_msids: list[int] = []
    if off + 4 > len(buf):
        raise ValueError("SSDYNN01: truncated collision owner index")
    collision_msid_count, _reserved = struct.unpack_from("<HH", buf, off)
    off += 4
    for _ in range(int(collision_msid_count)):
        if off + 2 > len(buf):
            raise ValueError("SSDYNN01: truncated collision owner msid")
        (msid,) = struct.unpack_from("<H", buf, off)
        off += 2
        collision_msids.append(int(msid))
    source_step_msids: list[int] = []
    if off + 4 > len(buf):
        raise ValueError("SSDYNN01: truncated source-step owner index")
    source_step_msid_count, _reserved = struct.unpack_from("<HH", buf, off)
    off += 4
    for _ in range(int(source_step_msid_count)):
        if off + 2 > len(buf):
            raise ValueError("SSDYNN01: truncated source-step owner msid")
        (msid,) = struct.unpack_from("<H", buf, off)
        off += 2
        source_step_msids.append(int(msid))
    cone_msids: list[int] = []
    if off + 4 > len(buf):
        raise ValueError("SSDYNN01: truncated cone owner index")
    cone_msid_count, _reserved = struct.unpack_from("<HH", buf, off)
    off += 4
    for _ in range(int(cone_msid_count)):
        if off + 2 > len(buf):
            raise ValueError("SSDYNN01: truncated cone owner msid")
        (msid,) = struct.unpack_from("<H", buf, off)
        off += 2
        cone_msids.append(int(msid))
    catch_grabbable_msids: list[int] = []
    if off + 4 > len(buf):
        raise ValueError("SSDYNN01: truncated catch grabbable owner index")
    catch_grabbable_msid_count, _reserved = struct.unpack_from("<HH", buf, off)
    off += 4
    for _ in range(int(catch_grabbable_msid_count)):
        if off + 2 > len(buf):
            raise ValueError("SSDYNN01: truncated catch grabbable owner msid")
        (msid,) = struct.unpack_from("<H", buf, off)
        off += 2
        catch_grabbable_msids.append(int(msid))
    disabled_msids: list[int] = []
    if off + 4 > len(buf):
        raise ValueError("SSDYNN01: truncated disabled owner index")
    disabled_msid_count, _reserved = struct.unpack_from("<HH", buf, off)
    off += 4
    for _ in range(int(disabled_msid_count)):
        if off + 2 > len(buf):
            raise ValueError("SSDYNN01: truncated disabled owner msid")
        (msid,) = struct.unpack_from("<H", buf, off)
        off += 2
        disabled_msids.append(int(msid))
    if off + 4 > len(buf):
        raise ValueError("SSDYNN01: truncated collider index")
    collider_count, _reserved = struct.unpack_from("<HH", buf, off)
    off += 4
    colliders: list[dict[str, object]] = []
    for _ in range(int(collider_count)):
        if off + 20 > len(buf):
            raise ValueError("SSDYNN01: truncated collider")
        part, _pad, ox, oy, oz, radius = struct.unpack_from("<HH4f", buf, off)
        off += 20
        colliders.append(
            {
                "part": int(part),
                "offset": (float(ox), float(oy), float(oz)),
                "radius": float(radius),
            }
        )
    if off != len(buf):
        raise ValueError("SSDYNN01: trailing bytes")
    return {
        "version": int(version),
        "set_count": int(set_count),
        "total_nodes": int(total_nodes),
        "sets": sets,
        "collision_msids": collision_msids,
        "source_step_msids": source_step_msids,
        "cone_msids": cone_msids,
        "catch_grabbable_msids": catch_grabbable_msids,
        "disabled_msids": disabled_msids,
        "colliders": colliders,
    }


def _write_minimal_ssanim(
    path: Path, *, version: int = 5, msid: int = 2, part_id: int = 0
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    ident = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    path.write_bytes(
        b"SSANIM01"
        + struct.pack(
            "<IHHBHH12f3f",
            int(version),
            1,
            1,
            int(part_id) & 0xFF,
            int(msid),
            1,
            *ident,
            0.0,
            0.0,
            0.0,
        )
    )


def _write_minimal_locals(path: Path, *, msid: int = 2, part_id: int = 17) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"SSANIML1"
        + struct.pack("<IHH", 1, 1, 1)
        + bytes([int(part_id) & 0xFF])
        + struct.pack("<hI", -1, 0)
        + struct.pack(
            "<HH9f", int(msid) & 0xFFFF, 1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0
        )
    )


def _write_dyn(
    path: Path,
    sets: list[tuple[int, list[int]]],
    *,
    version: int = 9,
    collision_msids: list[int] | None = None,
    source_step_msids: list[int] | None = None,
    cone_msids: list[int] | None = None,
    catch_grabbable_msids: list[int] | None = None,
    disabled_msids: list[int] | None = None,
    colliders: list[tuple[int, tuple[float, float, float], float]] | None = None,
) -> None:
    total_nodes = sum(len(parts) for _root, parts in sets)
    buf = bytearray()
    buf += b"SSDYNN01"
    buf += struct.pack("<IHH", int(version), len(sets), total_nodes)
    constants = (
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.7853981852531433,
        3.1415927410125732,
        3.1415927410125732,
        3.1415927410125732,
        -3.1415927410125732,
        -3.1415927410125732,
        -3.1415927410125732,
        0.008726646192371845,
        0.05235987901687622,
    )
    for root, parts in sets:
        buf += struct.pack(
            "<HH3f", int(root) & 0xFFFF, len(parts), 1.0, 1.0, 0.04363323003053665
        )
        for part in parts:
            buf += struct.pack(
                "<HH24f",
                int(part) & 0xFFFF,
                0,
                *constants,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                1.0,
                1.0,
                1.0,
            )
    if version >= 2:
        owner_msids = sorted({int(msid) & 0xFFFF for msid in (collision_msids or [])})
        buf += struct.pack("<HH", len(owner_msids), 0)
        for msid in owner_msids:
            buf += struct.pack("<H", msid)
    if version >= 6:
        source_step_owner_msids = sorted(
            {int(msid) & 0xFFFF for msid in (source_step_msids or [])}
        )
        buf += struct.pack("<HH", len(source_step_owner_msids), 0)
        for msid in source_step_owner_msids:
            buf += struct.pack("<H", msid)
    if version >= 7:
        cone_owner_msids = sorted({int(msid) & 0xFFFF for msid in (cone_msids or [])})
        buf += struct.pack("<HH", len(cone_owner_msids), 0)
        for msid in cone_owner_msids:
            buf += struct.pack("<H", msid)
    if version >= 8:
        catch_owner_msids = sorted(
            {int(msid) & 0xFFFF for msid in (catch_grabbable_msids or [])}
        )
        buf += struct.pack("<HH", len(catch_owner_msids), 0)
        for msid in catch_owner_msids:
            buf += struct.pack("<H", msid)
    if version >= 9:
        disabled_owner_msids = sorted(
            {int(msid) & 0xFFFF for msid in (disabled_msids or [])}
        )
        buf += struct.pack("<HH", len(disabled_owner_msids), 0)
        for msid in disabled_owner_msids:
            buf += struct.pack("<H", msid)
    if version >= 5:
        collider_rows = list(colliders or [])
        buf += struct.pack("<HH", len(collider_rows), 0)
        for part, offset, radius in collider_rows:
            buf += struct.pack(
                "<HH4f", int(part) & 0xFFFF, 0, offset[0], offset[1], offset[2], radius
            )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(buf))


def _write_truncated_ssanimt1_varint(
    path: Path, *, msid: int = 2, part_id: int = 0
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    buf = bytearray()
    buf += b"SSANIMT1"
    buf += struct.pack("<IHH", 3, 1, 1)
    buf += bytes([int(part_id) & 0xFF])
    buf += struct.pack("<hI", -1, 0)
    buf += struct.pack("<HfBB", int(msid) & 0xFFFF, 1.0, 0, 0)
    buf += bytes([int(part_id) & 0xFF, 1])  # part, n_tracks
    # Track payload is one unterminated variable-length pack-info byte. The extractor/data-contract
    # path must reject this before generated SSANIMT1 inputs are accepted.
    # refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
    buf += struct.pack(
        "<BBBBHH", 5, 0, 0, 0, 0, 1
    )  # obj_type, frac*, pad, startframe, len
    buf += bytes([0x81])
    path.write_bytes(bytes(buf))


def _populate_data_dir(dst_data_dir: Path, *, exclude: set[Path]) -> None:
    src_data_dir = Path("data").resolve()
    dst_data_dir.mkdir(parents=True, exist_ok=True)
    for src in src_data_dir.rglob("*"):
        rel = src.relative_to(src_data_dir)
        if rel in exclude:
            continue
        dst = dst_data_dir / rel
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.link(src, dst)
        except OSError:
            dst.write_bytes(src.read_bytes())


def _skip_if_missing(paths: list[Path]) -> None:
    missing = [str(p) for p in paths if not p.exists()]
    if missing:
        pytest.skip("missing local generated data: " + ", ".join(missing))


def _parse_ssdynn01_or_skip(path: Path) -> dict[str, object]:
    if not path.exists():
        pytest.skip(f"missing local generated data: {path}")
    try:
        return _parse_ssdynn01(path)
    except ValueError as exc:
        pytest.skip(
            f"stale or unsupported local dynamic artifact: {path}: {exc}. "
            "Run `uv run python -m tools.extraction.build_data --iso-dir _iso --stages grnla,grnba,griz,grps,grst,grop`."
        )


def test_runtime_rejects_stale_ssanim01_v4_artifacts() -> None:
    import msl_binding

    exclude = {
        Path("anims/fox.bin"),
        Path("anims/fox.locals.bin"),
        Path("anims/fox.dyn.bin"),
        Path("anims/fox.tracks.bin"),
        Path("anims/falco.bin"),
        Path("anims/falco.locals.bin"),
        Path("anims/falco.dyn.bin"),
        Path("anims/falco.tracks.bin"),
    }

    build_dir = Path("build")
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="ssanim-v4-stale-", dir=build_dir
    ) as tmp_raw:
        data_dir = Path(tmp_raw) / "data"
        _populate_data_dir(data_dir, exclude=exclude)
        _write_minimal_ssanim(data_dir / "anims/fox.bin", version=4)
        _write_minimal_ssanim(data_dir / "anims/falco.bin", version=4)

        old_data_dir = os.environ.get("MSL_DATA_DIR")
        try:
            os.environ["MSL_DATA_DIR"] = str(data_dir)
            msl_binding.debug_reset_pose_and_hitboxes_tables()
            with pytest.raises(RuntimeError, match="msl_batch_create failed"):
                msl_binding.init(batch_size=1, num_players=2)
        finally:
            if old_data_dir is None:
                os.environ.pop("MSL_DATA_DIR", None)
            else:
                os.environ["MSL_DATA_DIR"] = old_data_dir
            msl_binding.debug_reset_pose_and_hitboxes_tables()


def test_runtime_rejects_stale_ssdynn01_v3_artifacts() -> None:
    import msl_binding

    exclude = {
        Path("anims/fox.bin"),
        Path("anims/fox.locals.bin"),
        Path("anims/fox.dyn.bin"),
        Path("anims/falco.bin"),
        Path("anims/falco.locals.bin"),
        Path("anims/falco.dyn.bin"),
    }

    build_dir = Path("build")
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="ssdynn-v3-stale-", dir=build_dir
    ) as tmp_raw:
        data_dir = Path(tmp_raw) / "data"
        _populate_data_dir(data_dir, exclude=exclude)
        _write_minimal_ssanim(data_dir / "anims/fox.bin")
        _write_minimal_ssanim(data_dir / "anims/falco.bin")
        _write_minimal_locals(data_dir / "anims/fox.locals.bin")
        _write_minimal_locals(data_dir / "anims/falco.locals.bin")
        _write_dyn(
            data_dir / "anims/fox.dyn.bin",
            [(17, [17, 18, 19, 20])],
            version=3,
            collision_msids=[17, 52, 58],
        )
        _write_dyn(data_dir / "anims/falco.dyn.bin", [], version=3)

        old_data_dir = os.environ.get("MSL_DATA_DIR")
        try:
            os.environ["MSL_DATA_DIR"] = str(data_dir)
            msl_binding.debug_reset_pose_and_hitboxes_tables()
            with pytest.raises(RuntimeError, match="msl_batch_create failed"):
                msl_binding.init(batch_size=1, num_players=2)
        finally:
            if old_data_dir is None:
                os.environ.pop("MSL_DATA_DIR", None)
            else:
                os.environ["MSL_DATA_DIR"] = old_data_dir
            msl_binding.debug_reset_pose_and_hitboxes_tables()


def test_runtime_rejects_nonempty_ssdynn01_source_step_index() -> None:
    import msl_binding

    exclude = {
        Path("anims/fox.bin"),
        Path("anims/fox.locals.bin"),
        Path("anims/fox.dyn.bin"),
        Path("anims/falco.bin"),
        Path("anims/falco.locals.bin"),
        Path("anims/falco.dyn.bin"),
    }

    build_dir = Path("build")
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="ssdynn-source-step-disabled-", dir=build_dir
    ) as tmp_raw:
        data_dir = Path(tmp_raw) / "data"
        _populate_data_dir(data_dir, exclude=exclude)
        _write_minimal_ssanim(data_dir / "anims/fox.bin")
        _write_minimal_ssanim(data_dir / "anims/falco.bin")
        _write_minimal_locals(data_dir / "anims/fox.locals.bin", msid=17)
        _write_minimal_locals(data_dir / "anims/falco.locals.bin")
        _write_dyn(
            data_dir / "anims/fox.dyn.bin",
            [(17, [17, 18, 19, 20])],
            version=6,
            collision_msids=[17],
            source_step_msids=[17],
        )
        _write_dyn(data_dir / "anims/falco.dyn.bin", [], version=6)

        old_data_dir = os.environ.get("MSL_DATA_DIR")
        try:
            os.environ["MSL_DATA_DIR"] = str(data_dir)
            msl_binding.debug_reset_pose_and_hitboxes_tables()
            with pytest.raises(RuntimeError, match="msl_batch_create failed"):
                msl_binding.init(batch_size=1, num_players=2)
        finally:
            if old_data_dir is None:
                os.environ.pop("MSL_DATA_DIR", None)
            else:
                os.environ["MSL_DATA_DIR"] = old_data_dir
            msl_binding.debug_reset_pose_and_hitboxes_tables()


@pytest.mark.integration
def test_committed_fox_falco_dynamic_contract_matches_supported_loader_surface() -> (
    None
):
    paths = [Path("data/anims/fox.dyn.bin"), Path("data/anims/falco.dyn.bin")]
    fox = _parse_ssdynn01_or_skip(paths[0])
    falco = _parse_ssdynn01_or_skip(paths[1])

    assert fox["set_count"] == 1
    assert fox["total_nodes"] == 4
    assert fox["version"] == 9
    assert fox["collision_msids"] == []
    assert fox["source_step_msids"] == []
    assert fox["cone_msids"] == []
    assert fox["catch_grabbable_msids"] == []
    assert fox["disabled_msids"] == [
        2,
        3,
        30,
        31,
        34,
        59,
        68,
        69,
        70,
        71,
        72,
        73,
        74,
        75,
        76,
        77,
        210,
        211,
        239,
    ]
    assert fox["colliders"] == [
        {
            "part": 41,
            "offset": pytest.approx((0.0, 2.0, 0.0)),
            "radius": pytest.approx(3.0),
        }
    ]
    fox_set = fox["sets"][0]  # type: ignore[index]
    assert fox_set["root_part"] == 17
    assert fox_set["node_count"] == 4
    assert [n["part"] for n in fox_set["nodes"]] == [17, 18, 19, 20]
    c0 = fox_set["nodes"][0]["constants"]
    assert c0[6] == pytest.approx(0.7853981852531433)
    assert c0[13] == pytest.approx(0.008726646192371845)
    assert c0[14] == pytest.approx(0.05235987901687622)
    assert fox_set["nodes"][0]["rest_scl"] == pytest.approx((1.0, 1.0, 1.0))

    assert falco == {
        "version": 9,
        "set_count": 0,
        "total_nodes": 0,
        "sets": [],
        "collision_msids": [],
        "source_step_msids": [],
        "cone_msids": [],
        "catch_grabbable_msids": [],
        "disabled_msids": [],
        "colliders": [],
    }


def test_dynamic_collision_owner_predicate_is_data_driven_not_raw_msid_gate() -> None:
    src = Path("src/anim_pose.c").read_text()
    assert "msid != 52" not in src
    assert "msid == 52" not in src
    assert "msid != 58" not in src
    assert "msid == 58" not in src
    assert "MSL_SM_CATCH_DASH" not in src


def test_dynamic_owner_uses_source_animation_flags_not_motion_allowlists() -> None:
    from tools.extraction.extract_fighter_anims import _dynamic_disabled_msids

    assert _dynamic_disabled_msids("fox", [2, 52, 58, 71, 222, 242]) == [2, 71]
    src = Path("src/anim_pose.c").read_text()
    assert "dyn_collision_have_msid[msid]" not in src
    assert "dyn_source_step_have_msid" not in src
    assert "dyn_disabled_have_msid[msid]" in src
    assert "ftCo_8009E7B4" in src


def test_dynamic_state_validity_is_split_from_collision_matrix_application() -> None:
    state_h = Path("src/state.h").read_text()
    src = Path("src/anim_pose.c").read_text()

    assert "dynamic_pose_state_valid" in state_h
    assert "dynamic_pose_apply_collision_matrix" in state_h
    assert "anim_pose_sync_dynamic_ownership" in src
    assert "ftCo_8009CF84,ftCo_8009DD94" in src
    assert "batch->state.dynamic_pose_state_valid[idx] = 1u;" in src
    assert (
        "batch->state.dynamic_pose_apply_collision_matrix[idx] = apply_collision_pose;"
        in src
    )
    assert "batch->state.dynamic_pose_apply_collision_matrix[player_idx]" in src


_DYNAMIC_POSE_STATE_DTYPE = np.dtype(
    [
        ("player", "u1"),
        ("char_id", "u1"),
        ("state_valid", "u1"),
        ("apply_collision_matrix", "u1"),
        ("node_count", "u1"),
        ("_pad0", "V3"),
        ("msid", "<u2"),
        ("frame", "<u2"),
        ("rot_x", "<f4", (16,)),
        ("rot_y", "<f4", (16,)),
        ("rot_z", "<f4", (16,)),
        ("pos_x", "<f4", (16,)),
        ("pos_y", "<f4", (16,)),
        ("pos_z", "<f4", (16,)),
        ("axis_x", "<f4", (16,)),
        ("axis_y", "<f4", (16,)),
        ("axis_z", "<f4", (16,)),
        ("angle", "<f4", (16,)),
    ],
    align=False,
)


def _sample_field_bytes(row: np.ndarray, field: str, stride: int) -> np.ndarray:
    return (
        np.frombuffer(row[field].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, stride)
    )


def _dynamic_state_record(binding: object, handle: object, player: int) -> np.void:
    raw = binding.debug_dynamic_pose_state(handle, 0, player)
    return raw.view(_DYNAMIC_POSE_STATE_DTYPE).reshape(-1)[0]


@pytest.mark.integration
def test_fox_dynamic_reseed_preserves_absolute_world_state_bhh1599() -> None:
    # Normal validation only observes public fighter/contact outputs, so it cannot protect the
    # hidden coordinate contract of DynamicsDesc. BHH:1598..1599 supplies an active Fox chain for
    # this out-of-band binding invariant: translating the fighter must translate the persistent
    # node positions in world space, and reseed must restore that derived state verbatim. Comparing
    # a free-running step to a teacher-forced replay row is not valid here because their visible
    # world positions can already differ.
    # refs/melee/src/melee/lb/lb_00F9.c::{lb_8000FD48,lb_8001044C}
    # refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009CF84,ftCo_8009DD94}
    dataset_path = Path(
        "replays/validation/aggregate_recent/BlondHardHippopotamus.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")
    _parse_ssdynn01_or_skip(Path("data/anims/fox.dyn.bin"))

    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    prev_record = 1598
    target_record = 1599
    seed_rows = samples["seed_t"][prev_record : target_record + 1].copy()
    translated_rows = seed_rows.copy()
    player = 1

    assert int(seed_rows[0]["animation_index"][player]) == 58
    assert int(seed_rows[1]["animation_index"][player]) == 58
    assert int(seed_rows[1]["action_frame"][player]) == 3

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_rows.dtype.itemsize == seed_stride
    translation_x = np.float32(13.0)
    translated_rows["pos_x"][:, player] += translation_x
    for rows in (seed_rows, translated_rows):
        binding.validation_derive_dynamic_pose_buffers(
            rows.view(np.uint8).reshape(len(rows), seed_stride), int(ds.num_players)
        )

    node_count = int(seed_rows[1]["dynamic_pose_seed_node_count_u8"][player])
    assert node_count == 4
    assert np.all(seed_rows["dynamic_pose_seed_valid_u8"][:, player] == 1)
    assert np.all(translated_rows["dynamic_pose_seed_valid_u8"][:, player] == 1)
    for field in ("rot_x", "rot_y", "rot_z", "axis_x", "axis_y", "axis_z", "angle"):
        key = f"dynamic_pose_seed_{field}_f32"
        np.testing.assert_allclose(
            translated_rows[key][:, player, :node_count],
            seed_rows[key][:, player, :node_count],
            rtol=0.0,
            atol=5.0e-4,
            err_msg=field,
        )
    for field, expected_delta in (
        ("pos_x", translation_x),
        ("pos_y", np.float32(0.0)),
        ("pos_z", np.float32(0.0)),
    ):
        key = f"dynamic_pose_seed_{field}_f32"
        np.testing.assert_allclose(
            translated_rows[key][:, player, :node_count]
            - seed_rows[key][:, player, :node_count],
            expected_delta,
            rtol=0.0,
            atol=5.0e-4,
            err_msg=field,
        )

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed(
            handle,
            seed_rows[1:2].view(np.uint8).reshape(1, seed_stride),
        )
        reconstructed = _dynamic_state_record(binding, handle, player)
    finally:
        binding.destroy(handle)

    assert int(reconstructed["state_valid"]) == 1
    assert int(reconstructed["node_count"]) == node_count
    assert int(reconstructed["msid"]) == 58
    for field in (
        "rot_x",
        "rot_y",
        "rot_z",
        "pos_x",
        "pos_y",
        "pos_z",
        "axis_x",
        "axis_y",
        "axis_z",
        "angle",
    ):
        key = f"dynamic_pose_seed_{field}_f32"
        np.testing.assert_array_equal(
            reconstructed[field][:node_count],
            seed_rows[1][key][player, :node_count],
            err_msg=field,
        )


@pytest.mark.integration
def test_fox_cliffattackquick_dynamic_collision_pose_rejects_tail_false_body_sds6237() -> (
    None
):
    # SDS:6237 locks the CliffAttackQuick SSDYNN01 BODY owner:
    # - p0 Fox CliffAttackQuick frame 37 exposes cap12/FtPart-18 near Falco's weak BAir hb2.
    # - Static baked pose admits a false BODY hit; source consumes the live ftData.x2C tail chain
    #   through `ftColl_80078C70 -> lbColl_8000805C`, so the row stays in CliffAttackQuick.
    # - ftData submotion 222 does not carry x594_b3, so the source-wide dynamic chain owns it; no
    #   replay-curated collision-msid allowlist is involved.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AEA4
    # refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    dataset_path = Path(
        "replays/validation/dream_land_recent/ShadyDecimalStarling.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")
    fox_dyn = _parse_ssdynn01_or_skip(Path("data/anims/fox.dyn.bin"))
    assert 222 not in fox_dyn["disabled_msids"]

    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[6237:6238]
    player = 0
    attacker = 1

    assert int(row["seed_t"][0]["action_id"][player]) == 257  # CliffAttackQuick.
    assert int(row["seed_t"][0]["animation_index"][player]) == 222
    assert int(row["seed_t"][0]["action_frame"][player]) == 36
    assert int(row["seed_t"][0]["action_id"][attacker]) == 67  # AttackAirB.

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed(handle, _sample_field_bytes(row, "seed_t", seed_stride))
        binding.debug_step_input_pre_combat(
            handle,
            _sample_field_bytes(row, "prev_input_t", input_stride),
            _sample_field_bytes(row, "input_t", input_stride),
        )
        dyn = _dynamic_state_record(binding, handle, player)
        assert int(dyn["state_valid"]) == 1
        assert int(dyn["apply_collision_matrix"]) == 1
        assert int(dyn["msid"]) == 222
        assert int(dyn["frame"]) == 37

        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    finally:
        binding.destroy(handle)

    ref = row["ref_t1"][0]
    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 257
    assert int(out["hitlag"][player]) == int(ref["hitlag"][player]) == 0
    assert int(out["hitstun"][player]) == int(ref["hitstun"][player]) == 0
    assert float(out["percent"][player]) == pytest.approx(
        float(ref["percent"][player]), abs=1e-5
    )


@pytest.mark.parametrize(
    ("name", "sets"),
    [
        ("multi_set", [(17, [17]), (18, [18])]),
        ("oversized_chain", [(17, [17, 18, 19, 20, 21])]),
    ],
)
def test_runtime_dynamic_loader_rejects_unsupported_present_files(
    name: str, sets: list[tuple[int, list[int]]]
) -> None:
    import msl_binding

    exclude = {
        Path("anims/fox.bin"),
        Path("anims/fox.locals.bin"),
        Path("anims/fox.dyn.bin"),
        Path("anims/falco.bin"),
        Path("anims/falco.locals.bin"),
        Path("anims/falco.dyn.bin"),
    }

    build_dir = Path("build")
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=f"dyn-contract-{name}-", dir=build_dir
    ) as tmp_raw:
        data_dir = Path(tmp_raw) / "data"
        _populate_data_dir(data_dir, exclude=exclude)
        _write_minimal_ssanim(data_dir / "anims/fox.bin")
        _write_minimal_ssanim(data_dir / "anims/falco.bin")
        _write_minimal_locals(data_dir / "anims/fox.locals.bin")
        _write_minimal_locals(data_dir / "anims/falco.locals.bin")
        _write_dyn(data_dir / "anims/fox.dyn.bin", sets)
        _write_dyn(data_dir / "anims/falco.dyn.bin", [])

        old_data_dir = os.environ.get("MSL_DATA_DIR")
        try:
            os.environ["MSL_DATA_DIR"] = str(data_dir)
            msl_binding.debug_reset_pose_and_hitboxes_tables()
            with pytest.raises(RuntimeError, match="msl_batch_create failed"):
                msl_binding.init(batch_size=1, num_players=2)
        finally:
            if old_data_dir is None:
                os.environ.pop("MSL_DATA_DIR", None)
            else:
                os.environ["MSL_DATA_DIR"] = old_data_dir
            msl_binding.debug_reset_pose_and_hitboxes_tables()


def test_runtime_dynamic_loader_rejects_present_dyn_without_locals() -> None:
    import msl_binding

    exclude = {
        Path("anims/fox.bin"),
        Path("anims/fox.locals.bin"),
        Path("anims/fox.dyn.bin"),
        Path("anims/falco.bin"),
        Path("anims/falco.locals.bin"),
        Path("anims/falco.dyn.bin"),
    }

    build_dir = Path("build")
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="dyn-contract-missing-locals-", dir=build_dir
    ) as tmp_raw:
        data_dir = Path(tmp_raw) / "data"
        _populate_data_dir(data_dir, exclude=exclude)
        _write_minimal_ssanim(data_dir / "anims/fox.bin")
        _write_minimal_ssanim(data_dir / "anims/falco.bin")
        _write_dyn(data_dir / "anims/fox.dyn.bin", [])

        old_data_dir = os.environ.get("MSL_DATA_DIR")
        try:
            os.environ["MSL_DATA_DIR"] = str(data_dir)
            msl_binding.debug_reset_pose_and_hitboxes_tables()
            with pytest.raises(RuntimeError, match="msl_batch_create failed"):
                msl_binding.init(batch_size=1, num_players=2)
        finally:
            if old_data_dir is None:
                os.environ.pop("MSL_DATA_DIR", None)
            else:
                os.environ["MSL_DATA_DIR"] = old_data_dir
            msl_binding.debug_reset_pose_and_hitboxes_tables()


def test_runtime_dynamic_loader_allows_missing_dyn_and_locals_for_synthetic_pose_only() -> (
    None
):
    import msl_binding

    exclude = {
        Path("anims/fox.bin"),
        Path("anims/fox.locals.bin"),
        Path("anims/fox.dyn.bin"),
        Path("anims/falco.bin"),
        Path("anims/falco.locals.bin"),
        Path("anims/falco.dyn.bin"),
    }

    build_dir = Path("build")
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="dyn-contract-missing-both-", dir=build_dir
    ) as tmp_raw:
        data_dir = Path(tmp_raw) / "data"
        _populate_data_dir(data_dir, exclude=exclude)
        _write_minimal_ssanim(data_dir / "anims/fox.bin")
        _write_minimal_ssanim(data_dir / "anims/falco.bin")

        old_data_dir = os.environ.get("MSL_DATA_DIR")
        handle = None
        try:
            os.environ["MSL_DATA_DIR"] = str(data_dir)
            msl_binding.debug_reset_pose_and_hitboxes_tables()
            handle = msl_binding.init(batch_size=1, num_players=2)
        finally:
            if handle is not None:
                msl_binding.destroy(handle)
            if old_data_dir is None:
                os.environ.pop("MSL_DATA_DIR", None)
            else:
                os.environ["MSL_DATA_DIR"] = old_data_dir
            msl_binding.debug_reset_pose_and_hitboxes_tables()


def test_tracks_data_contract_rejects_truncated_variable_length_records() -> None:
    from tools.extraction.extract_fighter_anims import validate_ssanimt1_file

    build_dir = Path("build")
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="tracks-contract-truncated-varint-", dir=build_dir
    ) as tmp_raw:
        tracks_path = Path(tmp_raw) / "data" / "anims" / "fox.tracks.bin"
        _write_truncated_ssanimt1_varint(tracks_path)
        with pytest.raises(ValueError, match="corrupt SSANIMT1 FObj payload"):
            validate_ssanimt1_file(tracks_path)
