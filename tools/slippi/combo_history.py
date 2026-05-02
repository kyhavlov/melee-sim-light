from __future__ import annotations

from pathlib import Path

import numpy as np


# Decomp trail (GALE01) for combo victim + last-attack/combo counter:
# - Combo update + last-attack id:
#   refs/melee/src/melee/ft/ftcoll.c::ftColl_800763C0 (writes fp->x208C, fp->x2090, fp->x2094)
#   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076444 (fighter->fighter; passes fp->x2068_attackID)
#   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007646C (item->fighter; passes item attack id domain)
# - Combo end clearing + timer decrement:
#   refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC (decrements fp->x2098; clears fp->x2094 based on victim hitstun + fp->x2098)
# - Victim combo-window timer reset on hitstun end:
#   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744 (fp->x2098 = p_ftCommonData->x4CC)
#
# Slippi outputs:
# - last_attack_landed is the low byte of lwz 0x208C(fp)
# - combo_count is the low byte of lhz 0x2090(fp)
# refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
#
# NOTE(x2094_equivalence): combo_victim_instance_id is seeded/debug metadata only; the sim's
# ftColl_800763C0/ftColl_800764DC port uses port-only equivalence for `fp->x2094` (raw pointer).

MAX_PLAYERS = 4


def derive_combo_push_timer_seed(
    *,
    combo_count: np.ndarray,  # [n_frames, MAX_PLAYERS] u8
    last_attack_landed: np.ndarray,  # [n_frames, MAX_PLAYERS] u8
    combo_victim_port: np.ndarray | None = None,  # [n_frames, MAX_PLAYERS] u8, 0xFF = none
    data_root: str | Path = "data",
) -> np.ndarray:
    """Derive post-frame snapshots for attacker combo-push timer fp->x2092.

    Decomp:
    - `ftColl_800763C0` sets `fp->x2092 = p_ftCommonData->x4D8` when repeated same-attack
      combo_count reaches `p_ftCommonData->x4C4`.
    - `ftColl_80076528` decrements x2092 and applies the grounded attacker push while nonzero.

    Slippi exposes the post-frame low byte of `fp->x2090` as `combo_count` and `fp->x208C` as
    `last_attack_landed`, but not x2092; this replay-history lane reconstructs x2092 causally from
    visible same-attack count increments. When the combo-victim lane is available, require the
    attacker's current victim provenance to stay on the same non-NULL player.
    """
    if str(data_root) not in ("data", "./data"):
        raise ValueError(
            "derive_combo_push_timer_seed now uses native common-data tables; set MSL_DATA_DIR for "
            "non-default data roots instead of running the removed Python fallback"
        )
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_combo_push_timer_seed is required for preprocessing; run `make build`"
        ) from exc
    return msl_binding.derive_combo_push_timer_seed(
        np.ascontiguousarray(combo_count, dtype=np.uint8),
        np.ascontiguousarray(last_attack_landed, dtype=np.uint8),
        (
            np.ascontiguousarray(combo_victim_port, dtype=np.uint8)
            if combo_victim_port is not None
            else None
        ),
    )


def derive_combo_seed_fields(
    *,
    num_players: int,
    src_ports: list[int],
    hitlag: np.ndarray,  # [n_frames, MAX_PLAYERS] u16
    state_flags: np.ndarray,  # [n_frames, MAX_PLAYERS, 5] u8
    instance_id: np.ndarray,  # [n_frames, MAX_PLAYERS] u16
    last_hit_by: np.ndarray,  # [n_frames, MAX_PLAYERS] u8 (Slippi port0 domain; 0xFF = none)
    instance_hit_by: np.ndarray | None = None,  # [n_frames, MAX_PLAYERS] u16 (optional)
    data_root: str | Path = "data",
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Derive combo victim pointer + combo timer strictly causally (prefix-invariant).

    Outputs are per-post-frame snapshots of:
    - combo_victim_port[t,p]         : 0..3 or 0xFF (NULL)  (sim fp->x2094)
    - combo_victim_instance_id[t,p]  : victim instance_id identity key (respawn-safe overlay)
    - combo_timer_x2098[t,p]         : frames remaining (sim fp->x2098)

    Derivation is rollout-causal and uses only victim-side observable hit events:
    - `last_hit_by` (attacker port),
    - hitlag rising edge (new hit),
    - hitstun flag transitions via state_flags[3] bit 0x02.
    """
    if num_players not in (2, 4):
        raise ValueError(f"num_players must be 2 or 4, got {num_players}")
    if len(src_ports) != num_players:
        raise ValueError(f"src_ports length {len(src_ports)} != num_players {num_players}")
    if any(p < 1 or p > 4 for p in src_ports):
        raise ValueError(f"src_ports must be in 1..4, got {src_ports}")
    if str(data_root) not in ("data", "./data"):
        raise ValueError(
            "derive_combo_seed_fields now uses native common-data tables; set MSL_DATA_DIR for "
            "non-default data roots instead of running the removed Python fallback"
        )
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_combo_seed_fields is required for preprocessing; run `make build`"
        ) from exc

    return msl_binding.derive_combo_seed_fields(
        int(num_players),
        [int(p) for p in src_ports],
        np.ascontiguousarray(hitlag, dtype=np.uint16),
        np.ascontiguousarray(state_flags, dtype=np.uint8),
        np.ascontiguousarray(instance_id, dtype=np.uint16),
        np.ascontiguousarray(last_hit_by, dtype=np.uint8),
        (
            np.ascontiguousarray(instance_hit_by, dtype=np.uint16)
            if instance_hit_by is not None
            else None
        ),
    )
