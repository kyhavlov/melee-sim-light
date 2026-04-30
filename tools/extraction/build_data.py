from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path


FTCO_SM_DAMAGEAIR2 = 175
FTCO_SM_DAMAGEAIR3 = 176
# Decomp: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.

_RUN_TIMINGS: list[tuple[str, float]] | None = None


def _run(mod: str, argv: list[str], timings: list[tuple[str, float]] | None = None) -> None:
    cmd = [sys.executable, "-m", mod, *argv]
    print("$", " ".join(str(x) for x in cmd), flush=True)
    t0 = time.perf_counter()
    subprocess.run(cmd, check=True)
    dt = time.perf_counter() - t0
    sink = _RUN_TIMINGS if timings is None else timings
    if sink is not None:
        sink.append((mod, dt))
        print(f"[timing] {mod} {dt:.3f}s", flush=True)


def _require(path: Path, hint: str) -> None:
    if path.exists():
        return
    raise SystemExit(f"missing required file: {path}\n\nhint:\n{hint}\n")


def _copy_anim_outputs(character: str, *, src_dir: Path, out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for suffix in (".bin", ".locals.bin", ".tracks.bin", ".blend.bin", ".dyn.bin"):
        src = src_dir / f"{character}{suffix}"
        if src.exists():
            shutil.copyfile(src, out_dir / f"{character}{suffix}")


def main() -> None:
    global _RUN_TIMINGS
    ap = argparse.ArgumentParser(description="Build ISO-derived `data/` artifacts.")
    ap.add_argument("--iso-dir", type=Path, default=Path("_iso"), help="directory containing extracted *.dat files")
    ap.add_argument("--chars", type=str, default="fox,falco", help="comma-separated characters (fox,falco,...)")
    ap.add_argument(
        "--stage",
        type=str,
        default="grnla",
        help="stage key (currently: grnla = Final Destination / GrNLa.dat; grnba = Battlefield / GrNBa.dat)",
    )
    ap.add_argument("--melee-decomp", type=Path, default=Path("refs/melee"), help="path to doldecomp/melee checkout")
    ap.add_argument("--timings", action="store_true", help="print per-generator wall-clock timings")
    args = ap.parse_args()

    iso_dir = args.iso_dir
    chars = [c.strip() for c in args.chars.split(",") if c.strip()]
    timings: list[tuple[str, float]] | None = [] if args.timings else None
    _RUN_TIMINGS = timings

    # Sources we need in _iso.
    _require(
        iso_dir / "PlCo.dat",
        "uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*PlCo.dat' --out-dir _iso",
    )
    _require(
        iso_dir / "ItCo.dat",
        "uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*ItCo.dat' --out-dir _iso",
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
    for d in (
        out_stage.parent,
        out_common.parent,
        Path("data/airborne_state_events"),
        Path("data/anims"),
        Path("data/anims_ecb"),
        Path("data/attack_id/move_id"),
        Path("data/characters"),
        Path("data/ecb"),
        Path("data/hit_status"),
        Path("data/hitboxes"),
        Path("data/hurtbox_states"),
        Path("data/hurtcaps"),
        Path("data/items"),
        Path("data/motion_state/owners"),
        Path("data/moves"),
        Path("data/shields"),
        Path("data/special_msids"),
        Path("data/staling/move_id"),
    ):
        d.mkdir(parents=True, exist_ok=True)

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
    _run(
        "tools.extraction.extract_staling_weights",
        ["--plco", str(iso_dir / "PlCo.dat"), "--out", "data/staling/weights.bin"],
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
    _run(
        "tools.extraction.extract_item_common_data",
        ["--itco", str(iso_dir / "ItCo.dat"), "--out", "data/items/item_common.json"],
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
    _run(
        "tools.extraction.extract_staling_move_id",
        [
            "--melee_decomp",
            str(args.melee_decomp),
            "--out_dir",
            "data/staling/move_id",
            "--chars",
            ",".join(chars),
        ],
    )
    _run(
        "tools.extraction.extract_attack_id_move_id",
        [
            "--melee_decomp",
            str(args.melee_decomp),
            "--out_dir",
            "data/attack_id/move_id",
            "--chars",
            ",".join(chars),
        ],
    )
    # The MSLACID1 binary is the data contract for move_id, x4_flags, and MotionState +0x8/x9
    # lanes. Debug JSON from extract_attack_id_move_id is optional inspection output only and is
    # intentionally not produced by build_data.
    _run(
        "tools.extraction.extract_motion_state_owners",
        [
            "--melee_decomp",
            str(args.melee_decomp),
            "--out_dir",
            "data/motion_state/owners",
            "--chars",
            ",".join(chars),
        ],
    )
    # The MSLMSO01 binary is the MotionState owner/callback contract. Its callback_symbols.json
    # manifest is review/debug metadata mapping generated callback ids back to decomp symbols.

    # Hitbox event tables (moves.json → hitboxes.bin; compact binary for init-time load).
    #
    # This is a single codepath: we do not emit MSLHITB1 from the ISO directly, and we do not patch
    # the bins post-hoc. If you need to change hitbox metadata, do it by improving the movescript
    # extraction (data/moves/*.json) and then rebuild.
    for ch in chars:
        _run(
            "tools.extraction.extract_fighter_hitboxes",
            ["--moves", f"data/moves/{ch}.json", "--out", f"data/hitboxes/{ch}.bin"],
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

    # Movescript-derived fp->x221C_u16_y timelines (opcode 52 / ftAction_80072C6C).
    _run(
        "tools.extraction.extract_fighter_state_flags_221c_y",
        [
            "--iso_dir",
            str(iso_dir),
            "--melee_decomp",
            str(args.melee_decomp),
            "--special_msids_dir",
            "data/special_msids",
            "--out_dir",
            "data/state_flags_221c_y",
            "--chars",
            ",".join(chars),
        ],
    )

    # Movescript-derived set_airborne_state timelines (opcode 25 / ftAction_80071998).
    _run(
        "tools.extraction.extract_fighter_airborne_state_events",
        [
            "--iso_dir",
            str(iso_dir),
            "--melee_decomp",
            str(args.melee_decomp),
            "--special_msids_dir",
            "data/special_msids",
            "--out_dir",
            "data/airborne_state_events",
            "--chars",
            ",".join(chars),
        ],
    )

    # Anim matrices per needed msid (depends on data/moves + data/hurtcaps + data/characters).
    #
    # Bake the ECB-capable set once per character, then copy the byte-identical runtime anim files
    # from that output. The regression test locks this assumption so adding a genuinely broader ECB
    # set later must update this path instead of silently changing data/anims/<char>*.
    for ch in chars:
        ecb_anim_dir = "data/anims_ecb"
        # ECB tables need broader msid coverage than the runtime pose/move subset.
        #
        # In particular, the canonical Fox/Falco suite includes DamageAir2/3 (ftCo_Submotion 175/176),
        # and missing ECB samples can spuriously ground (Landing) during hitstun.
        #
        # Decomp source: refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`.
        _run(
            "tools.extraction.extract_fighter_anims",
            [
                "--character",
                ch,
                "--out-dir",
                ecb_anim_dir,
                "--add-msid",
                str(FTCO_SM_DAMAGEAIR2),
                "--add-msid",
                str(FTCO_SM_DAMAGEAIR3),
            ],
        )
        t_copy0 = time.perf_counter()
        _copy_anim_outputs(ch, src_dir=Path(ecb_anim_dir), out_dir=Path("data/anims"))
        if timings is not None:
            dt = time.perf_counter() - t_copy0
            timings.append(("tools.extraction.copy_fighter_anims", dt))
            print(f"[timing] tools.extraction.copy_fighter_anims {dt:.3f}s", flush=True)
        _run(
            "tools.extraction.extract_ecb_bottom",
            [
                "--character",
                ch,
                "--anims",
                f"{ecb_anim_dir}/{ch}.bin",
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
                f"{ecb_anim_dir}/{ch}.bin",
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
    if timings is not None:
        print("[timing-summary]")
        for mod, dt in sorted(timings, key=lambda x: x[1], reverse=True):
            print(f"{dt:9.3f}s {mod}")
        print(f"{sum(dt for _, dt in timings):9.3f}s total_subprocess")


if __name__ == "__main__":
    main()
