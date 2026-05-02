from __future__ import annotations

import os
import json
import struct
import tempfile
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import read_dataset


def _parse_ssdynn01(path: Path) -> dict[str, object]:
    buf = path.read_bytes()
    if len(buf) < 16:
        raise ValueError("SSDYNN01: truncated header")
    if buf[:8] != b"SSDYNN01":
        raise ValueError(f"SSDYNN01: bad magic {buf[:8]!r}")
    version, set_count, total_nodes = struct.unpack_from("<IHH", buf, 8)
    if version != 4:
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
            if off + 64 > len(buf):
                raise ValueError("SSDYNN01: truncated node")
            part, _pad = struct.unpack_from("<HH", buf, off)
            off += 4
            constants = struct.unpack_from("<15f", buf, off)
            off += 15 * 4
            nodes.append({"part": int(part), "constants": constants})
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
    if off != len(buf):
        raise ValueError("SSDYNN01: trailing bytes")
    return {
        "version": int(version),
        "set_count": int(set_count),
        "total_nodes": int(total_nodes),
        "sets": sets,
        "collision_msids": collision_msids,
    }


def _write_minimal_ssanim(path: Path, *, version: int = 4, msid: int = 2, part_id: int = 0) -> None:
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
        + struct.pack("<HH9f", int(msid) & 0xFFFF, 1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0)
    )


def _write_dyn(
    path: Path,
    sets: list[tuple[int, list[int]]],
    *,
    version: int = 4,
    collision_msids: list[int] | None = None,
) -> None:
    total_nodes = sum(len(parts) for _root, parts in sets)
    buf = bytearray()
    buf += b"SSDYNN01"
    buf += struct.pack("<IHH", int(version), len(sets), total_nodes)
    constants = (1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.7853981852531433, 3.1415927410125732,
                 3.1415927410125732, 3.1415927410125732, -3.1415927410125732,
                 -3.1415927410125732, -3.1415927410125732, 0.008726646192371845,
                 0.05235987901687622)
    for root, parts in sets:
        buf += struct.pack("<HH3f", int(root) & 0xFFFF, len(parts), 1.0, 1.0, 0.04363323003053665)
        for part in parts:
            buf += struct.pack("<HH15f", int(part) & 0xFFFF, 0, *constants)
    if version >= 2:
        owner_msids = sorted({int(msid) & 0xFFFF for msid in (collision_msids or [])})
        buf += struct.pack("<HH", len(owner_msids), 0)
        for msid in owner_msids:
            buf += struct.pack("<H", msid)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(buf))


def _write_truncated_ssanimt1_varint(path: Path, *, msid: int = 2, part_id: int = 0) -> None:
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
    buf += struct.pack("<BBBBHH", 5, 0, 0, 0, 0, 1)  # obj_type, frac*, pad, startframe, len
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
            "Run `uv run python -m tools.extraction.build_data --iso-dir _iso --stages grnla,grnba --chars fox,falco`."
        )


def test_runtime_rejects_stale_ssanim01_v3_artifacts() -> None:
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
    with tempfile.TemporaryDirectory(prefix="ssanim-v3-stale-", dir=build_dir) as tmp_raw:
        data_dir = Path(tmp_raw) / "data"
        _populate_data_dir(data_dir, exclude=exclude)
        _write_minimal_ssanim(data_dir / "anims/fox.bin", version=3)
        _write_minimal_ssanim(data_dir / "anims/falco.bin", version=3)

        old_data_dir = os.environ.get("MSL_DATA_DIR")
        try:
            os.environ["MSL_DATA_DIR"] = str(data_dir)
            msl_binding.debug_reset_pose_and_hitboxes_tables()
            with pytest.raises(MemoryError):
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
    with tempfile.TemporaryDirectory(prefix="ssdynn-v3-stale-", dir=build_dir) as tmp_raw:
        data_dir = Path(tmp_raw) / "data"
        _populate_data_dir(data_dir, exclude=exclude)
        _write_minimal_ssanim(data_dir / "anims/fox.bin")
        _write_minimal_ssanim(data_dir / "anims/falco.bin")
        _write_minimal_locals(data_dir / "anims/fox.locals.bin")
        _write_minimal_locals(data_dir / "anims/falco.locals.bin")
        _write_dyn(data_dir / "anims/fox.dyn.bin", [(17, [17, 18, 19, 20])], version=3, collision_msids=[17, 52, 58])
        _write_dyn(data_dir / "anims/falco.dyn.bin", [], version=3)

        old_data_dir = os.environ.get("MSL_DATA_DIR")
        try:
            os.environ["MSL_DATA_DIR"] = str(data_dir)
            msl_binding.debug_reset_pose_and_hitboxes_tables()
            with pytest.raises(MemoryError):
                msl_binding.init(batch_size=1, num_players=2)
        finally:
            if old_data_dir is None:
                os.environ.pop("MSL_DATA_DIR", None)
            else:
                os.environ["MSL_DATA_DIR"] = old_data_dir
            msl_binding.debug_reset_pose_and_hitboxes_tables()


