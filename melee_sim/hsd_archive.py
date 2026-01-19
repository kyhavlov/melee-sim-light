from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable


def _u32_be(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "big", signed=False)


def _read_cstr(buf: bytes, off: int) -> str:
    if off < 0 or off >= len(buf):
        return ""
    end = buf.find(b"\x00", off)
    if end < 0:
        end = len(buf)
    return buf[off:end].decode("ascii", errors="replace")


@dataclass(frozen=True)
class HsdArchiveHeader:
    file_size: int
    data_size: int
    nb_reloc: int
    nb_public: int
    nb_extern: int
    version: bytes


@dataclass(frozen=True)
class HsdArchive:
    buf: bytes
    base: int
    header: HsdArchiveHeader
    data_off: int
    reloc_off: int
    public_off: int
    extern_off: int
    symbols_off: int

    @property
    def data_base(self) -> int:
        return self.base + self.data_off

    def public_symbols(self) -> list[str]:
        out: list[str] = []
        for i in range(self.header.nb_public):
            entry = self.public_off + i * 8
            sym = _u32_be(self.buf, self.base + entry + 4)
            out.append(_read_cstr(self.buf, self.base + self.symbols_off + sym))
        return out

    def get_public_offset(self, name: str) -> int | None:
        for i in range(self.header.nb_public):
            entry = self.public_off + i * 8
            off = _u32_be(self.buf, self.base + entry + 0)
            sym = _u32_be(self.buf, self.base + entry + 4)
            sym_name = _read_cstr(self.buf, self.base + self.symbols_off + sym)
            if sym_name == name:
                return self.data_base + off
        return None

    def slice_public(self, name: str, size: int) -> bytes | None:
        off = self.get_public_offset(name)
        if off is None:
            return None
        rel = off - self.base
        if rel < 0 or rel + size > self.header.file_size:
            raise ValueError(f"public symbol {name!r} out of bounds")
        return self.buf[off : off + size]

    def parse_nested_public_archive(self, name: str) -> "HsdArchive" | None:
        off = self.get_public_offset(name)
        if off is None:
            return None
        return parse_hsd_archive(self.buf, base=off)

    def ptr32(self, abs_off: int) -> int:
        """Interpret a u32 at abs_off as an offset into this archive's data section."""
        v = _u32_be(self.buf, abs_off)
        return self.data_base + v


def parse_hsd_archive(buf: bytes, *, base: int = 0) -> HsdArchive:
    if base < 0 or base + 0x20 > len(buf):
        raise ValueError("archive header out of bounds")

    file_size = _u32_be(buf, base + 0x00)
    data_size = _u32_be(buf, base + 0x04)
    nb_reloc = _u32_be(buf, base + 0x08)
    nb_public = _u32_be(buf, base + 0x0C)
    nb_extern = _u32_be(buf, base + 0x10)
    version = bytes(buf[base + 0x14 : base + 0x18])

    if file_size <= 0 or base + file_size > len(buf):
        raise ValueError("archive file_size out of bounds")

    data_off = 0x20
    off = data_off
    if data_size != 0:
        off = data_off + data_size

    reloc_off = off
    off += nb_reloc * 4
    public_off = off
    off += nb_public * 8
    extern_off = off
    off += nb_extern * 8
    symbols_off = off
    if base + symbols_off > base + file_size:
        raise ValueError("archive symbols out of bounds")

    hdr = HsdArchiveHeader(
        file_size=file_size,
        data_size=data_size,
        nb_reloc=nb_reloc,
        nb_public=nb_public,
        nb_extern=nb_extern,
        version=version,
    )
    return HsdArchive(
        buf=buf,
        base=base,
        header=hdr,
        data_off=data_off,
        reloc_off=reloc_off,
        public_off=public_off,
        extern_off=extern_off,
        symbols_off=symbols_off,
    )


def iter_archives_via_public(archive: HsdArchive, names: Iterable[str]) -> list[HsdArchive]:
    out: list[HsdArchive] = []
    for n in names:
        sub = archive.parse_nested_public_archive(n)
        if sub is not None:
            out.append(sub)
    return out

