from __future__ import annotations

import os
from pathlib import Path
import subprocess
import textwrap

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, SEED_DTYPE


HIT_GROUNDED = 1 << 9
HIT_AERIAL = 1 << 10

ACT_WAIT = 0x000E
ACT_ATTACK_HI3 = 0x0038
ACT_DAMAGE_FLY_HI = 0x0057
ACT_DAMAGE_FLY_N = 0x0058
ACT_DAMAGE_FLY_LW = 0x0059
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_DAMAGE_FLY_ROLL = 0x005B

SM_ATTACK_HI3 = 58
SM_DAMAGE_FLY_HI = 177
SM_DAMAGE_FLY_N = 178
SM_DAMAGE_FLY_LW = 179
SM_DAMAGE_FLY_TOP = 180
SM_DAMAGE_FLY_ROLL = 181

CHAR_FOX = 1
CHAR_FALCO = 22
STAGE_FD = 32
FOX_TAIL_CAP_ID = 12
FOX_NON_TAIL_CAP_ID = 0


def _debug_attackhi3_terminal_damagefly_body_hit_applied(
    *,
    defender_char: int = CHAR_FOX,
    defender_action: int = ACT_DAMAGE_FLY_TOP,
    defender_motion: int = SM_DAMAGE_FLY_TOP,
    defender_cap_id: int = FOX_TAIL_CAP_ID,
    enable_edge: bool = True,
    same_attacker_action: bool = True,
) -> bool:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FALCO)
    seed["char_id"][0, 1] = np.uint8(defender_char)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["on_ground"][0, 1] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["ground_id"][0, 1] = np.uint16(0xFFFF)
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_HI3)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_ATTACK_HI3 if same_attacker_action else ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_HI3)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    seed["action_id"][0, 1] = np.uint16(defender_action)
    seed["seed_prev_action_id"][0, 1] = np.uint16(defender_action)
    seed["animation_index"][0, 1] = np.uint32(defender_motion)
    seed["action_frame"][0, 1] = np.int16(54)
    seed["anim_frame_f32"][0, 1] = np.float32(54.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["instance_id"][0, 0] = np.uint16(111)
    seed["instance_id"][0, 1] = np.uint16(222)
    seed["instance_hit_by"][0, 1] = np.uint16(77)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        binding.debug_set_prev_action_id(
            handle, 0, 0, ACT_ATTACK_HI3 if same_attacker_action else ACT_WAIT
        )
        binding.debug_set_prev_action_id(handle, 0, 1, defender_action)
        binding.debug_clear_hitboxes_world(handle, 0, 0)
        binding.debug_clear_hurtcaps_world(handle, 0, 1)
        binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 9.0, 1)
        binding.debug_set_hitbox_flags(handle, 0, 0, 0, HIT_GROUNDED | HIT_AERIAL)
        binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
        binding.debug_set_hitbox_enable_edge(handle, 0, 0, 0, 1 if enable_edge else 0)
        binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 90, 100, 0, 20)
        binding.debug_set_hurtcap_world(
            handle, 0, 1, defender_cap_id, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5
        )
        binding.debug_set_hurtcap_height(handle, 0, 1, defender_cap_id, 1)
        binding.debug_combat_resolve(handle)
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
        return int(out["hitlag"][0]) > 0 and int(out["hitlag"][1]) > 0
    finally:
        binding.destroy(handle)


@pytest.mark.parametrize(
    ("defender_action", "defender_motion"),
    [
        (ACT_DAMAGE_FLY_HI, SM_DAMAGE_FLY_HI),
        (ACT_DAMAGE_FLY_N, SM_DAMAGE_FLY_N),
        (ACT_DAMAGE_FLY_LW, SM_DAMAGE_FLY_LW),
        (ACT_DAMAGE_FLY_TOP, SM_DAMAGE_FLY_TOP),
        (ACT_DAMAGE_FLY_ROLL, SM_DAMAGE_FLY_ROLL),
    ],
)
def test_attackhi3_create_edge_terminal_damagefly_family_tail_suppresses_body(
    defender_action: int, defender_motion: int
) -> None:
    # Source-shaped synthetic lock for the broader MSLMSO01 DamageFly family:
    # terminal damage callback ownership is not Top-specific, but the current runtime correction is
    # limited to Fox's part-18 dynamic tail chain and a same-action ftAction_8007121C enable edge.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008F744,ftCo_DamageFly_IASA}
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    assert (
        _debug_attackhi3_terminal_damagefly_body_hit_applied(
            defender_action=defender_action,
            defender_motion=defender_motion,
            defender_cap_id=FOX_TAIL_CAP_ID,
            enable_edge=True,
            same_attacker_action=True,
        )
        is False
    )


@pytest.mark.parametrize(
    ("case", "kwargs"),
    [
        ("already-live same hitbox", {"enable_edge": False}),
        ("non-Fox target", {"defender_char": CHAR_FALCO}),
        ("non-tail Fox cap", {"defender_cap_id": FOX_NON_TAIL_CAP_ID}),
        ("same-frame new action entry", {"same_attacker_action": False}),
    ],
)
def test_attackhi3_terminal_damagefly_boundary_controls_stay_on_body_path(
    case: str, kwargs: dict[str, object]
) -> None:
    assert _debug_attackhi3_terminal_damagefly_body_hit_applied(**kwargs) is True, case


def test_terminal_fall_from_damage_owner_does_not_require_concrete_cap(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    source = tmp_path / "damage_terminal_owner_capless_test.c"
    exe = tmp_path / "damage_terminal_owner_capless_test"
    source.write_text(
        textwrap.dedent(
            """
            #include <assert.h>
            #include <stdlib.h>
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

            /* The weak common-class table above is defined in this TU, so the _fast helper's
             * out-of-line fallback is unreachable. Mach-O still resolves relocations in dead
             * branches at -O0 (ELF/GCC folds the defined-weak null check away), so satisfy the
             * linker with a stub that fails loudly if the contract is ever broken. */
            uint8_t msl_motion_state_common_class_has(uint16_t action_id, uint32_t class_bit) {
              (void)action_id;
              (void)class_bit;
              abort();
            }

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
