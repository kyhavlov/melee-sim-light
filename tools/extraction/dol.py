from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class _Section:
    file_offset: int
    address: int
    size: int


class DolImage:
    """Minimal reader for the GALE01 MotionState tables in main.dol."""

    def __init__(self, path: Path):
        self.path = path
        self._data = path.read_bytes()
        if len(self._data) < 0xD8:
            raise ValueError(f"DOL header is truncated: {path}")
        file_offsets = struct.unpack_from(">18I", self._data, 0)
        addresses = struct.unpack_from(">18I", self._data, 0x48)
        sizes = struct.unpack_from(">18I", self._data, 0x90)
        sections = tuple(
            _Section(file_offset, address, size)
            for file_offset, address, size in zip(file_offsets, addresses, sizes)
        )
        self._sections = tuple(section for section in sections if section.size != 0)
        self._text_sections = tuple(
            section for section in sections[:7] if section.size != 0
        )
        for section in self._sections:
            if section.file_offset < 0xD8 or section.file_offset + section.size > len(
                self._data
            ):
                raise ValueError(
                    f"DOL section is truncated: {path} "
                    f"(offset={section.file_offset:#x} size={section.size:#x})"
                )

    def read(self, address: int, size: int) -> bytes:
        for section in self._sections:
            relative = address - section.address
            if 0 <= relative and size <= section.size - relative:
                start = section.file_offset + relative
                return self._data[start : start + size]
        raise ValueError(
            f"DOL address range is not mapped: {address:#010x}..{address + size:#010x}"
        )

    def u32(self, address: int) -> int:
        return struct.unpack(">I", self.read(address, 4))[0]

    def is_text_address(self, address: int) -> bool:
        return any(
            section.address <= address < section.address + section.size
            for section in self._text_sections
        )
