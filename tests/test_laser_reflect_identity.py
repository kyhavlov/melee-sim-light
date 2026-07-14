from __future__ import annotations

import subprocess
import struct
import textwrap
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


BUTTON_L = 0x0040
TRIGGER_FULL = np.uint8(255)

# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E
ACT_DASH = 0x0014
ACT_GUARD_ON = 0x00B2
ACT_GUARD_REFLECT = 0x00B6
ACT_DAMAGE_N1 = 0x004E

CHAR_FOX = 1
CHAR_FALCO = 22
STAGE_FD = 32


def test_item_reflect_episode_helper_owns_snapshot_damage_and_seed_lanes(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    source = tmp_path / "item_reflect_episode_test.c"
    exe = tmp_path / "item_reflect_episode_test"
    source.write_text(
        textwrap.dedent(
            """
            #include <assert.h>
            #include <math.h>
            #include <string.h>

            #include "item_reflect.h"

            static MslBatch batch;
            static int8_t item_owner[MSL_MAX_ITEMS];
            static uint16_t item_instance_id[MSL_MAX_ITEMS];
            static float item_reflect_damage_mul[MSL_MAX_ITEMS];
            static uint8_t item_stale_damage_valid[MSL_MAX_ITEMS];
            static float item_stale_damage_mul[MSL_MAX_ITEMS];
            static uint8_t item_reflect_body_owner_port[MSL_MAX_ITEMS];
            static uint16_t item_reflect_body_attack_id[MSL_MAX_ITEMS];
            static uint16_t item_reflect_body_attack_instance[MSL_MAX_ITEMS];
            static uint8_t item_reflect_body_damage_valid[MSL_MAX_ITEMS];
            static uint8_t item_pending_reflect_owner_port[MSL_MAX_ITEMS];
            static uint16_t item_pending_reflect_instance_id[MSL_MAX_ITEMS];
            static uint8_t item_reflect_transfer_seed_port[MSL_MAX_ITEMS];
            static uint16_t item_reflect_transfer_seed_iid[MSL_MAX_ITEMS];
            static uint8_t item_shield_bounce_seed_valid[MSL_MAX_ITEMS];
            static float item_shield_bounce_seed_vel_x[MSL_MAX_ITEMS];
            static float item_shield_bounce_seed_vel_y[MSL_MAX_ITEMS];
            static float item_vel_x[MSL_MAX_ITEMS];
            static float item_vel_y[MSL_MAX_ITEMS];
            static float item_pos_x[MSL_MAX_ITEMS];
            static float item_direction[MSL_MAX_ITEMS];
            static uint8_t item_misc2[MSL_MAX_ITEMS];
            static uint8_t item_misc3[MSL_MAX_ITEMS];
            static uint16_t instance_id[MSL_MAX_PLAYERS];
            static uint16_t attack_id[MSL_MAX_PLAYERS];
            static uint16_t attack_instance[MSL_MAX_PLAYERS];
            static float pos_x[MSL_MAX_PLAYERS];
            static uint8_t stale_queue_index[MSL_MAX_PLAYERS];
            static uint16_t stale_move_id[MSL_MAX_PLAYERS * MSL_STALE_QUEUE_SIZE];
            static uint16_t stale_attack_instance[MSL_MAX_PLAYERS * MSL_STALE_QUEUE_SIZE];

            static int almost(float a, float b) {
              return fabsf(a - b) < 0.00001f;
            }

            static void bind_state(void) {
              memset(&batch, 0, sizeof(batch));
              batch.config.num_players = 2;
              batch.state.item_owner = item_owner;
              batch.state.item_instance_id = item_instance_id;
              batch.state.item_reflect_damage_mul = item_reflect_damage_mul;
              batch.state.item_stale_damage_valid = item_stale_damage_valid;
              batch.state.item_stale_damage_mul = item_stale_damage_mul;
              batch.state.item_reflect_body_owner_port = item_reflect_body_owner_port;
              batch.state.item_reflect_body_attack_id = item_reflect_body_attack_id;
              batch.state.item_reflect_body_attack_instance = item_reflect_body_attack_instance;
              batch.state.item_reflect_body_damage_valid = item_reflect_body_damage_valid;
              batch.state.item_pending_reflect_owner_port = item_pending_reflect_owner_port;
              batch.state.item_pending_reflect_instance_id = item_pending_reflect_instance_id;
              batch.state.item_reflect_transfer_seed_port = item_reflect_transfer_seed_port;
              batch.state.item_reflect_transfer_seed_iid = item_reflect_transfer_seed_iid;
              batch.state.item_shield_bounce_seed_valid = item_shield_bounce_seed_valid;
              batch.state.item_shield_bounce_seed_vel_x = item_shield_bounce_seed_vel_x;
              batch.state.item_shield_bounce_seed_vel_y = item_shield_bounce_seed_vel_y;
              batch.state.item_vel_x = item_vel_x;
              batch.state.item_vel_y = item_vel_y;
              batch.state.item_pos_x = item_pos_x;
              batch.state.item_direction = item_direction;
              batch.state.item_misc2 = item_misc2;
              batch.state.item_misc3 = item_misc3;
              batch.state.instance_id = instance_id;
              batch.state.attack_id = attack_id;
              batch.state.attack_instance = attack_instance;
              batch.state.pos_x = pos_x;
              batch.state.stale_queue_index = stale_queue_index;
              batch.state.stale_move_id = stale_move_id;
              batch.state.stale_attack_instance = stale_attack_instance;
            }

            int main(void) {
              bind_state();
              instance_id[1] = 222u;

              msl_item_reflect_clear_all_lanes(&batch, 0u);
              assert(almost(item_reflect_damage_mul[0], 1.0f));
              assert(item_stale_damage_valid[0] == 0u);
              assert(item_reflect_body_owner_port[0] == MSL_ITEM_REFLECT_NO_PORT);
              assert(item_reflect_body_attack_id[0] == MSL_FT_MOVE_ID_DEFAULT);
              assert(item_reflect_body_attack_instance[0] == 0u);
              assert(item_reflect_body_damage_valid[0] == 0u);
              assert(item_pending_reflect_owner_port[0] == MSL_ITEM_REFLECT_NO_PORT);
              assert(item_reflect_transfer_seed_port[0] == MSL_ITEM_REFLECT_NO_PORT);
              assert(item_shield_bounce_seed_valid[0] == 0u);
              assert(msl_item_reflect_has_transfer_provenance(&batch, 0u) == 0u);

              msl_item_reflect_set_damage_mul(&batch, 0u, 0.5f);
              assert(msl_item_reflect_has_transfer_provenance(&batch, 0u) == 1u);
              assert(item_stale_damage_valid[0] == 0u);
              assert(almost(item_stale_damage_mul[0], 1.0f));
              assert(almost(msl_item_reflect_damage_lane(&batch, 0u, 3.0f), 2.0f));
              msl_item_reflect_clear_all_lanes(&batch, 0u);

              item_owner[0] = 0;
              item_instance_id[0] = 111u;
              item_vel_x[0] = 5.0f;
              item_vel_y[0] = 1.0f;
              item_direction[0] = 1.0f;
              msl_item_reflect_stage_snapshot_defer_velocity(&batch, 0u, 1, 0.5f);
              assert(item_owner[0] == 0);
              assert(item_instance_id[0] == 111u);
              assert(item_pending_reflect_owner_port[0] == 1u);
              assert(item_pending_reflect_instance_id[0] == 222u);
              assert(almost(item_direction[0], -1.0f));
              assert(almost(item_reflect_damage_mul[0], 0.5f));
              assert(msl_item_reflect_has_transfer_provenance(&batch, 0u) == 1u);

              msl_item_reflect_apply_pending_laser_callback(&batch, 0u);
              assert(item_owner[0] == 1);
              assert(item_instance_id[0] == 222u);
              assert(item_pending_reflect_owner_port[0] == MSL_ITEM_REFLECT_NO_PORT);
              assert(almost(item_vel_x[0], -5.0f));
              assert(almost(item_vel_y[0], -1.0f));

              item_owner[0] = 0;
              item_instance_id[0] = 111u;
              item_vel_x[0] = 4.0f;
              item_vel_y[0] = -2.0f;
              item_direction[0] = 1.0f;
              attack_id[1] = 55u;
              attack_instance[1] = 222u;
              msl_item_reflect_apply_immediate_transfer(&batch, 0u, 1u, 1, 0.5f, 2.0f);
              assert(item_owner[0] == 1);
              assert(item_instance_id[0] == 222u);
              assert(almost(item_vel_x[0], -8.0f));
              assert(almost(item_vel_y[0], 4.0f));
              assert(almost(item_direction[0], -1.0f));
              assert(item_reflect_body_owner_port[0] == 1u);
              assert(item_reflect_body_attack_id[0] == 55u);
              assert(item_reflect_body_attack_instance[0] == 222u);
              assert(item_reflect_body_damage_valid[0] == 0u);
              assert(msl_item_reflect_has_transfer_provenance(&batch, 0u) == 1u);

              item_owner[0] = 0;
              item_vel_x[0] = 3.0f;
              item_pos_x[0] = -10.0f;
              pos_x[1] = 0.0f;
              msl_item_reflect_apply_seeded_transfer(&batch, 0u, 1u, 333u, 0.25f);
              assert(item_owner[0] == 1);
              assert(item_instance_id[0] == 333u);
              assert(almost(item_direction[0], -1.0f));
              assert(almost(item_reflect_damage_mul[0], 0.25f));
              assert(msl_item_reflect_has_transfer_provenance(&batch, 0u) == 1u);

              item_reflect_transfer_seed_port[0] = 1u;
              item_reflect_transfer_seed_iid[0] = 333u;
              item_shield_bounce_seed_valid[0] = 1u;
              item_shield_bounce_seed_vel_x[0] = 1.0f;
              item_shield_bounce_seed_vel_y[0] = 2.0f;
              msl_item_reflect_clear_seed_lanes(&batch, 0u);
              assert(item_reflect_transfer_seed_port[0] == MSL_ITEM_REFLECT_NO_PORT);
              assert(item_reflect_transfer_seed_iid[0] == 0u);
              assert(item_shield_bounce_seed_valid[0] == 0u);
              assert(almost(item_shield_bounce_seed_vel_x[0], 0.0f));
              assert(almost(item_shield_bounce_seed_vel_y[0], 0.0f));

              return 0;
            }
            """
        ),
        encoding="utf-8",
    )
    subprocess.run(
        ["cc", "-std=c11", "-I", str(root / "src"), str(source), "-lm", "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)


def test_item_hidden_callback_seed_lanes_keep_shallow_shieldbounced_velocity() -> None:
    import msl_binding  # type: ignore

    seed_exists = np.zeros((1, 15), dtype=np.uint8)
    seed_type = np.zeros((1, 15), dtype=np.uint16)
    seed_owner = np.full((1, 15), -1, dtype=np.int8)
    seed_iid = np.zeros((1, 15), dtype=np.uint16)
    seed_spawn = np.zeros((1, 15), dtype=np.uint32)
    seed_dir = np.zeros((1, 15), dtype=np.float32)
    seed_vx = np.zeros((1, 15), dtype=np.float32)
    seed_vy = np.zeros((1, 15), dtype=np.float32)
    ref_exists = np.zeros((1, 15), dtype=np.uint8)
    ref_type = np.zeros((1, 15), dtype=np.uint16)
    ref_owner = np.full((1, 15), -1, dtype=np.int8)
    ref_iid = np.zeros((1, 15), dtype=np.uint16)
    ref_spawn = np.zeros((1, 15), dtype=np.uint32)
    ref_vx = np.zeros((1, 15), dtype=np.float32)
    ref_vy = np.zeros((1, 15), dtype=np.float32)
    seed_action = np.zeros((1, 4), dtype=np.uint16)
    ref_action = np.zeros((1, 4), dtype=np.uint16)
    ref_hitlag = np.zeros((1, 4), dtype=np.uint16)
    ref_hitstun = np.zeros((1, 4), dtype=np.uint16)
    ref_instance_hit_by = np.zeros((1, 4), dtype=np.uint16)
    laser_lut = np.zeros(65536, dtype=np.uint8)

    seed_exists[0, 0] = 1
    seed_type[0, 0] = 55
    seed_owner[0, 0] = 0
    seed_iid[0, 0] = 16
    seed_spawn[0, 0] = 2
    seed_dir[0, 0] = 1.0
    seed_vx[0, 0] = 5.0
    ref_exists[0, 0] = 1
    ref_type[0, 0] = 55
    ref_owner[0, 0] = 0
    ref_iid[0, 0] = 16
    ref_spawn[0, 0] = 2
    ref_vx[0, 0] = np.float32(4.9860768)
    ref_vy[0, 0] = np.float32(0.37187797)
    seed_action[0, 1] = ACT_GUARD_REFLECT
    ref_action[0, 1] = 181  # GuardSetOff
    laser_lut[55] = 1

    _, _, bounce_valid, bounce_vx, bounce_vy, *_ = msl_binding.derive_item_hidden_callback_seed_lanes(
        seed_exists,
        seed_type,
        seed_owner,
        seed_iid,
        seed_spawn,
        seed_dir,
        seed_vx,
        seed_vy,
        ref_exists,
        ref_type,
        ref_owner,
        ref_iid,
        ref_spawn,
        ref_vx,
        ref_vy,
        seed_action,
        ref_action,
        ref_hitlag,
        ref_hitstun,
        ref_instance_hit_by,
        laser_lut,
        laser_lut,
        2,
    )
    assert int(bounce_valid[0, 0]) == 1
    assert float(bounce_vx[0, 0]) == pytest.approx(float(ref_vx[0, 0]), abs=1e-7)
    assert float(bounce_vy[0, 0]) == pytest.approx(float(ref_vy[0, 0]), abs=1e-7)

    ref_vx[0, 0] = np.float32(5.0)
    ref_vy[0, 0] = np.float32(0.0005)
    _, _, bounce_valid, *_ = msl_binding.derive_item_hidden_callback_seed_lanes(
        seed_exists,
        seed_type,
        seed_owner,
        seed_iid,
        seed_spawn,
        seed_dir,
        seed_vx,
        seed_vy,
        ref_exists,
        ref_type,
        ref_owner,
        ref_iid,
        ref_spawn,
        ref_vx,
        ref_vy,
        seed_action,
        ref_action,
        ref_hitlag,
        ref_hitstun,
        ref_instance_hit_by,
        laser_lut,
        laser_lut,
        2,
    )
    assert int(bounce_valid[0, 0]) == 0


def test_item_hidden_callback_seed_lanes_allow_nonlaser_shield_bounce_without_body_latch() -> None:
    import msl_binding

    seed_exists = np.zeros((1, 15), dtype=np.uint8)
    seed_type = np.zeros((1, 15), dtype=np.uint16)
    seed_owner = np.full((1, 15), -1, dtype=np.int8)
    seed_iid = np.zeros((1, 15), dtype=np.uint16)
    seed_spawn = np.zeros((1, 15), dtype=np.uint32)
    seed_dir = np.ones((1, 15), dtype=np.float32)
    seed_vx = np.zeros((1, 15), dtype=np.float32)
    seed_vy = np.zeros((1, 15), dtype=np.float32)
    ref_exists = np.zeros((1, 15), dtype=np.uint8)
    ref_type = np.zeros((1, 15), dtype=np.uint16)
    ref_owner = np.full((1, 15), -1, dtype=np.int8)
    ref_iid = np.zeros((1, 15), dtype=np.uint16)
    ref_spawn = np.zeros((1, 15), dtype=np.uint32)
    ref_vx = np.zeros((1, 15), dtype=np.float32)
    ref_vy = np.zeros((1, 15), dtype=np.float32)
    seed_action = np.zeros((1, 4), dtype=np.uint16)
    ref_action = np.zeros((1, 4), dtype=np.uint16)
    ref_hitlag = np.zeros((1, 4), dtype=np.uint16)
    ref_hitstun = np.zeros((1, 4), dtype=np.uint16)
    ref_instance_hit_by = np.zeros((1, 4), dtype=np.uint16)
    laser_lut = np.zeros(65536, dtype=np.uint8)
    shield_bounce_lut = np.zeros(65536, dtype=np.uint8)

    seed_exists[0, 0] = 1
    seed_type[0, 0] = 79  # Sheik thrown Needle.
    seed_owner[0, 0] = 0
    seed_iid[0, 0] = 44
    seed_spawn[0, 0] = 9
    seed_vx[0, 0] = -4.0
    ref_exists[0, 0] = 1
    ref_type[0, 0] = 79
    ref_owner[0, 0] = 0
    ref_iid[0, 0] = 44
    ref_spawn[0, 0] = 9
    ref_vx[0, 0] = np.float32(1.25)
    ref_vy[0, 0] = np.float32(-3.8)
    seed_action[0, 1] = ACT_GUARD_REFLECT
    ref_action[0, 1] = 181  # GuardSetOff.
    shield_bounce_lut[79] = 1

    *_, body_victim, body_height, callback_flags = msl_binding.derive_item_hidden_callback_seed_lanes(
        seed_exists,
        seed_type,
        seed_owner,
        seed_iid,
        seed_spawn,
        seed_dir,
        seed_vx,
        seed_vy,
        ref_exists,
        ref_type,
        ref_owner,
        ref_iid,
        ref_spawn,
        ref_vx,
        ref_vy,
        seed_action,
        ref_action,
        ref_hitlag,
        ref_hitstun,
        ref_instance_hit_by,
        laser_lut,
        shield_bounce_lut,
        2,
    )
    _, _, bounce_valid, bounce_vx, bounce_vy, *_ = msl_binding.derive_item_hidden_callback_seed_lanes(
        seed_exists,
        seed_type,
        seed_owner,
        seed_iid,
        seed_spawn,
        seed_dir,
        seed_vx,
        seed_vy,
        ref_exists,
        ref_type,
        ref_owner,
        ref_iid,
        ref_spawn,
        ref_vx,
        ref_vy,
        seed_action,
        ref_action,
        ref_hitlag,
        ref_hitstun,
        ref_instance_hit_by,
        laser_lut,
        shield_bounce_lut,
        2,
    )
    assert int(bounce_valid[0, 0]) == 1
    assert float(bounce_vx[0, 0]) == pytest.approx(float(ref_vx[0, 0]), abs=1e-7)
    assert float(bounce_vy[0, 0]) == pytest.approx(float(ref_vy[0, 0]), abs=1e-7)
    assert int(body_victim[0, 0]) == 0xFF
    assert int(body_height[0, 0]) == 0
    assert int(callback_flags[0, 0]) == 0

    ref_vx[0, 0] = 0.0
    ref_vy[0, 0] = 0.0
    _, _, bounce_valid, *_ = msl_binding.derive_item_hidden_callback_seed_lanes(
        seed_exists,
        seed_type,
        seed_owner,
        seed_iid,
        seed_spawn,
        seed_dir,
        seed_vx,
        seed_vy,
        ref_exists,
        ref_type,
        ref_owner,
        ref_iid,
        ref_spawn,
        ref_vx,
        ref_vy,
        seed_action,
        ref_action,
        ref_hitlag,
        ref_hitstun,
        ref_instance_hit_by,
        laser_lut,
        shield_bounce_lut,
        2,
    )
    assert int(bounce_valid[0, 0]) == 0


def test_item_hidden_callback_seed_lanes_mark_reflected_laser_body_latch_only_when_proven() -> None:
    # Reflected laser callbacks can expose public facing/owner before the hidden laser angle and
    # BODY latch are consumed. The seed lane is source-owned only when the next frame proves this
    # item instance uniquely damaged a fighter and disappeared; ordinary aligned laser contacts stay
    # on runtime collision.
    # refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxLaser_Logic94_Reflected,
    # itFoxlaser_UnkMotion1_Anim}
    # refs/melee/src/melee/it/item.c::{OnGiveDamageThink,Item_8026A294}
    import msl_binding  # type: ignore

    seed_exists = np.zeros((1, 15), dtype=np.uint8)
    seed_type = np.zeros((1, 15), dtype=np.uint16)
    seed_owner = np.full((1, 15), -1, dtype=np.int8)
    seed_iid = np.zeros((1, 15), dtype=np.uint16)
    seed_spawn = np.zeros((1, 15), dtype=np.uint32)
    seed_dir = np.zeros((1, 15), dtype=np.float32)
    seed_vx = np.zeros((1, 15), dtype=np.float32)
    seed_vy = np.zeros((1, 15), dtype=np.float32)
    ref_exists = np.zeros((1, 15), dtype=np.uint8)
    ref_type = np.zeros((1, 15), dtype=np.uint16)
    ref_owner = np.full((1, 15), -1, dtype=np.int8)
    ref_iid = np.zeros((1, 15), dtype=np.uint16)
    ref_spawn = np.zeros((1, 15), dtype=np.uint32)
    ref_vx = np.zeros((1, 15), dtype=np.float32)
    ref_vy = np.zeros((1, 15), dtype=np.float32)
    seed_action = np.zeros((1, 4), dtype=np.uint16)
    ref_action = np.zeros((1, 4), dtype=np.uint16)
    ref_hitlag = np.zeros((1, 4), dtype=np.uint16)
    ref_hitstun = np.zeros((1, 4), dtype=np.uint16)
    ref_instance_hit_by = np.zeros((1, 4), dtype=np.uint16)
    laser_lut = np.zeros(65536, dtype=np.uint8)

    seed_exists[0, 0] = 1
    seed_type[0, 0] = 55
    seed_owner[0, 0] = 0
    seed_iid[0, 0] = 131
    seed_spawn[0, 0] = 3
    seed_dir[0, 0] = 1.0
    seed_vx[0, 0] = -5.0
    ref_action[0, 1] = ACT_DAMAGE_N1
    ref_hitlag[0, 1] = 4
    ref_hitstun[0, 1] = 9
    ref_instance_hit_by[0, 1] = 131
    laser_lut[55] = 1

    *_, body_victim, body_height, callback_flags = msl_binding.derive_item_hidden_callback_seed_lanes(
        seed_exists,
        seed_type,
        seed_owner,
        seed_iid,
        seed_spawn,
        seed_dir,
        seed_vx,
        seed_vy,
        ref_exists,
        ref_type,
        ref_owner,
        ref_iid,
        ref_spawn,
        ref_vx,
        ref_vy,
        seed_action,
        ref_action,
        ref_hitlag,
        ref_hitstun,
        ref_instance_hit_by,
        laser_lut,
        laser_lut,
        2,
    )
    assert int(body_victim[0, 0]) == 1
    assert int(body_height[0, 0]) == 1
    assert int(callback_flags[0, 0]) == 1

    ref_exists[0, 0] = 1
    ref_type[0, 0] = 55
    ref_owner[0, 0] = 1
    ref_iid[0, 0] = 222
    ref_spawn[0, 0] = 4
    *_, body_victim, body_height, callback_flags = msl_binding.derive_item_hidden_callback_seed_lanes(
        seed_exists,
        seed_type,
        seed_owner,
        seed_iid,
        seed_spawn,
        seed_dir,
        seed_vx,
        seed_vy,
        ref_exists,
        ref_type,
        ref_owner,
        ref_iid,
        ref_spawn,
        ref_vx,
        ref_vy,
        seed_action,
        ref_action,
        ref_hitlag,
        ref_hitstun,
        ref_instance_hit_by,
        laser_lut,
        laser_lut,
        2,
    )
    assert int(body_victim[0, 0]) == 0xFF
    assert int(body_height[0, 0]) == 0
    assert int(callback_flags[0, 0]) == 0

    ref_exists[0, 0] = 0
    ref_type[0, 0] = 0
    ref_owner[0, 0] = -1
    ref_iid[0, 0] = 0
    ref_spawn[0, 0] = 0
    seed_vx[0, 0] = 5.0
    *_, body_victim, body_height, callback_flags = msl_binding.derive_item_hidden_callback_seed_lanes(
        seed_exists,
        seed_type,
        seed_owner,
        seed_iid,
        seed_spawn,
        seed_dir,
        seed_vx,
        seed_vy,
        ref_exists,
        ref_type,
        ref_owner,
        ref_iid,
        ref_spawn,
        ref_vx,
        ref_vy,
        seed_action,
        ref_action,
        ref_hitlag,
        ref_hitstun,
        ref_instance_hit_by,
        laser_lut,
        laser_lut,
        2,
    )
    assert int(body_victim[0, 0]) == 0xFF
    assert int(body_height[0, 0]) == 0
    assert int(callback_flags[0, 0]) == 0


def _common_attr(name: str) -> float:
    import json
    from pathlib import Path

    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    return float(common[name])


def _load_laser_shot_itkind_and_first_offset_x_and_lifetime(char_id_target: int) -> tuple[int, float, int]:
    # data/items/lasers.bin layout: tools/extraction/extract_lasers.py (MSLLASR1 v2..v7).
    path = "data/items/lasers.bin"
    if not Path(path).exists():
        pytest.skip(f"missing local artifact: {path}")
    buf = open(path, "rb").read()
    if buf[:8] != b"MSLLASR1":
        raise AssertionError(f"{path}: bad magic")
    (ver,) = struct.unpack_from("<I", buf, 8)
    if ver not in (1, 2, 3, 4, 5, 6, 7):
        raise AssertionError(f"{path}: unsupported ver={ver}")
    (count,) = struct.unpack_from("<H", buf, 12)
    off = 16

    # Record packing (see tools/extraction/extract_lasers.py::_pack_record).
    record_bytes = {1: 158, 2: 166, 3: 254, 4: 254, 5: 254, 6: 218, 7: 226}[int(ver)]

    for _ in range(int(count)):
        base = off
        char_id = int(buf[off])
        shot_itkind = int(struct.unpack_from("<H", buf, off + 2)[0])

        # Match src/laser_params.c offsets. v6 removed SpecialN script shoot-frame arrays; those
        # cmd_var[2] pulses now come from MSLFTSC1/move_tables.
        if int(ver) == 1:
            off_part2 = base + 70
        elif int(ver) >= 6:
            off_part2 = base + 42
        else:
            off_part2 = base + 78

        hitbox_offsets_x_count = int(buf[off_part2 + (19 if int(ver) >= 5 else 20)])
        offs0 = off_part2 + 24
        first = float(struct.unpack_from("<f", buf, offs0)[0]) if hitbox_offsets_x_count > 0 else 0.0

        if int(char_id) == int(char_id_target):
            lifetime = int(struct.unpack_from("<H", buf, base + 40)[0]) if int(ver) >= 2 else int(
                struct.unpack_from("<H", buf, base + 32)[0]
            )
            return int(shot_itkind), first, lifetime

        # Defensive: ensure we don't desync parsing if record size changes.
        off += record_bytes
        if off <= base:
            raise AssertionError("record parse did not advance")

    raise AssertionError(f"laser record not found in lasers.bin for char_id={char_id_target}")


def _load_laser_shot_size_and_offsets_x(char_id_target: int) -> tuple[float, tuple[float, ...]]:
    # data/items/lasers.bin layout: tools/extraction/extract_lasers.py (MSLLASR1 v2..v7).
    path = Path("data/items/lasers.bin")
    if not path.exists():
        pytest.skip(f"missing local artifact: {path}")
    buf = path.read_bytes()
    if buf[:8] != b"MSLLASR1":
        raise AssertionError(f"{path}: bad magic")
    (ver,) = struct.unpack_from("<I", buf, 8)
    if ver not in (1, 2, 3, 4, 5, 6, 7):
        raise AssertionError(f"{path}: unsupported ver={ver}")
    (count,) = struct.unpack_from("<H", buf, 12)
    off = 16
    record_bytes = {1: 158, 2: 166, 3: 254, 4: 254, 5: 254, 6: 218, 7: 226}[int(ver)]

    for _ in range(int(count)):
        base = off
        char_id = int(buf[off])
        if int(ver) == 1:
            off_part2 = base + 70
        elif int(ver) >= 6:
            off_part2 = base + 42
        else:
            off_part2 = base + 78
        size = float(struct.unpack_from("<f", buf, off_part2 + 4)[0])
        hitbox_offsets_x_count = int(buf[off_part2 + (19 if int(ver) >= 5 else 20)])
        offsets = tuple(
            float(struct.unpack_from("<f", buf, off_part2 + 24 + (size_t * 4))[0])
            for size_t in range(hitbox_offsets_x_count)
        )
        if int(char_id) == int(char_id_target):
            return size, offsets
        off += record_bytes
        if off <= base:
            raise AssertionError("record parse did not advance")

    raise AssertionError(f"laser record not found in lasers.bin for char_id={char_id_target}")


def _char_key_for_shield_data(char_id: int) -> str:
    if int(char_id) == CHAR_FOX:
        return "fox"
    if int(char_id) == CHAR_FALCO:
        return "falco"
    raise AssertionError(f"unsupported char_id={char_id} for shield data")


def _load_shield_pose_table(char_id_target: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, int]:
    # data/shields/<char>.bin layout: tools/extraction/extract_shield_tilt_table.py (MSLSHLD1 v5).
    key = _char_key_for_shield_data(char_id_target)
    path = Path("data/shields") / f"{key}.bin"
    if not path.exists():
        pytest.skip(f"missing local artifact: {path}")
    buf = path.read_bytes()
    if buf[:8] != b"MSLSHLD1":
        raise AssertionError(f"{path}: bad magic")
    (ver,) = struct.unpack_from("<I", buf, 8)
    if int(ver) != 5:
        raise AssertionError(f"{path}: unsupported ver={ver} (want v5 guard_on pose + shield_part data)")
    frame_count = int(struct.unpack_from("<H", buf, 12)[0])
    neutral_frame = int(struct.unpack_from("<H", buf, 14)[0])
    guard_on_frame_count = int(struct.unpack_from("<H", buf, 28)[0])
    hdr = 32
    guard_on_x20_xyz = np.frombuffer(buf, dtype="<f4", count=3, offset=16).copy()
    steady_xyz = np.frombuffer(buf, dtype="<f4", count=frame_count * 3, offset=hdr).reshape((frame_count, 3))
    guard_on_xyz = np.frombuffer(
        buf, dtype="<f4", count=guard_on_frame_count * 3, offset=hdr + (frame_count * 3 * 4)
    ).reshape((guard_on_frame_count, 3))
    if neutral_frame < 0 or neutral_frame >= frame_count:
        raise AssertionError(f"{path}: neutral_frame out of range")
    if guard_on_frame_count <= 0:
        raise AssertionError(f"{path}: missing guard_on pose data")
    return steady_xyz, guard_on_x20_xyz, guard_on_xyz, neutral_frame


def _full_shield_radius(char_id: int) -> float:
    import json

    key = _char_key_for_shield_data(char_id)
    common = json.loads(Path("data/common/ft_common_data.json").read_text())
    attrs = json.loads((Path("data/characters") / f"{key}.json").read_text())

    trigger_deadzone = float(common["trigger_deadzone"])
    shield_size_lightshield_min = float(common["shield_size_lightshield_min"])
    shield_size_lightshield_max = float(common["shield_size_lightshield_max"])
    shield_size_min_scale = float(common["shield_size_min_scale"])
    initial_shield_size = float(attrs["initial_shield_size"])

    # Full digital shield hold in this probe row uses trigger=1.0.
    denom = 1.0 - trigger_deadzone
    light = (1.0 - trigger_deadzone) / denom if denom > 0.0 else 0.0
    hp_ratio = 1.0
    light_scale = light * (shield_size_lightshield_max - shield_size_lightshield_min) + shield_size_lightshield_min
    scale = ((1.0 - shield_size_min_scale) * (hp_ratio * light_scale)) + shield_size_min_scale
    return float(scale * initial_shield_size)


def _shield_local_xyz_to_world(
    local_xyz: np.ndarray, *, pos_x: float, pos_y: float, pos_z: float, facing: int, scale_y: float = 1.0
) -> tuple[float, float, float]:
    facing_dir = 1.0 if int(facing) != 0 else -1.0
    lx = float(local_xyz[0]) * float(scale_y)
    ly = float(local_xyz[1]) * float(scale_y)
    lz = float(local_xyz[2]) * float(scale_y)
    return (
        float(pos_x + (facing_dir * lz)),
        float(pos_y + ly),
        float(pos_z + (-facing_dir * lx)),
    )


def _swept_sphere_sphere_intersects_3d(
    ax0: float, ay0: float, az0: float, ax1: float, ay1: float, az1: float, ar: float, bx: float, by: float, bz: float, br: float
) -> bool:
    vx = ax1 - ax0
    vy = ay1 - ay0
    vz = az1 - az0
    wx = bx - ax0
    wy = by - ay0
    wz = bz - az0
    vv = vx * vx + vy * vy + vz * vz
    t = 0.0
    if vv > 0.0:
        t = (wx * vx + wy * vy + wz * vz) / vv
        if t < 0.0:
            t = 0.0
        elif t > 1.0:
            t = 1.0
    cx = ax0 + (t * vx)
    cy = ay0 + (t * vy)
    cz = az0 + (t * vz)
    dx = cx - bx
    dy = cy - by
    dz = cz - bz
    rr = ar + br
    return (dx * dx + dy * dy + dz * dz) <= (rr * rr)


def _laser_sweep_hits_shield(
    *,
    x0: float,
    y0: float,
    vx: float,
    vy: float,
    offsets_x: tuple[float, ...],
    laser_radius: float,
    shield_center: tuple[float, float, float],
    shield_radius: float,
) -> bool:
    speed_sq = (vx * vx) + (vy * vy)
    if speed_sq > 0.0:
        inv_speed = 1.0 / float(np.sqrt(speed_sq))
        ux = vx * inv_speed
        uy = vy * inv_speed
    else:
        ux = 1.0
        uy = 0.0
    x1 = x0 + vx
    y1 = y0 + vy
    for off_x in offsets_x:
        sx0 = x0 + (ux * float(off_x))
        sy0 = y0 + (uy * float(off_x))
        sx1 = x1 + (ux * float(off_x))
        sy1 = y1 + (uy * float(off_x))
        if _swept_sphere_sphere_intersects_3d(
            sx0,
            sy0,
            0.0,
            sx1,
            sy1,
            0.0,
            float(laser_radius),
            float(shield_center[0]),
            float(shield_center[1]),
            float(shield_center[2]),
            float(shield_radius),
        ):
            return True
    return False


def _mk_input_bytes(batch: int, input_stride: int) -> np.ndarray:
    return np.zeros((batch, input_stride), dtype=np.uint8)


@pytest.mark.integration
def test_reflected_laser_updates_owner_instance_and_staling_identity() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    shot_itkind, off0, _ = _load_laser_shot_itkind_and_first_offset_x_and_lifetime(CHAR_FOX)

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)  # right
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)

    # Attacker P0: idle.
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(2)  # Wait1_0

    # Defender P1: GuardReflect reflect bubble active (Slippi seeds fp->reflecting via state_flags).
    # Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C (ftColl_CreateReflectHit).
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD_REFLECT)
    seed["action_frame"][0, 1] = np.int16(0)
    seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)  # shield states commonly report -1 in Slippi
    seed["shield_hp"][0, 1] = np.float32(_common_attr("start_shield_health"))
    # Drive reflect-active via the decomp-shaped GuardReflect timer (mv.co.guard.x14).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50 (init x14=x2A4)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0 (tick/expire; clears fp->reflecting)
    seed["guard_reflect_timer_x14"][0, 1] = np.uint8(int(_common_attr("powershield_reflect_frames")) + 1)
    # Powershield-active lifetime (mv.co.guard.x18) must also be active for powershield reflect.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093A50,ftCo_80093BC0}
    seed["guard_reflect_timer_x18"][0, 1] = np.uint8(int(_common_attr("powershield_reflect_total_frames")) + 1)
    # Slippi fp+0x221C powershield-active bit (0x20).
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    seed["state_flags"][0, 1, 3] = np.uint8(0x20)
    # Slippi fp+0x221B_b0 shield descriptor bit. Aged GuardReflect item reflect ownership is
    # source-gated by the live descriptor, not only by the x14/x18 timer carry.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80077464}
    seed["state_flags"][0, 1, 2] = np.uint8(0x80)

    # Distinct identities so the test can assert the transfer.
    seed["instance_id"][0, 0] = np.uint16(100)
    seed["instance_id"][0, 1] = np.uint16(200)
    seed["attack_instance"][0, 0] = np.uint16(111)
    seed["attack_instance"][0, 1] = np.uint16(222)

    # Place defender at a stable point above the stage floor to avoid stage collision in the same step.
    seed["pos_x"][0, 0] = np.float32(-5.0)
    seed["pos_y"][0, 0] = np.float32(5.0)
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, 1] = np.float32(5.0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        # Hold shield via analog trigger so GuardReflect doesn't immediately exit.
        inp_view["p"]["l"][0, 1] = TRIGGER_FULL

        # First: compute the shield-bone center the sim uses as the reflect-bubble center (derived in C).
        # We do this without the laser present to keep the test independent of collision timing.
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy = float(bubbles[1, 0]), float(bubbles[1, 1])

        # Second: reseed and place a laser directly on the reflect bubble center (vel=0 so motion doesn't move it).
        seed2 = seed.copy()
        seed2["items"][0, 0]["exists"] = np.uint8(1)
        seed2["items"][0, 0]["type"] = np.uint16(shot_itkind)
        seed2["items"][0, 0]["owner"] = np.int8(0)
        seed2["items"][0, 0]["instance_id"] = np.uint16(999)
        seed2["items"][0, 0]["attack_id"] = np.uint16(3333)
        seed2["items"][0, 0]["attack_instance"] = np.uint16(111)
        seed2["items"][0, 0]["direction"] = np.float32(1.0)
        seed2["items"][0, 0]["vel_x"] = np.float32(0.0)
        seed2["items"][0, 0]["vel_y"] = np.float32(0.0)
        seed2["items"][0, 0]["pos_x"] = np.float32(shx - np.float32(off0))
        seed2["items"][0, 0]["pos_y"] = np.float32(shy)
        seed2["items"][0, 0]["timer"] = np.float32(10.0)
        seed2["items"][0, 0]["spawn_id"] = np.uint32(123)

        seed2_bytes = seed2.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed2_bytes)
        msl_binding.step_input(handle, prev_inp, inp)

        out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out_cmp)
        cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

        it0 = cmp0["items"][0]
        assert int(it0["exists"]) == 1
        # Decomp-split ownership model:
        # - overlap writes reflect snapshot fields (ftColl_80077464),
        # - item pass consumes snapshot ownership (Item_80269F14).
        # The pending owner snapshot is now hidden fixed-capacity runtime state rather than a
        # user-visible misc2/misc3 marker; ownership transfer is still observed on the next step.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
        # refs/melee/src/melee/it/item.c::Item_80269F14
        assert int(it0["owner"]) == int(seed2["items"][0, 0]["owner"])
        assert int(it0["misc2"]) == int(seed2["items"][0, 0]["misc2"])
        assert int(it0["misc3"]) == int(seed2["items"][0, 0]["misc3"])
        # Spawn-latched staling identity (v1): does not transfer on reflect.
        assert int(it0["attack_id"]) == int(seed2["items"][0, 0]["attack_id"])
        assert int(it0["attack_instance"]) == int(seed2["items"][0, 0]["attack_instance"])

        msl_binding.step_input(handle, prev_inp, inp)
        out_cmp2 = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out_cmp2)
        cmp1 = out_cmp2.view(COMPARE_DTYPE).reshape((1,))[0]
        it1 = cmp1["items"][0]
        assert int(it1["exists"]) == 1
        assert int(it1["owner"]) == 1
        assert int(it1["misc2"]) == 0
        assert int(it1["misc3"]) == 0
    finally:
        msl_binding.destroy(handle)


