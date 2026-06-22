from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp


SELFPLAY_181413_SLP = Path("replays/validation/aggregate_recent/Game_20260514T181413.slpz")
AGN_SLP = Path("replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz")
EWT_SLP = Path("replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slpz")
PTE_SLP = Path("replays/validation/fountain_of_dreams_recent/ParallelTemptingElk.slpz")
AGN_DATASET = Path("datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl")


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


def _rollout_segment_from_slp(
    slp_path: Path, *, start_record: int, record: int, ports: list[int]
) -> tuple[np.void, np.void, np.void]:
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    assert start_record <= record
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
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for j in range(start_record, record + 1):
            prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
            input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return samples["seed_t"][record], samples["ref_t1"][record], out[0].copy()


def _one_step_from_slp(slp_path: Path, *, record: int, ports: list[int]) -> tuple[np.void, np.void, np.void]:
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=ports,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    samples = ds.samples
    row = samples[record : record + 1]
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
    prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    )
    input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)
    return row["seed_t"][0], row["ref_t1"][0], out[0].copy()


def _one_step_sample(ds, row: np.ndarray) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, seed_stride
    ).copy()
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    ).copy()
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    ).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
    return out_compare_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()


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
def test_guardreflect_entry_bits_survive_same_frame_capture_pulled_lw() -> None:
    # ElatedWearyTermite rec 3602: p1 enters GuardReflect from AttackAirN in the input callback,
    # then p0's catch-connect callback installs CapturePulledLw in the same frame. Vanilla
    # preserves the GuardReflect x14/x18 timer bits (x221C_b1/b2 -> 0x60) while the later
    # Fighter_ChangeMotionState clears x221C_b3.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093A50,ftCo_80093BC0}
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
    seed, ref, out = _one_step_from_slp(EWT_SLP, record=3602, ports=[1, 2])
    p = 1
    assert int(seed["action_id"][p]) == 41  # AttackHi3
    assert int(ref["action_id"][p]) == int(out["action_id"][p]) == 226  # CapturePulledLw
    assert int(ref["state_flags"][p, 3]) == 0x60
    assert int(out["state_flags"][p, 3]) == int(ref["state_flags"][p, 3])


@pytest.mark.integration
def test_guardsetoff_runtime_hitlag_sdi_uses_live_shield_damage_owner() -> None:
    # ElatedWearyTermite rec 2958: p1 is in active GuardSetOff hitlag after a runtime shield hit.
    # Vanilla's ftColl shield contact writes x19A4 before ftCo_80092F2C installs ftCo_80093240;
    # the callback later consumes the live x19A4/input-timer window to apply grounded shield SDI.
    # A rollout that only seeds x19A4 from replay rows misses this free-running displacement.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_80093240}
    seed_before, ref_before, out_before = _rollout_segment_from_slp(
        EWT_SLP, start_record=2951, record=2957, ports=[1, 2]
    )
    p = 1
    assert int(seed_before["action_id"][p]) == int(ref_before["action_id"][p]) == 181
    assert int(out_before["action_id"][p]) == 181
    assert int(ref_before["hitlag"][p]) == int(out_before["hitlag"][p]) == 3
    assert float(out_before["pos_x"][p]) == pytest.approx(float(ref_before["pos_x"][p]), abs=1e-6)

    seed_sdi, ref_sdi, out_sdi = _rollout_segment_from_slp(
        EWT_SLP, start_record=2951, record=2958, ports=[1, 2]
    )
    assert int(seed_sdi["action_id"][p]) == int(ref_sdi["action_id"][p]) == 181
    assert int(out_sdi["action_id"][p]) == 181
    assert int(ref_sdi["hitlag"][p]) == int(out_sdi["hitlag"][p]) == 2
    assert float(out_sdi["pos_x"][p]) == pytest.approx(float(ref_sdi["pos_x"][p]), abs=1e-6)
    assert abs(float(out_sdi["pos_x"][p]) - float(seed_sdi["pos_x"][p])) > 3.0


