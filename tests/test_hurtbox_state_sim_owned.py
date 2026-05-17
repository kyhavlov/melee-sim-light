from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tools.extraction.extract_fighter_script_timeline import EVENT_IDS
from tools.slippi.known_data_artifacts import read_mslftsc1_v1


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


def test_passive_tech_entry_uses_new_state_hurt_status_but_downbound_still_defers() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    def _seed_damageflytop_tech(*, tech_available: bool) -> np.ndarray:
        seed = np.zeros((1,), dtype=SEED_DTYPE)
        seed["stage_id"][0] = np.uint32(STAGE_FD)
        seed["num_players"][0] = np.uint8(2)
        seed["stocks"][0, :2] = np.uint8(4)
        seed["char_id"][0, 0] = np.uint8(CHAR_FOX)
        seed["char_id"][0, 1] = np.uint8(CHAR_FALCO)
        seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)

        # Stable grounded idle for the untouched player.
        seed["action_id"][0, 0] = np.uint16(0x000E)  # Wait
        seed["animation_index"][0, 0] = np.uint32(2)  # ftCo_SM_Wait1_0
        seed["on_ground"][0, 0] = np.uint8(1)
        seed["ground_id"][0, 0] = np.uint16(0)
        seed["facing"][0, 0] = np.uint8(1)

        # Synthetic DamageFlyTop landing row derived from the replay-real GAT rec 265 family.
        # It deterministically lands this step and runs the ftCo_80090184 tech callback ladder.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_80098928
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097D40
        p = 1
        seed["action_id"][0, p] = np.uint16(90)  # DamageFlyTop
        seed["seed_prev_action_id"][0, p] = np.uint16(90)
        seed["seed_prev_action_frame"][0, p] = np.int16(38)
        seed["action_frame"][0, p] = np.int16(39)
        seed["animation_index"][0, p] = np.uint32(180)
        seed["anim_frame_f32"][0, p] = np.float32(39.0)
        seed["facing"][0, p] = np.uint8(1)
        seed["facing_dir1"][0, p] = np.int8(1)
        seed["on_ground"][0, p] = np.uint8(0)
        seed["ground_id"][0, p] = np.uint16(1)
        seed["pos_x"][0, p] = np.float32(65.290565)
        seed["pos_y"][0, p] = np.float32(-4.5349264)
        seed["speed_air_x_self"][0, p] = np.float32(0.0)
        seed["speed_ground_x_self"][0, p] = np.float32(0.0)
        seed["speed_y_self"][0, p] = np.float32(-3.1)
        seed["speed_x_attack"][0, p] = np.float32(0.32850978)
        seed["speed_y_attack"][0, p] = np.float32(1.0110495)
        seed["jumps_left"][0, p] = np.uint8(1)
        seed["hitlag"][0, p] = np.uint16(0)
        seed["hitstun"][0, p] = np.uint16(2)
        seed["hurtbox_state"][0, p] = np.uint8(0)
        seed["colanim_hit_status_x198c"][0, p] = np.uint8(1)
        seed["colanim_timer_x1994"][0, p] = np.uint16(24)
        seed["x680"][0, p] = np.uint8(8)
        seed["x684"][0, p] = np.uint8(255 if tech_available else 0)
        seed["state_flags"][0, p, 3] = np.uint8(0x02)
        return seed

    prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
    inp = np.zeros((1, input_stride), dtype=np.uint8)
    prev_view = prev_inp.view(INPUT_DTYPE).reshape(-1)
    inp_view = inp.view(INPUT_DTYPE).reshape(-1)
    prev_view["p"]["buttons"][0, 1] = np.uint16(0x0020)
    prev_view["p"]["l"][0, 1] = np.uint8(9)
    prev_view["p"]["r"][0, 1] = np.uint8(255)
    inp_view["p"]["buttons"][0, 1] = np.uint16(0x0020)
    inp_view["p"]["l"][0, 1] = np.uint8(9)
    inp_view["p"]["r"][0, 1] = np.uint8(255)

    passive_out = _step_once(_seed_damageflytop_tech(tech_available=True), prev_inp, inp)
    downbound_out = _step_once(_seed_damageflytop_tech(tech_available=False), prev_inp, inp)

    # Passive / PassiveStand entry ownership:
    # - ftCo_80090184 routes grounded tech rows into PassiveStand when tech_is_available and
    #   stick_x is inside the roll gate.
    # - Those entry rows already expose the new state's hurt-status table on frame 0.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_80090184
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_80098928
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
    assert int(passive_out["action_id"][1]) == 199  # PassiveStandB
    assert int(passive_out["action_frame"][1]) == 0
    assert int(passive_out["animation_index"][1]) == 199
    assert int(passive_out["hurtbox_state"][1]) == 2
    assert int(passive_out["on_ground"][1]) == 1
    assert int(passive_out["jumps_left"][1]) == 2

    # Non-tech landing stays on the generic frame-0 defer path and therefore keeps the prior
    # visible hurtbox_state on DownBound entry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097D40
    assert int(downbound_out["action_id"][1]) == 183  # DownBoundU
    assert int(downbound_out["action_frame"][1]) == 0
    assert int(downbound_out["animation_index"][1]) == 183
    assert int(downbound_out["hurtbox_state"][1]) == 0
