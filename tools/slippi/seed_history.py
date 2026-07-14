from __future__ import annotations

import functools
import json
import struct
from pathlib import Path

import numpy as np

from tools.extraction.known_data_artifacts import read_mslstg01_v7, stage_metadata_path_for_stage_id


def derive_instance_id_x2073(
    *,
    char_id_u8: np.ndarray,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    data_dir: str = "data",
) -> np.ndarray:
    """
    Derive the fp+0x2073 compare byte used by ft_800895E0 (instance_id bump gate), strictly causally.

    Representation:
    - Returns a u8 array `instance_id_x2073` with length N, representing the *post-frame* value at each
      replay frame index.

    Causality / prefix-invariance:
    - Updates only on motion-state entry events, approximated causally from post-frame action_id
      transitions (and same-action timebase restarts detected by action_frame decreasing).
    - Does not consult future frames or replay outcomes.

    Decomp anchors:
    - refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
    - refs/melee/src/melee/pl/plattack.c::plAttack_80037B08

    x4_flags mapping source:
    - data/attack_id/move_id/<char>.bin (ISO-derived; also used by C via src/attack_id_tables.c)
    """
    if str(data_dir) not in ("data", "./data"):
        raise ValueError(
            "derive_instance_id_x2073 now uses native action-id tables; set MSL_DATA_DIR for "
            "non-default data roots instead of running the removed Python fallback"
        )
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_instance_id_x2073 is required for preprocessing; run `make build`"
        ) from exc
    return msl_binding.derive_instance_id_x2073(
        np.ascontiguousarray(char_id_u8, dtype=np.uint8).reshape(-1),
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(action_frame_i16, dtype=np.int16).reshape(-1),
    )


def derive_instance_id_counter(
    *,
    fighter_instance_id_u16_2d: np.ndarray,
    item_instance_id_u16_2d: np.ndarray,
) -> np.ndarray:
    """
    Derive plAttack_80037B08's "next id" counter strictly causally from replay-visible ids.

    Source signals (Slippi-visible):
    - fighter_instance_id_u16_2d: post-frame fighter `instance_id` lanes.
    - item_instance_id_u16_2d: post-frame item `instance_id` lanes from parsed item snapshots.

    Representation:
    - Returns u16 array length N, where output[i] is the seeded "next instance_id" value to use
      after replay post-frame i (and therefore for one-step seed_t at sample i).

    Seed-bridge rationale:
    - Slippi exposes fighter/item instance ids, but not the internal global counter
      (`unk_804D6480`) read/written by plAttack_80037B08.
    - Use a strictly-causal lower-bound bridge: next nonzero id after the running max id observed
      so far across fighters + items.
    - Prefix-invariant by construction: output[i] depends only on rows <= i via running max.

    Decomp anchors:
    - refs/melee/src/melee/pl/plattack.c::plAttack_80037B08 (monotonic u16 counter; skips 0)
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_instance_id_counter is required for preprocessing; run `make build`"
        ) from exc
    return msl_binding.derive_instance_id_counter(
        np.ascontiguousarray(fighter_instance_id_u16_2d, dtype=np.uint16),
        np.ascontiguousarray(item_instance_id_u16_2d, dtype=np.uint16),
    )


def derive_item_spawn_id_counter(
    *, item_exists_u8_2d: np.ndarray, item_spawn_id_u32_2d: np.ndarray
) -> np.ndarray:
    """
    Derive the global item spawn-id counter (`it_804D6D10`) strictly causally from replay items.

    Slippi exposes item->x1C as `item.spawn_id`, but not the global next counter used by
    Item_80267AA8. Return the next id after the running max observed spawn_id so a rollout seeded
    after an itemless gap does not reset future item ordering identity.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_item_spawn_id_counter is required for preprocessing; run `make build`"
        ) from exc
    return msl_binding.derive_item_spawn_id_counter(
        np.ascontiguousarray(item_exists_u8_2d, dtype=np.uint8),
        np.ascontiguousarray(item_spawn_id_u32_2d, dtype=np.uint32),
    )


def ucf_process_stick_i8(
    raw_x: np.ndarray,
    raw_y: np.ndarray,
    *,
    ucf_enabled: bool,
    ucf_cardinals_1_0_enabled: bool,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Vectorized mirror of src/ucf.c::ucf_process_stick_i8.

    Returns int8 arrays in Melee-legalized range (radius-clamped to 80),
    optionally with UCF 1.0 cardinals snap applied.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.process_stick_i8_units is required for preprocessing; run `make build`"
        ) from exc
    proc_x, proc_y, _, _ = msl_binding.process_stick_i8_units(
        np.ascontiguousarray(raw_x, dtype=np.int8).reshape(-1),
        np.ascontiguousarray(raw_y, dtype=np.int8).reshape(-1),
        int(bool(ucf_enabled)),
        int(bool(ucf_cardinals_1_0_enabled)),
        0.0,
        0.0,
    )
    return proc_x, proc_y


def process_stick_i8_units(
    raw_x: np.ndarray,
    raw_y: np.ndarray,
    *,
    ucf_enabled: bool,
    ucf_cardinals_1_0_enabled: bool,
    deadzone_x: float,
    deadzone_y: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Native bulk stick processing plus per-axis deadzone to unit coordinates."""
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.process_stick_i8_units is required for preprocessing; run `make build`"
        ) from exc
    return msl_binding.process_stick_i8_units(
        np.ascontiguousarray(raw_x, dtype=np.int8).reshape(-1),
        np.ascontiguousarray(raw_y, dtype=np.int8).reshape(-1),
        int(bool(ucf_enabled)),
        int(bool(ucf_cardinals_1_0_enabled)),
        float(deadzone_x),
        float(deadzone_y),
    )


def compute_tilt_timer_axis(
    axis_unit: np.ndarray,
    *,
    tilt_thresh: float,
    start_timer: int = 0xFE,
) -> np.ndarray:
    """
    Compute fp->x670_timer_lstick_tilt_x or fp->x671_timer_lstick_tilt_y per frame.

    Decomp reference: refs/melee/src/melee/ft/fighter.c:1908-2008.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.compute_tilt_timer_axis is required for preprocessing; run `make build`"
        ) from exc
    return msl_binding.compute_tilt_timer_axis(
        np.ascontiguousarray(axis_unit, dtype=np.float32).reshape(-1),
        float(tilt_thresh),
        int(start_timer),
    )


def derive_ucf_pad_buffer_state(
    raw_x: np.ndarray,
    raw_y: np.ndarray,
    *,
    stick_y_hold_time: np.ndarray,
    ucf_enabled: bool,
    ucf_cardinals_1_0_enabled: bool,
    lstick_deadzone_x: float,
    lstick_deadzone_y: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive the UCF 0.84 pad-buffer internal state strictly causally.

    State matches refs/ucf/include/ucf/pad_buffer.h:
    - entries[4].stick (raw PADStatus stick bytes)
    - index (u8 ring index)
    - sdrop_up_frames (u8)

    Ordering matches refs/ucf/src/pad_buffer/pad_buffer.cpp:
    - index = (index + 1) & 3
    - entries[index] = get_input<0>(port).stick  (raw bytes)
    - sdrop_up_frames update uses player->input.stick (post-UCF cardinals + Melee clamp, then deadzone)

    Notes:
    - Source tie-down:
      - UCF's `player->input.stick_y_hold_time` is a u8 at offset 0x671:
        refs/ucf/include/melee/asm/player.h
      - In decomp, the per-frame update for fp->x671_timer_lstick_tilt_y is:
        refs/melee/src/melee/ft/fighter.c:1963-2008
    - Assumption: the dataset's `stick_y_hold_time` input here is the x671-style timer computed
      from the post-processed stick y (UCF cardinals snap + Melee clamp, then deadzone), and
      corresponds closely enough to UCF's hold timer for gating `check_sdrop_up`.
    - Ordering assumption to revisit if shielddrop behavior is off later:
      UCF's pad-buffer injection applies 1.0 cardinals before `check_sdrop_up`
      (refs/ucf/src/pad_buffer/pad_buffer.cpp), but we haven't proven whether Melee updates
      `stick_y_hold_time` using pre- or post-injection stick values.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_ucf_pad_buffer_state is required for preprocessing; run `make build`"
        ) from exc
    return msl_binding.derive_ucf_pad_buffer_state(
        np.ascontiguousarray(raw_x, dtype=np.int8).reshape(-1),
        np.ascontiguousarray(raw_y, dtype=np.int8).reshape(-1),
        np.ascontiguousarray(stick_y_hold_time, dtype=np.uint8).reshape(-1),
        int(bool(ucf_enabled)),
        int(bool(ucf_cardinals_1_0_enabled)),
        float(lstick_deadzone_x),
        float(lstick_deadzone_y),
    )


def compute_tilt_timer_axis_pre_post(
    axis_unit: np.ndarray,
    *,
    tilt_thresh: float,
    override_post_mask: np.ndarray | None = None,
    override_post_value: int = 0xFE,
    reset_post_mask: np.ndarray | None = None,
    start_timer_post: int = 0xFE,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Compute per-frame (timer_pre, timer_post) for x670/x671.

    - timer_pre: value after the per-frame input update (used for input gating in that frame)
    - timer_post: value after action-entry overrides that can occur later in the frame

    Decomp references:
    - fighter input update: refs/melee/src/melee/ft/fighter.c:1908-2008
    - action-entry overrides vary by action (e.g. Dash/Jump)
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.compute_tilt_timer_axis_pre_post is required for preprocessing; run `make build`"
        ) from exc
    return msl_binding.compute_tilt_timer_axis_pre_post(
        np.ascontiguousarray(axis_unit, dtype=np.float32).reshape(-1),
        float(tilt_thresh),
        (
            np.ascontiguousarray(override_post_mask, dtype=np.bool_).reshape(-1)
            if override_post_mask is not None
            else None
        ),
        int(override_post_value),
        (
            np.ascontiguousarray(reset_post_mask, dtype=np.bool_).reshape(-1)
            if reset_post_mask is not None
            else None
        ),
        int(start_timer_post),
    )