@pytest.mark.integration
def test_cliff_option_end_wait_iasa_admits_fresh_lr_guardreflect() -> None:
    # ParallelTemptingElk rec 3606: grounded CliffClimbQuick animation ends through
    # ftCommon_8007D92C -> ft_8008A2BC -> Wait, then the same proc's Wait_IASA reaches
    # ftCo_80091A4C. A fresh R press with x672 inside the powershield window must enter
    # GuardReflect before the held-shield GuardOn branch.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Anim
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C
    # refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4}
    seed, ref, out = _one_step_from_slp(PTE_SLP, record=3606, ports=[1, 2])
    p = 1
    assert int(seed["action_id"][p]) == 255  # CliffClimbQuick
    assert int(ref["action_id"][p]) == int(out["action_id"][p]) == 182  # GuardReflect
    assert int(out["action_frame"][p]) == -1
    assert int(out["animation_index"][p]) == 0xFFFFFFFF


@pytest.mark.integration
def test_cliff_option_end_held_lr_without_fresh_press_stays_guardon() -> None:
    # Same source handoff as the positive lock, but with the L/R fresh-press edge removed. The
    # source ftCo_80091A4C ordering should then fall through to held-shield GuardOn instead of
    # entering GuardReflect from a stale x672 timer alone.
    if not PTE_SLP.exists():
        pytest.skip(f"missing local replay: {PTE_SLP}")
    ds = build_dataset_from_slp(
        slp_path=str(PTE_SLP),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    row = ds.samples[3606:3607].copy()
    p = 1
    assert int(row["seed_t"]["action_id"][0, p]) == 255  # CliffClimbQuick
    assert int(row["input_t"]["p"]["buttons"][0, p]) & 0x0020  # R held
    row["prev_input_t"]["p"]["buttons"][0, p] = row["input_t"]["p"]["buttons"][0, p]

    out = _one_step_sample(ds, row)
    assert int(out["action_id"][p]) == 178  # GuardOn
    assert int(out["action_frame"][p]) == -1
    assert int(out["animation_index"][p]) == 0xFFFFFFFF


@pytest.mark.integration
def test_landing_origin_guardon_uses_live_x672_for_followup_guardreflect() -> None:
    # AGN rec 1339 enters no-submotion GuardOn from Landing on the previous frame. Landing_IASA
    # publishes GuardOn/ShieldDesc state, but the follow-up GuardOn_IASA powershield gate consumes
    # the live x672 timer; it must not reuse the grounded-locomotion frame-start x672 bridge.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::*_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80093694}
    if not AGN_DATASET.exists():
        pytest.skip(f"missing local dataset: {AGN_DATASET}")
    ds = read_dataset(str(AGN_DATASET))
    row = ds.samples[1339:1340].copy()
    p = 1
    assert int(row["seed_t"]["action_id"][0, p]) == 178  # GuardOn
    assert int(row["seed_t"]["seed_prev_action_id"][0, p]) == 42  # Landing
    assert int(row["input_t"]["p"]["buttons"][0, p]) & 0x0020  # R pressed

    out = _one_step_sample(ds, row)
    assert int(out["action_id"][p]) == 178  # GuardOn
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])


@pytest.mark.integration
def test_guardreflect_x18_nonfinal_reject_does_not_false_hitshield() -> None:
    # AGN rec 115 is a no-submotion GuardReflect row with x14 already expired and x18 still above
    # the final tick. Source reflect ownership is gone, but the remaining powershield-damage timer
    # does not create a new ShieldDesc hit after the exact item matrix path rejects the laser.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
    seed, ref, out = _one_step_from_slp(AGN_SLP, record=115, ports=[1, 2])
    p = 1
    assert int(seed["action_id"][p]) == 182  # GuardReflect
    assert int(seed["guard_reflect_timer_x14"][p]) == 0
    assert int(seed["guard_reflect_timer_x18"][p]) > 1
    assert int(ref["action_id"][p]) == int(out["action_id"][p]) == 182
    assert int(out["hitlag"][p]) == 0
    assert float(out["shield_hp"][p]) == pytest.approx(float(ref["shield_hp"][p]), abs=5e-4)


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
    assert (int(out["state_flags"][p, 3]) & 0x60) == 0
