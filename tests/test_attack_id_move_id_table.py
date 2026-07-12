from __future__ import annotations

import struct
from pathlib import Path

import pytest

from tools.slippi.action_state_tables import VERSION, read_mslacid1_v3


@pytest.mark.integration
def test_attack_id_move_id_tables_exist_and_cover_fox_falco_specialn() -> None:
    fox_path = Path("data/attack_id/move_id/fox.bin")
    falco_path = Path("data/attack_id/move_id/falco.bin")
    assert fox_path.exists() and falco_path.exists(), (
        "missing MSLACID1 attack_id/move_id tables; generate them with:\n"
        "  make bootstrap ISO=/path/to/SSBM.iso"
    )

    # Fox/Falco SpecialNStart action id is ftFx_MS_SpecialNStart = 0x0155 (341).
    # refs/melee/src/melee/ft/chara/ftFox/forward.h (ftFox_MotionState enum)
    specialn_start = 0x0155

    fox = read_mslacid1_v3(fox_path)
    falco = read_mslacid1_v3(falco_path)

    assert fox.move_id.shape[0] > specialn_start
    assert falco.move_id.shape[0] > specialn_start

    # move_id should be a real FtMoveId (not 0xFFFF sentinel, not FtMoveId_Default).
    assert int(fox.move_id[specialn_start]) not in (0xFFFF, 1)
    assert int(falco.move_id[specialn_start]) not in (0xFFFF, 1)


@pytest.mark.integration
def test_attack_id_binary_exports_motionstate_x9_b1() -> None:
    fox = read_mslacid1_v3(Path("data/attack_id/move_id/fox.bin"))

    # Fighter_ChangeMotionState seeds dmg.x18C8 from MotionState.x9_b1.
    # GuardOn carries `(1 << 22) | (1 << 23)` in the raw +0x8 word, while Guard carries
    # only `(1 << 23)`. This locks the x9_b1 bit mapping used by validation preprocessing.
    # refs/melee/src/melee/ft/ftmotionstates.c::{ftCo_MS_GuardOn,ftCo_MS_Guard}
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    assert int(fox.x9_b1[178]) == 1
    assert int(fox.x9_b1[179]) == 0


def test_attack_id_binary_reader_rejects_stale_versions(tmp_path: Path) -> None:
    stale = tmp_path / "fox.bin"
    buf = bytearray(32)
    buf[0:8] = b"MSLACID1"
    struct.pack_into("<I", buf, 8, VERSION - 1)
    struct.pack_into("<H", buf, 12, 1)
    struct.pack_into("<I", buf, 28, len(buf))
    stale.write_bytes(bytes(buf))

    with pytest.raises(ValueError, match="unsupported MSLACID1 version"):
        read_mslacid1_v3(stale)
