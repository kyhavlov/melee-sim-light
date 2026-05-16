from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, read_dataset


BUTTON_B = 0x0200
ACT_DAMAGE_FLY_TOP = 90
ACT_THROWN_HI = 241
ACT_FX_SPECIAL_AIR_LW_START = 365

_AGG_DAMAGEFLY_JUMP_BUFFER_DATASET = (
    "datasets/fox_falco_fd_ucf084_recent/replays/validation/"
    "cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
)
_THROWN_HI_RELEASE_DATASET = (
    "datasets/fox_falco_fd_ucf084_recent/replays/validation/"
    "cardinal_1.0_recent/TreasuredBackKangaroo.msl"
)


def _run_seed_with_input(
    dataset_path: Path,
    record: int,
    seed_t: np.ndarray,
    prev_input_t: np.ndarray,
    input_t: np.ndarray,
) -> np.void:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    assert int(ds.samples.shape[0]) > int(record)
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(prev_input_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(input_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


@pytest.mark.integration
def test_damagefly_hitstun_counter_blocks_shine_when_x221c_hitstun_bit_is_stale_clear() -> None:
    # A true DamageFly victim cannot run aerial special dispatch until hitstun ends:
    # - ftCo_DamageFly_IASA only delegates to DamageFall_IASA once !fp->x221C_b6.
    # - ftCo_8008DCE0 sets x221C_b6 with hitstun, and ftCo_8008F744 clears it when hitstun reaches 0.
    #
    # Replay/modelplay seeds can carry an impossible split where the explicit Slippi hitstun scalar is
    # still positive but the serialized fp+0x221C byte is stale/clear. The gameplay lock must follow
    # the hitstun owner, not grant SpecialAirLw from the stale flag byte.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_DamageFly_IASA,ftCo_8008DCE0,ftCo_8008F744}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _AGG_DAMAGEFLY_JUMP_BUFFER_DATASET
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_AGG_DAMAGEFLY_JUMP_BUFFER_DATASET}")

    ds = read_dataset(str(dataset_path))
    record = 1681
    p = 1
    row = ds.samples[record : record + 1]

    seed = row["seed_t"].copy()
    inp = row["input_t"].copy()
    prev = row["prev_input_t"].copy()
    assert int(seed["action_id"][0, p]) == ACT_DAMAGE_FLY_TOP
    assert int(seed["hitstun"][0, p]) == 1
    assert int(seed["hitlag"][0, p]) == 0
    assert int(seed["on_ground"][0, p]) == 0

    seed["state_flags"][0, p, 3] = np.uint8(int(seed["state_flags"][0, p, 3]) & ~0x02)
    prev["p"]["buttons"][0, p] = np.uint16(int(prev["p"]["buttons"][0, p]) & ~BUTTON_B)
    inp["p"]["buttons"][0, p] = np.uint16(int(inp["p"]["buttons"][0, p]) | BUTTON_B)
    inp["p"]["main_y"][0, p] = np.int8(-80)

    out = _run_seed_with_input(dataset_path, record, seed, prev, inp)
    assert int(out["action_id"][p]) != ACT_FX_SPECIAL_AIR_LW_START
    assert int(out["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out["hitstun"][p]) == 1


@pytest.mark.integration
def test_damagefly_hitstun_zero_stale_clear_boundary_still_allows_shine() -> None:
    # Boundary lock for the fix above: once hitstun is actually zero and x221C_b6 is clear,
    # DamageFly_IASA can reach DamageFall_IASA and aerial special dispatch.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _AGG_DAMAGEFLY_JUMP_BUFFER_DATASET
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_AGG_DAMAGEFLY_JUMP_BUFFER_DATASET}")

    ds = read_dataset(str(dataset_path))
    record = 1681
    p = 1
    row = ds.samples[record : record + 1]

    seed = row["seed_t"].copy()
    prev = np.zeros((1,), dtype=INPUT_DTYPE)
    inp = np.zeros((1,), dtype=INPUT_DTYPE)
    assert int(seed["action_id"][0, p]) == ACT_DAMAGE_FLY_TOP
    seed["hitstun"][0, p] = np.uint16(0)
    seed["state_flags"][0, p, 3] = np.uint8(int(seed["state_flags"][0, p, 3]) & ~0x02)
    inp["p"]["buttons"][0, p] = np.uint16(BUTTON_B)
    inp["p"]["main_y"][0, p] = np.int8(-80)

    out = _run_seed_with_input(dataset_path, record, seed, prev, inp)
    assert int(out["action_id"][p]) == ACT_FX_SPECIAL_AIR_LW_START


@pytest.mark.integration
def test_thrownhi_victim_input_cannot_enter_shine_before_throw_release_damage() -> None:
    # Direct up-throw-victim lock: ThrownHi IASA is empty, so B+down during the victim throw state
    # cannot enter SpecialAirLw. On the release frame, the throw owner transitions the victim into
    # DamageFlyTop with hitstun instead.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{
    #   ftCo_ThrownHi_IASA,ftCo_ThrownHi_Anim,ftCo_800DE5A4,ftCo_800DE7C0}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / _THROWN_HI_RELEASE_DATASET
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {_THROWN_HI_RELEASE_DATASET}")

    ds = read_dataset(str(dataset_path))
    record = 444
    p = 1
    row = ds.samples[record : record + 1]

    seed = row["seed_t"].copy()
    prev = row["prev_input_t"].copy()
    inp = row["input_t"].copy()
    assert int(seed["action_id"][0, p]) == ACT_THROWN_HI
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_DAMAGE_FLY_TOP
    assert int(row["ref_t1"]["hitstun"][0, p]) > 0

    prev["p"]["buttons"][0, p] = np.uint16(int(prev["p"]["buttons"][0, p]) & ~BUTTON_B)
    inp["p"]["buttons"][0, p] = np.uint16(int(inp["p"]["buttons"][0, p]) | BUTTON_B)
    inp["p"]["main_y"][0, p] = np.int8(-80)

    out = _run_seed_with_input(dataset_path, record, seed, prev, inp)
    assert int(out["action_id"][p]) == ACT_DAMAGE_FLY_TOP
    assert int(out["action_id"][p]) != ACT_FX_SPECIAL_AIR_LW_START
    assert int(out["hitstun"][p]) == int(row["ref_t1"]["hitstun"][0, p])
