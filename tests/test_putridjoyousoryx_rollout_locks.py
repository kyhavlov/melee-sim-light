from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


DATASET_REL = "datasets/aggregate_recent/replays/validation/aggregate_recent/PutridJoyousOryx.msl"
TBK_DATASET_REL = (
    "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
    "TreasuredBackKangaroo.msl"
)
BHH_DATASET_REL = "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _run_one_step(record: int, dataset_rel: str = DATASET_REL) -> tuple[np.void, np.void, np.void]:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride).copy()
        prev_input_bytes = (
            np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, input_stride)
            .copy()
        )
        input_bytes = (
            np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride).copy()
        )
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        seed = row["seed_t"].reshape(-1)[0].copy()
        ref = row["ref_t1"].reshape(-1)[0].copy()
        return seed, out, ref
    finally:
        binding.destroy(handle)


def test_jumpaerial_terminal_anim_runs_before_same_frame_attackair_pjo_1933() -> None:
    seed, out, ref = _run_one_step(1933)
    p = 0
    assert int(seed["action_id"][p]) == 27  # JumpAerialF
    assert int(seed["animation_index"][p]) == 18
    assert int(ref["action_id"][p]) == 67  # AttackAirB

    # Source order: JumpAerial_Anim enters FallAerial before the current-frame IASA chain can
    # consume AttackAir input.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{
    #   ftCo_JumpAerial_Anim,ftCo_JumpAerial_IASA}
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])


def test_jumpaerial_escapeair_rejects_stale_nonplatform_floor_projection_pjo_3338() -> None:
    seed, out, ref = _run_one_step(3338)
    p = 1
    assert int(seed["action_id"][p]) == 27  # JumpAerialF
    assert int(ref["action_id"][p]) == 236  # EscapeAir
    assert int(ref["on_ground"][p]) == 0

    # A restored/no-owner hard-floor projection is not enough for the first EscapeAir_Coll floor
    # hit; the source root/bottom producer is absent, so the row stays airborne.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    np.testing.assert_allclose(float(out["pos_y"][p]), float(ref["pos_y"][p]), atol=1e-6)


def test_jumpaerial_escapeair_keeps_valid_nonplatform_floor_publication_tbk_7158() -> None:
    seed, out, ref = _run_one_step(7158, TBK_DATASET_REL)
    p = 1
    assert int(seed["action_id"][p]) == 27  # JumpAerialF
    assert int(ref["action_id"][p]) == 43  # LandingFallSpecial
    assert int(ref["on_ground"][p]) == 1

    # Valid first EscapeAir_Coll floor publication is not the stale restored/no-owner shape guarded
    # by PJO 3338. The later JumpAerial frame has source floor/bottom evidence, so the hard-floor
    # landing must remain admitted.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    np.testing.assert_allclose(float(out["pos_y"][p]), float(ref["pos_y"][p]), atol=1e-6)


def test_jumpaerial_escapeair_keeps_shallow_diagonal_floor_publication_bhh_5517() -> None:
    seed, out, ref = _run_one_step(5517, BHH_DATASET_REL)
    p = 1
    assert int(seed["action_id"][p]) == 27  # JumpAerialF
    assert int(seed["action_frame"][p]) == 2
    assert int(ref["action_id"][p]) == 43  # LandingFallSpecial
    assert int(ref["on_ground"][p]) == 1

    # Same fresh JumpAerial -> EscapeAir frame shape as PJO 3338, but the air-dodge input is a
    # shallow diagonal rather than a down-held pass-through owner. Source EscapeAir_Coll publishes
    # this hard-floor handoff, so the PJO stale-projection guard must not suppress it.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
    #   ftCo_80099A9C,ftCo_EscapeAir_Coll}
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    np.testing.assert_allclose(float(out["pos_y"][p]), float(ref["pos_y"][p]), atol=1e-6)


@pytest.mark.parametrize(
    ("record", "expected_action", "expected_anim", "expected_on_ground"),
    [
        (4416, 90, 180, 0),
        (4420, 42, 35, 1),
        (7060, 27, 18, 0),
    ],
    ids=[
        "frame_start_hitstun_lock_stays_damagefly",
        "post_lockout_below_floor_stick_fall_lands",
        "frame_start_lock_current_jump_enters_jumpaerial",
    ],
)
def test_damagefly_iasa_frame_start_owner_boundaries_pjo(
    record: int, expected_action: int, expected_anim: int, expected_on_ground: int
) -> None:
    seed, out, ref = _run_one_step(record)
    p = 1
    assert int(seed["action_id"][p]) == 90  # DamageFlyTop

    # DamageFly_IASA owner split:
    # - frame-start x221C hitstun lockout blocks stale stick-Fall rows,
    # - current-root-below-floor rows with an x14 damage buffer observe held X before the
    #   current-frame x670 input timer increment,
    # - current jump input can consume the existing buffered JumpAerial path without unlocking
    #   attack/stick owners.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008F744,ftCo_DamageFly_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    assert int(ref["action_id"][p]) == expected_action
    assert int(ref["animation_index"][p]) == expected_anim
    assert int(ref["on_ground"][p]) == expected_on_ground
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p])
    np.testing.assert_allclose(float(out["pos_y"][p]), float(ref["pos_y"][p]), atol=1e-6)


def test_damagefly_frame_start_current_hit_owner_blocks_stale_fall_tbk_1556() -> None:
    seed, out, ref = _run_one_step(1556, TBK_DATASET_REL)
    p = 0
    assert int(seed["action_id"][p]) == 90  # DamageFlyTop
    assert int(seed["hitstun"][p]) == 1
    assert int(seed["last_hit_by"][p]) != 0
    assert int(ref["action_id"][p]) == 85  # DamageFlyHi from current ProcessHit

    # A frame-start DamageFly lock with a live current-hit source must not run the generic
    # DamageFall_IASA stick-Fall path before combat. The source-owned last_hit_by lane keeps the
    # current ProcessHit owner in charge of the destination damage state.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AC68
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
