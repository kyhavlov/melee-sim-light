from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
from typing import Iterable


MANIFEST_NAME = "manifest.json"
MANIFEST_MAGIC = "MSLRAW1"
MANIFEST_VERSION = 1
SUPPORTED_CHARACTERS = (
    "fox",
    "falco",
    "marth",
    "falcon",
    "sheik",
    "zelda",
    "puff",
    "peach",
    "luigi",
    "mario",
    "drmario",
)
SUPPORTED_STAGES = ("grnla", "grnba", "griz", "grps", "grst", "grop")


class DataError(RuntimeError):
    pass


def data_root(path: str | os.PathLike[str] | None = None) -> Path:
    selected = path if path is not None else os.environ.get("MSL_DATA_DIR", "data")
    return Path(selected).expanduser().resolve()


def raw_dir(path: str | os.PathLike[str] | None = None) -> Path:
    return data_root(path) / "raw"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_manifest(directory: Path) -> dict[str, object]:
    path = directory / MANIFEST_NAME
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise DataError(
            f"missing extracted game data: {path}\n"
            "Run `python -m tools.data.extract --iso /path/to/SSBM.iso`."
        ) from exc
    except (OSError, json.JSONDecodeError) as exc:
        raise DataError(f"invalid game-data manifest {path}: {exc}") from exc
    if not isinstance(payload, dict):
        raise DataError(f"invalid game-data manifest root: {path}")
    return payload


def validate_raw_dir(
    directory: Path,
    *,
    required: Iterable[str] = (),
    verify_hashes: bool = True,
) -> dict[str, object]:
    payload = load_manifest(directory)
    disc = payload.get("disc")
    if (
        payload.get("magic") != MANIFEST_MAGIC
        or payload.get("version") != MANIFEST_VERSION
        or payload.get("profile") != "rl_1_0"
        or payload.get("characters") != list(SUPPORTED_CHARACTERS)
        or payload.get("stages") != list(SUPPORTED_STAGES)
        or not isinstance(disc, dict)
        or disc.get("game_id") != "GALE01"
        or disc.get("revision") != 2
    ):
        raise DataError(f"unsupported game-data profile: {directory / MANIFEST_NAME}")

    records = payload.get("files")
    if not isinstance(records, list) or not records:
        raise DataError(f"empty game-data manifest: {directory / MANIFEST_NAME}")
    indexed: dict[str, dict[str, object]] = {}
    for record in records:
        if not isinstance(record, dict):
            raise DataError("malformed game-data file record")
        name = record.get("name")
        size = record.get("size")
        digest = record.get("sha256")
        if (
            not isinstance(name, str)
            or Path(name).name != name
            or not isinstance(size, int)
            or size < 0
            or not isinstance(digest, str)
            or len(digest) != 64
            or name in indexed
        ):
            raise DataError(f"malformed game-data file record: {record!r}")
        indexed[name] = record

    missing = sorted(set(required) - indexed.keys())
    if missing:
        raise DataError("missing required game-data files: " + ", ".join(missing))
    for name, record in indexed.items():
        path = directory / name
        try:
            size = path.stat().st_size
        except FileNotFoundError as exc:
            raise DataError(f"missing extracted file: {path}") from exc
        if size != record["size"]:
            raise DataError(f"wrong extracted file size: {path}")
        if verify_hashes and sha256_file(path) != record["sha256"]:
            raise DataError(f"wrong extracted file digest: {path}")
    return payload
