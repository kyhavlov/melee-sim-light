from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_ESCAPE_AIR = 236
ACT_LANDING_FALL_SPECIAL = 43
ACT_JUMP_AERIAL_F = 27
ACT_FALL = 29
ACT_JUMP_F = 25
ACT_ATTACK_AIR_F = 66
ACT_LANDING = 42
SM_ESCAPE_AIR = 44
SM_LANDING_FALL_SPECIAL = 36
SM_ATTACK_AIR_F = 69
SM_LANDING = 35


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
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl",
            2565,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            6243,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            2771,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "PhysicalElectricCapybara.msl",
            6328,
            1,
        ),
    ],
)
def test_locked_escapeair_platform_start_lifetime_enters_landing_fall_special(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real locks for platform-stage EscapeAir over soft platforms.
    #
    # Source owner:
    # - EscapeAir_Coll delegates through ft_80082C74 -> ft_80081D0C.
    # - mpColl_80043754 owns the callback-local substep result.
    # - With CollData_X130_Locked active, mpColl_80046904 can consume a platform floor result via
    #   mpColl_80044838_Floor(ignore_bottom=true), projecting from the fighter root while the locked
    #   bottom point is still zeroed.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_80043754,mpColl_80046904,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["animation_index"][p]) == SM_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in (
        "action_id",
        "animation_index",
        "action_frame",
        "on_ground",
        "ground_id",
        "jumps_left",
        "instance_id",
    ):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "CornyDelayedOkapi.msl",
            3936,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "PhysicalElectricCapybara.msl",
            4812,
            0,
        ),
    ],
)
def test_locked_escapeair_first_platform_crossing_stays_airborne(
    dataset_rel: str, record: int, p: int
) -> None:
    # Negative replay-real controls: the first post-physics root crossing under a soft platform does
    # not yet have the callback-local platform result lifetime consumed by the retained owner.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
