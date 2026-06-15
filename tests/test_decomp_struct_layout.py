"""Verify hand-mapped ext-attr layout tables against the parsed decomp structs.

The per-character ftData.x4 ext-attr blocks (MarsAttributes etc.) need semantic key
names that only a human can assign, but their OFFSETS are mechanical facts of the
decomp struct. tools/extraction/decomp_struct_layout.py parses the struct under GC
natural-alignment rules; this test pins every layout-table offset to a parsed field
boundary, so a transcription error (or upstream decomp drift) fails here instead of
silently extracting garbage for a character.

Adding a character: write the semantic table (key -> struct offset) for its
ftXxxAttributes, parse the struct here, and assert the same way.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.extraction.decomp_struct_layout import (  # noqa: E402
    parse_struct_layout,
    struct_size,
)
from tools.extraction.extract_character_attrs import (  # noqa: E402
    MARS_SWORD_ATTRS_LAYOUT,
    SEAK_SPECIAL_ATTRS_LAYOUT,
)

MARS_TYPES = ROOT / "refs" / "melee" / "src" / "melee" / "ft" / "chara" / "ftMars" / "types.h"
SEAK_TYPES = ROOT / "refs" / "melee" / "src" / "melee" / "ft" / "chara" / "ftSeak" / "types.h"
LB_TYPES = ROOT / "refs" / "melee" / "src" / "melee" / "lb" / "types.h"

decomp_available = MARS_TYPES.exists() and LB_TYPES.exists()
seak_decomp_available = SEAK_TYPES.exists()


@pytest.mark.skipif(not decomp_available, reason="decomp refs not available")
def test_mars_sword_attrs_layout_matches_parsed_struct() -> None:
    lb_text = LB_TYPES.read_text(encoding="utf-8", errors="replace")
    absorb = parse_struct_layout(lb_text, "AbsorbDesc")
    extra = {"AbsorbDesc": (struct_size(absorb), 4)}
    ms_text = MARS_TYPES.read_text(encoding="utf-8", errors="replace")
    sword = parse_struct_layout(ms_text, "SwordAttrs", extra_types=extra)
    extra["SwordAttrs"] = (struct_size(sword, extra), 4)
    fields = parse_struct_layout(ms_text, "_MarsAttributes", extra_types=extra)

    field_offsets = {f.offset for f in fields}
    # AbsorbDesc sub-fields (bone/offset/size) are addressed through the x64 composite.
    absorb_base = next(f.offset for f in fields if f.c_type == "AbsorbDesc")
    for sub in absorb:
        field_offsets.add(absorb_base + sub.offset)

    for key, off, kind in MARS_SWORD_ATTRS_LAYOUT:
        assert off in field_offsets, (
            f"{key}: extractor offset 0x{off:X} is not a field boundary of the parsed "
            "_MarsAttributes layout - transcription error or decomp drift"
        )
        if kind == "vec3":
            assert off + 8 <= struct_size(fields, extra)

    # The table must stay within the struct.
    last_key, last_off, last_kind = MARS_SWORD_ATTRS_LAYOUT[-1]
    assert last_off + 4 <= struct_size(fields, extra)


@pytest.mark.skipif(not seak_decomp_available, reason="decomp refs not available")
def test_sheik_special_attrs_layout_matches_parsed_struct() -> None:
    sk_text = SEAK_TYPES.read_text(encoding="utf-8", errors="replace")
    fields = parse_struct_layout(sk_text, "_ftSeakAttributes")
    field_offsets = {f.offset for f in fields}

    for key, off, kind in SEAK_SPECIAL_ATTRS_LAYOUT:
        assert off in field_offsets, (
            f"{key}: extractor offset 0x{off:X} is not a field boundary of the parsed "
            "_ftSeakAttributes layout - transcription error or decomp drift"
        )
        assert kind in {"f32", "i32"}

    last_key, last_off, last_kind = SEAK_SPECIAL_ATTRS_LAYOUT[-1]
    assert last_off + 4 <= struct_size(fields)


@pytest.mark.skipif(not decomp_available, reason="decomp refs not available")
def test_parser_rejects_declared_offset_mismatch() -> None:
    bad = """
struct Broken {
    /* +0x0 */ int a;
    /* +0x8 */ int b;
};
"""
    with pytest.raises(ValueError, match="computed offset"):
        parse_struct_layout(bad, "Broken")
