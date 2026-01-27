from __future__ import annotations

import argparse
from pathlib import Path

from melee_sim.hsd_archive import HsdArchive, _u32_be, parse_hsd_archive


def _opcode(word: int) -> int:
    return (word >> 26) & 0x3F


def _cmd_len_words(op: int) -> int:
    # From `refs/melee/src/melee/ft/ftaction.c` `ftAction_803C0870`.
    # Index 0 corresponds to opcode 10.
    arr = [
        5,
        5,
        1,
        1,
        1,
        1,
        1,
        3,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        3,
        1,
        1,
        1,
        7,
        4,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        3,
        3,
        2,
        1,
        4,
    ]
    if op < 10:
        # Control commands: 0..9.
        if op in (5, 7):
            return 2
        return 1
    i = op - 10
    if 0 <= i < len(arr):
        return int(arr[i])
    return 1


def _read_s_temp4_subaction_ptr(archive: HsdArchive, s_temp4_list_abs: int, submotion_id: int) -> int | None:
    # struct S_TEMP4 is 0x18 bytes; ftSubactionList* at +0x0C.
    entry_abs = s_temp4_list_abs + submotion_id * 0x18
    if entry_abs < 0 or entry_abs + 0x0C + 4 > len(archive.buf):
        return None
    ptr = archive.ptr32(entry_abs + 0x0C)
    # NULL stored as 0 => ptr==data_base.
    if ptr == archive.data_base:
        return None
    return ptr


def _decode_spawn_hitbox_0(word0: int) -> dict[str, int]:
    # refs/melee/src/melee/lb/types.h::spawn_hitbox_0 (bitfield packing)
    return {
        "hitbox_id": (word0 >> 23) & 0x7,
        "hit_group": (word0 >> 20) & 0x7,
        "only_hit_grabbed": (word0 >> 19) & 0x1,
        "bone": (word0 >> 11) & 0xFF,
        "use_common_bone_ids": (word0 >> 10) & 0x1,
        "damage_u10": word0 & 0x3FF,
    }


def dump_spawn_hitbox0(*, archive: HsdArchive, subaction_abs_off: int, max_frames: int, max_steps_per_frame: int, limit: int) -> None:
    pc: int | None = subaction_abs_off
    timer: float = 0.0
    frame_count: float = 0.0
    call_stack: list[int] = []
    loop_stack: list[tuple[int, int]] = []  # (start_pc, remaining)

    printed = 0

    for frame in range(max_frames):
        if pc is None:
            break

        timer -= 1.0
        frame_count += 1.0

        for _step in range(max_steps_per_frame):
            if pc is None:
                break
            if timer > 0.0:
                break

            w0 = _u32_be(archive.buf, pc)
            op = _opcode(w0)

            # Control commands (0..9): mirror lbcommand.c / ftAction_80073240 control flow.
            if op == 0:
                return
            if op == 1:
                timer += float(w0 & ((1 << 26) - 1))
                pc += 4
                continue
            if op == 2:
                timer = float(w0 & ((1 << 26) - 1)) - frame_count
                pc += 4
                continue
            if op == 3:
                count = w0 & ((1 << 26) - 1)
                loop_stack.append((pc + 4, int(count)))
                pc += 4
                continue
            if op == 4:
                if not loop_stack:
                    pc += 4
                    continue
                start_pc, remaining = loop_stack[-1]
                remaining -= 1
                if remaining > 0:
                    loop_stack[-1] = (start_pc, remaining)
                    pc = start_pc
                else:
                    loop_stack.pop()
                    pc += 4
                continue
            if op == 5:
                # Subroutine: next word is pointer (data-section-relative).
                ptr_word = _u32_be(archive.buf, pc + 4)
                call_stack.append(pc + 8)
                pc = archive.data_base + ptr_word
                continue
            if op == 6:
                pc = call_stack.pop() if call_stack else None
                continue
            if op == 7:
                ptr_word = _u32_be(archive.buf, pc + 4)
                pc = archive.data_base + ptr_word
                continue
            if op == 8:
                # SetTimerAnimation: treat as halt for this dump.
                return
            if op == 9:
                pc += 4
                continue

            n_words = _cmd_len_words(op)
            if op == 11:
                fields = _decode_spawn_hitbox_0(w0)
                print(
                    f"frame={frame:3d} pc=0x{pc:08X} w0=0x{w0:08X} "
                    f"id={fields['hitbox_id']} group={fields['hit_group']} "
                    f"only_hit_grabbed={fields['only_hit_grabbed']} bone={fields['bone']} "
                    f"use_common={fields['use_common_bone_ids']} dmg_u10={fields['damage_u10']}"
                )
                printed += 1
                if printed >= limit:
                    return

            pc += 4 * n_words


def main() -> None:
    ap = argparse.ArgumentParser(description="Dump raw spawn_hitbox_0 fields from a fighter subaction script (debug/sanity).")
    ap.add_argument("--character", choices=["fox", "falco"], required=True)
    ap.add_argument(
        "--dat-dir",
        type=Path,
        default=Path("refs/melee-disc/files"),
        help="directory containing PlFx.dat / PlFc.dat (default: refs/melee-disc/files)",
    )
    ap.add_argument("--msid", type=int, default=46, help="submotion id to dump (default: 46 = jab 1)")
    ap.add_argument("--limit", type=int, default=16, help="max create-hitbox occurrences to print")
    ap.add_argument("--max-frames", type=int, default=240)
    ap.add_argument("--max-steps-per-frame", type=int, default=256)
    args = ap.parse_args()

    char_to_dat = {
        "fox": ("PlFx.dat", "ftDataFox"),
        "falco": ("PlFc.dat", "ftDataFalco"),
    }
    dat_name, sym = char_to_dat[args.character]
    buf = (args.dat_dir / dat_name).read_bytes()
    arc = parse_hsd_archive(buf)
    ft_off = arc.get_public_offset(sym)
    if ft_off is None:
        raise SystemExit(f"{dat_name}: missing public symbol {sym!r}")
    s_temp4_list = arc.ptr32(ft_off + 0x0C)
    sub_ptr = _read_s_temp4_subaction_ptr(arc, s_temp4_list, int(args.msid))
    if sub_ptr is None:
        raise SystemExit(f"{dat_name}: no subaction ptr for msid={args.msid}")
    dump_spawn_hitbox0(
        archive=arc,
        subaction_abs_off=sub_ptr,
        max_frames=int(args.max_frames),
        max_steps_per_frame=int(args.max_steps_per_frame),
        limit=int(args.limit),
    )


if __name__ == "__main__":
    main()

