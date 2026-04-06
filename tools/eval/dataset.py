from __future__ import annotations

# Dataset file format + NumPy dtypes.

import dataclasses
import struct
from typing import Final

import numpy as np


MAGIC: Final[bytes] = b"MSLDSLT "


HEADER_DTYPE = np.dtype(
    [
        ("magic", "S8"),
        ("record_size", "<u4"),
        ("num_records", "<u4"),
        ("num_players", "<u1"),
        ("_pad0", "V3"),
    ],
    align=False,
)


def _arr(dtype: str, n: int):
    return (dtype, (n,))


MAX_PLAYERS: Final[int] = 4
MAX_ITEMS: Final[int] = 15
HITLIST_GROUPS: Final[int] = 8
STALE_QUEUE_SIZE: Final[int] = 10  # decomp: refs/melee/src/melee/pl/types.h::StaleMoveTable.StaleMoves[10]

INPUT_PLAYER_DTYPE = np.dtype(
    [
        ("buttons", "<u2"),
        ("main_x", "i1"),
        ("main_y", "i1"),
        ("c_x", "i1"),
        ("c_y", "i1"),
        ("l", "u1"),
        ("r", "u1"),
    ],
    align=False,
)

INPUT_DTYPE = np.dtype([("p", INPUT_PLAYER_DTYPE, (MAX_PLAYERS,))], align=False)

ITEM_DTYPE = np.dtype(
    [
        ("exists", "u1"),
        ("state", "u1"),
        ("type", "<u2"),
        ("owner", "i1"),
        ("_pad0", "V1"),
        ("instance_id", "<u2"),
        ("attack_id", "<u2"),
        ("attack_instance", "<u2"),
        ("direction", "<f4"),
        ("vel_x", "<f4"),
        ("vel_y", "<f4"),
        ("pos_x", "<f4"),
        ("pos_y", "<f4"),
        ("damage", "<u2"),
        ("_pad1", "V2"),
        ("timer", "<f4"),
        ("spawn_id", "<u4"),
        ("misc0", "u1"),
        ("misc1", "u1"),
        ("misc2", "u1"),
        ("misc3", "u1"),
    ],
    align=False,
)

