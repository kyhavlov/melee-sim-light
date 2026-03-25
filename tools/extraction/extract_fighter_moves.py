from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path

from melee_sim.hsd_archive import HsdArchive, _u32_be, parse_hsd_archive


_U26_MASK = (1 << 26) - 1
_U23_MASK = (1 << 23) - 1


def _s16(v: int) -> int:
    v &= 0xFFFF
    return v - 0x1_0000 if (v & 0x8000) else v


def _s8(v: int) -> int:
    v &= 0xFF
    return v - 0x100 if (v & 0x80) else v


def _opcode(word: int) -> int:
    # `gmScriptEventDefault.opcode` is a 6-bit bitfield at the top of the u32.
    # (See `refs/melee/src/melee/ft/types.h` struct gmScriptEventDefault.)
    return (word >> 26) & 0x3F


def _u26(word: int) -> int:
    return word & _U26_MASK


@dataclass(frozen=True)
class Hitbox:
    hitbox_id: int
    hit_group: int
    rehit_frames: int
    only_hit_grabbed: bool
    bone: int
    use_common_bone_ids: bool
    damage: float
    size: float
    x_offset: float
    y_offset: float
    z_offset: float
    angle: int
    kbg: int
    wsk: int
    bkb: int
    element: int
    shield_damage: int
    sfx_severity: int
    sfx_kind: int
    hit_grounded: bool
    hit_aerial: bool
    item_hit_interaction: bool
    ignore_thrown_fighters: bool
    ignore_fighter_scale: bool
    clank: bool
    rebound: bool


@dataclass(frozen=True)
class Event:
    frame: int
    kind: str
    data: dict


def _decode_create_hitbox(words: list[int]) -> Hitbox:
    # Decomp shape: ftAction_8007121C reads spawn_hitbox_0..spawn_hitbox_5 while configuring fp->x914.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/lb/types.h::spawn_hitbox_{0..5}
    if len(words) not in (5, 6):
        raise ValueError(f"create_hitbox expects 5 or 6 words, got {len(words)}")
    w0, w1, w2, w3, w4 = words[:5]
    w5 = words[5] if len(words) == 6 else None

    # `spawn_hitbox_0`: opcode is top 6 bits; remaining fields are MSB→LSB packed.
    hitbox_id = (w0 >> 23) & 0x7
    hit_group = (w0 >> 20) & 0x7
    only_hit_grabbed = bool((w0 >> 19) & 0x1)
    bone = (w0 >> 11) & 0xFF
    use_common_bone_ids = bool((w0 >> 10) & 0x1)
    damage = float(w0 & 0x3FF)

    size = float((w1 >> 16) & 0xFFFF) * (1.0 / 256.0)
    z_offset = float(_s16(w1)) * (1.0 / 256.0)

    y_offset = float(_s16((w2 >> 16) & 0xFFFF)) * (1.0 / 256.0)
    x_offset = float(_s16(w2 & 0xFFFF)) * (1.0 / 256.0)

    angle = (w3 >> 23) & 0x1FF
    kbg = (w3 >> 14) & 0x1FF
    wsk = (w3 >> 5) & 0x1FF
    item_hit_interaction = bool((w3 >> 4) & 0x1)
    ignore_thrown_fighters = bool((w3 >> 3) & 0x1)
    ignore_fighter_scale = bool((w3 >> 2) & 0x1)
    clank = bool((w3 >> 1) & 0x1)
    rebound = bool(w3 & 0x1)

    bkb = (w4 >> 23) & 0x1FF
    element = (w4 >> 18) & 0x1F
    shield_damage = _s8((w4 >> 10) & 0xFF)
    sfx_severity = (w4 >> 7) & 0x7
    sfx_kind = (w4 >> 2) & 0x1F
    hit_grounded = bool((w4 >> 1) & 0x1)
    hit_aerial = bool(w4 & 0x1)

    # Rehit rate (frames) used by HitCapsule hitlists:
    # - lbColl_80008688 stores the per-victim timer from HitCapsule.x40_b4
    # - lbColl_80008A5C decrements it and clears the victim when it reaches 0
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 and ::lbColl_80008A5C
    #
    # Decomp/ASM note:
    # ftAction_8007121C (create hitbox) does NOT write the HitCapsule.x40_b4 bitfield (bits 4..11 of the
    # u16 at hitbox+0x40). GALE01 asm only sets the neighboring flag bits (x40_b0..b3 and x41_b4..b7).
    # refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_8007121C
    #
    # Until we find an asm-backed source for x40_b4 (if any), keep it at 0 ("indefinite until cleared").
    rehit_frames = 0

    return Hitbox(
        hitbox_id=hitbox_id,
        hit_group=hit_group,
        rehit_frames=rehit_frames,
        only_hit_grabbed=only_hit_grabbed,
        bone=bone,
        use_common_bone_ids=use_common_bone_ids,
        damage=damage,
        size=size,
        x_offset=x_offset,
        y_offset=y_offset,
        z_offset=z_offset,
        angle=angle,
        kbg=kbg,
        wsk=wsk,
        bkb=bkb,
        element=element,
        shield_damage=shield_damage,
        sfx_severity=sfx_severity,
        sfx_kind=sfx_kind,
        hit_grounded=hit_grounded,
        hit_aerial=hit_aerial,
        item_hit_interaction=item_hit_interaction,
        ignore_thrown_fighters=ignore_thrown_fighters,
        ignore_fighter_scale=ignore_fighter_scale,
        clank=clank,
        rebound=rebound,
    )


