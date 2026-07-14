from __future__ import annotations

import os
from pathlib import Path
import subprocess
import textwrap

def test_terminal_fall_from_damage_owner_does_not_require_concrete_cap(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    source = tmp_path / "damage_terminal_owner_capless_test.c"
    exe = tmp_path / "damage_terminal_owner_capless_test"
    source.write_text(
        textwrap.dedent(
            """
            #include <assert.h>
            #include <string.h>

            #include "batch_internal.h"
            #include "damage_terminal_owner.h"

            static MslBatch batch;
            static uint16_t action_id[MSL_MAX_PLAYERS];
            static uint16_t prev_action_id[MSL_MAX_PLAYERS];
            static uint16_t hitlag[MSL_MAX_PLAYERS];
            static uint16_t hitstun[MSL_MAX_PLAYERS];
            static uint16_t instance_hit_by[MSL_MAX_PLAYERS];
            static uint8_t char_id[MSL_MAX_PLAYERS];
            static uint8_t hitbox_enable_edge[MSL_MAX_PLAYERS * MSL_MAX_HITBOXES];

            uint32_t msl_motion_state_common_class_bits_by_action
                [MSL_MOTION_STATE_COMMON_ACTION_CAP] = {
                    [MSL_ACT_DAMAGE_FLY_HI] = MSL_MS_CLASS_DAMAGE_FLY,
                    [MSL_ACT_DAMAGE_FLY_N] = MSL_MS_CLASS_DAMAGE_FLY,
                    [MSL_ACT_DAMAGE_FLY_LW] = MSL_MS_CLASS_DAMAGE_FLY,
                    [MSL_ACT_DAMAGE_FLY_TOP] = MSL_MS_CLASS_DAMAGE_FLY,
                    [MSL_ACT_DAMAGE_FLY_ROLL] = MSL_MS_CLASS_DAMAGE_FLY,
                };

            static void bind_state(void) {
              memset(&batch, 0, sizeof(batch));
              batch.state.action_id = action_id;
              batch.state.prev_action_id = prev_action_id;
              batch.state.hitlag = hitlag;
              batch.state.hitstun = hitstun;
              batch.state.instance_hit_by = instance_hit_by;
              batch.state.char_id = char_id;
              batch.state.hitbox_enable_edge = hitbox_enable_edge;
            }

            int main(void) {
              bind_state();
              batch.state.hitbox_enable_edge[0] = 1u;
              batch.state.action_id[0] = (uint16_t)MSL_ACT_ATTACK_HI3;
              batch.state.prev_action_id[0] = (uint16_t)MSL_ACT_ATTACK_HI3;
              batch.state.action_id[1] = (uint16_t)MSL_ACT_FALL;
              batch.state.prev_action_id[1] = (uint16_t)MSL_ACT_DAMAGE_FALL;

              assert(msl_damage_owner_terminal_state_blocks_enable_edge_body(
                         &batch, 0u, 0u, 1u, 0u, 0u) == 1u);

              batch.state.prev_action_id[1] = (uint16_t)MSL_ACT_DAMAGE_FLY_TOP;
              assert(msl_damage_owner_terminal_state_blocks_enable_edge_body(
                         &batch, 0u, 0u, 1u, 0u, 0u) == 1u);

              batch.state.prev_action_id[1] = (uint16_t)MSL_ACT_WAIT;
              assert(msl_damage_owner_terminal_state_blocks_enable_edge_body(
                         &batch, 0u, 0u, 1u, 0u, 0u) == 0u);

              batch.state.action_id[1] = (uint16_t)MSL_ACT_DAMAGE_FLY_TOP;
              batch.state.prev_action_id[1] = (uint16_t)MSL_ACT_DAMAGE_FLY_TOP;
              batch.state.char_id[1] = (uint8_t)MSL_CHAR_ID_FOX;
              batch.state.instance_hit_by[1] = 77u;

              assert(msl_damage_owner_terminal_state_blocks_enable_edge_body(
                         &batch, 0u, 0u, 1u, 0u, 0u) == 0u);
              assert(msl_damage_owner_terminal_state_blocks_enable_edge_body(
                         &batch, 0u, 0u, 1u, MSL_DAMAGE_OWNER_FOX_DYNAMIC_TAIL_PART_ID, 1u) == 1u);

              return 0;
            }
            """
        ),
        encoding="utf-8",
    )

    subprocess.run(
        [
            os.environ.get("CC", "cc"),
            "-std=c11",
            "-I",
            str(root / "src"),
            str(source),
            "-o",
            str(exe),
        ],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
