from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        ).copy()
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        ref = row["ref_t1"][0]
        return out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_attackairlw_same_port_stale_owner_rehit_lands_on_replay_frame() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        / "AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))

    # Replay-real timing lock for the AttackAirLw stale-owner bridge:
    # - Falco Dair keeps same-group hitboxes live across the active window while hitlist ownership
    #   is rewired on create/copy lanes in ftAction_8007121C / ftColl_800768A0.
    # - Teacher-forced reseed only carries dense victim presence + instance_hit_by, so same-port
    #   stale suppression must not fire a frame early on the continuing DamageFlyTop victim.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
    cases = [
        (7047, 1, 90, 0, 8, [192, 0, 0, 2, 0]),
        (7048, 1, 86, 7, 28, [192, 48, 0, 2, 0]),
        (7049, 1, 86, 6, 28, [192, 48, 0, 2, 0]),
    ]

    for record, p, exp_action, exp_hitlag, exp_hitstun, exp_flags in cases:
        assert int(ds.samples.shape[0]) > record, f"dataset too short for rec={record}"
        out, ref = _step_one_row(dataset_path, record)

        assert int(ref["action_id"][p]) == exp_action
        assert int(ref["hitlag"][p]) == exp_hitlag
        assert int(ref["hitstun"][p]) == exp_hitstun
        assert [int(x) for x in ref["state_flags"][p]] == exp_flags

        assert int(out["action_id"][p]) == exp_action
        assert int(out["hitlag"][p]) == exp_hitlag
        assert int(out["hitstun"][p]) == exp_hitstun
        assert [int(x) for x in out["state_flags"][p]] == exp_flags
