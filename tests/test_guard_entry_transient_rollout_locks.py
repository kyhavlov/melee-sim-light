from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp


SELFPLAY_181413_SLP = Path("replays/validation/aggregate_recent/Game_20260514T181413.slpz")
AGN_SLP = Path("replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz")


def _rollout_until_from_slp(slp_path: Path, *, record: int, ports: list[int]) -> tuple[np.void, np.void, np.void]:
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=ports,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    samples = ds.samples
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(samples.shape[0], sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes[0, :] = samples_u8[0, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for j in range(record + 1):
            prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
            input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return samples["seed_t"][record], samples["ref_t1"][record], out[0].copy()


@pytest.mark.integration
def test_guardon_wait_entry_marker_does_not_stale_carry_into_later_spotdodge_rollout() -> None:
    # Game_20260514T181413 rec 1164 enters frozen no-submotion GuardOn through the grounded
    # Wait/Dash callback family. That entry marker spans only the immediate GuardOn/spotdodge
    # handoff window: it may suppress same-proc re-consume on the entry row and the first EscapeN
    # handoff, but must not still suppress ftCo_800925A4 shield drain when a later unrelated
    # GuardOn_IASA frame enters EscapeN.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_80091A4C,ftCo_GuardOn_Anim,ftCo_800925A4,ftCo_GuardOn_IASA}
    seed_entry, ref_entry, out_entry = _rollout_until_from_slp(SELFPLAY_181413_SLP, record=1164, ports=[1, 2])
    p = 0
    assert int(seed_entry["action_id"][p]) == 196
    assert int(ref_entry["action_id"][p]) == int(out_entry["action_id"][p]) == 178  # GuardOn
    assert float(out_entry["shield_hp"][p]) == pytest.approx(float(ref_entry["shield_hp"][p]), abs=1e-6)

    seed_escape, ref_escape, out_escape = _rollout_until_from_slp(
        SELFPLAY_181413_SLP, record=1169, ports=[1, 2]
    )
    assert int(seed_escape["action_id"][p]) == 178  # GuardOn
    assert int(ref_escape["action_id"][p]) == int(out_escape["action_id"][p]) == 235  # EscapeN
    assert float(out_escape["shield_hp"][p]) == pytest.approx(float(ref_escape["shield_hp"][p]), abs=1e-6)
    assert float(out_escape["shield_hp"][p]) < float(seed_escape["shield_hp"][p])


@pytest.mark.integration
def test_guardreflect_terminal_drain_uses_frame_start_lightshield_before_jump_oos() -> None:
    # Game_20260514T181413 rec 1912 is an expired no-submotion GuardReflect snapshot that drains
    # shield through ftCo_GuardReflect_Anim -> GuardOn_Anim, transitions to Guard, then later jumps
    # out of shield. The terminal drain uses the frame-start lightshield owner; the current analog
    # trigger refresh is carried to later Guard rows instead of reducing this row's drain.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_800925A4,ftCo_80093BC0,ftCo_GuardReflect_Anim,ftCo_GuardOn_Anim,ftCo_800928CC}
    seed_guard, ref_guard, out_guard = _rollout_until_from_slp(SELFPLAY_181413_SLP, record=1912, ports=[1, 2])
    p = 1
    assert int(seed_guard["action_id"][p]) == 182  # GuardReflect
    assert int(ref_guard["action_id"][p]) == int(out_guard["action_id"][p]) == 179  # Guard
    # The fixture is rebuilt from the source .slp, so analog-trigger float reconstruction can leave
    # a tiny carried residual. The source boundary locked here is the terminal GuardReflect drain
    # using the frame-start lightshield owner, eliminating the prior >0.09 HP stale-refresh drift.
    assert float(out_guard["shield_hp"][p]) == pytest.approx(float(ref_guard["shield_hp"][p]), abs=3e-3)

    _seed_jump, ref_jump, out_jump = _rollout_until_from_slp(SELFPLAY_181413_SLP, record=1917, ports=[1, 2])
    assert int(ref_jump["action_id"][p]) == int(out_jump["action_id"][p]) == 24  # KneeBend
    assert float(out_jump["shield_hp"][p]) == pytest.approx(float(ref_jump["shield_hp"][p]), abs=3e-3)


@pytest.mark.integration
def test_catch_connect_after_guard_recharges_once_after_exiting_shield_family() -> None:
    # Game_20260514T181413 rec 527 has victim Guard -> CapturePulledLw from catch-connect. Source
    # order runs Guard_Anim shield drain before fn_800DAADC exits the shield family; the late
    # Fighter_ProcessHit recharge gate then applies once on the capture entry row.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_UnkProcessGrab_8006CA5C,Fighter_ProcessHit_8006D1EC}
    # refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800DAADC
    seed, ref, out = _rollout_until_from_slp(SELFPLAY_181413_SLP, record=527, ports=[1, 2])
    p = 1
    assert int(seed["action_id"][p]) == 179  # Guard
    assert int(ref["action_id"][p]) == int(out["action_id"][p]) == 226  # CapturePulledLw
    assert float(out["shield_hp"][p]) == pytest.approx(float(ref["shield_hp"][p]), abs=1e-6)
    assert float(out["shield_hp"][p]) > float(seed["shield_hp"][p]) - 0.28
    assert float(out["shield_hp"][p]) < float(seed["shield_hp"][p])


@pytest.mark.integration
def test_nonshield_catch_connect_does_not_apply_extra_shield_family_recharge() -> None:
    # AGN rec 564 captures a non-shield victim into CapturePulledLw. It gets only the ordinary
    # inactive-shield recharge, proving the catch-connect shield-family branch is not applied to
    # non-shield pre-connect actions.
    seed, ref, out = _rollout_until_from_slp(AGN_SLP, record=564, ports=[1, 2])
    p = 1
    assert int(seed["action_id"][p]) == 43  # AttackAirF
    assert int(ref["action_id"][p]) == int(out["action_id"][p]) == 226  # CapturePulledLw
    assert float(out["shield_hp"][p]) == pytest.approx(float(ref["shield_hp"][p]), abs=1e-6)
    assert float(out["shield_hp"][p]) == pytest.approx(
        float(seed["shield_hp"][p]) + 0.07, abs=1e-5
    )
