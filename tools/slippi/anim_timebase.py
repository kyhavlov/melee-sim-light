from __future__ import annotations

import struct
from dataclasses import dataclass
from functools import lru_cache
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


@lru_cache(maxsize=4)
def _load_end_frame_tables_cached(data_root: str) -> EndFrameTables:
    # All registry characters: the old hardcoded (1, 22) loop left non-spacie characters
    # without anim end-frame tables (silent None in every consumer).
    from tools.extraction.char_registry import CHARS as _REGISTRY_CHARS

    data_root_path = Path(data_root)
    by_char_id: dict[int, dict[int, float]] = {}
    for info in _REGISTRY_CHARS.values():
        path = data_root_path / "anims" / f"{info.name}.tracks.bin"
        if not path.exists():
            continue
        by_char_id[int(info.internal_id)] = _read_end_frames_from_tracks_bin(path)
    return EndFrameTables(by_char_id=by_char_id)


def load_end_frame_tables(data_root: Path) -> EndFrameTables:
    return _load_end_frame_tables_cached(str(Path(data_root).resolve()))


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

    The production pass is native because validation builds this lane for every player frame.
    It preserves the previous Python semantics: stable-segment state_age deltas, decomp-backed
    LandingAir/LandingFallSpecial entry formulas, and the GuardSetOff shield backsolve.
    """
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:  # pragma: no cover - build/config error path
        raise RuntimeError(
            "native msl_binding.derive_frame_speed_mul_f32 is required for preprocessing; run `make build`"
        ) from exc

    state_age_arr = np.ascontiguousarray(np.asarray(state_age_f32, dtype=np.float32).reshape(-1))
    action_arr = np.ascontiguousarray(np.asarray(action_id, dtype=np.uint16).reshape(-1))
    hitlag_arr = np.ascontiguousarray(np.asarray(hitlag, dtype=np.uint16).reshape(-1))
    char_arr = np.ascontiguousarray(np.asarray(char_id, dtype=np.uint8).reshape(-1))
    anim_arr = np.ascontiguousarray(np.asarray(animation_index, dtype=np.uint32).reshape(-1))
    lr_arr = np.ascontiguousarray(np.asarray(lr_press_timer, dtype=np.uint8).reshape(-1))

    hp = None if shield_hp is None else np.ascontiguousarray(np.asarray(shield_hp, dtype=np.float32).reshape(-1))
    light = (
        None
        if lightshield_amount is None
        else np.ascontiguousarray(np.asarray(lightshield_amount, dtype=np.float32).reshape(-1))
    )
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

    return msl_binding.derive_frame_speed_mul_f32(
        state_age_arr,
        action_arr,
        hitlag_arr,
        char_arr,
        anim_arr,
        lr_arr,
        hp,
        light,
        int(use_guard_setoff_entry_rate),
        float(0.0 if common_shield_hit_damage_mul is None else common_shield_hit_damage_mul),
        float(0.0 if common_shield_hit_damage_base is None else common_shield_hit_damage_base),
        float(0.0 if common_shield_hit_lightshield_min is None else common_shield_hit_lightshield_min),
        float(0.0 if common_shield_hit_lightshield_max is None else common_shield_hit_lightshield_max),
        float(0.0 if common_shield_stun_mul is None else common_shield_stun_mul),
        float(0.0 if common_shield_stun_base is None else common_shield_stun_base),
        float(0.0 if common_shield_stun_lightshield_min is None else common_shield_stun_lightshield_min),
        float(0.0 if common_shield_stun_lightshield_max is None else common_shield_stun_lightshield_max),
        end_frames.by_char_id,
        int(common_lcancel_window_frames),
        float(common_lcancel_lag_div),
        float(common_landing_fall_special_lag_frames),
        char_landing_air_lag_frames,
        {} if char_fallspecial_origin_lag is None else char_fallspecial_origin_lag,
    )
