"""Parse C struct layouts from decomp headers (GC natural alignment).

Per-character ext-attr blocks (ftData.x4: MarsAttributes, ftFoxAttributes, ...) were
hand-transcribed offset tables; every new character repeats that error-prone work. The
decomp header IS the layout ground truth, so parse it: field order + C types determine
offsets under the GameCube ABI (natural alignment, struct alignment = max member
alignment).

Supported: scalar fields (u8/s8/u16/s16/u32/s32/int/f32/float/Vec3), fixed arrays of
those, `pad`/underscore filler arrays, and /* +0xNN */ offset comments (verified against
the computed layout when present - a mismatch raises, catching both parser gaps and
decomp drift).

Usage:
    fields = parse_struct_layout(header_text, "_MarsAttributes")
    fields[3].offset  # 0xC
"""

from __future__ import annotations

import re
from dataclasses import dataclass

_SCALAR_SIZES: dict[str, int] = {
    "u8": 1,
    "s8": 1,
    "char": 1,
    "u16": 2,
    "s16": 2,
    "u32": 4,
    "s32": 4,
    "int": 4,
    "f32": 4,
    "float": 4,
    "Vec3": 12,  # three f32, alignment 4
}

_SCALAR_ALIGNS: dict[str, int] = {k: (4 if k == "Vec3" else v) for k, v in _SCALAR_SIZES.items()}


@dataclass(frozen=True)
class StructField:
    name: str
    c_type: str
    offset: int
    size: int
    count: int  # array element count (1 for scalars)


# Offset comments appear as /* +0xNN */, /* 0xNN */, or /* +NN */ (hex without 0x).
_FIELD_RE = re.compile(
    r"^\s*(?:/\*\s*\+?\s*(?P<off>(?:0x)?[0-9A-Fa-f]+)\s*\*/\s*)?"
    r"(?:struct\s+)?(?P<type>[A-Za-z_][A-Za-z0-9_]*)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"(?:\[(?P<count>\d+)\])?\s*;"
)


def parse_struct_layout(
    header_text: str,
    struct_name: str,
    extra_types: dict[str, tuple[int, int]] | None = None,
) -> list[StructField]:
    """Return the field layout of `struct_name` from decomp header text.

    `extra_types` maps composite type names to (size, align) - e.g. AbsorbDesc parsed
    recursively from lb/types.h - so nested known structs lay out correctly.
    """
    # struct body: from "struct <name> {" or "typedef struct <name> {" to its closing "}".
    m = re.search(
        r"(?:typedef\s+)?struct\s+" + re.escape(struct_name) + r"\s*\{(?P<body>.*?)\n\}",
        header_text,
        re.S,
    )
    if m is None:
        raise ValueError(f"struct {struct_name} not found")
    body = m.group("body")

    fields: list[StructField] = []
    offset = 0
    max_align = 1
    for raw_line in body.splitlines():
        line = raw_line.split("//")[0].rstrip()
        if not line.strip():
            continue
        fm = _FIELD_RE.match(line)
        if fm is None:
            # Non-field lines (nested unions/structs, function pointers) are out of scope:
            # fail loudly rather than silently skewing every later offset.
            stripped = line.strip()
            if stripped.startswith("/*") or stripped.startswith("*"):
                continue
            raise ValueError(f"unsupported struct line in {struct_name}: {raw_line!r}")
        c_type = fm.group("type")
        if c_type in _SCALAR_SIZES:
            size = _SCALAR_SIZES[c_type]
            align = _SCALAR_ALIGNS[c_type]
        elif extra_types is not None and c_type in extra_types:
            size, align = extra_types[c_type]
        else:
            raise ValueError(f"unsupported field type in {struct_name}: {c_type!r} ({raw_line!r})")
        count = int(fm.group("count") or 1)
        offset = (offset + align - 1) & ~(align - 1)
        declared = fm.group("off")
        if declared is not None and int(declared, 16) != offset:
            raise ValueError(
                f"{struct_name}.{fm.group('name')}: computed offset 0x{offset:X} != "
                f"declared {declared} - parser gap or decomp drift"
            )
        fields.append(
            StructField(
                name=fm.group("name"), c_type=c_type, offset=offset, size=size * count, count=count
            )
        )
        offset += size * count
        max_align = max(max_align, align)
    if not fields:
        raise ValueError(f"struct {struct_name} has no parseable fields")
    return fields


def struct_size(fields: list[StructField], extra_types: dict[str, tuple[int, int]] | None = None) -> int:
    last = fields[-1]
    end = last.offset + last.size

    def field_align(f: StructField) -> int:
        if f.c_type in _SCALAR_ALIGNS:
            return _SCALAR_ALIGNS[f.c_type]
        if extra_types is not None and f.c_type in extra_types:
            return extra_types[f.c_type][1]
        return 4

    max_align = max(field_align(f) for f in fields)
    return (end + max_align - 1) & ~(max_align - 1)
