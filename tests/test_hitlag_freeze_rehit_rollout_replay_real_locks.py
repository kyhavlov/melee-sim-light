from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class FreezeRehitCase:
    dataset_rel: str
    target_record: int
    attacker: int
    defender: int
    attacker_char: int
    defender_char: int
    attacker_action: int
    plus_overlap_persists: bool
    negative_record_delta: int


def _precombat_body_counts_after_one_step(dataset_path: Path, record: int) -> tuple[int, int]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record + 1, f"dataset too short for rollout lock: record={record}"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    row = samples[record : record + 1]
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    next_prev_input_bytes = np.frombuffer(samples[record]["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    next_input_bytes = np.frombuffer(samples[record + 1]["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.debug_step_input_pre_combat(handle, next_prev_input_bytes, next_input_bytes)
        _, select_count = binding.debug_combat_select_body_hits(handle, 0, 64)
        _, filtered_count = binding.debug_combat_contacts_filtered(handle, 0, 64)
        return int(select_count), int(filtered_count)
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        FreezeRehitCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.msl"
            ),
            target_record=5088,
            attacker=1,
            defender=0,
            attacker_char=22,
            defender_char=1,
            attacker_action=69,
            plus_overlap_persists=False,
            negative_record_delta=2,
        ),
        FreezeRehitCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "TreasuredBackKangaroo.msl"
            ),
            target_record=3907,
            attacker=0,
            defender=1,
            attacker_char=1,
            defender_char=22,
            attacker_action=67,
            plus_overlap_persists=True,
            negative_record_delta=7,
        ),
        FreezeRehitCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.msl"
            ),
            target_record=9911,
            attacker=1,
            defender=0,
            attacker_char=1,
            defender_char=22,
            attacker_action=65,
            plus_overlap_persists=False,
            negative_record_delta=2,
        ),
    ],
)
def test_hitlag_freeze_keeps_rehit_suppression_on_followup_frame(case: FreezeRehitCase) -> None:
    # Replay-real short-rollout lock for hitlag-frozen hitbox ownership.
    #
    # Decomp ownership:
    # - Fighter_8006A360 freezes anim/script/collision callbacks while hitlag is active.
    # - ftAction_8007121C owns hitbox create/clear writes; those events do not re-run on frozen
    #   hitlag frames.
    # - lbColl_8000ACFC consumes the per-hitbox victims_1 ring to suppress same-window re-hits.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    for rec in (
        case.target_record - 1,
        case.target_record,
        case.target_record + 1,
        case.target_record + case.negative_record_delta,
    ):
        assert int(samples.shape[0]) > rec + 1, f"dataset too short for rollout lock: record={rec}"

    target = samples[case.target_record]
    assert int(target["seed_t"]["char_id"][case.attacker]) == case.attacker_char
    assert int(target["seed_t"]["char_id"][case.defender]) == case.defender_char
    assert int(target["seed_t"]["action_id"][case.attacker]) == case.attacker_action
    assert float(target["ref_t1"]["percent"][case.defender]) > float(target["seed_t"]["percent"][case.defender])

    minus_select, minus_filtered = _precombat_body_counts_after_one_step(dataset_path, case.target_record - 1)
    target_select, target_filtered = _precombat_body_counts_after_one_step(dataset_path, case.target_record)
    plus_select, plus_filtered = _precombat_body_counts_after_one_step(dataset_path, case.target_record + 1)
    neg_select, neg_filtered = _precombat_body_counts_after_one_step(
        dataset_path, case.target_record + case.negative_record_delta
    )

    # target-1 control: the contact is still live before the first accepted hit.
    assert minus_select > 0
    assert minus_filtered >= minus_select

    # target: after the accepted hit, the next frozen-hitlag frame still has geometric overlap but
    # must not select a BODY hit again.
    assert target_filtered > 0
    assert target_select == 0

    # target+1 control: selection stays suppressed on the next replay frame even if geometric
    # overlap persists through hitlag.
    assert plus_select == 0
    if case.plus_overlap_persists:
        assert plus_filtered > 0
    else:
        assert plus_filtered == 0

    # explicit negative control: later row in the same local window has fully cleared.
    assert neg_select == 0
    assert neg_filtered == 0
