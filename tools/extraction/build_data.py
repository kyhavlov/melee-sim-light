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
    ap.add_argument("--out-dir", type=Path, default=Path("data"), help="directory for generated simulator data")
    ap.add_argument("--chars", type=str, default="fox,falco", help="comma-separated characters (fox,falco,...)")
    ap.add_argument(
        "--stage",
        type=str,
        default=None,
        help=(
            "partial/debug-only single stage key alias for --stages "
            "(grnla = Final Destination, grnba = Battlefield, griz = Fountain of Dreams, "
            "grps = Pokemon Stadium, grst = Yoshi's Story, grop = Dream Land N64)"
        ),
    )
    ap.add_argument(
        "--stages",
        type=str,
        default="grnla,grnba,griz,grps,grst,grop",
        help="comma-separated stage keys (default: grnla,grnba,griz,grps,grst,grop)",
    )
    ap.add_argument("--melee-decomp", type=Path, default=Path("refs/melee"), help="path to doldecomp/melee checkout")
    ap.add_argument("--timings", action="store_true", help="print per-generator wall-clock timings")
    args = ap.parse_args()

    iso_dir = args.iso_dir
    out_root = args.out_dir

    def out(rel: str) -> Path:
        return out_root / rel

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

    stage_dat_by_key = {
        # Decomp:
        # - Battlefield: refs/melee/src/melee/gr/grbattle.c:127 uses "/GrNBa.dat"
        # - Final Destination: refs/melee/src/melee/gr/grlast.c:151 uses "/GrNLa.dat"
        # - Fountain of Dreams: refs/melee/src/melee/gr/grizumi.c:162 uses "/GrIz.dat"
        # - Pokemon Stadium: refs/melee/src/melee/gr/grpstadium.c:148 uses "/GrPs"
        # - Yoshi's Story: refs/melee/src/melee/gr/grstory.c:49 uses "/GrSt.dat"
        # - Dream Land N64: refs/melee/src/melee/gr/groldpupupu.c uses "/GrOp.dat"
        "grnba": "GrNBa.dat",
        "grnla": "GrNLa.dat",
        "griz": "GrIz.dat",
        "grps": "GrPs.dat",
        "grst": "GrSt.dat",
        "grop": "GrOp.dat",
    }
    if args.stage is not None:
        stage_keys = [args.stage.lower()]
    else:
        stage_keys = [s.strip().lower() for s in args.stages.split(",") if s.strip()]
    if not stage_keys:
        raise SystemExit("--stages must contain at least one stage key")
    unknown = [s for s in stage_keys if s not in stage_dat_by_key]
    if unknown:
        raise SystemExit(f"unsupported stage key(s): {unknown!r}")
    for stage_key in stage_keys:
        stage_dat = stage_dat_by_key[stage_key]
        _require(
            iso_dir / stage_dat,
            f"uv run python -m tools.extraction.iso_extract --iso SSBM.iso --glob '*{stage_dat}' --out-dir _iso",
        )

    # Outputs.
    out_stage_by_key = {
        "grnla": out("stages/final_destination.json"),
        "grnba": out("stages/battlefield.json"),
        "griz": out("stages/fountain_of_dreams.json"),
        "grps": out("stages/pokemon_stadium.json"),
        "grst": out("stages/yoshis_story.json"),
        "grop": out("stages/dream_land_n64.json"),
    }
    out_common = out("common/ft_common_data.json")
    for d in (
        out("stages"),
        out_common.parent,
        out("anims"),
        out("anims_ecb"),
        out("attack_id/move_id"),
        out("characters"),
        out("ecb"),
        out("hitboxes"),
        out("hurtcaps"),
        out("items"),
        out("items/articles"),
        out("model_parts"),
        out("motion_state/owners"),
        out("moves"),
        out("shields"),
        out("scripts"),
        out("special_msids"),
        out("stage_items"),
        out("staling/move_id"),
        out("stages/bin"),
    ):
        d.mkdir(parents=True, exist_ok=True)

    # Stage collision.
    for stage_key in stage_keys:
        stage_dat = stage_dat_by_key[stage_key]
        _run(
            "tools.extraction.extract_stage_collision",
            ["--dat", str(iso_dir / stage_dat), "--out", str(out_stage_by_key[stage_key])],
        )
        _run(
            "tools.extraction.extract_stage_metadata",
            [
                "--dat",
                str(iso_dir / stage_dat),
                "--out",
                str(out("stages/bin") / f"{stage_key}.bin"),
                "--audit",
                str(out("stages/bin") / f"{stage_key}.json"),
            ],
        )

    # Common constants.
    _run(
        "tools.extraction.extract_ftcommon_data",
        ["--plco", str(iso_dir / "PlCo.dat"), "--out", str(out_common)],
    )
    _run(
        "tools.extraction.extract_staling_weights",
        ["--plco", str(iso_dir / "PlCo.dat"), "--out", str(out("staling/weights.bin"))],
    )

    # Character attrs (also contains key ECB/ledge snap params and laser special attrs).
    _run(
        "tools.extraction.extract_character_attrs",
        ["--pl-dir", str(iso_dir), "--out-dir", str(out("characters")), "--chars", ",".join(chars)],
    )

    # Laser item params (Fox/Falco blaster shot) as a compact binary table.
    _run(
        "tools.extraction.extract_lasers",
        ["--iso_dir", str(iso_dir), "--out", str(out("items/lasers.bin"))],
    )
    _run(
        "tools.extraction.extract_item_common_data",
        ["--itco", str(iso_dir / "ItCo.dat"), "--out", str(out("items/item_common.json"))],
    )
    _run(
        "tools.extraction.extract_stage_item_objects",
        [
            "--grst",
            str(iso_dir / "GrSt.dat"),
            "--grop",
            str(iso_dir / "GrOp.dat"),
            "--out",
            str(out("stage_items/yoshi_shyguy.bin")),
            "--audit",
            str(out("stage_items/yoshi_shyguy.json")),
            "--dream-out",
            str(out("stage_items/dream_whispy.bin")),
            "--dream-audit",
            str(out("stage_items/dream_whispy.json")),
        ],
    )

    # Guard-tilt shield bubble placement tables (used by shields_refresh for debug geometry).
    for ch in chars:
        _run(
            "tools.extraction.extract_shield_tilt_table",
            ["--iso-dir", str(iso_dir), "--character", ch, "--out", str(out(f"shields/{ch}.bin"))],
        )

    # Hurt capsule init tables.
    for ch in chars:
        _run(
            "tools.extraction.extract_fighter_hurtcapsules",
            [
                "--iso_dir",
                str(iso_dir),
                "--out_dir",
                str(out("hurtcaps")),
                "--out_bin_dir",
                str(out("hurtcaps")),
                "--character",
                ch,
            ],
        )

    # Subaction/move timelines.
    _run(
        "tools.extraction.extract_special_msids",
        ["--iso_dir", str(iso_dir), "--out_dir", str(out("special_msids")), "--chars", ",".join(chars)],
    )
    _run(
        "tools.extraction.extract_fighter_moves",
        [
            "--iso_dir",
            str(iso_dir),
            "--melee_decomp",
            str(args.melee_decomp),
            "--out_dir",
            str(out("moves")),
            "--special_msids_dir",
            str(out("special_msids")),
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
            str(out("staling/move_id")),
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
            str(out("attack_id/move_id")),
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
            str(out("motion_state/owners")),
            "--chars",
            ",".join(chars),
        ],
    )
    # The MSLMSO01 binary is the MotionState owner/callback contract. Its callback_symbols.json
    # manifest is review/debug metadata mapping generated callback ids back to decomp symbols.

    for ch in chars:
        _run(
            "tools.extraction.extract_fighter_script_timeline",
            [
                "--moves",
                str(out(f"moves/{ch}.json")),
                "--character",
                ch,
                "--iso_dir",
                str(iso_dir),
                "--melee_decomp",
                str(args.melee_decomp),
                "--special_msids_dir",
                str(out("special_msids")),
                "--out",
                str(out(f"scripts/{ch}.bin")),
                "--manifest",
                str(out(f"scripts/{ch}_manifest.json")),
            ],
        )

    # Hitbox event tables (moves.json → hitboxes.bin; compact binary for init-time load).
    #
    # This is a single codepath: we do not emit MSLHITB1 from the ISO directly, and we do not patch
    # the bins post-hoc. If you need to change hitbox metadata, do it by improving the movescript
    # extraction (data/moves/*.json) and then rebuild.
    for ch in chars:
        _run(
            "tools.extraction.extract_fighter_hitboxes",
            ["--moves", str(out(f"moves/{ch}.json")), "--out", str(out(f"hitboxes/{ch}.bin"))],
        )

    # Anim matrices per needed msid (depends on data/moves + data/hurtcaps + data/characters).
    #
    # Bake the ECB-capable set once per character, then copy the byte-identical runtime anim files
    # from that output. The regression test locks this assumption so adding a genuinely broader ECB
    # set later must update this path instead of silently changing data/anims/<char>*.
    for ch in chars:
        ecb_anim_dir = out("anims_ecb")
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
                "--iso-dir",
                str(iso_dir),
                "--data-dir",
                str(out_root),
                "--out-dir",
                str(ecb_anim_dir),
                "--add-msid",
                str(FTCO_SM_DAMAGEAIR2),
                "--add-msid",
                str(FTCO_SM_DAMAGEAIR3),
            ],
        )
        t_copy0 = time.perf_counter()
        _copy_anim_outputs(ch, src_dir=ecb_anim_dir, out_dir=out("anims"))
        if timings is not None:
            dt = time.perf_counter() - t_copy0
            timings.append(("tools.extraction.copy_fighter_anims", dt))
            print(f"[timing] tools.extraction.copy_fighter_anims {dt:.3f}s", flush=True)
        _run(
            "tools.extraction.extract_fighter_parts",
            [
                "--character",
                ch,
                "--attrs",
                str(out(f"characters/{ch}.json")),
                "--tracks",
                str(out(f"anims/{ch}.tracks.bin")),
                "--out",
                str(out(f"model_parts/{ch}.bin")),
                "--audit",
                str(out(f"model_parts/{ch}.json")),
            ],
        )
        _run(
            "tools.extraction.extract_ecb_bottom",
            [
                "--character",
                ch,
                "--anims",
                str(ecb_anim_dir / f"{ch}.bin"),
                "--attrs",
                str(out(f"characters/{ch}.json")),
                "--out",
                str(out(f"ecb/{ch}_bottom.bin")),
            ],
        )

        _run(
            "tools.extraction.extract_ecb_extents",
            [
                "--character",
                ch,
                "--anims",
                str(ecb_anim_dir / f"{ch}.bin"),
                "--attrs",
                str(out(f"characters/{ch}.json")),
                "--out",
                str(out(f"ecb/{ch}_extents.bin")),
            ],
        )

    _run(
        "tools.extraction.extract_item_articles",
        [
            "--attrs-dir",
            str(out("characters")),
            "--item-common",
            str(out("items/item_common.json")),
            "--out",
            str(out("items/articles/fox_falco.bin")),
            "--manifest",
            str(out("items/articles/manifest.json")),
            "--chars",
            ",".join(chars),
        ],
    )

    summary = {
        "stages": [str(out_stage_by_key[key]) for key in stage_keys],
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
