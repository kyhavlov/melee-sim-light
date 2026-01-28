from __future__ import annotations

import argparse
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path


_U16_MAX = 0xFFFF
_U32_MAX = 0xFFFF_FFFF


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

def _parse_motion_flags_constants(headers: list[Path]) -> dict[str, str]:
    """
    Parse `static MotionFlags const NAME = EXPR;` definitions from decomp headers.

    We store the raw expression string; callers can evaluate lazily with _eval_motion_flags_expr.
    """

    out: dict[str, str] = {}

    # Match a definition start. The RHS can span multiple lines until ';'.
    # Example:
    #   static MotionFlags const ftCo_MF_Rebirth =
    #       Ft_MF_SkipModelPartVis | Ft_MF_SkipMetalB;
    head_pat = re.compile(r"^\s*static\s+MotionFlags\s+const\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?P<rhs>.*)$")

    for header in headers:
        lines = header.read_text(encoding="utf-8", errors="replace").splitlines()
        i = 0
        while i < len(lines):
            line = lines[i]
            m = head_pat.match(line)
            if m is None:
                i += 1
                continue

            name = m.group("name")
            rhs = m.group("rhs").split("//", 1)[0].strip()

            # Accumulate until ';' appears.
            expr_parts: list[str] = []
            if rhs:
                # Fast path: single-line definition.
                if ";" in rhs:
                    expr = rhs.split(";", 1)[0].strip()
                    if expr:
                        out[name] = expr
                    i += 1
                    continue
                expr_parts.append(rhs)
            i += 1
            while i < len(lines):
                cur = lines[i]
                # Strip // comments, preserve operators.
                cur = cur.split("//", 1)[0].strip()
                if cur:
                    expr_parts.append(cur)
                if ";" in cur:
                    break
                i += 1

            expr = " ".join(expr_parts)
            expr = expr.split(";", 1)[0].strip()
            if expr:
                out[name] = expr
            i += 1

    if not out:
        raise RuntimeError("failed to parse any MotionFlags constants from headers")
    return out


class _Token:
    __slots__ = ("kind", "value")

    def __init__(self, kind: str, value: str) -> None:
        self.kind = kind
        self.value = value


def _tokenize_motion_flags_expr(expr: str) -> list[_Token]:
    # Very small expression language: identifiers, integer literals, '|', '<<', '(' , ')'.
    s = expr.strip()
    out: list[_Token] = []
    i = 0
    while i < len(s):
        c = s[i]
        if c.isspace():
            i += 1
            continue
        if c == "(":
            out.append(_Token("LPAREN", c))
            i += 1
            continue
        if c == ")":
            out.append(_Token("RPAREN", c))
            i += 1
            continue
        if c == "|":
            out.append(_Token("OR", c))
            i += 1
            continue
        if c == "<" and i + 1 < len(s) and s[i + 1] == "<":
            out.append(_Token("SHL", "<<"))
            i += 2
            continue
        if c.isdigit():
            j = i + 1
            while j < len(s) and (s[j].isalnum() or s[j] in "xX_"):
                j += 1
            out.append(_Token("NUM", s[i:j]))
            i = j
            continue
        if c.isalpha() or c == "_":
            j = i + 1
            while j < len(s) and (s[j].isalnum() or s[j] == "_"):
                j += 1
            out.append(_Token("IDENT", s[i:j]))
            i = j
            continue
        raise RuntimeError(f"unexpected token char {c!r} in MotionFlags expr: {expr!r}")
    return out


