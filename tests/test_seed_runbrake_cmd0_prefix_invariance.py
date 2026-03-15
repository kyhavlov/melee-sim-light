from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from tools.slippi.seed_history import derive_runbrake_cmd0


def _load_runbrake_window() -> tuple[dict[int, int], dict[int, int]]:
    cmd0_on_by_char: dict[int, int] = {}
    cmd0_off_by_char: dict[int, int] = {}
    for char_id, key in ((1, "fox"), (22, "falco")):
        moves = json.loads((Path("data/moves") / f"{key}.json").read_text())["moves"]
        events = moves["ftCo_SM_RunBrake"]["events"]
        cmd0_on_by_char[char_id] = min(
            int(ev["frame"])
            for ev in events
            if ev["kind"] == "set_cmd_var"
            and int(ev["data"]["idx"]) == 0
            and int(ev["data"]["value"]) != 0
        )
        cmd0_off_by_char[char_id] = min(
            int(ev["frame"])
            for ev in events
            if ev["kind"] == "set_cmd_var"
            and int(ev["data"]["idx"]) == 0
            and int(ev["data"]["value"]) == 0
        )
    return cmd0_on_by_char, cmd0_off_by_char


def test_runbrake_cmd0_derivation_is_prefix_invariant() -> None:
    # Strictly causal seed lane for RunBrake -> TurnRun gating.
    #
    # Decomp:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::{
    #     ftCo_RunBrake_Enter,ftCo_RunBrake_IASA}
    # - refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
    cmd0_on_by_char, cmd0_off_by_char = _load_runbrake_window()

    act_wait = 0x000E
    act_run_brake = 0x0017

    action_id = np.array(
        [
            act_wait,
            act_wait,
            act_run_brake,
            act_run_brake,
            act_run_brake,
            act_run_brake,
            act_wait,
            act_run_brake,
            act_run_brake,
            act_wait,
        ],
        dtype=np.uint16,
    )
    anim_frame = np.array([0.0, 1.0, 0.0, 8.0, 14.0, 15.0, 0.0, 3.0, 16.0, 0.0], dtype=np.float32)
    char_id = np.array([1, 1, 1, 1, 1, 1, 22, 22, 22, 22], dtype=np.uint8)

    full = derive_runbrake_cmd0(
        action_id_u16=action_id,
        anim_frame_f32=anim_frame,
        char_id_u8=char_id,
        cmd0_on_by_char=cmd0_on_by_char,
        cmd0_off_by_char=cmd0_off_by_char,
        act_run_brake=act_run_brake,
    )

    assert full.tolist() == [0, 0, 1, 1, 1, 0, 0, 1, 0, 0]

    for k in (1, 2, 3, 4, 6, 8, int(action_id.size)):
        got = derive_runbrake_cmd0(
            action_id_u16=action_id[:k],
            anim_frame_f32=anim_frame[:k],
            char_id_u8=char_id[:k],
            cmd0_on_by_char=cmd0_on_by_char,
            cmd0_off_by_char=cmd0_off_by_char,
            act_run_brake=act_run_brake,
        )
        assert np.array_equal(got, full[:k])
