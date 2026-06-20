from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_THROW_LW = 222
ACT_THROWN_LW = 242
ACT_DAMAGE_FLY_TOP = 90
ACT_DOWN_BOUND_U = 183
STATE_FLAG_221C_B0 = 0x80


def _field_bytes(samples, record: int, field: str, stride: int) -> np.ndarray:
    off = int(samples.dtype.fields[field][1])
    raw = samples[record : record + 1].view(np.uint8).reshape(1, -1)
    return np.array(raw[:, off : off + stride], dtype=np.uint8, order="C", copy=True)


def _rollout_to(dataset_path: Path, start_record: int, target_record: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, _field_bytes(samples, start_record, "seed_t", seed_stride))
        for record in range(start_record, target_record + 1):
            binding.step_input_replay_frame_rng(
                handle,
                _field_bytes(samples, record, "seed_t", seed_stride),
                _field_bytes(samples, record, "prev_input_t", input_stride),
                _field_bytes(samples, record, "input_t", input_stride),
            )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy(), samples[target_record]["ref_t1"]


@pytest.mark.integration
def test_throw_owner_body_hitbox_damages_attached_victim_without_victim_hitlag() -> None:
    # Sheik Down Throw has ordinary create_hitbox capsules before set_throw_flags(0) releases the
    # victim. While the defender is still attached in ThrownLw, source publishes damage and
    # thrower-side hitlag/bookkeeping, but the victim does not enter visible hitlag.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE508,ftCo_ThrownLw_Phys,ftCo_ThrownLw_Coll}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007891C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/sheik/replays/validation/sheik/RuralReasonableRat.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 3160
    thrower = 0
    victim = 1
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][thrower]) == ACT_THROW_LW
    assert int(row["seed_t"]["action_id"][victim]) == ACT_THROWN_LW
    assert int(row["seed_t"]["grab_owner_port"][victim]) == thrower
    assert int(row["seed_t"]["hitlag"][thrower]) == 0
    assert int(row["seed_t"]["hitlag"][victim]) == 0
    assert int(row["ref_t1"]["hitlag"][thrower]) > 0
    assert int(row["ref_t1"]["hitlag"][victim]) == 0

    _seed, ref, out = _run_one_step_row(dataset_path, record, victim)
    assert int(out["action_id"][victim]) == ACT_THROWN_LW
    assert int(out["hitlag"][thrower]) == int(ref["hitlag"][thrower])
    assert int(out["hitlag"][victim]) == 0
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]))
    assert int(out["state_flags"][victim, 3]) & STATE_FLAG_221C_B0
    assert int(out["last_attack_landed"][thrower]) == int(ref["last_attack_landed"][thrower])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("filename", "record", "thrower", "victim"),
    [
        ("MixedAllQuetzal.msl", 10097, 0, 1),
        ("UnusedLivelyLouse.msl", 5224, 0, 1),
        ("AttractiveAnyClam.msl", 176, 0, 1),
        ("TornEnchantingGiraffe.msl", 8374, 0, 1),
    ],
)
def test_sheik_attached_throw_body_no_hitlag_sets_x221c_b0(
    filename: str, record: int, thrower: int, victim: int
) -> None:
    # Attached throw BODY percent pulses can leave the victim in Thrown* with no victim hitlag, but
    # still publish fp+0x221C_b0 as the no-reaction damage lane. This locks the source split:
    # accepted damage/bookkeeping is not the same owner as victim hitlag/x221A.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineB1
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007891C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/sheik/replays/validation/sheik" / filename
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["grab_owner_port"][victim]) == thrower
    assert int(row["seed_t"]["hitlag"][victim]) == 0
    assert int(row["ref_t1"]["hitlag"][victim]) == 0
    assert float(row["ref_t1"]["percent"][victim]) > float(row["seed_t"]["percent"][victim])
    assert int(row["ref_t1"]["state_flags"][victim, 3]) & STATE_FLAG_221C_B0

    _seed, ref, out = _run_one_step_row(dataset_path, record, victim)
    _assert_transition_lock_fields_match_ref(out_row=out, ref_row=ref, record=record, p=victim)


@pytest.mark.integration
def test_throw_owner_body_hitbox_hitlag_suppression_requires_attachment() -> None:
    # Adjacent synthetic negative: if the same throw-state BODY hitbox overlaps a non-attached
    # defender, it falls back to ordinary BODY hitlag. This protects against a broad ThrowLw or
    # action-id-only suppression.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/sheik/replays/validation/sheik/RuralReasonableRat.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 3160
    victim = 1

    def clear_attachment(seed_t):
        seed_t["grab_owner_port"][0, victim] = 0xFF

    _seed, _ref, out = _run_one_step_row(dataset_path, record, victim, seed_mutator=clear_attachment)
    assert int(out["hitlag"][victim]) > 0


