"""Recover a missing UCF C-stick snap from an observed ASDI displacement."""

from dataclasses import dataclass
import re

import numpy as np
import pyarrow as pa


def _replace_fields(array, **fields):
    columns = {field.name: array.field(field.name) for field in array.type}
    columns.update(fields)
    return pa.StructArray.from_arrays(list(columns.values()), names=list(columns),
                                      mask=array.is_null())


@dataclass
class Stick:
    leader: pa.StructArray
    raw_x: np.ndarray
    raw_y: np.ndarray
    hitlag: np.ndarray


class CStickRecovery:
    def __init__(self, frames, ports):
        self.frames = frames
        self.ids = frames.field("id").to_numpy(zero_copy_only=False)
        self.ports = frames.field("ports")
        self.names = ports
        self.sticks = {}

    def _stick(self, port):
        if port not in self.sticks:
            leader = self.ports.field(port).field("leader")
            axes = leader.field("pre").field("cstick")
            raw_x, raw_y = (
                (np.clip(np.nan_to_num(axes.field(axis).to_numpy(zero_copy_only=False)), -1, 1)
                 * np.float32(80)).astype(np.int8) for axis in ("x", "y")
            )
            hitlag = leader.field("post").field("hitlag").to_numpy(zero_copy_only=False)
            self.sticks[port] = Stick(leader, raw_x, raw_y, hitlag)
        return self.sticks[port]

    def _recorded_cardinal_asdi(self, stick, frame, previous, axis, direction):
        following = np.flatnonzero(self.ids == frame)
        if not len(following):
            return False
        post = stick.leader.field("post").field("position")[previous].as_py()
        pre = stick.leader.field("pre").field("position")[following[-1]].as_py()
        other = "y" if axis == "x" else "x"
        # ftCo_Damage_OnExitHitlag uses the 3-unit common ASDI distance.
        return (pre is not None and post is not None and pre[other] == post[other] and
                pre[axis] == np.float32(post[axis] + direction * 3.0))

    def _recover(self, result):
        frame = result["first_mismatch_frame"]
        # ftCo_Damage_OnExitHitlag runs before the next input callback.
        # The snap must therefore belong to the previous frame's C-stick.
        rows = np.flatnonzero(self.ids == frame - 1)
        if not len(rows):
            return 0
        changed = 0
        for detail in result["details"]:
            if detail["frame"] != frame:
                break
            match = re.fullmatch(r"pos_[xy]\[(\d+)\]", detail["field"])
            if not match:
                continue
            stick = self._stick(self.names[int(match[1])])
            raw, other = stick.raw_x, stick.raw_y
            if abs(raw[rows[-1]]) < abs(other[rows[-1]]):
                raw, other = other, raw
            current = int(raw[rows[-1]])
            if abs(current) != 79 or abs(other[rows[-1]]) > 5:
                continue
            side = int(other[rows[-1]])
            direction = 1 if current > 0 else -1
            if stick.hitlag[rows[-1]] != 1 and (
                side or not self._recorded_cardinal_asdi(
                    stick, frame, rows[-1], "x" if raw is stick.raw_x else "y", direction
                )
            ):
                continue
            # A radial clamp can publish (79, side) from (80, side + 1).
            # ASDI can change the ensuing stage collision, and a new hit can
            # start another hitlag window on the same frame.
            raw[rows] = direction * 80
            other[rows] = side + (1 if side >= 0 else -1)
            changed += 1
        return changed

    def _frames(self):
        ports = {}
        for port in self.names:
            stick = self._stick(port)
            pre = _replace_fields(stick.leader.field("pre"),
                                  raw_analog_cstick_x=pa.array(stick.raw_x.copy()),
                                  raw_analog_cstick_y=pa.array(stick.raw_y.copy()))
            ports[port] = _replace_fields(self.ports.field(port),
                                          leader=_replace_fields(stick.leader, pre=pre))
        return _replace_fields(self.frames, ports=_replace_fields(self.ports, **ports))

    def validate(self, run):
        result = run(self.frames)
        changes = 0
        # Reject a candidate unless it resolves the first divergence.
        # A fresh full comparison still governs acceptance.
        while not result["pass"] and changes < 64:
            changed = self._recover(result)
            if not changed:
                break
            candidate = run(self._frames())
            if not candidate["pass"] and (
                candidate["first_mismatch_frame"] <= result["first_mismatch_frame"]
            ):
                break
            result = candidate
            changes += changed
        return result, changes