@pytest.mark.integration
def test_committed_fox_falco_dynamic_contract_matches_supported_loader_surface() -> None:
    paths = [Path("data/anims/fox.dyn.bin"), Path("data/anims/falco.dyn.bin")]
    fox = _parse_ssdynn01_or_skip(paths[0])
    falco = _parse_ssdynn01_or_skip(paths[1])

    assert fox["set_count"] == 1
    assert fox["total_nodes"] == 4
    assert fox["version"] == 4
    assert fox["collision_msids"] == [17, 36, 58]
    fox_set = fox["sets"][0]  # type: ignore[index]
    assert fox_set["root_part"] == 17
    assert fox_set["node_count"] == 4
    assert [n["part"] for n in fox_set["nodes"]] == [17, 18, 19, 20]
    c0 = fox_set["nodes"][0]["constants"]
    assert c0[6] == pytest.approx(0.7853981852531433)
    assert c0[13] == pytest.approx(0.008726646192371845)
    assert c0[14] == pytest.approx(0.05235987901687622)

    assert falco == {"version": 4, "set_count": 0, "total_nodes": 0, "sets": [], "collision_msids": []}


@pytest.mark.integration
def test_extract_fighter_anims_emits_fox_falco_dynamic_contract(tmp_path: Path) -> None:
    required = [
        Path("_iso/PlFx.dat"),
        Path("_iso/PlFxNr.dat"),
        Path("_iso/PlFc.dat"),
        Path("_iso/PlFcNr.dat"),
        Path("_iso/PlCo.dat"),
    ]
    missing = [p for p in required if not p.exists()]
    if missing:
        pytest.skip("missing local _iso assets: " + ", ".join(str(p) for p in missing))

    from tools.extraction.extract_fighter_anims import _read_fighter_dynamics
    from tools.extraction.extract_fighter_anims import _read_rest_srt_and_parents
    from tools.extraction.extract_fighter_anims import _dynamic_collision_owner_msids
    from tools.extraction.extract_fighter_anims import _write_fighter_dynamics_data

    for character in ("fox", "falco"):
        _rest_rot, _rest_scl, _rest_pos, parent_part, _part_flags = _read_rest_srt_and_parents(character)
        dynamic_sets = _read_fighter_dynamics(character)
        moves = json.loads((Path("data") / "moves" / f"{character}.json").read_text())
        _write_fighter_dynamics_data(
            character,
            tmp_path,
            dynamic_sets,
            parent_part,
            collision_msids=_dynamic_collision_owner_msids(character, moves, dynamic_sets),
        )

    fox = _parse_ssdynn01(tmp_path / "fox.dyn.bin")
    falco = _parse_ssdynn01(tmp_path / "falco.dyn.bin")
    assert fox["version"] == 4
    assert fox["set_count"] == 1
    assert fox["total_nodes"] == 4
    assert fox["collision_msids"] == [17, 36, 58]
    assert [n["part"] for n in fox["sets"][0]["nodes"]] == [17, 18, 19, 20]  # type: ignore[index]
    assert falco == {"version": 4, "set_count": 0, "total_nodes": 0, "sets": [], "collision_msids": []}


def test_dynamic_collision_owner_predicate_is_data_driven_not_raw_msid_gate() -> None:
    src = Path("src/anim_pose.c").read_text()
    assert "msid != 52" not in src
    assert "msid == 52" not in src
    assert "msid != 58" not in src
    assert "msid == 58" not in src
    assert "dyn_collision_have_msid[msid]" in src


def test_dynamic_state_validity_is_split_from_collision_matrix_application() -> None:
    state_h = Path("src/state.h").read_text()
    src = Path("src/anim_pose.c").read_text()

    assert "dynamic_pose_state_valid" in state_h
    assert "dynamic_pose_apply_collision_matrix" in state_h
    assert "const uint8_t sequential = batch->state.dynamic_pose_state_valid[idx]" in src
    assert "batch->state.dynamic_pose_state_valid[idx] = 1u;" in src
    assert "batch->state.dynamic_pose_apply_collision_matrix[idx] = apply_collision_pose;" in src
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
        ("rot_x", "<f4", (4,)),
        ("rot_y", "<f4", (4,)),
        ("rot_z", "<f4", (4,)),
        ("pos_x", "<f4", (4,)),
        ("pos_y", "<f4", (4,)),
        ("pos_z", "<f4", (4,)),
        ("axis_x", "<f4", (4,)),
        ("axis_y", "<f4", (4,)),
        ("axis_z", "<f4", (4,)),
        ("angle", "<f4", (4,)),
    ],
    align=False,
)


def _sample_field_bytes(row: np.ndarray, field: str, stride: int) -> np.ndarray:
    return np.frombuffer(row[field].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, stride)


def _dynamic_state_record(binding: object, handle: object, player: int) -> np.void:
    raw = binding.debug_dynamic_pose_state(handle, 0, player)
    return raw.view(_DYNAMIC_POSE_STATE_DTYPE).reshape(-1)[0]


