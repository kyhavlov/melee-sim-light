from __future__ import annotations

import numpy as np
import pytest

from tools.slippi.seed_history import derive_ecb_lock_state, derive_ecb_lock_timer


def test_ecb_lock_timer_derivation_basic_countdown() -> None:
    # Post-frame grounding history with two grounded->air transitions.
    on_ground = np.array(
        [
            1,  # grounded
            1,  # grounded
            0,  # takeoff frame -> lock set then same-frame tick => 9
            0,  # 8
            0,  # 7
            1,  # landing clears
            0,  # second takeoff -> 9
            0,  # 8
        ],
        dtype=np.uint8,
    )
    action_id = np.array(
        [
            0x000E,
            0x000E,
            0x0019,  # JumpF entry
            0x0019,
            0x0019,
            0x000E,
            0x001B,  # JumpAerialF entry
            0x001B,
        ],
        dtype=np.uint16,
    )
    got = derive_ecb_lock_timer(
        on_ground_u8=on_ground,
        action_id_u16=action_id,
        lock_frames_ground_to_air=10,
    )
    want = np.array([0, 0, 9, 8, 7, 0, 9, 8], dtype=np.uint8)
    assert np.array_equal(got, want)


def test_ecb_lock_timer_derivation_is_prefix_invariant() -> None:
    on_ground = np.array([1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0], dtype=np.uint8)
    action_id = np.array([0x000E, 0x000E, 0x0019, 0x0019, 0x0019, 0x0019, 0x000E, 0x001B, 0x001B, 0x001B, 0x000E, 0x000E, 0x001A, 0x001A], dtype=np.uint16)
    full = derive_ecb_lock_timer(
        on_ground_u8=on_ground,
        action_id_u16=action_id,
        lock_frames_ground_to_air=10,
    )
    for k in (1, 2, 3, 4, 6, 8, 10, int(on_ground.size)):
        got = derive_ecb_lock_timer(
            on_ground_u8=on_ground[:k],
            action_id_u16=action_id[:k],
            lock_frames_ground_to_air=10,
        )
        assert np.array_equal(got, full[:k])


def test_falcon_ecb_lock_timer_uses_callback_phase_owners() -> None:
    # Falcon's alternate helper is called from both pre-map Anim/entry callbacks (post value 4)
    # and post-decrement Coll callbacks (post value 5). Dive Throw0 refreshes every Anim frame.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHiThrow0_Anim
    char = np.full((10,), 2, dtype=np.uint8)
    action = np.array(
        [
            0x001D,  # Fall
            0x015F,  # SpecialAirSStart entry: pre-map 8007D60C
            0x015F,
            0x0160,  # hit transition: no refresh
            0x0023,  # aerial Raptor anim end: pre-map 8007D60C
            0x0163,  # SpecialHiCatch
            0x0164,  # Throw0 entry: pre-map 8007D5D4 release owner
            0x0164,  # sustained Throw0: pre-map 8007D60C every frame
            0x015D,  # grounded SpecialSStart
            0x0023,  # grounded Raptor floor loss: Coll-time 8007D60C
        ],
        dtype=np.uint16,
    )
    action_frame = np.array([5, 0, 1, 0, 0, 8, 0, 1, 12, 0], dtype=np.int16)
    on_ground = np.array([0, 0, 0, 0, 0, 0, 0, 0, 1, 0], dtype=np.uint8)

    got, owner = derive_ecb_lock_state(
        on_ground_u8=on_ground,
        action_id_u16=action,
        char_id_u8=char,
        action_frame_i16=action_frame,
    )

    assert got.tolist() == [0, 4, 3, 2, 4, 3, 9, 4, 0, 5]
    assert owner.tolist() == [0, 4, 4, 4, 4, 4, 4, 4, 0, 4]


def test_falcon_ecb_lock_owner_persists_across_sustained_special_and_fall_rows() -> None:
    # The callback writes CollData_X130_Locked once; Fighter_procMap only decrements the timer on
    # later rows, so provenance survives same-action rows and Fall/FallSpecial destinations.
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_8007D60C}
    # refs/melee/src/melee/ft/fighter.c::Fighter_procMap
    timer, owner = derive_ecb_lock_state(
        on_ground_u8=np.array([1, 0, 0, 0, 0, 0], dtype=np.uint8),
        action_id_u16=np.array([0x015B, 0x015C, 0x015C, 0x001D, 0x001D, 0x0023], dtype=np.uint16),
        char_id_u8=np.full((6,), 2, dtype=np.uint8),
        action_frame_i16=np.array([20, 20, 21, 0, 1, 0], dtype=np.int16),
    )

    assert timer.tolist() == [0, 10, 9, 8, 7, 6]
    assert owner.tolist() == [0, 4, 4, 4, 4, 4]

    for end in range(1, len(timer) + 1):
        prefix_timer, prefix_owner = derive_ecb_lock_state(
            on_ground_u8=np.array([1, 0, 0, 0, 0, 0], dtype=np.uint8)[:end],
            action_id_u16=np.array(
                [0x015B, 0x015C, 0x015C, 0x001D, 0x001D, 0x0023], dtype=np.uint16
            )[:end],
            char_id_u8=np.full((end,), 2, dtype=np.uint8),
            action_frame_i16=np.array([20, 20, 21, 0, 1, 0], dtype=np.int16)[:end],
        )
        assert np.array_equal(prefix_timer, timer[:end])
        assert np.array_equal(prefix_owner, owner[:end])


