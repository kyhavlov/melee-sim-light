from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_LANDING = 42
ACT_ATTACK_AIR_LW = 69
ACT_ATTACK_AIR_B = 67
ACT_WAIT = 14
ACT_JUMP_F = 25
ACT_JUMP_AERIAL_F = 27


def _run_one_step(ds, record: int, *, seed_mutator=None) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    row = ds.samples[record : record + 1].copy()
    if seed_mutator is not None:
        seed_mutator(row["seed_t"])

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed(handle, row["seed_t"].view("u1").reshape(1, seed_stride).copy())
        binding.step_input(
            handle,
            row["prev_input_t"].view("u1").reshape(1, input_stride).copy(),
            row["input_t"].view("u1").reshape(1, input_stride).copy(),
        )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()


@pytest.mark.integration
def test_jumpaerial_attackair_entry_consumes_callback_local_ecb_for_landing() -> None:
    # Replay-real lock for JumpAerial -> AttackAirLw entry landing.
    #
    # Source owner:
    # - JumpAerial IASA enters AttackAir before the map callback.
    # - AttackAir_Coll routes through ft_80082C74 -> mpColl_800471F8.
    # - mpCollInterpolateECB promotes the carried JumpAerial CollData.ecb to prev_ecb before
    #   interpolating the entered AttackAir desired ECB.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80043754}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/dream_land_recent/FlippantEnchantedHorse.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 7100
    p = 1
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(ref["action_id"][p]) == ACT_LANDING
    assert int(ref["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1.0e-6)


@pytest.mark.integration
def test_jumpaerial_specialn_entry_consumes_callback_local_ecb_for_wait_landing() -> None:
    # Replay-real lock for JumpAerial -> SpecialAirNStart entry whose collision callback resolves
    # grounded Wait instead of publishing the airborne special-start row.
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNStart_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/LawfulInsistentMeerkat.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 603
    p = 1
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(ref["action_id"][p]) == ACT_WAIT

    out = _run_one_step(ds, record)
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1.0e-6)


@pytest.mark.integration
def test_jumpf_attackair_entry_does_not_borrow_jumpaerial_ecb_consumer() -> None:
    # Replay-real negative lock: fresh JumpF -> AttackAirB remains airborne. The retained consumer
    # is JumpAerial callback-local ECB lifetime, not a broad AttackAir landing gate.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/battlefield_recent/DelayedSuperbGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 8626
    p = 0
    seed = ds.samples[record]["seed_t"]
    ref = ds.samples[record]["ref_t1"]
    assert int(seed["action_id"][p]) == ACT_JUMP_F
    assert int(ref["action_id"][p]) == ACT_ATTACK_AIR_B
    assert int(ref["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1.0e-6)


@pytest.mark.integration
def test_mutated_jumpf_provenance_does_not_consume_jumpaerial_ecb_landing() -> None:
    # Synthetic provenance control using the Dream Land landing row above: changing only the
    # prefix-causal previous action owner from JumpAerialF to JumpF prevents the callback-local
    # JumpAerial ECB consumer from firing.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/dream_land_recent/FlippantEnchantedHorse.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 7100
    p = 1

    def mutate(seed: np.ndarray) -> None:
        seed["seed_prev_action_id"][0, p] = np.uint16(ACT_JUMP_F)
        seed["seed_prev_action_frame"][0, p] = np.int16(20)

    out = _run_one_step(ds, record, seed_mutator=mutate)
    assert int(out["action_id"][p]) == ACT_ATTACK_AIR_LW
    assert int(out["on_ground"][p]) == 0
    assert int(out["ground_id"][p]) == 4
