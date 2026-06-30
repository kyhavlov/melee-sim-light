from __future__ import annotations

import numpy as np

def derive_damage_time_since_hit_x18ac(
    *,
    action_id_u16: np.ndarray,
    hitlag_u16: np.ndarray,
    hitstun_u16: np.ndarray,
    state_flags_u8: np.ndarray,
) -> np.ndarray:
    """Derive fp->dmg.x18AC_time_since_hit from replay-visible damage edges.

    Decomp trail:
    - Fighter init sets x18AC to -1.
    - Fighter_8006A360 increments it once per non-hitlag frame while active.
    - ftCo_8008DCE0 resets it to 0 on fresh Damage entry.
    - ftCo_Damage_CalcVel uses it for the p_ftCommonData->xFC KB velocity merge gate.

    Slippi does not expose x18AC. This producer stays prefix-invariant by detecting only causal
    post-frame damage events: hitlag starts with active hitstun, or hitstun rises.
    """

    action = np.ascontiguousarray(action_id_u16, dtype=np.uint16).reshape(-1)
    hitlag = np.ascontiguousarray(hitlag_u16, dtype=np.uint16).reshape(-1)
    hitstun = np.ascontiguousarray(hitstun_u16, dtype=np.uint16).reshape(-1)
    flags = np.ascontiguousarray(state_flags_u8, dtype=np.uint8)
    if flags.ndim != 2 or flags.shape[1] < 4:
        raise ValueError("state_flags_u8 must have shape [n, >=4]")
    if (
        int(hitlag.shape[0]) != int(hitstun.shape[0])
        or int(hitlag.shape[0]) != int(flags.shape[0])
        or int(hitlag.shape[0]) != int(action.shape[0])
    ):
        raise ValueError("damage-history input arrays must have matching frame counts")

    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_damage_time_since_hit_x18ac is required; run `make build`"
        ) from exc

    return msl_binding.derive_damage_time_since_hit_x18ac(action, hitlag, hitstun, flags)
