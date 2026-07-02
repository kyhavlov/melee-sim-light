from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import json
import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers

ACT_SQUAT = 39
SM_SQUAT = 30
ACT_SQUAT_RV = 41
SM_SQUAT_RV = 34


def _apply_deadzone(v: float, dz: float) -> float:
    return 0.0 if abs(v) < dz else v


def _step_one_record(*, binding, row: np.ndarray, num_players: int) -> np.ndarray:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(num_players))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int
    seed_action: int


_BASE = "replays/validation/cardinal_1.0_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.slpz", 265, 0, 235),
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.slpz", 685, 0, 74),
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.slpz", 1855, 0, 70),
        _Case(f"{_BASE}/GracefulAttachedTurtle.slpz", 1034, 1, 70),
        _Case(f"{_BASE}/GracefulAttachedTurtle.slpz", 3646, 1, 74),
        _Case(f"{_BASE}/GracefulAttachedTurtle.slpz", 4209, 0, 74),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.slpz", 315, 0, 72),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.slpz", 1312, 0, 72),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.slpz", 1469, 0, 74),
        _Case(f"{_BASE}/TreasuredBackKangaroo.slpz", 306, 0, 73),
        _Case(f"{_BASE}/TreasuredBackKangaroo.slpz", 344, 0, 74),
        _Case(f"{_BASE}/TreasuredBackKangaroo.slpz", 390, 0, 20),
    ],
)
def test_squat_entry_records_hold_down(case: _Case) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")
    common_path = root / "data/common/ft_common_data.json"
    if not common_path.exists():
        pytest.skip("missing local artifact: data/common/ft_common_data.json")

    common = json.loads(common_path.read_text())
    crouch_thr = float(common["crouch_stick_threshold"])
    dz_y = float(common["lstick_deadzone_y"])

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > int(case.record), f"replay too short: num_records={int(samples.shape[0])}"
    row = samples[case.record : case.record + 1]
    p = int(case.p)

    assert int(row["seed_t"]["action_id"][0, p]) == int(case.seed_action)
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_SQUAT
    assert int(row["seed_t"]["on_ground"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    main_y = int(row["input_t"]["p"][0, p]["main_y"])
    stick_y = _apply_deadzone(float(main_y) * (1.0 / 80.0), dz_y)
    assert stick_y < -crouch_thr

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == ACT_SQUAT
    assert int(out["animation_index"][p]) == SM_SQUAT
    assert int(out["hitlag"][p]) == int(row["ref_t1"]["hitlag"][0, p])
    assert int(out["hitstun"][p]) == int(row["ref_t1"]["hitstun"][0, p])


@pytest.mark.integration
def test_wait_iasa_neutral_stick_does_not_enter_squat() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    dataset_rel = f"{_BASE}/AttachedGoodNaturedGuanaco.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")
    common_path = root / "data/common/ft_common_data.json"
    if not common_path.exists():
        pytest.skip("missing local artifact: data/common/ft_common_data.json")

    common = json.loads(common_path.read_text())
    crouch_thr = float(common["crouch_stick_threshold"])
    dz_y = float(common["lstick_deadzone_y"])

    # Replay-real neutral Wait frame: no crouch input, and ref/out should remain Wait.
    # Replay-buffer row: AGG rec=156 p=1.
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[156:157]
    p = 1

    assert int(row["seed_t"]["action_id"][0, p]) == 14  # Wait
    assert int(row["ref_t1"]["action_id"][0, p]) == 14  # Wait
    assert int(row["seed_t"]["on_ground"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    main_y = int(row["input_t"]["p"][0, p]["main_y"])
    stick_y = _apply_deadzone(float(main_y) * (1.0 / 80.0), dz_y)
    assert abs(stick_y) < crouch_thr

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == 14
    assert int(out["action_id"][p]) != ACT_SQUAT


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "player", "seed_action"),
    [
        (f"{_BASE}/AttachedGoodNaturedGuanaco.slpz", 5867, 1, 39),
        (f"{_BASE}/AttachedGoodNaturedGuanaco.slpz", 7106, 0, 57),
        (f"{_BASE}/GracefulAttachedTurtle.slpz", 6234, 1, 39),
        (f"{_BASE}/QuerulousGrandDinosaur.slpz", 6462, 0, 57),
        (f"{_BASE}/QuerulousGrandDinosaur.slpz", 7616, 1, 57),
    ],
)
def test_squat_and_attacklw3_anim_end_route_to_squatrv_on_release(
    dataset_rel: str, record: int, player: int, seed_action: int
) -> None:
    # Replay-real lock for the crouch-release chain:
    # - ftCo_Squat_Anim and ftCo_AttackLw3_Anim both route anim-end through ftCo_800D638C
    #   (enter SquatWait), then SquatWait_IASA checks Dash first and SquatRv_CheckInput after.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatWait.c::{ftCo_800D638C,ftCo_SquatWait_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_CheckInput
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > int(record), f"replay too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]
    p = int(player)

    assert int(row["seed_t"]["action_id"][0, p]) == int(seed_action)
    assert int(row["seed_t"]["on_ground"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_SQUAT_RV
    assert int(row["ref_t1"]["action_frame"][0, p]) == 0
    assert int(row["ref_t1"]["animation_index"][0, p]) == SM_SQUAT_RV
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["action_frame"][p]) == int(row["ref_t1"]["action_frame"][0, p])
    assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])
    assert int(out["hitlag"][p]) == int(row["ref_t1"]["hitlag"][0, p])
    assert int(out["hitstun"][p]) == int(row["ref_t1"]["hitstun"][0, p])
