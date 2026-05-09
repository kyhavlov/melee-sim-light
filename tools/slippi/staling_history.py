from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import pyarrow as pa

# Decomp trail (GALE01) for stale queue + duplicate suppression + multiplier:
# - Stale queue update + dup suppression:
#   refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromFighter
#   refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
# - Multiplier consult (previous 9, stop on move_id==0, move_id==1 => 1.0):
#   refs/melee/src/melee/ft/ft_0881.c::ft_80089118
#
# Decomp trail for "attack instance" (the value stored alongside move_id in the stale table):
# - Global counter + increment helper:
#   refs/melee/src/melee/pl/plstale.c::plStale_IncrementAttackInstance
# - Fighter-side assignment on motion-state change:
#   refs/melee/src/melee/ft/ft_0881.c::ft_800890D0 (calls plStale_IncrementAttackInstance)
# - Additional fighter-side bump sites (not fully reconstructible from Slippi post-frames):
#   refs/melee/src/melee/ft/ft_0881.c::ft_800892A0 (calls plStale_IncrementAttackInstance)
#
# Stale table reset on stock loss:
# - refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D34E0 (calls plStale_ResetStaleMoveTableForPlayer)


@dataclass(frozen=True)
class StalingHistory:
    # [n_frames, num_players]
    attack_id: np.ndarray
    # [n_frames, num_players]
    attack_instance: np.ndarray
    stale_queue_index: np.ndarray
    # [n_frames, num_players, 10]
    stale_move_id: np.ndarray
    stale_attack_instance: np.ndarray


def _to_numpy(arr) -> np.ndarray:
    x = arr.to_numpy(zero_copy_only=False)
    if isinstance(x, np.ma.MaskedArray):
        x = x.filled(0)
    if isinstance(x, np.ndarray) and x.dtype.kind == "f":
        x = np.nan_to_num(x, nan=0.0, posinf=0.0, neginf=0.0)
    return x


def _infer_attacker_slot_from_last_hit_by_instance(
    *,
    state_iid_row: np.ndarray,
    last_hit_by_instance: int,
) -> int:
    """Test-visible mirror of the native unique same-frame instance-id fallback."""
    hit_iid = int(last_hit_by_instance)
    if hit_iid == 0:
        return -1
    row = np.asarray(state_iid_row, dtype=np.uint16).reshape(-1)
    matches = np.flatnonzero(row.astype(np.int64) == hit_iid)
    return int(matches[0]) if matches.size == 1 else -1


