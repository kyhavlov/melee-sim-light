from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_DATASET = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
)
_BHH_DATASET = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
)
_MAJ_DATASET = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/MotionlessAggressiveJay.msl"
)
_MGS_DATASET = (
    "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.msl"
)
_MARTH_VSA_DATASET = "datasets/marth/replays/validation/marth/VictoriousSpitefulAlpaca.msl"
_MARTH_RWS_DATASET = "datasets/marth/replays/validation/marth/RipeWealthySeahorse.msl"
_MARTH_IPW_DATASET = "datasets/marth/replays/validation/marth/InternalPowerlessWallaby.msl"

_ACT_CATCH = 212
_ACT_CATCH_PULL = 213
_ACT_CATCH_DASH = 214
_ACT_CATCH_DASH_PULL = 215
_ACT_JUMP_F = 25
_ACT_FX_SPECIAL_LW_START = 360
_ACT_DAMAGE_FLY_TOP = 90
_ACT_CAPTURE_PULLED_HI = 223
_ACT_CAPTURE_PULLED_LW = 226
_STAGE_FINAL_DESTINATION = 32


def _run_rollout_row(ds_path: Path, *, start_record: int, target_record: int) -> np.void:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for rec in range(start_record, target_record + 1):
            prev_input_bytes[0, :] = samples_u8[rec, prev_input_off : prev_input_off + input_stride]
            input_bytes[0, :] = samples_u8[rec, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


@pytest.mark.integration
def test_catch_frame6_marginal_airborne_hurtcap_uses_collision_skeleton_scale() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _DATASET
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {_DATASET}")

    victim = 0
    catcher = 1
    seed, ref, out = _run_one_step_row(ds_path, 7851, victim)

    assert int(seed["action_id"][victim]) == _ACT_JUMP_F
    assert int(seed["action_id"][catcher]) == _ACT_CATCH
    assert int(seed["action_frame"][catcher]) == 5

    # This row is a narrow miss: applying Falco's character model_scaling to the catch hitbox
    # center makes hb0 barely overlap Fox's grabbable cap12. Vanilla catch selection consumes the
    # collision-skeleton HitCapsule point through lbColl_80007ECC, so the catch must remain a miss.
    assert int(ref["action_id"][victim]) == _ACT_JUMP_F
    assert int(ref["action_id"][catcher]) == _ACT_CATCH
    assert int(out["action_id"][victim]) == _ACT_JUMP_F
    assert int(out["action_id"][catcher]) == _ACT_CATCH
    assert int(out["instance_id"][victim]) == int(ref["instance_id"][victim])
    assert int(out["instance_id"][catcher]) == int(ref["instance_id"][catcher])


@pytest.mark.integration
def test_catch_collision_skeleton_scale_still_connects_nearby_positive() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _DATASET
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {_DATASET}")

    catcher = 0
    victim = 1
    seed, ref, out = _run_one_step_row(ds_path, 921, catcher)

    assert int(seed["action_id"][catcher]) == _ACT_CATCH
    assert int(seed["action_frame"][catcher]) == 5
    assert int(ref["action_id"][catcher]) == _ACT_CATCH_PULL
    assert int(ref["action_id"][victim]) == _ACT_CAPTURE_PULLED_HI
    assert int(out["action_id"][catcher]) == _ACT_CATCH_PULL
    assert int(out["action_id"][victim]) == _ACT_CAPTURE_PULLED_HI


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset", "miss_record", "connect_record"),
    [
        (_MARTH_VSA_DATASET, 6787, 6788),
        (_MARTH_RWS_DATASET, 1810, 1811),
    ],
)
def test_marth_root_authored_catch_keeps_source_reach_without_early_connect(
    dataset: str, miss_record: int, connect_record: int
) -> None:
    # Marth's Catch/CatchDash HitCapsules are authored on FtPart 0. The non-root
    # collision-skeleton scale counterfactual used by Falco's part-1 Catch miss must not shrink
    # these root-authored capsules: vanilla accepts the marginal connect on the next active row, but
    # the adjacent row stays a miss.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftColl_8007AD18}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC
    # data/moves/marth.json::ftCo_SM_Catch create_hitbox bone=0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / dataset
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {dataset}")

    catcher = 0
    victim = 1
    seed, ref, out = _run_one_step_row(ds_path, miss_record, catcher)
    assert int(seed["action_id"][catcher]) == _ACT_CATCH
    assert int(ref["action_id"][catcher]) == _ACT_CATCH
    assert int(out["action_id"][catcher]) == _ACT_CATCH
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim])

    seed, ref, out = _run_one_step_row(ds_path, connect_record, catcher)
    assert int(seed["action_id"][catcher]) == _ACT_CATCH
    assert int(ref["action_id"][catcher]) == _ACT_CATCH_PULL
    assert int(ref["action_id"][victim]) == _ACT_CAPTURE_PULLED_LW
    assert int(out["action_id"][catcher]) == _ACT_CATCH_PULL
    assert int(out["action_id"][victim]) == _ACT_CAPTURE_PULLED_LW
    assert int(out["instance_id"][catcher]) == int(ref["instance_id"][catcher])
    assert int(out["instance_id"][victim]) == int(ref["instance_id"][victim])


