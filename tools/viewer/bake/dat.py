"""Minimal HSD archive (DAT) reader."""
from __future__ import annotations

import struct
from dataclasses import dataclass


@dataclass
class Dat:
    data: bytes
    roots: dict[str, int]
    relocs: set[int]

    def u8(self, off: int) -> int:
        return self.data[off]

    def u16(self, off: int) -> int:
        return struct.unpack_from('>H', self.data, off)[0]

    def s16(self, off: int) -> int:
        return struct.unpack_from('>h', self.data, off)[0]

    def u32(self, off: int) -> int:
        return struct.unpack_from('>I', self.data, off)[0]

    def s32(self, off: int) -> int:
        return struct.unpack_from('>i', self.data, off)[0]

    def f32(self, off: int) -> float:
        return struct.unpack_from('>f', self.data, off)[0]

    def vec3(self, off: int) -> tuple[float, float, float]:
        return struct.unpack_from('>fff', self.data, off)

    def ptr(self, off: int) -> int:
        """Pointer field: 0 when unrelocated (NULL)."""
        if off not in self.relocs:
            return 0
        return self.u32(off)


def load_dat(blob: bytes) -> Dat:
    _, dsize, nrel, nroot, nxref = struct.unpack_from('>IIIII', blob, 0)
    data = blob[0x20:0x20 + dsize]
    reloc_off = 0x20 + dsize
    relocs = set(struct.unpack_from('>%dI' % nrel, blob, reloc_off))
    roots_off = reloc_off + nrel * 4
    strtab = roots_off + (nroot + nxref) * 8
    roots: dict[str, int] = {}
    for i in range(nroot + nxref):
        doff, soff = struct.unpack_from('>II', blob, roots_off + i * 8)
        end = blob.index(b'\0', strtab + soff)
        roots[blob[strtab + soff:end].decode()] = doff
    return Dat(data, roots, relocs)


def load_dat_file(path) -> Dat:
    with open(path, 'rb') as handle:
        return load_dat(handle.read())
