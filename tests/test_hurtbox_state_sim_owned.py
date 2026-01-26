from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE


# Character ids (GALE01): Slippi post-frame `character`.
CHAR_FOX = 1
CHAR_FALCO = 22

# Stage ids (GALE01): Final Destination.
STAGE_FD = 32


def _find_nonzero_hit_status_entry(path: Path) -> tuple[int, int, int] | None:
    with path.open("rb") as f:
        magic = f.read(8)
        if magic != b"MSLHSTA1":
            return None
        (ver, frame_count, reserved, entry_count) = struct.unpack("<IHHI", f.read(12))
        if int(ver) != 1 or int(reserved) != 0:
            return None
        if int(frame_count) <= 0 or int(entry_count) <= 0:
            return None

        index_recs: list[tuple[int, int]] = []
        for _ in range(int(entry_count)):
            (msid, _res, payload_bytes, payload_off) = struct.unpack("<HHII", f.read(12))
            if int(_res) != 0:
                return None
            if int(payload_bytes) != int(frame_count):
                return None
            index_recs.append((int(msid), int(payload_off)))

        prefer = [41, 42, 43]  # EscapeN/EscapeF/EscapeB.
        ordered = [rec for rec in index_recs if rec[0] in set(prefer)] + [
            rec for rec in index_recs if rec[0] not in set(prefer)
        ]

        for (msid, payload_off) in ordered:
            f.seek(payload_off)
            payload = f.read(int(frame_count))
            if len(payload) != int(frame_count):
                return None
            for frame in range(int(frame_count)):
                st = int(payload[frame])
                if st != 0:
                    return (msid, frame, st)

    return None


def _step_once(seed: np.ndarray, prev_inp: np.ndarray, inp: np.ndarray) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed.dtype == SEED_DTYPE
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        out = np.zeros((1, compare_stride), dtype=np.uint8)
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_inp, inp)
        msl_binding.write_compare(handle, out)
        return out.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        msl_binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    "char_name,char_id",
    [
        ("fox", CHAR_FOX),
        ("falco", CHAR_FALCO),
    ],
)
def test_hurtbox_state_overwritten_when_hit_status_nonzero(char_name: str, char_id: int) -> None:
    path = Path("data") / "hit_status" / f"{char_name}.bin"
    if not path.exists():
        pytest.skip(f"missing hit status artifact: {path}")

    found = _find_nonzero_hit_status_entry(path)
    if found is None:
        pytest.skip(f"no nonzero hit status entry found in: {path}")
    (msid, frame, status) = found
    assert int(status) != 0

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(char_id)
    seed["action_id"][0, :2] = np.uint16(0xFFFF)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["hitlag"][0, :2] = np.uint16(2)  # Prevent anim/action timebase advancement in this step.

    seed["hurtbox_state"][0, 1] = np.uint8(0)
    seed["action_frame"][0, 1] = np.int16(int(frame))
    seed["anim_frame_f32"][0, 1] = np.float32(float(int(frame)))
    seed["animation_index"][0, 1] = np.uint32(int(msid))

    prev_inp = np.zeros((1, INPUT_DTYPE.itemsize), dtype=np.uint8)
    inp = np.zeros((1, INPUT_DTYPE.itemsize), dtype=np.uint8)

    out = _step_once(seed, prev_inp, inp)
    got = int(out["hurtbox_state"][1])
    assert got == int(status)