SEED_DTYPE = np.dtype(
    [
        ("frame_id", "<i4"),
        ("frame_pre_random_seed", "<u4"),
        ("stage_id", "<u4"),
        ("match_damage_ratio", "<f4"),
        ("num_players", "u1"),
        ("is_teams", "u1"),
        ("_pad0", "V2"),
        ("team_id", _arr("u1", MAX_PLAYERS)),
        ("char_id", _arr("u1", MAX_PLAYERS)),
        ("attack_ratio", _arr("<f4", MAX_PLAYERS)),
        ("defense_ratio", _arr("<f4", MAX_PLAYERS)),
        ("pos_x", _arr("<f4", MAX_PLAYERS)),
        ("pos_y", _arr("<f4", MAX_PLAYERS)),
        ("pos_z", _arr("<f4", MAX_PLAYERS)),
        ("speed_air_x_self", _arr("<f4", MAX_PLAYERS)),
        ("speed_ground_x_self", _arr("<f4", MAX_PLAYERS)),
        ("speed_y_self", _arr("<f4", MAX_PLAYERS)),
        ("speed_x_attack", _arr("<f4", MAX_PLAYERS)),
        ("speed_y_attack", _arr("<f4", MAX_PLAYERS)),
        ("fighter_scale_y", _arr("<f4", MAX_PLAYERS)),
        ("facing", _arr("u1", MAX_PLAYERS)),
        ("facing_dir1", _arr("i1", MAX_PLAYERS)),
        ("ground_friction_mul", _arr("<f4", MAX_PLAYERS)),
        ("kb_smashcharge_active", _arr("u1", MAX_PLAYERS)),
        ("on_ground", _arr("u1", MAX_PLAYERS)),
        ("_pad1", "V1"),
        ("action_id", _arr("<u2", MAX_PLAYERS)),
        ("action_frame", _arr("<i2", MAX_PLAYERS)),
        # Replay-true previous action snapshot (t-1 -> t), kept separate from runtime's per-step
        # cache so entry-shaped one-step rows can still see their real source motion.
        ("seed_prev_action_id", _arr("<u2", MAX_PLAYERS)),
        ("seed_prev_action_frame", _arr("<i2", MAX_PLAYERS)),
        # Throw-side projectile pulse consume lane (causal producer in make_dataset_from_slp.py).
        # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
        ("throw_pulse_consumed", _arr("u1", MAX_PLAYERS)),
        # Previous-step throw pulse crossing lane (strictly causal):
        # - 0 means no throw projectile pulse crossing in (t-1 -> t),
        # - N is the crossed pulse frame from extracted throw move events.
        ("throw_pulse_crossed_prev_frame", _arr("u1", MAX_PLAYERS)),
        # Source-owner clear countdown (`fp->dmg.x18C8`) with +1 bias.
        # - 0: inactive (decomp internal is -1)
        # - N>0: decomp timer value + 1
        # refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
        ("source_clear_timer_x18c8", _arr("u1", MAX_PLAYERS)),
        # Source-owner set phase lane for active x18C8 runs (strict-causal, t/t-1 only):
        # - 0: active run has no observed source-owner set edge backing.
        # - 1: active run is backed by a source-owner set edge (6 -> owner).
        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
        ("source_clear_owner_set_phase", _arr("u1", MAX_PLAYERS)),
        # Hidden ProcessHit damage-pending source-owner clear bridge (one-step transient).
        # - 0: no ProcessHit-owned clear override.
        # - 1: consume source-owner clear at the ProcessHit/ftCommon_800804FC ownership point.
        # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
        ("source_clear_processhit_damage_pending_phase", _arr("u1", MAX_PLAYERS)),
        # Hidden pre-gate Fighter_8006CDA4 RNG-consume phase bridge for DamageFlyRoll entry.
        # - 0: no seeded pre-gate consume ownership on this row
        # - 1: consume one pre-gate HSD_Randi before ftCo_8008DCE0 block_33
        # - 2: consume two pre-gate HSD_Randi calls before ftCo_8008DCE0 block_33
        # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
        ("damageflyroll_fighter_8006cda4_phase_hint", _arr("u1", MAX_PLAYERS)),
        # Grounded source-owner clear phase bridge (`ftCommon_800804FC` path).
        # - 0: no grounded clear-phase override.
        # - 1: consume grounded clear before x18C8 decrement for this one-step row.
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
        ("source_clear_grounded_damage_clear_phase", _arr("u1", MAX_PLAYERS)),
        # Terminal source-owner clear phase bridge for `source_clear_timer_x18c8 == 1` rows.
        # - 0: default terminal-clear behavior
        # - 1: defer terminal clear one frame
        # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        ("source_clear_terminal_phase", _arr("u1", MAX_PLAYERS)),
        # fp+0x2340 AttackDash lane (targeted seed ownership):
        # - mv.co.attackdash.x0 countdown consumed by ftCo_800D8AE0.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
        # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_800D8AE0
        # Slippi source lane: SendGamePostFrame emits fp+0x2340 as `misc_as`.
        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        ("attackdash_x0", _arr("<i2", MAX_PLAYERS)),
        # fp+0x2340 Attack1 lane:
        # - mv.co.attack1.x0 latched jab intent consumed by checkAttack12/checkAttack13.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{checkAttack12,checkAttack13}
        # Slippi source lane: SendGamePostFrame emits fp+0x2340 as `misc_as`.
        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        ("jab_x0", _arr("u1", MAX_PLAYERS)),
        ("match_flow_timer", _arr("u1", MAX_PLAYERS)),
        # Replay-visible camera-box visibility bit (`fp->x221F_b0`) promoted as an explicit seed
        # lane for F04 Rebirth/dead-flow ownership fixes.
        # refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
        # refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
        ("camera_box_visible_x221f_b0", _arr("u1", MAX_PLAYERS)),
        # Hidden Rebirth camera anchor Y (`fp->mv.co.common.x8`) promoted as a foundational seed
        # lane for F04 ownership work. Derived from current-row Rebirth state plus ISO respawn-point
        # data; on FD this is the respawn platform Y, not replay-visible fighter cur_pos.y.
        # refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
        # data/stages/final_destination.json: respawn_points
        ("rebirth_camera_anchor_y_f32", _arr("<f4", MAX_PLAYERS)),
        # Hidden fighter camera-subject world target (`camera_box->x1C`) and radius
        # (`camera_box->x34.z`) promoted as explicit seed lanes for F04 camera-target ownership.
        # refs/melee/src/melee/ft/ftlib.c::ftLib_800866DC
        # refs/melee/src/melee/ft/ftcamera.c::ftCamera_80076018
        # data/characters/{fox,falco}.json: camera_zoom_target_bone_part_id,
        #   camera_zoom_target_offset, camera_box_radius
        ("camera_target_world_x_f32", _arr("<f4", MAX_PLAYERS)),
        ("camera_target_world_y_f32", _arr("<f4", MAX_PLAYERS)),
        ("camera_target_world_z_f32", _arr("<f4", MAX_PLAYERS)),
        ("camera_box_radius_f32", _arr("<f4", MAX_PLAYERS)),
        ("downwait_timer", _arr("<i2", MAX_PLAYERS)),
        ("anim_frame_f32", _arr("<f4", MAX_PLAYERS)),
        ("frame_speed_mul_f32", _arr("<f4", MAX_PLAYERS)),
        # Walk callback source velocity lane (`mv_x0` consumed by ftWalkCommon_800DFDDC).
        # refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
        ("walk_anim_source_vel_f32", _arr("<f4", MAX_PLAYERS)),
        ("guard_tilt_x8", _arr("<u2", MAX_PLAYERS)),
        ("guard_tilt_x4", _arr("<f4", MAX_PLAYERS)),
        ("guard_reflect_timer_x14", _arr("u1", MAX_PLAYERS)),
        ("guard_reflect_timer_x18", _arr("u1", MAX_PLAYERS)),
        ("guard_release_latched_xc", _arr("u1", MAX_PLAYERS)),
        ("guard_x10", _arr("u1", MAX_PLAYERS)),
        ("lightshield_amount", _arr("<f4", MAX_PLAYERS)),
        # GuardSetOff hidden shield-hit int-damage lower bound (`fp->x19A4` consumer lane).
        # - Decomp owner: GuardSetOff entry anim-rate formula reads fp->x19A4.
        # - Seed bridge stores the minimum non-negative int damage consistent with the segment's
        #   entry hitlag, carried causally across the contiguous GuardSetOff segment.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
        ("guard_setoff_hitlag_damage_min", _arr("u1", MAX_PLAYERS)),
        ("jumps_left", _arr("u1", MAX_PLAYERS)),
        ("stocks", _arr("u1", MAX_PLAYERS)),
        ("kneebend_jump_input", _arr("u1", MAX_PLAYERS)),
        ("kneebend_is_short_hop", _arr("u1", MAX_PLAYERS)),
        ("tilt_timer_x", _arr("u1", MAX_PLAYERS)),
        ("tilt_timer_y", _arr("u1", MAX_PLAYERS)),
        ("fall_fast", _arr("u1", MAX_PLAYERS)),
        ("fall_fast_hitlag_exit_owner", _arr("u1", MAX_PLAYERS)),
        ("run_x0", _arr("u1", MAX_PLAYERS)),
        ("runbrake_cmd0", _arr("u1", MAX_PLAYERS)),
        ("dash_x4", _arr("u1", MAX_PLAYERS)),
        ("shine_release_lag", _arr("u1", MAX_PLAYERS)),
        ("shine_is_release", _arr("u1", MAX_PLAYERS)),
        ("ecb_lock_timer", _arr("u1", MAX_PLAYERS)),
        ("ledge_cooldown", _arr("u1", MAX_PLAYERS)),
        ("turn_frames_to_turn", _arr("u1", MAX_PLAYERS)),
        ("turn_has_turned", _arr("u1", MAX_PLAYERS)),
        ("turn_x8", _arr("i1", MAX_PLAYERS)),
        ("lr_press_timer", _arr("u1", MAX_PLAYERS)),
        ("x672_input_timer", _arr("u1", MAX_PLAYERS)),
        ("x673", _arr("u1", MAX_PLAYERS)),
        ("x674", _arr("u1", MAX_PLAYERS)),
        ("x675", _arr("u1", MAX_PLAYERS)),
        ("x676_x", _arr("u1", MAX_PLAYERS)),
        # Decomp: fp->x2228_b7 tracks most-recent fresh X-directional entry sign.
        # refs/melee/src/melee/ft/fighter.c:1924,1949
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
        ("x2228_b7", _arr("u1", MAX_PLAYERS)),
        ("x677_y", _arr("u1", MAX_PLAYERS)),
        ("x678", _arr("u1", MAX_PLAYERS)),
        ("x679_x", _arr("u1", MAX_PLAYERS)),
        ("x67A_y", _arr("u1", MAX_PLAYERS)),
        ("x67B", _arr("u1", MAX_PLAYERS)),
        ("x67C", _arr("u1", MAX_PLAYERS)),
        ("x67D", _arr("u1", MAX_PLAYERS)),
        ("x67E", _arr("u1", MAX_PLAYERS)),
        ("x680", _arr("u1", MAX_PLAYERS)),
        ("x681", _arr("u1", MAX_PLAYERS)),
        ("x682", _arr("u1", MAX_PLAYERS)),
        ("x683", _arr("u1", MAX_PLAYERS)),
        ("x684", _arr("u1", MAX_PLAYERS)),
        ("ucf_padbuf_index", _arr("u1", MAX_PLAYERS)),
        ("ucf_padbuf_sdrop_up_frames", _arr("u1", MAX_PLAYERS)),
        ("ucf_padbuf_stick_x", ("i1", (MAX_PLAYERS, 4))),
        ("ucf_padbuf_stick_y", ("i1", (MAX_PLAYERS, 4))),
        ("percent", _arr("<f4", MAX_PLAYERS)),
        # Damage pipeline gates (ftColl_80079AB0 non-WSK else-branch).
        # - dmg_x2225_b7 corresponds to Fighter fp+0x2225 bit0 (LSB).
        # - dmg_x2224_b2 corresponds to Fighter fp+0x2224 bit5 (mask 0x20).
        ("dmg_x2225_b7", _arr("u1", MAX_PLAYERS)),
        ("dmg_x2224_b2", _arr("u1", MAX_PLAYERS)),
        ("shield_hp", _arr("<f4", MAX_PLAYERS)),
        ("hitlag", _arr("<u2", MAX_PLAYERS)),
        ("hitstun", _arr("<u2", MAX_PLAYERS)),
        ("damage_jump_buffer_x14", _arr("<u2", MAX_PLAYERS)),
        ("l_cancel", _arr("u1", MAX_PLAYERS)),
        ("hurtbox_state", _arr("u1", MAX_PLAYERS)),
        ("colanim_hit_status_x198c", _arr("u1", MAX_PLAYERS)),
        ("colanim_lock_x2221_b0", _arr("u1", MAX_PLAYERS)),
        ("colanim_timer_x1990", _arr("<u2", MAX_PLAYERS)),
        ("colanim_timer_x1994", _arr("<u2", MAX_PLAYERS)),
        ("ground_id", _arr("<u2", MAX_PLAYERS)),
        ("animation_index", _arr("<u4", MAX_PLAYERS)),
        ("instance_hit_by", _arr("<u2", MAX_PLAYERS)),
        ("instance_id", _arr("<u2", MAX_PLAYERS)),
        # Fighter action-state instance_id compare byte (GALE01 fp+0x2073 within fp->x2070).
        # Used by ft_800895E0 to gate instance_id bumps on motion-state change.
        # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
        ("instance_id_x2073", _arr("u1", MAX_PLAYERS)),
        # Seeded next value for plAttack_80037B08 (global instance_id counter).
        # Slippi does not expose this directly; preprocessing derives it causally from replay history.
        ("instance_id_counter", "<u2"),
        ("attack_id", _arr("<u2", MAX_PLAYERS)),
        ("attack_instance", _arr("<u2", MAX_PLAYERS)),
        ("last_attack_landed", _arr("u1", MAX_PLAYERS)),
        ("combo_count", _arr("u1", MAX_PLAYERS)),
        ("combo_victim_port", _arr("u1", MAX_PLAYERS)),
        ("combo_victim_instance_id", _arr("<u2", MAX_PLAYERS)),
        ("combo_timer_x2098", _arr("<u2", MAX_PLAYERS)),
        ("last_hit_by", _arr("u1", MAX_PLAYERS)),
        ("grab_owner_port", _arr("u1", MAX_PLAYERS)),
        ("grab_mash_stick_x_sign", _arr("i1", MAX_PLAYERS)),
        ("grab_mash_stick_y_sign", _arr("i1", MAX_PLAYERS)),
        ("_pad2", "V1"),
        ("state_flags", ("u1", (MAX_PLAYERS, 5))),
        ("combat_hitlist_cd", ("<u2", (MAX_PLAYERS, HITLIST_GROUPS, MAX_PLAYERS))),
        ("combat_hitlist_victim_iid", ("<u2", (MAX_PLAYERS, HITLIST_GROUPS, MAX_PLAYERS))),
        ("stale_queue_index", _arr("u1", MAX_PLAYERS)),
        ("stale_move_id", ("<u2", (MAX_PLAYERS, STALE_QUEUE_SIZE))),
        ("stale_attack_instance", ("<u2", (MAX_PLAYERS, STALE_QUEUE_SIZE))),
        # Damage hitlag-exit callback lane (decomp: fp->post_hitlag_cb = ftCo_Damage_OnExitHitlag).
        # Slippi does not expose callback pointers; this seeded lane is derived causally from replay
        # history for one-step reseed parity.
        ("damage_post_hitlag_cb_kind", _arr("u1", MAX_PLAYERS)),
        ("item_reflect_damage_mul", _arr("<f4", MAX_ITEMS)),
        # NOTE (PP#4): these staling fields are populated by replay-history preprocessing:
        # tools/slippi/staling_history.py (derive) and tools/slippi/make_dataset_from_slp.py (wire).
        # They seed the per-player `StaleMoveTable` ring buffer and `attack_instance`.
        ("items", ITEM_DTYPE, (MAX_ITEMS,)),
    ],
    align=False,
)

