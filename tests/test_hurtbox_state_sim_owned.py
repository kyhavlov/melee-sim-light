from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tools.extraction.extract_fighter_script_timeline import EVENT_IDS
from tools.extraction.known_data_artifacts import read_mslftsc1_v1


# Character ids (GALE01): Slippi post-frame `character`.
CHAR_FOX = 1
CHAR_FALCO = 22

# Stage ids (GALE01): Final Destination.
STAGE_FD = 32


def _find_nonzero_hit_status_entry(path: Path) -> tuple[int, int, int] | None:
    table = read_mslftsc1_v1(path)
    by_msid = {
        int(entry.msid): table.events[entry.first_event : entry.first_event + entry.event_count]
        for entry in table.entries
    }
    prefer = [41, 42, 43]  # EscapeN/EscapeF/EscapeB.
    ordered_msids = prefer + [msid for msid in sorted(by_msid) if msid not in set(prefer)]
    for msid in ordered_msids:
        for ev in by_msid.get(msid, ()):
            if int(ev.kind_id) != EVENT_IDS["set_hit_status"]:
                continue
            status = int(ev.payload.get("state", 0))
            if status != 0:
                return (msid, int(ev.frame), status)
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
    path = Path("data") / "scripts" / f"{char_name}.bin"
    if not path.exists():
        pytest.skip(f"missing script timeline artifact: {path}")

    found = _find_nonzero_hit_status_entry(path)
    if found is None:
        pytest.skip(f"no nonzero hit status event found in: {path}")
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
