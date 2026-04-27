from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _rollout_window(dataset_path: Path, start_record: int, length: int) -> list[tuple[np.void, np.void]]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) >= start_record + length

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(
            samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, seed_stride).copy()
        binding.reseed_seed(handle, seed_bytes)

        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        out: list[tuple[np.void, np.void]] = []
        for rec in range(start_record, start_record + length):
            prev_input_bytes = np.frombuffer(
                samples[rec : rec + 1]["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride).copy()
            input_bytes = np.frombuffer(
                samples[rec : rec + 1]["input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(1, input_stride).copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out_row = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            ref_row = samples[rec]["ref_t1"].copy()
            out.append((out_row, ref_row))
        return out
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_throwlw_frame25_slowest_rate_attachment_anchor_positive_prh() -> None:
    # Replay-real positive for the slowest supported weight-dependent ThrowLw frame-25 attached
    # callback phase. The old capture-anchor-only rollout stayed too low by >1.0 by record 5635 and
    # cascaded into an early blastzone death.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/debug/fd_mixed_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    rows = _rollout_window(dataset_path, start_record=5628, length=8)
    out, ref = rows[7]
    victim = 0

    assert int(out["action_id"][victim]) == 242  # ThrownLw
    assert int(ref["action_id"][victim]) == 242
    assert int(out["action_frame"][victim]) == int(ref["action_frame"][victim])
    assert abs(float(out["pos_x"][victim]) - float(ref["pos_x"][victim])) <= 0.01
    assert abs(float(out["pos_y"][victim]) - float(ref["pos_y"][victim])) <= 0.30


@pytest.mark.integration
def test_throwlw_frame25_faster_victim_rate_uses_shared_transn_anchor_qgd() -> None:
    # Replay-real positive for the faster Fox-victim ThrowLw rate. The attached callback owner is
    # not a slowest-rate-only frame-25 carveout: ftCo_800DB368 constrains victim XRotN to the
    # thrower's TransN2/capture anchor, and ftCo_800DE508 applies the static x1A70 offset to that
    # live joint for the shared ThrownLw attached window.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    rows = _rollout_window(dataset_path, start_record=8108, length=3)
    out, ref = rows[2]
    victim = 1

    assert int(out["action_id"][victim]) == 242  # ThrownLw
    assert int(ref["action_id"][victim]) == 242
    assert abs(float(out["pos_x"][victim]) - float(ref["pos_x"][victim])) <= 0.01
    assert abs(float(out["pos_y"][victim]) - float(ref["pos_y"][victim])) <= 0.002


@pytest.mark.integration
def test_throwlw_release_floor_contact_enters_downbound_fsp() -> None:
    # Replay-real positive for the low-throw release floor-contact handoff:
    # - ftCo_800DDDE4 applies the throw hit, places the released victim, then runs mpColl_800471F8.
    # - The resulting DamageFly floor contact reaches ftCo_80090184 -> DownBound in the same frame.
    # - The throw hitbox damage is the pre-created HitCapsule.damage, so later same-instance
    #   throw-laser stale queue writes must not retroactively stale the +1.0 release hit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_DamageFly_Coll,ftCo_80090184}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    rows = _rollout_window(dataset_path, start_record=9179, length=9)
    out, ref = rows[8]
    victim = 1

    assert int(out["action_id"][victim]) == int(ref["action_id"][victim]) == 183  # DownBoundU
    assert int(out["action_frame"][victim]) == int(ref["action_frame"][victim]) == 0
    assert int(out["on_ground"][victim]) == int(ref["on_ground"][victim]) == 1
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim]) == 0
    assert abs(float(out["percent"][victim]) - float(ref["percent"][victim])) <= 1.0e-5
    assert abs(float(out["pos_y"][victim]) - float(ref["pos_y"][victim])) <= 1.0e-5
