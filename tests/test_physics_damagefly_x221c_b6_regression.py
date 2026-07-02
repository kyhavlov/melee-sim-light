from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _step_one_row(dataset_path: Path, record: int, p: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    assert int(row.shape[0]) == 1

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = (
            np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, seed_stride)
            .copy()
        )
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = (
            np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, input_stride)
            .copy()
        )
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        seed = row["seed_t"][0]
        ref = row["ref_t1"][0]
        return seed, out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz",
            576,
            1,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz",
            578,
            1,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            976,
            0,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.slpz",
            3082,
            1,
        ),
    ],
)
def test_damageflytop_x221c_b6_runtime_rows_keep_vertical_lane_parity(
    dataset_rel: str, record: int, p: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record, p)

    # Runtime ownership lock target:
    # - seed is DamageFlyTop with x221C_b6 set, so decomp Phys callback chooses ft_80084EEC.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Phys
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084EEC
    assert int(seed["action_id"][p]) == 90  # ftCo_MS_DamageFlyTop
    assert (int(seed["state_flags"][p, 3]) & 0x02) != 0  # x221C_b6 set
    assert int(seed["hitlag"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 90
    assert int(out["state_flags"][p, 3]) == int(ref["state_flags"][p, 3]) == 0x02

    # Strict parity lock: the vertical self-velocity lane is replay-exact on this branch.
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=2e-6)

    # Scope lock: this row family should stay on the tightened (~0.051) pos_y error band, not the
    # prior broad (~0.281) branch.
    assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 0.055


@pytest.mark.integration
def test_damageflyroll_adjacent_context_control_stays_in_expected_family() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    record = 2376
    p = 0
    seed, out, ref = _step_one_row(dataset_path, record, p)

    # Adjacent same-family context control:
    # - this is DamageFlyRoll with x221C_b6 set in the same local window as the strict lock rows.
    # - keep this as a context/scope guard only; do not lock in known mismatch magnitudes.
    assert int(seed["action_id"][p]) == 91  # ftCo_MS_DamageFlyRoll
    assert (int(seed["state_flags"][p, 3]) & 0x02) != 0  # x221C_b6 set
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 91

    # Scope guard only:
    # - keep airborne roll context
    # - keep numeric lanes finite
    # - do not enforce an error floor that would preserve incorrect behavior forever.
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert np.isfinite(float(out["speed_y_self"][p]))
    assert np.isfinite(float(ref["speed_y_self"][p]))


@pytest.mark.integration
def test_tbk_2378_context_guard_does_not_worsen_posy_regression_band() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    # Mixed runtime/reseed-sensitive lane in the local DamageFlyRoll->DownBound window.
    # Keep a non-worsening guard here without locking exact mismatch magnitude.
    record = 2378
    p = 0
    seed, out, ref = _step_one_row(dataset_path, record, p)

    assert int(seed["action_id"][p]) == 91  # ftCo_MS_DamageFlyRoll
    assert int(ref["action_id"][p]) == 191  # ftCo_MS_DownBoundD
    assert (int(seed["state_flags"][p, 3]) & 0x02) != 0  # x221C_b6 set

    # Row-baseline non-regression guard for this lane: keep this row in the improved
    # post-fix band and block drift back toward the historical ~8.49 regression.
    err = abs(float(out["pos_y"][p]) - float(ref["pos_y"][p]))
    assert err <= 8.311