def derive_damage_hitlag_sdi_reset_post_mask(
    *,
    action_id: np.ndarray,
    hitlag_u16: np.ndarray,
    state_flags_u8: np.ndarray,
    pos_x: np.ndarray,
    pos_y: np.ndarray,
    stick_x_unit: np.ndarray,
    stick_y_unit: np.ndarray,
    damage_actions: tuple[int, ...],
    sdi_step_mul: float,
) -> np.ndarray:
    """
    Infer post-frame x670/x671 resets caused by ftCo_Damage_OnEveryHitlag SDI.

    The reset is hidden: Slippi exposes the state-flags byte around `allow_sdi`, but not
    `allow_sdi` itself or the callback-local x670/x671 reset. This mask is still prefix-causal for
    seed row i: a reset observed in the transition i-1 -> i only changes the timer state seeded at
    row i and later.

    Source:
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
    - refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_damage_hitlag_sdi_reset_post_mask is required for "
            "preprocessing; run `make build`"
        ) from exc
    return msl_binding.derive_damage_hitlag_sdi_reset_post_mask(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(hitlag_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(state_flags_u8, dtype=np.uint8),
        np.ascontiguousarray(pos_x, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(pos_y, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(stick_x_unit, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(stick_y_unit, dtype=np.float32).reshape(-1),
        tuple(int(v) for v in damage_actions),
        float(sdi_step_mul),
    )


def derive_damage_entry_tilt_timer_reset_post_mask(
    *,
    action_id: np.ndarray,
    action_frame: np.ndarray,
    hitlag_u16: np.ndarray,
    percent: np.ndarray,
    instance_hit_by: np.ndarray,
    damage_actions: tuple[int, ...],
) -> np.ndarray:
    """
    Infer post-frame x670/x671 resets caused by fresh ftCo_8008DCE0 damage entry.

    `ftCo_8008DCE0` installs the damage motion and writes both tilt timers to 0xFE. Replay-visible
    entry can be an action-id change into a Damage state or a same-action re-entry where
    `state_age`/action_frame resets, but natural damage-family transitions such as
    DamageFly* -> DamageFall do not call this owner. Require fresh damage evidence: active hitlag,
    percent movement, or a new `instance_hit_by` source.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_damage_entry_tilt_timer_reset_post_mask is required for "
            "preprocessing; run `make build`"
        ) from exc
    return msl_binding.derive_damage_entry_tilt_timer_reset_post_mask(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(action_frame, dtype=np.int16).reshape(-1),
        np.ascontiguousarray(hitlag_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(percent, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(instance_hit_by, dtype=np.uint32).reshape(-1),
        tuple(int(v) for v in damage_actions),
    )


def _derive_guard_reflect_timer_plus1(
    *,
    action_id_u16: np.ndarray,
    hitlag_u16: np.ndarray,
    act_guard_reflect: int,
    init_frames: int,
) -> np.ndarray:
    """Shared +1-biased GuardReflect timer derivation (strictly causal)."""
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_guard_reflect_timer_plus1 is required for preprocessing; "
            "run `make build`"
        ) from exc
    return msl_binding.derive_guard_reflect_timer_plus1(
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(hitlag_u16, dtype=np.uint16).reshape(-1),
        int(act_guard_reflect),
        int(init_frames),
    )


def derive_guard_reflect_timer_x14(
    *,
    action_id_u16: np.ndarray,
    hitlag_u16: np.ndarray,
    act_guard_reflect: int,
    reflect_frames_x2a4: int,
) -> np.ndarray:
    """
    Derive GuardReflect's reflect-active timer (mv.co.guard.x14) as a strictly-causal u8 countdown.

    Decomp trail:
    - Init: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
      sets `mv.co.guard.x14 = p_ftCommonData->x2A4`.
    - Tick/expire: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
      decrements `mv.co.guard.x14` and clears `fp->reflecting` when it drops below 0.
    - Hitlag gate: per-action anim callbacks (including GuardReflect_Anim) run after prio-0 hitlag
      decrement and gate on the post-decrement lane (`!fp->x2219_b5`):
      refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    """
    return _derive_guard_reflect_timer_plus1(
        action_id_u16=action_id_u16,
        hitlag_u16=hitlag_u16,
        act_guard_reflect=act_guard_reflect,
        init_frames=reflect_frames_x2a4,
    )


def derive_guard_reflect_timer_x18(
    *,
    action_id_u16: np.ndarray,
    hitlag_u16: np.ndarray,
    act_guard_reflect: int,
    reflect_total_frames_x2b4: int,
) -> np.ndarray:
    """
    Derive GuardReflect's powershield-active timer (mv.co.guard.x18) as a strictly-causal u8 countdown.

    Decomp trail:
    - Init: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093A50
      sets `mv.co.guard.x18 = p_ftCommonData->x2B4`.
    - Tick/expire: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
      decrements `mv.co.guard.x18` and clears `fp->x221C_b2` when it drops below 0.
    - Hitlag gate: per-action anim callbacks (including GuardReflect_Anim) run after prio-0 hitlag
      decrement and gate on the post-decrement lane (`!fp->x2219_b5`):
      refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    """
    return _derive_guard_reflect_timer_plus1(
        action_id_u16=action_id_u16,
        hitlag_u16=hitlag_u16,
        act_guard_reflect=act_guard_reflect,
        init_frames=reflect_total_frames_x2b4,
    )


def derive_guard_reflect_origin_guardon(
    *,
    action_id_u16: np.ndarray,
    act_guard_reflect: int,
    act_guard_on: int,
    act_guard: int,
) -> np.ndarray:
    """
    Derive the GuardReflect entry provenance lane.

    Decomp paths:
    - Already-shielding Guard/GuardOn -> GuardReflect uses ftCo_8009388C. It keeps the current
      Guard/GuardOn pose owner and creates only ReflectDesc until ftCo_80093BC0 recreates
      ShieldDesc at x14 expiry.
    - Direct locomotion powershield uses ftCo_80093A50, which enters GuardReflect from locomotion.

    The lane is prefix-causal and persists only for the current contiguous GuardReflect episode.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_guard_reflect_origin_guardon is required for preprocessing; "
            "run `make build`"
        ) from exc
    return msl_binding.derive_guard_reflect_origin_guardon(
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        int(act_guard_reflect),
        int(act_guard_on),
        int(act_guard),
    )


def load_shield_tilt_table_meta(*, data_dir: str = "data") -> dict[int, tuple[int, int]]:
    """
    Load (neutral_frame, frame_max) for extracted MSLSHLD1 guard tilt tables.

    Source of truth: `data/shields/<character>.bin` (ISO-derived via tools/extraction/extract_shield_tilt_table.py).
    Binary format: tools/extraction/extract_shield_tilt_table.py::_write_table.
    """
    # Character ids (GALE01): Fox=1, Falco=22.
    char_files = {
        1: "fox.bin",
        22: "falco.bin",
    }
    out: dict[int, tuple[int, int]] = {}
    base = Path(str(data_dir)) / "shields"
    for char_id, fname in char_files.items():
        p = base / fname
        if not p.exists():
            raise FileNotFoundError(
                f"missing shield tilt table {p} (run tools.extraction.build_data to generate data/ artifacts)"
            )
        buf = p.read_bytes()
        if len(buf) < 8 + 4 + 2 + 2:
            raise ValueError(f"{p}: too small for MSLSHLD1 header (size={len(buf)})")
        if buf[:8] != b"MSLSHLD1":
            raise ValueError(f"{p}: bad magic (want MSLSHLD1)")

        ver = struct.unpack_from("<I", buf, 8)[0]
        if ver != 5:
            raise ValueError(f"{p}: unsupported MSLSHLD1 version={ver} (want 5)")

        frame_count, neutral_frame = struct.unpack_from("<HH", buf, 12)
        if frame_count == 0:
            raise ValueError(f"{p}: frame_count is 0")
        if neutral_frame >= frame_count:
            raise ValueError(
                f"{p}: neutral_frame out of range (neutral_frame={neutral_frame}, frame_count={frame_count})"
            )

        hdr = 32
        want = hdr + int(frame_count) * 3 * 4
        guard_on_frame_count = struct.unpack_from("<H", buf, 28)[0]
        want += int(guard_on_frame_count) * 3 * 4
        if len(buf) != want:
            raise ValueError(f"{p}: size mismatch (got {len(buf)}, want {want})")

        out[int(char_id)] = (int(neutral_frame), int(frame_count - 1))
    return out


def derive_guard_tilt_state(
    stick_x_unit: np.ndarray,
    stick_y_unit: np.ndarray,
    *,
    facing: np.ndarray,
    action_id: np.ndarray,
    action_frame: np.ndarray,
    neutral_frame: np.ndarray,
    frame_max: np.ndarray,
    guard_stick_lerp_x44c: float,
    act_guard_on: int,
    act_guard: int,
    act_guard_reflect: int,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Derive decomp-shaped guard tilt pose state (mv.co.guard.x8 + mv.co.guard.x4) strictly causally.

    Mirrors refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:
    - ftCo_800921DC initializes x8=neutral (10 in GALE01) and x4=0 on GuardOn entry.
    - ftCo_80091BC4 updates:
        x8 = neutral + normalizeAngle0(normalizeAngle180(deg - (x8-neutral)) * x44C + (x8-neutral))
        x4 = x44C * (mag - x4) + x4

    Returns arrays (x8_post_u16, x4_post_f32) with length N, where "post" means after the per-frame update.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError("native msl_binding.derive_guard_tilt_state is required; run `make build`") from exc
    return msl_binding.derive_guard_tilt_state(
        np.ascontiguousarray(stick_x_unit, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(stick_y_unit, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(facing, dtype=np.uint8).reshape(-1),
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(action_frame, dtype=np.int16).reshape(-1),
        np.ascontiguousarray(neutral_frame, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(frame_max, dtype=np.uint16).reshape(-1),
        float(guard_stick_lerp_x44c),
        int(act_guard_on),
        int(act_guard),
        int(act_guard_reflect),
    )


def derive_guard_release_lockout_and_lightshield(
    *,
    action_id: np.ndarray,
    shield_hp: np.ndarray,
    hitlag: np.ndarray,
    buttons_held: np.ndarray,
    button_mask_lr: int,
    button_mask_z: int,
    trigger_unit: np.ndarray,
    trigger_deadzone: float,
    guard_x10_init_frames: int,
    act_guard_on: int,
    act_guard: int,
    act_guard_reflect: int,
    act_guard_set_off: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive Guard release lockout state (mv.co.guard.xC + mv.co.guard.x10) and the lightshield amount
    latch (fp->lightshield_amount) strictly causally from replay history.

    Decomp (GALE01):
    - GuardOn/GuardReflect entry calls ftCo_800921DC, which initializes:
        mv.co.guard.xC = false
        mv.co.guard.x10 = p_ftCommonData->x268
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800921DC
    - Each Guard{On}/Guard/GuardReflect Anim calls ftCo_800925A4 while the shield is active
      (fp->x221B_b0), which:
        - updates fp->lightshield_amount with a "reuse previous value if negative" latch
        - decrements mv.co.guard.x10 once per frame (under !hitlag)
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800925A4
    - GuardOn/Guard/GuardReflect IASA latches mv.co.guard.xC on trigger release and exits to GuardOff
      only once (xC && x10==0) OR the shield is no longer active. The held-input LR lane includes
      Z-mapped shield ownership from Fighter_Spaghetti_8006AD10_Inner1, matching runtime
      msl_trigger_unit_from_input / shield-held gating.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092BCC
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_Guard_IASA (inlineC0)
      refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10_Inner1}
    - GuardSetOff entry consumes the existing fp->lightshield_amount when computing anim rate, and
      does not reset that fighter field on entry.
    - GuardSetOff -> Guard path uses ftCo_800928CC (via ftCo_GuardSetOff_Anim) and does not call
      ftCo_800921DC, so mv.co.guard.xC/x10 should not be reinitialized on this transition.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardSetOff_Anim,ftCo_800928CC}

    Returns (guard_release_latched_xc_u8, guard_x10_u8, lightshield_amount_f32) arrays with length N,
    where the values represent the *post-frame* state for each frame index.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_guard_release_lockout_and_lightshield is required for "
            "preprocessing; run `make build`"
        ) from exc
    return msl_binding.derive_guard_release_lockout_and_lightshield(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(shield_hp, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(hitlag, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(buttons_held, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(trigger_unit, dtype=np.float32).reshape(-1),
        int(button_mask_lr),
        int(button_mask_z),
        float(trigger_deadzone),
        int(guard_x10_init_frames),
        int(act_guard_on),
        int(act_guard),
        int(act_guard_reflect),
        int(act_guard_set_off),
    )


def derive_guard_special_enable_timer_x1c(
    *,
    action_id: np.ndarray,
    hitlag: np.ndarray,
    state_flags_u8: np.ndarray,
    guard_special_enable_frames: int,
    act_guard_on: int,
    act_guard: int,
    act_guard_off: int,
    act_guard_reflect: int,
    act_guard_set_off: int,
) -> np.ndarray:
    """
    Derive GuardOff's hidden special/attack enable timer (mv.co.guard.x1C) from replay history.

    Decomp:
    - Fighter-vs-fighter shield collision calls ftCo_80094138 when defender x221C_b2 is active,
      setting `mv.co.guard.x1C = p_ftCommonData->x2B8` and clearing x10.
    - GuardOn/Guard/GuardReflect inlineC0 decrements x1C only when it does not exit to GuardOff.
    - GuardOff_IASA runs the special/attack chain only while x1C is non-zero.

    Replay surface:
    - Slippi exposes x221C_b2 in state_flags[3] bit 0x20 but not x1C directly.
    - A GuardSetOff post-frame with x221C_b2 live is the visible result of the powershield-active
      shield-hit branch that arms x1C.

    Returns a post-frame-indexed uint8 countdown.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_guard_special_enable_timer_x1c is required for "
            "preprocessing; run `make build`"
        ) from exc
    return msl_binding.derive_guard_special_enable_timer_x1c(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(hitlag, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(state_flags_u8, dtype=np.uint8),
        int(guard_special_enable_frames),
        int(act_guard_on),
        int(act_guard),
        int(act_guard_off),
        int(act_guard_reflect),
        int(act_guard_set_off),
    )


def derive_guard_setoff_hitlag_damage_min(
    *,
    action_id: np.ndarray,
    action_frame_i16: np.ndarray,
    hitlag: np.ndarray,
    hitlag_dmg_mul: float,
    hitlag_base: float,
    act_guard_set_off: int,
) -> np.ndarray:
    """
    Derive a causal lower-bound bridge for GuardSetOff's hidden x19A4 shield-hit int damage.

    Purpose:
    - `ftCo_80092F2C` shapes GuardSetOff anim rate from `fp->x19A4`, but Slippi does not expose
      that field directly.
    - The shield-hit entry does expose the resulting hitlag countdown. Use the decomp
      `ftCommon_CalcHitlag` formula to recover the minimum non-negative integer damage consistent
      with the entry hitlag, then carry that value across the contiguous GuardSetOff segment.

    Causality / prefix-invariance:
    - Updates only on GuardSetOff segment entry or same-action restart (action_frame drop or
      hitlag increase).
    - Depends only on current and previous replay frames.

    Decomp anchors:
    - refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC (writes fp->x19A4 = getEnvDmg(hit0->damage))
    - refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_guard_setoff_hitlag_damage_min is required for "
            "preprocessing; run `make build`"
        ) from exc
    return msl_binding.derive_guard_setoff_hitlag_damage_min(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(action_frame_i16, dtype=np.int16).reshape(-1),
        np.ascontiguousarray(hitlag, dtype=np.uint16).reshape(-1),
        float(hitlag_dmg_mul),
        float(hitlag_base),
        int(act_guard_set_off),
    )


def derive_guard_setoff_hitlag_exit_phase(
    *,
    action_id: np.ndarray,
    hitlag: np.ndarray,
    act_guard_set_off: int,
) -> np.ndarray:
    """
    Derive a causal GuardSetOff hitlag-exit ownership phase discriminator.

    Purpose:
    - GuardSetOff action-frame parity depends on the entry-owned anim rate surviving through the
      frozen hitlag tail and then resuming on the first non-hitlag row.
    - Slippi exposes action_id, hitlag, anim_frame, and frame_speed_mul, but not the hidden
      "which step of the hitlag-exit handoff are we on?" ownership phase.
    - This lane marks that phase explicitly so runtime work can target the last-hitlag and first
      post-hitlag rows without replay-fitting broad GuardSetOff behavior.

    Phase encoding:
    - 0: not GuardSetOff, or GuardSetOff steady row outside the hitlag-exit handoff
    - 1: GuardSetOff hitlag carry row with `hitlag > 1`
    - 2: GuardSetOff last-hitlag row with `hitlag == 1`
    - 3: first non-hitlag GuardSetOff row after a same-segment hitlag row

    Causality / prefix-invariance:
    - Uses only the current and previous replay rows.

    Decomp anchors:
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Anim
    - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_guard_setoff_hitlag_exit_phase is required for "
            "preprocessing; run `make build`"
        ) from exc
    return msl_binding.derive_guard_setoff_hitlag_exit_phase(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(hitlag, dtype=np.uint16).reshape(-1),
        int(act_guard_set_off),
    )


