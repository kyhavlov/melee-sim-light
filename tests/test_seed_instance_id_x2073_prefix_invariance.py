from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tools.slippi.seed_history import derive_instance_id_x2073


# Action ids (GALE01): refs/melee/src/melee/ft/chara/ftCommon/forward.h
ACT_WAIT = 0x000E

# Fox/Falco down special (SpecialLw / Shine) start (GALE01): 360.
# refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c (commented numeric ids)
ACT_FX_SPECIAL_LW_START = 0x0168

CHAR_FOX = 1


def _write_mslacid1_v3(*, path, action_count: int, x4_flags_low_by_action: dict[int, int]) -> None:
    """
    Write a minimal MSLACID1 v3 file that satisfies tools.slippi.seed_history's reader.

    The file stores u32 x4_flags per action id, and the derivation uses only the low byte.
    """
    if action_count <= 0:
        raise ValueError("action_count must be positive")
    header_bytes = 32
    move_off = header_bytes
    flags_off = move_off + action_count * 2
    motion_word_off = flags_off + action_count * 4
    buf = bytearray(motion_word_off + action_count * 4)
    buf[0:8] = b"MSLACID1"
    struct.pack_into("<I", buf, 8, 3)  # version
    struct.pack_into("<H", buf, 12, int(action_count))
    struct.pack_into("<I", buf, 16, int(move_off))
    struct.pack_into("<I", buf, 20, int(flags_off))
    struct.pack_into("<I", buf, 24, int(motion_word_off))
    struct.pack_into("<I", buf, 28, len(buf))
    for a in range(action_count):
        struct.pack_into("<H", buf, move_off + a * 2, 1)
    for a, low in x4_flags_low_by_action.items():
        if a < 0 or a >= action_count:
            raise ValueError(f"action_id {a} out of range for action_count={action_count}")
        struct.pack_into("<I", buf, flags_off + a * 4, int(low) & 0xFF)
    path.write_bytes(bytes(buf))


def test_seed_instance_id_x2073_derivation_is_prefix_invariant(tmp_path: Path) -> None:
    """
    Regression test for the "strictly causal" fp+0x2073 (instance_id compare byte) derivation.

    The derived x2073 for a prefix of frames must not depend on any future frames.
    """
    # Use a synthetic (tiny) MSLACID1 table so this unit test does not depend on local gitignored
    # `data/attack_id/move_id/*.bin` artifacts.
    base = tmp_path / "attack_id" / "move_id"
    base.mkdir(parents=True, exist_ok=True)
    count = ACT_FX_SPECIAL_LW_START + 1  # must cover max action id used below
    mapping = {
        ACT_WAIT: ACT_WAIT & 0xFF,
        ACT_FX_SPECIAL_LW_START: ACT_FX_SPECIAL_LW_START & 0xFF,
    }
    _write_mslacid1_v3(path=base / "fox.bin", action_count=count, x4_flags_low_by_action=mapping)
    _write_mslacid1_v3(path=base / "falco.bin", action_count=count, x4_flags_low_by_action=mapping)

    # Build a sequence with multiple action transitions.
    action_id = np.array(
        [
            *([ACT_WAIT] * 3),
            ACT_FX_SPECIAL_LW_START,
            *([ACT_FX_SPECIAL_LW_START] * 4),
            ACT_WAIT,
            *([ACT_WAIT] * 2),
            ACT_FX_SPECIAL_LW_START,
            *([ACT_FX_SPECIAL_LW_START] * 2),
        ],
        dtype=np.uint16,
    )

    # Use a monotonic action_frame ramp (we only use action_id transitions here).
    action_frame = np.arange(action_id.size, dtype=np.int16)
    char_id = np.full(action_id.shape[0], np.uint8(CHAR_FOX), dtype=np.uint8)

    full = derive_instance_id_x2073(
        char_id_u8=char_id,
        action_id_u16=action_id,
        action_frame_i16=action_frame,
        data_dir=str(tmp_path),
    )

    for k in (1, 2, 3, 4, 7, 10, int(action_id.size)):
        got = derive_instance_id_x2073(
            char_id_u8=char_id[:k],
            action_id_u16=action_id[:k],
            action_frame_i16=action_frame[:k],
            data_dir=str(tmp_path),
        )
        assert np.array_equal(got, full[:k])

    # Sanity: on motion-state entry frames (action_id transitions), x2073 becomes our table's low byte.
    for i in range(int(action_id.size)):
        if i == 0 or int(action_id[i]) != int(action_id[i - 1]):
            assert int(full[i]) == (int(action_id[i]) & 0xFF)
