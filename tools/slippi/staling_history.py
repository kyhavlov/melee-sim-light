from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

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


_U16_MAX = 0xFFFF
_FT_MOVE_ID_DEFAULT = 1  # refs/melee/src/melee/ft/forward.h::FtMoveId (Default is value 1)
_STALE_QUEUE_SIZE = 10
_ACT_GUARD_ON = 178
_ACT_GUARD = 179
_ACT_GUARD_OFF = 180
_NO_SUBMOTION_INDEX = 0xFFFFFFFF


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


_action_move_id_tables: dict[int, list[int]] = {}


def _load_action_move_id_table_for_char(char_id: int, *, data_dir: Path = Path("data")) -> list[int]:
    cached = _action_move_id_tables.get(int(char_id))
    if cached is not None:
        return cached

    # Match the runtime binary format written by tools/extraction/extract_attack_id_move_id.py.
    # data/attack_id/move_id/{fox,falco}.bin use magic "MSLACID1".
    name_by_char = {1: "fox", 22: "falco"}
    rel = name_by_char.get(int(char_id))
    if rel is None:
        _action_move_id_tables[int(char_id)] = []
        return []

    path = data_dir / "attack_id" / "move_id" / f"{rel}.bin"
    try:
        buf = path.read_bytes()
    except FileNotFoundError as e:
        raise FileNotFoundError(
            "\n".join(
                [
                    f"Missing action_id->move_id table: {path}",
                    "",
                    "Fighter attack identity preprocessing requires these generated artifacts.",
                    "Generate them from the decomp refs with:",
                    "  uv run python -m tools.extraction.extract_attack_id_move_id "
                    "--melee_decomp refs/melee --out_dir data/attack_id/move_id --chars fox,falco",
                ]
            )
        ) from e
    if len(buf) < 24:
        raise ValueError(f"action move_id table too small: {path}")
    if buf[:8] != b"MSLACID1":
        raise ValueError(f"bad action move_id magic: {path}")
    (ver,) = struct.unpack_from("<I", buf, 8)
    if ver not in (1, 2):
        raise ValueError(f"unsupported action move_id table version {ver} in {path}")
    (count,) = struct.unpack_from("<H", buf, 12)
    if ver == 1:
        (toc_off,) = struct.unpack_from("<I", buf, 16)
        (file_bytes,) = struct.unpack_from("<I", buf, 20)
    else:
        # v2 adds a second table (MotionState.x4_flags) after move_id.
        # Layout is documented in docs/DATA_CONTRACT.md under MSLACID1 v2.
        if len(buf) < 28:
            raise ValueError(f"action move_id table header too small for v2: {path}")
        (toc_off,) = struct.unpack_from("<I", buf, 16)
        (_flags_off,) = struct.unpack_from("<I", buf, 20)
        (file_bytes,) = struct.unpack_from("<I", buf, 24)
    if file_bytes != len(buf):
        raise ValueError(f"action move_id file_bytes mismatch in {path}: {file_bytes} != {len(buf)}")
    if toc_off + count * 2 > len(buf):
        raise ValueError(f"action move_id toc out of range in {path}")

    out: list[int] = []
    off = toc_off
    for _ in range(int(count)):
        (mv,) = struct.unpack_from("<H", buf, off)
        off += 2
        out.append(int(mv))

    _action_move_id_tables[int(char_id)] = out
    return out


def _move_id_from_char_action(char_id: int, action_id_u16: int) -> int:
    if action_id_u16 < 0 or action_id_u16 > 0xFFFF:
        return _FT_MOVE_ID_DEFAULT
    action_id = int(action_id_u16) & 0xFFFF
    tab = _load_action_move_id_table_for_char(int(char_id))
    if action_id >= len(tab):
        return _FT_MOVE_ID_DEFAULT
    mv = int(tab[action_id])
    # The extracted table uses 0xFFFF as a sentinel for "unknown/absent". For fighter-side
    # `x2068_attackID`, prefer the decomp-default `FtMoveId_Default` (1).
    # refs/melee/src/melee/ft/forward.h::FtMoveId
    if mv == _U16_MAX:
        return _FT_MOVE_ID_DEFAULT
    return mv


