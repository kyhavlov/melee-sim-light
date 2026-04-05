from __future__ import annotations

import importlib
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
    expected_hitstun: int | None = None
    expected_hitlag: int | None = None
    expected_state_flags_0: int | None = None
    expected_state_flags_3: int | None = None
    expected_state_flags_4: int | None = None


def _step_case(c: _Case) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / c.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {c.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > c.record, f"dataset too short: record={c.record}"
    row = samples[c.record : c.record + 1]

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

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        ref = row["ref_t1"][0]

        if c.expected_hitstun is not None:
            got = int(out["hitstun"][c.p])
            assert got == c.expected_hitstun == int(ref["hitstun"][c.p]), c.note
        if c.expected_hitlag is not None:
            got = int(out["hitlag"][c.p])
            assert got == c.expected_hitlag == int(ref["hitlag"][c.p]), c.note
        if c.expected_state_flags_0 is not None:
            got = int(out["state_flags"][c.p, 0])
            assert got == c.expected_state_flags_0 == int(ref["state_flags"][c.p, 0]), c.note
        if c.expected_state_flags_3 is not None:
            got = int(out["state_flags"][c.p, 3])
            assert got == c.expected_state_flags_3 == int(ref["state_flags"][c.p, 3]), c.note
        if c.expected_state_flags_4 is not None:
            got = int(out["state_flags"][c.p, 4])
            assert got == c.expected_state_flags_4 == int(ref["state_flags"][c.p, 4]), c.note
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    "c",
    [
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
            ),
            record=734,
            p=0,
            note="GuardReflect -> KneeBend clears x221C_b1 while preserving powershield-active carry",
            expected_state_flags_3=32,
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
            ),
            record=1891,
            p=1,
            note="Rebirth early steady frame restores x221F_b0 camera-box visibility carry",
            expected_state_flags_4=128,
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
            ),
            record=2390,
            p=0,
            note="GuardReflect -> GuardSetOff clears the reflecting lane on the shieldstun destination",
            expected_state_flags_0=4,
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
            ),
            record=2391,
            p=0,
            note="Adjacent GuardSetOff hitlag row keeps the non-reflecting destination shape after entry",
            expected_state_flags_0=4,
            expected_state_flags_3=96,
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
            ),
            record=3599,
            p=1,
            note="DamageAir2 -> Landing consumes hitstun on landing entry",
            expected_hitstun=0,
            expected_state_flags_3=0,
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/GracefulAttachedTurtle.msl"
            ),
            record=3944,
            p=1,
            note="DeadUpStar phase-2 boundary raises x221F_b1 on the delayed dead-flow latch",
            expected_state_flags_4=64,
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
            ),
            record=4482,
            p=0,
            note="DeadUpStar -> Rebirth transition clears dead-flow carry before steady Rebirth visibility resumes",
            expected_state_flags_3=0,
            expected_state_flags_4=0,
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
                "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
            ),
            record=4021,
            p=1,
            note="DeadLeft -> Rebirth keeps the existing x221F_b0 carry on the transition row",
            expected_state_flags_3=0,
            expected_state_flags_4=128,
        ),
    ],
)
def test_phase1_guard_rebirth_damage_rows_are_replay_exact(c: _Case) -> None:
    # Decomp refs:
    # - GuardReflect jump-out: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardReflect_IASA
    #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_800CB024
    # - Rebirth camera-box flag: refs/melee/src/melee/ft/ft_0D31.c::ftCo_Rebirth_Cam
    #   refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
    # - DeadUpStar delayed phase boundary / Rebirth transition:
    #   refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_DeadUpStar_Anim
    #   refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_800D4FF4
    # - DamageAir landing handoff: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
    #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter
    _step_case(c)
