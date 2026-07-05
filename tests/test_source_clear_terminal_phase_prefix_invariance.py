from __future__ import annotations

import numpy as np

from melee_sim import _native as msl_binding
from tools.slippi.validation_buffer_builder import build_validation_buffers_from_slp
from tools.slippi.validation_buffer_seed import _derive_source_clear_terminal_phase_seed_lane


def _derive_terminal_phase(
    *,
    action_id: np.ndarray,
    action_frame: np.ndarray,
    state_flags: np.ndarray,
    combo_count: np.ndarray | None = None,
    last_attack_landed: np.ndarray | None = None,
    last_hit_by: np.ndarray | None = None,
) -> np.ndarray:
    n = int(action_id.shape[0])
    return _derive_source_clear_terminal_phase_seed_lane(
        char_id_u8=np.ones(n, dtype=np.uint8),
        action_id_u16=np.asarray(action_id, dtype=np.uint16),
        action_frame_i16=np.asarray(action_frame, dtype=np.int16),
        hitlag_u16=np.zeros(n, dtype=np.uint16),
        hitstun_u16=np.zeros(n, dtype=np.uint16),
        combo_count_u8=np.ones(n, dtype=np.uint8) if combo_count is None else combo_count,
        last_attack_landed_u8=(
            np.full(n, 15, dtype=np.uint8) if last_attack_landed is None else last_attack_landed
        ),
        source_clear_timer_x18c8_u8=np.array([2, 1], dtype=np.uint8),
        source_clear_owner_set_phase_u8=np.array([1, 1], dtype=np.uint8),
        state_flags_u8=np.asarray(state_flags, dtype=np.uint8),
        last_hit_by_u8=np.array([1, 1], dtype=np.uint8) if last_hit_by is None else last_hit_by,
        terminal_followup_cmd0_on_by_char_action={(1, 0x0041): 4, (1, 0x0045): 5, (1, 0x00EC): 30},
        terminal_followup_cmd0_off_by_char_action={(1, 0x0041): 37, (1, 0x0045): 31, (1, 0x00EC): -1},
    )


def test_source_clear_terminal_phase_prefix_invariance_suffix_mutation() -> None:
    # Guard: seed lane derivation must be strictly causal (t / t-1->t only).
    # Mutating suffix rows must not change derived prefix rows.
    n = 8
    # Use a decomp-defined downed recovery action id that is in the modeled allowlist.
    # refs/melee/src/melee/ft/chara/ftCommon/forward.h (ftCo_MS_DownFowardD = 0x00C4)
    action_id = np.array([0x00C4, 0x00C4, 0x00C4, 0x00C4, 0x00C4, 0x00C4, 0x00C4, 0x00C4], dtype=np.uint16)
    char_id = np.array([1, 1, 1, 1, 1, 1, 1, 1], dtype=np.uint8)
    action_frame = np.array([5, 6, 7, 8, 9, 10, 11, 12], dtype=np.int16)
    hitlag = np.zeros(n, dtype=np.uint16)
    hitstun = np.zeros(n, dtype=np.uint16)
    combo_count = np.ones(n, dtype=np.uint8)
    last_attack_landed = np.full(n, 15, dtype=np.uint8)
    source_clear_timer = np.array([3, 2, 1, 0, 0, 2, 1, 0], dtype=np.uint8)
    source_owner_set_phase = np.array([1, 1, 1, 0, 0, 1, 1, 0], dtype=np.uint8)
    state_flags = np.zeros((n, 5), dtype=np.uint8)
    last_hit_by = np.array([1, 1, 1, 6, 6, 0, 0, 6], dtype=np.uint8)

    base = _derive_source_clear_terminal_phase_seed_lane(
        char_id_u8=char_id,
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        hitlag_u16=hitlag,
        hitstun_u16=hitstun,
        combo_count_u8=combo_count,
        last_attack_landed_u8=last_attack_landed,
        source_clear_timer_x18c8_u8=source_clear_timer,
        source_clear_owner_set_phase_u8=source_owner_set_phase,
        state_flags_u8=state_flags,
        last_hit_by_u8=last_hit_by,
        terminal_followup_cmd0_on_by_char_action={(1, 0x0041): 4, (1, 0x0045): 5, (1, 0x00EC): 30},
        terminal_followup_cmd0_off_by_char_action={(1, 0x0041): 37, (1, 0x0045): 31, (1, 0x00EC): -1},
    )

    # Mutate only suffix rows [cutoff:].
    cutoff = 5
    mut_action_id = action_id.copy()
    mut_action_frame = action_frame.copy()
    mut_hitlag = hitlag.copy()
    mut_hitstun = hitstun.copy()
    mut_combo_count = combo_count.copy()
    mut_last_attack_landed = last_attack_landed.copy()
    mut_source_clear_timer = source_clear_timer.copy()
    mut_source_owner_set_phase = source_owner_set_phase.copy()
    mut_state_flags = state_flags.copy()
    mut_last_hit_by = last_hit_by.copy()

    mut_action_id[cutoff:] = np.array([0x0014, 0x0014, 0x0014], dtype=np.uint16)
    mut_action_frame[cutoff:] = np.array([1, 2, 3], dtype=np.int16)
    mut_hitlag[cutoff:] = np.array([0, 1, 0], dtype=np.uint16)
    mut_hitstun[cutoff:] = np.array([0, 2, 0], dtype=np.uint16)
    mut_combo_count[cutoff:] = np.array([1, 0, 1], dtype=np.uint8)
    mut_last_attack_landed[cutoff:] = np.array([12, 0, 12], dtype=np.uint8)
    mut_source_clear_timer[cutoff:] = np.array([2, 1, 0], dtype=np.uint8)
    mut_source_owner_set_phase[cutoff:] = np.array([1, 1, 0], dtype=np.uint8)
    mut_state_flags[cutoff:, 1] = np.array([0, 8, 0], dtype=np.uint8)
    mut_last_hit_by[cutoff:] = np.array([0, 0, 6], dtype=np.uint8)

    mutated = _derive_source_clear_terminal_phase_seed_lane(
        char_id_u8=char_id,
        action_id_u16=mut_action_id,
        action_frame_i16=mut_action_frame,
        hitlag_u16=mut_hitlag,
        hitstun_u16=mut_hitstun,
        combo_count_u8=mut_combo_count,
        last_attack_landed_u8=mut_last_attack_landed,
        source_clear_timer_x18c8_u8=mut_source_clear_timer,
        source_clear_owner_set_phase_u8=mut_source_owner_set_phase,
        state_flags_u8=mut_state_flags,
        last_hit_by_u8=mut_last_hit_by,
        terminal_followup_cmd0_on_by_char_action={(1, 0x0041): 4, (1, 0x0045): 5, (1, 0x00EC): 30},
        terminal_followup_cmd0_off_by_char_action={(1, 0x0041): 37, (1, 0x0045): 31, (1, 0x00EC): -1},
    )

    assert int(base[2]) == 1, "expected a modeled terminal-phase row in the prefix"
    np.testing.assert_array_equal(base[:cutoff], mutated[:cutoff])


