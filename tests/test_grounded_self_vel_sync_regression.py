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


def _step_one_row(dataset_path: Path, record: int, p: int) -> tuple[np.void, np.void]:
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
        ref = row["ref_t1"][0]
        return out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz",
            1799,
            0,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            586,
            1,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz",
            1428,
            0,
        ),
    ],
)
def test_grounded_rows_sync_self_vel_x_lane_to_ref(dataset_rel: str, record: int, p: int) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    out, ref = _step_one_row(dataset_path, record, p)
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["action_id"][p]) == int(ref["action_id"][p])

    got_air = float(out["speed_air_x_self"][p])
    got_ground = float(out["speed_ground_x_self"][p])
    ref_air = float(ref["speed_air_x_self"][p])
    ref_ground = float(ref["speed_ground_x_self"][p])

    # Decomp parity lock:
    # grounded phys callbacks route through ftCommon_ApplyGroundMovement, which writes self_vel.x
    # from gr_vel each frame after ground accel/friction resolution.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyGroundMovement
    assert got_air == pytest.approx(got_ground, abs=2e-6)
    assert got_air == pytest.approx(ref_air, abs=2e-6)
    assert got_ground == pytest.approx(ref_ground, abs=2e-6)


@pytest.mark.integration
def test_dash_entry_xe8_updates_gr_vel_after_self_vel_copy() -> None:
    # Dash entry uses ftCo_Dash_Enter -> ftCommon_800804A0 to queue xE8_ground_accel_2.
    # Fighter_procUpdate applies xE8 after Dash_Phys has called ftCommon_ApplyGroundMovement, so
    # same-frame self_vel.x / position still use the old gr_vel while post-frame gr_vel is updated.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{ftCo_Dash_Enter,ftCo_Dash_Phys}
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_800804A0,ftCommon_ApplyGroundMovement}
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    record = 397
    p = 0
    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[record]["seed_t"]
    assert int(seed["action_id"][p]) != 20
    assert int(ds.rows[record]["ref_t1"]["action_id"][p]) == 20  # Dash

    out, ref = _step_one_row(dataset_path, record, p)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 20
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["speed_air_x_self"][p]) == pytest.approx(
        float(ref["speed_air_x_self"][p]), abs=2e-6
    )
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=2e-6
    )
    assert float(out["speed_air_x_self"][p]) != pytest.approx(
        float(out["speed_ground_x_self"][p]), abs=1e-3
    )


@pytest.mark.integration
def test_airborne_negative_control_does_not_get_ground_sync() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    record = 2691
    p = 0
    out, ref = _step_one_row(dataset_path, record, p)

    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 223

    # Scope guard:
    # - this row is airborne, so the grounded self-velocity sync invariant must not be hard-required
    #   by this test.
    # - if future runtime fixes naturally align air/ground speed here, allow it as long as replay
    #   context agrees.
    got_air = float(out["speed_air_x_self"][p])
    got_ground = float(out["speed_ground_x_self"][p])
    ref_air = float(ref["speed_air_x_self"][p])
    ref_ground = float(ref["speed_ground_x_self"][p])

    assert np.isfinite(got_air)
    assert np.isfinite(got_ground)
    if got_air == pytest.approx(got_ground, abs=2e-6):
        assert ref_air == pytest.approx(ref_ground, abs=2e-6)
