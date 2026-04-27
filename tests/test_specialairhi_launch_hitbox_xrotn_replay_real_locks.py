from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _dataset_path() -> Path:
    root = Path(__file__).resolve().parents[1]
    return (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
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
def test_specialairhi_launch_hitbox_xrotn_qgd_target_window_stays_replay_exact() -> None:
    # Replay-real lock for the recent-suite SpecialAirHi launch-contact family.
    #
    # Decomp:
    # - SpecialAirHi launch rotates FtPart_XRotN by `2*pi - rotateModel`.
    # - rotateModel is written from launch velocity / collision response.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Coll}
    #
    # Runtime scope:
    # - Only launch-hit hitboxes attached under FtPart_XRotN need this local-chain rotation.
    # - Victim hurtcaps already match replay-real rows without an extra correction.
    dataset_path = _dataset_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")

    # target-1 / target / target+1 around the first real recent-suite front door
    for record in (9388, 9389, 9390):
        row = samples[record : record + 1]
        got = _run_row(binding, row)
        np.testing.assert_array_equal(
            got["action_id"], row["ref_t1"]["action_id"][0], err_msg=f"record={record} action_id"
        )
        np.testing.assert_array_equal(
            got["hitlag"], row["ref_t1"]["hitlag"][0], err_msg=f"record={record} hitlag"
        )
        np.testing.assert_allclose(
            got["percent"], row["ref_t1"]["percent"][0], atol=1e-6, err_msg=f"record={record} percent"
        )
        np.testing.assert_allclose(
            got["pos_x"], row["ref_t1"]["pos_x"][0], atol=1e-6, err_msg=f"record={record} pos_x"
        )
        np.testing.assert_allclose(
            got["pos_y"], row["ref_t1"]["pos_y"][0], atol=1e-6, err_msg=f"record={record} pos_y"
        )


@pytest.mark.integration
def test_specialairhi_launch_hitbox_xrotn_qgd_target_precombat_hitbox_matches_probe() -> None:
    dataset_path = _dataset_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[9389:9390]
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

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

        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        hitboxes, count = binding.hitboxes_world_full(handle, 0, 0)

        assert int(count) == 1
        # Vanilla probe from reports/triage/qgd9389_live_probe.json frame 9267:
        # raw `pos` is stored as [z, y, x] in that legacy probe format. Radius
        # uses the extracted ftAction scale literal from GALE01, not exact 1/256.
        np.testing.assert_allclose(
            hitboxes[0][:4],
            np.asarray([76.5973663, 24.2701416, -1.0097059, 3.9997439], dtype=np.float32),
            atol=1e-5,
        )
    finally:
        binding.destroy(handle)
