from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import json
import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _apply_deadzone(v: float, dz: float) -> float:
    return 0.0 if abs(v) < dz else v


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
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


_BASE = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        # 236/43/236: EscapeAir should land into LandingFallSpecial.
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 815, 0, 236, 43),
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 6259, 0, 236, 43),
        _Case(f"{_BASE}/GracefulAttachedTurtle.msl", 2069, 0, 236, 43),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.msl", 159, 0, 236, 43),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.msl", 248, 0, 236, 43),
        _Case(f"{_BASE}/TreasuredBackKangaroo.msl", 1267, 0, 236, 43),
        # 178/179/178: GuardOn should become Guard.
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 334, 1, 178, 179),
        _Case(f"{_BASE}/GracefulAttachedTurtle.msl", 1382, 0, 178, 179),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.msl", 2478, 1, 178, 179),
        _Case(f"{_BASE}/TreasuredBackKangaroo.msl", 1590, 0, 178, 179),
        # 42/40/42: Landing should transition to SquatWait (crouch).
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.msl", 1400, 0, 42, 40),
        _Case(f"{_BASE}/GracefulAttachedTurtle.msl", 144, 0, 42, 40),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.msl", 122, 0, 42, 40),
        _Case(f"{_BASE}/TreasuredBackKangaroo.msl", 7260, 1, 42, 40),
    ],
)
def test_one_step_action_transition_cluster_records(case: _Case) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > int(case.record), f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[case.record : case.record + 1]
    p = int(case.p)

    assert int(row["seed_t"]["action_id"][0, p]) == int(case.seed_action)
    assert int(row["ref_t1"]["action_id"][0, p]) == int(case.ref_action)
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    if int(case.seed_action) == 236:
        assert int(row["seed_t"]["on_ground"][0, p]) == 0
        assert int(row["ref_t1"]["on_ground"][0, p]) == 1

    if int(case.seed_action) == 178:
        buttons = int(row["input_t"]["p"][0, p]["buttons"])
        l = int(row["input_t"]["p"][0, p]["l"])
        r = int(row["input_t"]["p"][0, p]["r"])
        assert int(row["seed_t"]["on_ground"][0, p]) == 1
        assert int(row["seed_t"]["guard_x10"][0, p]) == 0
        assert (buttons & (0x0040 | 0x0020)) != 0 or l > 0 or r > 0

    if int(case.seed_action) == 42:
        cd = json.loads((root / "data/common/ft_common_data.json").read_text())
        crouch_thr = float(cd["crouch_stick_threshold"])
        dz_y = float(cd["lstick_deadzone_y"])

        main_y = int(row["input_t"]["p"][0, p]["main_y"])
        stick_y = _apply_deadzone(float(main_y) * (1.0 / 80.0), dz_y)
        assert stick_y < -crouch_thr

        # Seed sanity: this cluster triggers on the first interruptible Landing frame (see decomp
        # ftCo_Landing_IASA squat gate). The sim advances timebase before IASA, so a seed state_age
        # of (landing_lag - frame_speed_mul) makes the post-advance frame land exactly at landing_lag.
        char_id = int(row["seed_t"]["char_id"][0, p])
        if char_id == 1:
            char_key = "fox"
        elif char_id == 22:
            char_key = "falco"
        else:
            pytest.skip(f"unexpected char_id for Landing crouch case: {char_id}")
        landing_lag_frames = int(
            json.loads((root / f"data/characters/{char_key}.json").read_text())["landing_lag_frames"]
        )
        seed_af = float(row["seed_t"]["anim_frame_f32"][0, p])
        seed_spd = float(row["seed_t"]["frame_speed_mul_f32"][0, p])
        cur_af = seed_af + seed_spd
        assert cur_af >= float(landing_lag_frames)
        assert cur_af < float(landing_lag_frames) + seed_spd

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.header["num_players"]))
    got_a = int(out["action_id"][p])
    want_a = int(row["ref_t1"]["action_id"][0, p])
    assert got_a == want_a, f"dataset={case.dataset_rel} record={case.record} p={p} expected action_id={want_a}, got {got_a}"
    assert int(out["hitlag"][p]) == int(row["ref_t1"]["hitlag"][0, p])
    assert int(out["hitstun"][p]) == int(row["ref_t1"]["hitstun"][0, p])
