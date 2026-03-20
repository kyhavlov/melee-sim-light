from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    rows: tuple[int, int, int, int]
    player: int


_CASES = (
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl"
        ),
        rows=(5415, 5416, 5417, 5418),
        player=1,
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl"
        ),
        rows=(5919, 5920, 5921, 5922),
        player=1,
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl"
        ),
        rows=(3873, 3874, 3875, 3876),
        player=0,
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl"
        ),
        rows=(707, 708, 709, 710),
        player=1,
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).stem}-p{c.player}")
def test_landingfallspecial_illusion_source_rows_and_controls_are_replay_exact(case: _Case) -> None:
    # Replay-real lock for LandingFallSpecial source-lag parity:
    # - EscapeAir_Coll enters ftCo_LandingFallSpecial_Enter(..., p_ftCommonData->x344).
    # - Fox/Falco Illusion end collision enters ftCo_LandingFallSpecial_Enter(...,
    #   da->x50_FOX_ILLUSION_LANDING_LAG).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    rows = list(case.rows)
    p = int(case.player)
    for record in rows:
        assert int(samples.shape[0]) > record, f"dataset too short for replay lock row: record={record}"

    lock = samples[rows]

    target_i = 2
    target = lock[target_i]
    assert int(target["seed_t"]["action_id"][p]) == 43  # LandingFallSpecial
    assert int(target["seed_t"]["action_frame"][p]) == 0
    assert int(target["seed_t"]["animation_index"][p]) == 36
    assert int(target["seed_t"]["seed_prev_action_id"][p]) == 352  # SpecialAirSEnd
    assert int(target["ref_t1"]["action_id"][p]) == 43
    assert int(target["ref_t1"]["action_frame"][p]) == 1
    assert int(target["ref_t1"]["animation_index"][p]) == 36
    assert int(lock[1]["seed_t"]["action_id"][p]) == 352  # source row
    assert int(lock[1]["ref_t1"]["action_id"][p]) == 43

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

    handle = binding.init(batch_size=len(rows), num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    # Nearby negative control/source row stays exact.
    assert int(out["action_id"][0, p]) == int(lock["ref_t1"]["action_id"][0, p]) == 352
    assert int(out["action_frame"][0, p]) == int(lock["ref_t1"]["action_frame"][0, p])
    assert int(out["animation_index"][0, p]) == int(lock["ref_t1"]["animation_index"][0, p]) == 306
    assert int(out["on_ground"][0, p]) == int(lock["ref_t1"]["on_ground"][0, p]) == 0
    assert int(out["instance_id"][0, p]) == int(lock["ref_t1"]["instance_id"][0, p])

    # Target-1 source transition stays exact.
    assert int(out["action_id"][1, p]) == int(lock["ref_t1"]["action_id"][1, p]) == 43
    assert int(out["action_frame"][1, p]) == int(lock["ref_t1"]["action_frame"][1, p]) == 0
    assert int(out["animation_index"][1, p]) == int(lock["ref_t1"]["animation_index"][1, p]) == 36
    assert int(out["on_ground"][1, p]) == int(lock["ref_t1"]["on_ground"][1, p]) == 1
    assert int(out["instance_id"][1, p]) == int(lock["ref_t1"]["instance_id"][1, p])

    # Improved target row.
    assert int(out["action_id"][target_i, p]) == int(target["ref_t1"]["action_id"][p]) == 43
    assert int(out["action_frame"][target_i, p]) == int(target["ref_t1"]["action_frame"][p]) == 1
    assert int(out["animation_index"][target_i, p]) == int(target["ref_t1"]["animation_index"][p]) == 36
    assert int(out["on_ground"][target_i, p]) == int(target["ref_t1"]["on_ground"][p]) == 1
    assert int(out["instance_id"][target_i, p]) == int(target["ref_t1"]["instance_id"][p])

    # Target+1 flank stays exact.
    assert int(out["action_id"][3, p]) == int(lock["ref_t1"]["action_id"][3, p]) == 43
    assert int(out["action_frame"][3, p]) == int(lock["ref_t1"]["action_frame"][3, p]) == 3
    assert int(out["animation_index"][3, p]) == int(lock["ref_t1"]["animation_index"][3, p]) == 36
    assert int(out["on_ground"][3, p]) == int(lock["ref_t1"]["on_ground"][3, p]) == 1
    assert int(out["instance_id"][3, p]) == int(lock["ref_t1"]["instance_id"][3, p])
