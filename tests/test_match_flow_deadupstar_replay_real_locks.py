from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


@dataclass(frozen=True)
class _DeadUpStarCase:
    dataset_rel: str
    record: int
    port: int
    note: str


_MAJ_DATASET = "replays/validation/aggregate_recent/MotionlessAggressiveJay.slpz"
_PTE_FOD_DATASET = (
    "replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.slpz"
)


def _dataset(root: Path, rel: str) -> Path:
    path = root / rel
    if not path.exists():
        pytest.skip(f"missing local replay: {rel}")
    return path


@pytest.mark.integration
def test_deadupstar_phase1_velocity_entry_replay_real_lock() -> None:
    # Top-blastzone DeadUpStar starts with a one-frame phase-0 timer. When it expires,
    # ftCo_DeadUpStar_Anim writes self_vel.y from p_ftCommonData->x514, Stage_GetCamBoundsTopOffset,
    # cur_pos.y, and x508 before Fighter_procUpdate integrates the fighter position.
    # refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_DeadUpStar_Anim
    # data/common/ft_common_data.json:{dead_up_star_phase1_frames,dead_up_star_phase1_cam_top_mul}
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset(root, _MAJ_DATASET)

    seed, ref, out = _run_one_step_row(dataset_path, 7937, 0)
    assert int(seed["action_id"][0]) == 4
    assert int(seed["match_flow_timer"][0]) == 176
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == 4
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 1
    assert float(out["speed_y_self"][0]) == pytest.approx(float(ref["speed_y_self"][0]), abs=1e-7)
    assert float(out["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)


@pytest.mark.integration
def test_deadupstar_phase1_velocity_not_recomputed_after_entry_control() -> None:
    # The phase-1 velocity write belongs to the phase-0 expiry boundary, not every DeadUpStar frame.
    # The next replay row is already in phase 1; recomputing from its lower Y would introduce a small
    # but rollout-visible drift.
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset(root, _MAJ_DATASET)

    seed, ref, out = _run_one_step_row(dataset_path, 7938, 0)
    assert int(seed["action_id"][0]) == 4
    assert int(seed["match_flow_timer"][0]) == 175
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == 4
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 2
    assert float(out["speed_y_self"][0]) == pytest.approx(float(ref["speed_y_self"][0]), abs=1e-7)
    assert float(out["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)


@pytest.mark.integration
def test_deadupstar_phase1_expiry_zeroes_velocity_replay_real_lock() -> None:
    # At phase-1 expiry, ftCo_DeadUpStar_Anim calls ftCommon_8007E2FC before the delayed stock-loss
    # side effects. The fighter should hold the top-blast pose instead of integrating the prior
    # phase-1 vertical velocity for one extra frame.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpStar_Anim
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E2FC
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset(root, _PTE_FOD_DATASET)

    seed, ref, out = _run_one_step_row(dataset_path, 8917, 1)
    assert int(seed["action_id"][1]) == 4
    assert int(seed["match_flow_timer"][1]) == 46
    assert int(out["action_id"][1]) == int(ref["action_id"][1]) == 4
    assert int(out["stocks"][1]) == int(ref["stocks"][1]) == 1
    assert float(out["speed_y_self"][1]) == pytest.approx(0.0, abs=1e-7)
    assert float(out["pos_y"][1]) == pytest.approx(float(ref["pos_y"][1]), abs=1e-6)


@pytest.mark.integration
def test_deadupstar_phase1_velocity_persists_before_expiry_replay_real_lock() -> None:
    # One frame earlier, phase 1 is still active. The expiry zero must not suppress the source-owned
    # phase-1 velocity while the countdown has not crossed into phase 2.
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset(root, _PTE_FOD_DATASET)

    seed, ref, out = _run_one_step_row(dataset_path, 8916, 1)
    assert int(seed["action_id"][1]) == 4
    assert int(seed["match_flow_timer"][1]) == 47
    assert int(out["action_id"][1]) == int(ref["action_id"][1]) == 4
    assert int(out["stocks"][1]) == int(ref["stocks"][1]) == 2
    assert float(out["speed_y_self"][1]) == pytest.approx(float(ref["speed_y_self"][1]), abs=1e-7)
    assert float(out["pos_y"][1]) == pytest.approx(float(ref["pos_y"][1]), abs=1e-6)


@pytest.mark.integration
def test_deadupstar_rollout_from_damageflytop_crosses_phase1_boundary() -> None:
    # Rollout starts before the top blastzone crossing so the DeadUpStar seed rows are not
    # teacher-forced. This locks the entry-owned phase velocity rather than a one-step seed carry.
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset(root, _MAJ_DATASET)
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > 7938

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(
        samples[7899:7900]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for rec in range(7899, 7939):
            row = samples[rec : rec + 1]
            prev_input_bytes = np.frombuffer(
                row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).copy().reshape(1, input_stride)
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            if rec in (7937, 7938):
                out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
                ref = samples[rec]["ref_t1"]
                assert int(out["action_id"][0]) == int(ref["action_id"][0]) == 4
                assert int(out["action_frame"][0]) == int(ref["action_frame"][0])
                assert float(out["speed_y_self"][0]) == pytest.approx(
                    float(ref["speed_y_self"][0]), abs=1e-7
                )
                assert float(out["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)
    finally:
        binding.destroy(handle)
