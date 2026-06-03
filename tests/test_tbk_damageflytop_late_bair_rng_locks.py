from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_ATTACK_AIR_B = 0x0043
ACT_DAMAGE_FLY_N = 0x0058
ACT_DAMAGE_FLY_ROLL = 0x005B
ACT_DAMAGE_FLY_TOP = 0x005A


def _dataset_path(root: Path) -> Path:
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    return dataset_path


def _bytes(row_field: np.ndarray, stride: int) -> np.ndarray:
    return np.frombuffer(row_field.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, stride)


def _step_one_with_seed(seed: np.ndarray, row: np.ndarray, *, num_players: int) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=num_players, ucf_enabled=1)
    out = np.empty((1, compare_stride), dtype=np.uint8)
    try:
        binding.reseed_seed(handle, _bytes(seed, seed_stride))
        binding.step_input(
            handle,
            _bytes(row["prev_input_t"], input_stride),
            _bytes(row["input_t"], input_stride),
        )
        binding.write_compare(handle, out)
    finally:
        binding.destroy(handle)
    return out.view(COMPARE_DTYPE).reshape(1)[0]


@pytest.mark.integration
def test_tbk_damageflytop_late_bair_live_source_supplies_pre_gate_count() -> None:
    # TBK 6929: the explicit seed lane says the DamageFlyTop victim needs two
    # Fighter_8006CDA4 pre-gate consumes, but this live rollout boundary starts before the direct
    # seed row. The current ProcessHit source is late AttackAirB frame 6; that source owner must
    # supply the two consumes even when the replay seed lane is absent.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    ds = read_dataset(str(_dataset_path(root)))
    record = 6929
    victim = 1
    attacker = 0
    row = ds.samples[record : record + 1]
    seed = row["seed_t"].copy()

    assert int(seed[0]["action_id"][victim]) == ACT_DAMAGE_FLY_TOP
    assert int(seed[0]["action_id"][attacker]) == ACT_ATTACK_AIR_B
    assert int(seed[0]["action_frame"][attacker]) == 6
    assert int(seed[0]["fighter_8006cda4_pre_gate_consume_count"][victim]) == 2
    assert int(seed[0]["instance_hit_by"][victim]) != int(seed[0]["instance_id"][attacker])

    seed[0]["fighter_8006cda4_pre_gate_consume_count"][victim] = np.uint8(0)
    out = _step_one_with_seed(seed, row, num_players=int(ds.header["num_players"]))
    ref = ds.samples[record]["ref_t1"]

    assert int(out["action_id"][victim]) == int(ref["action_id"][victim]) == ACT_DAMAGE_FLY_ROLL
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim])
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim])
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]), abs=1e-5)


@pytest.mark.integration
def test_tbk_damageflytop_early_bair_create_edge_does_not_use_late_pre_gate_count() -> None:
    # The late-Bair source owner starts after the create-edge phase. TBK 3907 is an early BAir
    # frame-3 DamageFlyTop contact; with the direct replay seed lane removed, it must not receive
    # the late two-consume runtime owner.
    root = Path(__file__).resolve().parents[1]
    ds = read_dataset(str(_dataset_path(root)))
    record = 3907
    victim = 1
    attacker = 0
    row = ds.samples[record : record + 1]
    seed = row["seed_t"].copy()

    assert int(seed[0]["action_id"][victim]) == ACT_DAMAGE_FLY_TOP
    assert int(seed[0]["action_id"][attacker]) == ACT_ATTACK_AIR_B
    assert int(seed[0]["action_frame"][attacker]) == 3
    assert int(seed[0]["fighter_8006cda4_pre_gate_consume_count"][victim]) == 2

    seed[0]["fighter_8006cda4_pre_gate_consume_count"][victim] = np.uint8(0)
    out = _step_one_with_seed(seed, row, num_players=int(ds.header["num_players"]))

    assert int(out["action_id"][victim]) == ACT_DAMAGE_FLY_N
    assert int(out["action_id"][victim]) != ACT_DAMAGE_FLY_ROLL