def test_falcon_dive_throw_refresh_precedes_damage_interruption() -> None:
    # Throw0_Anim unconditionally refreshes the five-frame lock before a later ProcessHit may enter
    # DamageFlyN, so both post-frame rows retain timer 4 and live ftCommon ownership.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHiThrow0_Anim
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_ProcessHit_8006D1EC}
    timer, owner = derive_ecb_lock_state(
        on_ground_u8=np.array([0, 0], dtype=np.uint8),
        action_id_u16=np.array([0x0164, 0x0058], dtype=np.uint16),
        char_id_u8=np.full((2,), 2, dtype=np.uint8),
        action_frame_i16=np.array([1, 0], dtype=np.int16),
    )

    assert timer.tolist() == [4, 4]
    assert owner.tolist() == [4, 4]


def test_falcon_ground_raptor_damage_entry_uses_normal_ground_to_air_lock() -> None:
    # A grounded Raptor state does not by itself prove the Coll callback's floor-loss branch.
    # Incoming damage owns the ordinary ftCommon_8007D5D4 ground-to-air lock instead.
    # refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialS_Coll
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_8007D60C}
    got, owner = derive_ecb_lock_state(
        on_ground_u8=np.array([1, 0, 0], dtype=np.uint8),
        action_id_u16=np.array([0x015E, 0x0058, 0x0058], dtype=np.uint16),
        char_id_u8=np.full((3,), 2, dtype=np.uint8),
        action_frame_i16=np.array([1, 1, 1], dtype=np.int16),
    )

    assert got.tolist() == [0, 9, 8]
    assert owner.tolist() == [0, 1, 1]


@pytest.mark.parametrize(
    "prev_action",
    (0x015B, 0x0161, 0x0162, 0x0163, 0x0165, 0x0166, 0x016A),
)
@pytest.mark.parametrize(
    "damage_action",
    (0x0026, 0x0054, 0x0057, 0x0058, 0x0059, 0x005A, 0x005B),
)
def test_grounded_falcon_special_damage_output_never_claims_live_ftcommon_owner(
    prev_action: int, damage_action: int
) -> None:
    # Incoming ProcessHit runs after the special Coll callback. Its Damage* output explains the
    # post-map ten-frame ground-to-air timer but does not prove that Coll took a floor-loss branch.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_procMap,Fighter_ProcessHit_8006D1EC}
    timer, owner = derive_ecb_lock_state(
        on_ground_u8=np.array([1, 0, 0], dtype=np.uint8),
        action_id_u16=np.array([prev_action, damage_action, damage_action], dtype=np.uint16),
        char_id_u8=np.full((3,), 2, dtype=np.uint8),
        action_frame_i16=np.array([5, 0, 1], dtype=np.int16),
    )

    assert timer.tolist() == [0, 10, 9]
    assert owner.tolist() == [0, 1, 1]


@pytest.mark.parametrize(
    ("prev_action", "floor_loss_action"),
    (
        (0x015B, 0x015C),
        (0x0161, 0x0161),
        (0x0162, 0x0162),
        (0x0165, 0x0165),
        (0x0166, 0x0166),
        (0x016A, 0x016A),
    ),
)
def test_grounded_falcon_special_exact_floor_loss_output_claims_live_ftcommon_owner(
    prev_action: int, floor_loss_action: int
) -> None:
    timer, owner = derive_ecb_lock_state(
        on_ground_u8=np.array([1, 0], dtype=np.uint8),
        action_id_u16=np.array([prev_action, floor_loss_action], dtype=np.uint16),
        char_id_u8=np.full((2,), 2, dtype=np.uint8),
        action_frame_i16=np.array([5, 6], dtype=np.int16),
    )

    assert timer.tolist() == [0, 10]
    assert owner.tolist() == [0, 4]
