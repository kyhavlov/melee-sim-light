#!/usr/bin/env python3
"""Generate native DAT field descriptions from PPC32/x86-64 DWARF.

The decomp declarations are the schema.  Compiling the same type-root TU for
both targets gives exact disk-side and hosted-runtime offsets without a second
hand-maintained set of structure definitions.

Source format owner: refs/melee/src/sysdolphin/baselib/archive.c.
"""

from __future__ import annotations

import argparse
import dataclasses
import re
import shutil
import subprocess
from pathlib import Path


_DIE_RE = re.compile(
    r"^\s*<(\d+)><(?:0x)?([0-9a-f]+)>: Abbrev Number: \d+ \((DW_TAG_[^)]+)\)"
)
_ATTR_RE = re.compile(r"^\s*<[0-9a-f]+>\s+(DW_AT_[A-Za-z0-9_]+)\s*:\s*(.*)$")
_REF_RE = re.compile(r"<0x([0-9a-f]+)>")
_INT_RE = re.compile(r"(?:^|\s)(-?(?:0x[0-9a-fA-F]+|\d+))(?:\s|$)")

# llvm-dwarfdump renders the same info with a different surface syntax. It is
# the fallback on hosts without GNU readelf (macOS), and unlike readelf it
# parses both ELF and Mach-O objects.
_DWARFDUMP_DIE_RE = re.compile(r"^0x([0-9a-f]+):(\s+)(DW_TAG_[A-Za-z0-9_]+)")
# Compile-unit address size: "Pointer Size:  8" (readelf) or
# "addr_size = 0x08" (dwarfdump). Clang omits DW_AT_byte_size on pointer
# DIEs, so pointer sizes fall back to this.
_ADDRESS_SIZE_RE = re.compile(
    r"(?:Pointer Size:\s+(?:0x)?([0-9a-f]+)|addr_size = (?:0x)?([0-9a-f]+))"
)
_DWARFDUMP_ATTR_RE = re.compile(r"^\s+(DW_AT_[A-Za-z0-9_]+)\s*\t?\((.*)\)$")
_DWARFDUMP_REF_RE = re.compile(r"^0x([0-9a-f]+)")
_DWARF_ATE_CODES = {
    "DW_ATE_address": 1,
    "DW_ATE_boolean": 2,
    "DW_ATE_complex_float": 3,
    "DW_ATE_float": 4,
    "DW_ATE_signed": 5,
    "DW_ATE_signed_char": 6,
    "DW_ATE_unsigned": 7,
    "DW_ATE_unsigned_char": 8,
}


def _dwarf_tool() -> str:
    if shutil.which("readelf"):
        return "readelf"
    # Prefer a full LLVM llvm-dwarfdump (e.g. Homebrew's) over Apple's
    # dwarfdump, which lacks the PowerPC target and warns on PPC32 ELF
    # objects (it still dumps the DWARF correctly).
    for candidate in (
        "llvm-dwarfdump",
        "/opt/homebrew/opt/llvm/bin/llvm-dwarfdump",
        "/usr/local/opt/llvm/bin/llvm-dwarfdump",
        "dwarfdump",
    ):
        if shutil.which(candidate):
            return candidate
    raise RuntimeError("neither readelf nor dwarfdump is available")


@dataclasses.dataclass
class Die:
    offset: int
    depth: int
    tag: str
    attrs: dict[str, str] = dataclasses.field(default_factory=dict)
    children: list["Die"] = dataclasses.field(default_factory=list)


