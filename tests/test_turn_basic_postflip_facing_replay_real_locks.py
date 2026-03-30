from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int
    note: str


_CASES = (
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl"
        ),
        record=4527,
        p=1,
        note="basic Turn pre-target control stays unflipped at af=4",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl"
        ),
        record=4528,
        p=1,
        note="basic Turn first steady post-flip frame reconstructs facing",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl"
        ),
        record=4529,
        p=1,
        note="basic Turn target+1 control stays matched after the reconstructed frame",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl"
        ),
        record=4538,
        p=1,
        note="mirrored basic Turn pre-target control stays unflipped at af=4",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl"
        ),
        record=4539,
        p=1,
        note="mirrored basic Turn first steady post-flip frame reconstructs facing",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl"
        ),
        record=4540,
        p=1,
        note="mirrored basic Turn target+1 control stays matched after the reconstructed frame",
    ),
    _Case(
        dataset_rel=(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.msl"
        ),
        record=5744,
        p=0,
        note="smash-turn control (turn_x8!=0) must not inherit the basic-Turn reconstruction",
    ),
)


def _step_one_row(ds, row: np.ndarray) -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
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
        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).name}:rec{c.record}:p{c.p}")
def test_turn_basic_postflip_facing_replay_real_locks(case: _Case) -> None:
    # Replay-real locks for the basic-Turn steady post-flip facing lane:
    # - ftCo_Turn_Enter_Basic seeds mv.co.turn.x8=0.
    # - ftCo_Turn_Anim_Inner flips facing once frames_to_turn reaches 0.
    # - Smash-turn rows keep x8 ownership and must not inherit the basic-Turn reconstruction.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{
    #   ftCo_Turn_Enter,ftCo_Turn_Enter_Basic,ftCo_Turn_Anim_Inner,ftCo_Turn_Enter_Smash}
    # data/characters/{fox,falco}.json turn_frames
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > case.record, f"dataset too short for record={case.record}"

    row = samples[case.record : case.record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    p = int(case.p)

    assert int(seed["action_id"][p]) == 18, case.note  # Turn
    assert int(seed["turn_has_turned"][p]) == 1, case.note
    assert int(seed["turn_frames_to_turn"][p]) == 0, case.note
    assert int(seed["speed_ground_x_self"][p]) == 0, case.note

    out = _step_one_row(ds, row)[0]

    for field in ("action_id", "action_frame", "animation_index", "on_ground", "hitlag", "hitstun"):
        assert int(out[field][p]) == int(ref[field][p]), (
            f"{case.note}: field={field} expected={int(ref[field][p])} got={int(out[field][p])}"
        )

    assert int(out["facing"][p]) == int(ref["facing"][p]), (
        f"{case.note}: facing expected={int(ref['facing'][p])} got={int(out['facing'][p])}"
    )