def derive_guard_setoff_post_hitlag_owner(
    *,
    action_id: np.ndarray,
    guard_setoff_hitlag_exit_phase_u8: np.ndarray,
    state_flags_221c_u8: np.ndarray,
    act_guard_set_off: int,
) -> np.ndarray:
    """
    Derive the GuardSetOff post-hitlag ownership discriminator for the handoff rows.

    Meaning:
    - 0: not a GuardSetOff post-hitlag handoff row
    - 1: GuardSetOff handoff row with normal (non-powershield) ownership
    - 2: GuardSetOff handoff row with powershield-active ownership (`x221C_b2` still live)

    Purpose:
    - The remaining F02 blocker rows all occur on the last-hitlag / first-post-hitlag GuardSetOff
      handoff, but they split into two ownership shapes:
    - normal GuardSetOff rows where only ftCo_GuardSetOff_Anim owns the anim-rate handoff, and
    - powershield-active rows where ftCo_80093BC0 still owns the `x221C_b2` substate while the
      same GuardSetOff handoff occurs.

    Causality / prefix-invariance:
    - Uses only the current replay row and the already-causal handoff phase lane.

    Decomp anchors:
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Anim
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
    - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_guard_setoff_post_hitlag_owner is required for "
            "preprocessing; run `make build`"
        ) from exc
    return msl_binding.derive_guard_setoff_post_hitlag_owner(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(guard_setoff_hitlag_exit_phase_u8, dtype=np.uint8).reshape(-1),
        np.ascontiguousarray(state_flags_221c_u8, dtype=np.uint8).reshape(-1),
        int(act_guard_set_off),
    )


def derive_camera_box_visible_x221f_b0(*, state_flags_u8: np.ndarray) -> np.ndarray:
    """
    Extract the replay-visible `fp->x221F_b0` camera-box visibility bit as a named seed lane.

    Purpose:
    - F04 Rebirth/dead-flow rows mismatch on `state_flags[4]` because the camera callback
      (`ftCo_Rebirth_Cam`) and `ftLib_80086A8C` own `fp->x221F_b0` visibility outside the current
      lite sim's explicit state model.
    - Slippi already exposes fp+0x221F in post-frame `state_flags[...,4]`. Promote the b0 mask as
      a semantic seed lane so future runtime fixes can key on the decomp meaning directly.

    Causality / prefix-invariance:
    - Pure current-row extraction from replay-visible post-frame state. No future frames.

    Decomp / replay anchors:
    - refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
    - refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
    - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    """
    sf = np.asarray(state_flags_u8, dtype=np.uint8)
    if sf.ndim != 2 or int(sf.shape[1]) < 5:
        raise ValueError("camera_box_visible_x221f_b0 requires state_flags_u8 shape [n,5]")
    state_flags_221f_index = 4
    state_flag_221f_b0_mask = 0x80
    return ((sf[:, state_flags_221f_index] & np.uint8(state_flag_221f_b0_mask)) != 0).astype(np.uint8)


def derive_magnify_damage_counter_x1910(
    *,
    action_id_u16: np.ndarray,
    state_flags_u8: np.ndarray,
    camera_target_point_inside_stage_cam_bounds_u8: np.ndarray,
    percent_f32: np.ndarray,
    hitlag_u16: np.ndarray | None = None,
    hitstun_u16: np.ndarray | None = None,
    instance_hit_by_u16: np.ndarray | None = None,
    last_hit_by_u8: np.ndarray | None = None,
    interval_frames: int,
    percent_limit: int,
    damage_amount: int,
) -> np.ndarray:
    """
    Derive the hidden magnifying-glass damage counter (`fp->dmg.x1910`) at seed time.

    Purpose:
    - Fighter_procUpdate increments `dmg.x1910` while the player is offscreen in the magnifying
      display and applies `p_ftCommonData->x7B4` damage every `x7AC` frames below the `x7B0`
      percent limit.
    - Slippi exposes the camera/magnify visibility bit (`fp->x221F_b0`) and percent, while the
      seed pipeline derives the decomp-shaped camera-target-inside predicate. `Camera_80031144`
      and `Player_GetMoreFlagsBit3` are hidden, so the terminal counter value is teacher-forced
      only when the next replay row shows the source-shaped magnify percent tick. This avoids
      turning ordinary camera-subject visibility or contact-attributed damage into percent damage.

    Causality / prefix-invariance:
    - Non-causal one-step hidden lane: uses `percent[i + 1]` only to decide whether row `i`
      should carry the terminal counter that causes the vanilla tick. Replay-seeded rollout clears
      this lane; live starts build it causally in runtime.

    Decomp / data anchors:
    - refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate (`fp->dmg.x1910`)
    - refs/melee/src/melee/if/ifmagnify.c::ifMagnify_802FC998
    - data/common/ft_common_data.json::{
      magnify_damage_interval_frames,magnify_damage_percent_limit,magnify_damage_amount}
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_magnify_damage_counter_x1910 is required; run `make build`"
        ) from exc
    return msl_binding.derive_magnify_damage_counter_x1910(
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(state_flags_u8, dtype=np.uint8),
        np.ascontiguousarray(camera_target_point_inside_stage_cam_bounds_u8, dtype=np.uint8).reshape(-1),
        np.ascontiguousarray(percent_f32, dtype=np.float32).reshape(-1),
        None if hitlag_u16 is None else np.ascontiguousarray(hitlag_u16, dtype=np.uint16).reshape(-1),
        None if hitstun_u16 is None else np.ascontiguousarray(hitstun_u16, dtype=np.uint16).reshape(-1),
        (
            None
            if instance_hit_by_u16 is None
            else np.ascontiguousarray(instance_hit_by_u16, dtype=np.uint16).reshape(-1)
        ),
        None if last_hit_by_u8 is None else np.ascontiguousarray(last_hit_by_u8, dtype=np.uint8).reshape(-1),
        int(interval_frames),
        int(percent_limit),
        int(damage_amount),
    )


def derive_rebirth_camera_anchor_y(
    *,
    action_id_u16: np.ndarray,
    stage_id_u32: int,
    respawn_point_y: float,
) -> np.ndarray:
    """
    Derive the hidden Rebirth camera anchor Y (`fp->mv.co.common.x8`) as a seed lane.

    Purpose:
    - F04 late-Rebirth blocker rows mismatch on `fp->x221F_b0` because `ftCo_Rebirth_Cam` owns the
      camera subject outside the lite sim's current explicit state model.
    - The callback writes the camera subject Y from `fp->mv.co.common.x8` plus a camera-data offset;
      on Final Destination this hidden base lane matches the stage respawn platform Y, not the
      fighter's replay-visible `cur_pos.y`.

    Causality / prefix-invariance:
    - Strictly current-row derivation from replay-visible `action_id` plus ISO-derived stage data.
      No future frames.

    Decomp / data anchors:
    - refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
    - data/stages/bin/*.bin::MSLSTG01 respawn_points
    """
    action = np.asarray(action_id_u16, dtype=np.uint16).reshape(-1)
    out = np.zeros(action.shape[0], dtype=np.float32)

    # Unsupported stages leave the foundational lane zero until their ISO-derived respawn points are
    # wired into MSLSTG01.
    # refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MS_Rebirth
    # data/stages/bin/*.bin::MSLSTG01 respawn_points
    if respawn_point_y == 0.0:
        return out

    act_rebirth = 0x000C
    rebirth_mask = action == np.uint16(act_rebirth)
    if rebirth_mask.any():
        out[rebirth_mask] = np.float32(respawn_point_y)
    return out


