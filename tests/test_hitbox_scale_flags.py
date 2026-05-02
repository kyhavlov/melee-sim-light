from __future__ import annotations

import os
import struct
import tempfile
from pathlib import Path

import numpy as np
import json

from tools.eval.dataset import INPUT_DTYPE, SEED_DTYPE


def _write_minimal_ssanim_v4(path: Path, *, msid: int, part_id: int, mtx34: list[float]) -> None:
    # Format matches src/anim_pose.c (SSANIM01 v4).
    path.parent.mkdir(parents=True, exist_ok=True)
    buf = bytearray()
    buf += b"SSANIM01"
    buf += struct.pack("<IHH", 4, 1, 1)  # ver=4, joint_count=1, anim_count=1
    buf += struct.pack("<B", int(part_id) & 0xFF)  # joint_parts[1]
    buf += struct.pack("<HH", int(msid) & 0xFFFF, 1)  # anim header: msid, frame_count=1
    buf += struct.pack("<12f", *[float(x) for x in mtx34])  # frame0/joint0 matrix
    buf += struct.pack("<3f", 0.0, 0.0, 0.0)  # TransN tail (unused by anim_pose_get_matrix)
    path.write_bytes(bytes(buf))


def _write_minimal_mslhitb1(
    path: Path, *, msid: int, records: list[dict]
) -> None:
    # Format matches src/hitboxes_tables.c (MSLHITB1 v1).
    rec_bytes = 44
    idx_bytes = 12
    path.parent.mkdir(parents=True, exist_ok=True)

    entry_count = 1
    index_base = 16
    payload_off = index_base + entry_count * idx_bytes
    payload_bytes = len(records) * rec_bytes

    buf = bytearray()
    buf += b"MSLHITB1"
    buf += struct.pack("<II", 1, entry_count)  # version, entry_count
    buf += struct.pack("<HHII", int(msid) & 0xFFFF, len(records), payload_bytes, payload_off)
    for r in records:
        buf += struct.pack(
            "<HBBIfffff8H",
            int(r["frame"]) & 0xFFFF,
            int(r["kind"]) & 0xFF,
            int(r["hitbox_id"]) & 0xFF,
            int(r["bone_part_id"]) & 0xFFFFFFFF,
            float(r["x"]),
            float(r["y"]),
            float(r["z"]),
            float(r["radius"]),
            float(r["damage"]),
            int(r.get("u16_0", 0)) & 0xFFFF,
            int(r.get("u16_1", 0)) & 0xFFFF,
            int(r.get("u16_2", 0)) & 0xFFFF,
            int(r.get("u16_3", 0)) & 0xFFFF,
            int(r.get("u16_4", 0)) & 0xFFFF,
            int(r.get("u16_5", 0)) & 0xFFFF,
            int(r.get("u16_6", 0)) & 0xFFFF,
            int(r.get("u16_7", 0)) & 0xFFFF,
        )
    path.write_bytes(bytes(buf))


def _populate_data_dir(dst_data_dir: Path, *, exclude: set[Path]) -> None:
    # Create a data/ overlay that shares everything via hardlinks (fast), except excluded paths.
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
            # Fall back to copying if hardlinks are unavailable (e.g., cross-filesystem tmp).
            dst.write_bytes(src.read_bytes())