def _ftaction_skip_words_table() -> dict[int, int]:
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
    return {10 + i: n for i, n in enumerate(arr)}


_SKIP_WORDS_BY_OPCODE = _ftaction_skip_words_table()


def _cmd_len_words(op: int) -> int:
    # Control commands (0..9).
    if op == 5 or op == 7:
        return 2
    if op < 10:
        return 1
    # Fighter events.
    return _SKIP_WORDS_BY_OPCODE.get(op, 1)


def _read_words(archive: HsdArchive, abs_off: int, n_words: int) -> list[int]:
    out: list[int] = []
    for i in range(n_words):
        off = abs_off + i * 4
        out.append(_u32_be(archive.buf, off))
    return out


def _ptr_word_to_abs(archive: HsdArchive, word: int) -> int:
    # Most pointers inside these HSD archives are stored as offsets into the data section.
    return archive.data_base + word


def _parse_subaction_events(
    archive: HsdArchive,
    subaction_abs_off: int,
    *,
    max_frames: int,
    max_steps_per_frame: int,
) -> list[Event]:
    pc: int | None = subaction_abs_off
    timer: float = 0.0
    frame_count: float = 0.0
    call_stack: list[int] = []
    loop_stack: list[tuple[int, int]] = []  # (start_pc, remaining)

    out: list[Event] = []

    for frame in range(max_frames):
        frame_count = float(frame)
        if pc is None:
            break

        if timer != float("inf"):
            timer -= 1.0

        steps = 0
        while pc is not None and timer != float("inf") and timer <= 0.0:
            steps += 1
            if steps > max_steps_per_frame:
                raise RuntimeError("subaction interpreter exceeded step budget (likely infinite loop)")

            w0 = _u32_be(archive.buf, pc)
            op = _opcode(w0)

            if op < 10:
                if op == 0:
                    pc = None
                    break
                if op == 1:
                    # SynchronousTimer: add frames to the current timer.
                    timer += float(_u26(w0))
                    pc += 4
                    continue
                if op == 2:
                    # AsynchronousTimer: timer = value - frame_count.
                    timer = float(_u26(w0)) - frame_count
                    pc += 4
                    continue
                if op == 3:
                    # SetLoop: push (start, remaining) and continue.
                    remaining = int(_u26(w0))
                    loop_stack.append((pc + 4, remaining))
                    pc += 4
                    continue
                if op == 4:
                    # ExecuteLoop: decrement remaining; if still >0 jump back else pop.
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
                    # Subroutine: next word is the pointer target (data-section-relative).
                    ptr_word = _u32_be(archive.buf, pc + 4)
                    target = _ptr_word_to_abs(archive, ptr_word)
                    call_stack.append(pc + 8)
                    pc = target
                    continue
                if op == 6:
                    # Return.
                    pc = call_stack.pop() if call_stack else None
                    continue
                if op == 7:
                    # Goto: next word is the pointer target (data-section-relative).
                    ptr_word = _u32_be(archive.buf, pc + 4)
                    pc = _ptr_word_to_abs(archive, ptr_word)
                    continue
                if op == 8:
                    # SetTimerAnimation: we don't currently emulate the animation-timer mode;
                    # in practice this command appears late in many subactions. Treat it as a halt.
                    timer = float("inf")
                    pc += 4
                    continue
                if op == 9:
                    # BgFlash: ignore.
                    pc += 4
                    continue

                # Fallback for unknown control opcodes.
                pc += 4
                continue

            # Fighter events (>=10): parse subset we care about, otherwise skip.
            n_words = _cmd_len_words(op)
            if op == 11:
                words = _read_words(archive, pc, _cmd_len_words(op))
                hb = _decode_create_hitbox(words)
                out.append(
                    Event(
                        frame=frame,
                        kind="create_hitbox",
                        data={
                            "hitbox": hb.__dict__,
                        },
                    )
                )
            elif op == 12:
                # Adjust Hitbox Damage: idx=3, value=23.
                idx = (w0 >> 23) & 0x7
                value = w0 & _U23_MASK
                out.append(Event(frame=frame, kind="set_hitbox_damage", data={"idx": idx, "damage": float(value)}))
            elif op == 13:
                # Adjust Hitbox Size: idx=3, value=23 (fixed 1/256).
                idx = (w0 >> 23) & 0x7
                value = w0 & _U23_MASK
                out.append(
                    Event(
                        frame=frame,
                        kind="set_hitbox_size",
                        data={"idx": idx, "size": float(value) * (1.0 / 256.0)},
                    )
                )
            elif op == 14:
                payload = _u26(w0)
                idx = (payload >> 2) & 0xFF_FFFF
                typ = (payload >> 1) & 0x1
                value = payload & 0x1
                out.append(
                    Event(
                        frame=frame,
                        kind="set_hitbox_interaction",
                        data={"idx": idx, "type": int(typ), "value": int(value)},
                    )
                )
            elif op == 15:
                out.append(Event(frame=frame, kind="remove_hitbox", data={"idx": int(_u26(w0))}))
            elif op == 16:
                out.append(Event(frame=frame, kind="clear_hitboxes", data={}))
            elif op == 19:
                # Set cmd var: idx=2, value=24 (ftAction_80071820).
                idx = (w0 >> 24) & 0x3
                value = w0 & 0xFF_FFFF
                out.append(Event(frame=frame, kind="set_cmd_var", data={"idx": int(idx), "value": int(value)}))
            elif op == 20:
                # Set throw flags (ftAction_800718A4): used by ftCo_Throw.c to drive throw facing flip/release.
                # Payload is `hit_idx` in practice (0 -> throw_flags_b3 (release), 1 -> throw_flags_b4 (flip facing)).
                out.append(Event(frame=frame, kind="set_throw_flags", data={"hit_idx": int(_u26(w0))}))
            elif op == 23:
                # Allow interrupt: sets `fp->allow_interrupt = true` (ftAction_80071950).
                out.append(Event(frame=frame, kind="allow_interrupt", data={}))
            elif op == 24:
                # Throw script projectile pulse (ftAction_80071974): sets fp->throw_flags_b0.
                #
                # Decomp:
                # - refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
                # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                #   (consumes throw_flags_b0 to spawn blaster shots in Throw{B,Hi,Lw} flows)
                out.append(Event(frame=frame, kind="set_throw_spawn_projectile", data={}))
            elif op == 25:
                # Set airborne/grounded state (ftAction_80071998 -> ftCommon_8007D7FC/8007D5D4/8007D60C).
                # Payload is `state` in [0,2].
                out.append(Event(frame=frame, kind="set_airborne_state", data={"state": int(_u26(w0))}))
            elif op == 26:
                # Hit status (ftAction_80071A14 -> ftColl_8007B62C): used for various invincibility/intangibility states.
                # Payload is an enum (0..2).
                out.append(Event(frame=frame, kind="set_hit_status", data={"state": int(_u26(w0))}))
            elif op == 27:
                # Set all hurt capsule states (HurtCapsuleState).
                out.append(Event(frame=frame, kind="set_all_hurt_state", data={"state": int(_u26(w0))}))
            elif op == 28:
                # Set hurt capsule state for a bone id: bone_idx=8, state=18.
                bone_idx = (w0 >> 18) & 0xFF
                state = w0 & ((1 << 18) - 1)
                out.append(
                    Event(
                        frame=frame,
                        kind="set_hurt_state",
                        data={"bone_idx": int(bone_idx), "state": int(state)},
                    )
                )
            elif op == 29:
                # Set jab combo gate (ftAction_80071AE8 -> fp->x2218_b1).
                #
                # Decomp:
                # refs/melee/src/melee/ft/ftaction.c::ftAction_80071AE8
                # refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_80071AE8
                #
                # Payload semantics:
                # - low 26-bit payload is `set_jab_combo.disabled`.
                # - command sets x2218_b1 when !disabled (or when fp->x197C != NULL).
                #   Fox/Falco lane uses !disabled path.
                out.append(
                    Event(
                        frame=frame,
                        kind="set_jab_combo",
                        data={"disabled": int(_u26(w0) != 0)},
                    )
                )
            elif op == 30:
                # Set jab rapid flag (ftAction_80071B28 -> fp->x2218_b2).
                #
                # Decomp:
                # refs/melee/src/melee/ft/ftaction.c::ftAction_80071B28
                # refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_80071B28
                out.append(
                    Event(
                        frame=frame,
                        kind="set_jab_rapid",
                        data={"state": int(_u26(w0) != 0)},
                    )
                )
            elif op == 52:
                # Set fp->x221C_u16_y flags (ftAction_80072C6C -> ft_8008A1B8).
                # refs/melee/src/melee/ft/ftaction.c::ftAction_80072C6C
                # refs/melee/src/melee/ft/ft_0892.c::ft_8008A1B8
                out.append(
                    Event(
                        frame=frame,
                        kind="set_state_flags_221c_u16_y",
                        data={"flags": int(_u26(w0))},
                    )
                )
            elif op == 38:
                # Pseudo-random SFX command (ftAction_80071FC8):
                # - consumes one HSD_Randi(random_range) per command execution.
                # - then selects one of six SFX ids and dispatches via behavior.
                # refs/melee/src/melee/ft/ftaction.c::ftAction_80071FC8
                # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
                #
                # Word0 bit layout (MSB->LSB), matching lb/types.h::pseudo_random_sfx_0:
                # - opcode       : bits 26..31 (already decoded as `op`)
                # - volume       : bits 18..25
                # - panning      : bits 10..17
                # - behavior     : bits  6.. 9
                # - random_range : bits  0.. 5
                out.append(
                    Event(
                        frame=frame,
                        kind="pseudo_random_sfx",
                        data={
                            "volume": int((w0 >> 18) & 0xFF),
                            "panning": int((w0 >> 10) & 0xFF),
                            "behavior": int((w0 >> 6) & 0xF),
                            "random_range": int(w0 & 0x3F),
                        },
                    )
                )
            elif op == 34:
                # Set throw hitbox (ftAction_80071E04): configures fp->xDF4[2] hitboxes used during throws.
                # Word0: opcode, idx (3), damage (23)
                # Word1: kb_angle (9), kbg (9), wsk (9)
                # Word2: bkb (9), element (4), sfx_severity (3), sfx_kind (4)
                if n_words < 3:
                    pc += 4 * n_words
                    continue
                w1 = _u32_be(archive.buf, pc + 4)
                w2 = _u32_be(archive.buf, pc + 8)

                idx = (w0 >> 23) & 0x7
                damage = w0 & _U23_MASK

                # Decomp shape: refs/melee/src/melee/lb/types.h::set_throw_hitbox_1
                # Bit layout matches spawn_hitbox_3:
                # - kb_angle: bits 23..31 (9)
                # - kbg:      bits 14..22 (9)
                # - wsk:      bits  5..13 (9)
                kb_angle = (w1 >> 23) & 0x1FF
                kbg = (w1 >> 14) & 0x1FF
                wsk = (w1 >> 5) & 0x1FF

                bkb = (w2 >> 23) & 0x1FF
                element = (w2 >> 19) & 0xF
                sfx_severity = (w2 >> 16) & 0x7
                sfx_kind = (w2 >> 12) & 0xF

                out.append(
                    Event(
                        frame=frame,
                        kind="set_throw_hitbox",
                        data={
                            "idx": int(idx),
                            "damage": float(damage),
                            "angle": int(kb_angle),
                            "kbg": int(kbg),
                            "wsk": int(wsk),
                            "bkb": int(bkb),
                            "element": int(element),
                            "sfx_severity": int(sfx_severity),
                            "sfx_kind": int(sfx_kind),
                        },
                    )
                )

            pc += 4 * n_words

    return out


