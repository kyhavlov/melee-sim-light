from __future__ import annotations

import argparse
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path

from tools.extraction.extract_attack_id_move_id import (
    _eval_motion_flags_expr,
    _parse_ft_move_id_enum,
    _parse_motion_flags_constants,
    _u16_le,
    _u32_le,
)


FORMAT_MAGIC = b"MSLMSO01"
FORMAT_VERSION = 1
U16_ABSENT = 0xFFFF

CLASS_ATTACK_AIR = 1 << 0
CLASS_ATTACK_S3 = 1 << 1
CLASS_ATTACK_S4 = 1 << 2
CLASS_DAMAGE_COMMON = 1 << 3
CLASS_DAMAGE_FLY = 1 << 4
CLASS_LANDING_AIR = 1 << 5
CLASS_COMMON_FALL = 1 << 6
CLASS_SPECIALHI = 1 << 7


@dataclass(frozen=True)
class MotionStateRow:
    action_id: int
    symbol: str
    submotion_id: int
    x4_flags: int
    motion_state_word: int
    anim_cb: str
    iasa_cb: str
    phys_cb: str
    coll_cb: str
    cam_cb: str
    class_bits: int


@dataclass(frozen=True)
class TableSpec:
    src: Path
    rel_name: str


def _strip_comments(line: str) -> str:
    return line.split("//", 1)[0].strip()


def _parse_enum(header: Path, *, enum_typedef: str, prefixes: tuple[str, ...], symbols: dict[str, int]) -> dict[str, int]:
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

        s = _strip_comments(line).rstrip(",")
        if not s or not s.startswith(prefixes):
            continue
        if "=" in s:
            name, rhs = [x.strip() for x in s.split("=", 1)]
            if not re.fullmatch(r"[A-Za-z0-9_()+\- \t]+", rhs):
                raise RuntimeError(f"unsupported enum expression {rhs!r} in {header}")
            env = dict(symbols)
            env.update(out)
            value = int(eval(rhs, {"__builtins__": {}}, env))  # noqa: S307 - trusted local decomp
            out[name] = value
        else:
            if value is None:
                value = 0
            else:
                value += 1
            out[s] = value

    if not out:
        raise RuntimeError(f"failed to parse enum {enum_typedef!r} from {header}")
    return out


def _parse_submotion_ids(melee_decomp_root: Path) -> dict[str, int]:
    common_forward = melee_decomp_root / "src" / "melee" / "ft" / "chara" / "ftCommon" / "forward.h"
    fox_forward = melee_decomp_root / "src" / "melee" / "ft" / "chara" / "ftFox" / "forward.h"

    out: dict[str, int] = {}
    out.update(_parse_enum(common_forward, enum_typedef="typedef enum ftCo_Submotion", prefixes=("ftCo_SM_",), symbols=out))
    out.update(_parse_enum(fox_forward, enum_typedef="typedef enum ftFx_Submotion", prefixes=("ftFx_SM_",), symbols=out))
    # Falco shares Fox's special submotion enum names in this decomp tree.
    return out


def _class_bits_for_callbacks(callbacks: tuple[str, str, str, str, str]) -> int:
    bits = 0
    if any(cb.startswith("ftCo_AttackAir") for cb in callbacks):
        bits |= CLASS_ATTACK_AIR
    if any(cb.startswith("ftCo_AttackS3") for cb in callbacks):
        bits |= CLASS_ATTACK_S3
    if any(cb.startswith("ftCo_AttackS4") for cb in callbacks):
        bits |= CLASS_ATTACK_S4
    if any(cb.startswith("ftCo_Damage_") or cb.startswith("ftCo_DownDamage_") for cb in callbacks):
        bits |= CLASS_DAMAGE_COMMON
    if any(cb.startswith("ftCo_DamageFly") or cb.startswith("ftCo_FlyReflect_") for cb in callbacks):
        bits |= CLASS_DAMAGE_FLY
    if any(cb.startswith("ftCo_LandingAir") for cb in callbacks):
        bits |= CLASS_LANDING_AIR
    if any(cb.startswith("ftCo_Fall") for cb in callbacks):
        bits |= CLASS_COMMON_FALL
    if any(cb.startswith("ftFx_SpecialHi") or cb.startswith("ftFx_SpecialAirHi") for cb in callbacks):
        bits |= CLASS_SPECIALHI
    return bits


