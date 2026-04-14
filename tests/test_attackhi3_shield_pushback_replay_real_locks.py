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


def _run_one_step_row(binding, row: np.ndarray) -> np.ndarray:
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


def _run_rollout_window(binding, samples: np.ndarray, start_record: int, end_record: int) -> dict[int, np.ndarray]:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=2)
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    outs: dict[int, np.ndarray] = {}
    try:
        start_row = samples[start_record : start_record + 1]
        seed_bytes = np.frombuffer(start_row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        binding.reseed_seed(handle, seed_bytes)

        for rec in range(start_record, end_record):
            row = samples[rec : rec + 1]
            prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_bytes)
            outs[rec] = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)
    return outs


def _precombat_contact_counts(binding, row: np.ndarray) -> tuple[int, int, int]:
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
        _, classified = binding.debug_combat_contacts_classified(handle, 0, 64)
        _, filtered = binding.debug_combat_contacts_classified_filtered(handle, 0, 64)
        _, selected = binding.debug_combat_select_body_hits(handle, 0, 64)
        return int(classified), int(filtered), int(selected)
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_attackhi3_shield_pushback_seed_and_exit_rows_stay_replay_exact() -> None:
    # Replay-real lock for grounded attacker shield pushback after Falco utilt hits shield.
    #
    # Decomp:
    # - ftColl_80076CBC stores the grounded attacker shield-hit internals (`x1928`, `x192C`).
    # - Fighter_ProcessHit_8006D1EC shapes `xF4_ground_attacker_shield_kb_vel`.
    # - Fighter_procUpdate decays that scalar through ftCommon_8007CE4C and projects it via
    #   `x98_atk_shield_kb`, which is then added to position after self/KB velocity.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_procUpdate}
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007CE4C,ftCommon_8007E2A4}
    dataset_path = _dataset_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")

    expected_kb = {
        5129: -0.58,
        5130: -0.58,
        5131: -0.58,
        5132: -0.58,
        5133: -0.58,
        5134: -0.492,
        5135: -0.404,
        5136: -0.316,
        5137: -0.228,
        5138: -0.14,
    }
    for record, expected in expected_kb.items():
        got = float(samples[record]["seed_t"]["attacker_shield_ground_kb_vel"][0])
        assert got == pytest.approx(expected, abs=1e-6), f"record={record}"

    for record in range(5133, 5140):
        row = samples[record : record + 1]
        got = _run_one_step_row(binding, row)
        ref = row["ref_t1"][0]
        np.testing.assert_array_equal(got["action_id"], ref["action_id"], err_msg=f"record={record} action_id")
        np.testing.assert_array_equal(got["hitlag"], ref["hitlag"], err_msg=f"record={record} hitlag")
        np.testing.assert_allclose(got["pos_x"], ref["pos_x"], atol=1e-6, err_msg=f"record={record} pos_x")
        np.testing.assert_allclose(got["pos_y"], ref["pos_y"], atol=1e-6, err_msg=f"record={record} pos_y")


@pytest.mark.integration
def test_attackhi3_shield_pushback_rollout_closes_agn_5167_front_door() -> None:
    dataset_path = _dataset_path()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")

    # Same local window, adjacent owner:
    # - utilt shield pushback drift previously pulled the victim into an early aerial BODY hit.
    # - AttackAir entry uses an immediate ftAnim tick in decomp; pre-combat hurtcaps must sample
    #   that post-enter pose so the first AttackAirN frame remains miss-only, then the hit lands
    #   one frame later.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_EnterFromMsid
    # refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
    counts_5167 = _precombat_contact_counts(binding, samples[5167 : 5168])
    counts_5168 = _precombat_contact_counts(binding, samples[5168 : 5169])
    assert counts_5167 == (0, 0, 0)
    assert counts_5168 == (2, 2, 1)

    outs = _run_rollout_window(binding, samples, 5128, 5170)
    for record in (5167, 5168, 5169):
        out = outs[record]
        ref = samples[record]["ref_t1"]
        np.testing.assert_array_equal(out["action_id"], ref["action_id"], err_msg=f"record={record} action_id")
        np.testing.assert_array_equal(out["hitlag"], ref["hitlag"], err_msg=f"record={record} hitlag")
        np.testing.assert_allclose(out["percent"], ref["percent"], atol=1e-6, err_msg=f"record={record} percent")
        np.testing.assert_allclose(out["pos_x"], ref["pos_x"], atol=1e-6, err_msg=f"record={record} pos_x")
        np.testing.assert_allclose(out["pos_y"], ref["pos_y"], atol=1e-6, err_msg=f"record={record} pos_y")
