"""Compress ZIP members in workers and assemble them without recompression."""

import time
import zlib
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


def deflate_member(name: str, data: bytes, level: int) -> tuple[ZipInfo, bytes]:
    compressor = zlib.compressobj(level, wbits=-zlib.MAX_WBITS)
    compressed = compressor.compress(data) + compressor.flush()
    info = ZipInfo(name, time.localtime()[:6])
    info.compress_type = ZIP_DEFLATED
    info.file_size = len(data)
    info.compress_size = len(compressed)
    info.CRC = zlib.crc32(data)
    info.external_attr = 0o600 << 16
    return info, compressed


class DeflateZipFile(ZipFile):
    def write_deflated(self, info: ZipInfo, compressed: bytes) -> None:
        # ZipFile writes the central directory, including ZIP64 offsets, from
        # this index. FileHeader encodes the already known sizes and CRC.
        info.header_offset = self.start_dir
        self._writecheck(info)
        self.fp.seek(info.header_offset)
        self.fp.write(info.FileHeader())
        self.fp.write(compressed)
        self.start_dir = self.fp.tell()
        self.filelist.append(info)
        self.NameToInfo[info.filename] = info
