from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)


def _run_rollout_window(dataset_path: Path, start: int, stop: int):
    import msl_binding

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    def field_bytes(record: int, off: int, stride: int) -> np.ndarray:
        raw = samples[record : record + 1].view(np.uint8).reshape(1, -1)
        return np.array(raw[:, off : off + stride], dtype=np.uint8, order="C", copy=True)

    handle = msl_binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        msl_binding.reseed_seed(handle, field_bytes(start, seed_off, seed_stride))
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8, order="C")
        for record in range(int(start), int(stop) + 1):
            msl_binding.step_input(
                handle,
                field_bytes(record, prev_off, input_stride),
                field_bytes(record, input_off, input_stride),
            )
            msl_binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()
    finally:
        msl_binding.destroy(handle)

    return samples["ref_t1"][stop], out


@pytest.mark.integration
def test_guard_shielddesc_seed_accepts_replay_proven_guardsetoff_contact() -> None:
    # Replay-visible GuardSetOff + attacker/defender hitlag proves the hidden
    # `lbColl_80007BCC` ShieldDesc path accepted before BODY selection.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 0
    attacker = 1
    seed, ref, out = _run_one_step_row(dataset_path, 11352, defender)

    assert int(ref["action_id"][defender]) == 181
    assert int(out["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender])
    assert all(
        int(seed["combat_shield_contact_hb_kind"][attacker][hb][defender]) == 2 for hb in range(4)
    )


@pytest.mark.integration
def test_guard_shielddesc_seed_rejects_replay_proven_body_damage_contact() -> None:
    # Replay-visible BODY damage + attacker/defender hitlag proves the hidden ShieldDesc path did
    # not accept: `ftColl_80078C70` tests shield first, and accepted shield contact suppresses BODY.
    # This prevents near-rim Guard rows from becoming false GuardSetOff hits under one-step reseed.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    attacker = 0
    defender = 1
    seed, ref, out = _run_one_step_row(dataset_path, 3702, defender)

    assert int(seed["action_id"][defender]) == 179
    assert int(ref["action_id"][defender]) == 87
    assert int(out["action_id"][defender]) == 87
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender])
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender])
    assert all(
        int(seed["combat_shield_contact_hb_kind"][attacker][hb][defender]) == 1 for hb in range(4)
    )


@pytest.mark.integration
def test_guard_shielddesc_runtime_pose_accepts_prh_rollout_contact() -> None:
    # Runtime-positive boundary: no one-step ShieldDesc seed is available in this natural rollout
    # window. The decomp-shaped live Guard pose must still place Falco's ShieldDesc where
    # `lbColl_80007BCC` accepts the shield contact before BODY.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091E78
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 0
    ref, out = _run_rollout_window(dataset_path, 11336, 11352)

    assert int(ref["action_id"][defender]) == 181
    assert int(out["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender])
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]))


@pytest.mark.integration
def test_guard_shielddesc_runtime_pose_keeps_iat_body_negative() -> None:
    # Runtime-negative boundary: the same live Guard pose correction must not become a broad
    # shield-rim suppressor. This window was the false GuardSetOff control for the rejected broad
    # ShieldDesc extent experiment.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    defender = 1
    ref, out = _run_rollout_window(dataset_path, 3686, 3702)

    assert int(ref["action_id"][defender]) == 87
    assert int(out["action_id"][defender]) == 87
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender])
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender])
