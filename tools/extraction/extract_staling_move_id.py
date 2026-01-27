from __future__ import annotations

import argparse
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path


_U16_MAX = 0xFFFF


@dataclass(frozen=True)
class _MotionStateEntry:
    anim_id_sym: str
    move_id_sym: str
    src: str


def _parse_c_enum(header: Path, *, enum_typedef: str, prefix: str) -> dict[str, int]:
    txt = header.read_text(encoding="utf-8", errors="replace").splitlines()

    in_enum = False
    value: int | None = None
    out: dict[str, int] = {}
    for line in txt:
        if enum_typedef in line:
            in_enum = True
            continue
        if not in_enum:
            continue
        if "}" in line and ";" in line:
            break

        s = line.split("//", 1)[0].strip()
        if not s or not s.startswith(prefix):
            continue
        s = s.rstrip(",")
        if "=" in s:
            name, rhs = [x.strip() for x in s.split("=", 1)]
            try:
                value = int(rhs, 0)
            except ValueError:
                # Some enums reference other symbols (e.g. `... = ftCo_SM_Count`).
                continue
            out[name] = value
        else:
            if value is None:
                value = 0
            else:
                value += 1
            out[s] = value

    if not out:
        raise RuntimeError(f"failed to parse enum {enum_typedef!r} with prefix {prefix!r} from {header}")
    return out


def _parse_ftco_submotion_enum(melee_decomp_root: Path) -> dict[str, int]:
    header = melee_decomp_root / "src" / "melee" / "ft" / "chara" / "ftCommon" / "forward.h"
    return _parse_c_enum(header, enum_typedef="typedef enum ftCo_Submotion", prefix="ftCo_SM_")


def _parse_ft_move_id_enum(melee_decomp_root: Path) -> dict[str, int]:
    header = melee_decomp_root / "src" / "melee" / "ft" / "forward.h"
    return _parse_c_enum(header, enum_typedef="typedef enum FtMoveId", prefix="FtMoveId_")


def _parse_char_submotion_enum(melee_decomp_root: Path, char_dir: str, *, typedef: str, prefix: str) -> dict[str, int]:
    header = melee_decomp_root / "src" / "melee" / "ft" / "chara" / char_dir / "forward.h"
    if not header.exists():
        return {}
    return _parse_c_enum(header, enum_typedef=typedef, prefix=prefix)


def _parse_motion_state_table_entries(src: Path) -> list[_MotionStateEntry]:
    txt = src.read_text(encoding="utf-8", errors="replace")

    # Decomp shape:
    # struct MotionState (refs/melee/src/melee/ft/types.h::MotionState):
    #   - anim_id (submotion id)
    #   - flags
    #   - u32 union where move_id is stored in the top byte (initializer uses `FtMoveId_* << 24`)
    #
    # We intentionally only pluck:
    #   - field 0: anim_id symbol (ftCo_SM_* / ftFx_SM_* / ftFc_SM_* ...)
    #   - field 2: the first `FtMoveId_*` token present in the expression
    #
    # This regex assumes the decomp formatting where the first 3 fields are one per line.
    pat = re.compile(
        r"\{\s*\n"
        r"(?:\s*//[^\n]*\n)?"
        r"\s*(?P<anim>[A-Za-z_][A-Za-z0-9_]*)\s*,\s*\n"
        r"\s*(?:[^,\n]+)\s*,\s*\n"
        r"\s*(?P<expr>[^,\n]+)\s*,",
        re.MULTILINE,
    )

    out: list[_MotionStateEntry] = []
    for m in pat.finditer(txt):
        anim = m.group("anim")
        expr = m.group("expr")
        mm = re.search(r"\b(FtMoveId_[A-Za-z0-9_]+)\b", expr)
        if mm is None:
            continue
        out.append(_MotionStateEntry(anim_id_sym=anim, move_id_sym=mm.group(1), src=str(src)))
    if not out:
        raise RuntimeError(f"failed to find MotionState entries in {src}")
    return out


def _u16_le(v: int) -> bytes:
    return struct.pack("<H", v & 0xFFFF)


def _u32_le(v: int) -> bytes:
    return struct.pack("<I", v & 0xFFFF_FFFF)


