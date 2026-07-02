from __future__ import annotations

import numpy as np
import pytest

from tools.eval.validation_dtypes import SEED_DTYPE
from tools.modelplay import state_adapter
from tools.modelplay.state_adapter import frame_state_from_seed, viewer_frame_from_state


class _Buttons:
    A = False
    B = False
    X = False
    Y = False
    Z = False
    L = False
    R = False
    D_UP = False


class _Stick:
    x = np.float32(0.5)
    y = np.float32(0.5)


class _Controller:
    buttons = _Buttons()
    main_stick = _Stick()
    c_stick = _Stick()
    shoulder = np.float32(0.0)


def test_viewer_shield_and_flag_booleans_follow_state_flags_not_shield_hp(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(state_adapter, "empty_controller", lambda: _Controller())
    seed = np.zeros((), dtype=SEED_DTYPE)
    seed["num_players"] = 2
    seed["stage_id"] = 32
    seed["stocks"][:2] = 4
    seed["char_id"][:2] = [1, 22]
    seed["shield_hp"][:2] = [60.0, 55.0]
    # Slippi state_flags bytes: (0x2218, 0x221A, 0x221B, 0x221C, 0x221F).
    seed["state_flags"][0] = [0x10, 0x08, 0x00, 0x20, 0x00]
    seed["state_flags"][1] = [0x00, 0x00, 0x80, 0x02, 0x00]

    state = frame_state_from_seed(seed)
    frame = viewer_frame_from_state(state, controllers={1: _Controller(), 2: _Controller()})

    p0 = frame["players"][0]["state"]
    p1 = frame["players"][1]["state"]

    assert p0["shieldSize"] == 60.0
    assert p1["shieldSize"] == 55.0

    assert p0["isReflectActive"] is True
    assert p0["isFastfalling"] is True
    assert p0["isShieldActive"] is False
    assert p0["isPowershieldActive"] is True

    assert p1["isReflectActive"] is False
    assert p1["isFastfalling"] is False
    assert p1["isShieldActive"] is True
    assert p1["isInHitstun"] is True
