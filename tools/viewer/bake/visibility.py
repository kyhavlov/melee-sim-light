"""Fighter model-part visibility (ftparts.c) and subaction script timelines.

DObj visibility for the normal draw path (ftdrawcommon.c) is: every DObj
listed in any lookup group is hidden, except the DObjs of the selected state
of each model in group 0, where the selection is fp->x5F4_arr[i].idx.
ftParts_800749CC seeds the selection from the character Init (prev values),
and each motion state change restores those defaults before the subaction
script runs its set_dobj_flags events (ftaction.c ftAction_80071D40, opcode
31), commit-prev (32) or reset-all (33).
"""
from __future__ import annotations

from dataclasses import dataclass

from .dat import Dat

OP_END, OP_SYNC, OP_ASYNC, OP_SET_LOOP, OP_EXEC_LOOP, OP_SUBROUTINE, OP_RETURN, OP_GOTO, OP_TIMER_ANIM, OP_BGFLASH = range(10)
OP_SET_DOBJ = 31
OP_COMMIT_DOBJ = 32
OP_RESET_DOBJ = 33

# Word counts of fighter events 10.. (tools/build/derive_gameplay_parts_mask.py)
FT_EVENT_WORDS = [
    5, 5, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1, 7, 4, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 2, 1, 4,
]


@dataclass
class VisTables:
    model_num: int
    # groups[g][model] = list of states, each a list of dobj indices
    groups: list[list[list[list[int]]]]

    def listed(self) -> set[int]:
        out: set[int] = set()
        for group in self.groups:
            for model in group:
                for state in model:
                    out.update(state)
        return out

    def visible(self, all_dobjs: int, selection: list[int]) -> set[int]:
        hidden = self.listed()
        shown: set[int] = set()
        group0 = self.groups[0]
        for i, model in enumerate(group0):
            idx = selection[i] if i < len(selection) else -1
            if 0 <= idx < len(model):
                shown.update(model[idx])
        return (set(range(all_dobjs)) - hidden) | shown


def _read_lookup(ftdata: Dat, lk: int, model_num: int) -> list[list[list[int]]]:
    models = []
    if lk:
        for i in range(model_num):
            cnt = ftdata.u32(lk + i * 8)
            arr = ftdata.ptr(lk + i * 8 + 4)
            states = []
            for j in range(cnt):
                n = ftdata.u32(arr + j * 8)
                p = ftdata.ptr(arr + j * 8 + 4)
                states.append(list(ftdata.data[p:p + n]))
            models.append(states)
    return models


def read_vis_tables(ftdata: Dat, root_name: str, costume: int = 0,
                    extra_item_slot: int | None = None) -> VisTables:
    """Groups 0..3 from ftData x8 (costume row, falling back to row 0).
    Group 2 indexes the metal model's separate DObj list and is ignored for
    the main model. `extra_item_slot` names an x48_items entry holding a
    fifth lookup (Game & Watch's outline copies, fp->x5AC.xC[4]); the draw
    path hides every DObj it lists (ftdrawcommon.c disables group 4)."""
    ft = ftdata.roots[root_name]
    x8 = ftdata.ptr(ft + 8)
    model_num = ftdata.u32(x8)
    vis_table = ftdata.ptr(x8 + 4)
    groups = []
    for g in range(4):
        lk = ftdata.ptr(vis_table + costume * 16 + g * 4) or ftdata.ptr(vis_table + g * 4)
        groups.append(_read_lookup(ftdata, lk, model_num) if g != 2 else [])
    if extra_item_slot is not None:
        items = ftdata.ptr(ft + 0x48)
        groups.append(_read_lookup(ftdata, ftdata.ptr(items + extra_item_slot * 4), model_num))
    return VisTables(model_num, groups)


@dataclass
class ScriptTimeline:
    """Per-frame model selections produced by a subaction script."""
    selections: list[list[int]]  # index = frame


def simulate_script(ftdata: Dat, script: int, defaults: list[int], frames: int,
                    code_overrides=None) -> ScriptTimeline:
    """Run the subaction command stream frame by frame (lbcommand.c
    Command_00..09 plus the three model-visibility fighter events) and
    record the selection in effect on each frame.

    Timer semantics: `timer` counts frames until the next event; events run
    while timer <= 0 (ftAction: `while (timer <= 0) execute`), then the
    frame advances by 1. Loops, subroutines and gotos follow lbcommand.c.
    """
    data = ftdata.data
    sel = list(defaults)
    prev = list(defaults)
    committed = False
    timeline = []
    u = script
    timer = 0.0
    frame_count = 0.0
    stack: list = []
    done = u == 0
    guard = 0
    for frame in range(frames):
        # ftAction_80073240: timer -= frame_speed_mul, then run events while
        # timer <= 0, all before this frame is displayed.
        frame_count = float(frame)
        if timer != float('inf'):
            timer -= 1.0
        while not done and timer <= 0.0 and guard < 100000:
            guard += 1
            word = int.from_bytes(data[u:u + 4], 'big')
            op = word >> 26
            if op == OP_END:
                done = True
                break
            if op == OP_SYNC:
                timer += float(word & 0x3FFFFFF)
                u += 4
            elif op == OP_ASYNC:
                timer = float(word & 0x3FFFFFF) - frame_count
                u += 4
            elif op == OP_SET_LOOP:
                stack.append(u + 4)
                stack.append(word & 0x3FFFFFF)
                u += 4
            elif op == OP_EXEC_LOOP:
                remaining = stack[-1] - 1
                stack[-1] = remaining
                if remaining != 0:
                    u = stack[-2]
                else:
                    u += 4
                    stack.pop()
                    stack.pop()
            elif op == OP_SUBROUTINE:
                target = ftdata.ptr(u + 4)
                stack.append(u + 8)
                u = target
            elif op == OP_RETURN:
                u = stack.pop()
            elif op == OP_GOTO:
                u = ftdata.ptr(u + 4)
            elif op == OP_TIMER_ANIM:
                u += 4
                timer = float('inf')
            elif op == OP_BGFLASH:
                u += 4
            else:
                if op == OP_SET_DOBJ:
                    idx = (word >> 19) & 0x7F
                    if idx & 0x40:
                        idx -= 0x80
                    value = word & 0x7FFFF
                    if value & 0x40000:
                        value -= 0x80000
                    if 0 <= idx < len(sel):
                        sel[idx] = value
                elif op == OP_COMMIT_DOBJ:
                    sel = list(prev)
                elif op == OP_RESET_DOBJ:
                    sel = [-1] * len(sel)
                words = FT_EVENT_WORDS[op - 10] if op - 10 < len(FT_EVENT_WORDS) else 1
                u += 4 * words
        current = list(sel)
        if code_overrides:
            current = code_overrides(frame, current)
        timeline.append(current)
    return ScriptTimeline(timeline)
