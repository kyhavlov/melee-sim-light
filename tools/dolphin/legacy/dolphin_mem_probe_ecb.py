#!/usr/bin/env python3
from __future__ import annotations

"""
Live Dolphin memory probe (ECB + optional Gecko hook injection).

WARNING (read before using):
- Run this from an isolated terminal session (e.g. `tmux`) and prefer a hard timeout.
- Stale/incorrect Gecko hooks or bad reads can wedge Dolphin, and that can wedge this probe; in
  this repo's setup it has historically taken down the surrounding terminal session.

Recommended usage pattern:
  timeout 60s uv run python scripts/dolphin_mem_probe_ecb.py ... --max_seconds 50
Set `SSBM_SUPPRESS_DOLPHIN_PROBE_WARNING=1` to suppress the runtime warning banner.
"""

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np

import peppi_bytes
from melee_sim.metrics import MASK_F32, MASK_U8, canonicalize_slippi_sample_last
from melee_sim.replay_io import read_replay_bytes


LIBMELEE_ROOT_DEFAULT = Path("/media/kyle/Windows/Users/kyleh/git/libmelee")

# Debug scratch space for Gecko hooks.
#
# Prefer a high MEM1 address to avoid colliding with game BSS / static allocations. This is only used in probe runs.
DEBUG_SP2C_CAPTURE_BASE = 0x817FF000
DEBUG_DYN_BONES_CAPTURE_BASE = 0x817FE000


def _add_libmelee_to_path(root: Path) -> None:
    root = root.resolve()
    if not root.exists():
        raise FileNotFoundError(f"libmelee path does not exist: {root}")
    sys.path.insert(0, str(root))


def _ensure_extracted_dolphin(appimage: Path) -> Path:
    """Ensure we have a Dolphin binary path that:
    - contains 'ExiAI' in the path (libmelee build detection),
    - launches with argv[0] 'dolphin-emu' (dolphin_memory_engine hook detection).
    """
    appimage = appimage.resolve()
    if not appimage.exists():
        raise FileNotFoundError(f"missing AppImage: {appimage}")

    # AppImage extraction folder (relative to repo root).
    squash = Path.cwd() / "squashfs-root"
    dolphin_bin = squash / "usr" / "bin" / "dolphin-emu"
    if not dolphin_bin.exists():
        # Extract in-place.
        subprocess.run([str(appimage), "--appimage-extract"], check=True)
        if not dolphin_bin.exists():
            raise RuntimeError(f"expected extracted Dolphin binary at {dolphin_bin}")

    # Put a stable, hookable path in `.local_ExiAI/dolphin-emu`.
    out_dir = Path.cwd() / ".local_ExiAI"
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / "dolphin-emu"
    if out.exists() or out.is_symlink():
        out.unlink()
    out.symlink_to(dolphin_bin)
    return out


def _update_gecko_ini(*, ini_text: str, code_name: str, gecko_block: str) -> str:
    """Ensure `code_name` is enabled and its Gecko block is present and up to date.

    IMPORTANT: this always replaces any existing Gecko block with the same name. Stale Gecko hooks
    can hang Dolphin, which in turn blocks the memory probe (and can take down the surrounding
    terminal session if the probe is attached to it).
    """

    enabled_tag = "[Gecko_Enabled]"
    gecko_tag = "[Gecko]"

    enabled_i = ini_text.find(enabled_tag)
    gecko_i = ini_text.find(gecko_tag)
    if enabled_i == -1 or gecko_i == -1 or gecko_i <= enabled_i:
        raise RuntimeError("unexpected GALE01r2.ini structure (missing [Gecko_Enabled]/[Gecko])")

    lines = ini_text.splitlines()
    out: list[str] = []
    in_enabled = False
    in_gecko = False
    skipping_old_block = False
    enabled_has = False
    gecko_replaced = False

    def _flush_enabled_if_needed() -> None:
        nonlocal enabled_has
        if in_enabled and not enabled_has:
            out.append(code_name)
            enabled_has = True

    for ln in lines:
        if ln.strip() == enabled_tag:
            _flush_enabled_if_needed()
            in_enabled = True
            in_gecko = False
            skipping_old_block = False
            out.append(ln)
            continue
        if ln.strip() == gecko_tag:
            _flush_enabled_if_needed()
            in_enabled = False
            in_gecko = True
            skipping_old_block = False
            out.append(ln)
            continue

        if ln.startswith("["):
            # Leaving a section.
            _flush_enabled_if_needed()
            in_enabled = False
            if in_gecko and not gecko_replaced:
                out.append(gecko_block)
                gecko_replaced = True
            in_gecko = False
            skipping_old_block = False
            out.append(ln)
            continue

        if in_enabled and ln.strip() == code_name:
            enabled_has = True

        if in_gecko:
            if ln.strip() == code_name:
                if not gecko_replaced:
                    out.append(gecko_block)
                    gecko_replaced = True
                skipping_old_block = True
                continue
            if skipping_old_block:
                # Old Gecko blocks are separated by either a new `$Name` header or a new section.
                if ln.startswith("$") or ln.startswith("["):
                    skipping_old_block = False
                    out.append(ln)
                continue

        out.append(ln)

    # EOF flush.
    _flush_enabled_if_needed()
    if in_gecko and not gecko_replaced:
        out.append(gecko_block)

    return "\n".join(out) + "\n"


