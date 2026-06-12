from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np


@dataclass(frozen=True)
class EndFrameTables:
    # Map: char_id (Slippi post `character`) -> {msid(u16): end_frame(f32)}
    by_char_id: dict[int, dict[int, float]]


def _char_key_from_char_id(char_id: int) -> str | None:
    # Registry-driven internal-id -> pipeline-name mapping (GALE01 FighterKind ids).
    # refs/melee/src/melee/ft/types.h and tools/extraction/char_registry.py
    from tools.extraction.char_registry import CHAR_BY_INTERNAL_ID

    info = CHAR_BY_INTERNAL_ID.get(int(char_id))
    return info.name if info is not None else None


def _read_end_frames_from_tracks_bin(path: Path) -> dict[int, float]:
    """
    Parse `data/anims/<char>.tracks.bin` (SSANIMT1 v3) and return msid->end_frame.

    Format is mirrored in `src/anim_table.c::load_tracks_for_char`.
    """
    buf = path.read_bytes()
    if len(buf) < 8 + 4 + 4:
        raise ValueError(f"{path}: SSANIMT1 file too small")
    if buf[:8] != b"SSANIMT1":
        raise ValueError(f"{path}: bad magic {buf[:8]!r}")
    (ver,) = struct.unpack_from("<I", buf, 8)
    if int(ver) != 3:
        raise ValueError(f"{path}: unsupported version {ver} (want 3)")

    local_count, anim_count = struct.unpack_from("<HH", buf, 12)
    off = 16

    # Skip local_parts[u8], local_parent[i16], local_flags[u32]
    skip_hdr = int(local_count) + int(local_count) * 2 + int(local_count) * 4
    if off + skip_hdr > len(buf):
        raise ValueError(f"{path}: truncated locals header")
    off += skip_hdr

    out: dict[int, float] = {}
    for _ in range(int(anim_count)):
        extra = 2
        if off + 2 + 4 + extra > len(buf):
            raise ValueError(f"{path}: truncated anim table")
        (msid,) = struct.unpack_from("<H", buf, off)
        (end_frame,) = struct.unpack_from("<f", buf, off + 2)
        off += 6
        off += 1  # aobj_loop (u8)
        off += 1  # uses_root_motion (u8)

        out[int(msid)] = float(np.float32(end_frame))

        # Skip the per-local track payloads (we only need end_frame).
        for _li in range(int(local_count)):
            if off + 2 > len(buf):
                raise ValueError(f"{path}: truncated local track header")
            n_tracks = int(buf[off + 1])
            off += 2
            for _ti in range(n_tracks):
                if off + 8 > len(buf):
                    raise ValueError(f"{path}: truncated track header")
                (payload_len,) = struct.unpack_from("<H", buf, off + 6)
                off += 8
                if off + int(payload_len) > len(buf):
                    raise ValueError(f"{path}: truncated track payload")
                off += int(payload_len)

    if off != len(buf):
        raise ValueError(f"{path}: unexpected trailing bytes: {len(buf) - off}")
    return out


def load_end_frame_tables(data_root: Path) -> EndFrameTables:
    # All registry characters: the old hardcoded (1, 22) loop left non-spacie characters
    # without anim end-frame tables (silent None in every consumer).
    from tools.extraction.char_registry import CHARS as _REGISTRY_CHARS

    by_char_id: dict[int, dict[int, float]] = {}
    for info in _REGISTRY_CHARS.values():
        path = data_root / "anims" / f"{info.name}.tracks.bin"
        if not path.exists():
            continue
        by_char_id[int(info.internal_id)] = _read_end_frames_from_tracks_bin(path)
    return EndFrameTables(by_char_id=by_char_id)


