from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset, read_dataset_window

ACT_WAIT = 14
ACT_TURN_RUN = 19
ACT_RUN_BRAKE = 23
ACT_GUARD_ON = 178
SM_TURN_RUN = 11
SM_RUN_BRAKE = 14


def _one_step_out_compare(*, ds, row) -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    dataset: str
    record: int
    p: int
    expected_seed_cmd0: int


_DATASET_DIR = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"

_CASES = [
    _Case(dataset="QuerulousGrandDinosaur", record=2341, p=0, expected_seed_cmd0=1),
    _Case(dataset="QuerulousGrandDinosaur", record=2342, p=0, expected_seed_cmd0=1),
    _Case(dataset="QuerulousGrandDinosaur", record=2343, p=0, expected_seed_cmd0=0),
    _Case(dataset="TreasuredBackKangaroo", record=7340, p=0, expected_seed_cmd0=1),
    _Case(dataset="TreasuredBackKangaroo", record=7341, p=0, expected_seed_cmd0=1),
    _Case(dataset="TreasuredBackKangaroo", record=7342, p=0, expected_seed_cmd0=0),
]


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{c.dataset}_{c.record}_p{c.p}")
def test_runbrake_turnrun_cmd0_replay_rows(case: _Case) -> None:
    # Replay-real locks for the seeded RunBrake cmd_var[0] -> TurnRun branch.
    #
    # Decomp:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::{
    #     ftCo_RunBrake_Enter,ftCo_RunBrake_IASA}
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::fn_800C9CEC
    # - refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _DATASET_DIR / f"{case.dataset}.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > case.record, f"dataset too short for record={case.record}"
    row = samples[case.record : case.record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    p = int(case.p)

    assert int(seed["runbrake_cmd0"][p]) == int(case.expected_seed_cmd0)

    out = _one_step_out_compare(ds=ds, row=row)[0]

    for field in ("action_id", "action_frame", "animation_index", "on_ground", "hitlag", "hitstun"):
        got = int(out[field][p])
        exp = int(ref[field][p])
        assert got == exp, f"{case.dataset} rec{case.record} p{p}: field={field} expected={exp} got={got}"

    got_flags = [int(x) for x in out["state_flags"][p]]
    exp_flags = [int(x) for x in ref["state_flags"][p]]
    assert got_flags == exp_flags, (
        f"{case.dataset} rec{case.record} p{p}: state_flags expected={exp_flags} got={got_flags}"
    )


@pytest.mark.integration
def test_marth_runbrake_cmd0_clear_suppresses_same_frame_turnrun_qhp_2131() -> None:
    # Replay-real negative for RunBrake script phase:
    # - seed has post-frame `runbrake_cmd0=1` from the prior frame,
    # - Fighter_procUpdate advances the RunBrake script to frame 15 before IASA,
    # - frame 15 runs set_cmd_var(0,0), so `fn_800C9CEC` must not enter TurnRun.
    #
    # Source owner:
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
    # - refs/melee/src/melee/ft/chara/ftCommon/ftCo_TurnRun.c::fn_800C9CEC
    # - data/moves/marth.json moves["ftCo_SM_RunBrake"].events set_cmd_var(0,0) at frame 15.
    root = Path(__file__).resolve().parents[1]
    dataset = root / "datasets/marth/replays/validation/marth/QuestionableHarmfulPanther.msl"
    if not dataset.exists():
        pytest.skip(f"missing local dataset: {dataset}")

    ds = read_dataset_window(str(dataset), 2131, 2132)
    row = ds.samples[0:1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    p = 0
    assert int(seed["action_id"][p]) == ACT_RUN_BRAKE
    assert int(seed["animation_index"][p]) == SM_RUN_BRAKE
    assert int(seed["action_frame"][p]) == 14
    assert int(seed["runbrake_cmd0"][p]) == 1
    assert int(row["input_t"][0]["p"]["main_x"][p]) == 80
    assert int(ref["action_id"][p]) == ACT_RUN_BRAKE

    out = _one_step_out_compare(ds=ds, row=row)[0]
    for field in ("action_id", "action_frame", "animation_index", "on_ground"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
def test_marth_runbrake_cmd0_active_frame_can_enter_turnrun_before_clear() -> None:
    # Adjacent positive for the same owner: one frame earlier, the advanced script frame is still
    # inside the cmd_var[0] window, so a patched opposite-stick input may enter TurnRun.
    root = Path(__file__).resolve().parents[1]
    dataset = root / "datasets/marth/replays/validation/marth/QuestionableHarmfulPanther.msl"
    if not dataset.exists():
        pytest.skip(f"missing local dataset: {dataset}")

    ds = read_dataset_window(str(dataset), 2130, 2131)
    row = ds.samples[0:1].copy()
    p = 0
    seed = row["seed_t"][0]
    assert int(seed["action_id"][p]) == ACT_RUN_BRAKE
    assert int(seed["action_frame"][p]) == 13
    assert int(seed["runbrake_cmd0"][p]) == 1

    row["input_t"][0]["p"]["main_x"][p] = np.int8(80)
    out = _one_step_out_compare(ds=ds, row=row)[0]
    assert int(out["action_id"][p]) == ACT_TURN_RUN
    assert int(out["animation_index"][p]) == SM_TURN_RUN


@pytest.mark.integration
def test_runbrake_anim_end_wait_iasa_turns_same_frame() -> None:
    # Replay-real lock for RunBrake_Anim -> Wait -> Wait_IASA in one fighter proc:
    # - RunBrake_Anim exits through ft_8008A2BC when the motion ends.
    # - The destination Wait_IASA can then consume the current stick into Turn on the same frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_Anim
    # refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[901:902]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    p = 0
    assert int(seed["action_id"][p]) == 23  # RunBrake
    assert int(seed["action_frame"][p]) == 17
    assert int(ref["action_id"][p]) == 18  # Turn
    assert int(ref["action_frame"][p]) == 1

    out = _one_step_out_compare(ds=ds, row=row)[0]
    for field in ("action_id", "action_frame", "animation_index"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=1e-6
    )


@pytest.mark.integration
@pytest.mark.parametrize("record", [3621, 5165])
def test_marth_runbrake_anim_end_wait_iasa_guardon_same_frame(record: int) -> None:
    # Replay-real positive for the same RunBrake_Anim -> Wait -> Wait_IASA owner as the Turn lock
    # above. Wait_IASA checks `ftCo_80091A4C` before Jump/Dash/Squat/Turn/Walk, so a held trigger on
    # the RunBrake terminal frame enters no-submotion GuardOn instead of exposing a settled Wait.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_Anim
    # refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/marth/replays/validation/marth/BreakableMundaneElephant.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    p = 1
    assert int(seed["action_id"][p]) == ACT_RUN_BRAKE
    assert int(seed["action_frame"][p]) == 25
    assert int(ref["action_id"][p]) == ACT_GUARD_ON
    assert int(ref["animation_index"][p]) == 0xFFFFFFFF

    out = _one_step_out_compare(ds=ds, row=row)[0]
    for field in ("action_id", "action_frame", "animation_index", "on_ground"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert [int(x) for x in out["state_flags"][p]] == [int(x) for x in ref["state_flags"][p]]