def _inject_gecko_capture_sp2c(console, *, capture_root: bool) -> None:
    """Inject a tiny Gecko hook that captures the `Vec3 sp2C` used by `ftFx_SpecialN_CreateBlasterShot`.

    Hook point: 0x800E5FE0 (the `addi r4, r1, 0x2c` immediately before `bl it_8029C6A4`).
    Captures stack vec3 at r1+0x2C (x/y/z) into DEBUG_SP2C_CAPTURE_BASE, and increments a u32 counter at +0x0C.
    Also hooks `it_8029C6A4` (0x8029C6A4) to capture the Vec3 argument directly (r4) + return address (LR).
    Additionally hooks the ftFox/ftFalco blaster-shot call site (0x800E68CC) to capture the exact HSD_JObj world
    matrix used by `lb_8000B1CC` (jobj->mtx @ +0x44) and the offset vector argument.
    """

    # NOTE: libmelee writes GALE01r2.ini during Console.__init__ when setup_gecko_codes=True.
    ini_path = Path(console._get_dolphin_home_path()) / "GameSettings" / "GALE01r2.ini"  # noqa: SLF001
    if not ini_path.exists():
        raise FileNotFoundError(f"expected Dolphin gecko ini at {ini_path}")

    code_name = "$Debug: Capture BlasterShot sp2C"

    # Zero the capture struct on boot (only a small header; the rest is overwritten on hook hits):
    # - +0x00..+0x08: f32 x/y/z
    # - +0x0C: u32 counter
    # - +0x10: u32 LR (caller return address)
    # - +0x14: u32 HSD_JObj* used by lb_8000B1CC
    # - +0x9C: u32 root HSD_JObj* (walked via parent pointers)
    # - +0xA0..+0xCC: 12 f32 (root jobj->mtx after lb has ensured matrices are up to date)
    base = DEBUG_SP2C_CAPTURE_BASE
    base_hi = (base >> 16) & 0xFFFF
    base_lo = base & 0xFFFF

    # Gecko addresses are written without the leading 0x80 (e.g. 0x80123456 -> 0x0123456).
    base_gecko = base - 0x8000_0000
    if base_gecko < 0 or base_gecko > 0x01FF_FFFF:
        raise ValueError(f"capture base not in MEM1: {base:#010x}")

    post_lines_common = [
        f"C0029870 3D80{base_hi:04X}",  # lfs f0,@244 (original) ; lis r12,base_hi
        f"618C{base_lo:04X} 816C0014",  # ori r12,r12,base_lo ; lwz r11,0x14(r12)
        "396B0044 C06B0000",  # addi r11,r11,0x44 ; lfs f3,0x00(r11)
        "D06C0060 C06B0004",  # stfs f3,0x60(r12) ; lfs f3,0x04(r11)
        "D06C0064 C06B0008",  # stfs f3,0x64(r12) ; lfs f3,0x08(r11)
        "D06C0068 C06B000C",  # stfs f3,0x68(r12) ; lfs f3,0x0C(r11)
        "D06C006C C06B0010",  # stfs f3,0x6C(r12) ; lfs f3,0x10(r11)
        "D06C0070 C06B0014",  # stfs f3,0x70(r12) ; lfs f3,0x14(r11)
        "D06C0074 C06B0018",  # stfs f3,0x74(r12) ; lfs f3,0x18(r11)
        "D06C0078 C06B001C",  # stfs f3,0x78(r12) ; lfs f3,0x1C(r11)
        "D06C007C C06B0020",  # stfs f3,0x7C(r12) ; lfs f3,0x20(r11)
        "D06C0080 C06B0024",  # stfs f3,0x80(r12) ; lfs f3,0x24(r11)
        "D06C0084 C06B0028",  # stfs f3,0x84(r12) ; lfs f3,0x28(r11)
        "D06C0088 C06B002C",  # stfs f3,0x88(r12) ; lfs f3,0x2C(r11)
        "D06C008C C061003C",  # stfs f3,0x8C(r12) ; lfs f3,0x3C(r1)
        "D06C0090 C0610040",  # stfs f3,0x90(r12) ; lfs f3,0x40(r1)
        "D06C0094 C0610044",  # stfs f3,0x94(r12) ; lfs f3,0x44(r1)
        "D06C0098 816C0014",  # stfs f3,0x98(r12) ; lwz r11,0x14(r12)
        # Capture jobj->rot (0x1C..0x28), jobj->scale (0x2C..0x34), jobj->translate (0x38..0x40).
        "C06B001C D06C00D0",  # lfs f3,0x1C(r11) ; stfs f3,0xD0(r12)
        "C06B0020 D06C00D4",  # lfs f3,0x20(r11) ; stfs f3,0xD4(r12)
        "C06B0024 D06C00D8",  # lfs f3,0x24(r11) ; stfs f3,0xD8(r12)
        "C06B0028 D06C00DC",  # lfs f3,0x28(r11) ; stfs f3,0xDC(r12)
        "C06B002C D06C00E0",  # lfs f3,0x2C(r11) ; stfs f3,0xE0(r12)
        "C06B0030 D06C00E4",  # lfs f3,0x30(r11) ; stfs f3,0xE4(r12)
        "C06B0034 D06C00E8",  # lfs f3,0x34(r11) ; stfs f3,0xE8(r12)
        "C06B0038 D06C00EC",  # lfs f3,0x38(r11) ; stfs f3,0xEC(r12)
        "C06B003C D06C00F0",  # lfs f3,0x3C(r11) ; stfs f3,0xF0(r12)
        "C06B0040 D06C00F4",  # lfs f3,0x40(r11) ; stfs f3,0xF4(r12)
        # Capture parent->mtx (jobj->parent at +0x0C, mtx at +0x44).
        "814B000C 914C0100",  # lwz r10,0x0C(r11) ; stw r10,0x100(r12)
        "394A0044 C06A0000",  # addi r10,r10,0x44 ; lfs f3,0x00(r10)
        "D06C0104 C06A0004",  # stfs f3,0x104(r12) ; lfs f3,0x04(r10)
        "D06C0108 C06A0008",  # stfs f3,0x108(r12) ; lfs f3,0x08(r10)
        "D06C010C C06A000C",  # stfs f3,0x10C(r12) ; lfs f3,0x0C(r10)
        "D06C0110 C06A0010",  # stfs f3,0x110(r12) ; lfs f3,0x10(r10)
        "D06C0114 C06A0014",  # stfs f3,0x114(r12) ; lfs f3,0x14(r10)
        "D06C0118 C06A0018",  # stfs f3,0x118(r12) ; lfs f3,0x18(r10)
        "D06C011C C06A001C",  # stfs f3,0x11C(r12) ; lfs f3,0x1C(r10)
        "D06C0120 C06A0020",  # stfs f3,0x120(r12) ; lfs f3,0x20(r10)
        "D06C0124 C06A0024",  # stfs f3,0x124(r12) ; lfs f3,0x24(r10)
        "D06C0128 C06A0028",  # stfs f3,0x128(r12) ; lfs f3,0x28(r10)
        "D06C012C C06A002C",  # stfs f3,0x12C(r12) ; lfs f3,0x2C(r10)
        "D06C0130 812C0100",  # stfs f3,0x130(r12) ; lwz r9,0x100(r12)
        # Capture parent jobj locals (rot/scl/tr) from parent pointer (r9).
        "C069001C D06C0140",  # lfs f3,0x1C(r9) ; stfs f3,0x140(r12)
        "C0690020 D06C0144",  # lfs f3,0x20(r9) ; stfs f3,0x144(r12)
        "C0690024 D06C0148",  # lfs f3,0x24(r9) ; stfs f3,0x148(r12)
        "C0690028 D06C014C",  # lfs f3,0x28(r9) ; stfs f3,0x14C(r12)
        "C069002C D06C0150",  # lfs f3,0x2C(r9) ; stfs f3,0x150(r12)
        "C0690030 D06C0154",  # lfs f3,0x30(r9) ; stfs f3,0x154(r12)
        "C0690034 D06C0158",  # lfs f3,0x34(r9) ; stfs f3,0x158(r12)
        "C0690038 D06C015C",  # lfs f3,0x38(r9) ; stfs f3,0x15C(r12)
        "C069003C D06C0160",  # lfs f3,0x3C(r9) ; stfs f3,0x160(r12)
        "C0690040 D06C0164",  # lfs f3,0x40(r9) ; stfs f3,0x164(r12)
        # Capture jobj->scl pointers (jobj and parent) for later deref in the probe loop.
        "80EB0074 90EC0168",  # lwz r7,0x74(r11) ; stw r7,0x168(r12)
        "80E90074 90EC016C",  # lwz r7,0x74(r9) ; stw r7,0x16C(r12)
    ]
    post_lines_no_root = [
        "60000000 60000000",  # nop ; nop (common block already stored 0x98 and loaded r11)
        "60000000 00000000",  # end
    ]
    post_lines_root = [
        # Walk to root via jobj->parent (0x0C) and capture root->mtx too.
        "60000000 60000000",  # nop ; nop (common block already stored 0x98 and loaded r11)
        # Guard: if we don't have a valid jobj pointer, skip the root capture block.
        "280B0000 41820064",  # cmplwi r11,0 ; beq end
        "394B0000 38000020",  # addi r10,r11,0 ; li r0,0x20 (max parent hops)
        "812A000C 28090000",  # (loop) lwz r9,0x0C(r10) ; cmplwi r9,0
        "41820010 39490000",  # beq done ; addi r10,r9,0
        "3800FFFF 2C000000",  # addi r0,r0,-1 ; cmpwi r0,0
        "4082FFE4 914C009C",  # bne loop ; (done) stw r10,0x9C(r12)
        "394A0044 60000000",  # addi r10,r10,0x44 ; nop
        "C06A0000 D06C00A0",  # lfs f3,0x00(r10) ; stfs f3,0xA0(r12)
        "C06A0004 D06C00A4",  # lfs f3,0x04(r10) ; stfs f3,0xA4(r12)
        "C06A0008 D06C00A8",  # lfs f3,0x08(r10) ; stfs f3,0xA8(r12)
        "C06A000C D06C00AC",  # lfs f3,0x0C(r10) ; stfs f3,0xAC(r12)
        "C06A0010 D06C00B0",  # lfs f3,0x10(r10) ; stfs f3,0xB0(r12)
        "C06A0014 D06C00B4",  # lfs f3,0x14(r10) ; stfs f3,0xB4(r12)
        "C06A0018 D06C00B8",  # lfs f3,0x18(r10) ; stfs f3,0xB8(r12)
        "C06A001C D06C00BC",  # lfs f3,0x1C(r10) ; stfs f3,0xBC(r12)
        "C06A0020 D06C00C0",  # lfs f3,0x20(r10) ; stfs f3,0xC0(r12)
        "C06A0024 D06C00C4",  # lfs f3,0x24(r10) ; stfs f3,0xC4(r12)
        "C06A0028 D06C00C8",  # lfs f3,0x28(r10) ; stfs f3,0xC8(r12)
        "C06A002C D06C00CC",  # lfs f3,0x2C(r10) ; stfs f3,0xCC(r12)
        "60000000 00000000",  # end
    ]
    post_lines = post_lines_common + (post_lines_root if capture_root else post_lines_no_root)

    gecko_block = "\n".join(
        [
            code_name,
            # Init
            f"{0x04000000 | base_gecko:08X} 00000000",
            f"{0x04000000 | (base_gecko + 0x04):08X} 00000000",
            f"{0x04000000 | (base_gecko + 0x08):08X} 00000000",
            f"{0x04000000 | (base_gecko + 0x0C):08X} 00000000",
            f"{0x04000000 | (base_gecko + 0x10):08X} 00000000",
            f"{0x04000000 | (base_gecko + 0x14):08X} 00000000",
            # Hook
            # C2 0E5FE0: overwrite `addi r4, r1, 0x2c`, run capture, then resume at 0x800E5FE4 (`bl it_8029C6A4`).
            "C20E5FE0 00000007",
            f"3881002C 3D80{base_hi:04X}",  # addi r4,r1,0x2c ; lis r12,base_hi
            f"618C{base_lo:04X} C001002C",  # ori r12,r12,base_lo ; lfs f0,0x2c(r1)
            "D00C0000 C0610030",  # stfs f0,0(r12) ; lfs f3,0x30(r1)
            "D06C0004 C0810034",  # stfs f3,4(r12) ; lfs f4,0x34(r1)
            "D08C0008 800C000C",  # stfs f4,8(r12) ; lwz r0,0x0C(r12)
            "38000001 900C000C",  # addi r0,r0,1 ; stw r0,0x0C(r12)
            "60000000 00000000",  # nop ; end
            # Also capture the Vec3 that is passed into `it_8029C6A4` directly, along with caller LR.
            "C229C6A4 00000007",
            f"7C0802A6 3D80{base_hi:04X}",  # mflr r0 (original) ; lis r12,base_hi
            f"618C{base_lo:04X} 900C0010",  # ori r12,r12,base_lo ; stw r0,0x10(r12)
            "C0040000 D00C0000",  # lfs f0,0(r4) ; stfs f0,0(r12)
            "C0640004 D06C0004",  # lfs f3,4(r4) ; stfs f3,4(r12)
            "C0840008 D08C0008",  # lfs f4,8(r4) ; stfs f4,8(r12)
            "816C000C 396B0001",  # lwz r11,0x0C(r12) ; addi r11,r11,1
            "916C000C 00000000",  # stw r11,0x0C(r12) ; end
            # Capture the exact `HSD_JObj::mtx` used by `lb_8000B1CC` for blaster shots.
            #
            # Hook point is inside `ftFx_SpecialN_Shoot` (see `refs/melee/build/.../ftFx_SpecialN.s`):
            #   0x800E68CC: `addi r5, r1, 0x3c` just before `bl lb_8000B1CC`
            #
            # At this point:
            # - r3 = HSD_JObj*
            # - r4 = &offset (stack, r1+0x2c)
            "C20E68CC 00000013",
            f"38A1003C 3D80{base_hi:04X}",  # addi r5,r1,0x3c (original) ; lis r12,base_hi
            f"618C{base_lo:04X} 906C0014",  # ori r12,r12,base_lo ; stw r3,0x14(r12)
            "39630044 C00B0000",  # addi r11,r3,0x44 ; lfs f0,0x00(r11)
            "D00C0020 C00B0004",  # stfs f0,0x20(r12) ; lfs f0,0x04(r11)
            "D00C0024 C00B0008",  # stfs f0,0x24(r12) ; lfs f0,0x08(r11)
            "D00C0028 C00B000C",  # stfs f0,0x28(r12) ; lfs f0,0x0C(r11)
            "D00C002C C00B0010",  # stfs f0,0x2C(r12) ; lfs f0,0x10(r11)
            "D00C0030 C00B0014",  # stfs f0,0x30(r12) ; lfs f0,0x14(r11)
            "D00C0034 C00B0018",  # stfs f0,0x34(r12) ; lfs f0,0x18(r11)
            "D00C0038 C00B001C",  # stfs f0,0x38(r12) ; lfs f0,0x1C(r11)
            "D00C003C C00B0020",  # stfs f0,0x3C(r12) ; lfs f0,0x20(r11)
            "D00C0040 C00B0024",  # stfs f0,0x40(r12) ; lfs f0,0x24(r11)
            "D00C0044 C00B0028",  # stfs f0,0x44(r12) ; lfs f0,0x28(r11)
            "D00C0048 C00B002C",  # stfs f0,0x48(r12) ; lfs f0,0x2C(r11)
            "D00C004C C001002C",  # stfs f0,0x4C(r12) ; lfs f0,0x2C(r1)
            "D00C0050 C0010030",  # stfs f0,0x50(r12) ; lfs f0,0x30(r1)
            "D00C0054 C0010034",  # stfs f0,0x54(r12) ; lfs f0,0x34(r1)
            "D00C0058 60000000",  # stfs f0,0x58(r12) ; nop
            "60000000 00000000",  # nop ; end
            # Capture the *post* `lb_8000B1CC` output and updated `HSD_JObj::mtx` for blaster shots.
            #
            # Hook point: 0x800E68D4 (`lfs f0, "@244"@sda21(r0)`) right after `bl lb_8000B1CC`.
            #
            # Layout:
            # - +0x60..+0x8C: 12 f32 (jobj->mtx after lb has ensured it's up to date)
            # - +0x90..+0x98: 3 f32 (lb output vec at stack r1+0x3C, before the caller zeros z)
            # - +0x9C: u32 root jobj ptr (walked via parent pointers) [optional]
            # - +0xA0..+0xCC: 12 f32 (root jobj->mtx, after walk) [optional]
            f"C20E68D4 {len(post_lines):08X}",
            *post_lines,
        ]
    )

    ini_text = ini_path.read_text()
    ini_text = _update_gecko_ini(ini_text=ini_text, code_name=code_name, gecko_block=gecko_block)
    ini_path.write_text(ini_text)


