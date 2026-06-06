from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_AGG = Path("datasets/aggregate_recent/replays/validation/aggregate_recent")
_IAT = _AGG / "ImpassionedAlarmedTarsier.msl"
_PPA = _AGG / "PriceyPartialAlbatross.msl"

ACT_LANDING_AIR_N = 70
ACT_FALL = 29
ACT_OTTOTTO = 245
SM_OTTOTTO = 210


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/bin/grnla.bin",
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/motion_state/owners/fox.bin",
        "data/motion_state/owners/falco.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _rollout_to_record(*, dataset_path: Path, start: int, target: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    assert int(ds.samples.shape[0]) > target, f"dataset too short for record={target}"
    samples = ds.samples
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = samples_u8[start : start + 1, seed_off : seed_off + seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, target + 1):
            prev_input_bytes = samples_u8[
                record : record + 1, prev_input_off : prev_input_off + input_stride
            ].copy()
            input_bytes = samples_u8[
                record : record + 1, input_off : input_off + input_stride
            ].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
        return out.copy(), samples["ref_t1"][target].copy()
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_landingairn_edge_collision_enters_ottotto_instead_of_fall() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _IAT
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_IAT}")

    out, ref = _rollout_to_record(dataset_path=dataset_path, start=5053, target=5059)
    p = 1
    assert int(ref["action_id"][p]) == ACT_OTTOTTO
    assert int(out["action_id"][p]) == ACT_OTTOTTO
    assert int(out["animation_index"][p]) == SM_OTTOTTO
    assert int(out["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-4)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-4)
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(0.0, abs=1e-6)
    assert float(out["speed_air_x_self"][p]) == pytest.approx(0.0, abs=1e-6)
    assert float(out["speed_y_self"][p]) == pytest.approx(0.0, abs=1e-6)


@pytest.mark.integration
def test_landingairn_not_at_edge_does_not_enter_ottotto_early() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _IAT
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_IAT}")

    out, ref = _rollout_to_record(dataset_path=dataset_path, start=5053, target=5056)
    p = 1
    assert int(ref["action_id"][p]) == ACT_LANDING_AIR_N
    assert int(out["action_id"][p]) == ACT_LANDING_AIR_N
    assert int(out["action_id"][p]) != ACT_OTTOTTO
    assert int(out["action_id"][p]) != ACT_FALL
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-4)


@pytest.mark.integration
def test_damage_grounded_floor_index_traverses_from_ledge_to_main_floor() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _IAT
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_IAT}")

    out, ref = _rollout_to_record(dataset_path=dataset_path, start=5052, target=5078)
    p = 1
    assert int(ref["action_id"][p]) == 78
    assert int(out["action_id"][p]) == 78
    assert int(ref["ground_id"][p]) == 1
    assert int(out["ground_id"][p]) == 1
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-4)


@pytest.mark.integration
def test_damagefly_floor_projection_uses_current_bottom_contact_ppa_5056() -> None:
    # PPA rollout lock for DamageFly floor publication:
    # current DamageFly collision may publish DownBound only when the loaded current ECB bottom
    # actually reaches the hard floor. The retained mpColl owner rejects stale/carried root-only
    # projection while preserving the real floor-contact row at rec5056.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _PPA
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_PPA}")

    out, ref = _rollout_to_record(dataset_path=dataset_path, start=5053, target=5056)
    p = 0
    assert int(ref["action_id"][p]) == 183  # DownBoundU
    assert int(out["action_id"][p]) == 183
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-4)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-4)
