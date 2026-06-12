from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

LDG = "datasets/marth/replays/validation/marth/LoudDullGoat.msl"
IPW = "datasets/marth/replays/validation/marth/InternalPowerlessWallaby.msl"
FSP = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/"
    "FavorableSuperficialPig.msl"
)

ACT_THROW_HI = 0x00DD
ACT_THROWN_HI = 0x00F1
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_THROW_LW = 0x00DE
ACT_THROWN_LW = 0x00F2
ACT_DOWN_BOUND_U = 0x00B7


def _one_step(dataset_rel: str, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = binding.sizes()

    def _field_bytes(field: str, stride: int) -> np.ndarray:
        row = samples[record : record + 1]
        return np.frombuffer(row[field].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, stride)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, _field_bytes("seed_t", int(sizes["seed"])))
        binding.step_input(
            handle,
            _field_bytes("prev_input_t", int(sizes["input"])),
            _field_bytes("input_t", int(sizes["input"])),
        )
        out_bytes = np.empty((1, int(sizes["compare"])), dtype=np.uint8)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        return samples[record]["seed_t"].copy(), out, samples[record]["ref_t1"].copy()
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize("record", [383, 455, 821])
def test_marth_throwhi_release_edge_fires_on_one_ulp_boundary(record: int) -> None:
    # The source AObj accumulates the 1/(weight*x37C) throw rate in f32 and sits one ulp
    # BELOW the frame-12 set_throw_flags command on marth's ThrowHi (internal 11.999999),
    # while the Slippi-seeded timebase reports exactly 12.000. The release-edge check must
    # reconstruct the source prev (the D13 cumsum model) or released_prev reads "already
    # past frame 12" and the victim holds ThrownHi one frame past truth.
    # LDG recs: marth ThrowHi owner af 11 / anim 12.000, fox victim ThrownHi af 11 ->
    # truth releases to DamageFlyTop THIS step.
    # refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD4B0,ftCo_800DD724}
    seed, out, ref = _one_step(LDG, record)
    assert int(seed["action_id"][0]) == ACT_THROWN_HI
    assert int(seed["action_id"][1]) == ACT_THROW_HI
    assert int(seed["action_frame"][1]) == 11

    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_DAMAGE_FLY_TOP
    assert int(out["hitstun"][0]) == int(ref["hitstun"][0])
    assert int(out["last_hit_by"][0]) == int(ref["last_hit_by"][0])


@pytest.mark.integration
def test_marth_throwlw_upward_release_stays_airborne() -> None:
    # marth's ThrowLw launches UP: ftCo_800DDDE4 places the detached victim and the same
    # frame's collision pass only floor-contacts a non-rising sweep, so the victim leaves
    # the release spot airborne in DamageFlyTop. The release-local floor-contact ladder
    # previously grounded him into a DownBoundU the source never had. IPW rec 4382 p0
    # (marth ditto dthrow, release frame 14, rate 1.1494).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    seed, out, ref = _one_step(IPW, 4382)
    assert int(seed["action_id"][0]) == ACT_THROWN_LW
    assert int(seed["action_id"][1]) == ACT_THROW_LW

    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_DAMAGE_FLY_TOP
    assert int(out["on_ground"][0]) == int(ref["on_ground"][0]) == 0
    assert int(out["hitstun"][0]) == int(ref["hitstun"][0])


@pytest.mark.integration
def test_marth_throwhi_pre_boundary_row_does_not_release_early() -> None:
    # Adjacent non-trigger control for the 1-ulp release reconstruction: one frame BEFORE
    # the boundary (owner af 10, source cumsum 10.666666 -> 11.999999, still short of the
    # frame-12 command) the release must NOT fire. LDG rec 382: victim stays ThrownHi
    # af 11 exactly like ref.
    seed, out, ref = _one_step(LDG, 382)
    assert int(seed["action_id"][0]) == ACT_THROWN_HI
    assert int(seed["action_frame"][1]) == 10
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_THROWN_HI
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 11


@pytest.mark.integration
def test_spacie_downward_throw_release_floor_contact_unchanged() -> None:
    # Adjacent control for the upward-release airborne gate: a downward/flat spacie
    # ThrowLw release must still take the release-local floor-contact ladder (the slam ->
    # DownBound class the ladder was built for). FSP rec 9187 p1: victim ThrownLw af 32
    # -> DownBoundU grounded, matching ref.
    seed, out, ref = _one_step(FSP, 9187)
    assert int(seed["action_id"][1]) == ACT_THROWN_LW
    assert int(out["action_id"][1]) == int(ref["action_id"][1]) == ACT_DOWN_BOUND_U
    assert int(out["on_ground"][1]) == int(ref["on_ground"][1]) == 1