@functools.lru_cache(maxsize=8)
def _stage_cam_bounds_world(
    *,
    stage_id: int,
    data_dir: str = "data",
) -> tuple[float, float, float, float] | None:
    stage_path = stage_metadata_path_for_stage_id(int(stage_id), Path(str(data_dir)))
    if stage_path is None:
        return None
    stage = read_mslstg01_v7(stage_path)
    left, right, top, bottom = stage.cam_bounds_world
    if not (left < right and bottom < top):
        return None
    return (float(left), float(right), float(bottom), float(top))


def derive_camera_target_world(
    *,
    char_id_u8: np.ndarray,
    animation_index_u32: np.ndarray,
    anim_frame_f32: np.ndarray,
    fighter_scale_y_f32: np.ndarray,
    facing_u8: np.ndarray,
    pos_x_f32: np.ndarray,
    pos_y_f32: np.ndarray,
    pos_z_f32: np.ndarray,
    data_dir: str = "data",
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive the fighter camera-subject target point (`camera_box->x1C`) and radius (`camera_box->x34.z`).

    Purpose:
    - F04 mixed-direction blockers are owned by `ftLib_80086A8C` camera-subject tests
      (`Camera_80030CD8` / `Camera_80030CFC`), not just the replay-visible `fp->x221F_b0` bit.
    - `ftLib_800866DC` writes the subject point from the fighter's camera-zoom target bone plus
      `co_attrs.x170`, and `ftCamera_80076018` scales the camera-box radius from fighter data.
    - Promote those hidden camera-target semantics as explicit seed lanes so later runtime work can
      reason from decomp-backed world-space inputs instead of replay-fit visibility toggles.

    Causality / prefix-invariance:
    - Strictly current-row derivation from replay-visible pose inputs plus ISO-derived character
      camera metadata and SSANIM pose data. No future frames.

    Decomp / data anchors:
    - refs/melee/src/melee/ft/ftlib.c::ftLib_800866DC
    - refs/melee/src/melee/ft/ftcamera.c::ftCamera_80076018
    - refs/melee/src/melee/ft/fighter.c (root facing rotation via ftPartSetRotY)
    - data/characters/<char>.json: camera_zoom_target_bone_part_id,
      camera_zoom_target_offset, camera_box_radius, model_scaling
    - data/anims/<char>.bin (SSANIM01 v5 pose matrices)
    """
    char = np.asarray(char_id_u8, dtype=np.uint8).reshape(-1)
    anim = np.asarray(animation_index_u32, dtype=np.uint32).reshape(-1)
    anim_frame = np.asarray(anim_frame_f32, dtype=np.float32).reshape(-1)
    scale_y = np.asarray(fighter_scale_y_f32, dtype=np.float32).reshape(-1)
    facing = np.asarray(facing_u8, dtype=np.uint8).reshape(-1)
    pos_x = np.asarray(pos_x_f32, dtype=np.float32).reshape(-1)
    pos_y = np.asarray(pos_y_f32, dtype=np.float32).reshape(-1)
    pos_z = np.asarray(pos_z_f32, dtype=np.float32).reshape(-1)
    n = int(char.size)
    if (
        int(anim.size) != n
        or int(anim_frame.size) != n
        or int(scale_y.size) != n
        or int(facing.size) != n
        or int(pos_x.size) != n
        or int(pos_y.size) != n
        or int(pos_z.size) != n
    ):
        raise ValueError("camera target world derivation inputs must share length")

    if str(data_dir) not in ("data", "./data"):
        raise ValueError(
            "derive_camera_target_world now uses the native data-table path; set MSL_DATA_DIR for "
            "non-default data roots instead of running the removed Python AnimPoseDB fallback"
        )
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_camera_target_world is required for preprocessing; run `make build`"
        ) from exc

    return msl_binding.derive_camera_target_world(
        char,
        anim,
        anim_frame,
        scale_y,
        facing,
        pos_x,
        pos_y,
        pos_z,
    )


def derive_camera_target_point_inside_stage_cam_bounds(
    *,
    stage_id_u32: int,
    camera_target_world_x_f32: np.ndarray,
    camera_target_world_y_f32: np.ndarray,
    camera_box_radius_f32: np.ndarray,
    data_dir: str = "data",
) -> np.ndarray:
    """
    Derive the current-row Camera_80030CD8-style point-inside-stage-cam predicate.

    Purpose:
    - F04 mixed-direction rows are owned by the camera-subject point test used by
      `ftLib_80086A8C` / `Camera_80030CD8`, even when the replay-visible `fp->x221F_b0`
      outcomes differ.
    - Promote that point-inside predicate as an explicit seed lane so later runtime work can key on
      the named decomp branch input rather than recomputing it ad hoc from packed fields.

    Causality / prefix-invariance:
    - Strictly current-row derivation from the promoted camera target world point plus ISO-derived
      stage camera bounds. No future frames.
    - Require a valid camera subject (`camera_box_radius_f32 > 0`) so pre-Rebirth / no-target
      control rows stay zero instead of treating the placeholder `(0,0)` point as on-screen.

    Decomp / data anchors:
    - refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
    - refs/melee/src/melee/cm/camera.c::{Camera_80030CD8,Camera_80030BBC}
    - data/stages/bin/*.bin::MSLSTG01 cam_bounds_world
    """
    x = np.asarray(camera_target_world_x_f32, dtype=np.float32).reshape(-1)
    y = np.asarray(camera_target_world_y_f32, dtype=np.float32).reshape(-1)
    r = np.asarray(camera_box_radius_f32, dtype=np.float32).reshape(-1)
    if int(y.size) != int(x.size) or int(r.size) != int(x.size):
        raise ValueError("camera target point-inside derivation inputs must share length")

    out = np.zeros(x.shape[0], dtype=np.uint8)
    bounds = _stage_cam_bounds_world(stage_id=int(stage_id_u32), data_dir=str(data_dir))
    if bounds is None:
        return out

    left, right, bottom, top = bounds
    valid = (r > np.float32(0.0)) & np.isfinite(x) & np.isfinite(y)
    inside = valid & (x >= np.float32(left)) & (x < np.float32(right)) & (y >= np.float32(bottom)) & (
        y < np.float32(top)
    )
    out[inside] = np.uint8(1)
    return out


def compute_tilt_timer_y_pre_post_with_fall_fast(
    stick_y_unit: np.ndarray,
    *,
    tilt_thresh: float,
    jump_entry: np.ndarray,
    pre_input_jump_entry: np.ndarray | None = None,
    fastfall_ok: np.ndarray,
    speed_y_self_post: np.ndarray,
    on_ground_post: np.ndarray,
    fastfall_stick_threshold: float,
    fastfall_tilt_max_frames: int,
    reset_post_mask: np.ndarray | None = None,
    start_timer_post: int = 0xFE,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Compute (tilt_timer_y_pre, tilt_timer_y_post, fall_fast_post) per frame, causally.

    Modeled decomp behaviors:
    - x671 per-frame update from inputs: refs/melee/src/melee/ft/fighter.c:1908-2008
    - Post-input JumpAerial entry override:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c (sets x671=0xFE)
    - Pre-input JumpF/B entry from KneeBend Anim:
      refs/melee/src/melee/ft/chara/ftCommon/{ftCo_KneeBend.c,ftCo_Jump.c}
    - Fastfall latch + x671 override: refs/melee/src/melee/ft/ftcommon.c:505-520 (ftCommon_CheckFallFast)

    Notes:
    - `speed_y_self_post` is Slippi post-frame self velocity.
    - ftCommon_CheckFallFast uses start-of-frame `self_vel.y`, which we model as `speed_y_self_post[i-1]`.
    - `on_ground_post` is Slippi post-frame grounding; we clear fall_fast in the post snapshot when grounded.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.compute_tilt_timer_y_pre_post_with_fall_fast is required for preprocessing; "
            "run `make build`"
        ) from exc
    return msl_binding.compute_tilt_timer_y_pre_post_with_fall_fast(
        np.ascontiguousarray(stick_y_unit, dtype=np.float32).reshape(-1),
        float(tilt_thresh),
        np.ascontiguousarray(jump_entry, dtype=np.bool_).reshape(-1),
        (
            np.ascontiguousarray(pre_input_jump_entry, dtype=np.bool_).reshape(-1)
            if pre_input_jump_entry is not None
            else None
        ),
        np.ascontiguousarray(fastfall_ok, dtype=np.bool_).reshape(-1),
        np.ascontiguousarray(speed_y_self_post, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(on_ground_post, dtype=np.bool_).reshape(-1),
        float(fastfall_stick_threshold),
        int(fastfall_tilt_max_frames),
        (
            np.ascontiguousarray(reset_post_mask, dtype=np.bool_).reshape(-1)
            if reset_post_mask is not None
            else None
        ),
        int(start_timer_post),
    )


def derive_turn_internals(
    *,
    action_id: np.ndarray,
    action_frame_i16: np.ndarray,
    facing: np.ndarray,
    stick_x_unit: np.ndarray,
    tilt_timer_x: np.ndarray,
    dash_flick_abs: float,
    dash_flick_tilt_max_frames: int,
    turn_frames: np.ndarray,
    act_turn: int,
    act_turn_run: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive TURN internals (`frames_to_turn`, `has_turned`, `x8`) per frame from replay history.

    Decomp reference: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c:56-88.

    Causality: this function is strictly causal and does not look ahead to "calibrate" based on
    future outcomes (e.g. post-frame facing flips).

    Deterministic assumption (do NOT tune via one-step mismatch metrics):
    - If TURN entry-frame ordering is ambiguous, we assume `ftCo_Turn_Anim_Inner` does NOT apply
      on the entry frame (same frame the action switches into TURN/TURN_RUN), matching the
      simulator's current update ordering (Turn flip is only ticked if Turn was already active at
      frame start).
    - This assumption is expected to be revisited once we seed more complete entry history
      (notably KneeBend/jump-squat entry history), rather than being "trained" against outcomes.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError("native msl_binding.derive_turn_internals is required; run `make build`") from exc
    return msl_binding.derive_turn_internals(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(action_frame_i16, dtype=np.int16).reshape(-1),
        np.ascontiguousarray(facing, dtype=np.uint8).reshape(-1),
        np.ascontiguousarray(stick_x_unit, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(tilt_timer_x, dtype=np.uint8).reshape(-1),
        float(dash_flick_abs),
        int(dash_flick_tilt_max_frames),
        np.ascontiguousarray(turn_frames, dtype=np.uint8).reshape(-1),
        int(act_turn),
        int(act_turn_run),
    )


def derive_run_x0(
    *,
    action_id: np.ndarray,
    hitlag_u16: np.ndarray,
    run_x0_init_x430: float,
    act_run: int,
    act_run_direct: int,
    act_turn_run: int,
) -> np.ndarray:
    """
    Derive Run IASA lockout (`fp->mv.co.run.x0`) per post-frame from replay history.

    Decomp:
    - `fp->mv.co.run.x0` is initialized by ftCo_Run_Enter_Full (arg0).
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Enter_Full
    - It is decremented by 1.0 each frame in Run_Anim.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
    - It gates TurnRun/RunBrake in Run_IASA:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA

    Suite-relevant entry:
    - TurnRun_Anim enters Run via fn_800CA644, which passes p_ftCommonData->x430 as arg0.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::ftCo_TurnRun_Anim
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::fn_800CA644

    Seed representation:
    - Store a reseed-friendly u8 countdown (clamped to 0..255).
    - Interpret it as "frames remaining while x0 > 0" for the simulator's Run IASA gate.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError("native msl_binding.derive_run_x0 is required; run `make build`") from exc
    return msl_binding.derive_run_x0(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(hitlag_u16, dtype=np.uint16).reshape(-1),
        float(run_x0_init_x430),
        int(act_run),
        int(act_run_direct),
        int(act_turn_run),
    )


def derive_runbrake_cmd0(
    *,
    action_id_u16: np.ndarray,
    anim_frame_f32: np.ndarray,
    char_id_u8: np.ndarray,
    cmd0_on_by_char: dict[int, int],
    cmd0_off_by_char: dict[int, int],
    act_run_brake: int,
) -> np.ndarray:
    """
    Derive RunBrake's `fp->cmd_vars[0]` gate per post-frame, strictly causally.

    Decomp:
    - ftCo_RunBrake_Enter clears `fp->cmd_vars[0] = 0`.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_Enter
    - ftCo_RunBrake_IASA only reaches `fn_800C9CEC` (TurnRun path) when `fp->cmd_vars[0] != 0`.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
    - The command script writes `cmd_vars[0]` via `set_cmd_var`.
      refs/melee/src/melee/ft/ftaction.c::ftAction_80071820

    Source of truth:
    - data/moves/<char>.json moves["ftCo_SM_RunBrake"]["events"] set_cmd_var(idx=0)

    Representation:
    - 0: RunBrake TurnRun gate disabled on this post-frame.
    - 1: RunBrake TurnRun gate enabled on this post-frame.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError("native msl_binding.derive_runbrake_cmd0 is required; run `make build`") from exc
    return msl_binding.derive_runbrake_cmd0(
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(anim_frame_f32, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(char_id_u8, dtype=np.uint8).reshape(-1),
        {int(k): int(v) for k, v in cmd0_on_by_char.items()},
        {int(k): int(v) for k, v in cmd0_off_by_char.items()},
        int(act_run_brake),
    )


def derive_dash_x4(
    *,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    act_dash: int = 0x0014,
    act_turn: int = 0x0012,
) -> np.ndarray:
    """
    Derive `fp->mv.co.dash.x4` per post-frame, strictly causally.

    Decomp anchors:
    - ftCo_Dash_Enter stores arg1 into `fp->mv.co.dash.x4`.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter
    - Turn->Dash path enters via `ftCo_Dash_Enter(gobj, 0)`.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
    - Dash_CheckInput enters Dash via `ftCo_Dash_Enter(gobj, 1)`.
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput

    Representation:
    - Returns a u8 array with length N, carrying the post-frame latch value.
    - For non-Dash frames, emits 0.

    Causal entry detection:
    - Dash entry is detected from post-frame action transitions and same-action frame resets
      (`action_frame` drop while staying in Dash).
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError("native msl_binding.derive_dash_x4 is required; run `make build`") from exc
    return msl_binding.derive_dash_x4(
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(action_frame_i16, dtype=np.int16).reshape(-1),
        int(act_dash),
        int(act_turn),
    )


def derive_shine_release_state(
    *,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    buttons_held_u16: np.ndarray,
    hitlag_u16: np.ndarray,
    release_lag_init_u8: np.ndarray,
    button_mask_b: int = 0x0200,
    act_special_lw_start: int = 0x0168,
    act_special_lw_loop: int = 0x0169,
    act_special_lw_hit: int = 0x016A,
    act_special_lw_end: int = 0x016B,
    act_special_lw_turn: int = 0x016C,
    act_special_air_lw_start: int = 0x016D,
    act_special_air_lw_loop: int = 0x016E,
    act_special_air_lw_hit: int = 0x016F,
    act_special_air_lw_end: int = 0x0170,
    act_special_air_lw_turn: int = 0x0171,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Derive Fox/Falco shine release internals strictly causally.

    Returns:
    - shine_release_lag_u8: post-frame mirror of `fp->mv.fx.SpecialLw.releaseLag`
    - shine_is_release_u8: post-frame mirror of `fp->mv.fx.SpecialLw.isRelease` (0/1)

    Decomp anchors:
    - ftFox_SpecialLw_SetVars initializes {releaseLag,isRelease} on SpecialLw enter.
      refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFox_SpecialLw_SetVars
    - Start anim callbacks latch isRelease from held B (no releaseLag decrement).
    - Loop/Turn/Hit anim callbacks latch isRelease and decrement releaseLag.
      refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
          ftFx_SpecialLwStart_Anim,ftFx_SpecialLwLoop_Anim,ftFx_SpecialLwTurn_Anim,ftFx_SpecialLwHit_Anim}
    - Loop/Turn/Hit end-vs-loop gating uses (releaseLag <= 0 && isRelease).
      refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwHit_Check
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_shine_release_state is required; run `make build`"
        ) from exc
    return msl_binding.derive_shine_release_state(
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(action_frame_i16, dtype=np.int16).reshape(-1),
        np.ascontiguousarray(buttons_held_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(hitlag_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(release_lag_init_u8, dtype=np.uint8).reshape(-1),
        int(button_mask_b),
        int(act_special_lw_start),
        int(act_special_lw_loop),
        int(act_special_lw_hit),
        int(act_special_lw_end),
        int(act_special_lw_turn),
        int(act_special_air_lw_start),
        int(act_special_air_lw_loop),
        int(act_special_air_lw_hit),
        int(act_special_air_lw_end),
        int(act_special_air_lw_turn),
    )


def derive_ecb_lock_state(
    *,
    on_ground_u8: np.ndarray,
    action_id_u16: np.ndarray,
    char_id_u8: np.ndarray | None = None,
    action_frame_i16: np.ndarray | None = None,
    lock_frames_ground_to_air: int = 10,
    act_jump_f: int = 0x0019,
    act_jump_b: int = 0x001A,
    act_jump_aerial_f: int = 0x001B,
    act_jump_aerial_b: int = 0x001C,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Derive `fp->ecb_lock` and its CollData owner per post-frame from replay history.

    Decomp anchors:
    - ftCommon_8007D5D4 sets `fp->ecb_lock = 10` and enables CollData_X130_Locked on ground->air.
      refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    - Fighter_procMap decrements `fp->ecb_lock` once per map/collision callback and clears the lock
      when it reaches 0.
      refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
    - Grounding transitions clear the lock via ftCommon_UnlockECB (called by ftCommon_8007D6A4).
      refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D6A4
    - Falcon's ftCommon_8007D60C owners refresh a five-frame lock from aerial Raptor entry/end and
      every SpecialHiThrow0 Anim callback; grounded Raptor floor loss calls the same helper from the
      collision callback after the ordinary map-phase decrement.
      refs/melee/src/melee/ft/chara/ftCaptain/ftCa_Special{S,Hi}.c

    Representation:
    - Return u8 post-frame countdown and owner values.
    - Owner 1 is a generic replay-seeded lock; owner 4 proves a live Falcon ftCommon callback.
      Ownership persists until refresh, landing/unlock, or countdown expiry.
    - On a detected post-frame grounded->air transition, write `(lock_frames_ground_to_air - 1)`
      for that frame to account for the same-frame procMap decrement.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError("native msl_binding.derive_ecb_lock_state is required; run `make build`") from exc
    ground = np.ascontiguousarray(on_ground_u8, dtype=np.uint8).reshape(-1)
    action = np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1)
    char = (
        np.zeros(ground.shape, dtype=np.uint8)
        if char_id_u8 is None
        else np.ascontiguousarray(char_id_u8, dtype=np.uint8).reshape(-1)
    )
    action_frame = (
        np.zeros(ground.shape, dtype=np.int16)
        if action_frame_i16 is None
        else np.ascontiguousarray(action_frame_i16, dtype=np.int16).reshape(-1)
    )
    return msl_binding.derive_ecb_lock_state(
        ground,
        action,
        char,
        action_frame,
        int(lock_frames_ground_to_air),
        int(act_jump_f),
        int(act_jump_b),
        int(act_jump_aerial_f),
        int(act_jump_aerial_b),
    )


def derive_ecb_lock_timer(
    *,
    on_ground_u8: np.ndarray,
    action_id_u16: np.ndarray,
    char_id_u8: np.ndarray | None = None,
    action_frame_i16: np.ndarray | None = None,
    lock_frames_ground_to_air: int = 10,
    act_jump_f: int = 0x0019,
    act_jump_b: int = 0x001A,
    act_jump_aerial_f: int = 0x001B,
    act_jump_aerial_b: int = 0x001C,
) -> np.ndarray:
    timer, _owner = derive_ecb_lock_state(
        on_ground_u8=on_ground_u8,
        action_id_u16=action_id_u16,
        char_id_u8=char_id_u8,
        action_frame_i16=action_frame_i16,
        lock_frames_ground_to_air=lock_frames_ground_to_air,
        act_jump_f=act_jump_f,
        act_jump_b=act_jump_b,
        act_jump_aerial_f=act_jump_aerial_f,
        act_jump_aerial_b=act_jump_aerial_b,
    )
    return timer


def derive_ecb_lock_bottom_rel_y(
    *,
    char_id_u8: np.ndarray,
    action_id_u16: np.ndarray,
    animation_index_u32: np.ndarray,
    anim_frame_f32: np.ndarray,
    on_ground_u8: np.ndarray,
    ecb_lock_timer_u8: np.ndarray,
    ecb_lock_owner_u8: np.ndarray,
    act_jump_aerial_f: int = 0x001B,
    act_jump_aerial_b: int = 0x001C,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Derive CollData.desired_ecb.bottom.y and its owner while the lock is live.

    The native producer uses extracted ECB tables and replay-prefix grounding/lock history. It
    preserves the prior desired bottom through air-jump and Falcon-special lock rows, matching the
    source mpColl_LoadECB_inline locked-bottom owner without per-frame Python loops in the seed path.

    Decomp anchors:
    - refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    - refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
    - data/ecb/*
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_ecb_lock_bottom_rel_y is required; run `make build`"
        ) from exc
    return msl_binding.derive_ecb_lock_bottom_rel_y(
        np.ascontiguousarray(char_id_u8, dtype=np.uint8).reshape(-1),
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(animation_index_u32, dtype=np.uint32).reshape(-1),
        np.ascontiguousarray(anim_frame_f32, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(on_ground_u8, dtype=np.uint8).reshape(-1),
        np.ascontiguousarray(ecb_lock_timer_u8, dtype=np.uint8).reshape(-1),
        np.ascontiguousarray(ecb_lock_owner_u8, dtype=np.uint8).reshape(-1),
        int(act_jump_aerial_f),
        int(act_jump_aerial_b),
    )


def derive_damage_hitlag_colldata_ecb(
    *,
    char_id_u8: np.ndarray,
    action_id_u16: np.ndarray,
    animation_index_u32: np.ndarray,
    anim_frame_f32: np.ndarray,
    frame_speed_mul_f32: np.ndarray,
    facing_u8: np.ndarray,
    on_ground_u8: np.ndarray,
    hitlag_u16: np.ndarray,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive the frozen CollData ECB carried by first active Damage hitlag rows.

    Decomp shape:
    - `Fighter_8006A360` skips Anim/Phys while hitlag is active.
    - `ftCo_Damage_Coll` still calls `ft_80081DD4 -> mpColl_800477E0`.
    - `mpColl_LoadECB_inline` consumes the currently loaded CollData ECB, which may still be the
      pre-Damage action pose on the first visible Damage hitlag row.

    The native producer samples that previous pose from extracted ECB tables once per Damage
    hitlag episode and carries it while hitlag remains active. It is seed reconstruction for
    CollData state, not a replay output clamp.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_damage_hitlag_colldata_ecb is required; run `make build`"
        ) from exc
    return msl_binding.derive_damage_hitlag_colldata_ecb(
        np.ascontiguousarray(char_id_u8, dtype=np.uint8).reshape(-1),
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(animation_index_u32, dtype=np.uint32).reshape(-1),
        np.ascontiguousarray(anim_frame_f32, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(frame_speed_mul_f32, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(facing_u8, dtype=np.uint8).reshape(-1),
        np.ascontiguousarray(on_ground_u8, dtype=np.uint8).reshape(-1),
        np.ascontiguousarray(hitlag_u16, dtype=np.uint16).reshape(-1),
    )


def derive_damage_jump_buffer_x14(
    *,
    action_id: np.ndarray,
    hitstun_u16: np.ndarray,
    buttons_pressed: np.ndarray,
    stick_y_unit: np.ndarray,
    tilt_timer_y: np.ndarray,
    hitlag_u16: np.ndarray | None = None,
    tap_jump_threshold: float,
    tap_jump_tilt_max_frames: int,
    button_mask_xy: int,
    damage_actions: tuple[int, ...],
) -> np.ndarray:
    """
    Derive `fp->mv.co.damage.x14` (damage jump-buffer snapshot) per post-frame.

    Seed bridge scope:
    - Slippi does not expose `mv.co.damage.x14` directly, so this derives the internal lane from
      replay-visible signals for reseed parity.

    Decomp anchors:
    - Cleared on Damage entry in `ftCo_8008DCE0` (`mv.co.damage.x14 = 0`):
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    - Set from `mv.co.damage.x0` when jump input is detected in `doIasa`:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::doIasa
    - Consumed by `inlineC0` gate before DamageFall enter in `ftCo_Damage_Anim` /
      `ftCo_DamageFly_Anim`, compared against `p_ftCommonData->x1D0`:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Anim
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineC0

    Causality / prefix-invariance:
    - Uses only <=t replay frames (current/past values), never future samples.
    - Resets on causal entry into the configured damage-action family.
    - Resets on in-family fresh-hit boundaries (`hitstun` increase), matching that
      ftCo_8008DCE0 clears x14 on damage re-entry.
    - Slippi exposes replay-visible stick timers, not the callback-local hidden `x671` value that
      `ftCo_Jump_GetInput` observes inside hitstun. Treat tap-jump as authoritative only inside the
      same replay-visible x671 window (`x671 < p_ftCommonData->x74`) used by the normal jump gate,
      and only outside hitlag because Fighter_8006A360 / Fighter_procUpdate skip Anim/IASA
      callbacks while fp->x2219_b5 is set. Stale held tilts outside that window are not producers.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_damage_jump_buffer_x14 is required; run `make build`"
        ) from exc
    return msl_binding.derive_damage_jump_buffer_x14(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(hitstun_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(buttons_pressed, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(stick_y_unit, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(tilt_timer_y, dtype=np.uint8).reshape(-1),
        None if hitlag_u16 is None else np.ascontiguousarray(hitlag_u16, dtype=np.uint16).reshape(-1),
        float(tap_jump_threshold),
        int(tap_jump_tilt_max_frames),
        int(button_mask_xy),
        tuple(int(v) for v in damage_actions),
    )


def derive_damage_meteor_cancel_x1a(
    *,
    action_id: np.ndarray,
    hitstun_u16: np.ndarray,
    source_angle_u16: np.ndarray,
    angle_min_deg: int,
    angle_max_deg: int,
    damage_actions: tuple[int, ...],
) -> np.ndarray:
    """
    Derive `fp->mv.co.damage.x1A` (meteor-cancel eligibility) per post-frame.

    Causality / prefix-invariance:
    - Uses only <=t replay rows.
    - Samples the source hit angle only at causal damage-entry / fresh-hit boundaries, then carries
      the hidden x1A bit through the continuous damage episode. Later DI-adjusted KB vectors are
      deliberately ignored; Sakurai-angle rows can become visibly vertical without source x1A.

    Decomp anchors:
    - `ftColl_8007AC68` sets x1A from the source hit angle window.
    - `doIasa` consumes x1A with the x1B lockout before immediate JumpAerial/SpecialAirHi escape.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_damage_meteor_cancel_x1a is required; run `make build`"
        ) from exc
    return msl_binding.derive_damage_meteor_cancel_x1a(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(hitstun_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(source_angle_u16, dtype=np.uint16).reshape(-1),
        int(angle_min_deg),
        int(angle_max_deg),
        tuple(int(v) for v in damage_actions),
    )


def derive_damage_post_hitlag_cb_kind(
    *,
    action_id: np.ndarray,
    hitstun_u16: np.ndarray,
    damage_actions: tuple[int, ...],
) -> np.ndarray:
    """
    Derive `fp->post_hitlag_cb` ownership lane for a configured action-family subset as a causal u8 enum.

    Enum encoding (u8):
    - 0: no callback
    - 1: `ftCo_Damage_OnExitHitlag`

    Decomp anchors:
    - Damage entry sets `fp->post_hitlag_cb = ftCo_Damage_OnExitHitlag`:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    - Hitlag-exit path invokes `post_hitlag_cb`:
      refs/melee/src/melee/ft/fighter.c::Fighter_8006D10C
    - Callback body:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag

    Seed bridge scope:
    - Slippi does not expose callback pointers directly, so this lane is seeded from replay-visible
      action family/history for teacher-forced one-step parity.

    Causality / prefix-invariance:
    - Uses only <=t samples.
    - Uses a damage-family ownership latch and fresh-hit boundaries (`hitstun` increase) as causal
      re-entry markers.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_damage_post_hitlag_cb_kind is required; run `make build`"
        ) from exc
    return msl_binding.derive_damage_post_hitlag_cb_kind(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(hitstun_u16, dtype=np.uint16).reshape(-1),
        tuple(int(v) for v in damage_actions),
    )


def derive_kneebend_internals(
    *,
    action_id: np.ndarray,
    buttons: np.ndarray,
    buttons_pressed: np.ndarray,
    stick_y_unit: np.ndarray,
    cstick_y_unit: np.ndarray,
    tilt_timer_y: np.ndarray,
    tap_jump_threshold: float,
    dash_run_jump_stick_y_threshold: float,
    tap_jump_tilt_max_frames: int,
    tap_jump_release_threshold: float,
    act_kneebend: int,
    act_dash: int,
    act_run: int,
    act_run_direct: int,
    act_run_brake: int,
    act_turn_run: int,
    button_mask_xy: int,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Derive KneeBend internals (`jump_input`, `is_short_hop`) per frame from replay history.

    Decomp references:
    - Jump input selection (L-stick vs XY): refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c:34-63
    - Dash/Run-family jump IASA path uses `fn_800CAF78`, whose L-stick path compares against
      `p_ftCommonData->x80` instead of the ordinary tap-jump `x74` threshold:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c:107-129
    - C-stick "jump input" path:
      - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c:90-104 (ftCo_800CB024)
      - refs/melee/src/melee/ft/ft_0DF1.c:239-249 (ftCo_800DF910)
      - refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c:16-28 (ftCo_KneeBend_Enter)
    - KneeBend storage + short-hop latch: refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c:16-28
      and :44-56

    Causality: strictly causal; uses only current and past frames to detect KneeBend entry and to
    latch the short-hop flag while remaining in KneeBend.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_kneebend_internals is required; run `make build`"
        ) from exc
    return msl_binding.derive_kneebend_internals(
        np.ascontiguousarray(action_id, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(buttons, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(buttons_pressed, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(stick_y_unit, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(cstick_y_unit, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(tilt_timer_y, dtype=np.uint8).reshape(-1),
        float(tap_jump_threshold),
        float(dash_run_jump_stick_y_threshold),
        int(tap_jump_tilt_max_frames),
        float(tap_jump_release_threshold),
        int(act_kneebend),
        int(act_dash),
        int(act_run),
        int(act_run_direct),
        int(act_run_brake),
        int(act_turn_run),
        int(button_mask_xy),
    )


def compute_press_timer_u8(
    *,
    buttons_pressed: np.ndarray,
    press_mask: int,
    start_timer: int = 0xFF,
) -> np.ndarray:
    """
    Compute a generic "frames since last press" timer (u8, saturating at 0xFF).

    This matches the pattern used by several `fp->x67*` counters in fighter input history.

    Decomp references:
    - init/reset to 0xFF: refs/melee/src/melee/ft/fighter.c:650-707
    - per-frame update example (x67F): refs/melee/src/melee/ft/fighter.c:2078-2086
      (`if (fp->input.x668 & HSD_PAD_LR) fp->x67F = 0; else if (fp->x67F < 0xFF) fp->x67F++;`)
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError("native msl_binding.compute_press_timer_u8 is required; run `make build`") from exc
    return msl_binding.compute_press_timer_u8(
        np.ascontiguousarray(buttons_pressed, dtype=np.uint16).reshape(-1),
        int(press_mask),
        int(start_timer),
    )


def compute_lr_press_timer_x67f(
    *,
    buttons: np.ndarray,
    trigger_unit: np.ndarray,
    hitlag_frames: np.ndarray | None = None,
    trigger_deadzone: float,
    button_mask_lr: int,
    button_mask_z: int,
    start_timer: int = 0xFF,
) -> np.ndarray:
    """
    Compute fp->x67F (frames since LR-lane press edge) causally.

    Decomp tie-down:
    - x67F update checks edge mask lane `fp->input.x668 & HSD_PAD_LR`.
      refs/melee/src/melee/ft/fighter.c:2078-2086
    - x668 edge lane derives from held-input synthesis that includes:
      - digital L/R,
      - trigger lane (`x650 > deadzone`), and
      - Z-mapped LR lane.
      refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10_Inner1}
      refs/melee/src/melee/ft/fighter.c:1868-1890
    - When hitlag gate x2219_b5 is set, x668 edges are OR-latched (`x668 |= edge`) rather than
      overwritten, so an LR edge persists for the rest of that hitlag window.
      refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10_Inner1}
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError("native msl_binding.compute_lr_press_timer_x67f is required; run `make build`") from exc
    return msl_binding.compute_lr_press_timer_x67f(
        np.ascontiguousarray(buttons, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(trigger_unit, dtype=np.float32).reshape(-1),
        (
            None
            if hitlag_frames is None
            else np.ascontiguousarray(hitlag_frames, dtype=np.uint16).reshape(-1)
        ),
        float(trigger_deadzone),
        int(button_mask_lr),
        int(button_mask_z),
        int(start_timer),
    )


def _colanim_timer_remaining_from_action_frame(init_frames: int, action_frame: int) -> int:
    if int(init_frames) <= 0:
        return 0
    if int(action_frame) <= 0:
        return int(init_frames)
    rem = int(init_frames) + 1 - int(action_frame)
    if rem < 0:
        rem = 0
    if rem > 0xFFFF:
        rem = 0xFFFF
    return int(rem)


def derive_colanim_internals(
    *,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    hitlag_u16: np.ndarray,
    hitstun_u16: np.ndarray,
    hurtbox_state_u8: np.ndarray,
    colanim_throw_x1994_frames: int,
    colanim_cliff_x1990_frames: int,
    colanim_damage_x1994_frames: int,
    colanim_passivewall_x1990_frames: int = 0,
    colanim_rebirth_fall_x1994_frames: int = 0,
    throw_actions: tuple[int, ...],
    cliff_actions: tuple[int, ...],
    passivewall_actions: tuple[int, ...] = (),
    damage_actions: tuple[int, ...],
    fall_actions: tuple[int, ...] = (),
    rebirth_actions: tuple[int, ...] = (),
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Derive fp->x198C/x1990/x1994/x2221_b0 seed internals strictly causally.

    Why this exists:
    - Slippi exposes merged `hurtbox_state` (x1988 when nonzero else x198C), but not x198C timers.
    - One-step reseed needs timer ownership internals to avoid single-frame inference drift.

    Decomp anchors:
    - refs/melee/src/melee/ft/fighter.c::Fighter_8006A360 (x1990/x1994 decrements + x198C updates)
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398 (ftColl_8007B7A4, x1994)
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A77C (ftColl_8007B760, x1990)
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_800C1E64
      (ftColl_8007B760 with p_ftCommonData->x764, x1990)
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnExitHitlag (x1994)
    - refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_RebirthWait_{Anim,IASA}
      (ftColl_8007B7A4 with p_ftCommonData->x5D8 before Fall)

    Notes:
    - `x2221_b0` currently has no reliable Slippi-exposed signal in this replay path; we keep it 0.
    - This is a seed bridge over missing internals, not a claim that every x198C source is modeled.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_colanim_internals is required; run `make build`"
        ) from exc
    return msl_binding.derive_colanim_internals(
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(action_frame_i16, dtype=np.int16).reshape(-1),
        np.ascontiguousarray(hitlag_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(hitstun_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(hurtbox_state_u8, dtype=np.uint8).reshape(-1),
        int(colanim_throw_x1994_frames),
        int(colanim_cliff_x1990_frames),
        int(colanim_damage_x1994_frames),
        int(colanim_passivewall_x1990_frames),
        int(colanim_rebirth_fall_x1994_frames),
        tuple(int(v) for v in throw_actions),
        tuple(int(v) for v in cliff_actions),
        tuple(int(v) for v in passivewall_actions),
        tuple(int(v) for v in damage_actions),
        tuple(int(v) for v in fall_actions),
        tuple(int(v) for v in rebirth_actions),
    )


def compute_x672_trigger_timer_pre_post(
    *,
    trigger_unit: np.ndarray,
    prev_trigger_unit: np.ndarray | None = None,
    trigger_min: float,
    guard_reflect_entry: np.ndarray | None = None,
    start_timer_post: int = 0xFE,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Compute fp->x672_input_timer_counter per frame (pre/post), causally.

    - timer_pre: value after Fighter input update (used for powershield/GuardReflect gate that frame)
    - timer_post: value after action-entry overrides later in the frame

    Decomp references:
    - per-frame update from analog triggers (x650/x654):
      refs/melee/src/melee/ft/fighter.c:2020-2050
    - action-entry override on GuardReflect entry:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:746-804
      (sets `fp->x672_input_timer_counter = 0xFE;`)

    Notes:
    - `trigger_unit` corresponds to `fp->input.x650` (max of analog L/R, with digital L/R treated as 1.0).
    - If `prev_trigger_unit` is omitted, we model `fp->input.x654` as the previous frame's `trigger_unit`.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.compute_x672_trigger_timer_pre_post is required; run `make build`"
        ) from exc
    return msl_binding.compute_x672_trigger_timer_pre_post(
        np.ascontiguousarray(trigger_unit, dtype=np.float32).reshape(-1),
        (
            None
            if prev_trigger_unit is None
            else np.ascontiguousarray(prev_trigger_unit, dtype=np.float32).reshape(-1)
        ),
        float(trigger_min),
        (
            None
            if guard_reflect_entry is None
            else np.ascontiguousarray(guard_reflect_entry, dtype=np.bool_).reshape(-1)
        ),
        int(start_timer_post),
    )


def compute_fighter_stick_input_counters(
    *,
    stick_x_unit: np.ndarray,
    stick_y_unit: np.ndarray,
    tilt_thresh_x: float,
    tilt_thresh_y: float,
    start_timer: int = 0xFE,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Compute stick-driven fighter input counters through the required native path.

    Source: refs/melee/src/melee/ft/fighter.c:1897-2019, including lb_8000D148 zeroing.
    The old Python loop was removed so preprocessing cannot silently fall back to the slow path.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.compute_fighter_stick_input_counters is required for preprocessing; "
            "run `make build`"
        ) from exc
    return msl_binding.compute_fighter_stick_input_counters(
        np.ascontiguousarray(stick_x_unit, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(stick_y_unit, dtype=np.float32).reshape(-1),
        float(tilt_thresh_x),
        float(tilt_thresh_y),
        int(start_timer),
    )


def compute_fighter_trigger_input_counters(
    *,
    trigger_unit: np.ndarray,
    trigger_min: float,
    start_timer: int = 0xFE,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Compute trigger-driven fighter input counters through the required native path.

    Source: refs/melee/src/melee/ft/fighter.c:2020-2050.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.compute_fighter_trigger_input_counters is required for preprocessing; "
            "run `make build`"
        ) from exc
    return msl_binding.compute_fighter_trigger_input_counters(
        np.ascontiguousarray(trigger_unit, dtype=np.float32).reshape(-1),
        float(trigger_min),
        int(start_timer),
    )


def compute_fighter_button_timers(
    *,
    buttons_pressed: np.ndarray,
    hitlag_frames: np.ndarray | None = None,
    mask_a: int,
    mask_b: int,
    mask_xy: int,
    mask_dpad_up: int,
    mask_dpad_down: int,
    mask_lr: int,
    mask_z: int = 0,
    start_timer: int = 0xFF,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Compute fighter button timers through the required native path.

    Source: refs/melee/src/melee/ft/fighter.c:2052-2094.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.compute_fighter_button_timers is required for preprocessing; run `make build`"
        ) from exc
    return msl_binding.compute_fighter_button_timers(
        np.ascontiguousarray(buttons_pressed, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(hitlag_frames, dtype=np.uint16).reshape(-1) if hitlag_frames is not None else None,
        int(mask_a),
        int(mask_b),
        int(mask_xy),
        int(mask_dpad_up),
        int(mask_dpad_down),
        int(mask_lr),
        int(mask_z),
        int(start_timer),
    )

def derive_downwait_timer(
    *,
    action_id_u16: np.ndarray,
    hitstun_u16: np.ndarray | None = None,
    down_wait_frames: int,
    act_down_damage_u: int | None = None,
    act_down_damage_d: int | None = None,
    act_down_wait_u: int,
    act_down_wait_d: int,
) -> np.ndarray:
    """
    Derive fp->mv.co.downwait.x0 (DownWait timer), strictly causally from replay history.

    Decomp:
    - init on DownBound->DownWait:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097E8C
        fp->mv.co.downwait.x0 = p_ftCommonData->x424;
    - DownDamage->DownWait:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
        fp->mv.co.damage.x0 = max(1, (int)(kb_applied * p_ftCommonData->x154));
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Anim
        fp->mv.co.downdamage.x0 -= 1;
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097F38
        changes to DownWaitU/D without resetting fp->mv.co.downwait.x0.
    - decrement each DownWait frame unless fp->x2224_b2:
      refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownWait_Anim

    Slippi post-frame does not expose fp->mv.* union fields, so teacher-forced reseeding requires
    deriving this internal from replay state history. For v1 suite, fp->x2224_b2 is not exposed by
    Slippi either, so this derivation assumes the common case (x2224_b2 == 0) and counts down once
    per contiguous DownWait frame after entry. When DownWait is entered through DownDamage, the
    previous replay row's visible hitstun is the prefix-causal exposed countdown paired with
    `mv.co.damage.x0`; the entered DownWait seed receives that timer after the entering Anim tick.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError("native msl_binding.derive_downwait_timer is required; run `make build`") from exc
    return msl_binding.derive_downwait_timer(
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        None if hitstun_u16 is None else np.ascontiguousarray(hitstun_u16, dtype=np.uint16).reshape(-1),
        int(down_wait_frames),
        -1 if act_down_damage_u is None else int(act_down_damage_u),
        -1 if act_down_damage_d is None else int(act_down_damage_d),
        int(act_down_wait_u),
        int(act_down_wait_d),
    )


# Grab/throw victim attachment owner identity (seeded; suite-focused).
#
# Decomp source of truth for action ids:
# - refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCommon_MotionState`
# - refs/melee/src/melee/ft/ftmotionstates.c (comments with numeric ids)
_ACT_CAPTURE_PULLED_HI = np.uint16(0x00DF)  # ftCo_MS_CapturePulledHi (223)
_ACT_CAPTURE_WAIT_HI = np.uint16(0x00E0)  # ftCo_MS_CaptureWaitHi (224)
_ACT_CAPTURE_DAMAGE_HI = np.uint16(0x00E1)  # ftCo_MS_CaptureDamageHi (225)
_ACT_CAPTURE_PULLED_LW = np.uint16(0x00E2)  # ftCo_MS_CapturePulledLw (226)
_ACT_CAPTURE_WAIT_LW = np.uint16(0x00E3)  # ftCo_MS_CaptureWaitLw (227)
_ACT_CAPTURE_DAMAGE_LW = np.uint16(0x00E4)  # ftCo_MS_CaptureDamageLw (228)
_ACT_CAPTURE_CUT = np.uint16(0x00E5)  # ftCo_MS_CaptureCut (229)
_ACT_CAPTURE_JUMP = np.uint16(0x00E6)  # ftCo_MS_CaptureJump (230)
_ACT_CAPTURE_NECK = np.uint16(0x00E7)  # ftCo_MS_CaptureNeck (231)
_ACT_CAPTURE_FOOT = np.uint16(0x00E8)  # ftCo_MS_CaptureFoot (232)
_ACT_CAPTURE_CAPTAIN = np.uint16(0x0113)  # ftCo_MS_CaptureCaptain (275, Falcon Dive victim)
_ACT_THROWN_F = np.uint16(0x00EF)  # ftCo_MS_ThrownF (239)
_ACT_THROWN_B = np.uint16(0x00F0)  # ftCo_MS_ThrownB (240)
_ACT_THROWN_HI = np.uint16(0x00F1)  # ftCo_MS_ThrownHi (241)
_ACT_THROWN_LW = np.uint16(0x00F2)  # ftCo_MS_ThrownLw (242)
_ACT_THROWN_LW_WOMEN = np.uint16(0x00F3)  # ftCo_MS_ThrownlwWomen (243)
_BTN_A = np.uint16(0x0100)
_BTN_B = np.uint16(0x0200)
_BTN_X = np.uint16(0x0400)
_BTN_Y = np.uint16(0x0800)
_BTN_L = np.uint16(0x0040)
_BTN_R = np.uint16(0x0020)
_BTN_XY = np.uint16(0x0C00)
_CAPTURE_ATTACH_ACTIONS = (
    _ACT_CAPTURE_PULLED_HI,
    _ACT_CAPTURE_WAIT_HI,
    _ACT_CAPTURE_DAMAGE_HI,
    _ACT_CAPTURE_PULLED_LW,
    _ACT_CAPTURE_WAIT_LW,
    _ACT_CAPTURE_DAMAGE_LW,
)
_CAPTURE_WAIT_ACTIONS = (_ACT_CAPTURE_WAIT_HI, _ACT_CAPTURE_WAIT_LW)
_CAPTURE_DAMAGE_ACTIONS = (_ACT_CAPTURE_DAMAGE_HI, _ACT_CAPTURE_DAMAGE_LW)
_CAPTURE_WAIT_OR_DAMAGE_ACTIONS = _CAPTURE_WAIT_ACTIONS + _CAPTURE_DAMAGE_ACTIONS


def derive_grab_owner_port_2p(*, action_id_u16_2p: np.ndarray) -> np.ndarray:
    """Derive per-frame grab owner identity for 2-player replays (slot domain).

    Returns an array of shape [n_frames, 2] (dtype u8) where:
    - out[i, p] = other slot index (0/1) if player p is a captured/thrown victim at frame i
    - out[i, p] = 0xFF otherwise

    Causality / prefix-invariance:
    - Depends only on the current frame's action_id values (no lookahead).
    """
    a = np.asarray(action_id_u16_2p, dtype=np.uint16)
    if a.ndim != 2 or a.shape[1] != 2:
        raise ValueError(f"action_id_u16_2p must have shape [n,2], got {a.shape}")

    # Attached captured/thrown victim action ids (common).
    #
    # CaptureCut / CaptureJump are not part of the attached owner link any more:
    # ftCo_CaptureWaitHi_Anim breaks out through ftCo_800DA698 / fn_800DC070, both of which route
    # through ftCo_800DC920 and clear owner/victim pointers before entering CatchCut/CaptureCut/
    # CaptureJump.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    #   ftCo_CaptureWaitHi_Anim,ftCo_800DA698,fn_800DC070
    # }
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_800DC920
    is_victim = (
        (a == _ACT_CAPTURE_PULLED_HI)
        | (a == _ACT_CAPTURE_WAIT_HI)
        | (a == _ACT_CAPTURE_DAMAGE_HI)
        | (a == _ACT_CAPTURE_PULLED_LW)
        | (a == _ACT_CAPTURE_WAIT_LW)
        | (a == _ACT_CAPTURE_DAMAGE_LW)
        | (a == _ACT_CAPTURE_NECK)
        | (a == _ACT_CAPTURE_FOOT)
        | (a == _ACT_CAPTURE_CAPTAIN)
        | (a == _ACT_THROWN_F)
        | (a == _ACT_THROWN_B)
        | (a == _ACT_THROWN_HI)
        | (a == _ACT_THROWN_LW)
        | (a == _ACT_THROWN_LW_WOMEN)
    )

    out = np.full((a.shape[0], 2), 0xFF, dtype=np.uint8)
    out[:, 0] = np.where(is_victim[:, 0], np.uint8(1), np.uint8(0xFF))
    out[:, 1] = np.where(is_victim[:, 1], np.uint8(0), np.uint8(0xFF))

    # Defensive guard: in real gameplay, "both players are grab/capture/thrown victims in the same
    # frame" should not occur. If it does appear in corrupted/weird data, do not create a cyclic
    # attachment (0<->1). Treat both as unattached.
    both_victims = is_victim[:, 0] & is_victim[:, 1]
    out[both_victims, :] = np.uint8(0xFF)
    return out


def derive_grab_owner_port(
    *, action_id_u16: np.ndarray, num_players: int | None = None
) -> np.ndarray:
    """Derive per-frame grab owner identity for 2-4 player replays.

    This is a conservative teacher-forced reconstruction of the hidden `fp->victim_gobj`
    attachment owner used by catch/capture/throw callbacks. For 2p it preserves the historical
    other-player derivation. For 4p doubles it only assigns an owner when exactly one attached
    victim action and exactly one owner action are visible in the same post-frame row; ambiguous
    simultaneous grab pairs remain unseeded instead of inventing a pairing from replay shape.

    Decomp owner:
    - refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftGrabDist}
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
    - refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_800DC920
    """
    a = np.asarray(action_id_u16, dtype=np.uint16)
    if a.ndim != 2:
        raise ValueError(f"action_id_u16 must have shape [n,players], got {a.shape}")
    width = int(a.shape[1])
    players = width if num_players is None else int(num_players)
    if players < 0 or players > width:
        raise ValueError(f"num_players must be in [0,{width}], got {players}")
    if players == 2:
        return derive_grab_owner_port_2p(action_id_u16_2p=a[:, :2])

    out = np.full((a.shape[0], width), 0xFF, dtype=np.uint8)
    if players <= 0:
        return out

    active = a[:, :players]
    is_victim = (
        (active == _ACT_CAPTURE_PULLED_HI)
        | (active == _ACT_CAPTURE_WAIT_HI)
        | (active == _ACT_CAPTURE_DAMAGE_HI)
        | (active == _ACT_CAPTURE_PULLED_LW)
        | (active == _ACT_CAPTURE_WAIT_LW)
        | (active == _ACT_CAPTURE_DAMAGE_LW)
        | (active == _ACT_CAPTURE_NECK)
        | (active == _ACT_CAPTURE_FOOT)
        | (active == _ACT_THROWN_F)
        | (active == _ACT_THROWN_B)
        | (active == _ACT_THROWN_HI)
        | (active == _ACT_THROWN_LW)
        | (active == _ACT_THROWN_LW_WOMEN)
    )
    is_owner = (
        (active == np.uint16(0x00D5))  # ftCo_MS_CatchPull
        | (active == np.uint16(0x00D7))  # ftCo_MS_CatchDashPull
        | (active == np.uint16(0x00D8))  # ftCo_MS_CatchWait
        | (active == np.uint16(0x00D9))  # ftCo_MS_CatchAttack
        | (active == np.uint16(0x00DB))  # ftCo_MS_ThrowF
        | (active == np.uint16(0x00DC))  # ftCo_MS_ThrowB
        | (active == np.uint16(0x00DD))  # ftCo_MS_ThrowHi
        | (active == np.uint16(0x00DE))  # ftCo_MS_ThrowLw
    )
    unique_pair = (np.count_nonzero(is_victim, axis=1) == 1) & (
        np.count_nonzero(is_owner, axis=1) == 1
    )
    if np.any(unique_pair):
        rows = np.nonzero(unique_pair)[0]
        victims = np.argmax(is_victim[rows, :], axis=1).astype(np.intp, copy=False)
        owners = np.argmax(is_owner[rows, :], axis=1).astype(np.uint8, copy=False)
        out[rows, victims] = owners
    return out


def derive_seed_prev_action_post(
    *, post_action_id_u16: np.ndarray, post_action_frame_i16: np.ndarray
) -> tuple[np.ndarray, np.ndarray]:
    """Derive replay-true previous-action seed lanes for one-step rows.

    Output row i corresponds to the seed snapshot built from post-frame row i:
    - row 0 has no prior replay row inside the dataset window, so it reuses the current seed action
    - rows i>0 use post-frame row i-1 as the previous-action source
    """
    action_id = np.asarray(post_action_id_u16, dtype=np.uint16).reshape(-1)
    action_frame = np.asarray(post_action_frame_i16, dtype=np.int16).reshape(-1)
    if action_id.shape != action_frame.shape:
        raise ValueError(
            f"post_action_id_u16 and post_action_frame_i16 must match, got "
            f"{action_id.shape} vs {action_frame.shape}"
        )
    if action_id.size == 0:
        return np.zeros((0,), dtype=np.uint16), np.zeros((0,), dtype=np.int16)

    out_len = max(0, int(action_id.size) - 1)
    out_action_id = np.empty((out_len,), dtype=np.uint16)
    out_action_frame = np.empty((out_len,), dtype=np.int16)
    if out_len == 0:
      return out_action_id, out_action_frame

    out_action_id[0] = action_id[0]
    out_action_frame[0] = action_frame[0]
    if out_len > 1:
      out_action_id[1:] = action_id[:-2]
      out_action_frame[1:] = action_frame[:-2]
    return out_action_id, out_action_frame


def derive_grab_mash_stick_sign_post(
    *,
    stick_x_unit: np.ndarray,
    stick_y_unit: np.ndarray,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    grab_owner_port_u8: np.ndarray,
    grab_mash_stick_threshold: float,
) -> tuple[np.ndarray, np.ndarray]:
    """Derive post-frame ftCommon_GrabMash stick-sign latches (`x1A50` / `x1A51`).

    The latches are GrabMash-scheduled: cleared at capture attach, updated only on frames
    whose CaptureWait/CaptureDamage callback runs GrabMash, reading fp-visible stick values
    that lag the serialized pre-frame rows by one.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_grab_mash_stick_sign_post is required for preprocessing; "
            "run `make build`"
        ) from exc
    return msl_binding.derive_grab_mash_stick_sign_post(
        np.ascontiguousarray(stick_x_unit, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(stick_y_unit, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(action_frame_i16, dtype=np.int16).reshape(-1),
        np.ascontiguousarray(grab_owner_port_u8, dtype=np.uint8).reshape(-1),
        float(grab_mash_stick_threshold),
    )


def derive_capture_mash_buttons_pressed(
    *,
    buttons_u16_2d: np.ndarray,
    l_trigger_u8_2d: np.ndarray,
    r_trigger_u8_2d: np.ndarray,
    trigger_deadzone: float,
    button_mask_a: int,
    button_mask_z: int,
    button_mask_lr: int,
) -> np.ndarray:
    """Derive CaptureWait mash button edges, including Z-as-A and analog/digital LR edges."""
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_capture_mash_buttons_pressed is required; run `make build`"
        ) from exc
    return msl_binding.derive_capture_mash_buttons_pressed(
        np.ascontiguousarray(buttons_u16_2d, dtype=np.uint16),
        np.ascontiguousarray(l_trigger_u8_2d, dtype=np.uint8),
        np.ascontiguousarray(r_trigger_u8_2d, dtype=np.uint8),
        float(trigger_deadzone),
        int(button_mask_a),
        int(button_mask_z),
        int(button_mask_lr),
    )


def derive_capture_grab_hidden_post(
    *,
    action_id_u16: np.ndarray,
    action_frame_i16: np.ndarray,
    grab_owner_port_u8: np.ndarray,
    percent_f32: np.ndarray,
    buttons_pressed_u16: np.ndarray,
    stick_x_unit: np.ndarray,
    stick_y_unit: np.ndarray,
    frame_speed_mul_f32: np.ndarray,
    grab_mash_stick_x_sign_post: np.ndarray,
    grab_mash_stick_y_sign_post: np.ndarray,
    slot_index: int,
    handicap: int,
    capture_grab_timer_base: float,
    capture_grab_timer_handicap_mul: float,
    capture_grab_timer_handicap_base: float,
    capture_grab_timer_slot_mul: float,
    capture_grab_timer_slot_base: float,
    capture_grab_timer_percent_mul: float,
    capture_wait_grab_timer_decrement: float,
    capture_wait_grab_mash_damage: float,
    capture_wait_anim_rate_hold_frames: float,
    capture_wait_jump_latch_window_frames: float,
    grab_mash_stick_threshold: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Derive explicit CaptureWait/CaptureDamage hidden owner lanes.

    Output row `i` corresponds to the post-frame replay row `i` and is suitable for seeding the
    next one-step transition from replay row `i` to replay row `i+1`.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_capture_grab_hidden_post is required; run `make build`"
        ) from exc
    return msl_binding.derive_capture_grab_hidden_post(
        np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(action_frame_i16, dtype=np.int16).reshape(-1),
        np.ascontiguousarray(grab_owner_port_u8, dtype=np.uint8).reshape(-1),
        np.ascontiguousarray(percent_f32, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(buttons_pressed_u16, dtype=np.uint16).reshape(-1),
        np.ascontiguousarray(stick_x_unit, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(stick_y_unit, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(frame_speed_mul_f32, dtype=np.float32).reshape(-1),
        np.ascontiguousarray(grab_mash_stick_x_sign_post, dtype=np.int8).reshape(-1),
        np.ascontiguousarray(grab_mash_stick_y_sign_post, dtype=np.int8).reshape(-1),
        int(slot_index),
        int(handicap),
        float(capture_grab_timer_base),
        float(capture_grab_timer_handicap_mul),
        float(capture_grab_timer_handicap_base),
        float(capture_grab_timer_slot_mul),
        float(capture_grab_timer_slot_base),
        float(capture_grab_timer_percent_mul),
        float(capture_wait_grab_timer_decrement),
        float(capture_wait_grab_mash_damage),
        float(capture_wait_anim_rate_hold_frames),
        float(capture_wait_jump_latch_window_frames),
        float(grab_mash_stick_threshold),
    )