@pytest.mark.integration
def test_direct_kb_pre_release_throw_body_hit_keeps_attached_victim_hitlag() -> None:
    # Adjacent real-row negative: Fox/Falco ThrowF has an ordinary BODY hit before its
    # set_throw_flags(0) release frame, but that script payload carries direct knockback. It must
    # keep the normal attached-victim hitlag path instead of inheriting Sheik's zero-direct-kb
    # pre-release damage pulse behavior.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 215
    thrower = 1
    victim = 0
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][thrower]) == 219
    assert int(row["seed_t"]["grab_owner_port"][victim]) == thrower
    assert int(row["ref_t1"]["hitlag"][victim]) > 0

    _seed, ref, out = _run_one_step_row(dataset_path, record, victim)
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim])


@pytest.mark.integration
def test_sheik_throwlw_body_hitlag_resumes_thrower_anim_before_release_rollout() -> None:
    # Sheik ThrowLw has an ordinary create_hitbox at frame 31 and a later
    # set_throw_flags(hit_idx=0) release at frame 36. The thrower enters attacker-side hitlag on the
    # BODY hit while the victim remains attached; after hitlag, source resumes Fighter_8006A360 AObj
    # advancement so ftCo_ThrowLw_Anim can reach the release command. This rollout boundary protects
    # the hidden frame-speed continuation rather than the visible one-step damage row.
    #
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowLw_Anim,ftCo_800DD724}
    # data/scripts/sheik.bin (MSLFTSC1) ftCo_SM_ThrowLw create_hitbox / set_throw_flags
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/sheik/replays/validation/sheik/RuralReasonableRat.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    start_record = 2950
    thrower = 0
    victim = 1
    out, ref = _rollout_to(dataset_path, start_record, 3168)
    assert int(ref["action_id"][thrower]) == ACT_THROW_LW
    assert int(out["action_id"][thrower]) == ACT_THROW_LW
    assert float(out["action_frame"][thrower]) == pytest.approx(float(ref["action_frame"][thrower]))
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim]) == ACT_DAMAGE_FLY_TOP
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim])
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]))
    assert float(out["pos_y"][victim]) == pytest.approx(float(ref["pos_y"][victim]), abs=2e-4)


@pytest.mark.integration
def test_sheik_throwlw_release_floor_publication_prevents_false_downbound_rollout() -> None:
    # ftCo_800DDDE4 publishes Sheik ThrowLw's release-local mpColl floor correction before
    # ftCo_800DE7C0 damage entry. RuralReasonableRat rec3168 would otherwise enter DamageFlyTop
    # from the raw below-floor x1A70 root and false-DownBound on rec3169.
    #
    # Probe evidence: reports/triage/newchar_sheik/dolphin_throw_release_rural_reasonable_rat_3168_p1
    # shows ftCo_800DDDE4 selected TransN2 bone 56 / XRotN bone 2, x2226_b2=1 at entry, and
    # mpColl_800471F8 returned victim CollData.cur_pos.y = 0.000100 before damage entry.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80043754}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/sheik/replays/validation/sheik/RuralReasonableRat.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    start_record = 2950
    victim = 1
    out, ref = _rollout_to(dataset_path, start_record, 3169)
    assert int(ref["action_id"][victim]) == ACT_DAMAGE_FLY_TOP
    assert int(out["action_id"][victim]) == ACT_DAMAGE_FLY_TOP
    assert int(out["action_id"][victim]) != ACT_DOWN_BOUND_U
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim])
    assert float(out["pos_y"][victim]) == pytest.approx(float(ref["pos_y"][victim]), abs=3e-4)


@pytest.mark.integration
def test_sheik_throwlw_body_hitlag_does_not_release_before_script_flag_rollout() -> None:
    # Adjacent negative: the same source-owned continuation must not release the victim before the
    # script reaches set_throw_flags(hit_idx=0). RuralReasonableRat rec3167 is one frame before the
    # release boundary and vanilla still keeps the victim in ThrownLw.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/sheik/replays/validation/sheik/RuralReasonableRat.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    start_record = 2950
    thrower = 0
    victim = 1
    out, ref = _rollout_to(dataset_path, start_record, 3167)
    assert int(out["action_id"][thrower]) == int(ref["action_id"][thrower]) == ACT_THROW_LW
    assert float(out["action_frame"][thrower]) == pytest.approx(float(ref["action_frame"][thrower]))
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim]) == ACT_THROWN_LW
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim]) == 0
