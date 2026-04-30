from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_DAMAGE_FLY_HI = 0x0057
ACT_DAMAGE_FLY_N = 0x0058
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_DOWN_BOUND_U = 0x00B7
ACT_PASSIVE = 0x00C7


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/ecb/fox_bottom.bin",
        "data/ecb/falco_bottom.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _dataset_path(root: Path) -> Path:
    dataset_rel = (
        "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    return dataset_path


def _aggregate_dataset_path(root: Path, name: str) -> Path:
    dataset_rel = f"datasets/aggregate_recent/replays/validation/aggregate_recent/{name}.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    return dataset_path


def _binding_sizes():
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    return binding, int(sizes["seed"]), int(sizes["input"]), int(sizes["compare"])


def _run_one_step(
    dataset_path: Path, record: int, *, ucf_cardinals_1_0_enabled: bool = False
) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    binding, seed_stride, input_stride, compare_stride = _binding_sizes()
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(
        row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, input_stride)
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=ucf_cardinals_1_0_enabled,
        ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    return row["seed_t"].reshape(-1)[0].copy(), row["ref_t1"].reshape(-1)[0].copy(), out


def _run_rollout_records(
    dataset_path: Path,
    start_record: int,
    target_records: tuple[int, ...],
    *,
    ucf_cardinals_1_0_enabled: bool = False,
) -> dict[int, tuple[np.void, np.void]]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_max = max(target_records)
    targets = set(target_records)
    assert int(samples.shape[0]) > target_max

    binding, seed_stride, input_stride, compare_stride = _binding_sizes()
    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=ucf_cardinals_1_0_enabled,
        ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        out_by_record: dict[int, tuple[np.void, np.void]] = {}
        for record in range(start_record, target_max + 1):
            row = samples[record : record + 1]
            prev_input_bytes = np.frombuffer(
                row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).copy().reshape(1, input_stride)
            input_bytes = np.frombuffer(
                row["input_t"].tobytes(order="C"), dtype=np.uint8
            ).copy().reshape(1, input_stride)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            if record in targets:
                out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
                out_by_record[record] = (samples[record]["ref_t1"].copy(), out)
        return out_by_record
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_damageflyhi_rollout_uses_prior_sweep_root_for_floor_tech_handoff() -> None:
    # Replay-real lock for AGN rec=5196 -> rec=5208 p0:
    # - direct one-step at rec=5208 already lands into Passive because the replay seed carries the
    #   previous floor-sweep root.
    # - rollout must promote the prior frame's pre-physics root for the next floor sweep; using the
    #   current root as the sweep start misses the floor and stays in DamageFlyHi.
    # refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpCheckFloor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)

    p = 0
    seed, ref, out = _run_one_step(dataset_path, 5208)
    assert int(seed["action_id"][p]) == ACT_DAMAGE_FLY_HI
    assert int(seed["floor_sweep_prev_pos_valid_u8"][p]) == 1
    assert float(seed["floor_sweep_prev_pos_y_f32"][p]) == pytest.approx(-1.881044864654541)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_PASSIVE
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1

    by_record = _run_rollout_records(dataset_path, 5196, (5207, 5208))
    ref_5207, out_5207 = by_record[5207]
    assert int(out_5207["action_id"][p]) == int(ref_5207["action_id"][p]) == ACT_DAMAGE_FLY_HI
    assert int(out_5207["on_ground"][p]) == int(ref_5207["on_ground"][p]) == 0
    assert float(out_5207["pos_y"][p]) == pytest.approx(float(ref_5207["pos_y"][p]), abs=1e-6)

    ref_5208, out_5208 = by_record[5208]
    assert int(out_5208["action_id"][p]) == int(ref_5208["action_id"][p]) == ACT_PASSIVE
    assert int(out_5208["on_ground"][p]) == int(ref_5208["on_ground"][p]) == 1
    assert int(out_5208["hitstun"][p]) == int(ref_5208["hitstun"][p]) == 0
    assert float(out_5208["pos_y"][p]) == pytest.approx(float(ref_5208["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_ground_to_air_damage_entry_ecb_lock_survives_hitlag_for_downbound_handoff() -> None:
    # Replay-real lock for FSP rec=8343 -> 8349 p0:
    # - AttackLw3 hits a grounded victim and ftCo_8008DCE0 launches via ftCommon_8007D5D4.
    # - ftCommon_8007D5D4 sets CollData_X130_Locked / fp->ecb_lock=10; Fighter_procMap ticks it
    #   during the frozen hitlag frames.
    # - On the hitlag-exit frame, DamageFly_Coll still sees the locked-bottom floor callback and
    #   reaches the DownBoundU handoff. Without the runtime ECB-lock producer, rollout samples the
    #   pose bottom, misses floor contact, and falls through in DamageFlyN.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_DamageFly_Coll}
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    # refs/melee/src/melee/ft/fighter.c::Fighter_procMap
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _aggregate_dataset_path(root, "FavorableSuperficialPig")

    p = 0
    seed, ref, out = _run_one_step(dataset_path, 8349, ucf_cardinals_1_0_enabled=True)
    assert int(seed["action_id"][p]) == ACT_DAMAGE_FLY_N
    assert int(seed["hitlag"][p]) == 1
    assert int(seed["ecb_lock_timer"][p]) == 4
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DOWN_BOUND_U
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-6)

    by_record = _run_rollout_records(
        dataset_path, 8302, (8348, 8349), ucf_cardinals_1_0_enabled=True
    )
    ref_8348, out_8348 = by_record[8348]
    assert int(out_8348["action_id"][p]) == int(ref_8348["action_id"][p]) == ACT_DAMAGE_FLY_N
    assert int(out_8348["hitlag"][p]) == int(ref_8348["hitlag"][p]) == 1
    assert int(out_8348["on_ground"][p]) == int(ref_8348["on_ground"][p]) == 0
    assert float(out_8348["pos_y"][p]) == pytest.approx(float(ref_8348["pos_y"][p]), abs=2e-6)

    ref_8349, out_8349 = by_record[8349]
    assert int(out_8349["action_id"][p]) == int(ref_8349["action_id"][p]) == ACT_DOWN_BOUND_U
    assert int(out_8349["on_ground"][p]) == int(ref_8349["on_ground"][p]) == 1
    assert int(out_8349["hitstun"][p]) == int(ref_8349["hitstun"][p]) == 0
    assert float(out_8349["pos_x"][p]) == pytest.approx(float(ref_8349["pos_x"][p]), abs=2e-6)
    assert float(out_8349["pos_y"][p]) == pytest.approx(float(ref_8349["pos_y"][p]), abs=2e-6)


@pytest.mark.integration
def test_throwlw_release_damageflytop_uses_pose_bottom_for_next_floor_handoff() -> None:
    # Replay-real lock for PRH rec=5628 -> rec=5648 p0:
    # - ThrowLw release enters DamageFlyTop with an active ECB lock and a persisted floor.index.
    # - The first post-release DamageFly_Coll floor pass uses the live DamageFlyTop ECB bottom,
    #   not a root-locked bottom, so the downward sweep reaches ftCo_80090184 -> DownBoundU.
    # - This remains separate from active-hitlag ground-to-air damage rows, which keep the
    #   zero-bottom lock owner covered by the FSP control above.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_DamageFly_Coll,ftCo_80090184}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_LoadECB_JObj}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _aggregate_dataset_path(root, "PositiveRevolvingHyena")

    p = 0
    seed, ref, out = _run_one_step(dataset_path, 5648, ucf_cardinals_1_0_enabled=True)
    assert int(seed["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(seed["seed_prev_action_id"][p]) == 242  # ThrownLw
    assert int(seed["ecb_lock_timer"][p]) == 9
    assert int(seed["floor_sweep_prev_pos_valid_u8"][p]) == 1
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == ACT_DOWN_BOUND_U
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-6)
    assert float(out["speed_y_attack"][p]) == pytest.approx(float(ref["speed_y_attack"][p]))

    by_record = _run_rollout_records(
        dataset_path, 5628, (5647, 5648), ucf_cardinals_1_0_enabled=True
    )
    ref_5647, out_5647 = by_record[5647]
    assert int(out_5647["action_id"][p]) == int(ref_5647["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out_5647["on_ground"][p]) == int(ref_5647["on_ground"][p]) == 0
    assert float(out_5647["pos_y"][p]) == pytest.approx(float(ref_5647["pos_y"][p]), abs=1e-5)

    ref_5648, out_5648 = by_record[5648]
    assert int(out_5648["action_id"][p]) == int(ref_5648["action_id"][p]) == ACT_DOWN_BOUND_U
    assert int(out_5648["on_ground"][p]) == int(ref_5648["on_ground"][p]) == 1
    assert int(out_5648["hitstun"][p]) == int(ref_5648["hitstun"][p]) == 0
    assert float(out_5648["pos_x"][p]) == pytest.approx(float(ref_5648["pos_x"][p]), abs=2e-6)
    assert float(out_5648["pos_y"][p]) == pytest.approx(float(ref_5648["pos_y"][p]), abs=2e-6)