def test_guardreflect_stale_x14_shield_hit_clears_reflect_active_and_destroys_laser() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    shot_itkind, off0, _ = _load_laser_shot_itkind_and_first_offset_x_and_lifetime(CHAR_FOX)

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)

    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(2)

    # Decomp-shaped stale-x18 GuardReflect snapshot:
    # - x14 (+1-bias seed) already expired, so projectile overlap should resolve through the regular
    #   shield-hit / GuardSetOff path rather than item reflect ownership.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD_REFLECT)
    seed["action_frame"][0, 1] = np.int16(-1)
    seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)
    seed["guard_reflect_timer_x14"][0, 1] = np.uint8(0)
    seed["guard_reflect_timer_x18"][0, 1] = np.uint8(1)
    seed["state_flags"][0, 1, 3] = np.uint8(0x20)
    seed["shield_hp"][0, 1] = np.float32(_common_attr("start_shield_health"))
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, 1] = np.float32(0.0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        inp_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
        inp_view["p"]["l"][0, 1] = TRIGGER_FULL

        # Use the runtime shield bubble center so the synthetic row stays robust to shield-bone data.
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)
        shx, shy = float(bubbles[1, 0]), float(bubbles[1, 1])

        seed2 = seed.copy()
        seed2["items"][0, 0]["exists"] = np.uint8(1)
        seed2["items"][0, 0]["type"] = np.uint16(shot_itkind)
        seed2["items"][0, 0]["owner"] = np.int8(0)
        seed2["items"][0, 0]["instance_id"] = np.uint16(999)
        seed2["items"][0, 0]["direction"] = np.float32(1.0)
        seed2["items"][0, 0]["vel_x"] = np.float32(5.0)
        seed2["items"][0, 0]["vel_y"] = np.float32(0.0)
        seed2["items"][0, 0]["pos_x"] = np.float32(shx - np.float32(off0))
        seed2["items"][0, 0]["pos_y"] = np.float32(shy)
        seed2["items"][0, 0]["timer"] = np.float32(10.0)
        seed2["items"][0, 0]["spawn_id"] = np.uint32(123)

        seed2_bytes = seed2.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed2_bytes)
        msl_binding.step_input(handle, prev_inp, inp)

        out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out_cmp)
        cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

        assert int(cmp0["action_id"][1]) == 181  # GuardSetOff
        assert int(cmp0["animation_index"][1]) == 40  # GuardDamage
        assert int(cmp0["hitlag"][1]) == 4
        assert int(cmp0["state_flags"][1, 1]) == 33
        assert int(cmp0["items"][0]["exists"]) == 0
    finally:
        msl_binding.destroy(handle)


