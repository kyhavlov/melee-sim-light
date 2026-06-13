from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

MUG = "datasets/marth/replays/validation/marth/MetallicUniqueGrouse.msl"
GAT = (
    "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
    "GracefulAttachedTurtle.msl"
)
QGD = (
    "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
    "QuerulousGrandDinosaur.msl"
)

ACT_DAMAGE_FALL = 0x0026
ACT_FALL = 0x001D


def _one_step(dataset_rel: str, record: int) -> tuple[np.void, np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = binding.sizes()

    def _field_bytes(field: str, stride: int) -> np.ndarray:
        row = samples[record : record + 1]
        return (
            np.frombuffer(row[field].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, stride)
        )

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, _field_bytes("seed_t", int(sizes["seed"])))
        binding.step_input(
            handle,
            _field_bytes("prev_input_t", int(sizes["input"])),
            _field_bytes("input_t", int(sizes["input"])),
        )
        out_bytes = np.empty((1, int(sizes["compare"])), dtype=np.uint8)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        row = samples[record]
        return row["seed_t"].copy(), row["input_t"].copy(), out, row["ref_t1"].copy()
    finally:
        binding.destroy(handle)


# The DamageFall stick-fall gate is ABS(lstick.x) >= x210 (0.8) and x670 < x214 (1) -- i.e. a
# fresh tilt-cross on the IASA frame. UCF 0.84's tumble component additionally hooks
# Interrupt_AS_DamageFall+0xCC and allows the fall at x670 == 1 when |lstick1.x| < x210 (no
# buffering) and check_ucf_xsmash passes (raw pad delta vs two polls ago, delta^2 > 75^2).
# All three witnesses were verified bit-level against playback Dolphin (v12 engine-dump probes
# reports/triage/probe_v11_mug2823 / probe_v11_gat8021 / probe_v11_qgd8825, plus the
# ishiiruka_damagefall_iasa_probe interpreter trace showing ftCo_Fall_Enter called from the
# gate with x670 == 1 on the positive rows and the gate refusing the MUG row).
# refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
# refs/ucf/src/tumble/tumble.cpp
# refs/ucf/include/ucf/pad_buffer.h::check_ucf_xsmash


@pytest.mark.integration
def test_ucf_tumble_xsmash_falls_at_x670_one_gat() -> None:
    # GAT rec 8021: DamageFall entry row; the x670 tilt cross registered one frame earlier at
    # |stick| = 0.5 (below the 0.8 fall threshold), so the IASA frame reads post-update
    # x670 == 1 and the vanilla gate refuses. UCF tumble passes: prev processed stick -0.5 is
    # below the wiggle threshold and the raw pad went 0 -> -97 (delta 97 > 75).
    seed, inp, out, ref = _one_step(GAT, 8021)
    assert int(seed["action_id"][0]) == ACT_DAMAGE_FALL
    assert int(seed["action_frame"][0]) == 0
    assert int(seed["tilt_timer_x"][0]) == 0
    assert int(inp["p"]["main_x"][0]) == -97
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_FALL
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 0


@pytest.mark.integration
def test_ucf_tumble_xsmash_falls_at_x670_one_qgd() -> None:
    # QGD rec 8825: mid-DamageFall (af 8) variant of the same class, opposite direction.
    # check_ucf_xsmash compares against the pad two polls back: 0 -> 100 (delta 100 > 75).
    seed, inp, out, ref = _one_step(QGD, 8825)
    assert int(seed["action_id"][0]) == ACT_DAMAGE_FALL
    assert int(seed["action_frame"][0]) == 8
    assert int(seed["tilt_timer_x"][0]) == 0
    assert int(inp["p"]["main_x"][0]) == 100
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_FALL
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 0


@pytest.mark.integration
def test_ucf_tumble_slow_ramp_stays_in_damagefall_mug() -> None:
    # Negative control (MUG rec 2823): identical gate observables -- post-update x670 == 1,
    # |lstick.x| = 0.8875 >= 0.8 -- but the raw pad ramped -14 -> -30 -> -71, so
    # check_ucf_xsmash fails (delta -71 - (-14) = -57, 57^2 < 75^2) and the vanilla x214
    # compare (1 < 1 false) keeps the fighter in DamageFall. This row is also the witness
    # that removed the old held-stick decrement bridge: the bridge re-passed the vanilla gate
    # here and entered Fall a frame early.
    seed, inp, out, ref = _one_step(MUG, 2823)
    assert int(seed["action_id"][0]) == ACT_DAMAGE_FALL
    assert int(seed["action_frame"][0]) == 8
    assert int(seed["tilt_timer_x"][0]) == 0
    assert int(inp["p"]["main_x"][0]) == -71
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_DAMAGE_FALL
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 9
