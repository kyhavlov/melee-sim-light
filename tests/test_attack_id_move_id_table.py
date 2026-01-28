from __future__ import annotations

import struct
from pathlib import Path

import pytest


def _read_u16_table(path: Path) -> list[int]:
    buf = path.read_bytes()
    assert buf[:8] == b"MSLACID1"
    (ver,) = struct.unpack_from("<I", buf, 8)
    (count,) = struct.unpack_from("<H", buf, 12)
    if ver == 1:
        (toc_off,) = struct.unpack_from("<I", buf, 16)
        (file_bytes,) = struct.unpack_from("<I", buf, 20)
    elif ver == 2:
        (toc_off,) = struct.unpack_from("<I", buf, 16)
        (_flags_off,) = struct.unpack_from("<I", buf, 20)
        (file_bytes,) = struct.unpack_from("<I", buf, 24)
    else:
        raise AssertionError(f"unsupported MSLACID1 version {ver} in {path}")
    assert file_bytes == len(buf)
    assert toc_off + count * 2 <= len(buf)
    out: list[int] = []
    off = toc_off
    for _ in range(int(count)):
        (mv,) = struct.unpack_from("<H", buf, off)
        off += 2
        out.append(int(mv))
    return out


@pytest.mark.integration
def test_attack_id_move_id_tables_exist_and_cover_fox_falco_specialn() -> None:
    fox_path = Path("data/attack_id/move_id/fox.bin")
    falco_path = Path("data/attack_id/move_id/falco.bin")
    assert fox_path.exists() and falco_path.exists(), (
        "missing MSLACID1 attack_id/move_id tables; generate them with:\n"
        "  uv run python -m tools.extraction.extract_attack_id_move_id "
        "--melee_decomp refs/melee --out_dir data/attack_id/move_id --chars fox,falco"
    )

    # Fox/Falco SpecialNStart action id is ftFx_MS_SpecialNStart = 0x0155 (341).
    # refs/melee/src/melee/ft/chara/ftFox/forward.h (ftFox_MotionState enum)
    specialn_start = 0x0155

    fox = _read_u16_table(fox_path)
    falco = _read_u16_table(falco_path)

    assert len(fox) > specialn_start
    assert len(falco) > specialn_start

    # move_id should be a real FtMoveId (not 0xFFFF sentinel, not FtMoveId_Default).
    assert fox[specialn_start] not in (0xFFFF, 1)
    assert falco[specialn_start] not in (0xFFFF, 1)