def _eval_motion_flags_expr(expr: str, expr_by_name: dict[str, str]) -> int:
    """
    Evaluate a MotionFlags expression to a u32.

    Supported ops: '|', '<<', parentheses, integer literals (hex/dec), identifiers.
    Identifiers can refer to other MotionFlags constants in expr_by_name.
    """

    tokens = _tokenize_motion_flags_expr(expr)
    pos = 0

    def peek() -> _Token | None:
        nonlocal pos
        return tokens[pos] if pos < len(tokens) else None

    def take(kind: str) -> _Token:
        nonlocal pos
        t = peek()
        if t is None or t.kind != kind:
            raise RuntimeError(f"expected {kind}, got {t.kind if t else 'EOF'} in MotionFlags expr: {expr!r}")
        pos += 1
        return t

    # Resolve identifiers via a caller-managed symbol cache so recursive definitions are handled
    # deterministically and without exponential re-parsing.
    # The cache is attached to expr_by_name to keep this module self-contained.
    # (We only ever call this from a single-threaded extraction script.)
    cache = expr_by_name.setdefault("__MSL_EVAL_CACHE__", {})  # type: ignore[assignment]
    visiting = expr_by_name.setdefault("__MSL_EVAL_VISITING__", set())  # type: ignore[assignment]

    def eval_ident(name: str) -> int:
        if name in cache:
            return int(cache[name])
        if name in visiting:
            raise RuntimeError(f"cycle in MotionFlags constants at {name!r}")
        rhs = expr_by_name.get(name)
        if rhs is None or name.startswith("__MSL_EVAL_"):
            raise RuntimeError(f"unknown MotionFlags identifier {name!r} (from expr {expr!r})")
        visiting.add(name)
        v = _eval_motion_flags_expr(rhs, expr_by_name) & _U32_MAX
        visiting.remove(name)
        cache[name] = int(v)
        return int(v)

    def parse_primary() -> int:
        t = peek()
        if t is None:
            raise RuntimeError(f"unexpected EOF in MotionFlags expr: {expr!r}")
        if t.kind == "LPAREN":
            take("LPAREN")
            v = parse_or()
            take("RPAREN")
            return v
        if t.kind == "NUM":
            take("NUM")
            return int(t.value.replace("_", ""), 0) & _U32_MAX
        if t.kind == "IDENT":
            take("IDENT")
            return eval_ident(t.value) & _U32_MAX
        raise RuntimeError(f"unexpected token {t.kind} in MotionFlags expr: {expr!r}")

    def parse_shift() -> int:
        v = parse_primary()
        t = peek()
        if t is not None and t.kind == "SHL":
            take("SHL")
            n_tok = take("NUM")
            sh = int(n_tok.value.replace("_", ""), 0)
            v = (v << sh) & _U32_MAX
        return v

    def parse_or() -> int:
        v = parse_shift()
        while True:
            t = peek()
            if t is None or t.kind != "OR":
                break
            take("OR")
            v = (v | parse_shift()) & _U32_MAX
        return v

    out = parse_or() & _U32_MAX
    if pos != len(tokens):
        raise RuntimeError(f"trailing tokens in MotionFlags expr: {expr!r}")
    return out


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


def _parse_action_id_to_motion_flags_u32(src: Path, motion_flags_expr_by_name: dict[str, str]) -> dict[int, int]:
    """Parse decomp MotionState tables into a mapping: action_id -> MotionState.x4_flags (u32).

    Decomp pointers (GALE01):
    - MotionState definition:
      refs/melee/src/melee/ft/types.h::MotionState (x4_flags at +0x4)
    - Common MotionState table:
      refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
    - Fox/Falco self MotionState tables:
      refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
      refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c::ftFc_Init_MotionStateTable
    """
    lines = src.read_text(encoding="utf-8", errors="replace").splitlines()

    act_pat = re.compile(r"^\s*//\s*(?P<sym>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?P<id>[0-9]+)\s*$")

    pending_action_id: int | None = None
    pending_field_index = 0
    pending_flags_expr: str | None = None
    out: dict[int, int] = {}

    for line in lines:
        m = act_pat.match(line)
        if m is not None:
            pending_action_id = int(m.group("id"), 10)
            pending_field_index = 0
            pending_flags_expr = None
            continue

        if pending_action_id is None:
            continue

        code = line.split("//", 1)[0].strip()
        if not code:
            continue
        if code.startswith("{") or code.startswith("}"):
            continue

        # Take the first top-level field token on this line (fields are emitted 1-per-line in decomp).
        tok = code.split(",", 1)[0].strip()
        if not tok:
            continue

        if pending_field_index == 0:
            # anim_id (unused here)
            pending_field_index = 1
            continue
        if pending_field_index == 1 and pending_flags_expr is None:
            pending_flags_expr = tok
            # Do not finalize here; the entry continues, but x4_flags is now known.
            pending_field_index = 2
            # We can commit immediately since the action id is unique per entry.
            out[pending_action_id] = int(_eval_motion_flags_expr(pending_flags_expr, motion_flags_expr_by_name))
            pending_action_id = None
            pending_flags_expr = None
            pending_field_index = 0
            continue

    if not out:
        raise RuntimeError(f"failed to parse any action_id->x4_flags entries from {src}")
    return out


