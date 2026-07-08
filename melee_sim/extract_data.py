from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from importlib import metadata
from pathlib import Path
from urllib.parse import urlparse, unquote

from .iso import extract_file, find_files, list_files


_STAGE_DAT_BY_KEY = {
    "grnla": "GrNLa.dat",
    "grnba": "GrNBa.dat",
    "griz": "GrIz.dat",
    "grps": "GrPs.dat",
    "grst": "GrSt.dat",
    "grop": "GrOp.dat",
}

_CHAR_GLOBS = {
    "fox": "*PlFx*.dat",
    "falcon": "*PlCa*.dat",
    "falco": "*PlFc*.dat",
    "marth": "*PlMs*.dat",
    "sheik": "*PlSk*.dat",
    "zelda": "*PlZd*.dat",
}


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


def _extract_glob(*, iso: Path, files, pattern: str, out_dir: Path, force: bool) -> int:
    matches = find_files(files, pattern)
    if not matches:
        raise SystemExit(f"missing required ISO file matching {pattern!r}")
    count = 0
    for match in matches:
        out = out_dir / Path(match.path).name
        if out.exists() and not force:
            print(f"exists {out}; skipping")
            continue
        extract_file(iso, match, out)
        print(f"wrote {out} ({match.size} bytes)")
        count += 1
    return count


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(description="Extract melee-sim-light data from an SSBM ISO.")
    ap.add_argument("--iso", type=Path, required=True, help="path to a valid SSBM ISO")
    ap.add_argument("--out-dir", type=Path, default=Path(".msl"), help="generated data directory")
    ap.add_argument("--iso-dir", type=Path, default=None, help="directory for extracted source DAT files")
    ap.add_argument("--force", action="store_true", help="rewrite already-extracted source DAT files")
    ap.add_argument(
        "--chars",
        type=str,
        default="fox,falco,marth,falcon,sheik,zelda",
        help="comma-separated characters",
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
    out_dir = args.out_dir.expanduser().resolve()
    iso_dir = (args.iso_dir or (out_dir / "_iso")).expanduser().resolve()
    chars = [c.strip().lower() for c in args.chars.split(",") if c.strip()]
    stages = [s.strip().lower() for s in args.stages.split(",") if s.strip()]

    unknown_chars = [c for c in chars if c not in _CHAR_GLOBS]
    if unknown_chars:
        raise SystemExit(f"unsupported character key(s): {unknown_chars!r}")
    unknown_stages = [s for s in stages if s not in _STAGE_DAT_BY_KEY]
    if unknown_stages:
        raise SystemExit(f"unsupported stage key(s): {unknown_stages!r}")

    iso_dir.mkdir(parents=True, exist_ok=True)
    out_dir.mkdir(parents=True, exist_ok=True)

    files = list_files(iso)
    for pattern in ("*PlCo.dat", "*ItCo.dat"):
        _extract_glob(iso=iso, files=files, pattern=pattern, out_dir=iso_dir, force=args.force)
    for ch in chars:
        _extract_glob(iso=iso, files=files, pattern=_CHAR_GLOBS[ch], out_dir=iso_dir, force=args.force)
    for stage in stages:
        _extract_glob(
            iso=iso,
            files=files,
            pattern=f"*{_STAGE_DAT_BY_KEY[stage]}",
            out_dir=iso_dir,
            force=args.force,
        )

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
        ",".join(chars),
    ]
    if melee_decomp is not None and melee_decomp.exists():
        cmd.extend(["--melee-decomp", str(melee_decomp)])
    if args.timings:
        cmd.append("--timings")
    print("$", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


if __name__ == "__main__":
    main()