@pytest.mark.integration
@pytest.mark.xfail(
    strict=False,
    reason=(
        "Residual witness: IPW:7311 needs source/data proof for any root-authored CatchDash "
        "scale distinction before it can be a normal source-positive lock."
    ),
)
def test_marth_root_catchdash_scale_distinction_residual_witness() -> None:
    # Marth root-authored standing Catch and CatchDash both use part-0 HitCapsules in extracted
    # data. The runtime deliberately does not apply the non-root model-scale compensation to
    # CatchDash until a source owner proves that distinction. Keep IPW:7311 visible as residual
    # debt, while IPW:7737 remains the standing-Catch over-admission negative.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftColl_8007AD18}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC
    # data/moves/marth.json::{ftCo_SM_Catch,ftCo_SM_CatchDash} create_hitbox bone=0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _MARTH_IPW_DATASET
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {_MARTH_IPW_DATASET}")

    owner = 1
    victim = 0
    seed, ref, out = _run_one_step_row(ds_path, 7311, owner)
    assert int(seed["action_id"][owner]) == _ACT_CATCH_DASH
    assert int(ref["action_id"][owner]) == _ACT_CATCH_DASH_PULL
    assert int(ref["action_id"][victim]) == _ACT_CAPTURE_PULLED_LW
    assert int(out["action_id"][owner]) == _ACT_CATCH_DASH_PULL
    assert int(out["action_id"][victim]) == _ACT_CAPTURE_PULLED_LW

    owner = 1
    other = 0
    seed, ref, out = _run_one_step_row(ds_path, 7737, owner)
    assert int(seed["action_id"][owner]) == _ACT_CATCH
    assert int(seed["action_id"][other]) == _ACT_CATCH
    assert int(ref["action_id"][owner]) == _ACT_CATCH
    assert int(ref["action_id"][other]) == _ACT_CATCH
    assert int(out["action_id"][owner]) == _ACT_CATCH
    assert int(out["action_id"][other]) == _ACT_CATCH


@pytest.mark.integration
def test_catch_first_enable_hitcapsule_collapses_x58_for_live_rollout_mgs_1791() -> None:
    # Live first-enable Catch HitCapsule ownership:
    # - MGS rolls into Fox Catch frame 6 while Falco is platform-dropping in Pass.
    # - ftAction_8007121C creates the Catch capsules, then ftColl_8007AD18 refreshes the newly
    #   enabled HitCapsule and copies x58=x4C before ftColl_80078A2C tests grabbable hurtcaps.
    # - A rollout that starts before the Catch cannot rely on one-step seed bootstrap for x58; the
    #   live first-enable path must collapse the previous endpoint to the current capsule instead of
    #   leaving it disabled/zero.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_80078A2C}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _MGS_DATASET
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {_MGS_DATASET}")

    ds = read_dataset(str(ds_path))
    samples = ds.samples
    start = 1785
    target = 1791
    catcher = 0
    victim = 1
    assert int(samples[target]["seed_t"]["action_id"][catcher]) == _ACT_CATCH
    assert int(samples[target]["seed_t"]["action_frame"][catcher]) == 5
    assert int(samples[target]["seed_t"]["action_id"][victim]) == 244  # Pass.
    assert int(samples[target]["ref_t1"]["action_id"][catcher]) == _ACT_CATCH_PULL
    assert int(samples[target]["ref_t1"]["action_id"][victim]) == 226  # CapturePulledLw.

    out = _run_rollout_row(ds_path, start_record=start, target_record=target)
    ref = samples[target]["ref_t1"]
    assert int(out["action_id"][catcher]) == int(ref["action_id"][catcher]) == _ACT_CATCH_PULL
    assert int(out["action_frame"][catcher]) == int(ref["action_frame"][catcher])
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim]) == 226
    assert int(out["action_frame"][victim]) == int(ref["action_frame"][victim])
    assert float(out["pos_x"][catcher]) == pytest.approx(float(ref["pos_x"][catcher]), abs=2.0e-5)
    assert float(out["pos_x"][victim]) == pytest.approx(float(ref["pos_x"][victim]), abs=2.0e-5)