def derive_frame_speed_mul_f32(
    *,
    state_age_f32: np.ndarray,  # [n] f32
    action_id: np.ndarray,  # [n] u16
    hitlag: np.ndarray,  # [n] u16 (frames left)
    char_id: np.ndarray,  # [n] u8
    animation_index: np.ndarray,  # [n] u32 (msid or 0xFFFFFFFF)
    lr_press_timer: np.ndarray,  # [n] u8 (x67F: frames since L/R press)
    shield_hp: np.ndarray | None = None,  # [n] f32 (post-frame shield health)
    lightshield_amount: np.ndarray | None = None,  # [n] f32 (fp->lightshield_amount)
    common_shield_hit_damage_mul: float | None = None,  # p_ftCommonData->x284
    common_shield_hit_damage_base: float | None = None,  # p_ftCommonData->x288
    common_shield_hit_lightshield_min: float | None = None,  # p_ftCommonData->x2DC
    common_shield_hit_lightshield_max: float | None = None,  # p_ftCommonData->x2E0
    common_shield_stun_mul: float | None = None,  # p_ftCommonData->x28C
    common_shield_stun_base: float | None = None,  # p_ftCommonData->x290
    common_shield_stun_lightshield_min: float | None = None,  # p_ftCommonData->x2E4
    common_shield_stun_lightshield_max: float | None = None,  # p_ftCommonData->x2E8
    end_frames: EndFrameTables,
    common_lcancel_window_frames: int,
    common_lcancel_lag_div: float,
    common_landing_fall_special_lag_frames: float,
    char_landing_air_lag_frames: dict[int, dict[str, int]],  # char_id -> {"airn":..,"airf":..,...}
    char_fallspecial_origin_lag: dict[int, dict[int, float]] | None = None,
) -> np.ndarray:
    """
    Strictly-causal derivation of fp->frame_speed_mul (float).

    Goal: provide a reseedable approximation to the engine's animation rate for the *next* frame's
    anim advance, without looking at future frames.

    - On stable segments (same action+msid), use delta(state_age) as a proxy for the rate.
    - On motion-state entry frames where state_age is reset (delta may be negative), use decomp-backed
      LandingAir / LandingFallSpecial / GuardSetOff formulas to seed the new rate.

    This is intentionally conservative and suite-scoped: LandingAir*/LandingFallSpecial are the early
    primary drivers for fractional anim advance in the Fox/Falco FD suite.
    """
    n = int(state_age_f32.shape[0])
    out = np.empty(n, dtype=np.float32)
    last = np.float32(1.0)
    if n == 0:
        return out
    out[0] = last

    hp = None if shield_hp is None else np.asarray(shield_hp, dtype=np.float32).reshape(-1)
    light = None if lightshield_amount is None else np.asarray(lightshield_amount, dtype=np.float32).reshape(-1)
    use_guard_setoff_entry_rate = (
        hp is not None
        and light is not None
        and common_shield_hit_damage_mul is not None
        and common_shield_hit_damage_base is not None
        and common_shield_hit_lightshield_min is not None
        and common_shield_hit_lightshield_max is not None
        and common_shield_stun_mul is not None
        and common_shield_stun_base is not None
        and common_shield_stun_lightshield_min is not None
        and common_shield_stun_lightshield_max is not None
    )
    if use_guard_setoff_entry_rate:
        if int(hp.size) != n or int(light.size) != n:
            raise ValueError("shield_hp/lightshield_amount must match state_age length when provided")
        shield_hit_mul = np.float32(float(common_shield_hit_damage_mul))
        shield_hit_base = np.float32(float(common_shield_hit_damage_base))
        shield_hit_ls_min = np.float32(float(common_shield_hit_lightshield_min))
        shield_hit_ls_max = np.float32(float(common_shield_hit_lightshield_max))
        shield_stun_mul = np.float32(float(common_shield_stun_mul))
        shield_stun_base = np.float32(float(common_shield_stun_base))
        shield_stun_ls_min = np.float32(float(common_shield_stun_lightshield_min))
        shield_stun_ls_max = np.float32(float(common_shield_stun_lightshield_max))
    else:
        shield_hit_mul = np.float32(0.0)
        shield_hit_base = np.float32(0.0)
        shield_hit_ls_min = np.float32(0.0)
        shield_hit_ls_max = np.float32(0.0)
        shield_stun_mul = np.float32(0.0)
        shield_stun_base = np.float32(0.0)
        shield_stun_ls_min = np.float32(0.0)
        shield_stun_ls_max = np.float32(0.0)

    # Action ids (GALE01) for suite-relevant landing states.
    # These are stable identifiers, not heuristics.
    ACT_LANDING_AIR_N = np.uint16(0x0046)
    ACT_LANDING_AIR_F = np.uint16(0x0047)
    ACT_LANDING_AIR_B = np.uint16(0x0048)
    ACT_LANDING_AIR_HI = np.uint16(0x0049)
    ACT_LANDING_AIR_LW = np.uint16(0x004A)
    ACT_LANDING_FALL_SPECIAL = np.uint16(0x002B)
    ACT_FALL_SPECIAL = np.uint16(0x0023)
    ACT_FALL_SPECIAL_F = np.uint16(0x0024)
    ACT_FALL_SPECIAL_B = np.uint16(0x0025)
    ACT_ESCAPE_AIR = np.uint16(0x00EC)
    ACT_GUARD_SET_OFF = np.uint16(0x00B5)
    fall_special_actions = {int(ACT_FALL_SPECIAL), int(ACT_FALL_SPECIAL_F), int(ACT_FALL_SPECIAL_B)}

    def landing_fall_special_lag_for_entry(i: int, cid: int) -> float:
        if i <= 0:
            return float(common_landing_fall_special_lag_frames)
        prev_action = int(action_id[i - 1])
        origin_action = prev_action
        if prev_action in fall_special_actions:
            j = i - 1
            while j > 0 and int(action_id[j - 1]) in fall_special_actions:
                j -= 1
            origin_action = int(action_id[j - 1]) if j > 0 else prev_action

        if origin_action == int(ACT_ESCAPE_AIR):
            # EscapeAir_Anim and EscapeAir_Coll both use p_ftCommonData->x344 as the FallSpecial /
            # LandingFallSpecial landing lag.
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
            #   ftCo_EscapeAir_Anim,ftCo_80099D70}
            return float(common_landing_fall_special_lag_frames)

        # Per-char resolved origin map (built from extracted MotionState identity in
        # make_dataset_from_slp: owners fx_special_kind lane for the spacie illusion/
        # firefox rows, special-msids x submotion for marth-style up-special freefall).
        # No raw action-id literals: the same id means different rows per character.
        origin_map = (
            {} if char_fallspecial_origin_lag is None else char_fallspecial_origin_lag.get(cid, {})
        )
        lag = origin_map.get(int(origin_action))
        if lag is not None:
            return float(lag)

        return float(common_landing_fall_special_lag_frames)

    for i in range(1, n):
        changed = (
            int(action_id[i]) != int(action_id[i - 1])
            or int(animation_index[i]) != int(animation_index[i - 1])
            or int(char_id[i]) != int(char_id[i - 1])
        )
        if not changed:
            if int(hitlag[i]) != 0:
                # Decomp: hitlag freezes animation advancement (Fighter_8006A360 gate) but does not
                # modify fp->frame_speed_mul; keep the last known rate on stable segments.
                # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
                out[i] = last
                continue

            delta = np.float32(state_age_f32[i] - state_age_f32[i - 1])
            if np.isfinite(delta) and float(delta) >= 0.0:
                last = delta
            out[i] = last
            continue

        # Action change (or msid/char change): derive the new state's frame_speed_mul independent
        # of hitlag. Key distinction:
        # - Hitlag freezes the *tick* of fp->cur_anim_frame (and thus freezes Slippi `state_age`),
        #   so delta(state_age) cannot be used to infer the rate while hitlag is active.
        # - However, motion-state changes (Fighter_ChangeMotionState) still set fp->frame_speed_mul
        #   to the motion state's `anim_speed` (typically 1.0f) even if hitlag is active; hitlag
        #   only gates the subsequent ftAnim_8006EBA4 tick in Fighter_8006A360.
        # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        cid = int(char_id[i])
        msid_u32 = int(animation_index[i])
        msid = int(msid_u32 & 0xFFFF) if msid_u32 != 0xFFFFFFFF else None
        end_frame = None
        if msid is not None:
            end_frame = end_frames.by_char_id.get(cid, {}).get(int(msid))

        a = np.uint16(action_id[i])

        if a == ACT_LANDING_FALL_SPECIAL and end_frame is not None:
            # Decomp: ftCo_LandingFallSpecial_Enter uses (0.1 + fp->x2EC) / landing_lag.
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
            lag = landing_fall_special_lag_for_entry(i, cid)
            if lag > 0.0:
                last = np.float32((np.float32(end_frame) + np.float32(0.1)) / np.float32(lag))
                out[i] = last
                continue

        if a in (ACT_LANDING_AIR_N, ACT_LANDING_AIR_F, ACT_LANDING_AIR_B, ACT_LANDING_AIR_HI, ACT_LANDING_AIR_LW):
            # Decomp:
            # - LandingAir sets anim rate to (ftAnim_8006F484 + 0.1) / lag.
            #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithMsidLag
            # - Lag is divided when x67F < p_ftCommonData->xE4.
            #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
            lag_map = char_landing_air_lag_frames.get(cid)
            if lag_map is not None and end_frame is not None:
                key = (
                    "airn"
                    if a == ACT_LANDING_AIR_N
                    else "airf"
                    if a == ACT_LANDING_AIR_F
                    else "airb"
                    if a == ACT_LANDING_AIR_B
                    else "airhi"
                    if a == ACT_LANDING_AIR_HI
                    else "airlw"
                )
                lag = float(lag_map.get(key, 0))
                if lag > 0.0 and int(lr_press_timer[i]) < int(common_lcancel_window_frames):
                    div_lag = lag / float(common_lcancel_lag_div)
                    int_lag = int(div_lag)
                    if int_lag == 0:
                        int_lag = 1
                    lag = float(int_lag)
                if lag > 0.0:
                    last = np.float32((np.float32(end_frame) + np.float32(0.1)) / np.float32(lag))
                    out[i] = last
                    continue

        if (
            a == ACT_GUARD_SET_OFF
            and int(action_id[i - 1]) != int(ACT_GUARD_SET_OFF)
            and end_frame is not None
            and use_guard_setoff_entry_rate
        ):
            # Decomp:
            # - GuardSetOff entry sets anim rate to:
            #     (0.1f + lbGetJObjEndFrame(...)) /
            #     (x28C * (x19A4 * (1 - (lightshield_amount*(x2E8-x2E4)+x2E4))) + x290)
            #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
            # - x19A4 is written from shield-hit int damage on resolve:
            #   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
            # - Shield HP drop is driven by x19A0_shieldDamageTaken and lightshield_amount:
            #   refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
            #
            # APPROXIMATION(seed bridge): Slippi does not expose x19A4/x19A0 directly, so this
            # backsolves an inferred x19A4 from the causal post-frame shield HP drop on
            # GuardSetOff entry, then applies the decomp GuardSetOff anim-rate formula.
            #
            # TODO(seed source): replace this inference when we have a direct/reseedable source for
            # shield-hit internals (prefer Slippi-ASM export of x19A4/x19A0, targeted Dolphin dump,
            # or promoted explicit seed field derived from a decomp-backed pipeline).
            shield_drop = np.float32(hp[i - 1] - hp[i])
            if float(shield_drop) > 0.0 and float(shield_hit_mul) > 0.0:
                ls = np.float32(light[i])
                if float(ls) < 0.0:
                    ls = np.float32(0.0)
                if float(ls) > 1.0:
                    ls = np.float32(1.0)

                shield_hit_light_term = np.float32(
                    ls * (shield_hit_ls_max - shield_hit_ls_min) + shield_hit_ls_min
                )
                shield_hit_den = np.float32(shield_hit_mul * (np.float32(1.0) - shield_hit_light_term))
                if float(shield_hit_den) > 0.0:
                    shield_damage_taken = np.float32((shield_drop - shield_hit_base) / shield_hit_den)
                    if float(shield_damage_taken) < 0.0:
                        shield_damage_taken = np.float32(0.0)
                    # Decomp rounding semantics for x19A4 shield-hit integer damage:
                    # ftColl_80076CBC assigns `int int_dmg = getEnvDmg(hit0->damage);` then writes
                    # `fp1->x19A4 = int_dmg`. `getEnvDmg` uses C float->int cast semantics
                    # (`(int)dmg`), i.e. truncation toward zero (not round-to-nearest).
                    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
                    # refs/melee/src/melee/ft/ftcoll.c::getEnvDmg
                    int_dmg_est = np.float32(np.trunc(shield_damage_taken))
                    if float(int_dmg_est) < 0.0:
                        int_dmg_est = np.float32(0.0)

                    shield_stun_light_term = np.float32(
                        ls * (shield_stun_ls_max - shield_stun_ls_min) + shield_stun_ls_min
                    )
                    setoff_f = np.float32(
                        shield_stun_mul * (int_dmg_est * (np.float32(1.0) - shield_stun_light_term)) + shield_stun_base
                    )
                    if float(setoff_f) > 0.0:
                        last = np.float32((np.float32(end_frame) + np.float32(0.1)) / setoff_f)
                        out[i] = last
                        continue

        # Fallback: default rate.
        #
        # IMPORTANT(hitlag+action_change):
        # Slippi post-frame `state_age` (fp->cur_anim_frame) is frozen under hitlag, so we cannot
        # infer the new state's rate from delta(state_age) on the entry frame if hitlag is active.
        #
        # Decomp: Fighter_ChangeMotionState sets fp->frame_speed_mul to `anim_speed` (typically 1.0f)
        # on state changes, regardless of hitlag, and hitlag only gates the tick (Fighter_8006A360).
        # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        last = np.float32(1.0)
        out[i] = last

    return out