COMPARE_DTYPE = np.dtype(
    [
        ("frame_id", "<i4"),
        ("frame_pre_random_seed", "<u4"),
        ("stage_id", "<u4"),
        ("num_players", "u1"),
        ("is_teams", "u1"),
        ("_pad0", "V2"),
        ("team_id", _arr("u1", MAX_PLAYERS)),
        ("char_id", _arr("u1", MAX_PLAYERS)),
        ("pos_x", _arr("<f4", MAX_PLAYERS)),
        ("pos_y", _arr("<f4", MAX_PLAYERS)),
        ("speed_air_x_self", _arr("<f4", MAX_PLAYERS)),
        ("speed_ground_x_self", _arr("<f4", MAX_PLAYERS)),
        ("speed_y_self", _arr("<f4", MAX_PLAYERS)),
        ("speed_x_attack", _arr("<f4", MAX_PLAYERS)),
        ("speed_y_attack", _arr("<f4", MAX_PLAYERS)),
        ("facing", _arr("u1", MAX_PLAYERS)),
        ("on_ground", _arr("u1", MAX_PLAYERS)),
        ("is_dead", _arr("u1", MAX_PLAYERS)),
        ("_pad1", "V1"),
        ("action_id", _arr("<u2", MAX_PLAYERS)),
        ("action_frame", _arr("<i2", MAX_PLAYERS)),
        ("jumps_left", _arr("u1", MAX_PLAYERS)),
        ("stocks", _arr("u1", MAX_PLAYERS)),
        ("percent", _arr("<f4", MAX_PLAYERS)),
        ("shield_hp", _arr("<f4", MAX_PLAYERS)),
        ("hitlag", _arr("<u2", MAX_PLAYERS)),
        ("hitstun", _arr("<u2", MAX_PLAYERS)),
        ("l_cancel", _arr("u1", MAX_PLAYERS)),
        ("hurtbox_state", _arr("u1", MAX_PLAYERS)),
        ("ground_id", _arr("<u2", MAX_PLAYERS)),
        ("animation_index", _arr("<u4", MAX_PLAYERS)),
        ("instance_hit_by", _arr("<u2", MAX_PLAYERS)),
        ("instance_id", _arr("<u2", MAX_PLAYERS)),
        ("last_attack_landed", _arr("u1", MAX_PLAYERS)),
        ("combo_count", _arr("u1", MAX_PLAYERS)),
        ("last_hit_by", _arr("u1", MAX_PLAYERS)),
        ("_pad2", "V1"),
        ("state_flags", ("u1", (MAX_PLAYERS, 5))),
        ("items", ITEM_DTYPE, (MAX_ITEMS,)),
    ],
    align=False,
)

