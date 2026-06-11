from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, asdict
from pathlib import Path

from tools.extraction import extract_fighter_anims
from tools.extraction.extract_fighter_anims import _msid_anim_entry


@dataclass(frozen=True)
class SpecialSlot:
    default: int
    left: int | None = None
    right: int | None = None


@dataclass(frozen=True)
class SpecialPhases:
    start: SpecialSlot | None = None
    loop: SpecialSlot | None = None
    main: SpecialSlot | None = None
    end: SpecialSlot | None = None
    hold: SpecialSlot | None = None
    hit: SpecialSlot | None = None


@dataclass(frozen=True)
class SpecialMsids:
    neutral_ground: SpecialPhases | None = None
    neutral_air: SpecialPhases | None = None
    side_ground: SpecialPhases | None = None
    side_air: SpecialPhases | None = None
    up_ground: SpecialPhases | None = None
    up_air: SpecialPhases | None = None
    down_ground: SpecialPhases | None = None
    down_air: SpecialPhases | None = None
    # Multi-stage chain submotions that do not fit the start/loop/main/hit/end slot scheme
    # (e.g. Marth's Dancing Blade stages and the SpecialN full-charge End1). Consumers that
    # enumerate special msids for script/hitbox extraction must include these.
    extra_script_msids: list[int] | None = None


def _scan_special_entries(character: str, *, limit: int = 512) -> list[tuple[int, str]]:
    out: list[tuple[int, str]] = []
    for msid in range(limit):
        e = _msid_anim_entry(character, msid)
        if not e:
            continue
        name = e[0]
        if "Special" in name:
            out.append((msid, name))
    return out


def _pick_first(entries: list[tuple[int, str]], *, includes: list[str], excludes: list[str] | None = None) -> int | None:
    exc = excludes or []
    for msid, name in entries:
        ok = True
        for inc in includes:
            if inc not in name:
                ok = False
                break
        if not ok:
            continue
        for ex in exc:
            if ex in name:
                ok = False
                break
        if ok:
            return msid
    return None


def _pick_slot(
    entries: list[tuple[int, str]],
    *,
    want_right: list[str],
    want_left: list[str],
    want_default: list[list[str]],
    excludes: list[str],
) -> SpecialSlot | None:
    right = _pick_first(entries, includes=want_right, excludes=excludes)
    left = _pick_first(entries, includes=want_left, excludes=excludes)

    default = None
    for inc in want_default:
        default = _pick_first(entries, includes=inc, excludes=excludes)
        if default is not None:
            break
    if default is None:
        # Prefer right variant as the default when present, else left, else nothing.
        default = right if right is not None else left
    if default is None:
        return None
    return SpecialSlot(
        default=int(default),
        left=int(left) if left is not None else None,
        right=int(right) if right is not None else None,
    )


def _pick_phase(
    entries: list[tuple[int, str]],
    *,
    base: str,
    base_fallbacks: list[str] | None = None,
    use_rl_suffix: bool = True,
    excludes: list[str],
) -> SpecialSlot | None:
    fb = base_fallbacks or []
    want_default = [[base], *[[b] for b in fb]]
    want_right = [f"{base}R"] if use_rl_suffix else [base]
    want_left = [f"{base}L"] if use_rl_suffix else [base]
    return _pick_slot(entries, want_right=want_right, want_left=want_left, want_default=want_default, excludes=excludes)


