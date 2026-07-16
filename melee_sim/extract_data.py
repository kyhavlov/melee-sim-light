from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from importlib import metadata
from pathlib import Path
from urllib.parse import unquote, urlparse

from tools.extraction.char_registry import CHARS

from .iso import (
    DiscIdentity,
    IsoFile,
    extract_files,
    extract_main_dol,
    find_files,
    list_files,
    read_disc_identity,
)
from .raw_data import (
    RAW_MANIFEST,
    RAW_MANIFEST_MAGIC,
    RAW_MANIFEST_VERSION,
    RL_1_0_CHARS,
    RL_1_0_STAGES,
    RawDataError,
    raw_manifest_digest,
    resolve_data_root,
    sha256_path,
    validate_raw_data_root,
)


_STAGE_DAT_BY_KEY = {
    "grnla": "GrNLa.dat",
    "grnba": "GrNBa.dat",
    "griz": "GrIz.dat",
    "grps": "GrPs.dat",
    "grst": "GrSt.dat",
    "grop": "GrOp.dat",
}

_DERIVED_SIM_CHARS = tuple(CHARS)
_SOURCE_CORE_ONLY_CHAR_GLOBS = {
    # The canonical decomp core consumes Puff's original archives directly.
    # The legacy derived simulator remains on its existing six-character
    # registry until public cutover, so keep this raw-only closure explicit.
    # refs/melee/src/melee/ft/chara/ftPurin/ftPr_Init.c
    "puff": "*PlPr*.dat",
}
_RUNTIME_CHARS = RL_1_0_CHARS
_RUNTIME_STAGES = RL_1_0_STAGES
if (
    tuple(ch for ch in _RUNTIME_CHARS if ch not in _SOURCE_CORE_ONLY_CHAR_GLOBS)
    != _DERIVED_SIM_CHARS
    or tuple(_STAGE_DAT_BY_KEY) != _RUNTIME_STAGES
):
    raise RuntimeError("raw-data profile and extraction registries disagree")
_CHAR_GLOBS = {name: f"*{Path(info.pl_dat).stem}*.dat" for name, info in CHARS.items()}
_CHAR_GLOBS.update(_SOURCE_CORE_ONLY_CHAR_GLOBS)
_SOURCE_CORE_COMMON_FILES = (
    "PlCo.dat",
    "PdPm.dat",
    "ItCo.dat",
    "ItCo.usd",
    "EfCoData.dat",
)
_SOURCE_CORE_EFFECT_FILES = tuple(
    dict.fromkeys((*[info.effect_dat for info in CHARS.values()], "EfPrData.dat"))
)


def _path_from_direct_url() -> Path | None:
    try:
        text = metadata.distribution("melee-sim-light").read_text("direct_url.json")
    except Exception:
        return None
    if not text:
        return None
    try:
        payload = json.loads(text)
    except Exception:
        return None
    url = payload.get("url")
    if not isinstance(url, str):
        return None
    parsed = urlparse(url)
    if parsed.scheme != "file":
        return None
    path = Path(unquote(parsed.path)).resolve()
    if path.name == "python":
        path = path.parent
    return path


def _default_melee_decomp() -> Path | None:
    env_path = os.environ.get("MELEE_SIM_MELEE_DECOMP")
    if env_path:
        return Path(env_path).expanduser().resolve()
    cwd_path = Path("refs/melee").resolve()
    if cwd_path.exists():
        return cwd_path
    source_root = _path_from_direct_url()
    if source_root is not None:
        candidate = source_root / "refs" / "melee"
        if candidate.exists():
            return candidate.resolve()
    return None


def _select_raw_files(
    files: list[IsoFile], *, chars: list[str], stages: list[str]
) -> dict[str, IsoFile]:
    patterns = [f"*{name}" for name in _SOURCE_CORE_COMMON_FILES]
    patterns.extend(f"*{name}" for name in _SOURCE_CORE_EFFECT_FILES)
    patterns.extend(_CHAR_GLOBS[ch] for ch in chars)
    patterns.extend(f"*{_STAGE_DAT_BY_KEY[stage]}" for stage in stages)

    selected: dict[str, IsoFile] = {}
    for pattern in patterns:
        matches = find_files(files, pattern)
        if not matches:
            raise SystemExit(f"missing required ISO file matching {pattern!r}")
        for match in matches:
            name = Path(match.path).name
            previous = selected.get(name)
            if previous is not None and previous != match:
                raise SystemExit(
                    f"ISO contains duplicate required basename {name!r}: "
                    f"{previous.path!r}, {match.path!r}"
                )
            selected[name] = match
    return selected