@pytest.mark.integration
def test_fox_attackhi3_dynamic_reseed_reconstruction_matches_sequential_carry_bhh1599() -> None:
    # BHH:1599 exercises Fox AttackHi3's SSDYNN01 collision pose. The non-sequential
    # teacher-forced seed path reconstructs action-local dynamic state by replaying frame 0..N
    # during reseed/pre-combat update; normal rollout carries the same state frame-to-frame.
    dataset_path = Path("datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl")
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")
    _parse_ssdynn01_or_skip(Path("data/anims/fox.dyn.bin"))

    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    prev_record = 1598
    target_record = 1599
    prev_row = samples[prev_record : prev_record + 1]
    target_row = samples[target_record : target_record + 1]
    player = 1

    assert int(prev_row["seed_t"][0]["animation_index"][player]) == 58
    assert int(target_row["seed_t"][0]["animation_index"][player]) == 58
    assert int(target_row["seed_t"][0]["action_frame"][player]) == 3

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    seq_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    nonseq_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(seq_handle, _sample_field_bytes(prev_row, "seed_t", seed_stride))
        binding.step_input(
            seq_handle,
            _sample_field_bytes(prev_row, "prev_input_t", input_stride),
            _sample_field_bytes(prev_row, "input_t", input_stride),
        )
        binding.debug_step_input_pre_combat(
            seq_handle,
            _sample_field_bytes(target_row, "prev_input_t", input_stride),
            _sample_field_bytes(target_row, "input_t", input_stride),
        )
        sequential = _dynamic_state_record(binding, seq_handle, player)

        binding.reseed_seed(nonseq_handle, _sample_field_bytes(target_row, "seed_t", seed_stride))
        binding.debug_step_input_pre_combat(
            nonseq_handle,
            _sample_field_bytes(target_row, "prev_input_t", input_stride),
            _sample_field_bytes(target_row, "input_t", input_stride),
        )
        reconstructed = _dynamic_state_record(binding, nonseq_handle, player)
    finally:
        binding.destroy(seq_handle)
        binding.destroy(nonseq_handle)

    for field in ("player", "char_id", "state_valid", "apply_collision_matrix", "node_count", "msid", "frame"):
        assert int(sequential[field]) == int(reconstructed[field]), field
    assert int(reconstructed["state_valid"]) == 1
    assert int(reconstructed["node_count"]) == 4
    assert int(reconstructed["msid"]) == 58
    for field in ("rot_x", "rot_y", "rot_z", "pos_x", "pos_y", "pos_z", "axis_x", "axis_y", "axis_z", "angle"):
        np.testing.assert_allclose(sequential[field], reconstructed[field], rtol=0.0, atol=1.0e-6, err_msg=field)


@pytest.mark.parametrize(
    ("name", "sets"),
    [
        ("multi_set", [(17, [17]), (18, [18])]),
        ("oversized_chain", [(17, [17, 18, 19, 20, 21])]),
    ],
)
def test_runtime_dynamic_loader_rejects_unsupported_present_files(name: str, sets: list[tuple[int, list[int]]]) -> None:
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
    with tempfile.TemporaryDirectory(prefix=f"dyn-contract-{name}-", dir=build_dir) as tmp_raw:
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
            with pytest.raises(MemoryError):
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
    with tempfile.TemporaryDirectory(prefix="dyn-contract-missing-locals-", dir=build_dir) as tmp_raw:
        data_dir = Path(tmp_raw) / "data"
        _populate_data_dir(data_dir, exclude=exclude)
        _write_minimal_ssanim(data_dir / "anims/fox.bin")
        _write_minimal_ssanim(data_dir / "anims/falco.bin")
        _write_dyn(data_dir / "anims/fox.dyn.bin", [])

        old_data_dir = os.environ.get("MSL_DATA_DIR")
        try:
            os.environ["MSL_DATA_DIR"] = str(data_dir)
            msl_binding.debug_reset_pose_and_hitboxes_tables()
            with pytest.raises(MemoryError):
                msl_binding.init(batch_size=1, num_players=2)
        finally:
            if old_data_dir is None:
                os.environ.pop("MSL_DATA_DIR", None)
            else:
                os.environ["MSL_DATA_DIR"] = old_data_dir
            msl_binding.debug_reset_pose_and_hitboxes_tables()


def test_runtime_dynamic_loader_allows_missing_dyn_and_locals_for_synthetic_pose_only() -> None:
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
    with tempfile.TemporaryDirectory(prefix="dyn-contract-missing-both-", dir=build_dir) as tmp_raw:
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
    with tempfile.TemporaryDirectory(prefix="tracks-contract-truncated-varint-", dir=build_dir) as tmp_raw:
        tracks_path = Path(tmp_raw) / "data" / "anims" / "fox.tracks.bin"
        _write_truncated_ssanimt1_varint(tracks_path)
        with pytest.raises(ValueError, match="corrupt SSANIMT1 FObj payload"):
            validate_ssanimt1_file(tracks_path)