def _parse_ftco_submotion_enum(melee_decomp_root: Path) -> dict[str, int]:
    header = melee_decomp_root / "src" / "melee" / "ft" / "chara" / "ftCommon" / "forward.h"
    txt = header.read_text(encoding="utf-8", errors="replace").splitlines()

    in_enum = False
    value = None
    out: dict[str, int] = {}
    for line in txt:
        if "typedef enum ftCo_Submotion" in line:
            in_enum = True
            continue
        if not in_enum:
            continue
        if "} ftCo_Submotion;" in line:
            break

        # Strip comments and whitespace.
        s = line.split("//", 1)[0].strip()
        if not s or not s.startswith("ftCo_SM_"):
            continue
        s = s.rstrip(",")

        if "=" in s:
            name, rhs = [x.strip() for x in s.split("=", 1)]
            try:
                value = int(rhs, 0)
            except ValueError:
                continue
            out[name] = value
        else:
            if value is None:
                value = 0
            else:
                value += 1
            out[s] = value

    if not out:
        raise RuntimeError(f"failed to parse ftCo_Submotion enum from {header}")
    return out


def _load_fighter_dat(iso_dir: Path, dat_name: str) -> HsdArchive:
    buf = (iso_dir / dat_name).read_bytes()
    return parse_hsd_archive(buf)


