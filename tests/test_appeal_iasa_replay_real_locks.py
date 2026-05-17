from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.run_longest_rollout_streaks import _load_binding
from tools.slippi.known_data_artifacts import read_mslftsc1_v1


_HVG = Path("datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl")

ACT_WAIT = 14
ACT_WALK_SLOW = 15
ACT_DASH = 20
ACT_RUN = 21
ACT_ATTACK_DASH = 50
ACT_APPEAL_SR = 264
SM_WALK_SLOW = 7
SM_DASH = 12
SM_RUN = 13
SM_APPEAL_SR = 239
ACT_ATTACK_11 = 44
ACT_GUARD_ON = 178
ACT_GUARD_REFLECT = 182
ACT_CATCH = 212
ACT_FX_SPECIAL_N_START = 341
BUTTON_A = 0x0100
BUTTON_B = 0x0200
BUTTON_Z = 0x0010
BUTTON_L = 0x0040
BUTTON_D_UP = 0x0008
STATE_FLAG_ALLOW_INTERRUPT = 0x80


def _byte_views(ds):
    samples = ds.samples
    stride = int(samples.dtype.itemsize)
    u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), stride)
    return (
        u8,
        int(samples.dtype.fields["seed_t"][1]),
        int(samples.dtype.fields["prev_input_t"][1]),
        int(samples.dtype.fields["input_t"][1]),
    )


def _step_one(
    dataset_path: Path,
    record: int,
    *,
    mutate_seed=None,
    mutate_prev_input=None,
    mutate_input=None,
) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    samples_u8, seed_off, prev_input_off, input_off = _byte_views(ds)

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = samples_u8[record : record + 1, seed_off : seed_off + seed_stride].copy()
    prev_input_bytes = samples_u8[
        record : record + 1, prev_input_off : prev_input_off + input_stride
    ].copy()
    input_bytes = samples_u8[record : record + 1, input_off : input_off + input_stride].copy()
    if mutate_seed is not None:
        seed_view = seed_bytes.view(samples["seed_t"].dtype).reshape(1)
        mutate_seed(seed_view[0])
    if mutate_prev_input is not None:
        prev_input_view = prev_input_bytes.view(samples["prev_input_t"].dtype).reshape(1)
        mutate_prev_input(prev_input_view[0])
    if mutate_input is not None:
        input_view = input_bytes.view(samples["input_t"].dtype).reshape(1)
        mutate_input(input_view[0])

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)
    return samples["seed_t"][record].copy(), samples["ref_t1"][record].copy(), out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


def _rollout_to(dataset_path: Path, start_record: int, target_record: int) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    samples_u8, seed_off, prev_input_off, input_off = _byte_views(ds)

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = samples_u8[start_record : start_record + 1, seed_off : seed_off + seed_stride].copy()
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, target_record + 1):
            binding.step_input(
                handle,
                samples_u8[record : record + 1, prev_input_off : prev_input_off + input_stride].copy(),
                samples_u8[record : record + 1, input_off : input_off + input_stride].copy(),
            )
            binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)
    return samples["seed_t"][start_record].copy(), samples["ref_t1"][target_record].copy(), out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


@pytest.mark.integration
def test_wait_dpad_up_edge_enters_common_appeal_one_step_and_rollout() -> None:
    # HVG 619 is a replay-real Wait_IASA common taunt edge:
    # - seed row is steady Wait, current input has a fresh D-Pad Up edge.
    # - ftCo_Wait_IASA reaches ftCo_800DE9D8 after guard and before jump/locomotion.
    # - The continuous rollout starts earlier at HVG 588, matching the F10h disruptive packet.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::{ftCo_800DE9B8,ftCo_800DE9D8}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HVG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, ref, out = _step_one(dataset_path, 619)
    assert int(seed["action_id"][0]) == ACT_WAIT
    assert int(ref["action_id"][0]) == ACT_APPEAL_SR
    assert int(out["action_id"][0]) == ACT_APPEAL_SR
    assert int(out["animation_index"][0]) == SM_APPEAL_SR
    assert int(out["action_frame"][0]) == 0
    assert (int(out["state_flags"][0][0]) & STATE_FLAG_ALLOW_INTERRUPT) == 0

    _, ref_roll, out_roll = _rollout_to(dataset_path, 588, 619)
    assert int(ref_roll["action_id"][0]) == ACT_APPEAL_SR
    assert int(out_roll["action_id"][0]) == ACT_APPEAL_SR
    assert int(out_roll["animation_index"][0]) == SM_APPEAL_SR