def _inject_gecko_capture_dynbones(console) -> None:
    """Inject a Gecko hook to capture dynamic-bone vectors at lb_8001044C (pre-mod step)."""

    ini_path = Path(console._get_dolphin_home_path()) / "GameSettings" / "GALE01r2.ini"  # noqa: SLF001
    if not ini_path.exists():
        raise FileNotFoundError(f"expected Dolphin gecko ini at {ini_path}")

    code_name = "$Debug: Capture DynBones Vecs"
    base = DEBUG_DYN_BONES_CAPTURE_BASE
    base_hi = (base >> 16) & 0xFFFF
    base_lo = base & 0xFFFF
    base_gecko = base - 0x8000_0000
    if base_gecko < 0 or base_gecko > 0x01FF_FFFF:
        raise ValueError(f"capture base not in MEM1: {base:#010x}")

    # Hook point: 0x800107C0 (just after v_base/v_dyn/v_next normalization and before copies).
    hook_lines = [
        # Capture jobj ptr (r23), node ptr (r24), and v_next/v_dyn/v_base stack vectors.
        f"3D80{base_hi:04X} 618C{base_lo:04X}",  # lis r12,hi ; ori r12,lo
        "92EC0000 930C0004",  # stw r23,0(r12) ; stw r24,4(r12)
        "816C000C 396B0001",  # lwz r11,0x0C(r12) ; addi r11,1
        "916C000C 816102E8",  # stw r11,0x0C(r12) ; lwz r11,0x2E8(r1) (v_prev copy slot)
        "916C0010 816102EC",  # stw r11,0x10(r12) ; lwz r11,0x2EC(r1)
        "916C0014 816102F0",  # stw r11,0x14(r12) ; lwz r11,0x2F0(r1)
        "916C0018 81610300",  # stw r11,0x18(r12) ; lwz r11,0x300(r1) (v_next)
        "916C001C 81610304",  # stw r11,0x1C(r12) ; lwz r11,0x304(r1)
        "916C0020 81610308",  # stw r11,0x20(r12) ; lwz r11,0x308(r1)
        "916C0024 8161030C",  # stw r11,0x24(r12) ; lwz r11,0x30C(r1) (v_dyn)
        "916C0028 81610310",  # stw r11,0x28(r12) ; lwz r11,0x310(r1)
        "916C002C 81610314",  # stw r11,0x2C(r12) ; lwz r11,0x314(r1)
        "916C0030 81610318",  # stw r11,0x30(r12) ; lwz r11,0x318(r1) (v_base)
        "916C0034 8161031C",  # stw r11,0x34(r12) ; lwz r11,0x31C(r1)
        "916C0038 81610320",  # stw r11,0x38(r12) ; lwz r11,0x320(r1)
        "916C003C 80010300",  # stw r11,0x3C(r12) ; lwz r0,0x300(r1) (orig)
        "8178002C 916C0040",  # lwz r11,0x2C(r24) ; stw r11,0x40(r12)
        "81780030 916C0044",  # lwz r11,0x30(r24) ; stw r11,0x44(r12)
        "81780034 916C0048",  # lwz r11,0x34(r24) ; stw r11,0x48(r12)
        "81780058 916C004C",  # lwz r11,0x58(r24) ; stw r11,0x4C(r12)
        "8178005C 916C0050",  # lwz r11,0x5C(r24) ; stw r11,0x50(r12)
        "81780060 916C0054",  # lwz r11,0x60(r24) ; stw r11,0x54(r12)
        "81780000 916C0058",  # lwz r11,0x00(r24) ; stw r11,0x58(r12) (node jobj ptr)
        "81780090 916C005C",  # lwz r11,0x90(r24) ; stw r11,0x5C(r12) (node next ptr)
        "39600000 916C0060",  # li r11,0 ; stw r11,0x60(r12) (next pos x placeholder)
        "916C0064 916C0068",  # stw r11,0x64(r12) ; stw r11,0x68(r12) (next pos y/z)
        "816B001C 916C006C",  # lwz r11,0x1C(r23) ; stw r11,0x6C(r12) (jobj rot x)
        "816B0020 916C0070",  # lwz r11,0x20(r23) ; stw r11,0x70(r12) (jobj rot y)
        "816B0024 916C0074",  # lwz r11,0x24(r23) ; stw r11,0x74(r12) (jobj rot z)
        "816B0028 916C0078",  # lwz r11,0x28(r23) ; stw r11,0x78(r12) (jobj rot w)
        "816B0038 916C007C",  # lwz r11,0x38(r23) ; stw r11,0x7C(r12) (jobj trans x)
        "816B003C 916C0080",  # lwz r11,0x3C(r23) ; stw r11,0x80(r12) (jobj trans y)
        "816B0040 916C0084",  # lwz r11,0x40(r23) ; stw r11,0x84(r12) (jobj trans z)
        "816B002C 916C0088",  # lwz r11,0x2C(r23) ; stw r11,0x88(r12) (jobj scale x)
        "816B0030 916C008C",  # lwz r11,0x30(r23) ; stw r11,0x8C(r12) (jobj scale y)
        "816B0034 916C0090",  # lwz r11,0x34(r23) ; stw r11,0x90(r12) (jobj scale z)
        "60000000 00000000",  # nop ; end
    ]
    gecko_block = "\n".join(
        [
            code_name,
            # Init
            f"{0x04000000 | base_gecko:08X} 00000000",
            f"{0x04000000 | (base_gecko + 0x04):08X} 00000000",
            f"{0x04000000 | (base_gecko + 0x08):08X} 00000000",
            f"{0x04000000 | (base_gecko + 0x0C):08X} 00000000",
            # Hook
            f"C20107C0 {len(hook_lines):08X}",
            *hook_lines,
        ]
    )

    ini_text = ini_path.read_text()
    ini_text = _update_gecko_ini(ini_text=ini_text, code_name=code_name, gecko_block=gecko_block)
    ini_path.write_text(ini_text)


def _as_unit_f32(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float32)
    x = np.where(np.isfinite(x), x, 0.0).astype(np.float32, copy=False)
    x = np.where(x == MASK_F32, 0.0, x).astype(np.float32, copy=False)
    return np.clip(x, -1.0, 1.0).astype(np.float32, copy=False)


def _as_u8_bool(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.uint8)
    return np.where(x == MASK_U8, 0, x).astype(np.uint8, copy=False)


def _as_trigger(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float32)
    x = np.where(np.isfinite(x), x, 0.0).astype(np.float32, copy=False)
    x = np.where(x == MASK_F32, 0.0, x).astype(np.float32, copy=False)
    return np.clip(x, 0.0, 1.0).astype(np.float32, copy=False)


@dataclass(frozen=True)
class ReplayInputs:
    frames: np.ndarray  # int32, post-frame ids (canonicalized, contiguous)
    p1: dict[str, np.ndarray]
    p2: dict[str, np.ndarray]
    post: dict[str, np.ndarray]


def load_replay_inputs(replay: str) -> ReplayInputs:
    rb = read_replay_bytes(replay)
    sample = canonicalize_slippi_sample_last(peppi_bytes.read_slippi_bytes_sample(rb, 0, False))
    frames = sample["frame"].astype(np.int32, copy=False)

    def p(port: int) -> dict[str, np.ndarray]:
        pfx = f"p{port}"
        out: dict[str, np.ndarray] = {}
        out["stick_x"] = _as_unit_f32(sample[f"{pfx}_main_stick_x"])
        out["stick_y"] = _as_unit_f32(sample[f"{pfx}_main_stick_y"])
        out["cstick_x"] = _as_unit_f32(sample[f"{pfx}_c_stick_x"])
        out["cstick_y"] = _as_unit_f32(sample[f"{pfx}_c_stick_y"])
        out["l"] = _as_u8_bool(sample[f"{pfx}_button_l"]) if f"{pfx}_button_l" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["r"] = _as_u8_bool(sample[f"{pfx}_button_r"]) if f"{pfx}_button_r" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["a"] = _as_u8_bool(sample[f"{pfx}_button_a"]) if f"{pfx}_button_a" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["b"] = _as_u8_bool(sample[f"{pfx}_button_b"]) if f"{pfx}_button_b" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["x"] = _as_u8_bool(sample[f"{pfx}_button_x"]) if f"{pfx}_button_x" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["y"] = _as_u8_bool(sample[f"{pfx}_button_y"]) if f"{pfx}_button_y" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["z"] = _as_u8_bool(sample[f"{pfx}_button_z"]) if f"{pfx}_button_z" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["start"] = _as_u8_bool(sample[f"{pfx}_button_start"]) if f"{pfx}_button_start" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["d_up"] = _as_u8_bool(sample[f"{pfx}_button_d_up"]) if f"{pfx}_button_d_up" in sample else np.zeros_like(frames, dtype=np.uint8)
        out["l_shoulder"] = _as_trigger(sample[f"{pfx}_l_shoulder"]) if f"{pfx}_l_shoulder" in sample else _as_trigger(sample[f"{pfx}_trigger"])
        out["r_shoulder"] = _as_trigger(sample[f"{pfx}_r_shoulder"]) if f"{pfx}_r_shoulder" in sample else _as_trigger(sample[f"{pfx}_trigger"])
        return out

    post = {
        "p1_x": sample["p1_position_x"].astype(np.float32, copy=False),
        "p1_y": sample["p1_position_y"].astype(np.float32, copy=False),
        "p2_x": sample["p2_position_x"].astype(np.float32, copy=False),
        "p2_y": sample["p2_position_y"].astype(np.float32, copy=False),
        "p1_action": sample["p1_action"].astype(np.uint16, copy=False),
        "p2_action": sample["p2_action"].astype(np.uint16, copy=False),
        "p1_og": _as_u8_bool(sample["p1_on_ground"]),
        "p2_og": _as_u8_bool(sample["p2_on_ground"]),
    }
    if "p1_percent" in sample:
        post["p1_percent"] = sample["p1_percent"].astype(np.float32, copy=False)
    if "p2_percent" in sample:
        post["p2_percent"] = sample["p2_percent"].astype(np.float32, copy=False)
    if "p1_animation_index" in sample:
        post["p1_anim"] = sample["p1_animation_index"].astype(np.uint32, copy=False)
    if "p2_animation_index" in sample:
        post["p2_anim"] = sample["p2_animation_index"].astype(np.uint32, copy=False)

    return ReplayInputs(frames=frames, p1=p(1), p2=p(2), post=post)


