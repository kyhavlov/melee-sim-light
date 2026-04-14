from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _dataset_path() -> Path:
    root = Path(__file__).resolve().parents[1]
    return (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )


def _run_row(binding, row: np.ndarray) -> np.ndarray:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_specialairhi_hurtcaps_xrotn_agn_target_window_stays_replay_exact() -> None:
    # Replay-real lock for the recent-suite SpecialAirHi victim-geometry family.
    #
    # Decomp:
    # - SpecialAirHi launch rotates FtPart_XRotN by `2*pi - rotateModel`.
    # - rotateModel is written from launch velocity / collision response.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Coll}
    #
    # Runtime scope:
    # - Attacker AttackAirLw hitboxes already match the live probe on AGN:7263.
    # - The missing owner is victim hurtcaps on the SpecialAirHi launch family.
    dataset_path = _dataset_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")

    for record in (7262, 7263, 7264):
        row = samples[record : record + 1]
        got = _run_row(binding, row)
        np.testing.assert_array_equal(
            got["action_id"], row["ref_t1"]["action_id"][0], err_msg=f"record={record} action_id"
        )
        np.testing.assert_array_equal(
            got["hitlag"], row["ref_t1"]["hitlag"][0], err_msg=f"record={record} hitlag"
        )
        np.testing.assert_array_equal(
            got["hitstun"], row["ref_t1"]["hitstun"][0], err_msg=f"record={record} hitstun"
        )
        np.testing.assert_allclose(
            got["percent"], row["ref_t1"]["percent"][0], atol=1e-6, err_msg=f"record={record} percent"
        )
        np.testing.assert_array_equal(
            got["instance_hit_by"],
            row["ref_t1"]["instance_hit_by"][0],
            err_msg=f"record={record} instance_hit_by",
        )


@pytest.mark.integration
def test_specialairhi_hurtcaps_xrotn_adjacent_blockers_stay_separate() -> None:
    dataset_path = _dataset_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")

    # Keep the remaining family boundary explicit:
    # - AGN:5611 is still a separate SpecialAirHi victim row with a different attacker/action
    #   surface than the closed AGN:7263 launch family.
    for record, player, exp_action, exp_hitlag in (
        (5611, 1, 356, 0),
    ):
        row = samples[record : record + 1]
        got = _run_row(binding, row)
        assert int(got["action_id"][player]) == exp_action, f"record={record} action_id"
        assert int(got["hitlag"][player]) == exp_hitlag, f"record={record} hitlag"
        assert int(got["action_id"][player]) != int(row["ref_t1"]["action_id"][0, player]), (
            f"record={record} unexpectedly crossed into adjacent family"
        )
