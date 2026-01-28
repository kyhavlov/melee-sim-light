from __future__ import annotations

import argparse
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path


_U16_MAX = 0xFFFF


def _u16_le(v: int) -> bytes:
    return struct.pack("<H", v & 0xFFFF)


def _u32_le(v: int) -> bytes:
    return struct.pack("<I", v & 0xFFFF_FFFF)


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
            out[name] = int(rhs, 0)
            value = out[name]
        else:
            if value is None:
                value = 0
            else:
                value += 1
            out[s] = value

    if not out:
        raise RuntimeError(f"failed to parse enum {enum_typedef!r} with prefix {prefix!r} from {header}")
    return out


def _parse_ft_move_id_enum(melee_decomp_root: Path) -> dict[str, int]:
    header = melee_decomp_root / "src" / "melee" / "ft" / "forward.h"
    return _parse_c_enum(header, enum_typedef="typedef enum FtMoveId", prefix="FtMoveId_")


@dataclass(frozen=True)
class _TableSpec:
    name: str
    src: Path
    rel_name: str


def _parse_action_id_to_move_id(src: Path, ft_move_id: dict[str, int]) -> dict[int, int]:
    """Parse decomp MotionState tables into a mapping: action_id -> FtMoveId.

    Decomp pointers (GALE01):
    - Common MotionState table:
      refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
    - Fox/Falco self MotionState tables:
      refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
      refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c::ftFc_Init_MotionStateTable
    """
    lines = src.read_text(encoding="utf-8", errors="replace").splitlines()

    # Decomp formatting includes per-entry comments:
    #   // ftCo_MS_DeadDown = 0
    #   ...
    #   FtMoveId_Default << 24,
    #
    # and for self states:
    #   // ftFx_MS_SpecialNStart = 341
    #   ...
    #   FtMoveId_SpecialN << 24,
    act_pat = re.compile(r"^\s*//\s*(?P<sym>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?P<id>[0-9]+)\s*$")
    # MotionState.move_id is stored as FtMoveId_* << 24 in the decomp initializer.
    # Match only the move_id field expression (avoid accidentally grabbing other FtMoveId_* tokens).
    move_pat = re.compile(r"\b(?P<sym>FtMoveId_[A-Za-z0-9_]+)\b\s*<<\s*24\b")

    pending_action_id: int | None = None
    pending_sym: str | None = None
    out: dict[int, int] = {}
    for line in lines:
        m = act_pat.match(line)
        if m is not None:
            pending_sym = m.group("sym")
            pending_action_id = int(m.group("id"), 10)
            continue
        if pending_action_id is None:
            continue
        code = line.split("//", 1)[0]
        mm = move_pat.search(code)
        if mm is None:
            continue
        move_sym = mm.group("sym")
        mv = ft_move_id.get(move_sym)
        if mv is None:
            raise RuntimeError(f"unresolved FtMoveId symbol {move_sym!r} in {src} (for {pending_sym})")
        if pending_action_id in out and out[pending_action_id] != int(mv):
            raise RuntimeError(
                f"conflicting move_id for action_id {pending_action_id} in {src}: "
                f"{out[pending_action_id]} != {int(mv)}"
            )
        out[pending_action_id] = int(mv)
        pending_action_id = None
        pending_sym = None

    if not out:
        raise RuntimeError(f"failed to parse any action_id->move_id entries from {src}")
    return out


def _write_action_move_id_bin(out_path: Path, move_id_by_action: dict[int, int]) -> None:
    # File format v1:
    #   u8  magic[8] = "MSLACID1"
    #   u32 version = 1
    #   u16 action_count          (table length; action_id is the index)
    #   u16 reserved = 0
    #   u32 toc_off               (byte offset to u16 table; currently fixed)
    #   u32 file_bytes
    #   u16 move_id[action_count] (0xFFFF means "unknown/absent"; runtime should treat as Default)
    magic = b"MSLACID1"
    version = 1
    max_action = max(move_id_by_action.keys())
    action_count = int(max_action) + 1
    toc_off = 8 + 4 + 2 + 2 + 4 + 4
    file_bytes = toc_off + action_count * 2

    table = [_U16_MAX] * action_count
    for action_id, move_id in move_id_by_action.items():
        if action_id < 0 or action_id >= action_count:
            raise ValueError(f"action_id out of range: {action_id}")
        table[int(action_id)] = int(move_id) & 0xFFFF

    buf = bytearray()
    buf += magic
    buf += _u32_le(version)
    buf += _u16_le(action_count)
    buf += _u16_le(0)
    buf += _u32_le(toc_off)
    buf += _u32_le(file_bytes)
    for mv in table:
        buf += _u16_le(mv)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(bytes(buf))


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Extract (char, action_id)->FtMoveId mapping for fighter attack identity (decomp-first)."
    )
    ap.add_argument("--melee_decomp", type=Path, default=Path("refs/melee"))
    ap.add_argument("--out_dir", type=Path, default=Path("data/attack_id/move_id"))
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
    ft_move_id = _parse_ft_move_id_enum(melee)

    common_src = melee / "src" / "melee" / "ft" / "ftmotionstates.c"
    if not common_src.exists():
        raise SystemExit(f"missing common MotionState table: {common_src}")
    common = _parse_action_id_to_move_id(common_src, ft_move_id)

    specs = {
        "fox": _TableSpec(
            name="fox",
            src=melee / "src" / "melee" / "ft" / "chara" / "ftFox" / "ftFx_Init.c",
            rel_name="fox",
        ),
        "falco": _TableSpec(
            name="falco",
            src=melee / "src" / "melee" / "ft" / "chara" / "ftFalco" / "ftFc_Init.c",
            rel_name="falco",
        ),
    }

    for ch in [c.strip() for c in args.chars.split(",") if c.strip()]:
        spec = specs.get(ch)
        if spec is None:
            raise SystemExit(f"unsupported character for now: {ch!r}")
        if not spec.src.exists():
            raise SystemExit(f"missing MotionState table for {ch}: {spec.src}")

        self_tab = _parse_action_id_to_move_id(spec.src, ft_move_id)
        merged: dict[int, int] = dict(common)
        for k, v in self_tab.items():
            if k in merged and merged[k] != v:
                raise SystemExit(f"conflicting move_id for action_id={k} ({merged[k]} != {v}) in {spec.src}")
            merged[k] = v

        out_bin = args.out_dir / f"{spec.rel_name}.bin"
        _write_action_move_id_bin(out_bin, merged)

        if args.debug_json:
            out_json = args.out_dir / f"{spec.rel_name}.json"
            out_json.write_text(json.dumps({str(k): int(v) for k, v in sorted(merged.items())}, indent=2))


if __name__ == "__main__":
    main()
