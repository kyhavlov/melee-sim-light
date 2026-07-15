from __future__ import annotations

import hashlib
import os
from dataclasses import dataclass
from fnmatch import fnmatch
from pathlib import Path
from typing import BinaryIO, Iterable, Mapping


@dataclass(frozen=True)
class IsoFile:
    path: str
    offset: int
    size: int


@dataclass(frozen=True)
class DiscIdentity:
    game_id: str
    revision: int


def _u32_be(b: bytes, off: int) -> int:
    return int.from_bytes(b[off : off + 4], "big", signed=False)


def _read_exact(f: BinaryIO, n: int) -> bytes:
    out = f.read(n)
    if len(out) != n:
        raise EOFError(f"expected {n} bytes, got {len(out)}")
    return out


def _parse_fst(fst: bytes) -> list[IsoFile]:
    if len(fst) < 12:
        raise ValueError("FST too small")

    root_type_name = _u32_be(fst, 0)
    root_is_dir = (root_type_name & 0xFF00_0000) != 0
    if not root_is_dir:
        raise ValueError("FST root is not a directory")
    entry_count = _u32_be(fst, 8)
    if entry_count <= 0:
        return []

    table_bytes = entry_count * 12
    if len(fst) < table_bytes:
        raise ValueError("FST truncated entry table")

    strings = fst[table_bytes:]

    def name_at(off: int) -> str:
        if off < 0 or off >= len(strings):
            return ""
        end = strings.find(b"\x00", off)
        if end < 0:
            end = len(strings)
        return strings[off:end].decode("ascii", errors="replace")

    out: list[IsoFile] = []

    # Directory entries form a pre-order traversal; a dir's "size" is the next index after its subtree.
    stack: list[tuple[str, int]] = [("", entry_count)]
    i = 1
    while i < entry_count:
        while stack and i >= stack[-1][1]:
            stack.pop()
        if not stack:
            break

        type_name = _u32_be(fst, i * 12 + 0)
        is_dir = (type_name & 0xFF00_0000) != 0
        name_off = type_name & 0x00FF_FFFF
        name = name_at(name_off)
        parent_prefix, _end = stack[-1]
        full = f"{parent_prefix}/{name}" if parent_prefix else name

        offset = _u32_be(fst, i * 12 + 4)
        size = _u32_be(fst, i * 12 + 8)

        if is_dir:
            stack.append((full, size))
            i += 1
            continue

        out.append(IsoFile(path=full, offset=offset, size=size))
        i += 1

    return out


def list_files(iso_path: Path) -> list[IsoFile]:
    with iso_path.open("rb") as f:
        f.seek(0x424)
        hdr = _read_exact(f, 12)
        fst_offset = _u32_be(hdr, 0)
        fst_size = _u32_be(hdr, 4)
        if fst_offset <= 0 or fst_size <= 0:
            raise ValueError("invalid FST header")
        f.seek(fst_offset)
        fst = _read_exact(f, fst_size)
    return _parse_fst(fst)


def read_disc_identity(iso_path: Path) -> DiscIdentity:
    with iso_path.open("rb") as f:
        header = _read_exact(f, 8)
    return DiscIdentity(
        game_id=header[:6].decode("ascii", errors="replace"),
        revision=header[7],
    )


def find_files(files: Iterable[IsoFile], pattern: str) -> list[IsoFile]:
    return [x for x in files if fnmatch(x.path, pattern)]


def read_file_bytes(iso_path: Path, entry: IsoFile) -> bytes:
    if entry.offset < 0 or entry.size < 0:
        raise ValueError("invalid file entry")
    with iso_path.open("rb") as f:
        f.seek(entry.offset)
        return _read_exact(f, entry.size)


def extract_file(iso_path: Path, entry: IsoFile, out_path: Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(read_file_bytes(iso_path, entry))


def extract_files(
    iso_path: Path, entries: Mapping[str, IsoFile], out_dir: Path
) -> dict[str, str]:
    """Extract an ISO file set with one source handle and atomic destinations."""
    out_dir.mkdir(parents=True, exist_ok=True)
    digests: dict[str, str] = {}
    with iso_path.open("rb") as source:
        for name, entry in sorted(entries.items(), key=lambda item: item[1].offset):
            if Path(name).name != name:
                raise ValueError(f"ISO extraction destination must be a basename: {name!r}")
            if entry.offset < 0 or entry.size < 0:
                raise ValueError(f"invalid ISO file entry: {entry.path}")
            destination = out_dir / name
            temporary = destination.with_name(f".{destination.name}.tmp")
            digest = hashlib.sha256()
            source.seek(entry.offset)
            remaining = entry.size
            try:
                with temporary.open("wb") as output:
                    while remaining:
                        chunk = source.read(min(1024 * 1024, remaining))
                        if not chunk:
                            raise EOFError(
                                f"ISO file {entry.path!r} is truncated by {remaining} bytes"
                            )
                        output.write(chunk)
                        digest.update(chunk)
                        remaining -= len(chunk)
                os.replace(temporary, destination)
            except BaseException:
                temporary.unlink(missing_ok=True)
                raise
            digests[name] = digest.hexdigest()
    return digests


def extract_main_dol(iso_path: Path, out_path: Path) -> None:
    """Extract the executable, which is addressed separately from the ISO FST."""
    with iso_path.open("rb") as f:
        disc_header = _read_exact(f, 8)
        if disc_header[:6] != b"GALE01" or disc_header[7] != 2:
            game_id = disc_header[:6].decode("ascii", errors="replace")
            raise ValueError(
                "MotionState extraction requires an NTSC-U SSBM 1.02 ISO "
                f"(GALE01 revision 2); got {game_id!r} revision {disc_header[7]}"
            )
        f.seek(0x420)
        dol_offset = _u32_be(_read_exact(f, 4), 0)
        if dol_offset <= 0:
            raise ValueError("invalid main.dol offset")
        f.seek(dol_offset)
        header = _read_exact(f, 0xD8)
        file_offsets = tuple(_u32_be(header, i * 4) for i in range(18))
        sizes = tuple(_u32_be(header, 0x90 + i * 4) for i in range(18))
        dol_size = max(
            (offset + size for offset, size in zip(file_offsets, sizes)), default=0
        )
        if dol_size < len(header):
            raise ValueError("invalid main.dol section table")
        f.seek(dol_offset)
        data = _read_exact(f, dol_size)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = out_path.with_name(f".{out_path.name}.tmp")
    try:
        temporary.write_bytes(data)
        os.replace(temporary, out_path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
