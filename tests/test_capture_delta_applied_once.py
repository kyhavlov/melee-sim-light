from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tools.slippi.combat_history import AnimPoseDB

# Character ids (GALE01): tools/slippi/make_dataset_from_slp.py.
CHAR_FOX = 1
CHAR_FALCO = 22

# Stage ids (GALE01): Final Destination = 32.
STAGE_FD = 32

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h.
ACT_WAIT = 0x000E
ACT_CAPTURE_PULLED_LW = 0x00E2  # ftCo_MS_CapturePulledLw (226)

# Submotion ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h.
SM_WAIT1_0 = 2

# Fighter_Part ids (GALE01): refs/melee/src/melee/ft/forward.h::Fighter_Part.
FTPART_XROTN = 2


def _require_local_artifacts_or_skip() -> None:
    paths = [
        Path("data/stages/final_destination.json"),
        Path("data/characters/fox.json"),
        Path("data/anims/fox.bin"),
        Path("data/anims/falco.bin"),
    ]
    if any(not p.exists() for p in paths):
        pytest.skip("requires local data/ artifacts (stage + chars + anims); run tools/extraction to generate")


def _origin_world_facing_yrot90(
    m12: np.ndarray,
    *,
    fighter_pos_x: float,
    fighter_pos_y: float,
    fighter_pos_z: float,
    fighter_scale_y: float,
    facing_u8: int,
) -> tuple[float, float, float]:
    # SSANIM01 v3 matrix record layout: <12f (m00,m01,m02,tx, m10,m11,m12,ty, m20,m21,m22,tz).
    lx = float(m12[3])
    ly = float(m12[7])
    lz = float(m12[11])

    # Decomp-shaped root facing parity: +/-90deg Y rotation based on facing_dir, so local Z contributes to
    # world X (see src/grab_attachment.c:63 pose_part_origin_world_facing_yrot90()).
    facing_dir = 1.0 if int(facing_u8) != 0 else -1.0
    world_x = facing_dir * lz
    world_z = -facing_dir * lx
    world_y = ly

    return (
        fighter_pos_x + fighter_scale_y * world_x,
        fighter_pos_y + fighter_scale_y * world_y,
        fighter_pos_z + fighter_scale_y * world_z,
    )


@pytest.mark.integration
def test_capture_delta_applied_once_per_step() -> None:
    _require_local_artifacts_or_skip()
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    # Read the ISO-derived capture anchor mapping (decomp-first; no hardcoded anchors in the test).
    fox_params = json.loads(Path("data/characters/fox.json").read_text(encoding="utf-8"))
    anchor_part = int(fox_params["grab_capture_anchor_part_id"])
    owner_model_scaling = float(fox_params["model_scaling"])
    falco_params = json.loads(Path("data/characters/falco.json").read_text(encoding="utf-8"))
    victim_model_scaling = float(falco_params["model_scaling"])

    # Load pose DBs to compute the expected decomp-shaped delta once.
    fox_pose = AnimPoseDB(Path("data/anims/fox.bin").read_bytes())
    falco_pose = AnimPoseDB(Path("data/anims/falco.bin").read_bytes())

    # Hold animation timebase fixed so the expected frame is stable.
    msid = SM_WAIT1_0
    frame = 0
    mx = fox_pose.try_get_matrix(msid=msid, frame=frame, part_id=anchor_part)
    mv = falco_pose.try_get_matrix(msid=msid, frame=frame, part_id=FTPART_XROTN)
    if mx is None or mv is None:
        pytest.skip("local anims missing required joint parts for capture delta test; regenerate via extract_fighter_anims")

    owner_pos = (0.0, 50.0, 0.0)
    victim_pos = (10.0, 50.0, 0.0)
    scale_y = 1.0
    facing = 1  # right

    ax, ay, az = _origin_world_facing_yrot90(
        mx,
        fighter_pos_x=owner_pos[0],
        fighter_pos_y=owner_pos[1],
        fighter_pos_z=owner_pos[2],
        fighter_scale_y=scale_y * owner_model_scaling,
        facing_u8=facing,
    )
    vx, vy, vz = _origin_world_facing_yrot90(
        mv,
        fighter_pos_x=victim_pos[0],
        fighter_pos_y=victim_pos[1],
        fighter_pos_z=victim_pos[2],
        fighter_scale_y=scale_y * victim_model_scaling,
        facing_u8=facing,
    )
    exp_dx = ax - vx
    exp_dy = ay - vy
    exp_dz = az - vz

    handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        seed_bytes = np.zeros((1, seed_stride), dtype=np.uint8)
        seed = seed_bytes.view(SEED_DTYPE).reshape((1,))

        seed["stage_id"][0] = np.uint32(STAGE_FD)
        seed["num_players"][0] = np.uint8(2)
        seed["stocks"][0, :2] = np.uint8(4)

        # Owner (p0): stable grounded Wait (no physics movement).
        seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
        seed["pos_x"][0, 0] = np.float32(owner_pos[0])
        seed["pos_y"][0, 0] = np.float32(owner_pos[1])
        seed["pos_z"][0, 0] = np.float32(owner_pos[2])
        seed["fighter_scale_y"][0, 0] = np.float32(scale_y)
        seed["facing"][0, 0] = np.uint8(facing)
        seed["on_ground"][0, 0] = np.uint8(1)
        seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
        seed["action_frame"][0, 0] = np.int16(0)
        seed["animation_index"][0, 0] = np.uint32(msid)
        seed["anim_frame_f32"][0, 0] = np.float32(frame)
        seed["frame_speed_mul_f32"][0, 0] = np.float32(0.0)

        # Victim (p1): CapturePulledLw, attached to p0.
        seed["char_id"][0, 1] = np.uint8(CHAR_FALCO)
        seed["pos_x"][0, 1] = np.float32(victim_pos[0])
        seed["pos_y"][0, 1] = np.float32(victim_pos[1])
        seed["pos_z"][0, 1] = np.float32(victim_pos[2])
        seed["fighter_scale_y"][0, 1] = np.float32(scale_y)
        seed["facing"][0, 1] = np.uint8(facing)
        seed["on_ground"][0, 1] = np.uint8(0)
        seed["action_id"][0, 1] = np.uint16(ACT_CAPTURE_PULLED_LW)
        seed["action_frame"][0, 1] = np.int16(0)
        seed["animation_index"][0, 1] = np.uint32(msid)
        seed["anim_frame_f32"][0, 1] = np.float32(frame)
        seed["frame_speed_mul_f32"][0, 1] = np.float32(0.0)
        seed["grab_owner_port"][0, 1] = np.uint8(0)

        prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
        inp = np.zeros((1, input_stride), dtype=np.uint8)
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]

        got_x = float(out["pos_x"][1])
        got_y = float(out["pos_y"][1])
        # If the capture delta were applied twice, we'd observe ~2x displacement from the seed.
        assert got_x == pytest.approx(victim_pos[0] + exp_dx, abs=1e-4)
        assert got_y == pytest.approx(victim_pos[1] + exp_dy, abs=1e-4)
    finally:
        msl_binding.destroy(handle)
