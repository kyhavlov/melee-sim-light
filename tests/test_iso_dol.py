from __future__ import annotations

import struct
from pathlib import Path

import pytest

from melee_sim.iso import extract_main_dol
from tools.extraction.dol import DolImage


def _dol(*, section_size: int = 8, payload: bytes = b"abcdefgh") -> bytes:
    data = bytearray(0x100 + len(payload))
    struct.pack_into(">I", data, 0x00, 0x100)
    struct.pack_into(">I", data, 0x48, 0x80001000)
    struct.pack_into(">I", data, 0x90, section_size)
    data[0x100:] = payload
    return bytes(data)


def _iso(dol: bytes, *, game_id: bytes = b"GALE01", revision: int = 2) -> bytes:
    dol_offset = 0x500
    data = bytearray(dol_offset + len(dol))
    data[:6] = game_id
    data[7] = revision
    struct.pack_into(">I", data, 0x420, dol_offset)
    data[dol_offset:] = dol
    return bytes(data)


def test_extract_main_dol_accepts_gale01_revision_2_and_copies_exact_bytes(
    tmp_path: Path,
) -> None:
    expected = _dol()
    iso = tmp_path / "GALE01.iso"
    out = tmp_path / "main.dol"
    iso.write_bytes(_iso(expected))

    extract_main_dol(iso, out)

    assert out.read_bytes() == expected


@pytest.mark.parametrize(
    ("game_id", "revision"),
    ((b"GALE01", 0), (b"GALE01", 1), (b"GALP01", 2), (b"GALE00", 2)),
)
def test_extract_main_dol_rejects_non_gale01_revision_2(
    tmp_path: Path, game_id: bytes, revision: int
) -> None:
    iso = tmp_path / "wrong.iso"
    iso.write_bytes(_iso(_dol(), game_id=game_id, revision=revision))

    with pytest.raises(ValueError, match="GALE01 revision 2"):
        extract_main_dol(iso, tmp_path / "main.dol")


def test_dol_image_maps_virtual_addresses_within_a_section(tmp_path: Path) -> None:
    path = tmp_path / "main.dol"
    path.write_bytes(_dol())
    image = DolImage(path)

    assert image.read(0x80001002, 4) == b"cdef"
    assert image.u32(0x80001000) == 0x61626364
    assert image.is_text_address(0x80001007)


def test_dol_image_rejects_unmapped_and_truncated_ranges(tmp_path: Path) -> None:
    path = tmp_path / "main.dol"
    path.write_bytes(_dol())
    image = DolImage(path)
    with pytest.raises(ValueError, match="not mapped"):
        image.read(0x80002000, 1)
    with pytest.raises(ValueError, match="not mapped"):
        image.read(0x80001007, 2)

    truncated = tmp_path / "truncated.dol"
    truncated.write_bytes(_dol(section_size=9))
    with pytest.raises(ValueError, match="section is truncated"):
        DolImage(truncated)
