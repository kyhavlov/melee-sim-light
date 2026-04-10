from __future__ import annotations

import numpy as np


_IS_HITSTUN_MASK_221C = 0x02


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

    del action_id_u16  # Reserved for future action-specific damage-entry probes.

    hitlag = np.asarray(hitlag_u16, dtype=np.uint16).reshape(-1)
    hitstun = np.asarray(hitstun_u16, dtype=np.uint16).reshape(-1)
    flags = np.asarray(state_flags_u8, dtype=np.uint8)
    if flags.ndim != 2 or flags.shape[1] < 4:
        raise ValueError("state_flags_u8 must have shape [n, >=4]")
    if int(hitlag.shape[0]) != int(hitstun.shape[0]) or int(hitlag.shape[0]) != int(flags.shape[0]):
        raise ValueError("damage-history input arrays must have matching frame counts")

    out = np.full((int(hitlag.shape[0]),), -1, dtype=np.int16)
    timer = -1
    prev_hitlag = 0
    prev_hitstun = 0

    for fi in range(int(hitlag.shape[0])):
        hl = int(hitlag[fi])
        hs = int(hitstun[fi])
        in_hitstun = (int(flags[fi, 3]) & _IS_HITSTUN_MASK_221C) != 0
        fresh_damage = (prev_hitlag == 0 and hl > 0 and (hs > 0 or in_hitstun)) or (
            hs > prev_hitstun + 1
        )

        if fresh_damage:
            timer = 0
        elif timer >= 0 and hl == 0:
            timer = min(timer + 1, np.iinfo(np.int16).max)

        out[fi] = np.int16(timer)
        prev_hitlag = hl
        prev_hitstun = hs

    return out
