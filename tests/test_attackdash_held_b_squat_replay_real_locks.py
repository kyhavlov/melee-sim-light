from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_qgd_attackdash_held_b_down_squat_rows_and_control_are_replay_exact() -> None:
    # Replay-real lock for the held-B AttackDash -> Squat lane:
    # - AttackDash IASA delegates to Wait_IASA after the AttackDash-specific pre-gates.
    # - Wait_IASA checks ftCo_SpecialS_CheckInput before Squat.
    # - ftCo_SpecialS_CheckInput requires B plus horizontal stick magnitude, so held-B rows with
    #   neutral X still fall through to Squat.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{
    #   ftCo_SpecialS_CheckInput,ftCo_SpecialS_HasInput}
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    rows = [6222, 6224, 6225, 6226]
    p = 0
    for record in rows:
        assert int(samples.shape[0]) > record, f"replay too short for replay lock row: record={record}"

    lock = samples[rows]

    target_i = 2
    seed = lock["seed_t"][target_i]
    ref = lock["ref_t1"][target_i]
    prev = lock["prev_input_t"][target_i]
    cur = lock["input_t"][target_i]

    assert int(seed["action_id"][p]) == 0x0032  # AttackDash
    assert int(seed["action_frame"][p]) == 35
    assert int(seed["animation_index"][p]) == 52
    assert int(prev["p"]["buttons"][p]) == 0x0200  # held B
    assert int(cur["p"]["buttons"][p]) == 0x0200
    assert int(prev["p"]["main_x"][p]) == 0
    assert int(cur["p"]["main_x"][p]) == 0
    assert int(prev["p"]["main_y"][p]) == -100
    assert int(cur["p"]["main_y"][p]) == -100
    assert int(ref["action_id"][p]) == 0x0027  # Squat
    assert int(ref["action_frame"][p]) == 1
    assert int(ref["animation_index"][p]) == 30

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(lock["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        len(rows), seed_stride
    )
    prev_input_bytes = np.frombuffer(lock["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        len(rows), input_stride
    )
    input_bytes = np.frombuffer(lock["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        len(rows), input_stride
    )
    out_compare_bytes = np.empty((len(rows), compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=len(rows), num_players=int(ds.num_players))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    # Nearby negative control.
    assert int(out["action_id"][0, p]) == int(lock["ref_t1"]["action_id"][0, p]) == 0x0032
    assert int(out["action_frame"][0, p]) == int(lock["ref_t1"]["action_frame"][0, p]) == 33
    assert int(out["animation_index"][0, p]) == int(lock["ref_t1"]["animation_index"][0, p]) == 52
    assert int(out["instance_id"][0, p]) == int(lock["ref_t1"]["instance_id"][0, p]) == 1094
    assert int(out["on_ground"][0, p]) == int(lock["ref_t1"]["on_ground"][0, p]) == 1
    assert int(out["facing"][0, p]) == int(lock["ref_t1"]["facing"][0, p]) == 1
    assert int(out["jumps_left"][0, p]) == int(lock["ref_t1"]["jumps_left"][0, p]) == 2

    # Target-1 flank stays matched.
    assert int(out["action_id"][1, p]) == int(lock["ref_t1"]["action_id"][1, p]) == 0x0032
    assert int(out["action_frame"][1, p]) == int(lock["ref_t1"]["action_frame"][1, p]) == 35
    assert int(out["animation_index"][1, p]) == int(lock["ref_t1"]["animation_index"][1, p]) == 52
    assert int(out["instance_id"][1, p]) == int(lock["ref_t1"]["instance_id"][1, p]) == 1094

    # Improved target row.
    assert int(out["action_id"][target_i, p]) == int(ref["action_id"][p]) == 0x0027
    assert int(out["action_frame"][target_i, p]) == int(ref["action_frame"][p]) == 1
    assert int(out["animation_index"][target_i, p]) == int(ref["animation_index"][p]) == 30
    assert int(out["instance_id"][target_i, p]) == int(ref["instance_id"][p]) == 1096
    assert int(out["on_ground"][target_i, p]) == int(ref["on_ground"][p]) == 1
    assert int(out["facing"][target_i, p]) == int(ref["facing"][p]) == 1
    assert int(out["jumps_left"][target_i, p]) == int(ref["jumps_left"][p]) == 2

    # Target+1 flank stays matched after the Squat entry.
    assert int(out["action_id"][3, p]) == int(lock["ref_t1"]["action_id"][3, p]) == 0x0027
    assert int(out["action_frame"][3, p]) == int(lock["ref_t1"]["action_frame"][3, p]) == 2
    assert int(out["animation_index"][3, p]) == int(lock["ref_t1"]["animation_index"][3, p]) == 30
    assert int(out["instance_id"][3, p]) == int(lock["ref_t1"]["instance_id"][3, p]) == 1096