def _write_json_atomic(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    os.replace(temporary, path)


def _raw_manifest_is_current(
    raw_dir: Path,
    *,
    identity: DiscIdentity,
    iso_size: int,
    iso_sha256: str,
    entries: dict[str, IsoFile],
    chars: list[str],
    stages: list[str],
) -> bool:
    try:
        payload = validate_raw_data_root(raw_dir, verify_hashes=True)
    except RawDataError:
        return False
    if payload.get("chars") != chars or payload.get("stages") != stages:
        return False
    if payload.get("disc") != {
        "game_id": identity.game_id,
        "revision": identity.revision,
        "sha256": iso_sha256,
        "size": iso_size,
    }:
        return False
    records = payload.get("files")
    if not isinstance(records, list):
        return False
    by_name = {
        record.get("name"): record
        for record in records
        if isinstance(record, dict) and isinstance(record.get("name"), str)
    }
    if set(by_name) != {"main.dol", *entries}:
        return False
    for name, entry in entries.items():
        record = by_name[name]
        if (
            record.get("iso_path") != entry.path
            or record.get("iso_offset") != entry.offset
            or record.get("size") != entry.size
        ):
            return False
    return True


def _extract_raw_data(
    iso: Path,
    raw_dir: Path,
    *,
    identity: DiscIdentity,
    iso_size: int,
    iso_sha256: str,
    entries: dict[str, IsoFile],
    chars: list[str],
    stages: list[str],
) -> None:
    raw_dir.mkdir(parents=True, exist_ok=True)
    (raw_dir / RAW_MANIFEST).unlink(missing_ok=True)
    dol_path = raw_dir / "main.dol"
    extract_main_dol(iso, dol_path)
    digests = extract_files(iso, entries, raw_dir)
    records: list[dict[str, object]] = [
        {
            "iso_path": "sys/main.dol",
            "name": "main.dol",
            "sha256": sha256_path(dol_path),
            "size": dol_path.stat().st_size,
        }
    ]
    records.extend(
        {
            "iso_offset": entry.offset,
            "iso_path": entry.path,
            "name": name,
            "sha256": digests[name],
            "size": entry.size,
        }
        for name, entry in entries.items()
    )
    records.sort(key=lambda record: str(record["name"]))
    _write_json_atomic(
        raw_dir / RAW_MANIFEST,
        {
            "magic": RAW_MANIFEST_MAGIC,
            "version": RAW_MANIFEST_VERSION,
            "profile": "rl_1_0",
            "chars": chars,
            "stages": stages,
            "disc": {
                "game_id": identity.game_id,
                "revision": identity.revision,
                "sha256": iso_sha256,
                "size": iso_size,
            },
            "files": records,
        },
    )


def _derived_data_is_current(
    out_dir: Path,
    *,
    raw_digest: str,
    chars: list[str],
    stages: list[str],
) -> bool:
    from tools.extraction.build_data import DATA_SCHEMA_VERSIONS, GENERATED_DATA_DIRS

    try:
        payload = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    header_matches = (
        isinstance(payload, dict)
        and payload.get("magic") == "MSLDATA1"
        and payload.get("version") == 1
        and payload.get("chars") == chars
        and payload.get("stages") == stages
        and payload.get("schemas") == DATA_SCHEMA_VERSIONS
        and payload.get("raw_manifest_sha256") == raw_digest
    )
    if not header_matches:
        return False
    records = payload.get("files")
    if not isinstance(records, list) or not records:
        return False
    expected_paths: set[str] = set()
    for record in records:
        if not isinstance(record, dict):
            return False
        relative = record.get("path")
        size = record.get("size")
        digest = record.get("sha256")
        if (
            not isinstance(relative, str)
            or Path(relative).is_absolute()
            or ".." in Path(relative).parts
            or not isinstance(size, int)
            or not isinstance(digest, str)
        ):
            return False
        if relative in expected_paths:
            return False
        expected_paths.add(relative)
        path = out_dir / relative
        try:
            if path.stat().st_size != size or sha256_path(path) != digest:
                return False
        except OSError:
            return False
    actual_paths: set[str] = set()
    for directory in GENERATED_DATA_DIRS:
        root = out_dir / directory
        if not root.exists():
            continue
        actual_paths.update(
            path.relative_to(out_dir).as_posix()
            for path in root.rglob("*")
            if path.is_file()
            and path.relative_to(out_dir).as_posix()
            != "stages/slippi_neutral_spawns.json"
        )
    return actual_paths == expected_paths


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(description="Extract melee-sim-light data from an SSBM ISO.")
    ap.add_argument("--iso", type=Path, required=True, help="path to a valid SSBM ISO")
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="MSL data root; defaults to MSL_DATA_DIR or ./data",
    )
    ap.add_argument(
        "--iso-dir",
        type=Path,
        default=None,
        help="raw source directory; defaults to OUT_DIR/raw",
    )
    ap.add_argument("--force", action="store_true", help="rewrite already-extracted source DAT files")
    ap.add_argument(
        "--chars",
        type=str,
        default=",".join(_RUNTIME_CHARS),
        help="comma-separated characters; runtime data roots require the full registry",
    )
    ap.add_argument(
        "--stages",
        type=str,
        default="grnla,grnba,griz,grps,grst,grop",
        help="comma-separated stage keys",
    )
    ap.add_argument("--melee-decomp", type=Path, default=None, help="path to doldecomp/melee checkout")
    ap.add_argument("--timings", action="store_true", help="print per-generator wall-clock timings")
    args = ap.parse_args(argv)

    iso = args.iso.expanduser().resolve()
    if not iso.is_file():
        raise SystemExit(f"missing ISO: {iso}")
    out_dir = resolve_data_root(args.out_dir)
    iso_dir = (args.iso_dir or (out_dir / "raw")).expanduser().resolve()
    chars = [c.strip().lower() for c in args.chars.split(",") if c.strip()]
    stages = [s.strip().lower() for s in args.stages.split(",") if s.strip()]

    unknown_chars = [c for c in chars if c not in _CHAR_GLOBS]
    if unknown_chars:
        raise SystemExit(f"unsupported character key(s): {unknown_chars!r}")
    if len(chars) != len(_RUNTIME_CHARS) or set(chars) != set(_RUNTIME_CHARS):
        raise SystemExit(
            "runtime data roots require the full character registry: "
            f"{','.join(_RUNTIME_CHARS)}. Use individual extraction modules for debug subsets."
        )
    chars = list(_RUNTIME_CHARS)
    unknown_stages = [s for s in stages if s not in _STAGE_DAT_BY_KEY]
    if unknown_stages:
        raise SystemExit(f"unsupported stage key(s): {unknown_stages!r}")
    if len(stages) != len(_RUNTIME_STAGES) or set(stages) != set(_RUNTIME_STAGES):
        raise SystemExit(
            "runtime data roots require the full stage registry: "
            f"{','.join(_RUNTIME_STAGES)}. Use individual extraction modules for "
            "debug subsets."
        )
    stages = list(_RUNTIME_STAGES)
    derived_chars = list(_DERIVED_SIM_CHARS)

    started = time.perf_counter()
    identity = read_disc_identity(iso)
    if identity.game_id != "GALE01" or identity.revision != 2:
        raise SystemExit(
            "melee-sim-light requires an NTSC-U SSBM 1.02 ISO "
            f"(GALE01 revision 2); got {identity.game_id!r} "
            f"revision {identity.revision}"
        )
    files = list_files(iso)
    entries = _select_raw_files(files, chars=chars, stages=stages)
    iso_sha256 = sha256_path(iso)
    iso_size = iso.stat().st_size
    raw_current = not args.force and _raw_manifest_is_current(
        iso_dir,
        identity=identity,
        iso_size=iso_size,
        iso_sha256=iso_sha256,
        entries=entries,
        chars=chars,
        stages=stages,
    )
    if raw_current:
        print(f"raw game data current: {iso_dir} ({len(entries) + 1} files)")
    else:
        _extract_raw_data(
            iso,
            iso_dir,
            identity=identity,
            iso_size=iso_size,
            iso_sha256=iso_sha256,
            entries=entries,
            chars=chars,
            stages=stages,
        )
        print(f"extracted raw game data: {iso_dir} ({len(entries) + 1} files)")

    raw_digest = raw_manifest_digest(iso_dir)
    if not args.force and _derived_data_is_current(
        out_dir, raw_digest=raw_digest, chars=derived_chars, stages=stages
    ):
        print(f"derived simulator data current: {out_dir}")
        if args.timings:
            print(f"[timing] extraction_total {time.perf_counter() - started:.3f}s")
        return

    melee_decomp = args.melee_decomp.expanduser().resolve() if args.melee_decomp else _default_melee_decomp()
    cmd = [
        sys.executable,
        "-m",
        "tools.extraction.build_data",
        "--iso-dir",
        str(iso_dir),
        "--out-dir",
        str(out_dir),
        "--stages",
        ",".join(stages),
        "--chars",
        ",".join(derived_chars),
        "--raw-manifest",
        str(iso_dir / RAW_MANIFEST),
    ]
    if melee_decomp is not None and melee_decomp.exists():
        cmd.extend(["--melee-decomp", str(melee_decomp)])
    if args.timings:
        cmd.append("--timings")
    (out_dir / "manifest.json").unlink(missing_ok=True)
    print("$", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)
    if args.timings:
        print(f"[timing] extraction_total {time.perf_counter() - started:.3f}s")


if __name__ == "__main__":
    main()