def test_locked_escapeair_platform_consumer_does_not_broaden_fd_or_hard_floor() -> None:
    # Synthetic negative controls using a replay-real platform row:
    # - FD/cardinal has no soft platform line to consume.
    # - a hard-floor-only pose above Battlefield main floor is not admitted by the platform helper.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 2565
    p = 1

    def fd_mutation(seed: np.ndarray) -> None:
        seed["stage_id"][0] = np.uint32(32)
        seed["ground_id"][0, p] = np.uint16(0xFFFF)
        seed["pos_x"][0, p] = np.float32(0.0)
        seed["pos_y"][0, p] = np.float32(24.0)
        seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(0.0)
        seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(26.0)

    out_fd = _run_one_step(ds, record, seed_mutator=fd_mutation)
    assert int(out_fd["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_fd["on_ground"][p]) == 0

    def hard_floor_mutation(seed: np.ndarray) -> None:
        seed["stage_id"][0] = np.uint32(31)
        seed["ground_id"][0, p] = np.uint16(1)
        seed["pos_x"][0, p] = np.float32(0.0)
        seed["pos_y"][0, p] = np.float32(3.0)
        seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(0.0)
        seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(5.0)

    out_hard_floor = _run_one_step(ds, record, seed_mutator=hard_floor_mutation)
    assert int(out_hard_floor["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_hard_floor["on_ground"][p]) == 0


@pytest.mark.integration
def test_jumpaerial_escapeair_platform_entry_frame4_keeps_source_airborne() -> None:
    # Replay-real lock for the next JumpAerial -> EscapeAir platform entry residual after the
    # locked root-projection checkpoint. The source IASA can enter EscapeAir before the map
    # callback, but the frame-4 carried JumpAerial CollData lifetime still suppresses the same-frame
    # platform landing result.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    #   ftCo_80099A58,ftCo_EscapeAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80043754}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 958
    p = 0
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["seed_prev_action_frame"][p]) == 4
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
def test_jumpaerial_escapeair_platform_entry_frame4_gate_is_not_broadened() -> None:
    # Synthetic negatives for the retained frame-4 owner:
    # - later carried JumpAerial prefix age is not part of this source slice;
    # - JumpF provenance does not borrow the JumpAerial callback lifetime;
    # - FD and hard-floor mutations do not use the soft-platform suppression path.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "PhysicalElectricCapybara.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 958
    p = 0

    def prev_frame5(seed: np.ndarray) -> None:
        seed["seed_prev_action_frame"][0, p] = np.uint16(5)

    out_prev_frame5 = _run_one_step(ds, record, seed_mutator=prev_frame5)
    assert int(out_prev_frame5["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(out_prev_frame5["on_ground"][p]) == 1

    def jumpf_provenance(seed: np.ndarray) -> None:
        seed["seed_prev_action_id"][0, p] = np.uint16(25)

    out_jumpf = _run_one_step(ds, record, seed_mutator=jumpf_provenance)
    assert int(out_jumpf["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(out_jumpf["on_ground"][p]) == 1

    def fd_mutation(seed: np.ndarray) -> None:
        seed["stage_id"][0] = np.uint32(32)
        seed["ground_id"][0, p] = np.uint16(1)

    out_fd = _run_one_step(ds, record, seed_mutator=fd_mutation)
    assert int(out_fd["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_fd["on_ground"][p]) == 0

    def hard_floor_mutation(seed: np.ndarray) -> None:
        seed["ground_id"][0, p] = np.uint16(3)
        seed["pos_y"][0, p] = np.float32(2.0)
        seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(4.0)

    out_hard_floor = _run_one_step(ds, record, seed_mutator=hard_floor_mutation)
    assert int(out_hard_floor["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_hard_floor["on_ground"][p]) == 0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            4875,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            1581,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            10773,
            1,
        ),
    ],
)
def test_jumpaerial_escapeair_transformed_platform_remap_enters_landing_fall_special(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real locks for fresh JumpAerial -> EscapeAir over FoD transformed platforms. Source
    # IASA can enter EscapeAir before the map callback, and the same EscapeAir_Coll pass consumes
    # the callback-local transformed-platform floor result even when mpLib projection remaps the
    # accepted platform line.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    #   ftCo_80099A58,ftCo_EscapeAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["seed_t"]["seed_prev_action_id"][p]) == ACT_JUMP_AERIAL_F
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_jumpaerial_escapeair_transformed_platform_remap_gate_is_source_scoped() -> None:
    # Synthetic negatives using a retained FoD row:
    # - sustained EscapeAir provenance still keeps the transformed-platform remap guard;
    # - FD/cardinal hard floor behavior is not admitted by the transformed-platform owner.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 4875
    p = 1

    def sustained_escapeair_provenance(seed: np.ndarray) -> None:
        seed["seed_prev_action_id"][0, p] = np.uint16(ACT_ESCAPE_AIR)

    out_sustained = _run_one_step(ds, record, seed_mutator=sustained_escapeair_provenance)
    assert int(out_sustained["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_sustained["on_ground"][p]) == 0

    def fd_hard_floor_mutation(seed: np.ndarray) -> None:
        seed["stage_id"][0] = np.uint32(32)
        seed["ground_id"][0, p] = np.uint16(1)
        seed["pos_x"][0, p] = np.float32(0.0)
        seed["pos_y"][0, p] = np.float32(4.0)
        seed["floor_sweep_prev_pos_x_f32"][0, p] = np.float32(0.0)
        seed["floor_sweep_prev_pos_y_f32"][0, p] = np.float32(2.0)

    out_fd = _run_one_step(ds, record, seed_mutator=fd_hard_floor_mutation)
    assert int(out_fd["action_id"][p]) == ACT_ESCAPE_AIR
    assert int(out_fd["on_ground"][p]) == 0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "CheeryNumbMonkey.msl",
            680,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
            "LawfulInsistentMeerkat.msl",
            3654,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ParallelTemptingElk.msl",
            10926,
            0,
        ),
    ],
)
def test_fall_attackair_entry_platform_contact_stays_airborne(
    dataset_rel: str, record: int, p: int
) -> None:
    # Replay-real locks for Fall -> AttackAir IASA over platform contacts. The entry frame carries
    # the pre-entry CollData/ECB callback lifetime and must not be consumed as a generic Landing
    # before AttackAir_Coll owns a stable floor result.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::{
    #   ftCo_AttackAir_EnterFromMsid,ftCo_AttackAir_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(row["ref_t1"]["action_id"][p]) == ACT_ATTACK_AIR_F
    assert int(row["ref_t1"]["animation_index"][p]) == SM_ATTACK_AIR_F
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "ThisVioletRaccoon.msl",
            5886,
            0,
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
            "SweatyThisMallard.msl",
            6606,
            1,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "ElatedWearyTermite.msl",
            9375,
            0,
        ),
    ],
)
def test_jump_attackair_platform_entry_does_not_use_fall_owner(
    dataset_rel: str, record: int, p: int
) -> None:
    # Negative replay-real controls from the rejected broad Jump/JumpAerial AttackAir suppression:
    # Jump-family AttackAir entries have distinct locked-ECB/platform ownership and should still
    # land here.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == ACT_JUMP_F
    assert int(row["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(row["ref_t1"]["animation_index"][p]) == SM_LANDING
    assert int(row["ref_t1"]["on_ground"][p]) == 1

    out = _run_one_step(ds, record)
    ref = row["ref_t1"]
    for field in ("action_id", "animation_index", "action_frame", "on_ground", "ground_id"):
        assert int(out[field][p]) == int(ref[field][p]), field
