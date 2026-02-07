from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

_STICK_MAX = 80.0
_BUTTON_A = 0x0100


def _stick_unit(v: int) -> float:
    return float(v) * (1.0 / _STICK_MAX)


def _facing_dir(facing_u8: int) -> float:
    return 1.0 if facing_u8 != 0 else -1.0


def _skip_if_missing_local_artifacts(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local extracted artifacts: {', '.join(missing)}")


def _step_one_record(*, binding, row: np.ndarray, num_players: int) -> np.ndarray:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(num_players))
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
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int
    seed_action: int
    ref_action: int
    ref_anim: int
    cluster: str


_BASE = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        # AttackAirB (67) should not be mis-selected to AttackAirHi (68) on c-stick edge.
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 1306, 0, 25, 67, 70, "attackair_cstick_back"),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.msl", 545, 0, 26, 67, 70, "attackair_cstick_back"),
        # AttackAirF (66) should not be mis-selected to AttackAirHi (68) on A-press + main stick.
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 4495, 1, 25, 66, 69, "attackair_a_forward"),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.msl", 5979, 1, 25, 66, 69, "attackair_a_forward"),
        # KneeBend->Jump should use previous-frame stick for JumpF/JumpB selection.
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 282, 0, 24, 25, 16, "kneebend_jump_dir"),
        _Case(f"{_BASE}/GracefulAttachedTurtle.msl", 780, 1, 24, 25, 16, "kneebend_jump_dir"),
        # KneeBend->Jump direction reads deadzoned fp->input.lstick.x (common input update path).
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 7011, 0, 24, 25, 16, "kneebend_jump_dir_deadzone"),
        _Case(f"{_BASE}/GracefulAttachedTurtle.msl", 4233, 1, 24, 25, 16, "kneebend_jump_dir_deadzone"),
    ],
)
def test_attackair_direction_and_jump_selection_cluster_records(case: _Case) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_missing_local_artifacts(root)
    common = json.loads((root / "data/common/ft_common_data.json").read_text())

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    assert int(ds.samples.shape[0]) > int(case.record), f"dataset too short: num_records={int(ds.samples.shape[0])}"
    row = ds.samples[case.record : case.record + 1]
    p = int(case.p)

    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    inp = row["input_t"]["p"][0, p]
    prev_inp = row["prev_input_t"]["p"][0, p]

    assert int(seed["action_id"][p]) == int(case.seed_action)
    assert int(ref["action_id"][p]) == int(case.ref_action)
    assert int(ref["animation_index"][p]) == int(case.ref_anim)

    assert int(seed["hitlag"][p]) == 0
    assert int(ref["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) == 0
    assert int(ref["hitstun"][p]) == 0

    if case.cluster == "attackair_cstick_back":
        c_dz_x = float(common["attackair_stick_deadzone_x"])
        c_dz_y = float(common["attackair_stick_deadzone_y"])
        prev_cx = _stick_unit(int(prev_inp["c_x"]))
        prev_cy = _stick_unit(int(prev_inp["c_y"]))
        cur_cx = _stick_unit(int(inp["c_x"]))
        cur_cy = _stick_unit(int(inp["c_y"]))
        c_edge = (abs(prev_cx) < c_dz_x and abs(cur_cx) >= c_dz_x) or (abs(prev_cy) < c_dz_y and abs(cur_cy) >= c_dz_y)
        assert c_edge
        assert (int(inp["buttons"]) & _BUTTON_A) == 0

    if case.cluster == "attackair_a_forward":
        c_dz_x = float(common["attackair_stick_deadzone_x"])
        c_dz_y = float(common["attackair_stick_deadzone_y"])
        prev_cx = _stick_unit(int(prev_inp["c_x"]))
        prev_cy = _stick_unit(int(prev_inp["c_y"]))
        cur_cx = _stick_unit(int(inp["c_x"]))
        cur_cy = _stick_unit(int(inp["c_y"]))
        c_edge = (abs(prev_cx) < c_dz_x and abs(cur_cx) >= c_dz_x) or (abs(prev_cy) < c_dz_y and abs(cur_cy) >= c_dz_y)
        assert not c_edge
        assert (int(inp["buttons"]) & _BUTTON_A) != 0
        assert (int(prev_inp["buttons"]) & _BUTTON_A) == 0

    if case.cluster == "kneebend_jump_dir":
        jump_back = float(common["jump_back_x_threshold"])
        facing_dir = _facing_dir(int(seed["facing"][p]))
        prev_x = _stick_unit(int(prev_inp["main_x"]))
        cur_x = _stick_unit(int(inp["main_x"]))

        # Decomp: ftCo_Jump_Enter chooses JumpF/JumpB via (lstick.x * facing_dir) > -x78.
        # For these locked rows, current-frame stick would pick JumpB but replay picks JumpF.
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Enter
        assert (prev_x * facing_dir) > -jump_back
        assert (cur_x * facing_dir) <= -jump_back

    if case.cluster == "kneebend_jump_dir_deadzone":
        jump_back = float(common["jump_back_x_threshold"])
        deadzone_x = float(common["lstick_deadzone_x"])
        facing_dir = _facing_dir(int(seed["facing"][p]))
        prev_x = _stick_unit(int(prev_inp["main_x"]))

        # Decomp: fp->input.lstick.x is deadzoned in Fighter_Spaghetti_8006AD10 before
        # ftCo_KneeBend_Anim -> ftCo_Jump_Enter uses it for JumpF/JumpB selection.
        # refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10
        # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Enter
        assert abs(prev_x) < deadzone_x
        assert (prev_x * facing_dir) <= -jump_back

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.header["num_players"]))
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
