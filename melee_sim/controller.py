from __future__ import annotations

from typing import NamedTuple

import numpy as np

BUTTON_A = 0x0100
BUTTON_B = 0x0200
BUTTON_X = 0x0400
BUTTON_Y = 0x0800
BUTTON_Z = 0x0010
BUTTON_L = 0x0040
BUTTON_R = 0x0020
BUTTON_D_UP = 0x0008

BUTTON_NAMES = ("A", "B", "X", "Y", "Z", "L", "R", "D_UP")

NATIVE_AXIS_SPACING = 160
NATIVE_SHOULDER_SPACING = 140


class Buttons(NamedTuple):
    A: np.bool_
    B: np.bool_
    X: np.bool_
    Y: np.bool_
    Z: np.bool_
    L: np.bool_
    R: np.bool_
    D_UP: np.bool_


class Stick(NamedTuple):
    x: np.float32
    y: np.float32


class Controller(NamedTuple):
    main_stick: Stick
    c_stick: Stick
    shoulder: np.float32
    buttons: Buttons


def neutral_controller(shape=()) -> Controller:
    return Controller(
        main_stick=Stick(
            np.full(shape, 0.5, dtype=np.float32),
            np.full(shape, 0.5, dtype=np.float32),
        ),
        c_stick=Stick(
            np.full(shape, 0.5, dtype=np.float32),
            np.full(shape, 0.5, dtype=np.float32),
        ),
        shoulder=np.zeros(shape, dtype=np.float32),
        buttons=Buttons(**{name: np.zeros(shape, dtype=np.bool_) for name in BUTTON_NAMES}),
    )


def from_raw_axis(x):
    return (np.asarray(x, dtype=np.float32) + 80.0) / 160.0


def to_raw_axis(x):
    return np.rint(np.asarray(x, dtype=np.float32) * 160.0 - 80.0).astype(np.int32)


def from_raw_trigger(x):
    return np.asarray(x, dtype=np.float32) / 140.0


def to_raw_trigger(x):
    return np.rint(np.asarray(x, dtype=np.float32) * 140.0).astype(np.int32)


def write_controller(action_view: np.ndarray, controller: Controller, player: int = 0) -> None:
    p = action_view["players"][..., int(player)]
    p["main_stick_x"][...] = controller.main_stick.x
    p["main_stick_y"][...] = controller.main_stick.y
    p["c_stick_x"][...] = controller.c_stick.x
    p["c_stick_y"][...] = controller.c_stick.y
    p["shoulder"][...] = controller.shoulder
    for name in BUTTON_NAMES:
        p["buttons"][name][...] = getattr(controller.buttons, name)