@pytest.mark.integration
def test_catch_wall_obstruction_blocks_wall_separated_grabbable_capsule() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / _DATASET
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {_DATASET}")

    catcher = 0
    victim = 1
    fd_right_wall_x = np.float32(85.5656967163086)

    def place_near_fd_right_wall(seed, *, y: float, cross_wall: bool) -> None:
        # Source-shaped synthetic lock for ft_80084CE4:
        # - FD's right wall includes a fighter-solid wall segment at x=85.5656967 from y=0 to -10.5.
        # - The low pair has the same catch/hurtcap overlap as the high pair, but the segment
        #   between the fighters' ECB midpoints intersects that wall and must reject CatchPull.
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
        # refs/melee/src/melee/ft/ft_081B.c::ft_80084CE4
        # refs/melee/src/melee/mp/mplib.c::{mpCheckLeftWall,mpCheckRightWall}
        # data/stages/bin/grnla.bin::MSLSTG01 segment i=9
        seed["stage_id"][0] = np.uint32(_STAGE_FINAL_DESTINATION)
        if cross_wall:
            x0 = fd_right_wall_x - np.float32(0.5)
            x1 = fd_right_wall_x + np.float32(0.5)
        else:
            x0 = fd_right_wall_x + np.float32(0.5)
            x1 = fd_right_wall_x + np.float32(1.5)
        seed["pos_x"][0, catcher] = x0
        seed["pos_x"][0, victim] = x1
        seed["pos_y"][0, catcher] = np.float32(y)
        seed["pos_y"][0, victim] = np.float32(y)
        seed["ground_id"][0, catcher] = np.uint16(0xFFFF)
        seed["ground_id"][0, victim] = np.uint16(0xFFFF)
        seed["on_ground"][0, catcher] = np.uint8(0)
        seed["on_ground"][0, victim] = np.uint8(0)

    seed, _ref, out = _run_one_step_row(
        ds_path,
        921,
        catcher,
        seed_mutator=lambda seed_t: place_near_fd_right_wall(seed_t, y=-8.0, cross_wall=True),
    )
    assert int(seed["action_id"][catcher]) == _ACT_CATCH
    assert int(seed["action_id"][victim]) == _ACT_DAMAGE_FLY_TOP
    assert int(out["action_id"][catcher]) == _ACT_CATCH
    assert int(out["action_id"][victim]) == _ACT_DAMAGE_FLY_TOP

    _seed, _ref, out = _run_one_step_row(
        ds_path,
        921,
        catcher,
        seed_mutator=lambda seed_t: place_near_fd_right_wall(seed_t, y=5.0, cross_wall=True),
    )
    assert int(out["action_id"][catcher]) == _ACT_CATCH_PULL
    assert int(out["action_id"][victim]) == _ACT_CAPTURE_PULLED_HI

    _seed, _ref, out = _run_one_step_row(
        ds_path,
        921,
        catcher,
        seed_mutator=lambda seed_t: place_near_fd_right_wall(seed_t, y=-8.0, cross_wall=False),
    )
    assert int(out["action_id"][catcher]) == _ACT_CATCH_PULL
    assert int(out["action_id"][victim]) == _ACT_CAPTURE_PULLED_HI


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset", "record", "catcher", "defender", "defender_action"),
    [
        (_BHH_DATASET, 3377, 0, 1, _ACT_FX_SPECIAL_LW_START),
        (_MAJ_DATASET, 9428, 1, 0, _ACT_DAMAGE_FLY_TOP),
    ],
)
def test_catch_collision_skeleton_scale_does_not_extend_small_model_reach(
    dataset: str, record: int, catcher: int, defender: int, defender_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / dataset
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {dataset}")

    seed, ref, out = _run_one_step_row(ds_path, record, catcher)

    assert int(seed["action_id"][catcher]) == _ACT_CATCH
    assert int(seed["action_frame"][catcher]) == 5
    assert int(seed["action_id"][defender]) == defender_action

    # Fox's model_scaling is below 1.0. A broad inverse-scale catch correction would extend the
    # catch bubble and create false CatchPull/CapturePulledHi connects on these rows.
    assert int(ref["action_id"][catcher]) == _ACT_CATCH
    assert int(ref["action_id"][defender]) == defender_action
    assert int(out["action_id"][catcher]) == _ACT_CATCH
    assert int(out["action_id"][defender]) == defender_action
    assert int(out["instance_id"][catcher]) == int(ref["instance_id"][catcher])
    assert int(out["instance_id"][defender]) == int(ref["instance_id"][defender])