def _read_s_temp4_subaction_ptr(archive: HsdArchive, s_temp4_list_abs: int, submotion_id: int) -> int | None:
    # struct S_TEMP4 is 0x18 bytes; ftSubactionList* at +0x0C.
    entry_abs = s_temp4_list_abs + submotion_id * 0x18
    # Defensive: some decomp enum entries may not exist in a given DAT's table.
    # Avoid out-of-bounds reads when iterating msid domains beyond extracted coverage.
    if entry_abs < 0 or entry_abs + 0x0C + 4 > len(archive.buf):
        return None
    ptr = archive.ptr32(entry_abs + 0x0C)
    # Many entries are NULL (stored as 0) => ptr==data_base; treat as absent.
    if ptr == archive.data_base:
        return None
    return ptr


def _iter_msids_from_special_msids_json(v: object) -> list[int]:
    out: list[int] = []
    if isinstance(v, dict):
        for k, vv in v.items():
            if k in ("default", "left", "right") and isinstance(vv, int):
                out.append(int(vv))
            else:
                out.extend(_iter_msids_from_special_msids_json(vv))
    elif isinstance(v, list):
        for vv in v:
            out.extend(_iter_msids_from_special_msids_json(vv))
    return out


def _load_special_msids(special_msids_dir: Path, character: str) -> list[int]:
    path = special_msids_dir / f"{character}.json"
    if not path.exists():
        return []
    raw = json.loads(path.read_text(encoding="utf-8"))
    msids = _iter_msids_from_special_msids_json(raw)
    # Defensive: only allow plausible u16 ids.
    msids = [m for m in msids if 0 <= m <= 0xFFFF]
    return sorted(set(msids))


