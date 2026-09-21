from __future__ import annotations

import argparse
import hashlib
import json
import os
from dataclasses import dataclass
from pathlib import Path

from .raw import (
    MANIFEST_MAGIC,
    MANIFEST_NAME,
    MANIFEST_VERSION,
    SUPPORTED_CHARACTERS,
    SUPPORTED_STAGES,
    data_root,
    sha256_file,
    validate_raw_dir,
)


@dataclass(frozen=True)
class DiscFile:
    path: str
    offset: int
    size: int


PLAYER_PREFIXES = (
    "PlFx",
    "PlFc",
    "PlMs",
    "PlFe",
    "PlCa",
    "PlSk",
    "PlZd",
    "PlPr",
    "PlPe",
    "PlLg",
    "PlMr",
    "PlDr",
    "PlSs",
    "PlPp",
    "PlNn",
    "PlPk",
    "PlDk",
    "PlGn",
    "PlYs",
    "PlKp",
    "PlMt",
    "PlGw",
    "PlNs",
    "PlLk",
    "PlCl",
)
REQUIRED_FILES = {
    "PlCo.dat",
    "PdPm.dat",
    "ItCo.dat",
    "ItCo.usd",
    "EfCoData.dat",
    "EfFxData.dat",
    "EfCaData.dat",
    "EfMsData.dat",
    "EfFeData.dat",
    "EfPeData.dat",
    "EfPrData.dat",
    "EfZdData.dat",
    "EfLgData.dat",
    "EfMrData.dat",
    "EfSsData.dat",
    "EfIcData.dat",
    "EfPkData.dat",
    "EfDkData.dat",
    "EfGnData.dat",
    "EfYsData.dat",
    "EfKpData.dat",
    "EfMtData.dat",
    "EfNsData.dat",
    "EfLkData.dat",
    "GrNLa.dat",
    "GrNBa.dat",
    "GrIz.dat",
    "GrPs.dat",
    "GrSt.dat",
    "GrOp.dat",
}


def _be32(buffer: bytes, offset: int) -> int:
    return int.from_bytes(buffer[offset : offset + 4], "big")


def _read_at(source, offset: int, size: int) -> bytes:
    source.seek(offset)
    value = source.read(size)
    if len(value) != size:
        raise ValueError(f"disc image is truncated at 0x{offset:x}")
    return value


def _disc_files(source) -> list[DiscFile]:
    header = _read_at(source, 0x424, 8)
    fst_offset = _be32(header, 0)
    fst_size = _be32(header, 4)
    fst = _read_at(source, fst_offset, fst_size)
    if len(fst) < 12 or fst[0] == 0:
        raise ValueError("invalid disc filesystem table")
    count = _be32(fst, 8)
    table_size = count * 12
    if count == 0 or table_size > len(fst):
        raise ValueError("truncated disc filesystem table")
    strings = fst[table_size:]

    def entry_name(index: int) -> str:
        offset = _be32(fst, index * 12) & 0xFFFFFF
        end = strings.find(b"\0", offset)
        if offset >= len(strings) or end < 0:
            raise ValueError("invalid disc filesystem name")
        return strings[offset:end].decode("ascii")

    files: list[DiscFile] = []
    directories: list[tuple[str, int]] = [("", count)]
    for index in range(1, count):
        while directories and index >= directories[-1][1]:
            directories.pop()
        if not directories:
            raise ValueError("invalid disc directory traversal")
        word = _be32(fst, index * 12)
        name = entry_name(index)
        parent = directories[-1][0]
        path = f"{parent}/{name}" if parent else name
        first = _be32(fst, index * 12 + 4)
        second = _be32(fst, index * 12 + 8)
        if word >> 24:
            if second <= index or second > count:
                raise ValueError("invalid disc directory boundary")
            directories.append((path, second))
        else:
            files.append(DiscFile(path, first, second))
    return files