SAMPLE_DTYPE = np.dtype(
    [
        ("seed_t", SEED_DTYPE),
        ("prev_input_t", INPUT_DTYPE),
        ("input_t", INPUT_DTYPE),
        ("ref_t1", COMPARE_DTYPE),
    ],
    align=False,
)


@dataclasses.dataclass(frozen=True)
class Dataset:
    header: np.ndarray
    samples: np.ndarray


def write_dataset(path: str, num_players: int, samples: np.ndarray) -> None:
    if num_players not in (2, 4):
        raise ValueError(f"num_players must be 2 or 4, got {num_players}")
    if samples.dtype != SAMPLE_DTYPE:
        raise ValueError(f"samples dtype mismatch: got {samples.dtype}, want {SAMPLE_DTYPE}")

    header = np.zeros((), dtype=HEADER_DTYPE)
    header["magic"] = MAGIC
    header["record_size"] = samples.dtype.itemsize
    header["num_records"] = samples.shape[0]
    header["num_players"] = num_players

    with open(path, "wb") as f:
        f.write(header.tobytes(order="C"))
        f.write(samples.tobytes(order="C"))


def read_dataset(path: str) -> Dataset:
    with open(path, "rb") as f:
        header_bytes = f.read(HEADER_DTYPE.itemsize)
        if len(header_bytes) != HEADER_DTYPE.itemsize:
            raise ValueError("file too small for header")
        header = np.frombuffer(header_bytes, dtype=HEADER_DTYPE, count=1)[0]
        if bytes(header["magic"]) != MAGIC:
            raise ValueError(f"bad magic: {header['magic']!r}")

        record_size = int(header["record_size"])
        if record_size != SAMPLE_DTYPE.itemsize:
            raise ValueError(
                f"record_size mismatch: file={record_size} dtype={SAMPLE_DTYPE.itemsize}"
            )
        num_records = int(header["num_records"])
        samples_bytes = f.read(record_size * num_records)
        if len(samples_bytes) != record_size * num_records:
            raise ValueError("file truncated")
        samples = np.frombuffer(samples_bytes, dtype=SAMPLE_DTYPE, count=num_records)

    return Dataset(header=header, samples=samples)