class Dwarf:
    def __init__(self, object_path: Path):
        tool = _dwarf_tool()
        if tool == "readelf":
            command = ["readelf", "--debug-dump=info", "--wide", str(object_path)]
            die_re = _DIE_RE
            attr_re = _ATTR_RE
        else:
            command = [tool, "--debug-info", str(object_path)]
            die_re = _DWARFDUMP_DIE_RE
            attr_re = _DWARFDUMP_ATTR_RE
        result = subprocess.run(
            command,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
        )
        self.dies: dict[int, Die] = {}
        self.address_size = 0
        stack: list[Die] = []
        current: Die | None = None
        for line in result.stdout.splitlines():
            if self.address_size == 0:
                match = _ADDRESS_SIZE_RE.search(line)
                if match:
                    self.address_size = int(match.group(1) or match.group(2), 16)
            match = die_re.match(line)
            if match:
                if tool == "readelf":
                    depth = int(match.group(1))
                    offset = int(match.group(2), 16)
                    tag = match.group(3)
                else:
                    offset = int(match.group(1), 16)
                    depth = len(match.group(2))
                    tag = match.group(3)
                current = Die(offset, depth, tag)
                self.dies[current.offset] = current
                while stack and stack[-1].depth >= depth:
                    stack.pop()
                if stack:
                    stack[-1].children.append(current)
                stack.append(current)
                continue
            match = attr_re.match(line)
            if match and current is not None:
                value = match.group(2)
                if tool != "readelf":
                    code = _DWARF_ATE_CODES.get(value)
                    if code is not None:
                        value = str(code)
                current.attrs[match.group(1)] = value

    def ref(self, die: Die, attr: str = "DW_AT_type") -> Die | None:
        value = die.attrs.get(attr)
        if value is None:
            return None
        match = _REF_RE.search(value) or _DWARFDUMP_REF_RE.match(value)
        return self.dies.get(int(match.group(1), 16)) if match else None

    @staticmethod
    def integer(die: Die, attr: str, default: int | None = None) -> int:
        value = die.attrs.get(attr)
        if value is None:
            if default is None:
                raise ValueError(f"{die.tag}@{die.offset:x} has no {attr}")
            return default
        values = _INT_RE.findall(value)
        if not values:
            raise ValueError(f"cannot parse {attr}={value!r}")
        return int(values[-1], 0)

    @staticmethod
    def name(die: Die) -> str:
        value = die.attrs.get("DW_AT_name", "")
        if value.startswith('"') and value.endswith('"'):
            return value[1:-1]
        if "): " in value:
            return value.rsplit(": ", 1)[1]
        if value.startswith("(") and ") " in value:
            return value.split(") ", 1)[1]
        return value.rsplit(": ", 1)[-1]

    def named(self, name: str) -> Die:
        typedefs = [
            die
            for die in self.dies.values()
            if die.tag == "DW_TAG_typedef" and self.name(die) == name
        ]
        if typedefs:
            return typedefs[-1]
        records = [
            die
            for die in self.dies.values()
            if die.tag in {"DW_TAG_structure_type", "DW_TAG_union_type"}
            and self.name(die) == name
            and "DW_AT_byte_size" in die.attrs
        ]
        if not records:
            raise ValueError(f"DWARF type {name!r} not found")
        return records[-1]


_WRAPPERS = {
    "DW_TAG_typedef",
    "DW_TAG_const_type",
    "DW_TAG_volatile_type",
    "DW_TAG_restrict_type",
}


@dataclasses.dataclass
class TypePair:
    ident: int
    src: Die | None
    dst: Die | None
    tag: str
    src_size: int
    dst_size: int
    name: str
    fields: list[tuple[int, int, "TypePair"]] = dataclasses.field(default_factory=list)
    elem: "TypePair | None" = None
    count: int = 0
    src_bit_offset: int = 0
    dst_bit_offset: int = 0
    bit_size: int = 0
    base_encoding: int = 0


