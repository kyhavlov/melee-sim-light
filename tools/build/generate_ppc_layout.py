#!/usr/bin/env python3
"""Snapshot PPC32 disk layouts from the type-root TU (make ppc-layout)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.build.generate_native_dat_layout import DAT_ROOT_NAMES, Dwarf  # noqa: E402


def snapshot(dwarf: Dwarf) -> str:
    if dwarf.address_size != 4:
        raise ValueError("PPC disk layouts require 32-bit pointers")
    nodes = []
    indices = {}

    def visit(die):
        if die.offset in indices:
            return indices[die.offset]
        index = len(nodes)
        indices[die.offset] = index
        attrs = {}
        node = {"tag": die.tag, "attrs": attrs, "children": []}
        nodes.append(node)
        if "DW_AT_name" in die.attrs:
            attrs["DW_AT_name"] = dwarf.name(die)
        for attr in (
            "DW_AT_byte_size", "DW_AT_encoding", "DW_AT_data_member_location",
            "DW_AT_bit_size", "DW_AT_bit_offset", "DW_AT_data_bit_offset",
            "DW_AT_count", "DW_AT_upper_bound",
        ):
            if attr in die.attrs:
                attrs[attr] = dwarf.integer(die, attr)
        target = dwarf.ref(die)
        if target is not None:
            attrs["DW_AT_type"] = visit(target)
        node["children"] = [
            visit(child) for child in die.children
            if child.tag in {"DW_TAG_member", "DW_TAG_subrange_type"}
        ]
        return index

    for name in [*DAT_ROOT_NAMES, "CmdUnion", "ColorOverlay_x8_t"]:
        visit(dwarf.named(name))
    # One node per line keeps regenerated layout changes reviewable without
    # retaining compiler-specific DIE offsets, paths or debug-string tables.
    return (
        '{"version": 1, "address_size": 4, "dies": [\n'
        + ",\n".join(json.dumps(node, separators=(",", ":")) for node in nodes)
        + "\n]}\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--object", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    result = snapshot(Dwarf(args.object))
    if args.check:
        if args.output.read_text() != result:
            raise SystemExit("PPC layout is stale; regenerate with make ppc-layout")
    else:
        args.output.write_text(result)


if __name__ == "__main__":
    main()