def main() -> None:
    ap = argparse.ArgumentParser(description="Extract fighter hitbox command timelines from Pl*.dat")
    ap.add_argument("--iso_dir", type=Path, default=Path("_iso"))
    ap.add_argument("--melee_decomp", type=Path, default=Path("refs/melee"))
    ap.add_argument("--out_dir", type=Path, default=Path("data/moves"))
    ap.add_argument(
        "--special_msids_dir",
        type=Path,
        default=Path("data/special_msids"),
        help="directory containing phase-aware special msid JSON files",
    )
    ap.add_argument(
        "--chars",
        type=str,
        default="fox,falco,sheik,peach,marth,puff,falcon",
        help="comma-separated character set to extract",
    )
    ap.add_argument("--max_frames", type=int, default=240)
    ap.add_argument("--max_steps_per_frame", type=int, default=10000)
    args = ap.parse_args()

    char_to_dat = {
        "fox": ("PlFx.dat", "ftDataFox"),
        "falco": ("PlFc.dat", "ftDataFalco"),
        "sheik": ("PlSk.dat", "ftDataSeak"),
        "peach": ("PlPe.dat", "ftDataPeach"),
        "marth": ("PlMs.dat", "ftDataMars"),
        "puff": ("PlPr.dat", "ftDataPurin"),
        "falcon": ("PlCa.dat", "ftDataCaptain"),
    }

    enum_map = _parse_ftco_submotion_enum(args.melee_decomp)
    want = [
        # Grounded locomotion: needed for Dash IASA (cmd_var[0] gating) parity.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
        "ftCo_SM_Dash",
        # RunBrake cmd_var[0] gates the TurnRun branch in RunBrake IASA.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
        # refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
        "ftCo_SM_RunBrake",
        # Grounded->airborne jump transitions:
        # - ftCo_Jump* scripts own ftcmd var / allow_interrupt timing for jump followups.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c
        "ftCo_SM_JumpF",
        "ftCo_SM_JumpB",
        # Spotdodge (EscapeN) `allow_interrupt` is a command-script lane used by fp->allow_interrupt.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_Anim
        # refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
        "ftCo_SM_EscapeN",
        # Air dodge script timing (`allow_interrupt`) for common EscapeAir.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Anim
        # refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
        "ftCo_SM_EscapeAir",
        # Guard hold script timing for fp->allow_interrupt ownership.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c
        "ftCo_SM_Guard",
        "ftCo_SM_EscapeF",
        "ftCo_SM_EscapeB",
        "ftCo_SM_Attack11",
        "ftCo_SM_Attack12",
        "ftCo_SM_Attack13",
        "ftCo_SM_AttackDash",
        "ftCo_SM_AttackS3",
        "ftCo_SM_AttackHi3",
        "ftCo_SM_AttackLw3",
        "ftCo_SM_AttackS4",
        "ftCo_SM_AttackHi4",
        "ftCo_SM_AttackLw4",
        "ftCo_SM_AttackAirN",
        "ftCo_SM_AttackAirF",
        "ftCo_SM_AttackAirB",
        "ftCo_SM_AttackAirHi",
        "ftCo_SM_AttackAirLw",
        # Knockdown getup attacks (common): needed for replay parity (DownAttackU/D hitboxes).
        "ftCo_SM_DownAttackU",
        "ftCo_SM_DownAttackD",
        # Grabs / throws (common): needed for replay parity (ftCo_MS_Thrown*, etc.).
        "ftCo_SM_Catch",
        "ftCo_SM_CatchDash",
        "ftCo_SM_CatchWait",
        "ftCo_SM_CatchAttack",
        "ftCo_SM_ThrowF",
        "ftCo_SM_ThrowB",
        "ftCo_SM_ThrowHi",
        "ftCo_SM_ThrowLw",
        "ftCo_SM_ThrownF",
        "ftCo_SM_ThrownB",
        "ftCo_SM_ThrownHi",
        "ftCo_SM_ThrownLw",
    ]
    want_ids = {name: enum_map[name] for name in want if name in enum_map}
    if len(want_ids) != len(want):
        missing = sorted(set(want) - set(want_ids))
        raise RuntimeError(f"missing expected ftCo_SM_* entries: {missing}")

    out_dir: Path = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    for ch in [c.strip() for c in args.chars.split(",") if c.strip()]:
        if ch not in char_to_dat:
            raise RuntimeError(f"unknown character {ch!r}")
        dat_name, sym = char_to_dat[ch]
        arc = _load_fighter_dat(args.iso_dir, dat_name)
        ft_off = arc.get_public_offset(sym)
        if ft_off is None:
            raise RuntimeError(f"{dat_name}: missing public symbol {sym!r}")
        s_temp4_list = arc.ptr32(ft_off + 0x0C)

        moves: dict[str, dict] = {}
        for name, sm_id in want_ids.items():
            sub_ptr = _read_s_temp4_subaction_ptr(arc, s_temp4_list, sm_id)
            if sub_ptr is None:
                continue
            events = _parse_subaction_events(
                arc,
                sub_ptr,
                max_frames=int(args.max_frames),
                max_steps_per_frame=int(args.max_steps_per_frame),
            )
            moves[name] = {
                "submotion_id": sm_id,
                "subaction_abs_off": sub_ptr,
                "events": [e.__dict__ for e in events],
            }

        specials_by_msid: dict[str, dict] = {}
        for msid in _load_special_msids(args.special_msids_dir, ch):
            sub_ptr = _read_s_temp4_subaction_ptr(arc, s_temp4_list, int(msid))
            if sub_ptr is None:
                continue
            events = _parse_subaction_events(
                arc,
                sub_ptr,
                max_frames=int(args.max_frames),
                max_steps_per_frame=int(args.max_steps_per_frame),
            )
            specials_by_msid[str(int(msid))] = {
                "submotion_id": int(msid),
                "subaction_abs_off": sub_ptr,
                "events": [e.__dict__ for e in events],
            }

        out = {
            "schema_version": 1,
            "character": ch,
            "moves": moves,
            "specials_by_msid": specials_by_msid,
        }
        (out_dir / f"{ch}.json").write_text(json.dumps(out, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