def extract_special_msids(*, character: str) -> SpecialMsids:
    entries = _scan_special_entries(character)

    # Most entries are named like:
    #   PlyFox5K_Share_ACTION_SpecialNStart_figatree
    # Exclude FallSpecial entries here; they are not "B specials".
    excludes_fall = ["FallSpecial"]

    neutral_ground_excl = ["_ACTION_SpecialAir", "_ACTION_SpecialS", "_ACTION_SpecialHi", "_ACTION_SpecialLw", *excludes_fall]
    neutral_air_excl = ["_ACTION_SpecialN", "_ACTION_SpecialS", "_ACTION_SpecialHi", "_ACTION_SpecialLw", *excludes_fall]
    side_ground_excl = ["_ACTION_SpecialAir", "_ACTION_SpecialN", "_ACTION_SpecialHi", "_ACTION_SpecialLw", *excludes_fall]
    side_air_excl = ["_ACTION_SpecialN", "_ACTION_SpecialHi", "_ACTION_SpecialLw", *excludes_fall]
    up_ground_excl = ["_ACTION_SpecialAir", "_ACTION_SpecialN", "_ACTION_SpecialS", "_ACTION_SpecialLw", "_ACTION_SpecialHiLanding", "_ACTION_SpecialHiFall", "_ACTION_SpecialHiBound", *excludes_fall]
    up_air_excl = ["_ACTION_SpecialN", "_ACTION_SpecialS", "_ACTION_SpecialLw", "_ACTION_SpecialHiLanding", "_ACTION_SpecialHiFall", "_ACTION_SpecialHiBound", *excludes_fall]
    down_ground_excl = ["_ACTION_SpecialAir", "_ACTION_SpecialN", "_ACTION_SpecialS", "_ACTION_SpecialHi", *excludes_fall]
    down_air_excl = ["_ACTION_SpecialN", "_ACTION_SpecialS", "_ACTION_SpecialHi", *excludes_fall]

    neutral_ground = SpecialPhases(
        start=_pick_phase(entries, base="_ACTION_SpecialNStart", excludes=neutral_ground_excl),
        loop=_pick_phase(entries, base="_ACTION_SpecialNLoop", use_rl_suffix=False, excludes=neutral_ground_excl),
        main=_pick_phase(
            entries,
            base="_ACTION_SpecialN_figatree",
            base_fallbacks=["_ACTION_SpecialN"],
            use_rl_suffix=False,
            excludes=["_ACTION_SpecialNStart", "_ACTION_SpecialNLoop", "_ACTION_SpecialNEnd", *neutral_ground_excl],
        ),
        end=_pick_phase(
            entries,
            base="_ACTION_SpecialNEnd",
            base_fallbacks=["_ACTION_SpecialNCansel"],
            excludes=neutral_ground_excl,
        ),
    )
    if neutral_ground.start is None and neutral_ground.loop is None and neutral_ground.main is None and neutral_ground.end is None:
        neutral_ground = None

    neutral_air = SpecialPhases(
        start=_pick_phase(entries, base="_ACTION_SpecialAirNStart", excludes=neutral_air_excl),
        loop=_pick_phase(entries, base="_ACTION_SpecialAirNLoop", use_rl_suffix=False, excludes=neutral_air_excl),
        main=_pick_phase(
            entries,
            base="_ACTION_SpecialAirN_figatree",
            base_fallbacks=["_ACTION_SpecialAirN", "_ACTION_SpecialN"],
            use_rl_suffix=False,
            excludes=["_ACTION_SpecialAirNStart", "_ACTION_SpecialAirNLoop", "_ACTION_SpecialAirNEnd", *neutral_air_excl],
        ),
        end=_pick_phase(
            entries,
            base="_ACTION_SpecialAirNEnd",
            base_fallbacks=["_ACTION_SpecialAirNCansel"],
            excludes=neutral_air_excl,
        ),
    )
    if neutral_air.start is None and neutral_air.loop is None and neutral_air.main is None and neutral_air.end is None:
        neutral_air = None

    side_ground = SpecialPhases(
        start=_pick_phase(entries, base="_ACTION_SpecialSStart", excludes=side_ground_excl),
        main=_pick_phase(
            entries,
            base="_ACTION_SpecialS_figatree",
            base_fallbacks=["_ACTION_SpecialSJump_figatree", "_ACTION_SpecialS1_figatree", "_ACTION_SpecialS"],
            use_rl_suffix=False,
            excludes=["_ACTION_SpecialSStart", "_ACTION_SpecialSEnd", *side_ground_excl],
        ),
        end=_pick_phase(entries, base="_ACTION_SpecialSEnd", excludes=side_ground_excl),
    )
    if side_ground.start is None and side_ground.main is None and side_ground.end is None:
        side_ground = None

    side_air = SpecialPhases(
        start=_pick_phase(entries, base="_ACTION_SpecialAirSStart", excludes=side_air_excl),
        main=_pick_phase(
            entries,
            base="_ACTION_SpecialAirS_figatree",
            base_fallbacks=["_ACTION_SpecialAirS1_figatree", "_ACTION_SpecialAirS", "_ACTION_SpecialS1", "_ACTION_SpecialS"],
            use_rl_suffix=False,
            excludes=["_ACTION_SpecialAirSStart", "_ACTION_SpecialAirSEnd", *side_air_excl],
        ),
        end=_pick_phase(entries, base="_ACTION_SpecialAirSEnd", excludes=side_air_excl),
    )
    if side_air.start is None and side_air.main is None and side_air.end is None:
        side_air = None

    up_ground = SpecialPhases(
        hold=_pick_phase(entries, base="_ACTION_SpecialHiHold", use_rl_suffix=False, excludes=up_ground_excl),
        start=_pick_phase(entries, base="_ACTION_SpecialHiStart", use_rl_suffix=False, excludes=up_ground_excl),
        main=_pick_phase(entries, base="_ACTION_SpecialHi_figatree", base_fallbacks=["_ACTION_SpecialHi"], use_rl_suffix=True, excludes=up_ground_excl),
        end=_pick_phase(entries, base="_ACTION_SpecialHiEnd", use_rl_suffix=False, excludes=up_ground_excl),
    )
    if up_ground.hold is None and up_ground.start is None and up_ground.main is None and up_ground.end is None:
        up_ground = None

    up_air = SpecialPhases(
        hold=_pick_phase(entries, base="_ACTION_SpecialHiHoldAir", use_rl_suffix=False, excludes=up_air_excl),
        start=_pick_phase(entries, base="_ACTION_SpecialAirHiStart", use_rl_suffix=False, excludes=up_air_excl),
        main=_pick_phase(
            entries,
            base="_ACTION_SpecialAirHi_figatree",
            base_fallbacks=["_ACTION_SpecialAirHi", "_ACTION_SpecialHi"],
            use_rl_suffix=True,
            excludes=up_air_excl,
        ),
        end=_pick_phase(entries, base="_ACTION_SpecialAirHiEnd", use_rl_suffix=False, excludes=up_air_excl),
    )
    if up_air.hold is None and up_air.start is None and up_air.main is None and up_air.end is None:
        up_air = None

    down_ground = SpecialPhases(
        start=_pick_phase(entries, base="_ACTION_SpecialLwStart", use_rl_suffix=False, excludes=down_ground_excl),
        loop=_pick_phase(entries, base="_ACTION_SpecialLwLoop", use_rl_suffix=False, excludes=down_ground_excl),
        main=_pick_phase(
            entries,
            base="_ACTION_SpecialLw_figatree",
            base_fallbacks=["_ACTION_SpecialLwL_figatree", "_ACTION_SpecialLwR_figatree", "_ACTION_SpecialLw"],
            use_rl_suffix=True,
            excludes=["_ACTION_SpecialLwStart", "_ACTION_SpecialLwLoop", "_ACTION_SpecialLwEnd", "_ACTION_SpecialLwHit", *down_ground_excl],
        ),
        hit=_pick_phase(entries, base="_ACTION_SpecialLwHit", use_rl_suffix=False, excludes=down_ground_excl),
        end=_pick_phase(
            entries,
            base="_ACTION_SpecialLwEnd",
            base_fallbacks=["_ACTION_SpecialLwEndAir", "_ACTION_SpecialLwEndAir_figatree"],
            use_rl_suffix=False,
            excludes=down_ground_excl,
        ),
    )
    if down_ground.start is None and down_ground.loop is None and down_ground.main is None and down_ground.hit is None and down_ground.end is None:
        down_ground = None

    down_air = SpecialPhases(
        start=_pick_phase(entries, base="_ACTION_SpecialAirLwStart", use_rl_suffix=False, excludes=down_air_excl),
        loop=_pick_phase(entries, base="_ACTION_SpecialAirLwLoop", use_rl_suffix=False, excludes=down_air_excl),
        main=_pick_phase(
            entries,
            base="_ACTION_SpecialAirLw_figatree",
            base_fallbacks=["_ACTION_SpecialAirLwL_figatree", "_ACTION_SpecialAirLwR_figatree", "_ACTION_SpecialAirLw", "_ACTION_SpecialLw"],
            use_rl_suffix=True,
            excludes=["_ACTION_SpecialAirLwStart", "_ACTION_SpecialAirLwLoop", "_ACTION_SpecialAirLwEnd", "_ACTION_SpecialAirLwHit", *down_air_excl],
        ),
        hit=_pick_phase(entries, base="_ACTION_SpecialAirLwHit", use_rl_suffix=False, excludes=down_air_excl),
        end=_pick_phase(
            entries,
            base="_ACTION_SpecialAirLwEnd",
            base_fallbacks=["_ACTION_SpecialAirLwEndAir", "_ACTION_SpecialAirLwEndAir_figatree"],
            use_rl_suffix=False,
            excludes=down_air_excl,
        ),
    )
    if down_air.start is None and down_air.loop is None and down_air.main is None and down_air.hit is None and down_air.end is None:
        down_air = None

    # Chain stages: any scanned _ACTION_Special* figatree whose msid is not already covered by
    # the slot scheme is a chain/auxiliary special submotion (Marth: SpecialS2Hi..S4Lw stages and
    # SpecialNEnd1). Source-driven by the anim bank, not per-char literals.
    covered: set[int] = set()
    for ph in (neutral_ground, neutral_air, side_ground, side_air, up_ground, up_air, down_ground,
               down_air):
        if ph is None:
            continue
        for slot in (ph.start, ph.loop, ph.main, ph.hit, ph.end, ph.hold):
            if slot is None:
                continue
            for v in (slot.default, slot.left, slot.right):
                if v is not None:
                    covered.add(int(v))
    extra = sorted(
        int(msid)
        for msid, name in entries
        if "_ACTION_Special" in name and "FallSpecial" not in name and int(msid) not in covered
    )
    # Fox/Falco: the long-validated runtime special owners (blaster.c/physics.c SpecialHi
    # Bound/Landing handling) consume the ABSENCE of script data for the slot-scheme leftovers
    # (e.g. SpecialHiBound root-motion phys, HIS:562 lock). Do not retroactively add their
    # leftover msids; chain extras exist for multi-stage specials (Marth's Dancing Blade family).
    if character in ("fox", "falco"):
        extra = []

    return SpecialMsids(
        neutral_ground=neutral_ground,
        neutral_air=neutral_air,
        side_ground=side_ground,
        side_air=side_air,
        up_ground=up_ground,
        up_air=up_air,
        down_ground=down_ground,
        down_air=down_air,
        extra_script_msids=extra or None,
    )