@pytest.mark.integration
def test_common_appeal_requires_dpad_up_edge_not_held_button() -> None:
    # ftCo_800DE9B8 checks input.x668, not held_inputs. Mutating the previous input to already hold
    # D-Pad Up removes the edge and must not enter Appeal from steady Wait.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HVG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    def hold_dpad_up(prev_input):
        prev_input["p"][0]["buttons"] = np.uint16(
            int(prev_input["p"][0]["buttons"]) | BUTTON_D_UP
        )

    seed, _, out = _step_one(dataset_path, 619, mutate_prev_input=hold_dpad_up)
    assert int(seed["action_id"][0]) == ACT_WAIT
    assert int(out["action_id"][0]) == ACT_WAIT


@pytest.mark.integration
@pytest.mark.parametrize(
    ("button", "expected_actions", "note"),
    [
        (BUTTON_A, {ACT_ATTACK_11}, "Wait_IASA grounded attacks beat ftCo_800DE9D8"),
        (BUTTON_B, {ACT_FX_SPECIAL_N_START}, "Wait_IASA grounded specials beat ftCo_800DE9D8"),
        (BUTTON_Z, {ACT_CATCH}, "Wait_IASA catch beats ftCo_800DE9D8"),
        (
            BUTTON_L,
            {ACT_GUARD_ON, ACT_GUARD_REFLECT},
            "Wait_IASA guard entry beats ftCo_800DE9D8",
        ),
    ],
)
def test_wait_dpad_up_combined_inputs_keep_higher_priority_iasa(
    button: int, expected_actions: set[int], note: str
) -> None:
    # Common Appeal is the ftCo_800DE9D8 tail in Wait_IASA. Combined inputs must be consumed by
    # the earlier decomp checks rather than taunting merely because a D-Pad Up edge is present.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::ftCo_800DE9D8
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HVG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    def add_button(cur_input):
        cur_input["p"][0]["buttons"] = np.uint16(
            int(cur_input["p"][0]["buttons"]) | button | BUTTON_D_UP
        )

    seed, _, out = _step_one(dataset_path, 619, mutate_input=add_button)
    assert int(seed["action_id"][0]) == ACT_WAIT
    assert int(out["action_id"][0]) in expected_actions, note
    assert int(out["action_id"][0]) != ACT_APPEAL_SR


@pytest.mark.integration
def test_walk_dpad_up_edge_enters_common_appeal_from_non_wait_source() -> None:
    # Walk_IASA is another decomp caller of ftCo_800DE9D8. Use the replay-real Wait taunt row as
    # the state shell and mutate only the source motion to WalkSlow, proving the implementation is
    # not a Wait-only shortcut.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::ftCo_800DE9D8
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HVG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    def source_walk(seed):
        seed["action_id"][0] = np.uint16(ACT_WALK_SLOW)
        seed["animation_index"][0] = np.uint32(SM_WALK_SLOW)
        seed["action_frame"][0] = np.int16(3)
        seed["anim_frame_f32"][0] = np.float32(3.0)

    _, _, out = _step_one(dataset_path, 619, mutate_seed=source_walk)
    assert int(out["action_id"][0]) == ACT_APPEAL_SR
    assert int(out["animation_index"][0]) == SM_APPEAL_SR