def _write_action_move_id_bin(out_path: Path, move_id_by_action: dict[int, int], flags_by_action: dict[int, int]) -> None:
    # File format v2:
    #   u8  magic[8] = "MSLACID1"
    #   u32 version = 2
    #   u16 action_count          (table length; action_id is the index)
    #   u16 reserved = 0
    #   u32 move_toc_off          (byte offset to u16 move_id table)
    #   u32 flags_toc_off         (byte offset to u32 x4_flags table)
    #   u32 file_bytes
    #   u16 move_id[action_count] (0xFFFF means "unknown/absent"; runtime should treat as Default)
    #   u32 x4_flags[action_count] (decomp MotionState.x4_flags)
    magic = b"MSLACID1"
    version = 2
    max_action = max(max(move_id_by_action.keys(), default=0), max(flags_by_action.keys(), default=0))
    action_count = int(max_action) + 1
    hdr_bytes = 8 + 4 + 2 + 2 + 4 + 4 + 4
    move_toc_off = hdr_bytes
    flags_toc_off = move_toc_off + action_count * 2
    file_bytes = flags_toc_off + action_count * 4

    table = [_U16_MAX] * action_count
    for action_id, move_id in move_id_by_action.items():
        if action_id < 0 or action_id >= action_count:
            raise ValueError(f"action_id out of range: {action_id}")
        table[int(action_id)] = int(move_id) & 0xFFFF

    flags = [0] * action_count
    for action_id, mf in flags_by_action.items():
        if action_id < 0 or action_id >= action_count:
            raise ValueError(f"action_id out of range: {action_id}")
        flags[int(action_id)] = int(mf) & _U32_MAX

    buf = bytearray()
    buf += magic
    buf += _u32_le(version)
    buf += _u16_le(action_count)
    buf += _u16_le(0)
    buf += _u32_le(move_toc_off)
    buf += _u32_le(flags_toc_off)
    buf += _u32_le(file_bytes)
    for mv in table:
        buf += _u16_le(mv)
    for mf in flags:
        buf += _u32_le(mf)

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

    # MotionFlags constants needed to evaluate MotionState.x4_flags.
    # Decomp pointers:
    # - Ft_MF_* base flags: refs/melee/src/melee/ft/forward.h
    # - Common ftCo_MF_* composites: refs/melee/src/melee/ft/chara/ftCommon/forward.h
    # - Fox/Falco ftFx_MF_* composites: refs/melee/src/melee/ft/chara/ftFox/forward.h
    motion_flags_expr_by_name = _parse_motion_flags_constants(
        [
            melee / "src" / "melee" / "ft" / "forward.h",
            melee / "src" / "melee" / "ft" / "chara" / "ftCommon" / "forward.h",
            melee / "src" / "melee" / "ft" / "chara" / "ftFox" / "forward.h",
        ]
    )

    common_src = melee / "src" / "melee" / "ft" / "ftmotionstates.c"
    if not common_src.exists():
        raise SystemExit(f"missing common MotionState table: {common_src}")
    common = _parse_action_id_to_move_id(common_src, ft_move_id)
    common_flags = _parse_action_id_to_motion_flags_u32(common_src, motion_flags_expr_by_name)

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
        self_flags = _parse_action_id_to_motion_flags_u32(spec.src, motion_flags_expr_by_name)
        merged: dict[int, int] = dict(common)
        merged_flags: dict[int, int] = dict(common_flags)
        for k, v in self_tab.items():
            if k in merged and merged[k] != v:
                raise SystemExit(f"conflicting move_id for action_id={k} ({merged[k]} != {v}) in {spec.src}")
            merged[k] = v
        for k, v in self_flags.items():
            if k in merged_flags and merged_flags[k] != v:
                raise SystemExit(
                    f"conflicting x4_flags for action_id={k} ({merged_flags[k]:#x} != {v:#x}) in {spec.src}"
                )
            merged_flags[k] = v

        out_bin = args.out_dir / f"{spec.rel_name}.bin"
        _write_action_move_id_bin(out_bin, merged, merged_flags)

        if args.debug_json:
            out_json = args.out_dir / f"{spec.rel_name}.json"
            out_json.write_text(
                json.dumps(
                    {
                        str(k): {"move_id": int(merged[k]), "x4_flags": int(merged_flags.get(k, 0))}
                        for k in sorted(merged.keys())
                    },
                    indent=2,
                )
            )


if __name__ == "__main__":
    main()
