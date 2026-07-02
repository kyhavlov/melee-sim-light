from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
            1760,
            1,
        ),
        (
            "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            2795,
            1,
        ),
        (
            "replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
            2676,
            1,
        ),
        (
            "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
            9559,
            1,
        ),
        # JumpAerialB (28) sample (not part of the original four listed reps, but the same root cause).
        (
            "replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
            2349,
            1,
        ),
    ],
)
def test_jumpaerial_does_not_spuriously_land(dataset_rel: str, record: int, p: int) -> None:
    # Regression lock: JumpAerialF/B (27/28) should not spuriously enter Landing (42) while the
    # Slippi reference remains airborne.
    #
    # Root cause: our ISO-derived SSANIM01 ECB subset (data/anims_ecb/*.bin) omitted ftCo_Submotion
    # ids JumpAerialF/JumpAerialB (18/19), so ECB sampling fell back to 0 (missing table entry) and
    # floor collision grounded too early.
    #
    # Decomp source of these submotion ids:
    # - refs/melee/src/melee/ft/chara/ftCommon/forward.h `ftCo_Submotion`
    # (see tools/extraction/extract_fighter_anims.py::_extra_anim_msids)
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    # Integration policy: skip if required ISO-derived data artifacts are missing locally.
    required = [
        "data/anims_ecb/fox.bin",
        "data/anims_ecb/falco.bin",
        "data/ecb/fox_bottom.bin",
        "data/ecb/fox_extents.bin",
        "data/ecb/falco_bottom.bin",
        "data/ecb/falco_extents.bin",
    ]
    missing = [path for path in required if not (root / path).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    num_records = int(samples.shape[0])
    assert num_records > record, f"replay too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    seed_action = int(row["seed_t"]["action_id"][0, p])
    ref_action = int(row["ref_t1"]["action_id"][0, p])
    # We intentionally keep this as a seed==ref precondition: this regression is meant to lock the
    # dominant seed==ref mismatch cluster (JumpAerialF/B entering Landing due to ECB fallback).
    assert seed_action == ref_action
    assert ref_action in (27, 28)
    assert int(row["ref_t1"]["on_ground"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    expected_action = ref_action
    expected_anim = int(row["ref_t1"]["animation_index"][0, p])

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
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

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, p])
        got_on_ground = int(out["on_ground"][0, p])
        got_anim = int(out["animation_index"][0, p])
        got_hitlag = int(out["hitlag"][0, p])
        got_hitstun = int(out["hitstun"][0, p])

        assert got_action == expected_action, (
            f"record={record} p={p} expected action_id={expected_action}, got {got_action}"
        )
        assert got_on_ground == 0, f"record={record} p={p} expected on_ground=0, got {got_on_ground}"
        assert got_anim == expected_anim, (
            f"record={record} p={p} expected animation_index={expected_anim}, got {got_anim}"
        )
        assert got_hitlag == 0, f"record={record} p={p} expected hitlag=0, got {got_hitlag}"
        assert got_hitstun == 0, f"record={record} p={p} expected hitstun=0, got {got_hitstun}"
    finally:
        binding.destroy(handle)