def derive_staling_history(frames: pa.StructArray, *, src_ports: list[int], items_fixed=None) -> StalingHistory:
    """Derive stale-queue seed state strictly causally (prefix-invariant).

    Returns per-post-frame staling state:
      - attack_id[t,p]                : derived x2068_attackID (u16; decomp-backed)
      - attack_instance[t,p]          : derived x206C_attack_instance (u16; decomp-backed)
      - stale_queue_index[t,p]        : ring buffer write index (u8)
      - stale_move_id[t,p,10]         : stale table move_id entries (u16)
      - stale_attack_instance[t,p,10] : stale table attack_instance entries (u16)

    Notes / limitations (decomp-first, conservative):
    - We model the fighter-side bump on motion-state change (ft_800890D0) using Slippi post-frame
      action-state changes. We also model the Fox/Falco SpecialN Loop -> Loop restart callback
      (`ftFx_SpecialN_OnChangeAction -> ft_800892A0`) because it is visible as same-action
      SpecialNLoop instance-id changes with action_frame reset and is needed for laser stale queue
      duplicate suppression.
    - If the victim's `last_hit_by` does not map to a known source port (attacker unknown), we
      fall back to inferring the attacker from a unique match on `last_hit_by_instance` against the
      current frame's action-state instance_id set (no lookahead; skip if ambiguous).
    - Hit attribution uses Slippi post-frame percent deltas (damaging hits only) and the victim's
      last_hit_by / last_hit_by_instance fields to select the attacker and tie the hit to the
      attacker's historical action-state instance_id when needed (e.g. projectiles inheriting
      owner instance_id).
    - When `items_fixed` is provided, damaging hits first resolve any matching fighter action-state
      instance id. Only hits not attributable to a fighter row may use a unique matching item row,
      whose current owner plus spawn-latched attack_id/attack_instance mirrors
      plStale_UpdateStaleMovesFromItem for reflected lasers and other item-owned damage; the
      previous post-frame item row is consulted first because projectile hits can destroy the item
      before the damage post-frame is serialized.
    """
    if frames is None:
        raise ValueError("frames is None")
    if len(src_ports) == 0:
        raise ValueError("src_ports is empty")
    if any(p < 1 or p > 4 for p in src_ports):
        raise ValueError(f"src_ports must be in 1..4, got {src_ports}")

    src_ports = list(src_ports)
    src_port_names = [f"P{p}" for p in src_ports]
    num_players = len(src_port_names)

    ports_struct = frames.field("ports")
    available_ports = set(f.name for f in ports_struct.type)
    for name in src_port_names:
        if name not in available_ports:
            raise ValueError(f"Replay missing port {name}; available ports: {sorted(available_ports)}")

    # Pull all post-frame arrays we need (for vectorized indexing).
    post = []
    for name in src_port_names:
        post.append(ports_struct.field(name).field("leader").field("post"))

    char_id = np.stack([_to_numpy(p.field("character")).astype(np.uint8) for p in post], axis=1)
    action_id = np.stack([_to_numpy(p.field("state")).astype(np.uint16) for p in post], axis=1)
    action_frame = np.stack([_to_numpy(p.field("state_age")).astype(np.float32) for p in post], axis=1)
    animation_index = np.stack(
        [_to_numpy(p.field("animation_index")).astype(np.uint32) for p in post], axis=1
    )
    percent = np.stack([_to_numpy(p.field("percent")).astype(np.float32) for p in post], axis=1)
    stocks = np.stack([_to_numpy(p.field("stocks")).astype(np.uint8) for p in post], axis=1)
    state_iid = np.stack([_to_numpy(p.field("instance_id")).astype(np.uint16) for p in post], axis=1)
    last_hit_by = np.stack([_to_numpy(p.field("last_hit_by")).astype(np.uint8) for p in post], axis=1)
    last_hit_by_instance = np.stack(
        [_to_numpy(p.field("last_hit_by_instance")).astype(np.uint16) for p in post], axis=1
    )
    try:
        import msl_binding  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "native msl_binding.derive_staling_history is required for preprocessing; run `make build`"
        ) from exc
    args = [
        [int(p) for p in src_ports],
        np.ascontiguousarray(char_id, dtype=np.uint8),
        np.ascontiguousarray(action_id, dtype=np.uint16),
        np.ascontiguousarray(action_frame, dtype=np.float32),
        np.ascontiguousarray(animation_index, dtype=np.uint32),
        np.ascontiguousarray(percent, dtype=np.float32),
        np.ascontiguousarray(stocks, dtype=np.uint8),
        np.ascontiguousarray(state_iid, dtype=np.uint16),
        np.ascontiguousarray(last_hit_by, dtype=np.uint8),
        np.ascontiguousarray(last_hit_by_instance, dtype=np.uint16),
    ]
    if items_fixed is not None:
        args.extend(
            [
                np.ascontiguousarray(items_fixed["exists"], dtype=np.uint8),
                np.ascontiguousarray(items_fixed["owner"], dtype=np.int8),
                np.ascontiguousarray(items_fixed["instance_id"], dtype=np.uint16),
                np.ascontiguousarray(items_fixed["attack_id"], dtype=np.uint16),
                np.ascontiguousarray(items_fixed["attack_instance"], dtype=np.uint16),
            ]
        )
    (
        attack_id_out,
        attack_inst_out,
        qi_out,
        stale_mid_out,
        stale_inst_out,
    ) = msl_binding.derive_staling_history(*args)

    return StalingHistory(
        attack_id=attack_id_out,
        attack_instance=attack_inst_out,
        stale_queue_index=qi_out,
        stale_move_id=stale_mid_out,
        stale_attack_instance=stale_inst_out,
    )