def test_hitboxes_refresh_applies_fighter_scale_y_and_respects_ignore_flag() -> None:
    # This is intentionally synthetic (no replay suite): we provide:
    # - identity-ish SSANIM pose matrices,
    # - a minimal MSLHITB1 table with one hitbox that scales and one that ignores scale.
    #
    # C-side note: anim_pose / hitboxes tables are global singletons loaded once per process from
    # MSL_DATA_DIR. We reset just those tables for this test so we can point MSL_DATA_DIR at a
    # temporary overlay without spawning a subprocess, and then restore afterwards.
    import msl_binding

    exclude = {
        Path("anims/fox.bin"),
        Path("anims/falco.bin"),
        Path("hitboxes/fox.bin"),
        Path("hitboxes/falco.bin"),
    }

    model_scaling = np.float32(1.0)

    build_dir = Path("build")
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=build_dir) as tmp:
        tmp = Path(tmp)
        data_dir = tmp / "data"
        _populate_data_dir(data_dir, exclude=exclude)
        with open(data_dir / "characters/fox.json", encoding="utf-8") as f:
            model_scaling = np.float32(json.load(f)["model_scaling"])

        # Minimal identity pose for part_id=0 on msid=0, frame=0.
        ident = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]
        _write_minimal_ssanim_v4(data_dir / "anims/fox.bin", msid=0, part_id=0, mtx34=ident)
        _write_minimal_ssanim_v4(data_dir / "anims/falco.bin", msid=0, part_id=0, mtx34=ident)

        # Two always-on hitboxes at frame 0:
        # - hb0: scales with fighter_scale_y
        # - hb1: ignores fighter_scale_y (u16_6 bit 13)
        IGNORE_SCALE = 1 << 13
        records = [
            {
                "frame": 0,
                "kind": 0,
                "hitbox_id": 0,
                "bone_part_id": 0,
                "x": 1.5,
                "y": 2.0,
                "z": 3.0,
                "radius": 4.0,
                "damage": 5.0,
                "u16_6": 0,
            },
            {
                "frame": 0,
                "kind": 0,
                "hitbox_id": 1,
                "bone_part_id": 0,
                "x": 1.5,
                "y": 2.0,
                "z": 3.0,
                "radius": 4.0,
                "damage": 5.0,
                "u16_6": IGNORE_SCALE,
            },
        ]
        _write_minimal_mslhitb1(data_dir / "hitboxes/fox.bin", msid=0, records=records)
        _write_minimal_mslhitb1(data_dir / "hitboxes/falco.bin", msid=0, records=records)

        old_data_dir = os.environ.get("MSL_DATA_DIR")
        try:
            os.environ["MSL_DATA_DIR"] = str(data_dir)
            msl_binding.debug_reset_pose_and_hitboxes_tables()

            sizes = msl_binding.sizes()
            seed_stride = int(sizes["seed"])
            input_stride = int(sizes["input"])
            assert input_stride == INPUT_DTYPE.itemsize

            seed = np.zeros((1,), dtype=SEED_DTYPE)
            seed["stage_id"][0] = np.uint32(32)
            seed["num_players"][0] = np.uint8(2)
            seed["stocks"][0, :2] = np.uint8(4)
            seed["char_id"][0, :2] = np.uint8(1)  # Fox
            seed["action_id"][0, :2] = np.uint16(0xFFFF)

            seed["pos_x"][0, 0] = np.float32(100.0)
            seed["pos_y"][0, 0] = np.float32(-50.0)
            seed["pos_z"][0, 0] = np.float32(0.25)
            seed["pos_x"][0, 1] = np.float32(100.0)
            seed["pos_y"][0, 1] = np.float32(-50.0)
            seed["pos_z"][0, 1] = np.float32(0.25)

            seed["fighter_scale_y"][0, :2] = np.float32(2.0)
            seed["facing"][0, 0] = np.uint8(1)  # right => +X
            seed["facing"][0, 1] = np.uint8(0)  # left  => -X

            seed["action_frame"][0, :2] = np.int16(0)
            seed["anim_frame_f32"][0, :2] = np.float32(0.0)
            seed["animation_index"][0, :2] = np.uint32(0)

            # Freeze gameplay updates; we only care about refresh output.
            seed["hitlag"][0, :2] = np.uint16(2)

            prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
            inp = np.zeros((1, input_stride), dtype=np.uint8)

            handle = msl_binding.init(batch_size=1, num_players=2)
            try:
                seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
                msl_binding.reseed_seed(handle, seed_bytes)
                msl_binding.step_input(handle, prev_inp, inp)

                hb0, c0 = msl_binding.hitboxes_world(handle, 0, 0)
                hb1, c1 = msl_binding.hitboxes_world(handle, 0, 1)
            finally:
                msl_binding.destroy(handle)
        finally:
            if old_data_dir is None:
                os.environ.pop("MSL_DATA_DIR", None)
            else:
                os.environ["MSL_DATA_DIR"] = old_data_dir
            msl_binding.debug_reset_pose_and_hitboxes_tables()

    assert int(c0) == 2
    assert int(c1) == 2

    p0 = hb0[:, :5].astype(np.float32, copy=False)
    p1 = hb1[:, :5].astype(np.float32, copy=False)
    p0_enabled = hb0[:, 9].astype(np.float32, copy=False)
    p1_enabled = hb1[:, 9].astype(np.float32, copy=False)

    assert int(np.sum(p0_enabled != np.float32(0.0))) == 2
    assert int(np.sum(p1_enabled != np.float32(0.0))) == 2

    model_scale = np.float32(2.0) * model_scaling

    # hb0: scales center by (fighter_scale_y * model_scaling). Radius scaling is controlled by the
    # ignore-scale flag and uses only fighter_scale_y (see refs/melee/src/melee/lb/lbcollision.c::lbColl_80007AFC).
    #
    # Facing applies decomp-shaped root rotY90, mixing X/Z.
    #
    # With identity pose, local = (x,y,z) * (fighter_scale_y * model_scaling), then:
    # - facing right:  (x,z) -> ( z, -x)
    # - facing left:   (x,z) -> (-z,  x)
    assert np.isclose(p0[0, 0], np.float32(100.0 + model_scale * 3.0))  # +z -> +x
    assert np.isclose(p1[0, 0], np.float32(100.0 - model_scale * 3.0))  # -z -> +x
    assert np.isclose(p0[0, 1], np.float32(-50.0 + model_scale * 2.0))
    assert np.isclose(p0[0, 2], np.float32(0.25 - model_scale * 1.5))  # -x -> +z
    assert np.isclose(p0[0, 3], np.float32(4.0 * 2.0))

    # hb1: center still scales (pose space), but radius ignores fighter_scale_y.
    assert np.isclose(p0[1, 0], np.float32(100.0 + model_scale * 3.0))
    assert np.isclose(p0[1, 3], np.float32(4.0))