def test_guardreflect_late_locomotion_shield_hit_keeps_one_frame_old_laser_alive() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    shot_itkind, _, _ = _load_laser_shot_itkind_and_first_offset_x_and_lifetime(CHAR_FALCO)

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FALCO)
    seed["char_id"][0, 1] = np.uint8(CHAR_FOX)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)

    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(2)

    # One-frame-late locomotion -> GuardReflect frozen snapshot:
    # - replay can already resolve the projectile contact through GuardSetOff while keeping the
    #   one-frame-old laser alive on the shield-bounce path instead of reflecting or despawning it.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80091A4C,ftCo_800939B4,ftCo_8009370C,ftCo_GuardReflect_Anim}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{
    #   it_8029C504,itFoxlaser_UnkMotion1_Anim,itFoxLaser_Logic94_ShieldBounced}
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD_REFLECT)
    seed["action_frame"][0, 1] = np.int16(-1)
    seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)
    seed["guard_reflect_timer_x14"][0, 1] = np.uint8(1)
    seed["guard_reflect_timer_x18"][0, 1] = np.uint8(1)
    seed["state_flags"][0, 1, 3] = np.uint8(0x20)
    seed["shield_hp"][0, 1] = np.float32(_common_attr("start_shield_health"))
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, 1] = np.float32(0.0)

    # Row-shaped synthetic from the kept TBK family:
    # - defender is the grounded Fox on the first shield-admission frame,
    # - attacker-side Falco laser is one-step before the replay-kept bounce.
    seed["facing"][0, 0] = np.uint8(1)
    seed["facing"][0, 1] = np.uint8(0)
    seed["pos_x"][0, 0] = np.float32(-20.015694)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["pos_x"][0, 1] = np.float32(25.920704)
    seed["pos_y"][0, 1] = np.float32(0.0001)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        inp_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
        inp_view["p"]["l"][0, 1] = TRIGGER_FULL

        seed2 = seed.copy()
        seed2["items"][0, 0]["exists"] = np.uint8(1)
        seed2["items"][0, 0]["type"] = np.uint16(shot_itkind)
        seed2["items"][0, 0]["owner"] = np.int8(0)
        seed2["items"][0, 0]["instance_id"] = np.uint16(999)
        seed2["items"][0, 0]["direction"] = np.float32(1.0)
        seed2["items"][0, 0]["vel_x"] = np.float32(5.0)
        seed2["items"][0, 0]["vel_y"] = np.float32(0.0)
        seed2["items"][0, 0]["pos_x"] = np.float32(20.115339)
        seed2["items"][0, 0]["pos_y"] = np.float32(13.752838)
        seed2["items"][0, 0]["timer"] = np.float32(94.0)
        seed2["items"][0, 0]["spawn_id"] = np.uint32(123)
        # This synthetic row is a teacher-forced frozen GuardReflect contact, so preserve the
        # hidden Item_80269DC8 ShieldBounced result explicitly instead of depending on reduced
        # collision geometry to reconstruct xDCE/xC54/xC58.
        seed2["item_shield_bounce_valid"][0, 0] = np.uint8(1)
        seed2["item_shield_bounce_vel_x"][0, 0] = np.float32(-0.5)
        seed2["item_shield_bounce_vel_y"][0, 0] = np.float32(4.9)

        seed2_bytes = seed2.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed2_bytes)
        msl_binding.step_input(handle, prev_inp, inp)

        out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out_cmp)
        cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

        assert int(cmp0["action_id"][1]) == 181  # GuardSetOff
        assert int(cmp0["animation_index"][1]) == 40  # GuardDamage
        assert int(cmp0["hitlag"][1]) == 4
        assert int(cmp0["items"][0]["exists"]) == 1
        assert int(cmp0["items"][0]["owner"]) == 0
        assert int(cmp0["items"][0]["instance_id"]) == 999
    finally:
        msl_binding.destroy(handle)