def _parse_motion_state_rows(
    src: Path,
    *,
    submotion_ids: dict[str, int],
    ft_move_id: dict[str, int],
    motion_flags_expr_by_name: dict[str, str],
) -> dict[int, MotionStateRow]:
    lines = src.read_text(encoding="utf-8", errors="replace").splitlines()
    act_pat = re.compile(r"^\s*//\s*(?P<sym>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?P<id>[0-9]+)\s*$")
    symbols: dict[str, str] = dict(motion_flags_expr_by_name)
    symbols.update({name: str(value) for name, value in ft_move_id.items()})

    out: dict[int, MotionStateRow] = {}
    pending_action_id: int | None = None
    pending_symbol = ""
    fields: list[str] = []

    def finish() -> None:
        nonlocal pending_action_id, pending_symbol, fields
        if pending_action_id is None:
            return
        if len(fields) < 8:
            raise RuntimeError(f"incomplete MotionState row {pending_symbol} in {src}: fields={fields!r}")
        submotion_sym = fields[0]
        submotion_id = submotion_ids.get(submotion_sym, U16_ABSENT)
        if submotion_id == U16_ABSENT and submotion_sym not in {"0", "NULL"}:
            raise RuntimeError(f"unresolved submotion symbol {submotion_sym!r} in {src} ({pending_symbol})")
        x4_flags = int(_eval_motion_flags_expr(fields[1], motion_flags_expr_by_name))
        motion_word = int(_eval_motion_flags_expr(fields[2], symbols))
        callbacks = tuple("NULL" if f == "NULL" else f for f in fields[3:8])
        row = MotionStateRow(
            action_id=pending_action_id,
            symbol=pending_symbol,
            submotion_id=int(submotion_id) & 0xFFFF,
            x4_flags=x4_flags & 0xFFFF_FFFF,
            motion_state_word=motion_word & 0xFFFF_FFFF,
            anim_cb=callbacks[0],
            iasa_cb=callbacks[1],
            phys_cb=callbacks[2],
            coll_cb=callbacks[3],
            cam_cb=callbacks[4],
            class_bits=_class_bits_for_callbacks(callbacks),
        )
        if row.action_id in out:
            raise RuntimeError(f"duplicate action id {row.action_id} in {src}")
        out[row.action_id] = row
        pending_action_id = None
        pending_symbol = ""
        fields = []

    for line in lines:
        m = act_pat.match(line)
        if m is not None:
            finish()
            pending_symbol = m.group("sym")
            pending_action_id = int(m.group("id"), 10)
            fields = []
            continue

        if pending_action_id is None:
            continue
        code = _strip_comments(line)
        if not code or code.startswith("{") or code.startswith("}"):
            continue
        tok = code.split(",", 1)[0].strip()
        if tok:
            fields.append(tok)
            if len(fields) == 8:
                finish()

    finish()
    if not out:
        raise RuntimeError(f"failed to parse MotionState rows from {src}")
    return out


def _merge_rows(common: dict[int, MotionStateRow], self_rows: dict[int, MotionStateRow]) -> dict[int, MotionStateRow]:
    out = dict(common)
    for action_id, row in self_rows.items():
        if action_id in out:
            raise RuntimeError(f"self MotionState row duplicates common action id {action_id}")
        out[action_id] = row
    return out


def _write_table(out_path: Path, rows: dict[int, MotionStateRow], callback_ids: dict[str, int]) -> None:
    max_action = max(rows.keys(), default=0)
    action_count = max_action + 1
    hdr_bytes = 8 + 4 + 2 + 2 + 4 * 9
    submotion_off = hdr_bytes
    x4_flags_off = submotion_off + action_count * 2
    motion_word_off = x4_flags_off + action_count * 4
    anim_cb_off = motion_word_off + action_count * 4
    iasa_cb_off = anim_cb_off + action_count * 2
    phys_cb_off = iasa_cb_off + action_count * 2
    coll_cb_off = phys_cb_off + action_count * 2
    cam_cb_off = coll_cb_off + action_count * 2
    class_bits_off = cam_cb_off + action_count * 2
    file_bytes = class_bits_off + action_count * 4

    submotion = [U16_ABSENT] * action_count
    x4_flags = [0] * action_count
    motion_words = [0] * action_count
    anim_cb = [0] * action_count
    iasa_cb = [0] * action_count
    phys_cb = [0] * action_count
    coll_cb = [0] * action_count
    cam_cb = [0] * action_count
    class_bits = [0] * action_count
    for action_id, row in rows.items():
        submotion[action_id] = row.submotion_id
        x4_flags[action_id] = row.x4_flags
        motion_words[action_id] = row.motion_state_word
        anim_cb[action_id] = callback_ids[row.anim_cb]
        iasa_cb[action_id] = callback_ids[row.iasa_cb]
        phys_cb[action_id] = callback_ids[row.phys_cb]
        coll_cb[action_id] = callback_ids[row.coll_cb]
        cam_cb[action_id] = callback_ids[row.cam_cb]
        class_bits[action_id] = row.class_bits

    buf = bytearray()
    buf += FORMAT_MAGIC
    buf += _u32_le(FORMAT_VERSION)
    buf += _u16_le(action_count)
    buf += _u16_le(0)
    for off in (
        submotion_off,
        x4_flags_off,
        motion_word_off,
        anim_cb_off,
        iasa_cb_off,
        phys_cb_off,
        coll_cb_off,
        cam_cb_off,
        class_bits_off,
    ):
        buf += _u32_le(off)
    for value in submotion:
        buf += _u16_le(value)
    for table in (x4_flags, motion_words):
        for value in table:
            buf += _u32_le(value)
    for table in (anim_cb, iasa_cb, phys_cb, coll_cb, cam_cb):
        for value in table:
            buf += _u16_le(value)
    for value in class_bits:
        buf += _u32_le(value)
    if len(buf) != file_bytes:
        raise AssertionError((len(buf), file_bytes))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(bytes(buf))


