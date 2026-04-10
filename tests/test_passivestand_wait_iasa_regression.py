from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@pytest.mark.integration
def test_passivestand_anim_end_shield_hold_enters_guardon_not_escapef_gat_1157() -> None:
    # Replay-real negative for the PassiveStand end bridge:
    # - seed is PassiveStandB on the last anim frame with trigger hold and slight forward stick
    # - ref enters GuardOn, not EscapeF
    # - keep the PassiveStand end fix restricted so locomotion_update_pre() still owns the shield
    #   path instead of pre-consuming it into same-frame GuardOn -> EscapeF.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Anim
    # refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
    root = Path(__file__).resolve().parents[1]
    rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl"
    )
    dataset_path = root / rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[1157:1158]
    p = 0

    assert int(row["seed_t"]["action_id"][0, p]) == 201
    assert int(row["ref_t1"]["action_id"][0, p]) == 178
    assert int(row["input_t"]["p"]["l"][0, p]) == 255
    assert int(row["input_t"]["p"]["buttons"][0, p]) == 0

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = row["seed_t"].view("u1").reshape(1, seed_stride).copy()
    prev_input_bytes = row["prev_input_t"].view("u1").reshape(1, input_stride).copy()
    input_bytes = row["input_t"].view("u1").reshape(1, input_stride).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][p]) == 178
    assert int(out["action_frame"][p]) == -1
    assert int(out["animation_index"][p]) == 0xFFFFFFFF
