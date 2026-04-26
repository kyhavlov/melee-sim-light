from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_rollout_row(ds_path: Path, start_record: int, target_record: int) -> tuple[np.void, np.void]:
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    assert 0 <= start_record <= target_record < int(samples.shape[0])

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = (
        np.frombuffer(samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, seed_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        for rec in range(start_record, target_record + 1):
            row = samples[rec : rec + 1]
            prev_input_bytes = (
                np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            input_bytes = (
                np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    ref = samples[target_record : target_record + 1]["ref_t1"][0]
    return ref, out


@pytest.mark.integration
def test_throwlw_frame25_post_hitlag_attached_callback_positive_lock() -> None:
    # Replay-real lock for the ThrowLw frame-25 post-hitlag callback phase:
    # - victim starts the frame in the final hitlag tick;
    # - Fighter_8006A1BC ends hitlag at proc prio 0;
    # - resumed ThrowLw Anim crosses the frame-25 set_throw_spawn_projectile command;
    # - the freshly spawned state1 laser applies attached-victim BODY hitlag in the same frame.
    #
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/debug/fd_mixed_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    rec = 5631
    owner_p = 1
    victim_p = 0
    sample = read_dataset(str(dataset_path)).samples[rec]
    seed = sample["seed_t"]
    assert int(seed["action_id"][owner_p]) == 222  # ThrowLw
    assert int(seed["action_id"][victim_p]) == 242  # ThrownLw
    assert int(seed["grab_owner_port"][victim_p]) == owner_p
    assert int(seed["hitlag"][victim_p]) == 1
    assert float(seed["anim_frame_f32"][owner_p]) == pytest.approx(23.7500019)
    assert float(seed["frame_speed_mul_f32"][owner_p]) == pytest.approx(0.0)
    assert int(seed["throw_pulse_crossed_prev_frame"][owner_p]) == 0
    assert int(seed["throw_command_pending_pulse_frame"][owner_p]) == 0

    _, ref_row, out_row = _run_one_step_row(dataset_path, rec, owner_p)
    for p in (owner_p, victim_p):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=p)
    assert int(out_row["hitlag"][owner_p]) == 3
    assert int(out_row["hitlag"][victim_p]) == 4
    assert float(out_row["percent"][victim_p]) == pytest.approx(float(ref_row["percent"][victim_p]))


@pytest.mark.integration
@pytest.mark.parametrize("rec", [442, 4093, 8110])
def test_throwlw_frame25_post_hitlag_qgd_phase_controls_do_not_hit_immediately(rec: int) -> None:
    # Adjacent phase controls: QGD frame 24.0 -> 25.x serializes the state1 article at t+1, but
    # vanilla does not apply attached BODY hitlag immediately. This prevents broad "any post-hitlag
    # current frame-25 pulse hits" authority.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        / "QuerulousGrandDinosaur.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    owner_p = 0
    victim_p = 1
    sample = read_dataset(str(dataset_path)).samples[rec]
    seed = sample["seed_t"]
    assert int(seed["action_id"][owner_p]) == 222  # ThrowLw
    assert int(seed["action_id"][victim_p]) == 242  # ThrownLw
    assert int(seed["grab_owner_port"][victim_p]) == owner_p
    assert int(seed["hitlag"][victim_p]) == 1
    assert float(seed["anim_frame_f32"][owner_p]) == pytest.approx(24.0000019)

    _, ref_row, out_row = _run_one_step_row(dataset_path, rec, owner_p)
    for p in (owner_p, victim_p):
        _assert_transition_lock_fields_match_ref(out_row=out_row, ref_row=ref_row, record=rec, p=p)
    assert int(out_row["hitlag"][owner_p]) == 0
    assert int(out_row["hitlag"][victim_p]) == 0


@pytest.mark.integration
def test_throwlw_frame25_post_hitlag_qgd_rollout_control_does_not_hit_immediately() -> None:
    # Rollout control for the same QGD family: without the source-rate predicate, rollout can arrive
    # at the frame-25 pulse one command phase behind the replay seed and falsely apply same-frame
    # BODY hitlag at rec 442.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        / "QuerulousGrandDinosaur.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ref_row, out_row = _run_rollout_row(dataset_path, start_record=355, target_record=442)
    owner_p = 0
    victim_p = 1
    assert int(ref_row["hitlag"][owner_p]) == 0
    assert int(ref_row["hitlag"][victim_p]) == 0
    assert int(out_row["hitlag"][owner_p]) == 0
    assert int(out_row["hitlag"][victim_p]) == 0