class PairBuilder:
    def __init__(self, src: Dwarf, dst: Dwarf):
        self.src_dwarf = src
        self.dst_dwarf = dst
        self.cache: dict[tuple[int, int], TypePair] = {}
        self.types: list[TypePair] = []

    @staticmethod
    def _unwrap(dwarf: Dwarf, die: Die | None) -> Die | None:
        while die is not None and die.tag in _WRAPPERS:
            die = dwarf.ref(die)
        return die

    @staticmethod
    def _size(dwarf: Dwarf, die: Die | None) -> int:
        if die is None or die.tag == "DW_TAG_unspecified_type":
            return 0
        if die.tag == "DW_TAG_pointer_type":
            return dwarf.integer(die, "DW_AT_byte_size", dwarf.address_size)
        return dwarf.integer(die, "DW_AT_byte_size", 0)

    @staticmethod
    def _members(die: Die) -> list[Die]:
        return [child for child in die.children if child.tag == "DW_TAG_member"]

    def pair(self, src_die: Die | None, dst_die: Die | None) -> TypePair:
        src_die = self._unwrap(self.src_dwarf, src_die)
        dst_die = self._unwrap(self.dst_dwarf, dst_die)
        src_key = -1 if src_die is None else src_die.offset
        dst_key = -1 if dst_die is None else dst_die.offset
        key = (src_key, dst_key)
        if key in self.cache:
            return self.cache[key]

        src_tag = "DW_TAG_unspecified_type" if src_die is None else src_die.tag
        dst_tag = "DW_TAG_unspecified_type" if dst_die is None else dst_die.tag
        if src_tag != dst_tag:
            raise ValueError(f"type shape differs: {src_tag} vs {dst_tag}")
        name = self.src_dwarf.name(src_die) if src_die is not None else "void"
        item = TypePair(
            len(self.types),
            src_die,
            dst_die,
            src_tag,
            self._size(self.src_dwarf, src_die),
            self._size(self.dst_dwarf, dst_die),
            name,
        )
        if src_tag == "DW_TAG_base_type":
            item.base_encoding = self.src_dwarf.integer(
                src_die, "DW_AT_encoding", 0
            )
        self.cache[key] = item
        self.types.append(item)

        if src_tag in {"DW_TAG_structure_type", "DW_TAG_union_type"}:
            src_members = self._members(src_die)
            dst_members = self._members(dst_die)
            if len(src_members) != len(dst_members):
                raise ValueError(
                    f"member count differs for {name}: "
                    f"{len(src_members)} vs {len(dst_members)}"
                )
            for index, (src_member, dst_member) in enumerate(
                zip(src_members, dst_members, strict=True)
            ):
                src_name = self.src_dwarf.name(src_member)
                dst_name = self.dst_dwarf.name(dst_member)
                if src_name != dst_name:
                    raise ValueError(
                        f"member {index} differs for {name}: "
                        f"{src_name!r} vs {dst_name!r}"
                    )
                # HSD_Joint.u owns DObj/spline/particle presentation data.
                # The headless runtime still translates the skeleton, inverse
                # matrices, and RObj constraints, but HSD_JObjLoadJoint must
                # observe a null draw-object union just as the PPC headless
                # boundary does after renderer omission.
                # Source: refs/melee/src/sysdolphin/baselib/jobj.c.
                if name == "HSD_Joint" and src_name == "u":
                    continue
                # Article special attributes are declared void in the decomp.
                # The Fox public-root translator supplies the exact type for
                # each of its three article slots.
                # refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c
                if name == "Article" and src_name == "x4_specialAttributes":
                    continue
                # These ftData members have character-dependent concrete
                # types/counts. The public-root translator supplies them from
                # the selected character's source registry row rather than
                # imposing one fighter's graph shape on every archive.
                # refs/melee/src/melee/ft/ftdata.c::ftData_Table_Unk0
                if name == "ftData" and src_name in {
                    "ext_attr",
                    "xC",
                    "x10",
                    "x48_items",
                }:
                    continue
                field_override = None
                src_offset = self.src_dwarf.integer(
                    src_member, "DW_AT_data_member_location", 0
                )
                dst_offset = self.dst_dwarf.integer(
                    dst_member, "DW_AT_data_member_location", 0
                )
                if field_override is None:
                    field_type = self.pair(
                        self.src_dwarf.ref(src_member),
                        self.dst_dwarf.ref(dst_member),
                    )
                else:
                    field_type = self.pair(
                        self.src_dwarf.named(field_override),
                        self.dst_dwarf.named(field_override),
                    )
                bit_size = self.src_dwarf.integer(src_member, "DW_AT_bit_size", 0)
                dst_bit_size = self.dst_dwarf.integer(
                    dst_member, "DW_AT_bit_size", 0
                )
                if bit_size != dst_bit_size:
                    raise ValueError(f"bitfield width differs for {name}.{src_name}")
                if bit_size:
                    def bit_offsets(dwarf: Dwarf, member: Die, big_endian: bool) -> int:
                        if "DW_AT_data_bit_offset" in member.attrs:
                            return dwarf.integer(member, "DW_AT_data_bit_offset")
                        byte_offset = dwarf.integer(
                            member, "DW_AT_data_member_location", 0
                        )
                        storage_bits = dwarf.integer(
                            member, "DW_AT_byte_size"
                        ) * 8
                        high_bit_offset = dwarf.integer(member, "DW_AT_bit_offset")
                        if big_endian:
                            return byte_offset * 8 + high_bit_offset
                        return (
                            byte_offset * 8
                            + storage_bits
                            - high_bit_offset
                            - bit_size
                        )

                    field_type = dataclasses.replace(
                        field_type,
                        ident=len(self.types),
                        tag="DW_TAG_msl_bitfield",
                        src_bit_offset=bit_offsets(
                            self.src_dwarf, src_member, True
                        )
                        - src_offset * 8,
                        dst_bit_offset=bit_offsets(
                            self.dst_dwarf, dst_member, False
                        )
                        - dst_offset * 8,
                        bit_size=bit_size,
                        name=f"{name}.{src_name}",
                    )
                    self.types.append(field_type)
                item.fields.append((src_offset, dst_offset, field_type))
        elif src_tag == "DW_TAG_pointer_type":
            item.elem = self.pair(
                self.src_dwarf.ref(src_die), self.dst_dwarf.ref(dst_die)
            )
        elif src_tag == "DW_TAG_array_type":
            src_ranges = [
                child for child in src_die.children if child.tag == "DW_TAG_subrange_type"
            ]
            dst_ranges = [
                child for child in dst_die.children if child.tag == "DW_TAG_subrange_type"
            ]
            def total_count(dwarf: Dwarf, ranges: list[Die]) -> int:
                result = 1
                for subrange in ranges:
                    result *= dwarf.integer(
                        subrange,
                        "DW_AT_count",
                        dwarf.integer(subrange, "DW_AT_upper_bound", -1) + 1,
                    )
                return result if ranges else 0

            src_count = total_count(self.src_dwarf, src_ranges)
            dst_count = total_count(self.dst_dwarf, dst_ranges)
            if src_count != dst_count:
                raise ValueError(f"array extent differs: {src_count} vs {dst_count}")
            item.count = src_count
            item.elem = self.pair(
                self.src_dwarf.ref(src_die), self.dst_dwarf.ref(dst_die)
            )
            if item.src_size == 0 and item.count != 0:
                item.src_size = item.count * item.elem.src_size
            if item.dst_size == 0 and item.count != 0:
                item.dst_size = item.count * item.elem.dst_size
        elif src_tag == "DW_TAG_subroutine_type":
            # DAT function pointers are not relocatable gameplay data. The
            # runtime checks that any reached value is null.
            pass
        elif src_tag not in {
            "DW_TAG_base_type",
            "DW_TAG_enumeration_type",
            "DW_TAG_unspecified_type",
        }:
            raise ValueError(f"unsupported DWARF type tag {src_tag} ({name})")
        return item


