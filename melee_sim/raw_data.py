from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
from typing import Any, Iterable


RAW_DATA_DIR = "raw"
RAW_MANIFEST = "manifest.json"
RAW_MANIFEST_MAGIC = "MSLRAW1"
RAW_MANIFEST_VERSION = 1
RL_1_0_CHARS = ("fox", "falco", "marth", "falcon", "sheik", "zelda", "puff")
RL_1_0_STAGES = ("grnla", "grnba", "griz", "grps", "grst", "grop")


class RawDataError(RuntimeError):
    pass


def resolve_data_root(
    data_dir: str | os.PathLike[str] | None = None,
    *,
    default: str | os.PathLike[str] = "data",
) -> Path:
    selected = data_dir
    if selected is None:
        selected = os.environ.get("MSL_DATA_DIR") or default
    return Path(selected).expanduser().resolve()


def raw_data_dir(
    data_dir: str | os.PathLike[str] | None = None,
    *,
    default: str | os.PathLike[str] = "data",
) -> Path:
    return resolve_data_root(data_dir, default=default) / RAW_DATA_DIR


def sha256_path(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def raw_manifest_digest(raw_dir: Path) -> str:
    return sha256_path(raw_dir / RAW_MANIFEST)


def load_raw_manifest(raw_dir: Path) -> dict[str, Any]:
    path = raw_dir / RAW_MANIFEST
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise RawDataError(
            f"missing raw game-data manifest: {path}\n"
            "Run `python -m melee_sim.extract_data --iso /path/to/SSBM.iso`."
        ) from exc
    except (OSError, json.JSONDecodeError) as exc:
        raise RawDataError(f"invalid raw game-data manifest: {path}: {exc}") from exc
    if (
        not isinstance(payload, dict)
        or payload.get("magic") != RAW_MANIFEST_MAGIC
        or payload.get("version") != RAW_MANIFEST_VERSION
    ):
        raise RawDataError(f"unsupported raw game-data manifest header: {path}")
    return payload


def validate_raw_data_root(
    raw_dir: Path,
    *,
    required_names: Iterable[str] = (),
    verify_hashes: bool = True,
) -> dict[str, Any]:
    payload = load_raw_manifest(raw_dir)
    disc = payload.get("disc")
    if (
        not isinstance(disc, dict)
        or payload.get("profile") != "rl_1_0"
        or payload.get("chars") != list(RL_1_0_CHARS)
        or payload.get("stages") != list(RL_1_0_STAGES)
        or disc.get("game_id") != "GALE01"
        or disc.get("revision") != 2
        or not isinstance(disc.get("sha256"), str)
        or len(disc["sha256"]) != 64
        or not isinstance(disc.get("size"), int)
        or disc["size"] <= 0
    ):
        raise RawDataError(
            f"raw game data is not the complete GALE01 revision 2 RL 1.0 profile: "
            f"{raw_dir / RAW_MANIFEST}"
        )

    records = payload.get("files")
    if not isinstance(records, list) or not records:
        raise RawDataError(f"raw game-data manifest has no files: {raw_dir / RAW_MANIFEST}")

    by_name: dict[str, dict[str, Any]] = {}
    for record in records:
        if not isinstance(record, dict):
            raise RawDataError(f"malformed raw game-data record: {record!r}")
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
        ):
            raise RawDataError(f"malformed raw game-data record: {record!r}")
        if name in by_name:
            raise RawDataError(f"duplicate raw game-data record: {name}")
        by_name[name] = record

    missing_records = sorted(set(required_names) - set(by_name))
    if missing_records:
        raise RawDataError(
            "raw game-data manifest is missing required files: "
            + ", ".join(missing_records)
        )

    for name, record in by_name.items():
        path = raw_dir / name
        try:
            actual_size = path.stat().st_size
        except FileNotFoundError as exc:
            raise RawDataError(f"missing raw game-data file: {path}") from exc
        if actual_size != record["size"]:
            raise RawDataError(
                f"raw game-data file has wrong size: {path}: "
                f"expected {record['size']}, got {actual_size}"
            )
        if verify_hashes:
            actual_digest = sha256_path(path)
            if actual_digest != record["sha256"]:
                raise RawDataError(
                    f"raw game-data file has wrong SHA-256: {path}: "
                    f"expected {record['sha256']}, got {actual_digest}"
                )
    return payload