def main() -> None:
    ap = argparse.ArgumentParser(description="Extract per-character special submotion (msid) mappings from ftData tables (ISO-derived).")
    ap.add_argument(
        "--iso-dir",
        "--iso_dir",
        dest="iso_dir",
        type=Path,
        default=Path("_iso"),
        help="directory containing extracted *.dat files (default: _iso)",
    )
    ap.add_argument("--chars", type=str, default=None, help="comma-separated characters (fox,falco,...)")
    ap.add_argument("--character", type=str, default=None, help="single character (overridden by --chars)")
    ap.add_argument(
        "--out-dir",
        "--out_dir",
        dest="out_dir",
        type=Path,
        default=Path("data/special_msids"),
        help="output directory for JSON mappings",
    )
    args = ap.parse_args()

    if args.chars:
        chars = [c.strip() for c in args.chars.split(",") if c.strip()]
    elif args.character:
        chars = [args.character]
    else:
        chars = ["fox", "falco", "sheik", "peach", "marth", "puff", "falcon"]

    extract_fighter_anims.ISO_DIR = Path(args.iso_dir)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    for ch in chars:
        ms = extract_special_msids(character=ch)
        out_path = args.out_dir / f"{ch}.json"
        out_path.write_text(json.dumps(asdict(ms), indent=2, sort_keys=True) + "\n")
        print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