def _write_move_id_bin(out_path: Path, entries: list[tuple[int, int]]) -> None:
    # File format v1:
    #   u8  magic[8] = "MSLSTID1"
    #   u32 version = 1
    #   u16 entry_count
    #   u16 reserved = 0
    #   u32 toc_off (byte offset to entry table; currently fixed)
    #   u32 file_bytes
    #   entry[entry_count] where entry := { u16 msid; u16 move_id; }
    #
    # Sentinel: move_id==0xFFFF means "ambiguous / unknown; do not attribute staling".
    magic = b"MSLSTID1"
    version = 1
    entry_count = len(entries)
    toc_off = 8 + 4 + 2 + 2 + 4 + 4
    file_bytes = toc_off + entry_count * 4

    buf = bytearray()
    buf += magic
    buf += _u32_le(version)
    buf += _u16_le(entry_count)
    buf += _u16_le(0)
    buf += _u32_le(toc_off)
    buf += _u32_le(file_bytes)
    for msid, move_id in entries:
        buf += _u16_le(msid)
        buf += _u16_le(move_id)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(bytes(buf))


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Extract (char, msid)->FtMoveId mapping for staling attribution (decomp-first)."
    )
    ap.add_argument("--melee_decomp", type=Path, default=Path("refs/melee"))
    ap.add_argument("--out_dir", type=Path, default=Path("data/staling/move_id"))
    ap.add_argument("--chars", type=str, default="fox,falco", help="comma-separated character set to extract")
    ap.add_argument(
        "--debug-json",
        "--debug_json",
        dest="debug_json",
        action="store_true",
        help="also write a human-readable JSON mapping next to each .bin (debug-only; not used by runtime)",
    )
    args = ap.parse_args()

    melee = args.melee_decomp
    ftco_sm = _parse_ftco_submotion_enum(melee)
    ft_move_id = _parse_ft_move_id_enum(melee)

    # Common motionstate table: contains the bulk of actionable moves (jabs, tilts, aerials, throws, etc.).
    common_src = melee / "src" / "melee" / "ft" / "ftmotionstates.c"
    common_entries = _parse_motion_state_table_entries(common_src)

    char_specs = {
        "fox": {
            "char_dir": "ftFox",
            "init_src": melee / "src" / "melee" / "ft" / "chara" / "ftFox" / "ftFx_Init.c",
            "submotion_typedef": "typedef enum ftFx_Submotion",
            "submotion_prefix": "ftFx_SM_",
        },
        "falco": {
            "char_dir": "ftFalco",
            "init_src": melee / "src" / "melee" / "ft" / "chara" / "ftFalco" / "ftFc_Init.c",
            "submotion_typedef": "typedef enum ftFc_Submotion",
            "submotion_prefix": "ftFc_SM_",
        },
    }

    for ch in [c.strip() for c in args.chars.split(",") if c.strip()]:
        spec = char_specs.get(ch)
        if spec is None:
            raise SystemExit(f"unsupported character for now: {ch!r}")
        init_src: Path = spec["init_src"]
        if not init_src.exists():
            raise SystemExit(f"missing decomp init table for {ch}: {init_src}")

        char_sm = _parse_char_submotion_enum(
            melee,
            spec["char_dir"],
            typedef=spec["submotion_typedef"],
            prefix=spec["submotion_prefix"],
        )

        # Merge the submotion namespace needed to resolve MotionState.anim_id.
        sm_by_sym: dict[str, int] = {}
        sm_by_sym.update(ftco_sm)
        sm_by_sym.update(char_sm)

        # Merge MotionState tables: ftCo common + character self states.
        all_entries: list[_MotionStateEntry] = []
        all_entries.extend(common_entries)
        all_entries.extend(_parse_motion_state_table_entries(init_src))

        # Resolve to numeric ids, tracking ambiguity.
        msid_to_move: dict[int, int] = {}
        ambiguous: dict[int, set[int]] = {}
        dropped: list[dict[str, object]] = []

        for e in all_entries:
            msid = sm_by_sym.get(e.anim_id_sym)
            if msid is None:
                dropped.append({"anim_id_sym": e.anim_id_sym, "move_id_sym": e.move_id_sym, "src": e.src})
                continue
            mv = ft_move_id.get(e.move_id_sym)
            if mv is None:
                dropped.append({"anim_id_sym": e.anim_id_sym, "move_id_sym": e.move_id_sym, "src": e.src})
                continue

            prev = msid_to_move.get(msid)
            if prev is None:
                msid_to_move[msid] = int(mv)
                continue
            if prev != int(mv):
                ambiguous.setdefault(msid, set()).update([prev, int(mv)])
                msid_to_move[msid] = _U16_MAX

        out_entries = sorted(msid_to_move.items(), key=lambda kv: kv[0])
        out_path = args.out_dir / f"{ch}.bin"
        _write_move_id_bin(out_path, out_entries)

        if args.debug_json:
            js_path = args.out_dir / f"{ch}.json"
            js = {
                "schema_version": 1,
                "character": ch,
                "mapping": {str(msid): int(move_id) for msid, move_id in out_entries},
                "ambiguous_msids": {str(msid): sorted(int(x) for x in moves) for msid, moves in ambiguous.items()},
                "dropped": dropped,
                "note": "move_id==65535 means ambiguous/unknown; do not attribute staling.",
                "sources": {
                    "motion_state_struct": "refs/melee/src/melee/ft/types.h::MotionState",
                    "common_table": str(common_src),
                    "char_table": str(init_src),
                    "ft_move_id_enum": "refs/melee/src/melee/ft/forward.h::FtMoveId",
                },
            }
            js_path.write_text(json.dumps(js, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