@pytest.mark.integration
@pytest.mark.parametrize(
    ("source_action", "source_anim"),
    [(ACT_DASH, SM_DASH), (ACT_RUN, SM_RUN)],
)
def test_dash_and_run_dpad_up_edge_enter_common_appeal_after_priority_chain(
    source_action: int, source_anim: int
) -> None:
    # Dash_IASA and Run_IASA both call ftCo_800DE9D8 after catch/attack/guard and before their
    # later jump/locomotion tails. Mutate the replay-real Wait taunt row into each source action to
    # lock that the implementation is not limited to Wait/Walk/Turn/Squat callers.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HVG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    def source_motion(seed):
        seed["action_id"][0] = np.uint16(source_action)
        seed["animation_index"][0] = np.uint32(source_anim)
        seed["action_frame"][0] = np.int16(3)
        seed["anim_frame_f32"][0] = np.float32(3.0)
        seed["speed_ground_x_self"][0] = np.float32(1.0)
        seed["run_x0"][0] = np.uint8(1 if source_action == ACT_RUN else 0)

    _, _, out = _step_one(dataset_path, 619, mutate_seed=source_motion)
    assert int(out["action_id"][0]) == ACT_APPEAL_SR
    assert int(out["animation_index"][0]) == SM_APPEAL_SR


@pytest.mark.integration
@pytest.mark.parametrize(
    ("source_action", "source_anim"),
    [(ACT_DASH, SM_DASH), (ACT_RUN, SM_RUN)],
)
def test_dash_and_run_dpad_up_plus_a_keeps_attack_priority(
    source_action: int, source_anim: int
) -> None:
    # The Dash/Run Appeal hook sits below their attack checks, matching decomp priority.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HVG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    def source_motion(seed):
        seed["action_id"][0] = np.uint16(source_action)
        seed["animation_index"][0] = np.uint32(source_anim)
        seed["action_frame"][0] = np.int16(3)
        seed["anim_frame_f32"][0] = np.float32(3.0)
        seed["speed_ground_x_self"][0] = np.float32(1.0)
        seed["run_x0"][0] = np.uint8(1 if source_action == ACT_RUN else 0)

    def add_a(cur_input):
        cur_input["p"][0]["buttons"] = np.uint16(
            int(cur_input["p"][0]["buttons"]) | BUTTON_D_UP | BUTTON_A
        )

    _, _, out = _step_one(dataset_path, 619, mutate_seed=source_motion, mutate_input=add_a)
    assert int(out["action_id"][0]) == ACT_ATTACK_DASH


def test_fox_falco_common_appeal_has_no_extracted_allow_interrupt_script_event() -> None:
    # ftCo_AppealS_IASA can run only if the command script sets fp->allow_interrupt. MSLFTSC1 has
    # no AppealSR/SL allow_interrupt events for current Fox/Falco, so runtime keeps common Appeal
    # anim-end-only even though full-domain MSLFTSC1 includes those subactions.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::ftCo_AppealS_IASA
    root = Path(__file__).resolve().parents[1]
    for slug in ("fox", "falco"):
        table = read_mslftsc1_v1(root / "data" / "scripts" / f"{slug}.bin")
        by_msid = {
            entry.msid: table.events[entry.first_event : entry.first_event + entry.event_count]
            for entry in table.entries
        }
        assert all(int(ev.kind_id) != 9 for msid in (239, 240) for ev in by_msid.get(msid, ()))


@pytest.mark.integration
def test_common_appeal_stale_allow_interrupt_seed_does_not_open_iasa_without_script_event() -> None:
    # Defensive runtime lock for the data-backed policy above: even if a replay seed carries stale
    # state_flags[0]&0x80 on Appeal, current Fox/Falco Appeal has no script-owned allow window and
    # should not consume A into Attack11.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _HVG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    def source_appeal(seed):
        seed["action_id"][0] = np.uint16(ACT_APPEAL_SR)
        seed["animation_index"][0] = np.uint32(SM_APPEAL_SR)
        seed["action_frame"][0] = np.int16(3)
        seed["anim_frame_f32"][0] = np.float32(3.0)
        seed["state_flags"][0][0] = np.uint8(int(seed["state_flags"][0][0]) | STATE_FLAG_ALLOW_INTERRUPT)

    def add_a(cur_input):
        cur_input["p"][0]["buttons"] = np.uint16(
            int(cur_input["p"][0]["buttons"]) | BUTTON_A
        )

    _, _, out = _step_one(dataset_path, 619, mutate_seed=source_appeal, mutate_input=add_a)
    assert int(out["action_id"][0]) == ACT_APPEAL_SR
    assert int(out["action_id"][0]) != ACT_ATTACK_11
