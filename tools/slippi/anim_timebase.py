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
    # Target domain mapping (GALE01): Fox=1, Falco=22.
    # refs/melee/src/melee/ft/types.h and Slippi post-frame `character`.
    if int(char_id) == 1:
        return "fox"
    if int(char_id) == 22:
        return "falco"
    return None


def _read_end_frames_from_tracks_bin(path: Path) -> dict[int, float]:
    """
    Parse `data/anims/<char>.tracks.bin` (SSANIMT1 v1) and return msid->end_frame.

    Format is mirrored in `src/anim_table.c::load_tracks_for_char`.
    """
    buf = path.read_bytes()
    if len(buf) < 8 + 4 + 4:
        raise ValueError(f"{path}: SSANIMT1 file too small")
    if buf[:8] != b"SSANIMT1":
        raise ValueError(f"{path}: bad magic {buf[:8]!r}")
    (ver,) = struct.unpack_from("<I", buf, 8)
    if int(ver) != 1:
        raise ValueError(f"{path}: unsupported version {ver} (want 1)")

    local_count, anim_count = struct.unpack_from("<HH", buf, 12)
    off = 16

    # Skip local_parts[u8], local_parent[i16], local_flags[u32]
    skip_hdr = int(local_count) + int(local_count) * 2 + int(local_count) * 4
    if off + skip_hdr > len(buf):
        raise ValueError(f"{path}: truncated locals header")
    off += skip_hdr

    out: dict[int, float] = {}
    for _ in range(int(anim_count)):
        if off + 2 + 4 > len(buf):
            raise ValueError(f"{path}: truncated anim table")
        (msid,) = struct.unpack_from("<H", buf, off)
        (end_frame,) = struct.unpack_from("<f", buf, off + 2)
        off += 6

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
    by_char_id: dict[int, dict[int, float]] = {}
    for cid in (1, 22):
        key = _char_key_from_char_id(cid)
        if key is None:
            continue
        by_char_id[int(cid)] = _read_end_frames_from_tracks_bin(data_root / "anims" / f"{key}.tracks.bin")
    return EndFrameTables(by_char_id=by_char_id)


def derive_frame_speed_mul_f32(
    *,
    state_age_f32: np.ndarray,  # [n] f32
    action_id: np.ndarray,  # [n] u16
    hitlag: np.ndarray,  # [n] u16 (frames left)
    char_id: np.ndarray,  # [n] u8
    animation_index: np.ndarray,  # [n] u32 (msid or 0xFFFFFFFF)
    lr_press_timer: np.ndarray,  # [n] u8 (x67F: frames since L/R press)
    end_frames: EndFrameTables,
    common_lcancel_window_frames: int,
    common_lcancel_lag_div: float,
    common_landing_fall_special_lag_frames: float,
    char_landing_air_lag_frames: dict[int, dict[str, int]],  # char_id -> {"airn":..,"airf":..,...}
) -> np.ndarray:
    """
    Strictly-causal derivation of fp->frame_speed_mul (float).

    Goal: provide a reseedable approximation to the engine's animation rate for the *next* frame's
    anim advance, without looking at future frames.

    - On stable segments (same action+msid), use delta(state_age) as a proxy for the rate.
    - On motion-state entry frames where state_age is reset (delta may be negative), use decomp-backed
      LandingAir / LandingFallSpecial formulas to seed the new rate.

    This is intentionally conservative and suite-scoped: LandingAir*/LandingFallSpecial are the early
    primary drivers for fractional anim advance in the Fox/Falco FD suite.
    """
    n = int(state_age_f32.shape[0])
    out = np.empty(n, dtype=np.float32)
    last = np.float32(1.0)
    if n == 0:
        return out
    out[0] = last

    # Action ids (GALE01) for suite-relevant landing states.
    # These are stable identifiers, not heuristics.
    ACT_LANDING_AIR_N = np.uint16(0x0043)
    ACT_LANDING_AIR_F = np.uint16(0x0044)
    ACT_LANDING_AIR_B = np.uint16(0x0045)
    ACT_LANDING_AIR_HI = np.uint16(0x0046)
    ACT_LANDING_AIR_LW = np.uint16(0x0047)
    ACT_LANDING_FALL_SPECIAL = np.uint16(0x0048)

    for i in range(1, n):
        if int(hitlag[i]) != 0:
            out[i] = last
            continue

        changed = (
            int(action_id[i]) != int(action_id[i - 1])
            or int(animation_index[i]) != int(animation_index[i - 1])
            or int(char_id[i]) != int(char_id[i - 1])
        )
        if not changed:
            delta = np.float32(state_age_f32[i] - state_age_f32[i - 1])
            if np.isfinite(delta) and float(delta) >= 0.0:
                last = delta
            out[i] = last
            continue

        cid = int(char_id[i])
        msid_u32 = int(animation_index[i])
        msid = int(msid_u32 & 0xFFFF) if msid_u32 != 0xFFFFFFFF else None
        end_frame = None
        if msid is not None:
            end_frame = end_frames.by_char_id.get(cid, {}).get(int(msid))

        a = np.uint16(action_id[i])

        if a == ACT_LANDING_FALL_SPECIAL and end_frame is not None and common_landing_fall_special_lag_frames > 0.0:
            # Decomp: ftCo_LandingFallSpecial_Enter uses (0.1 + fp->x2EC) / landing_lag.
            # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
            last = np.float32((np.float32(end_frame) + np.float32(0.1)) / np.float32(common_landing_fall_special_lag_frames))
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

        # Fallback: default rate.
        last = np.float32(1.0)
        out[i] = last

    return out