def main() -> int:
    ap = argparse.ArgumentParser(description="Run Dolphin + replay inputs, and read live ECB values via dolphin_memory_engine.")
    ap.add_argument("--replay", required=True, type=str)
    ap.add_argument("--dolphin", default=None, type=str, help="path to dolphin-emu (must be hookable by dolphin_memory_engine)")
    ap.add_argument("--appimage", default=str(Path.cwd() / "Slippi_Online-x86_64-ExiAI.AppImage"), type=str)
    ap.add_argument("--iso", default=str(Path.cwd() / "SSBM.iso"))
    ap.add_argument("--libmelee", default=str(LIBMELEE_ROOT_DEFAULT))
    ap.add_argument("--start_frame", type=int, default=10)
    ap.add_argument("--end_frame", type=int, default=25)
    ap.add_argument("--input_offset", type=int, default=0, help="Replay input index offset applied before feeding controllers")
    ap.add_argument("--out", type=str, default="/tmp/dolphin_mem_probe_ecb.json")
    ap.add_argument("--max_seconds", type=float, default=60.0)
    ap.add_argument("--max_menu_steps", type=int, default=6000)
    ap.add_argument(
        "--i-accept-risk",
        action="store_true",
        help="Allow longer/unsafe probe runs. Live probes can wedge Dolphin and may take down your terminal; prefer `timeout` and small frame windows.",
    )
    ap.add_argument("--capture_sp2c", action="store_true", help="Inject a Gecko hook to capture blaster sp2C (debug).")
    ap.add_argument(
        "--capture_sp2c_root",
        action="store_true",
        help="(debug) Extend the sp2C hook to walk jobj parent pointers and capture the root matrix (riskier).",
    )
    ap.add_argument(
        "--capture_dynbones",
        action="store_true",
        help="Inject a Gecko hook to capture dyn-bone vectors in lb_8001044C (debug).",
    )
    args = ap.parse_args()
    verbose = os.environ.get("DOLPHIN_PROBE_VERBOSE") == "1"
    suppress = os.environ.get("SSBM_SUPPRESS_DOLPHIN_PROBE_WARNING") == "1"
    if os.environ.get("SSBM_SUPPRESS_DOLPHIN_PROBE_WARNING") != "1":
        print(
            "WARNING: live Dolphin memory probes can wedge Dolphin and may take down your terminal.\n"
            "Run in tmux/separate terminal and prefer `timeout` + small `--max_seconds` + small frame windows.",
            file=sys.stderr,
            flush=True,
        )
    # Guardrail: avoid accidentally running very long/unsafe probes (these have historically wedged
    # Dolphin and taken down the surrounding terminal session).
    frame_window = int(args.end_frame) - int(args.start_frame)
    if not args.i_accept_risk and not suppress:
        if args.max_seconds <= 0:
            raise ValueError("refusing to run with --max_seconds <= 0 without --i-accept-risk")
        if args.max_seconds > 60.0:
            raise ValueError("refusing to run with --max_seconds > 60 without --i-accept-risk")
        if frame_window > 120:
            raise ValueError("refusing to probe a large frame window (>120) without --i-accept-risk")

    def vprint(*a: object) -> None:
        if verbose:
            print(*a, flush=True)

    _add_libmelee_to_path(Path(args.libmelee))
    import melee  # noqa: E402
    from melee import enums  # noqa: E402

    import dolphin_memory_engine as dme  # noqa: E402

    r = load_replay_inputs(args.replay)
    replay_sample = canonicalize_slippi_sample_last(
        peppi_bytes.read_slippi_bytes_sample(read_replay_bytes(args.replay), 0, False)
    )
    min_frame = int(r.frames[0])
    max_frame = int(r.frames[-1])
    if args.start_frame < min_frame or args.end_frame > max_frame:
        raise ValueError(f"frame window [{args.start_frame},{args.end_frame}] outside replay [{min_frame},{max_frame}]")

    dolphin_path = Path(args.dolphin) if args.dolphin is not None else None
    if dolphin_path is None:
        dolphin_path = _ensure_extracted_dolphin(Path(args.appimage))
    else:
        dolphin_path = dolphin_path.resolve()

    iso_path = Path(args.iso).resolve()
    if not iso_path.exists():
        raise FileNotFoundError(f"missing ISO: {iso_path}")

    t0 = time.monotonic()
    console = None
    controllers = None
    try:
        vprint("init console")
        console = melee.Console(
            path=str(dolphin_path),
            slippi_address="127.0.0.1",
            online_delay=0,
            blocking_input=True,
            copy_home_directory=False,
            setup_gecko_codes=True,
            save_replays=False,
            use_exi_inputs=True,
            enable_ffw=True,
            gfx_backend="Null",
            disable_audio=True,
            infinite_time=False,
            fullscreen=False,
        )

        # Best-effort cleanup when this script is terminated (e.g. `timeout` SIGTERM).
        def _handle_term(_signum: int, _frame) -> None:  # type: ignore[no-untyped-def]
            try:
                if console is not None:
                    console.stop()
            finally:
                raise SystemExit(2)

        signal.signal(signal.SIGTERM, _handle_term)
        signal.signal(signal.SIGINT, _handle_term)

        controllers = {
            1: melee.Controller(console=console, port=1, type=enums.ControllerType.STANDARD),
            2: melee.Controller(console=console, port=2, type=enums.ControllerType.STANDARD),
        }
        menu = melee.MenuHelper(is_singles=True)

        if args.capture_sp2c:
            vprint("inject gecko")
            _inject_gecko_capture_sp2c(console, capture_root=bool(args.capture_sp2c_root))
        if args.capture_dynbones:
            vprint("inject dyn-bones gecko")
            _inject_gecko_capture_dynbones(console)

        vprint("launch dolphin")
        console.run(iso_path=str(iso_path))
        vprint("connect")
        if not console.connect():
            raise RuntimeError("failed to connect to dolphin slippstream")
        for c in controllers.values():
            if not c.connect():
                raise RuntimeError(f"failed to connect controller port {c.port}")

        # Wait for dme hook.
        vprint("hook dme")
        while True:
            dme.hook()
            if dme.is_hooked():
                break
            if time.monotonic() - t0 > min(args.max_seconds, 30.0):
                raise RuntimeError(f"dolphin_memory_engine failed to hook (status={dme.get_status()})")
            time.sleep(0.05)
        vprint("dme hooked")

        # Menu to a local VS match with requested characters/stage.
        # Prefer replay character ids when present (fallback to Fox/Falco).
        p1_char = None
        p2_char = None
        try:
            if "p1_character" in replay_sample:
                p1_char = int(replay_sample["p1_character"][0])
            if "p2_character" in replay_sample:
                p2_char = int(replay_sample["p2_character"][0])
        except Exception:
            p1_char = None
            p2_char = None

        def _map_char(cid: int | None):
            if cid == 22:
                return enums.Character.FALCO
            if cid == 1:
                return enums.Character.FOX
            return enums.Character.FOX

        stage = enums.Stage.FINAL_DESTINATION
        gamestate = None
        menu_steps = 0
        while True:
            if time.monotonic() - t0 > args.max_seconds:
                raise TimeoutError("timed out while menuing")
            if menu_steps > args.max_menu_steps:
                raise TimeoutError("exceeded max menu steps")

            gamestate = console.step()
            menu_steps += 1
            if gamestate is None:
                continue
            if gamestate.menu_state in [enums.Menu.IN_GAME, enums.Menu.SUDDEN_DEATH]:
                break
            if verbose and menu_steps % 120 == 0:
                vprint("menu step", menu_steps, "menu_state", gamestate.menu_state)
            menu.menu_helper_simple(
                gamestate,
                controllers[1],
                character_selected=_map_char(p1_char),
                stage_selected=stage,
                autostart=True,
                costume=0,
                connect_code="",
            )
            menu.menu_helper_simple(
                gamestate,
                controllers[2],
                character_selected=_map_char(p2_char),
                stage_selected=stage,
                autostart=True,
                costume=1,
                connect_code="",
            )

        assert gamestate is not None
        vprint("in game at frame", int(gamestate.frame), "menu_state", gamestate.menu_state)

        # Helpers: memory reads (PPC big-endian).
        import struct  # noqa: E402

        def f32_at(addr: int) -> float:
            bits = int(dme.read_word(addr)) & 0xFFFF_FFFF
            return struct.unpack(">f", struct.pack(">I", bits))[0]

        def u8_at(addr: int) -> int:
            return int(dme.read_byte(addr)) & 0xFF

        def u16_at(addr: int) -> int:
            b = bytes(dme.read_bytes(addr, 2))
            return int.from_bytes(b, "big", signed=False)

        def s16_at(addr: int) -> int:
            b = bytes(dme.read_bytes(addr, 2))
            return int.from_bytes(b, "big", signed=True)

        def u32_at(addr: int) -> int:
            return int(dme.read_word(addr)) & 0xFFFF_FFFF

        PLAYER_SLOTS = 0x80453080
        STATIC_PLAYER_SIZE = 0xE90
        GOBJ_USER_DATA_OFF = 0x2C
        GOBJ_NEXT_OFF = 0x08
        HSD_JOBJ_PARENT_OFF = 0x0C
        HSD_JOBJ_TRANSLATE_OFF = 0x38
        HSD_JOBJ_MTX_OFF = 0x44
        HSD_JOBJ_ID_OFF = 0x84
        HSD_JOBJ_FLAGS_OFF = 0x14
        HSD_JOBJ_AOBJ_OFF = 0x7C
        HSD_JOBJ_ROBJ_OFF = 0x80
        HSD_AOBJ_HSD_OBJ_OFF = 0x18
        HSD_AOBJ_FOBJ_OFF = 0x14

        # Fighter struct fields (from Slippi post-frame + decomp headers).
        FIGHTER_SELF_VEL_X_OFF = 0x80
        FIGHTER_GR_VEL_OFF = 0xEC
        FIGHTER_PLAYER_NUDGE_X_OFF = 0xF8
        FIGHTER_PLAYER_NUDGE_Z_OFF = 0xFC
        FIGHTER_POS_X_OFF = 0xB0
        FIGHTER_POS_Y_OFF = 0xB4
        FIGHTER_POS_Z_OFF = 0xB8
        # FighterHurtCapsule list (refs/melee/src/melee/lb/types.h + ft/ftcoll.s).
        FIGHTER_HURTCAPS_OFF = 0x11A0
        FIGHTER_HURTCAPS_LEN_OFF = 0x119E  # u8
        HURTCAP_STRIDE = 0x4C
        # Decomp source of truth: `refs/melee/src/melee/lb/types.h`
        # - `HurtCapsule.scale` @ 0x1C
        # - `HurtCapsule.a_pos` @ 0x28
        # - `HurtCapsule.b_pos` @ 0x34
        HURTCAP_SCALE_OFF = 0x1C
        HURTCAP_END1_X_OFF = 0x28
        HURTCAP_END1_Y_OFF = 0x2C
        HURTCAP_END1_Z_OFF = 0x30
        HURTCAP_END2_X_OFF = 0x34
        HURTCAP_END2_Y_OFF = 0x38
        HURTCAP_END2_Z_OFF = 0x3C
        HURTCAP_HEIGHT_OFF = 0x44  # u32 (0=low,1=mid,2=high)
        FIGHTER_POS_DELTA_X_OFF = 0xC8
        FIGHTER_XA4_UNK_VEL_X_OFF = 0xA4
        # Fighter.dmg (refs/melee/src/melee/ft/types.h).
        FIGHTER_DMG_PERCENT_OFF = 0x1830
        FIGHTER_DMG_X184C_OFF = 0x184C
        FIGHTER_DMG_X1850_KB_APPLIED_OFF = 0x1850
        FIGHTER_DMG_X1854_COLLPOS_X_OFF = 0x1854
        FIGHTER_DMG_X1854_COLLPOS_Y_OFF = 0x1858
        FIGHTER_DMG_X1854_COLLPOS_Z_OFF = 0x185C
        FIGHTER_DMG_X1948_OFF = 0x1948
        FIGHTER_DMG_X194C_OFF = 0x194C

        # CollData floor surface (refs/melee/src/melee/lb/types.h).
        COLL_FLOOR_SURFACE_OFF = 0x83C - 0x6F0
        SURFACE_NORMAL_OFF = 0x08
        SURFACE_NORMAL_X_OFF = SURFACE_NORMAL_OFF + 0x00
        SURFACE_NORMAL_Y_OFF = SURFACE_NORMAL_OFF + 0x04

        # Item manager (r13 base 0x804DB6A0 from __start.s).
        R13_BASE = 0x804DB6A0
        ITEM_MANAGER_PTR = R13_BASE - 0x3E74
        ITEM_MANAGER_FIRST_GOBJ_OFF = 0x24
        MAX_ITEMS = 15  # matches slippi asm Recording/Recording.s
        FT_PARTS_TABLE_PTR = R13_BASE - 0x515C  # ftPartsTable@sda21 (see ftparts.s)
        FRAME_INDEX_PTR = R13_BASE - 0x49AC  # slippi frameIndex@sda21
        SP2C_CAPTURE_BASE = DEBUG_SP2C_CAPTURE_BASE
        DYN_BONES_CAPTURE_BASE = DEBUG_DYN_BONES_CAPTURE_BASE
        DISABLE_SUDDEN_DEATH_PATCH_ADDR = 0x801A5C08  # libmelee GALE01r2.ini: 041A5C08 38600000
        CREATE_BLASTER_SHOT_HOOK_ADDR = 0x800E5FE0
        CREATE_BLASTER_SHOT_ADDR = 0x800E5F28

        # HSD_FObj layout (refs/melee/src/sysdolphin/baselib/fobj.h).
        HSD_FOBJ_NEXT_OFF = 0x00
        HSD_FOBJ_AD_OFF = 0x04
        HSD_FOBJ_AD_HEAD_OFF = 0x08
        HSD_FOBJ_LENGTH_OFF = 0x0C
        HSD_FOBJ_FLAGS_OFF = 0x10
        HSD_FOBJ_OP_OFF = 0x11
        HSD_FOBJ_OP_INTRP_OFF = 0x12
        HSD_FOBJ_OBJ_TYPE_OFF = 0x13
        HSD_FOBJ_FRAC_VALUE_OFF = 0x14
        HSD_FOBJ_FRAC_SLOPE_OFF = 0x15
        HSD_FOBJ_NB_PACK_OFF = 0x16
        HSD_FOBJ_STARTFRAME_OFF = 0x18
        HSD_FOBJ_FTERM_OFF = 0x1A
        HSD_FOBJ_TIME_OFF = 0x1C
        HSD_FOBJ_P0_OFF = 0x20
        HSD_FOBJ_P1_OFF = 0x24
        HSD_FOBJ_D0_OFF = 0x28
        HSD_FOBJ_D1_OFF = 0x2C

        def find_fobj_of_type(aobj_ptr: int, obj_type: int) -> dict | None:
            if aobj_ptr == 0:
                return None
            fobj_ptr = u32_at(aobj_ptr + HSD_AOBJ_FOBJ_OFF)
            steps = 0
            while fobj_ptr != 0 and steps < 128:
                steps += 1
                ot = u8_at(fobj_ptr + HSD_FOBJ_OBJ_TYPE_OFF)
                if ot == obj_type:
                    ad = u32_at(fobj_ptr + HSD_FOBJ_AD_OFF)
                    ad_head = u32_at(fobj_ptr + HSD_FOBJ_AD_HEAD_OFF)
                    return {
                        "ptr": int(fobj_ptr),
                        "flags": int(u8_at(fobj_ptr + HSD_FOBJ_FLAGS_OFF)),
                        "op": int(u8_at(fobj_ptr + HSD_FOBJ_OP_OFF)),
                        "op_intrp": int(u8_at(fobj_ptr + HSD_FOBJ_OP_INTRP_OFF)),
                        "obj_type": int(ot),
                        "frac_value": int(u8_at(fobj_ptr + HSD_FOBJ_FRAC_VALUE_OFF)),
                        "frac_slope": int(u8_at(fobj_ptr + HSD_FOBJ_FRAC_SLOPE_OFF)),
                        "nb_pack": int(u16_at(fobj_ptr + HSD_FOBJ_NB_PACK_OFF)),
                        "startframe": int(s16_at(fobj_ptr + HSD_FOBJ_STARTFRAME_OFF)),
                        "fterm": int(u16_at(fobj_ptr + HSD_FOBJ_FTERM_OFF)),
                        "time": float(f32_at(fobj_ptr + HSD_FOBJ_TIME_OFF)),
                        "p0": float(f32_at(fobj_ptr + HSD_FOBJ_P0_OFF)),
                        "p1": float(f32_at(fobj_ptr + HSD_FOBJ_P1_OFF)),
                        "d0": float(f32_at(fobj_ptr + HSD_FOBJ_D0_OFF)),
                        "d1": float(f32_at(fobj_ptr + HSD_FOBJ_D1_OFF)),
                        "ad_off": int(ad - ad_head) if (ad and ad_head) else None,
                        "len": int(u32_at(fobj_ptr + HSD_FOBJ_LENGTH_OFF)),
                    }
                fobj_ptr = u32_at(fobj_ptr + HSD_FOBJ_NEXT_OFF)
            return None

        def jobj_world_pos(jobj_ptr: int) -> tuple[float, float, float]:
            """Mirror lb_8000B1CC 'no offset' behavior for non-root joints."""
            if jobj_ptr == 0:
                return (0.0, 0.0, 0.0)
            parent = u32_at(jobj_ptr + HSD_JOBJ_PARENT_OFF)
            if parent != 0:
                # Mtx is 3x4; translation is column 3 at [0][3], [1][3], [2][3].
                x = f32_at(jobj_ptr + HSD_JOBJ_MTX_OFF + 0x0C)
                y = f32_at(jobj_ptr + HSD_JOBJ_MTX_OFF + 0x1C)
                z = f32_at(jobj_ptr + HSD_JOBJ_MTX_OFF + 0x2C)
                return (x, y, z)
            x = f32_at(jobj_ptr + HSD_JOBJ_TRANSLATE_OFF + 0x00)
            y = f32_at(jobj_ptr + HSD_JOBJ_TRANSLATE_OFF + 0x04)
            z = f32_at(jobj_ptr + HSD_JOBJ_TRANSLATE_OFF + 0x08)
            return (x, y, z)

        def fighter_ptr_for_port(port: int) -> int:
            slot = port - 1
            base = PLAYER_SLOTS + slot * STATIC_PLAYER_SIZE
            transformed0 = u8_at(base + 0x0C)
            gobj_ptr = u32_at(base + 0xB0 + transformed0 * 4)
            return u32_at(gobj_ptr + GOBJ_USER_DATA_OFF)

        # Align to replay's earliest frame.
        while int(gamestate.frame) < min_frame:
            if time.monotonic() - t0 > args.max_seconds:
                raise TimeoutError("timed out while aligning to replay start frame")
            gamestate = console.step()
            if gamestate is None:
                continue
        if int(gamestate.frame) != min_frame:
            raise RuntimeError(f"cannot align: game.frame={int(gamestate.frame)} but replay starts at {min_frame}")

        out_rows: list[dict] = []

        def apply_inputs(port: int, idx: int) -> None:
            ctrl = controllers[port]
            pi = r.p1 if port == 1 else r.p2
            idx = idx + int(args.input_offset)
            if idx < 0 or idx >= len(r.frames):
                return
            ctrl.tilt_analog_unit(enums.Button.BUTTON_MAIN, float(pi["stick_x"][idx]), float(pi["stick_y"][idx]))
            ctrl.tilt_analog_unit(enums.Button.BUTTON_C, float(pi["cstick_x"][idx]), float(pi["cstick_y"][idx]))
            ctrl.press_shoulder(enums.Button.BUTTON_L, float(pi["l_shoulder"][idx]))
            ctrl.press_shoulder(enums.Button.BUTTON_R, float(pi["r_shoulder"][idx]))

            def set_btn(name: str, b: enums.Button) -> None:
                pressed = int(pi[name][idx]) != 0
                if pressed:
                    ctrl.press_button(b)
                else:
                    ctrl.release_button(b)

            set_btn("a", enums.Button.BUTTON_A)
            set_btn("b", enums.Button.BUTTON_B)
            set_btn("x", enums.Button.BUTTON_X)
            set_btn("y", enums.Button.BUTTON_Y)
            set_btn("z", enums.Button.BUTTON_Z)
            set_btn("l", enums.Button.BUTTON_L)
            set_btn("r", enums.Button.BUTTON_R)
            set_btn("start", enums.Button.BUTTON_START)
            set_btn("d_up", enums.Button.BUTTON_D_UP)

        # Main loop.
        while True:
            if time.monotonic() - t0 > args.max_seconds:
                raise TimeoutError("timed out while running replay input loop")

            frame = int(gamestate.frame)
            idx = frame - min_frame
            if idx < 0 or idx >= len(r.frames):
                break
            if int(r.frames[idx]) != frame:
                raise RuntimeError(f"frame/index mismatch: replay.frames[{idx}]={int(r.frames[idx])} but game.frame={frame}")

            # Read live memory state for ports 1 and 2.
            row: dict = {
                "frame": int(frame),
                "frame_index": int(u32_at(FRAME_INDEX_PTR)),
            }
            if args.capture_sp2c:
                row["gecko_disable_sudden_death_instr"] = int(u32_at(DISABLE_SUDDEN_DEATH_PATCH_ADDR))
                row["gecko_create_blaster_shot_hook_word"] = int(u32_at(CREATE_BLASTER_SHOT_HOOK_ADDR))
                row["gecko_create_blaster_shot_word0"] = int(u32_at(CREATE_BLASTER_SHOT_ADDR))
                row["gecko_create_blaster_shot_word1"] = int(u32_at(CREATE_BLASTER_SHOT_ADDR + 4))
            if args.capture_sp2c:
                row["sp2c_capture_count"] = int(u32_at(SP2C_CAPTURE_BASE + 0x0C))
                row["sp2c_capture_lr"] = int(u32_at(SP2C_CAPTURE_BASE + 0x10))
                row["sp2c_capture_jobj_ptr"] = int(u32_at(SP2C_CAPTURE_BASE + 0x14))
                row["sp2c_capture"] = (
                    float(f32_at(SP2C_CAPTURE_BASE + 0x00)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x04)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x08)),
                )
                row["sp2c_capture_jobj_mtx"] = [
                    float(f32_at(SP2C_CAPTURE_BASE + 0x20 + off))
                    for off in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C)
                ]
                row["sp2c_capture_jobj_mtx_bits"] = [
                    int(u32_at(SP2C_CAPTURE_BASE + 0x20 + off))
                    for off in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C)
                ]
                row["sp2c_capture_offset"] = (
                    float(f32_at(SP2C_CAPTURE_BASE + 0x50)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x54)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x58)),
                )
                row["sp2c_capture_offset_bits"] = (
                    int(u32_at(SP2C_CAPTURE_BASE + 0x50)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0x54)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0x58)),
                )
                row["lb_capture_jobj_mtx"] = [
                    float(f32_at(SP2C_CAPTURE_BASE + 0x60 + off))
                    for off in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C)
                ]
                row["lb_capture_jobj_mtx_bits"] = [
                    int(u32_at(SP2C_CAPTURE_BASE + 0x60 + off))
                    for off in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C)
                ]
                row["lb_capture_out_vec"] = (
                    float(f32_at(SP2C_CAPTURE_BASE + 0x90)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x94)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x98)),
                )
                row["lb_capture_out_vec_bits"] = (
                    int(u32_at(SP2C_CAPTURE_BASE + 0x90)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0x94)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0x98)),
                )
                row["lb_capture_jobj_rot"] = (
                    float(f32_at(SP2C_CAPTURE_BASE + 0xD0)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0xD4)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0xD8)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0xDC)),
                )
                row["lb_capture_jobj_rot_bits"] = (
                    int(u32_at(SP2C_CAPTURE_BASE + 0xD0)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0xD4)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0xD8)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0xDC)),
                )
                row["lb_capture_jobj_scl"] = (
                    float(f32_at(SP2C_CAPTURE_BASE + 0xE0)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0xE4)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0xE8)),
                )
                row["lb_capture_jobj_scl_bits"] = (
                    int(u32_at(SP2C_CAPTURE_BASE + 0xE0)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0xE4)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0xE8)),
                )
                row["lb_capture_jobj_tr"] = (
                    float(f32_at(SP2C_CAPTURE_BASE + 0xEC)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0xF0)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0xF4)),
                )
                row["lb_capture_jobj_tr_bits"] = (
                    int(u32_at(SP2C_CAPTURE_BASE + 0xEC)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0xF0)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0xF4)),
                )
                row["lb_capture_parent_jobj_ptr"] = int(u32_at(SP2C_CAPTURE_BASE + 0x100))
                row["lb_capture_parent_jobj_mtx"] = [
                    float(f32_at(SP2C_CAPTURE_BASE + 0x104 + off))
                    for off in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C)
                ]
                row["lb_capture_parent_jobj_mtx_bits"] = [
                    int(u32_at(SP2C_CAPTURE_BASE + 0x104 + off))
                    for off in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C)
                ]
                row["lb_capture_parent_jobj_rot"] = (
                    float(f32_at(SP2C_CAPTURE_BASE + 0x140)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x144)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x148)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x14C)),
                )
                row["lb_capture_parent_jobj_rot_bits"] = (
                    int(u32_at(SP2C_CAPTURE_BASE + 0x140)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0x144)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0x148)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0x14C)),
                )
                row["lb_capture_parent_jobj_scl"] = (
                    float(f32_at(SP2C_CAPTURE_BASE + 0x150)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x154)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x158)),
                )
                row["lb_capture_parent_jobj_scl_bits"] = (
                    int(u32_at(SP2C_CAPTURE_BASE + 0x150)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0x154)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0x158)),
                )
                row["lb_capture_parent_jobj_tr"] = (
                    float(f32_at(SP2C_CAPTURE_BASE + 0x15C)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x160)),
                    float(f32_at(SP2C_CAPTURE_BASE + 0x164)),
                )
                row["lb_capture_parent_jobj_tr_bits"] = (
                    int(u32_at(SP2C_CAPTURE_BASE + 0x15C)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0x160)),
                    int(u32_at(SP2C_CAPTURE_BASE + 0x164)),
                )
                row["lb_capture_jobj_scl_ptr"] = int(u32_at(SP2C_CAPTURE_BASE + 0x168))
                row["lb_capture_parent_jobj_scl_ptr"] = int(u32_at(SP2C_CAPTURE_BASE + 0x16C))
                if row["lb_capture_jobj_scl_ptr"] != 0:
                    row["lb_capture_jobj_scl_world"] = (
                        float(f32_at(row["lb_capture_jobj_scl_ptr"] + 0x00)),
                        float(f32_at(row["lb_capture_jobj_scl_ptr"] + 0x04)),
                        float(f32_at(row["lb_capture_jobj_scl_ptr"] + 0x08)),
                    )
                if row["lb_capture_parent_jobj_scl_ptr"] != 0:
                    row["lb_capture_parent_jobj_scl_world"] = (
                        float(f32_at(row["lb_capture_parent_jobj_scl_ptr"] + 0x00)),
                        float(f32_at(row["lb_capture_parent_jobj_scl_ptr"] + 0x04)),
                        float(f32_at(row["lb_capture_parent_jobj_scl_ptr"] + 0x08)),
                    )
                if args.capture_sp2c_root:
                    row["lb_capture_root_jobj_ptr"] = int(u32_at(SP2C_CAPTURE_BASE + 0x9C))
                    row["lb_capture_root_jobj_mtx"] = [
                        float(f32_at(SP2C_CAPTURE_BASE + 0xA0 + off))
                        for off in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C)
                    ]
                    row["lb_capture_root_jobj_mtx_bits"] = [
                        int(u32_at(SP2C_CAPTURE_BASE + 0xA0 + off))
                        for off in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C)
                    ]
            if args.capture_dynbones:
                row["dyn_capture_count"] = int(u32_at(DYN_BONES_CAPTURE_BASE + 0x0C))
                row["dyn_capture_jobj_ptr"] = int(u32_at(DYN_BONES_CAPTURE_BASE + 0x00))
                row["dyn_capture_node_ptr"] = int(u32_at(DYN_BONES_CAPTURE_BASE + 0x04))
                row["dyn_capture_v_prev"] = (
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x10)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x14)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x18)),
                )
                row["dyn_capture_v_next"] = (
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x1C)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x20)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x24)),
                )
                row["dyn_capture_v_dyn"] = (
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x28)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x2C)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x30)),
                )
                row["dyn_capture_v_base"] = (
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x34)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x38)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x3C)),
                )
                row["dyn_capture_node_pos"] = (
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x40)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x44)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x48)),
                )
                row["dyn_capture_base_rot"] = (
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x4C)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x50)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x54)),
                )
                row["dyn_capture_node_jobj_ptr"] = int(u32_at(DYN_BONES_CAPTURE_BASE + 0x58))
                row["dyn_capture_node_next_ptr"] = int(u32_at(DYN_BONES_CAPTURE_BASE + 0x5C))
                row["dyn_capture_jobj_rot"] = [
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x6C)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x70)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x74)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x78)),
                ]
                row["dyn_capture_jobj_trans"] = [
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x7C)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x80)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x84)),
                ]
                row["dyn_capture_jobj_scale"] = [
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x88)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x8C)),
                    float(f32_at(DYN_BONES_CAPTURE_BASE + 0x90)),
                ]
                next_ptr = row["dyn_capture_node_next_ptr"]
                row["dyn_capture_next_jobj_ptr"] = int(u32_at(next_ptr + 0x00)) if next_ptr != 0 else 0
                node_ptr = row["dyn_capture_node_ptr"]
                if node_ptr != 0:
                    row["dyn_capture_node_desc_04"] = (
                        float(f32_at(node_ptr + 0x04)),
                        float(f32_at(node_ptr + 0x08)),
                        float(f32_at(node_ptr + 0x0C)),
                    )
                    row["dyn_capture_node_desc_10"] = float(f32_at(node_ptr + 0x10))
                    row["dyn_capture_node_desc_14"] = (
                        float(f32_at(node_ptr + 0x14)),
                        float(f32_at(node_ptr + 0x18)),
                        float(f32_at(node_ptr + 0x1C)),
                    )
                    row["dyn_capture_node_desc_20"] = (
                        float(f32_at(node_ptr + 0x20)),
                        float(f32_at(node_ptr + 0x24)),
                        float(f32_at(node_ptr + 0x28)),
                    )
                    row["dyn_capture_node_desc_2c"] = (
                        float(f32_at(node_ptr + 0x2C)),
                        float(f32_at(node_ptr + 0x30)),
                        float(f32_at(node_ptr + 0x34)),
                    )
                    row["dyn_capture_node_desc_38"] = (
                        float(f32_at(node_ptr + 0x38)),
                        float(f32_at(node_ptr + 0x3C)),
                        float(f32_at(node_ptr + 0x40)),
                    )
                    row["dyn_capture_node_desc_44"] = float(f32_at(node_ptr + 0x44))
                    row["dyn_capture_node_desc_48"] = float(f32_at(node_ptr + 0x48))
                    row["dyn_capture_node_desc_4c"] = (
                        float(f32_at(node_ptr + 0x4C)),
                        float(f32_at(node_ptr + 0x50)),
                    )
                    row["dyn_capture_node_desc_58"] = (
                        float(f32_at(node_ptr + 0x58)),
                        float(f32_at(node_ptr + 0x5C)),
                        float(f32_at(node_ptr + 0x60)),
                    )
                if next_ptr != 0:
                    row["dyn_capture_next_pos"] = (
                        float(f32_at(next_ptr + 0x2C)),
                        float(f32_at(next_ptr + 0x30)),
                        float(f32_at(next_ptr + 0x34)),
                    )
                else:
                    row["dyn_capture_next_pos"] = (0.0, 0.0, 0.0)
            for port in (1, 2):
                fp = fighter_ptr_for_port(port)
                row[f"p{port}_fp"] = int(fp)
                row[f"p{port}_motion_id"] = int(u32_at(fp + 0x10))
                row[f"p{port}_anim_id"] = int(u32_at(fp + 0x14))
                row[f"p{port}_kind"] = int(u32_at(fp + 0x04))
                row[f"p{port}_ground_or_air"] = int(u32_at(fp + 0xE0) & 0xFF)
                row[f"p{port}_scale_xyz"] = (
                    float(f32_at(fp + 0x34)),
                    float(f32_at(fp + 0x38)),
                    float(f32_at(fp + 0x3C)),
                )
                row[f"p{port}_ecb_lock"] = int(u32_at(fp + 0x88C))
                row[f"p{port}_cur_anim_frame"] = float(f32_at(fp + 0x894))
                row[f"p{port}_frame_speed_mul"] = float(f32_at(fp + 0x89C))
                row[f"p{port}_anim_blend_frames"] = float(f32_at(fp + 0x8A4))
                row[f"p{port}_anim_blend_unk"] = float(f32_at(fp + 0x8A8))
                row[f"p{port}_pos_x"] = float(f32_at(fp + FIGHTER_POS_X_OFF))
                row[f"p{port}_pos_y"] = float(f32_at(fp + FIGHTER_POS_Y_OFF))
                row[f"p{port}_pos_z"] = float(f32_at(fp + FIGHTER_POS_Z_OFF))
                row[f"p{port}_transn_pos"] = (
                    float(f32_at(fp + 0x68C)),
                    float(f32_at(fp + 0x690)),
                    float(f32_at(fp + 0x694)),
                )
                row[f"p{port}_transn_offset"] = (
                    float(f32_at(fp + 0x6A4)),
                    float(f32_at(fp + 0x6A8)),
                    float(f32_at(fp + 0x6AC)),
                )
                row[f"p{port}_self_vel_x"] = float(f32_at(fp + FIGHTER_SELF_VEL_X_OFF))
                row[f"p{port}_gr_vel"] = float(f32_at(fp + FIGHTER_GR_VEL_OFF))
                row[f"p{port}_pos_delta_x"] = float(f32_at(fp + FIGHTER_POS_DELTA_X_OFF))
                row[f"p{port}_nudge_x"] = float(f32_at(fp + FIGHTER_PLAYER_NUDGE_X_OFF))
                row[f"p{port}_nudge_z"] = float(f32_at(fp + FIGHTER_PLAYER_NUDGE_Z_OFF))
                row[f"p{port}_xA4_unk_vel_x"] = float(f32_at(fp + FIGHTER_XA4_UNK_VEL_X_OFF))
                row[f"p{port}_percent"] = float(f32_at(fp + FIGHTER_DMG_PERCENT_OFF))
                row[f"p{port}_dmg_x184c"] = int(u32_at(fp + FIGHTER_DMG_X184C_OFF))
                row[f"p{port}_dmg_kb_applied"] = float(f32_at(fp + FIGHTER_DMG_X1850_KB_APPLIED_OFF))
                row[f"p{port}_dmg_collpos"] = (
                    float(f32_at(fp + FIGHTER_DMG_X1854_COLLPOS_X_OFF)),
                    float(f32_at(fp + FIGHTER_DMG_X1854_COLLPOS_Y_OFF)),
                    float(f32_at(fp + FIGHTER_DMG_X1854_COLLPOS_Z_OFF)),
                )
                row[f"p{port}_dmg_x1948"] = int(u32_at(fp + FIGHTER_DMG_X1948_OFF))
                row[f"p{port}_dmg_x194c"] = int(u32_at(fp + FIGHTER_DMG_X194C_OFF))
                # Fighter hurt/Invincibility state gates (see `refs/melee/src/melee/ft/types.h` and `ft/ftcoll.c`):
                # - `fp+0x1988` (enum_t) and `fp+0x198C` (s32) are checked by collision for whether hits apply.
                row[f"p{port}_x1988"] = int(u32_at(fp + 0x1988))
                row[f"p{port}_x198c"] = int(u32_at(fp + 0x198C))

                # Hurt capsule snapshot (post-collision): global endpoints + height for debugging damage anim selection.
                hurt_len = int(u8_at(fp + FIGHTER_HURTCAPS_LEN_OFF))
                hurt_caps = []
                for hi in range(min(hurt_len, 15)):
                    hb = fp + FIGHTER_HURTCAPS_OFF + hi * HURTCAP_STRIDE
                    hurt_caps.append(
                        {
                            "i": int(hi),
                            "height": int(u32_at(hb + HURTCAP_HEIGHT_OFF)),
                            "scale": float(f32_at(hb + HURTCAP_SCALE_OFF)),
                            "a": (
                                float(f32_at(hb + HURTCAP_END1_X_OFF)),
                                float(f32_at(hb + HURTCAP_END1_Y_OFF)),
                                float(f32_at(hb + HURTCAP_END1_Z_OFF)),
                            ),
                            "b": (
                                float(f32_at(hb + HURTCAP_END2_X_OFF)),
                                float(f32_at(hb + HURTCAP_END2_Y_OFF)),
                                float(f32_at(hb + HURTCAP_END2_Z_OFF)),
                            ),
                        }
                    )
                row[f"p{port}_hurtcaps"] = hurt_caps

                # Fighter flags relevant to ftAnim_8006E054 special cases.
                x594_u32 = int(u32_at(fp + 0x594))
                row[f"p{port}_x594_u32"] = x594_u32
                row[f"p{port}_x594_u8"] = int(x594_u32 & 0xFF)
                # Decomp: union at fp+0x594 (see `refs/melee/src/melee/ft/types.h`).
                #
                # IMPORTANT: PowerPC bitfields are MSB-first within the u32. The decomp's layout:
                #   u32 x594_pad : 10;
                #   u32 x594_bits : 13;
                #   u32 x594_pad2 : 3;
                #   u32 x597_bits : 6;
                # corresponds to:
                #   - x597_bits in the low 6 bits
                #   - x594_bits in bits [21..9]
                #
                # `x594_bits` gates "inserted joints" (see `ftParts_8007506C`).
                row[f"p{port}_x594_bits"] = int((x594_u32 >> 9) & ((1 << 13) - 1))
                row[f"p{port}_x597_bits"] = int(x594_u32 & 0x3F)
                row[f"p{port}_x2221_u8"] = int(u8_at(fp + 0x2221))
                row[f"p{port}_x2226_u8"] = int(u8_at(fp + 0x2226))
                x2071_u8 = int(u8_at(fp + 0x2071))
                row[f"p{port}_x2071_u8"] = x2071_u8
                row[f"p{port}_x2071_b6"] = int((x2071_u8 >> 6) & 1)

                # ft_data->x8->x10 (a part index used by ftAnim_8006E054 when x2221_b2 is set).
                ft_data_ptr = int(u32_at(fp + 0x10C))
                row[f"p{port}_ft_data_ptr"] = ft_data_ptr
                x8_ptr = 0
                x10_part = None
                if ft_data_ptr != 0:
                    x8_ptr = int(u32_at(ft_data_ptr + 0x08))
                    if x8_ptr != 0:
                        x10_part = int(u8_at(x8_ptr + 0x10))
                row[f"p{port}_ftdata_x8_ptr"] = x8_ptr
                row[f"p{port}_ftdata_x8_x10_part"] = x10_part

                # Costume joint tree (fp+0x108): used to initialize the runtime skeleton.
                costume_joint = int(u32_at(fp + 0x108))
                row[f"p{port}_costume_joint_ptr"] = costume_joint
                # Probe a small subset of joint nodes by preorder index (which matches part indices when there are no inserted joints).
                probe_parts = {0, 1, 4, 7, 13, 25, 41, 55, 67}
                probe_max = max(probe_parts)
                probed: dict[int, dict] = {}
                if costume_joint != 0:
                    stack: list[tuple[int, int]] = [(costume_joint, -1)]
                    node_i = 0
                    while stack and node_i <= probe_max:
                        node_ptr, parent_idx = stack.pop()
                        # HSD_Joint layout (baselib/jobj.h):
                        # child @ +0x08, next @ +0x0C, flags @ +0x04, rot @ +0x14, scl @ +0x20, pos @ +0x2C.
                        child_ptr = int(u32_at(node_ptr + 0x08))
                        next_ptr = int(u32_at(node_ptr + 0x0C))
                        if node_i in probe_parts:
                            probed[node_i] = {
                                "ptr": node_ptr,
                                "parent": parent_idx,
                                "flags": int(u32_at(node_ptr + 0x04)),
                                "rot": (
                                    float(f32_at(node_ptr + 0x14)),
                                    float(f32_at(node_ptr + 0x18)),
                                    float(f32_at(node_ptr + 0x1C)),
                                ),
                                "scl": (
                                    float(f32_at(node_ptr + 0x20)),
                                    float(f32_at(node_ptr + 0x24)),
                                    float(f32_at(node_ptr + 0x28)),
                                ),
                                "pos": (
                                    float(f32_at(node_ptr + 0x2C)),
                                    float(f32_at(node_ptr + 0x30)),
                                    float(f32_at(node_ptr + 0x34)),
                                ),
                            }
                        if next_ptr != 0:
                            stack.append((next_ptr, parent_idx))
                        if child_ptr != 0:
                            stack.append((child_ptr, node_i))
                        node_i += 1
                row[f"p{port}_costume_joint_probe"] = probed

                # CollData (fp+0x6F0).
                coll = fp + 0x6F0
                row[f"p{port}_coll_pos_y"] = float(f32_at(coll + 0x04 + 0x04))
                row[f"p{port}_floor_normal_x"] = float(
                    f32_at(coll + COLL_FLOOR_SURFACE_OFF + SURFACE_NORMAL_X_OFF)
                )
                row[f"p{port}_floor_normal_y"] = float(
                    f32_at(coll + COLL_FLOOR_SURFACE_OFF + SURFACE_NORMAL_Y_OFF)
                )
                # ECBSource (fp+0x7F4).
                ecb_source = fp + 0x7F4
                row[f"p{port}_ecb_source_kind"] = int(u32_at(ecb_source + 0x00))
                joints: list[dict] = []
                parts_ptr = int(u32_at(fp + 0x5E8))
                for j in range(6):
                    jobj_ptr = int(u32_at(ecb_source + 0x08 + j * 4))
                    part_index = None
                    if jobj_ptr != 0 and parts_ptr != 0:
                        # Heuristic scan: find which FighterBone entry references this HSD_JObj*.
                        # FighterBone is 0x10 bytes, joint ptr at +0.
                        for pi in range(256):
                            if int(u32_at(parts_ptr + pi * 0x10)) == jobj_ptr:
                                part_index = pi
                                break
                    jid = int(u32_at(jobj_ptr + HSD_JOBJ_ID_OFF)) if jobj_ptr != 0 else -1
                    jflags = int(u32_at(jobj_ptr + HSD_JOBJ_FLAGS_OFF)) if jobj_ptr != 0 else 0
                    aobj_ptr = int(u32_at(jobj_ptr + HSD_JOBJ_AOBJ_OFF)) if jobj_ptr != 0 else 0
                    aobj_hsd_obj = int(u32_at(aobj_ptr + HSD_AOBJ_HSD_OBJ_OFF)) if aobj_ptr != 0 else 0
                    aobj_flags = int(u32_at(aobj_ptr + 0x00)) if aobj_ptr != 0 else 0
                    aobj_curr = float(f32_at(aobj_ptr + 0x04)) if aobj_ptr != 0 else 0.0
                    aobj_rate = float(f32_at(aobj_ptr + 0x10)) if aobj_ptr != 0 else 0.0
                    aobj_end = float(f32_at(aobj_ptr + 0x0C)) if aobj_ptr != 0 else 0.0
                    fobj_rotz = find_fobj_of_type(aobj_ptr, 3)  # HSD_A_J_ROTZ
                    local_rot = (
                        float(f32_at(jobj_ptr + 0x1C)),
                        float(f32_at(jobj_ptr + 0x20)),
                        float(f32_at(jobj_ptr + 0x24)),
                        float(f32_at(jobj_ptr + 0x28)),
                    ) if jobj_ptr != 0 else (0.0, 0.0, 0.0, 0.0)
                    local_scl = (
                        float(f32_at(jobj_ptr + 0x2C)),
                        float(f32_at(jobj_ptr + 0x30)),
                        float(f32_at(jobj_ptr + 0x34)),
                    ) if jobj_ptr != 0 else (1.0, 1.0, 1.0)
                    local_tr = (
                        float(f32_at(jobj_ptr + HSD_JOBJ_TRANSLATE_OFF + 0x00)),
                        float(f32_at(jobj_ptr + HSD_JOBJ_TRANSLATE_OFF + 0x04)),
                        float(f32_at(jobj_ptr + HSD_JOBJ_TRANSLATE_OFF + 0x08)),
                    ) if jobj_ptr != 0 else (0.0, 0.0, 0.0)
                    x, y, z = jobj_world_pos(jobj_ptr)
                    joints.append(
                        {
                            "j": j,
                            "ptr": jobj_ptr,
                            "id": jid,
                            "flags": jflags,
                            "aobj": aobj_ptr,
                            "aobj_hsd_obj": aobj_hsd_obj,
                            "aobj_flags": aobj_flags,
                            "aobj_curr_frame": aobj_curr,
                            "aobj_framerate": aobj_rate,
                            "aobj_end_frame": aobj_end,
                            "fobj_rotz": fobj_rotz,
                            "local_rot": local_rot,
                            "local_scl": local_scl,
                            "local_tr": local_tr,
                            "part_index": part_index,
                            "x": float(x),
                            "y": float(y),
                            "z": float(z),
                        }
                    )
                row[f"p{port}_ecb_source_joints"] = joints

                # Optional: scan a prefix of fp->parts[] to understand part-index mappings.
                # Useful when our extracted AnimDb matrices don't match live HSD_JObj transforms.
                scan: list[dict] = []
                if parts_ptr != 0:
                    coll_y = row[f"p{port}_coll_pos_y"]
                    for pi in range(80):
                        try:
                            jobj_ptr = int(u32_at(parts_ptr + pi * 0x10))
                            if jobj_ptr == 0:
                                continue
                            jflags = int(u32_at(jobj_ptr + HSD_JOBJ_FLAGS_OFF))
                            jobj_id = int(u32_at(jobj_ptr + HSD_JOBJ_ID_OFF))
                            parent_ptr = int(u32_at(jobj_ptr + HSD_JOBJ_PARENT_OFF))
                            mtx = [
                                float(f32_at(jobj_ptr + HSD_JOBJ_MTX_OFF + off))
                                for off in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C)
                            ]
                            flags8 = int(u16_at(parts_ptr + pi * 0x10 + 0x08))
                            flags0 = int(u8_at(parts_ptr + pi * 0x10 + 0x08))
                            # PowerPC bitfields in decomp comments are MSB-first within the byte.
                            b0 = bool(flags0 & 0x80)
                            b1 = bool(flags0 & 0x40)
                            b2 = bool(flags0 & 0x20)
                            local_tr = (
                                float(f32_at(jobj_ptr + HSD_JOBJ_TRANSLATE_OFF + 0x00)),
                                float(f32_at(jobj_ptr + HSD_JOBJ_TRANSLATE_OFF + 0x04)),
                                float(f32_at(jobj_ptr + HSD_JOBJ_TRANSLATE_OFF + 0x08)),
                            )
                            local_rot = (
                                float(f32_at(jobj_ptr + 0x1C)),
                                float(f32_at(jobj_ptr + 0x20)),
                                float(f32_at(jobj_ptr + 0x24)),
                                float(f32_at(jobj_ptr + 0x28)),
                            )
                            local_scl = (
                                float(f32_at(jobj_ptr + 0x2C)),
                                float(f32_at(jobj_ptr + 0x30)),
                                float(f32_at(jobj_ptr + 0x34)),
                            )
                            aobj_ptr = int(u32_at(jobj_ptr + HSD_JOBJ_AOBJ_OFF))
                            aobj_hsd_obj = int(u32_at(aobj_ptr + HSD_AOBJ_HSD_OBJ_OFF)) if aobj_ptr != 0 else 0
                            robj_ptr = int(u32_at(jobj_ptr + HSD_JOBJ_ROBJ_OFF))
                            scl_ptr = int(u32_at(jobj_ptr + 0x74))
                            parent_scl = None
                            if parent_ptr != 0:
                                parent_scl_ptr = int(u32_at(parent_ptr + 0x74))
                                if parent_scl_ptr != 0:
                                    parent_scl = (
                                        float(f32_at(parent_scl_ptr + 0x00)),
                                        float(f32_at(parent_scl_ptr + 0x04)),
                                        float(f32_at(parent_scl_ptr + 0x08)),
                                    )
                            jx, jy, jz = jobj_world_pos(jobj_ptr)
                            scan.append(
                                {
                                    "part": pi,
                                    "ptr": jobj_ptr,
                                    "jobj_id": jobj_id,
                                    "jflags": jflags,
                                    "parent": parent_ptr,
                                    "flags8": flags8,
                                    "flags0": flags0,
                                    "b0": b0,
                                    "b1": b1,
                                    "b2": b2,
                                    "local_tr": local_tr,
                                    "local_rot": local_rot,
                                    "local_scl": local_scl,
                                    "aobj_ptr": aobj_ptr,
                                    "aobj_hsd_obj": aobj_hsd_obj,
                                    "robj_ptr": robj_ptr,
                                    "scl_ptr": scl_ptr,
                                    "parent_scl": parent_scl,
                                    "mtx": mtx,
                                    "x": float(jx),
                                    "y": float(jy),
                                    "z": float(jz),
                                    "dy": float(jy - coll_y),
                                }
                            )
                        except Exception:
                            # `fp->parts` is length-limited; out-of-bounds reads can look pointer-like.
                            # Treat failures as "end of array" and stop scanning.
                            break
                row[f"p{port}_parts_scan"] = scan

                ecb_top_y = float(f32_at(fp + 0x794 + 0x04))
                ecb_bottom_y = float(f32_at(fp + 0x794 + 0x0C))
                row[f"p{port}_ecb_top_y"] = ecb_top_y
                row[f"p{port}_ecb_bottom_y"] = ecb_bottom_y
                row[f"p{port}_ecb_avg_y"] = 0.5 * (ecb_top_y + ecb_bottom_y)
                row[f"p{port}_desired_ecb_bottom_y"] = float(f32_at(fp + 0x774 + 0x0C))
                row[f"p{port}_ecb_bottom_abs_y"] = float(row[f"p{port}_pos_y"] + row[f"p{port}_ecb_bottom_y"])

                # ftPartsTable lookup for FtPart_RThumbNb (0x31 per ASM) -> part index.
                ft_parts_tbl = u32_at(FT_PARTS_TABLE_PTR)
                part_to_joint_val = None
                if ft_parts_tbl != 0:
                    fk = int(row[f"p{port}_kind"])
                    parts_tbl_ptr = u32_at(ft_parts_tbl + fk * 4)
                    if parts_tbl_ptr != 0:
                        part_to_joint_ptr = u32_at(parts_tbl_ptr + 0x04)
                        if part_to_joint_ptr != 0:
                            part_to_joint_val = int(u8_at(part_to_joint_ptr + 49))
                row[f"p{port}_ftpart_rthumbnb"] = part_to_joint_val

            # Read live item list (same order as slippi SendItemInfo).
            items = []
            item_mgr = u32_at(ITEM_MANAGER_PTR)
            if item_mgr != 0:
                item_gobj = u32_at(item_mgr + ITEM_MANAGER_FIRST_GOBJ_OFF)
                count = 0
                while item_gobj != 0 and count < MAX_ITEMS:
                    item_data = u32_at(item_gobj + GOBJ_USER_DATA_OFF)
                    if item_data != 0:
                        items.append(
                            {
                                "gobj": int(item_gobj),
                                "kind": int(u32_at(item_data + 0x10) & 0xFFFF),
                                "state": int(u32_at(item_data + 0x24) & 0xFF),
                                "dir": float(f32_at(item_data + 0x2C)),
                                "vel": (
                                    float(f32_at(item_data + 0x40)),
                                    float(f32_at(item_data + 0x44)),
                                ),
                                "pos": (
                                    float(f32_at(item_data + 0x4C)),
                                    float(f32_at(item_data + 0x50)),
                                ),
                                "spawn_id": int(u32_at(item_data + 0x1C)),
                            }
                        )
                    item_gobj = u32_at(item_gobj + GOBJ_NEXT_OFF)
                    count += 1
            row["items"] = items

            # Replay item snapshot for the same frame.
            replay_items = []
            for j in range(MAX_ITEMS):
                if f"item_{j}_exists" in replay_sample and int(replay_sample[f"item_{j}_exists"][idx]) != 0:
                    replay_items.append(
                        {
                            "slot": int(j),
                            "kind": int(replay_sample[f"item_{j}_type"][idx]),
                            "state": int(replay_sample[f"item_{j}_state"][idx]),
                            "pos": (
                                float(replay_sample[f"item_{j}_x"][idx]),
                                float(replay_sample[f"item_{j}_y"][idx]),
                            ),
                        }
                    )
            row["replay_items"] = replay_items

            # Replay post-frame snapshot at this same frame index (sanity check).
            row["replay_p1_y"] = float(r.post["p1_y"][idx])
            row["replay_p1_action"] = int(r.post["p1_action"][idx])
            row["replay_p1_og"] = bool(int(r.post["p1_og"][idx]) != 0)
            if "p1_percent" in r.post:
                row["replay_p1_percent"] = float(r.post["p1_percent"][idx])
            if "p1_anim" in r.post:
                row["replay_p1_anim"] = int(r.post["p1_anim"][idx])
            row["replay_p2_y"] = float(r.post["p2_y"][idx])
            row["replay_p2_action"] = int(r.post["p2_action"][idx])
            row["replay_p2_og"] = bool(int(r.post["p2_og"][idx]) != 0)
            if "p2_percent" in r.post:
                row["replay_p2_percent"] = float(r.post["p2_percent"][idx])
            if "p2_anim" in r.post:
                row["replay_p2_anim"] = int(r.post["p2_anim"][idx])

            if args.start_frame <= frame <= args.end_frame:
                out_rows.append(row)

            apply_inputs(1, idx)
            apply_inputs(2, idx)

            gamestate = console.step()
            if gamestate is None:
                continue
            if int(gamestate.frame) > args.end_frame:
                break

        Path(args.out).write_text(json.dumps({"replay": args.replay, "rows": out_rows}, indent=2))
        print(f"wrote {len(out_rows)} rows to {args.out}")
        return 0
    finally:
        try:
            dme.un_hook()
        except Exception:
            pass
        if controllers is not None:
            for c in controllers.values():
                try:
                    c.disconnect()
                except Exception:
                    pass
        if console is not None:
            try:
                console.stop()
            except Exception:
                pass


if __name__ == "__main__":
    raise SystemExit(main())
