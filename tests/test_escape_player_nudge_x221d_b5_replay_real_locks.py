from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset


ACT_WAIT = 0x000E
ACT_GUARD = 0x00B3
ACT_ESCAPE_F = 0x00E9
ACT_ESCAPE_B = 0x00EA
SM_WAIT1_0 = 2
SM_ESCAPE_F = 42


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.bin",
        "data/anims/falco.bin",
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _step_one_row(*, binding, row: np.ndarray, seed_override: np.ndarray | None = None) -> np.void:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_src = seed_override if seed_override is not None else row["seed_t"]
    seed_bytes = np.frombuffer(seed_src.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_src = row["prev_input_t"]
    input_src = row["input_t"]
    prev_input_bytes = np.frombuffer(prev_input_src.tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(input_src.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_escapef_x221d_b5_suppresses_self_player_nudge_but_peer_still_nudges() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    row = read_dataset(str(dataset_path)).samples[7448:7449]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

#Source owner:
#- Escape entry sets fp->x221D_b5.
#- ftCommon_8007E0E4 skips the rolling fighter's self ftCommon_8007DD7C overlap pass, but
#the non - rolling peer still sees the roll as the other fighter and nudges away.
#refs / melee / src / melee / ft / chara / ftCommon / ftCo_Escape.c::ftCo_80099314
#refs / melee / src / melee / ft / ftcommon.c::{ftCommon_8007E0E4, ftCommon_8007DD7C }
    assert int(seed["action_id"][1]) == ACT_ESCAPE_F
    assert int(seed["animation_index"][1]) == SM_ESCAPE_F

    out = _step_one_row(binding=binding, row=row)
    assert float(out["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=2e-6)
    assert float(out["pos_x"][1]) == pytest.approx(float(ref["pos_x"][1]), abs=2e-6)
    assert float(out["pos_x"][0]) == pytest.approx(float(seed["pos_x"][0]) - 0.3, abs=2e-6)
    assert float(out["pos_x"][1]) == pytest.approx(
        float(seed["pos_x"][1]) + float(out["speed_ground_x_self"][1]), abs=2e-6
    )


@pytest.mark.integration
def test_non_escape_overlap_keeps_reciprocal_player_nudge_boundary() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    row = read_dataset(str(dataset_path)).samples[7448:7449]
    seed = row["seed_t"].copy()
    for p in (0, 1):
        seed["action_id"][0, p] = np.uint16(ACT_WAIT)
        seed["seed_prev_action_id"][0, p] = np.uint16(ACT_WAIT)
        seed["action_frame"][0, p] = np.int16(4)
        seed["animation_index"][0, p] = np.uint32(SM_WAIT1_0)
        seed["speed_ground_x_self"][0, p] = np.float32(0.0)
        seed["speed_air_x_self"][0, p] = np.float32(0.0)
        seed["speed_y_self"][0, p] = np.float32(0.0)

    neutral = np.zeros((1,), dtype=INPUT_DTYPE)
    row_neutral = row.copy()
    row_neutral["prev_input_t"] = neutral
    row_neutral["input_t"] = neutral

    out = _step_one_row(binding=binding, row=row_neutral, seed_override=seed.view(SEED_DTYPE))
    assert float(out["pos_x"][0]) == pytest.approx(float(seed["pos_x"][0, 0]) - 0.3, abs=5e-6)
    assert float(out["pos_x"][1]) == pytest.approx(float(seed["pos_x"][0, 1]) + 0.3, abs=5e-6)


@pytest.mark.integration
def test_guard_iasa_escape_entry_keeps_current_frame_player_nudge() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    row = read_dataset(str(dataset_path)).samples[10282:10283]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][0]) == ACT_GUARD
    assert int(ref["action_id"][0]) == ACT_ESCAPE_B

#Guard_IASA enters EscapeB after Fighter_8006A360's ftCommon_8007E0E4 pass. The entry sets
#x221D_b5 for later frames, but must not retroactively suppress the current frame's nudge.
#refs / melee / src / melee / ft / fighter.c::Fighter_8006A360
#refs / melee / src / melee / ft / chara / ftCommon / ftCo_Guard.c::ftCo_Guard_IASA
#refs / melee / src / melee / ft / chara / ftCommon / ftCo_Escape.c::ftCo_80099314
    out = _step_one_row(binding=binding, row=row)
    assert int(out["action_id"][0]) == ACT_ESCAPE_B
    assert int(out["ground_id"][0]) == int(ref["ground_id"][0])
    assert float(out["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=2e-6)
    assert float(out["pos_x"][0]) == pytest.approx(float(seed["pos_x"][0]) + 0.3, abs=5e-6)
