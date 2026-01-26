from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def _run(mod: str, argv: list[str]) -> None:
    cmd = [sys.executable, "-m", mod, *argv]
    print("$", " ".join(str(x) for x in cmd))
    subprocess.run(cmd, check=True)


def _require(path: Path, hint: str) -> None:
    if path.exists():
        return
    raise SystemExit(f"missing required file: {path}\n\nhint:\n{hint}\n")


def main() -> None:
    ap = argparse.ArgumentParser(description="Build ISO-derived `data/` artifacts (cached, gitignored).")
    ap.add_argument("--iso-dir", type=Path, default=Path("_iso"), help="directory containing extracted *.dat files")
    ap.add_argument("--chars", type=str, default="fox,falco", help="comma-separated characters (fox,falco,...)")
    ap.add_argument(
        "--stage",
        type=str,
        default="grnla",
        help="stage key (currently: grnla = Final Destination / GrNLa.dat; grnba = Battlefield / GrNBa.dat)",
    )
    ap.add_argument("--melee-decomp", type=Path, default=Path("refs/melee"), help="path to doldecomp/melee checkout")
    args = ap.parse_args()

    iso_dir = args.iso_dir
    chars = [c.strip() for c in args.chars.split(",") if c.strip()]

    # Sources we need in _iso.
    _require(
        iso_dir / "PlCo.dat",
        "uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*PlCo.dat' --out-dir _iso",
    )
    for ch in chars:
        dat = {"fox": "PlFx.dat", "falco": "PlFc.dat"}.get(ch)
        if dat is None:
            raise SystemExit(f"unsupported character for now: {ch}")
        _require(
            iso_dir / dat,
            f"uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*{dat}' --out-dir _iso",
        )

    stage_key = args.stage.lower()
    stage_dat_by_key = {
        # Decomp:
        # - Battlefield: refs/melee/src/melee/gr/grbattle.c:127 uses "/GrNBa.dat"
        # - Final Destination: refs/melee/src/melee/gr/grlast.c:151 uses "/GrNLa.dat"
        "grnba": "GrNBa.dat",
        "grnla": "GrNLa.dat",
    }
    stage_dat = stage_dat_by_key.get(stage_key)
    if stage_dat is None:
        raise SystemExit(f"unsupported stage key: {args.stage!r}")
    _require(
        iso_dir / stage_dat,
        f"uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*{stage_dat}' --out-dir _iso",
    )

    # Outputs.
    out_stage_by_key = {
        "grnla": Path("data/stages/final_destination.json"),
        "grnba": Path("data/stages/battlefield.json"),
    }
    out_stage = out_stage_by_key[stage_key]
    out_common = Path("data/common/ft_common_data.json")

    # Stage collision.
    _run(
        "tools.extraction.extract_stage_collision",
        ["--dat", str(iso_dir / stage_dat), "--out", str(out_stage)],
    )

    # Common constants.
    _run(
        "tools.extraction.extract_ftcommon_data",
        ["--plco", str(iso_dir / "PlCo.dat"), "--out", str(out_common)],
    )

    # Character attrs (also contains key ECB/ledge snap params and laser special attrs).
    _run(
        "tools.extraction.extract_character_attrs",
        ["--pl-dir", str(iso_dir), "--out-dir", "data/characters", "--chars", ",".join(chars)],
    )

    # Laser item params (Fox/Falco blaster shot) as a compact binary table.
    _run(
        "tools.extraction.extract_lasers",
        ["--iso_dir", str(iso_dir), "--out", "data/items/lasers.bin"],
    )

    # Guard-tilt shield bubble placement tables (used by shields_refresh for debug geometry).
    for ch in chars:
        _run(
            "tools.extraction.extract_shield_tilt_table",
            ["--iso-dir", str(iso_dir), "--character", ch, "--out", f"data/shields/{ch}.bin"],
        )

    # Hurt capsule init tables.
    for ch in chars:
        _run(
            "tools.extraction.extract_fighter_hurtcapsules",
            [
                "--iso_dir",
                str(iso_dir),
                "--out_dir",
                "data/hurtcaps",
                "--out_bin_dir",
                "data/hurtcaps",
                "--character",
                ch,
            ],
        )

    # Subaction/move timelines.
    _run(
        "tools.extraction.extract_special_msids",
        ["--iso_dir", str(iso_dir), "--out_dir", "data/special_msids", "--chars", ",".join(chars)],
    )
    _run(
        "tools.extraction.extract_fighter_moves",
        [
            "--iso_dir",
            str(iso_dir),
            "--melee_decomp",
            str(args.melee_decomp),
            "--out_dir",
            "data/moves",
            "--special_msids_dir",
            "data/special_msids",
            "--chars",
            ",".join(chars),
        ],
    )

    # Movescript-derived hurt capsule state timelines.
    _run(
        "tools.extraction.extract_fighter_hurtbox_modes",
        [
            "--iso_dir",
            str(iso_dir),
            "--melee_decomp",
            str(args.melee_decomp),
            "--hurtcaps_dir",
            "data/hurtcaps",
            "--special_msids_dir",
            "data/special_msids",
            "--out_dir",
            "data/hurtbox_states",
            "--chars",
            ",".join(chars),
        ],
    )

    # Movescript-derived hit status timelines (opcode 26).
    _run(
        "tools.extraction.extract_fighter_hit_status",
        [
            "--iso_dir",
            str(iso_dir),
            "--melee_decomp",
            str(args.melee_decomp),
            "--special_msids_dir",
            "data/special_msids",
            "--out_dir",
            "data/hit_status",
            "--chars",
            ",".join(chars),
        ],
    )

    # Anim matrices per needed msid (depends on data/moves + data/hurtcaps + data/characters).
    for ch in chars:
        _run(
            "tools.extraction.extract_fighter_anims",
            ["--character", ch, "--out-dir", "data/anims"],
        )
        _run(
            "tools.extraction.extract_ecb_bottom",
            [
                "--character",
                ch,
                "--anims",
                f"data/anims/{ch}.bin",
                "--attrs",
                f"data/characters/{ch}.json",
                "--out",
                f"data/ecb/{ch}_bottom.bin",
            ],
        )
        _run(
            "tools.extraction.extract_ecb_extents",
            [
                "--character",
                ch,
                "--anims",
                f"data/anims/{ch}.bin",
                "--attrs",
                f"data/characters/{ch}.json",
                "--out",
                f"data/ecb/{ch}_extents.bin",
            ],
        )

    summary = {
        "stage": str(out_stage),
        "common": str(out_common),
        "chars": chars,
    }
    print("built:", json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