@pytest.mark.integration
def test_fresh_locomotion_guardreflect_front_door_uses_entry_pose_shield_math() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    # Modelplay/vanilla front door (desync_probe frame 112 -> vanilla raw -9):
    # - Fox is still Landing, Falco is still Run, and current-frame L press admits locomotion
    #   GuardReflect through ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50.
    # - GuardReflect locomotion entry immediately calls ftCo_800921DC, which zeroes the shield-joint
    #   translate and runs ftCo_80091E78(0), so the first entry frame uses the live current-pose
    #   shield center rather than the settled steady-Guard bubble.
    # - Vanilla keeps the laser moving straight past that fresh GuardReflect row (no GuardSetOff,
    #   no hitlag, no bounce) until a later real contact.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50,ftCo_800921DC,ftCo_80091E78}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{
    #   itFoxlaser_UnkMotion1_Phys,it_8029C4D4,itFoxLaser_Logic94_HitShield}
    shot_itkind, _, _ = _load_laser_shot_itkind_and_first_offset_x_and_lifetime(CHAR_FALCO)
    laser_radius, laser_offsets_x = _load_laser_shot_size_and_offsets_x(CHAR_FALCO)
    steady_xyz, guard_on_x20_xyz, guard_on_xyz, neutral_frame = _load_shield_pose_table(CHAR_FALCO)

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["char_id"][0, 1] = np.uint8(CHAR_FALCO)
    seed["facing"][0, 0] = np.uint8(1)
    seed["facing"][0, 1] = np.uint8(0)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)

    seed["action_id"][0, 0] = np.uint16(0x002A)  # Landing
    seed["action_frame"][0, 0] = np.int16(1)
    seed["seed_prev_action_id"][0, 0] = np.uint16(0x002A)
    seed["seed_prev_action_frame"][0, 0] = np.int16(0)
    seed["animation_index"][0, 0] = np.uint32(35)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["pos_x"][0, 0] = np.float32(-23.46000099182129)
    seed["pos_y"][0, 0] = np.float32(0.0001)
    seed["speed_ground_x_self"][0, 0] = np.float32(1.279998779296875)
    seed["speed_air_x_self"][0, 0] = np.float32(1.279998779296875)

    seed["action_id"][0, 1] = np.uint16(0x0015)  # Run
    seed["action_frame"][0, 1] = np.int16(15)
    seed["seed_prev_action_id"][0, 1] = np.uint16(0x0015)
    seed["seed_prev_action_frame"][0, 1] = np.int16(13)
    seed["animation_index"][0, 1] = np.uint32(13)
    seed["anim_frame_f32"][0, 1] = np.float32(15.0)
    seed["pos_x"][0, 1] = np.float32(9.189990997314453)
    seed["pos_y"][0, 1] = np.float32(0.0001)
    seed["speed_ground_x_self"][0, 1] = np.float32(-2.1999998092651367)
    seed["speed_air_x_self"][0, 1] = np.float32(-2.1999998092651367)
    seed["shield_hp"][0, 1] = np.float32(_common_attr("start_shield_health"))

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        prev_inp_view = prev_inp.view(INPUT_DTYPE).reshape((1,))
        prev_inp_view["p"]["main_x"][0, 1] = np.int8(-80)

        inp = _mk_input_bytes(1, input_stride)
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        inp_view["p"]["buttons"][0, 1] = np.uint16(BUTTON_L)
        inp_view["p"]["main_x"][0, 1] = np.int8(-80)
        inp_view["p"]["l"][0, 1] = TRIGGER_FULL

        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)

        out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out_cmp)
        cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]
        bubbles = msl_binding.debug_shield_bubbles_world(handle, 0)

        assert int(cmp0["action_id"][1]) == ACT_GUARD_REFLECT
        assert int(cmp0["action_frame"][1]) == -1
        assert int(cmp0["animation_index"][1]) == 0xFFFFFFFF
        assert int(cmp0["hitlag"][1]) == 0
        assert int(cmp0["hitstun"][1]) == 0
        shield_radius = float(bubbles[1, 3])
        assert shield_radius == pytest.approx(_full_shield_radius(CHAR_FALCO), abs=1e-5)

        defender_pos_x = float(seed["pos_x"][0, 1])
        defender_pos_y = float(seed["pos_y"][0, 1])
        defender_pos_z = 0.0
        defender_facing = int(seed["facing"][0, 1])
        steady_center = _shield_local_xyz_to_world(
            steady_xyz[neutral_frame],
            pos_x=defender_pos_x,
            pos_y=defender_pos_y,
            pos_z=defender_pos_z,
            facing=defender_facing,
        )
        entry_center = _shield_local_xyz_to_world(
            guard_on_xyz[0],
            pos_x=defender_pos_x,
            pos_y=defender_pos_y,
            pos_z=defender_pos_z,
            facing=defender_facing,
        )

        # Vanilla probe row (raw -10 -> -9): the Falco laser keeps traveling from x=4.341 to
        # x=9.341 at y=17.253 with no shield hit on the fresh GuardReflect entry. The settled steady
        # bubble would overlap this beam segment, while the GuardOn current-pose center does not.
        laser_x0 = 4.341033935546875
        laser_y0 = 17.252840042114258
        laser_vx = 5.0
        laser_vy = 0.0
        assert _laser_sweep_hits_shield(
            x0=laser_x0,
            y0=laser_y0,
            vx=laser_vx,
            vy=laser_vy,
            offsets_x=laser_offsets_x,
            laser_radius=laser_radius,
            shield_center=steady_center,
            shield_radius=shield_radius,
        )
        assert not _laser_sweep_hits_shield(
            x0=laser_x0,
            y0=laser_y0,
            vx=laser_vx,
            vy=laser_vy,
            offsets_x=laser_offsets_x,
            laser_radius=laser_radius,
            shield_center=entry_center,
            shield_radius=shield_radius,
        )
    finally:
        msl_binding.destroy(handle)


