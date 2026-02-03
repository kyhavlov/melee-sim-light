from __future__ import annotations

import numpy as np


# Slippi post-frame state flags are sent from Melee fighter bytes around fp+0x2218..0x221F.
# The fp+0x221C byte contains an `isHitstun` bit at mask 0x02.
#
# Source pointer:
# - refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
#   - \"send bitflags\": `lbz r3,0x221C(REG_PlayerData)   #0x2 = isHitstun`
#   - \"send misc AS variable\": `lwz r3,0x2340(REG_PlayerData)` and the comment stating it is
#     hitstun frames left when the 0x221C hitstun bool is enabled.
_IS_HITSTUN_MASK_221C = 0x02


def u16_from_hitstun_misc(misc_as_f32: np.ndarray | None, *, n: int) -> np.ndarray:
    # Slippi spec: misc_as is hitstun remaining when in hitstun; otherwise used for other things.
    # Keep conversion simple and deterministic: clamp-negative-to-0 and floor.
    if misc_as_f32 is None:
        return np.zeros(n, dtype=np.uint16)
    x = np.asarray(misc_as_f32, dtype=np.float32)
    x = np.clip(x, 0.0, 65535.0)
    return np.floor(x).astype(np.uint16)


def hitstun_u16_from_misc_as_and_state_flags3(
    *, misc_as_f32: np.ndarray | None, state_flags3_u8: np.ndarray | None, n: int
) -> np.ndarray:
    """Compute post-frame hitstun remaining from Slippi `misc_as` gated by the hitstun flag."""
    misc_u16 = u16_from_hitstun_misc(misc_as_f32, n=n)
    if state_flags3_u8 is None:
        return np.zeros(n, dtype=np.uint16)
    sf3 = np.asarray(state_flags3_u8, dtype=np.uint8).reshape(-1)
    if int(sf3.size) != int(n):
        raise ValueError(f"state_flags3 size {int(sf3.size)} != n {int(n)}")
    in_hitstun = (sf3 & np.uint8(_IS_HITSTUN_MASK_221C)) != 0
    return np.where(in_hitstun, misc_u16, np.uint16(0)).astype(np.uint16)