def test_source_clear_terminal_phase_wait_entry_uses_default_clear() -> None:
    # Early Wait-entry terminal rows are owned by Fighter_8006A360's default x18C8 expiry:
    # x18C4_source_ply clears to sentinel 6 instead of parking the source owner.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    got = _derive_terminal_phase(
        action_id=np.array([0x000E, 0x000E], dtype=np.uint16),
        action_frame=np.array([0, 1], dtype=np.int16),
        state_flags=np.array([[0x04, 0, 0, 0x60, 0], [0x04, 0, 0, 0x60, 0]], dtype=np.uint8),
    )

    assert int(got[1]) == 0


def test_source_clear_terminal_phase_pure_reflect_behavior_wait_remains_outside_owner() -> None:
    # Pure fp+0x2218 reflect-behavior carry has separate item/reflect provenance and is not the
    # ordinary Wait-entry terminal clear fixed by Fighter_8006A360.
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218/fp+0x221C lanes)
    got = _derive_terminal_phase(
        action_id=np.array([0x000E, 0x000E], dtype=np.uint16),
        action_frame=np.array([0, 1], dtype=np.int16),
        state_flags=np.array([[0x04, 0, 0, 0, 0], [0x04, 0, 0, 0, 0]], dtype=np.uint8),
    )

    assert int(got[1]) == 1


def test_source_clear_terminal_phase_motivating_wait_rows_match_last_hit_by() -> None:
    # Focused replay proof for the first-divergence rows recorded in
    # reports/triage/combat_owner_worklog.md.
    cases = (
        ("replays/validation/marth/WellWornSmallGoshawk.slpz", (1, 2), 408, 0),
        ("replays/validation/sheik/StiffLustrousZebra.slpz", (1, 2), 340, 0),
    )
    for replay, ports, record, player in cases:
        buffers = build_validation_buffers_from_slp(
            slp_path=replay,
            ports=list(ports),
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
        n = int(buffers.num_records)
        handle = msl_binding.init(n, int(buffers.num_players), True, True)
        out = np.zeros(n, dtype=buffers.ref_t1.dtype)
        try:
            msl_binding.reseed_seed(handle, buffers.seed_u8())
            msl_binding.step_input(handle, buffers.prev_input_u8(), buffers.input_u8())
            msl_binding.write_compare(handle, out.view(np.uint8).reshape(n, -1))
        finally:
            msl_binding.destroy(handle)

        assert int(buffers.seed_t["source_clear_terminal_phase"][record, player]) == 0
        assert int(buffers.seed_t["source_clear_timer_x18c8"][record, player]) == 1
        assert int(out["last_hit_by"][record, player]) == int(
            buffers.ref_t1["last_hit_by"][record, player]
        )