def _infer_attacker_slot_from_last_hit_by_instance(
    *,
    state_iid_row: np.ndarray,
    last_hit_by_instance: int,
) -> int:
    """Infer attacker slot when `last_hit_by` is unknown.

    Uses only same-frame (t) state:
    - `last_hit_by_instance` is the victim's Slippi `last_hit_by_instance` at t.
    - `state_iid_row` is the per-player Slippi `instance_id` (action-state iid) at t.

    Returns the unique matching player slot, or -1 if unknown/ambiguous.
    """
    hit_iid = int(last_hit_by_instance)
    if hit_iid == 0:
        return -1
    matches = np.flatnonzero(state_iid_row.astype(np.int64) == hit_iid)
    if matches.size != 1:
        return -1
    return int(matches[0])


def derive_staling_history(frames: pa.StructArray, *, src_ports: list[int]) -> StalingHistory:
    """Derive stale-queue seed state strictly causally (prefix-invariant).

    Returns per-post-frame staling state:
      - attack_id[t,p]                : derived x2068_attackID (u16; decomp-backed)
      - attack_instance[t,p]          : derived x206C_attack_instance (u16; decomp-backed)
      - stale_queue_index[t,p]        : ring buffer write index (u8)
      - stale_move_id[t,p,10]         : stale table move_id entries (u16)
      - stale_attack_instance[t,p,10] : stale table attack_instance entries (u16)

    Notes / limitations (decomp-first, conservative):
    - We model the fighter-side bump on motion-state change (ft_800890D0) using Slippi post-frame
      action-state changes. We do not currently model additional bump sites like ft_800892A0 because
      they are not unambiguously observable from Slippi post-frames in general.
    - If the victim's `last_hit_by` does not map to a known source port (attacker unknown), we
      fall back to inferring the attacker from a unique match on `last_hit_by_instance` against the
      current frame's action-state instance_id set (no lookahead; skip if ambiguous).
    - Hit attribution uses Slippi post-frame percent deltas (damaging hits only) and the victim's
      last_hit_by / last_hit_by_instance fields to select the attacker and tie the hit to the
      attacker's historical action-state instance_id when needed (e.g. projectiles inheriting
      owner instance_id).
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

    # Map Slippi's 0-based port indices (P1->0, ...) to our contiguous [0..num_players) slots.
    slot_by_port0: dict[int, int] = {p - 1: i for i, p in enumerate(src_ports)}

    ports_struct = frames.field("ports")
    available_ports = set(f.name for f in ports_struct.type)
    for name in src_port_names:
        if name not in available_ports:
            raise ValueError(f"Replay missing port {name}; available ports: {sorted(available_ports)}")

    n_frames = int(len(frames))
    qi_out = np.zeros((n_frames, num_players), dtype=np.uint8)
    stale_mid_out = np.zeros((n_frames, num_players, _STALE_QUEUE_SIZE), dtype=np.uint16)
    stale_inst_out = np.zeros((n_frames, num_players, _STALE_QUEUE_SIZE), dtype=np.uint16)
    attack_inst_out = np.zeros((n_frames, num_players), dtype=np.uint16)
    attack_id_out = np.zeros((n_frames, num_players), dtype=np.uint16)

    # Per-player live stale tables.
    qi = np.zeros(num_players, dtype=np.uint8)
    table_mid = np.zeros((num_players, _STALE_QUEUE_SIZE), dtype=np.uint16)
    table_inst = np.zeros((num_players, _STALE_QUEUE_SIZE), dtype=np.uint16)

    # Derived fighter-side attack_id + attack_instance (x2068/x206C), per player.
    cur_attack_id = np.full(num_players, _FT_MOVE_ID_DEFAULT, dtype=np.uint16)
    cur_attack_inst = np.zeros(num_players, dtype=np.uint16)
    stale_attack_counter = 1  # plStale_InitAttackInstance initializes to 1.

    # Map from Slippi action-state instance_id (fp+0x2088) to the derived (attack_id, attack_instance)
    # for that state, per player.
    # Used to attribute hits that reference an older state instance_id (e.g. projectiles inheriting owner).
    by_state_iid: list[dict[int, tuple[int, int]]] = [dict() for _ in range(num_players)]

    # Cache previous action_id + action-state instance_id for causality and hit attribution.
    prev_action_id = np.full(num_players, 0xFFFF, dtype=np.uint16)
    prev_state_iid = np.zeros(num_players, dtype=np.uint16)

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

    def _inc_attack_instance() -> int:
        nonlocal stale_attack_counter
        before = stale_attack_counter & 0xFFFF
        stale_attack_counter = (stale_attack_counter + 1) & 0xFFFF
        if stale_attack_counter == 0:
            stale_attack_counter = 1
        return before

    def _reset_player_stale_table(p: int) -> None:
        qi[p] = 0
        table_mid[p, :] = 0
        table_inst[p, :] = 0

    def _reset_player_attack_identity(p: int) -> None:
        # Decomp: fighter reset/default is (attackID=1, instance=0).
        # refs/melee/src/melee/ft/ft_0881.c::ft_800890BC
        cur_attack_id[p] = np.uint16(_FT_MOVE_ID_DEFAULT)
        cur_attack_inst[p] = np.uint16(0)
        prev_action_id[p] = np.uint16(0xFFFF)
        prev_state_iid[p] = np.uint16(0)

    def _queue_update(p: int, move_id: int, attack_instance: int) -> None:
        if move_id in (_U16_MAX, _FT_MOVE_ID_DEFAULT) or attack_instance == 0:
            return
        # Duplicate suppression: ignore if exact (move_id, attack_instance) already present.
        if np.any((table_mid[p, :] == np.uint16(move_id)) & (table_inst[p, :] == np.uint16(attack_instance))):
            return
        pos = int(qi[p])
        if pos >= _STALE_QUEUE_SIZE:
            pos = 0
        table_mid[p, pos] = np.uint16(move_id)
        table_inst[p, pos] = np.uint16(attack_instance)
        qi[p] = np.uint8(0 if pos == (_STALE_QUEUE_SIZE - 1) else (pos + 1))

    # Main causal pass.
    for t in range(n_frames):
        # Per-player: detect stock loss and reset stale tables + fighter attack identity.
        if t > 0:
            for p in range(num_players):
                if stocks[t, p] < stocks[t - 1, p]:
                    # Decomp: stale table reset happens on stock loss.
                    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D34E0 (calls plStale_ResetStaleMoveTableForPlayer)
                    _reset_player_stale_table(p)
                    _reset_player_attack_identity(p)

        # Per-player: update derived attack_id / attack_instance on motion-state changes.
        #
        # Decomp trail (GALE01):
        # - Reset/default: refs/melee/src/melee/ft/ft_0881.c::ft_800890BC
        # - Update on motion change: refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
        # - Call site: refs/melee/src/melee/ft/fighter.c inside Fighter_ChangeMotionState calls
        #   ft_800890D0(fp, new_motion_state->move_id).
        for p in range(num_players):
            iid = int(state_iid[t, p])
            if iid == 0:
                # Slippi spec: instance_id resets to 0 temporarily on death.
                # Keep the fighter's derived x206C at 0 until the next observable state transition.
                _reset_player_attack_identity(p)
                attack_id_out[t, p] = np.uint16(_FT_MOVE_ID_DEFAULT)
                continue

            act = int(action_id[t, p])
            if act != int(prev_action_id[p]):
                if (
                    act == _ACT_GUARD_OFF
                    and int(prev_action_id[p]) == _ACT_GUARD_ON
                    and t > 0
                    and int(action_id[t - 1, p]) == _ACT_GUARD_ON
                    and int(animation_index[t - 1, p]) == _NO_SUBMOTION_INDEX
                    and float(action_frame[t - 1, p]) < 0.0
                ):
                    # Hidden GuardOn -> Guard -> GuardOff same-frame handoff:
                    # - GuardOn_Anim can enter Guard through ftCo_800928CC/ftCo_80092908 before
                    #   GuardOn_IASA's release gate enters GuardOff.
                    # - Slippi exposes only final GuardOff, but both default-move
                    #   Fighter_ChangeMotionState bundles consume plStale_IncrementAttackInstance.
                    # Keep the derived global attack-instance counter aligned for later item-spawn
                    # copies without inventing a runtime replay row branch.
                    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
                    #   ftCo_GuardOn_Anim,ftCo_800928CC,ftCo_80092908,ftCo_GuardOn_IASA,
                    #   ftCo_80092C54}
                    hidden_guard_inst = _inc_attack_instance()
                    cur_attack_id[p] = np.uint16(_move_id_from_char_action(int(char_id[t, p]), _ACT_GUARD))
                    cur_attack_inst[p] = np.uint16(hidden_guard_inst)

                move_id = _move_id_from_char_action(int(char_id[t, p]), act)
                # Decomp: ft_800890D0 increments x206C when move_id==1 OR move_id != current attackID.
                if move_id == _FT_MOVE_ID_DEFAULT or move_id != int(cur_attack_id[p]):
                    cur_attack_id[p] = np.uint16(move_id)
                    cur_attack_inst[p] = np.uint16(_inc_attack_instance())
                prev_action_id[p] = np.uint16(act)

            if iid != int(prev_state_iid[p]):
                by_state_iid[p].setdefault(iid, (int(cur_attack_id[p]), int(cur_attack_inst[p])))
                prev_state_iid[p] = np.uint16(iid)

            attack_inst_out[t, p] = cur_attack_inst[p]
            attack_id_out[t, p] = cur_attack_id[p]

        # Damaging hits: detect via percent delta.
        if t > 0:
            for victim in range(num_players):
                dp = float(percent[t, victim] - percent[t - 1, victim])
                if not (dp > 0.0):
                    continue

                # Identify attacker slot (0-based port index in Slippi post is stored in last_hit_by).
                a_port0 = int(last_hit_by[t, victim])
                attacker = slot_by_port0.get(a_port0, -1)
                if attacker < 0:
                    # Fallback: infer attacker from the victim's `last_hit_by_instance` by matching
                    # it against a unique player's action-state instance_id at this frame.
                    #
                    # Rationale:
                    # - Some item/projectile damage events (notably "attached victim" edge cases)
                    #   can leave `last_hit_by` unmapped while still populating `last_hit_by_instance`.
                    # - Staling tables are attacker-owned and *do* update in-game for those hits,
                    #   so skipping the enqueue here causes stale table drift and percent mismatches
                    #   under teacher-forced reseed.
                    hit_iid = int(last_hit_by_instance[t, victim])
                    attacker = _infer_attacker_slot_from_last_hit_by_instance(
                        state_iid_row=state_iid[t, :],
                        last_hit_by_instance=hit_iid,
                    )
                    if attacker < 0:
                        continue
                if attacker == victim:
                    continue

                hit_iid = int(last_hit_by_instance[t, victim])
                att_move_id = _U16_MAX
                att_attack_inst = 0
                if hit_iid != 0:
                    v = by_state_iid[attacker].get(hit_iid)
                    if v is not None:
                        att_move_id, att_attack_inst = v

                # Fallback to the attacker's current state (best-effort).
                if att_move_id == _U16_MAX or att_attack_inst == 0:
                    att_move_id = int(cur_attack_id[attacker])
                    att_attack_inst = int(cur_attack_inst[attacker])

                _queue_update(attacker, att_move_id, att_attack_inst)

        qi_out[t, :] = qi
        stale_mid_out[t, :, :] = table_mid
        stale_inst_out[t, :, :] = table_inst

    return StalingHistory(
        attack_id=attack_id_out,
        attack_instance=attack_inst_out,
        stale_queue_index=qi_out,
        stale_move_id=stale_mid_out,
        stale_attack_instance=stale_inst_out,
    )
