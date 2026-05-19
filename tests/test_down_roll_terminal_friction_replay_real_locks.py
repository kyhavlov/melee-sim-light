from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_ATTACK_AIR_HI = 0x0044
ACT_LANDING_AIR_HI = 0x0049
ACT_DOWN_BACK_U = 0x00BD
ACT_MISS_FOOT = 0x00FB


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _run_one_step(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

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

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    return row["seed_t"].reshape(-1)[0].copy(), row["ref_t1"].reshape(-1)[0].copy(), out


def test_downback_terminal_missfoot_uses_single_destination_phys_pte_918() -> None:
    # DownBackU terminal ordering:
    # - ftCo_Down_Anim reaches ft_8008A2BC and enters Wait before Phys.
    # - The destination state's Phys runs once in Fighter_procUpdate before Wait_Coll/ft_80084104
    #   resolves the floor-edge loss into MissFoot.
    # - Applying ft_80084F3C once in Down_Anim and again in the generic Phys phase double-frictions
    #   the terminal row, moving Falco too far left before the FoD platform edge callback.
    #
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Anim
    # refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80084F3C,ft_80084104}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local FoD dataset")

    seed, ref, out = _run_one_step(dataset_path, 918)
    p = 1
    assert int(seed["action_id"][p]) == ACT_DOWN_BACK_U
    assert int(ref["action_id"][p]) == ACT_MISS_FOOT

    assert int(out["action_id"][p]) == ACT_MISS_FOOT
    assert float(out["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=1e-7
    )
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)


def test_downback_nonterminal_keeps_transn_phys_pte_917() -> None:
    # Negative/control for the terminal ordering above: while DownBackU still has animation frames
    # remaining, ftCo_Down_Phys owns TransN-derived roll velocity. The terminal single-Phys guard
    # must not suppress that ordinary down-roll Phys path.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local FoD dataset")

    seed, ref, out = _run_one_step(dataset_path, 917)
    p = 1
    assert int(seed["action_id"][p]) == ACT_DOWN_BACK_U
    assert int(ref["action_id"][p]) == ACT_DOWN_BACK_U

    assert int(out["action_id"][p]) == ACT_DOWN_BACK_U
    assert float(out["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=2e-6
    )
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)


def test_attackairhi_partner_state_stays_exact_on_downback_terminal_row_pte_918() -> None:
    # Boundary for the same row: the retained fix is only the terminal down-roll destination Phys
    # owner. The other fighter's AttackAirHi -> LandingAirHi handoff is not part of this slice.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip("missing local FoD dataset")

    seed, ref, out = _run_one_step(dataset_path, 918)
    p = 0
    assert int(seed["action_id"][p]) == ACT_ATTACK_AIR_HI
    assert int(ref["action_id"][p]) == ACT_LANDING_AIR_HI

    assert int(out["action_id"][p]) == ACT_LANDING_AIR_HI
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
