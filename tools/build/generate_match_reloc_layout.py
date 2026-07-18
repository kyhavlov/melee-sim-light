#!/usr/bin/env python3
"""Generate exact pointer-slot metadata for match savestate relocation."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from tools.build.generate_native_dat_layout import Dwarf


_TYPE_RE = re.compile(r"^MSL_RELOC_TYPE\(([A-Z0-9_]+),\s*([A-Za-z0-9_]+)\)$")
_WRAPPERS = {
    "DW_TAG_typedef",
    "DW_TAG_const_type",
    "DW_TAG_volatile_type",
    "DW_TAG_restrict_type",
}


def unwrap(dwarf: Dwarf, die):
    while die is not None and die.tag in _WRAPPERS:
        die = dwarf.ref(die)
    return die


def array_count(dwarf: Dwarf, die) -> int:
    result = 1
    for child in die.children:
        if child.tag != "DW_TAG_subrange_type":
            continue
        if "DW_AT_count" in child.attrs:
            result *= dwarf.integer(child, "DW_AT_count")
        elif "DW_AT_upper_bound" in child.attrs:
            result *= dwarf.integer(child, "DW_AT_upper_bound") + 1
        else:
            return 0
    return result


def byte_size(dwarf: Dwarf, die) -> int:
    die = unwrap(dwarf, die)
    if die is None:
        return 0
    if "DW_AT_byte_size" in die.attrs:
        return dwarf.integer(die, "DW_AT_byte_size")
    return 0


def pointer_offsets(dwarf: Dwarf, die, base: int = 0, seen=None) -> set[int]:
    die = unwrap(dwarf, die)
    if die is None:
        return set()
    if die.tag == "DW_TAG_pointer_type":
        return {base}
    if die.tag == "DW_TAG_array_type":
        elem = dwarf.ref(die)
        count = array_count(dwarf, die)
        stride = byte_size(dwarf, elem)
        result: set[int] = set()
        for index in range(count):
            result.update(pointer_offsets(dwarf, elem, base + index * stride))
        return result
    if die.tag not in {"DW_TAG_structure_type", "DW_TAG_union_type"}:
        return set()
    key = (die.offset, base)
    if seen is None:
        seen = set()
    if key in seen:
        return set()
    seen.add(key)
    result: set[int] = set()
    for member in die.children:
        if member.tag != "DW_TAG_member" or "DW_AT_bit_size" in member.attrs:
            continue
        if dwarf.name(die) == "MslCoreMatch" and dwarf.name(member) in {
            "memory",
            "relocation",
        }:
            continue
        offset = 0
        if die.tag != "DW_TAG_union_type":
            offset = dwarf.integer(member, "DW_AT_data_member_location", 0)
        result.update(pointer_offsets(dwarf, dwarf.ref(member), base + offset, seen))
    seen.remove(key)
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--object", type=Path, required=True)
    parser.add_argument("--types", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    entries: list[tuple[str, str]] = []
    for raw in args.types.read_text().splitlines():
        line = raw.strip()
        if not line:
            continue
        match = _TYPE_RE.fullmatch(line)
        if match is None:
            raise SystemExit(f"invalid relocation type row: {line}")
        entries.append((match.group(1), match.group(2)))

    dwarf = Dwarf(args.object)
    lines = [
        "/* Generated from canonical C declarations; do not edit. */",
        '#include "runtime/relocation.h"',
        "",
    ]
    layouts: list[tuple[str, int, list[int]]] = []
    for ident, name in entries:
        die = dwarf.named(name)
        offsets = sorted(pointer_offsets(dwarf, die))
        size = byte_size(dwarf, die)
        layouts.append((ident, size, offsets))
        values = ", ".join(str(value) for value in offsets) or "0"
        lines.append(f"static const uint32_t offsets_{ident}[] = {{ {values} }};")
    lines.extend(
        [
            "",
            "const MslRelocTypeDesc msl_reloc_type_descs[MSL_RELOC_TYPE_COUNT] = {",
            "    [MSL_RELOC_RAW] = { NULL, 0, 0 },",
            "    [MSL_RELOC_POINTER_ARRAY] = { NULL, 0, sizeof(void*) },",
        ]
    )
    for ident, size, offsets in layouts:
        lines.append(
            f"    [MSL_RELOC_{ident}] = {{ offsets_{ident}, {len(offsets)}, {size} }},"
        )
    lines.extend(["};", ""])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines))


if __name__ == "__main__":
    main()