def test_dash_full_shield_analog_guard_entry_preserves_laser_for_guardon_frame() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    shot_itkind, _, _ = _load_laser_shot_itkind_and_first_offset_x_and_lifetime(CHAR_FALCO)

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
    seed["char_id"][0, 1] = np.uint8(CHAR_FALCO)
    seed["facing"][0, 0] = np.uint8(0)
    seed["facing"][0, 1] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)

    # Row-shaped Dash seed from GAT rec 9342 p0.
    seed["action_id"][0, 0] = np.uint16(ACT_DASH)
    seed["action_frame"][0, 0] = np.int16(10)
    seed["animation_index"][0, 0] = np.uint32(12)
    seed["shield_hp"][0, 0] = np.float32(_common_attr("start_shield_health"))
    seed["pos_x"][0, 0] = np.float32(10.4230547)
    seed["pos_y"][0, 0] = np.float32(0.0001)

    seed["action_id"][0, 1] = np.uint16(ACT_WAIT)
    seed["animation_index"][0, 1] = np.uint32(2)
    seed["pos_x"][0, 1] = np.float32(-39.7287598)
    seed["pos_y"][0, 1] = np.float32(0.0001)

    # Row-shaped overlapping Falco laser from the same family.
    seed["items"][0, 0]["exists"] = np.uint8(1)
    seed["items"][0, 0]["type"] = np.uint16(shot_itkind)
    seed["items"][0, 0]["owner"] = np.int8(1)
    seed["items"][0, 0]["instance_id"] = np.uint16(2073)
    seed["items"][0, 0]["direction"] = np.float32(1.0)
    seed["items"][0, 0]["vel_x"] = np.float32(5.0)
    seed["items"][0, 0]["vel_y"] = np.float32(0.0)
    seed["items"][0, 0]["pos_x"] = np.float32(-2.1847191)
    seed["items"][0, 0]["pos_y"] = np.float32(12.9127483)
    seed["items"][0, 0]["timer"] = np.float32(94.0)
    seed["items"][0, 0]["spawn_id"] = np.uint32(201)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        prev_inp = _mk_input_bytes(1, input_stride)
        inp = _mk_input_bytes(1, input_stride)
        inp_view = inp.view(INPUT_DTYPE).reshape((1,))
        # Analog-trigger guard path (GuardOn, not GuardReflect).
        inp_view["p"]["l"][0, 0] = TRIGGER_FULL

        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)

        out_cmp = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.write_compare(handle, out_cmp)
        cmp0 = out_cmp.view(COMPARE_DTYPE).reshape((1,))[0]

        assert int(cmp0["action_id"][0]) == ACT_GUARD_ON
        assert int(cmp0["action_frame"][0]) == -1
        assert int(cmp0["animation_index"][0]) == 0xFFFFFFFF
        assert int(cmp0["hitlag"][0]) == 0
        assert int(cmp0["hitstun"][0]) == 0
        assert int(cmp0["items"][0]["exists"]) == 1
        assert int(cmp0["items"][0]["owner"]) == 1
        assert int(cmp0["items"][0]["instance_id"]) == 2073
    finally:
        msl_binding.destroy(handle)