def _c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit(builder: PairBuilder, roots: list[tuple[str, TypePair]], output: Path) -> None:
    lines = [
        "// Generated by tools/build/generate_native_dat_layout.py.",
        "// Do not edit; source and native layouts come from the shared decomp declarations.",
        '#include "platform/native_dat.h"',
        "",
    ]
    for item in builder.types:
        lines.append(f"static const MslDatType msl_dat_type_{item.ident};")
    lines.append("")
    for item in builder.types:
        if item.fields:
            lines.append(f"static const MslDatField msl_dat_fields_{item.ident}[] = {{")
            for src_offset, dst_offset, field_type in item.fields:
                lines.append(
                    f"    {{ {src_offset}U, {dst_offset}U, &msl_dat_type_{field_type.ident} }},"
                )
            lines.append("};")
    lines.append("")
    tag_map = {
        "DW_TAG_base_type": "MSL_DAT_BASE",
        "DW_TAG_enumeration_type": "MSL_DAT_BASE",
        "DW_TAG_structure_type": "MSL_DAT_STRUCT",
        "DW_TAG_union_type": "MSL_DAT_UNION",
        "DW_TAG_pointer_type": "MSL_DAT_POINTER",
        "DW_TAG_array_type": "MSL_DAT_ARRAY",
        "DW_TAG_subroutine_type": "MSL_DAT_FUNCTION",
        "DW_TAG_unspecified_type": "MSL_DAT_VOID",
        "DW_TAG_msl_bitfield": "MSL_DAT_BITFIELD",
    }
    for item in builder.types:
        fields = f"msl_dat_fields_{item.ident}" if item.fields else "NULL"
        elem = f"&msl_dat_type_{item.elem.ident}" if item.elem else "NULL"
        lines.extend(
            [
                f"static const MslDatType msl_dat_type_{item.ident} = {{",
                f"    {tag_map[item.tag]}, {item.src_size}U, {item.dst_size}U,",
                f"    {item.count}U, {len(item.fields)}U, {fields}, {elem},",
                f"    {item.src_bit_offset}U, {item.dst_bit_offset}U, {item.bit_size}U,",
                f"    {_c_string(item.name)},",
                f"    {item.base_encoding}U,",
                "};",
            ]
        )
    lines.append("")
    for root_name, item in roots:
        lines.append(
            f"const MslDatType* const msl_dat_root_{root_name} = &msl_dat_type_{item.ident};"
        )
    lines.append("")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ppc", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    src = Dwarf(args.ppc)
    dst = Dwarf(args.native)
    builder = PairBuilder(src, dst)
    root_names = [
        "HSD_PSCmdList",
        "UnkStageDat",
        "MapCollData",
        "UnkStage6B0",
        "DynamicModelDesc",
        "HSD_Joint",
        "HSD_Spline",
        "HSD_MatAnimJoint",
        "EF_EffectDesc",
        "ftData",
        "FigaTree",
        "it_804D6D20_t",
        "ItemCommonData",
        "it_804D6D40_t",
        "Fighter_804D653C_t",
        "pl_804D6470_t",
        "ftCommonData",
        "MslDatIntPointer",
        "MslDatItemThrowAttrs",
        "MslDatFloat",
        "MslDatFloat5",
        "MslDatByte",
        "MslDatVec2Pointer",
        "MslDatFighterPartsPointer",
        "MslDatFighter6540Pointer",
        "MslDatBattlefieldParams",
        "MslDatPokemonStadiumParams",
        "MslDatFountainParams",
        "MslDatYoshisStoryParams",
        "MslDatDreamLandParams",
        "MslDatStageItemEntry",
        "MslDatHeihoAttrs",
        "Fighter_WaitAnimData",
        "MslDatAnimBytePair",
        "MslDatSpaceAnimalArticles",
        "MslDatSheikArticles",
        "MslDatPeachArticles",
        "MslDatZeldaArticles",
        "MslDatLuigiArticles",
        "MslDatMarioArticles",
        "ftMario_DatAttrs",
        "MslDatSamusArticles",
        "ftSs_DatAttrs",
        "itSamusBombAttributes",
        "itSamusChargeShot_Attributes",
        "itSamusMissileAttributes",
        "itSamusGrappleAttributes",
        "MslDatIceClimberArticles",
        "ftIceClimberAttributes",
        "itClimbersIceAttributes",
        "itClimbersBlizzardAttributes",
        "itClimbersStringAttributes",
        "ftDonkeyAttributes",
        "MslDatPikachuArticles",
        "ftPikachuAttributes",
        "itPikachuthunderAttributes",
        "itPikachutJoltGroundAttributes",
        "MslDatYoshiArticles",
        "ftYoshiAttributes",
        "itYoshiEggThrowAttributes",
        "MslDatCommonItemArticles",
        "itBombHeiAttributes",
        "itDoseiAttributes",
        "itSword_UnkArticle1",
        "MslDatPurinAuxList",
        "ftFox_DatAttrs",
        "ftCaptain_DatAttrs",
        "MarsAttributes",
        "ftPe_DatAttrs",
        "ftPurinAttributes",
        "ftLuigiAttributes",
        "ftSeakAttributes",
        "ftZelda_DatAttrs",
        "FoxLaserAttr",
        "FoxBlasterAttr",
        "FoxIllusionAttr",
        "itSeakNeedleThrownAttributes",
        "itSeakChain_Attrs",
        "MslDatPeachTurnipAttrs",
        "itPeachToadSporeAttributes",
        "MslDatZeldaDinFireAttrs",
        "itZeldaDinFireExplodeAttributes",
        "itUnkAttributes",
        "Fighter_804D6518_t",
        "Fighter_804D651C_t",
        "Fighter_804D6520_t",
        "Fighter_804D6524_t",
        "Fighter_804D6528_t",
        "CrowdConfig",
        "Fighter_804D64FC_t",
        "Fighter_804D6534_t",
    ]
    roots = [
        (name, builder.pair(src.named(name), dst.named(name))) for name in root_names
    ]
    emit(builder, roots, args.output)


if __name__ == "__main__":
    main()