def _main_dol(source) -> DiscFile:
    offset = _be32(_read_at(source, 0x420, 4), 0)
    header = _read_at(source, offset, 0xD8)
    file_offsets = [_be32(header, index * 4) for index in range(18)]
    sizes = [_be32(header, 0x90 + index * 4) for index in range(18)]
    size = max((start + length for start, length in zip(file_offsets, sizes)), default=0)
    if size < len(header):
        raise ValueError("invalid main.dol section table")
    return DiscFile("sys/main.dol", offset, size)


def _profile(files: list[DiscFile]) -> dict[str, DiscFile]:
    selected: dict[str, DiscFile] = {}
    for entry in files:
        name = Path(entry.path).name
        wanted_player = name.endswith(".dat") and name.startswith(PLAYER_PREFIXES)
        if name not in REQUIRED_FILES and not wanted_player:
            continue
        if name in selected:
            raise ValueError(f"duplicate required disc basename: {name}")
        selected[name] = entry
    missing = sorted(REQUIRED_FILES - selected.keys())
    for prefix in PLAYER_PREFIXES:
        if not any(name.startswith(prefix) for name in selected):
            missing.append(f"{prefix}*.dat")
    if missing:
        raise ValueError("disc is missing required files: " + ", ".join(missing))
    return selected


def _copy_entry(source, entry: DiscFile, destination: Path) -> str:
    temporary = destination.with_name(f".{destination.name}.tmp")
    digest = hashlib.sha256()
    source.seek(entry.offset)
    remaining = entry.size
    try:
        with temporary.open("wb") as output:
            while remaining:
                chunk = source.read(min(1024 * 1024, remaining))
                if not chunk:
                    raise ValueError(f"disc file is truncated: {entry.path}")
                output.write(chunk)
                digest.update(chunk)
                remaining -= len(chunk)
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    return digest.hexdigest()


def extract(iso: Path, output: Path) -> None:
    iso = iso.expanduser().resolve()
    output = output.expanduser().resolve()
    raw = output / "raw"
    with iso.open("rb") as source:
        identity = _read_at(source, 0, 8)
        if identity[:6] != b"GALE01" or identity[7] != 2:
            game_id = identity[:6].decode("ascii", errors="replace")
            raise ValueError(f"expected GALE01 revision 2, got {game_id} revision {identity[7]}")
        files = _profile(_disc_files(source))
        files["main.dol"] = _main_dol(source)
        raw.mkdir(parents=True, exist_ok=True)
        (raw / MANIFEST_NAME).unlink(missing_ok=True)
        records: list[dict[str, object]] = []
        for name, entry in sorted(files.items(), key=lambda item: item[1].offset):
            digest = _copy_entry(source, entry, raw / name)
            records.append(
                {
                    "name": name,
                    "iso_path": entry.path,
                    "iso_offset": entry.offset,
                    "size": entry.size,
                    "sha256": digest,
                }
            )
    wanted = set(files) | {MANIFEST_NAME}
    for child in raw.iterdir():
        if child.is_file() and child.name not in wanted:
            child.unlink()
    records.sort(key=lambda record: str(record["name"]))
    payload = {
        "magic": MANIFEST_MAGIC,
        "version": MANIFEST_VERSION,
        "profile": "rl_1_0",
        "characters": list(SUPPORTED_CHARACTERS),
        "stages": list(SUPPORTED_STAGES),
        "disc": {
            "game_id": "GALE01",
            "revision": 2,
            "size": iso.stat().st_size,
            "sha256": sha256_file(iso),
        },
        "files": records,
    }
    temporary = raw / f".{MANIFEST_NAME}.tmp"
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, raw / MANIFEST_NAME)
    validate_raw_dir(raw, verify_hashes=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract the simulator's GALE01 data profile.")
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--out-dir", type=Path, default=None)
    arguments = parser.parse_args()
    output = data_root(arguments.out_dir)
    extract(arguments.iso, output)
    print(f"extracted gameplay data to {output / 'raw'}")


if __name__ == "__main__":
    main()
