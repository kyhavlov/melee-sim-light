from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

IPW = "datasets/marth/replays/validation/marth/InternalPowerlessWallaby.msl"
AGG = (
    "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
    "AttachedGoodNaturedGuanaco.msl"
)

ACT_CAPTURE_WAIT_LW = 0x00E3
ACT_CAPTURE_DAMAGE_LW = 0x00E4
ACT_CATCH_WAIT = 0x00D8
ACT_CATCH_ATTACK = 0x00D9


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
def test_capture_damage_persists_past_owner_pummel_end_for_long_pummel() -> None:
    # ftCo_CaptureDamage*_Anim exits to CaptureWait* ONLY when its own animation ends
    # (fn_800DB790/fn_800DBAE4) - there is no owner-CatchAttack-ended linkage in source.
    # Marth's 24f pummel lands its grabbed hit later than fox/falco's frame-4 hit, so the
    # victim's 20f damage anim outlives the owner's pummel by 2 frames. IPW rec 684 p0:
    # the owner (marth, CatchAttack af23) ends this step, but the victim must STAY in
    # CaptureDamageLw (af 17 -> 18). The removed owner-ended yank returned the victim to
    # CaptureWait here, which was the #1 marth rollout breaker (impact 4019, freq 54).
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureDamageLw_Anim
    seed, out, ref = _one_step(IPW, 684)
    assert int(seed["action_id"][0]) == ACT_CAPTURE_DAMAGE_LW
    assert int(seed["action_id"][1]) == ACT_CATCH_ATTACK
    assert int(ref["action_id"][0]) == ACT_CAPTURE_DAMAGE_LW
    assert int(out["action_id"][0]) == ACT_CAPTURE_DAMAGE_LW
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 18


@pytest.mark.integration
def test_capture_damage_exits_by_own_anim_end_clamp() -> None:
    # The victim's CaptureDamage* AObj must END-CLAMP (non-looping), not wrap: the
    # ftAnim_IsFramesRemaining exit fires when cur_anim_frame reaches the end frame. A
    # forced capture-loop wrap erased the end crossing and the victim never left the
    # damage state by its own animation. IPW rec 686 p0: victim CaptureDamageLw af 19
    # (end 20) exits to CaptureWaitLw af 0 this step, with the owner already in CatchWait.
    # refs/melee/src/melee/ft/ftanim.c::ftAnim_IsFramesRemaining
    seed, out, ref = _one_step(IPW, 686)
    assert int(seed["action_id"][0]) == ACT_CAPTURE_DAMAGE_LW
    assert int(seed["action_id"][1]) == ACT_CATCH_WAIT
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_CAPTURE_WAIT_LW
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 0


@pytest.mark.integration
def test_spacie_capture_damage_co_termination_unchanged() -> None:
    # Adjacent invariant: fox/falco's frame-4 pummel hit makes the victim's 20f damage
    # anim co-terminate with the owner's 24f pummel, so the own-anim-end exit lands on
    # the same frame the old owner-ended yank fired. AGG rec 979 p0 (falco victim):
    # CaptureDamageLw af 19 -> CaptureWaitLw af 0 exactly as before.
    seed, out, ref = _one_step(AGG, 979)
    assert int(seed["action_id"][0]) == ACT_CAPTURE_DAMAGE_LW
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_CAPTURE_WAIT_LW
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 0
