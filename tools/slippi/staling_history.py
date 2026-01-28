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


_move_id_tables: dict[int, dict[int, int]] = {}


def _load_move_id_table_for_char(char_id: int, *, data_dir: Path = Path("data")) -> dict[int, int]:
    cached = _move_id_tables.get(int(char_id))
    if cached is not None:
        return cached

    # Match the runtime binary format written by tools/extraction/extract_staling_move_id.py.
    # data/staling/move_id/{fox,falco}.bin use magic "MSLSTID1".
    name_by_char = {1: "fox", 22: "falco"}
    rel = name_by_char.get(int(char_id))
    if rel is None:
        _move_id_tables[int(char_id)] = {}
        return _move_id_tables[int(char_id)]

    path = data_dir / "staling" / "move_id" / f"{rel}.bin"
    try:
        buf = path.read_bytes()
    except FileNotFoundError as e:
        raise FileNotFoundError(
            "\n".join(
                [
                    f"Missing staling move-id table: {path}",
                    "",
                    "Staling is wired unconditionally into dataset preprocessing and requires these generated artifacts.",
                    "Generate them from the decomp refs with:",
                    "  uv run python -m tools.extraction.extract_staling_move_id "
                    "--melee_decomp refs/melee --out_dir data/staling/move_id --chars fox,falco",
                ]
            )
        ) from e
    if len(buf) < 24:
        raise ValueError(f"staling move_id table too small: {path}")
    if buf[:8] != b"MSLSTID1":
        raise ValueError(f"bad staling move_id magic: {path}")
    (ver,) = struct.unpack_from("<I", buf, 8)
    if ver != 1:
        raise ValueError(f"unsupported staling move_id table version {ver} in {path}")
    (count,) = struct.unpack_from("<H", buf, 12)
    (toc_off,) = struct.unpack_from("<I", buf, 16)
    (file_bytes,) = struct.unpack_from("<I", buf, 20)
    if file_bytes != len(buf):
        raise ValueError(f"staling move_id file_bytes mismatch in {path}: {file_bytes} != {len(buf)}")
    if toc_off + count * 4 > len(buf):
        raise ValueError(f"staling move_id toc out of range in {path}")

    out: dict[int, int] = {}
    off = toc_off
    for _ in range(int(count)):
        msid, move_id = struct.unpack_from("<HH", buf, off)
        off += 4
        out[int(msid)] = int(move_id)

    _move_id_tables[int(char_id)] = out
    return out


def _move_id_from_char_msid(char_id: int, msid_u32: int) -> int:
    if msid_u32 < 0 or msid_u32 > 0xFFFF:
        return _FT_MOVE_ID_DEFAULT
    msid = int(msid_u32) & 0xFFFF
    tab = _load_move_id_table_for_char(int(char_id))
    mv = tab.get(msid)
    # The extracted table uses 0xFFFF as a sentinel for "ambiguous/unknown". For fighter-side
    # `x2068_attackID`, prefer the decomp-default `FtMoveId_Default` (1) over propagating a
    # nonexistent 0xFFFF move id into combo/item attribution.
    # refs/melee/src/melee/ft/forward.h::FtMoveId
    if mv is None or int(mv) == _U16_MAX:
        return _FT_MOVE_ID_DEFAULT
    return int(mv)


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
    - If the victim's `last_hit_by` does not map to a known source port (attacker unknown), we skip
      enqueuing for that hit (no heuristic fallback).
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

    # Cache the last seen Slippi action-state instance_id to detect state transitions.
    prev_state_iid = np.zeros(num_players, dtype=np.uint16)

    # Pull all post-frame arrays we need (for vectorized indexing).
    post = []
    for name in src_port_names:
        post.append(ports_struct.field(name).field("leader").field("post"))

    char_id = np.stack([_to_numpy(p.field("character")).astype(np.uint8) for p in post], axis=1)
    msid_u32 = np.stack([_to_numpy(p.field("animation_index")).astype(np.uint32) for p in post], axis=1)
    action_id = np.stack([_to_numpy(p.field("state")).astype(np.uint16) for p in post], axis=1)
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

    def _reset_player(p: int) -> None:
        qi[p] = 0
        table_mid[p, :] = 0
        table_inst[p, :] = 0

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
        # Per-player: detect stock loss and reset stale tables.
        if t > 0:
            for p in range(num_players):
                if stocks[t, p] < stocks[t - 1, p]:
                    _reset_player(p)

        # Per-player: update derived attack_instance on action-state transitions (conservative).
        for p in range(num_players):
            iid = int(state_iid[t, p])
            if iid == 0:
                # Slippi spec: instance_id resets to 0 temporarily on death.
                # Keep the fighter's derived x206C at 0 until the next observable state transition.
                prev_state_iid[p] = np.uint16(0)
                cur_attack_id[p] = np.uint16(_FT_MOVE_ID_DEFAULT)
                cur_attack_inst[p] = np.uint16(0)
                attack_id_out[t, p] = np.uint16(_FT_MOVE_ID_DEFAULT)
                continue

            if iid != int(prev_state_iid[p]):
                # Conservative: treat any Slippi instance_id change as a motion-state transition.
                move_id = _move_id_from_char_msid(int(char_id[t, p]), int(msid_u32[t, p]))
                # Decomp: ft_800890D0 increments x206C when move_id==1 OR move_id != current attackID.
                if move_id == _FT_MOVE_ID_DEFAULT or move_id != int(cur_attack_id[p]):
                    cur_attack_id[p] = np.uint16(move_id)
                    cur_attack_inst[p] = np.uint16(_inc_attack_instance())

                prev_state_iid[p] = np.uint16(iid)
                by_state_iid[p].setdefault(iid, (int(cur_attack_id[p]), int(cur_attack_inst[p])))

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
                    # Decomp-pure behavior: if attacker port is unknown/unmapped, do not enqueue.
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