def _write_manifest(out_path: Path, callback_ids: dict[str, int]) -> None:
    classes = {
        "ATTACK_AIR": CLASS_ATTACK_AIR,
        "ATTACK_S3": CLASS_ATTACK_S3,
        "ATTACK_S4": CLASS_ATTACK_S4,
        "DAMAGE_COMMON": CLASS_DAMAGE_COMMON,
        "DAMAGE_FLY": CLASS_DAMAGE_FLY,
        "LANDING_AIR": CLASS_LANDING_AIR,
        "COMMON_FALL": CLASS_COMMON_FALL,
        "SPECIALHI": CLASS_SPECIALHI,
    }
    symbols = [{"id": int(i), "symbol": sym} for sym, i in sorted(callback_ids.items(), key=lambda kv: kv[1])]
    payload = {
        "magic": FORMAT_MAGIC.decode("ascii"),
        "version": FORMAT_VERSION,
        "id_policy": "0 is NULL; nonzero ids are sorted stable callback symbol names from decomp MotionState rows",
        "classes": classes,
        "symbols": symbols,
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser(description="Extract decomp MotionState owner/callback data.")
    ap.add_argument("--melee_decomp", type=Path, default=Path("refs/melee"))
    ap.add_argument("--out_dir", type=Path, default=Path("data/motion_state/owners"))
    ap.add_argument("--chars", type=str, default="fox,falco")
    args = ap.parse_args()

    chars = [c.strip() for c in args.chars.split(",") if c.strip()]
    supported = {
        "fox": TableSpec(
            src=args.melee_decomp / "src" / "melee" / "ft" / "chara" / "ftFox" / "ftFx_Init.c",
            rel_name="fox",
        ),
        "falco": TableSpec(
            src=args.melee_decomp / "src" / "melee" / "ft" / "chara" / "ftFalco" / "ftFc_Init.c",
            rel_name="falco",
        ),
    }

    common_src = args.melee_decomp / "src" / "melee" / "ft" / "ftmotionstates.c"
    common_headers = [
        args.melee_decomp / "src" / "melee" / "ft" / "forward.h",
        args.melee_decomp / "src" / "melee" / "ft" / "chara" / "ftCommon" / "forward.h",
        args.melee_decomp / "src" / "melee" / "ft" / "chara" / "ftFox" / "forward.h",
    ]
    ft_move_id = _parse_ft_move_id_enum(args.melee_decomp)
    motion_flags = _parse_motion_flags_constants(common_headers)
    submotion_ids = _parse_submotion_ids(args.melee_decomp)
    common_rows = _parse_motion_state_rows(
        common_src,
        submotion_ids=submotion_ids,
        ft_move_id=ft_move_id,
        motion_flags_expr_by_name=motion_flags,
    )

    rows_by_char: dict[str, dict[int, MotionStateRow]] = {}
    all_callback_symbols = {"NULL"}
    for ch in chars:
        spec = supported.get(ch)
        if spec is None:
            raise SystemExit(f"unsupported character for now: {ch}")
        self_rows = _parse_motion_state_rows(
            spec.src,
            submotion_ids=submotion_ids,
            ft_move_id=ft_move_id,
            motion_flags_expr_by_name=motion_flags,
        )
        merged = _merge_rows(common_rows, self_rows)
        rows_by_char[spec.rel_name] = merged
        for row in merged.values():
            all_callback_symbols.update((row.anim_cb, row.iasa_cb, row.phys_cb, row.coll_cb, row.cam_cb))

    callback_ids = {"NULL": 0}
    for i, sym in enumerate(sorted(s for s in all_callback_symbols if s != "NULL"), start=1):
        callback_ids[sym] = i

    for rel_name, rows in rows_by_char.items():
        _write_table(args.out_dir / f"{rel_name}.bin", rows, callback_ids)
    _write_manifest(args.out_dir / "callback_symbols.json", callback_ids)


if __name__ == "__main__":
    main()
