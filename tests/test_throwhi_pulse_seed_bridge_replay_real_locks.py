from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _assert_transition_lock_fields_match_ref,
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.run_longest_rollout_streaks import _load_binding


@dataclass(frozen=True)
class _ThrowHiPulseBridgeCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    expect_last_attack_landed_nonzero: bool
    expect_thrower_facing_nonzero: bool | None
    note: str


@dataclass(frozen=True)
class _ThrowHiCrossedPrevCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    item_slot: int
    expected_crossed_prev_frame: int
    expect_matches_ref: bool
    note: str


@dataclass(frozen=True)
class _ThrowHiFrame24CarryCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    positive: bool
    expect_matches_ref: bool
    expected_victim_hitlag_min: int
    note: str


@dataclass(frozen=True)
class _ThrowHiConsumedPulseCountCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    item_slot: int
    expect_item_exists: bool
    note: str


@dataclass(frozen=True)
class _FalcoThrowHiPrev18SecondArticleCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    item_slot: int
    note: str


@dataclass(frozen=True)
class _ThrowHiMidPulseCommandCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    positive_item_slot: int | None
    note: str


@dataclass(frozen=True)
class _ThrowHiPendingSpawnHitlistCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    item_slot: int
    expect_matches_ref: bool
    note: str
    expected_pending_frame: int | None = None


@dataclass(frozen=True)
class _ThrowHiSameCharacterCallbackCase:
    dataset_rel: str
    target_record: int
    thrower_port: int
    item_slot: int
    expect_exists: bool
    note: str


def _run_rollout_rows(dataset_path: Path, start_record: int, rows: tuple[int, ...]) -> dict[int, tuple[np.void, np.void]]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])
    end_record = max(rows)

    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        out: dict[int, tuple[np.void, np.void]] = {}
        for record in range(start_record, end_record + 1):
            prev_input_bytes[0, :] = samples_u8[record, prev_input_off : prev_input_off + input_stride]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            if record in rows:
                out[record] = (out_view[0].copy(), samples["ref_t1"][record].copy())
        return out
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            target_record=583,
            thrower_port=0,
            expect_last_attack_landed_nonzero=True,
            expect_thrower_facing_nonzero=None,
            note="ThrowHi pulse bridge family AGG",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=984,
            thrower_port=1,
            expect_last_attack_landed_nonzero=True,
            expect_thrower_facing_nonzero=None,
            note="ThrowHi pulse bridge family GAT",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=3091,
            thrower_port=0,
            expect_last_attack_landed_nonzero=True,
            expect_thrower_facing_nonzero=None,
            note="ThrowHi pulse bridge family QGD",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=2137,
            thrower_port=1,
            expect_last_attack_landed_nonzero=True,
            expect_thrower_facing_nonzero=None,
            note="ThrowHi pulse bridge family TBK",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            target_record=3424,
            thrower_port=1,
            expect_last_attack_landed_nonzero=False,
            expect_thrower_facing_nonzero=False,
            note="ThrowHi broadened ongoing-hitstun bridge (no damage provenance, left-facing)",
        ),
        _ThrowHiPulseBridgeCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            target_record=5087,
            thrower_port=1,
            expect_last_attack_landed_nonzero=False,
            expect_thrower_facing_nonzero=True,
            note="ThrowHi broadened ongoing-hitstun bridge (no damage provenance, right-facing)",
        ),
    ],
)
def test_throwhi_pulse_seed_bridge_target_pm1_both_players_strict_lock(case: _ThrowHiPulseBridgeCase) -> None:
    # Replay-real target+/-1 strict lock for ThrowHi throw-pulse seed bridge lane in src/items.c.
    #
    # Decomp/data refs for this lane:
    # - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # - refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # - refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,itFoxlaser_UnkMotion1_Phys}
    # - data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"]["events"]
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    thrower = int(case.thrower_port)
    rows = (target_record - 1, target_record, target_record + 1)
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"dataset too short for lock row: record={rec}"

    target = samples[target_record]
    seed = target["seed_t"]

    # ThrowHi seed-bridge preconditions from src/items.c:
    # - thrower in ThrowHi with seeded throw_pulse_consumed latch;
    # - victim in ongoing hitstun from this thrower (with or without damage provenance);
    # - seeded owner shot (state1) exists for velocity-direction bridge.
    victim = 1 - thrower
    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    assert int(seed["throw_pulse_consumed"][thrower]) == 1, case.note
    assert int(seed["hitlag"][thrower]) == 0, case.note
    assert int(seed["hitstun"][victim]) > 0, case.note
    assert int(seed["last_hit_by"][victim]) == thrower, case.note
    if case.expect_last_attack_landed_nonzero:
        assert int(seed["last_attack_landed"][victim]) != 0, case.note
    else:
        assert int(seed["last_attack_landed"][victim]) == 0, case.note
    if case.expect_thrower_facing_nonzero is not None:
        assert int(seed["facing"][thrower]) == int(case.expect_thrower_facing_nonzero), case.note

    owner_state1_count = 0
    for item in seed["items"]:
        if int(item["exists"]) == 0:
            continue
        if int(item["owner"]) != thrower:
            continue
        if int(item["state"]) != 1:
            continue
        owner_state1_count += 1
    assert owner_state1_count >= 1, case.note

    # Strict transition lock coverage for target-1 / target / target+1 on both players.
    for rec in rows:
        _, ref_row, out_row = _run_one_step_row(dataset_path, rec, thrower)
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=rec,
                p=p,
            )


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiCrossedPrevCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=938,
            thrower_port=1,
            item_slot=1,
            expected_crossed_prev_frame=20,
            expect_matches_ref=True,
            note="ThrowHi crossed-prev frame-20 spawn BHH owner p1",
        ),
        _ThrowHiCrossedPrevCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=1673,
            thrower_port=0,
            item_slot=1,
            expected_crossed_prev_frame=20,
            expect_matches_ref=True,
            note="ThrowHi crossed-prev frame-20 spawn BHH owner p0",
        ),
        _ThrowHiCrossedPrevCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=4337,
            thrower_port=0,
            item_slot=1,
            expected_crossed_prev_frame=20,
            expect_matches_ref=True,
            note="ThrowHi crossed-prev frame-20 spawn after prior despawn",
        ),
    ],
)
def test_throwhi_crossed_prev_frame20_article_spawn_locks(case: _ThrowHiCrossedPrevCase) -> None:
    # Replay-real locks for the narrow ThrowHi frame-20 crossed-prev article reconstruction:
    # - ftAction_80071974 sets throw_flags_b0 from set_throw_spawn_projectile.
    # - ftAction_80073354 owns command timer/cursor advancement.
    # - ftFx_Throw_Anim consumes throw_flags_b0 and spawns the state1 throw-side laser.
    # - data/moves/{fox,falco}.json has ThrowHi set_throw_spawn_projectile events at 18/20/24.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    assert int(samples.shape[0]) > target_record, f"dataset too short for lock row: record={target_record}"

    seed = samples[target_record]["seed_t"]
    thrower = int(case.thrower_port)
    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == int(
        case.expected_crossed_prev_frame
    ), case.note

    owner_state1_count = 0
    for item in seed["items"]:
        if int(item["exists"]) == 0:
            continue
        if int(item["owner"]) == thrower and int(item["state"]) == 1:
            owner_state1_count += 1
    if case.expect_matches_ref:
        assert owner_state1_count == 0, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, target_record, thrower)
    fields = ("item_exists", "item_type", "item_state", "item_owner", "item_instance_id")
    slot = int(case.item_slot)
    for field in fields:
        item_field = field.removeprefix("item_")
        got = int(out_row["items"][slot][item_field])
        exp = int(ref_row["items"][slot][item_field])
        if case.expect_matches_ref:
            assert got == exp, f"{case.note}: {field} expected={exp} got={got}"
        else:
            assert got != exp, f"{case.note}: negative unexpectedly matched {field}={got}"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiPendingSpawnHitlistCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=4335,
            thrower_port=0,
            item_slot=1,
            expect_matches_ref=True,
            note="Fox ThrowHi frame-18 pending spawn carries hitbox-2 victim ring",
            expected_pending_frame=18,
        ),
        _ThrowHiPendingSpawnHitlistCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            target_record=9282,
            thrower_port=0,
            item_slot=1,
            expect_matches_ref=True,
            note="Fox ThrowHi frame-18 pending spawn carry in FSP",
            expected_pending_frame=18,
        ),
        _ThrowHiPendingSpawnHitlistCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=1208,
            thrower_port=0,
            item_slot=1,
            expect_matches_ref=True,
            note="frame-20 mid-pulse row stays on the ordinal gate while matching item identity",
            expected_pending_frame=20,
        ),
    ],
)
def test_throwhi_pending_spawn_hitlist_carries_first_pulse_article(
    case: _ThrowHiPendingSpawnHitlistCase,
) -> None:
    # Replay-real locks for the pending-spawn item HitCapsule victim-ring lane:
    # - ftAction_80073354 / ftAction_80071974 expose one pending frame-18 throw_flags_b0 command.
    # - ftFx_Throw_Anim spawns a state1 throw laser through it_8029C6CC.
    # - Dolphin v10 item-hitlist dumps for BHH:4335 show that newly spawned article already carries
    #   the prior throw-laser victim in item hitbox 2, so the runtime seeds that hitbox ring at
    #   spawn rather than letting the immediate BODY path clear the carried article.
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    # refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    assert int(samples.shape[0]) > target_record, f"dataset too short for lock row: record={target_record}"

    seed = samples[target_record]["seed_t"]
    thrower = int(case.thrower_port)
    victim = 1 - thrower
    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    if case.expect_matches_ref:
        if case.expected_pending_frame is not None:
            assert int(seed["throw_command_pending_pulse_frame"][thrower]) == int(
                case.expected_pending_frame
            ), case.note
        assert int(seed["hitstun"][victim]) > 0, case.note
        assert int(seed["last_hit_by"][victim]) == thrower, case.note
        assert int(seed["instance_hit_by"][victim]) == int(seed["instance_id"][thrower]), case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, target_record, thrower)
    fields = ("exists", "type", "state", "owner", "instance_id")
    slot = int(case.item_slot)
    for field in fields:
        got = int(out_row["items"][slot][field])
        exp = int(ref_row["items"][slot][field])
        if case.expect_matches_ref:
            assert got == exp, f"{case.note}: item_{field} expected={exp} got={got}"
        else:
            assert got != exp, f"{case.note}: negative unexpectedly matched item_{field}={got}"
    if case.expect_matches_ref:
        for p in (0, 1):
            _assert_transition_lock_fields_match_ref(
                out_row=out_row,
                ref_row=ref_row,
                record=target_record,
                p=p,
            )


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiSameCharacterCallbackCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=937,
            thrower_port=1,
            item_slot=1,
            expect_exists=False,
            note="Fox/Fox ThrowHi frame-18 front-side first-pulse callback consumes article",
        ),
        _ThrowHiSameCharacterCallbackCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl",
            target_record=2428,
            thrower_port=0,
            item_slot=1,
            expect_exists=True,
            note="Fox/Fox ThrowHi frame-18 later-hitbox victim-ring phase carries article",
        ),
        _ThrowHiSameCharacterCallbackCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl",
            target_record=7337,
            thrower_port=1,
            item_slot=1,
            expect_exists=True,
            note="Fox/Fox ThrowHi frame-18 alternate later-hitbox victim-ring phase carries article",
        ),
        _ThrowHiSameCharacterCallbackCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=1250,
            thrower_port=0,
            item_slot=2,
            expect_exists=False,
            note="Fox/Fox ThrowHi frame-20 fallback does not duplicate live first-pulse article",
        ),
        _ThrowHiSameCharacterCallbackCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "AttachedGoodNaturedGuanaco.msl"
            ),
            target_record=998,
            thrower_port=1,
            item_slot=2,
            expect_exists=True,
            note="cross-character ThrowHi control keeps frame-20 article eligible",
        ),
        _ThrowHiSameCharacterCallbackCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.msl"
            ),
            target_record=463,
            thrower_port=0,
            item_slot=1,
            expect_exists=True,
            note="cross-character ThrowHi front-side control carries first-pulse article",
        ),
    ],
)
def test_throwhi_same_character_callback_phase_locks(
    case: _ThrowHiSameCharacterCallbackCase,
) -> None:
    # Replay-real locks for the same-character ThrowHi item callback phase:
    # - Same-character Fox/Fox rows expose two outcomes from the same first-pulse callback owner:
    #   front-side first-pulse BODY callback consumes the carried article, while frame-20 fallback
    #   command crossing must not duplicate an already-live first-pulse article.
    # - Later-hitbox victim-ring phases with a different last_attack_landed identity carry the article
    #   instead of re-running the hb0 destroy callback.
    # - Cross-character controls stay eligible for the existing carry / frame-20 command paths.
    # - This is intentionally scoped to item lifetime fields; gun misc/cmd_vars[1] bookkeeping is a
    #   separate throw-gun cursor surface.
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    # refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    assert int(samples.shape[0]) > target_record, f"dataset too short for lock row: record={target_record}"

    seed = samples[target_record]["seed_t"]
    thrower = int(case.thrower_port)
    victim = 1 - thrower
    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    assert int(seed["hitstun"][victim]) > 0, case.note
    assert int(seed["instance_hit_by"][victim]) == int(seed["instance_id"][thrower]), case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, target_record, thrower)
    slot = int(case.item_slot)
    assert int(ref_row["items"][slot]["exists"]) == int(case.expect_exists), case.note
    fields = ("exists", "type", "state", "owner", "instance_id")
    for field in fields:
        got = int(out_row["items"][slot][field])
        exp = int(ref_row["items"][slot][field])
        assert got == exp, f"{case.note}: item_{field} expected={exp} got={got}"


@pytest.mark.integration
def test_throwhi_rollout_uses_frame_crossing_after_seed_pending_authority_clears() -> None:
    # Rollout lock for the same ThrowHi command-cursor owner:
    # - `throw_command_pending_pulse_frame` is authoritative only for the teacher-forced reseed
    #   frame.
    # - After that seed authority clears, rollout must fall back to live frame crossing so later
    #   frame-20/frame-24 ThrowHi pulses still serialize instead of being suppressed by the
    #   same-character first-pulse callback negative.
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    assert int(ds.samples[2068]["seed_t"]["throw_command_pending_pulse_frame"][0]) == 20
    assert int(ds.samples[2071]["seed_t"]["throw_command_pending_pulse_frame"][0]) == 24

    rows = _run_rollout_rows(dataset_path, 2042, (2068, 2071))
    for record, slot in ((2068, 2), (2071, 3)):
        out_row, ref_row = rows[record]
        assert int(ref_row["items"][slot]["exists"]) == 1
        for field in ("exists", "type", "state", "owner", "instance_id"):
            assert int(out_row["items"][slot][field]) == int(ref_row["items"][slot][field])


@pytest.mark.integration
def test_throwhi_frame20_timebase_snap_defers_consumed_first_pulse_article_doubles() -> None:
    # Rollout-real lock for the ThrowHi timebase / command-cursor split:
    # - The 4/3 throw-rate AObj reaches action frame 20 on GAT-doubles rec 7734.
    # - The frame-20 throw_flags_b0 command is still serialized by the following ftFx_Throw_Anim
    #   callback because the frame-18 article has already advanced same-source item bookkeeping and
    #   left no live state1 article.
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_Throw_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD4B0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/doubles_recent/replays/validation/doubles_recent/Game_20260509T152622.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed_7734 = ds.samples[7734]["seed_t"]
    thrower = 0
    victim = 1
    assert int(seed_7734["char_id"][thrower]) == 1  # Fox
    assert int(seed_7734["action_id"][thrower]) == 221  # ThrowHi
    assert int(seed_7734["throw_pulse_crossed_prev_frame"][thrower]) == 18
    assert int(seed_7734["throw_command_pending_pulse_frame"][thrower]) == 0
    assert int(seed_7734["combo_count"][thrower]) == 2
    assert int(seed_7734["hitstun"][victim]) > 0
    assert int(seed_7734["last_hit_by"][victim]) == thrower
    assert int(seed_7734["instance_hit_by"][victim]) == int(seed_7734["instance_id"][thrower])
    seed_state1_count = sum(
        1
        for item in seed_7734["items"]
        if int(item["exists"]) != 0
        and int(item["owner"]) == thrower
        and int(item["type"]) == 54
        and int(item["state"]) == 1
    )
    assert seed_state1_count == 0

    rows = _run_rollout_rows(dataset_path, 7725, (7734, 7735))
    out_7734, ref_7734 = rows[7734]
    assert int(out_7734["action_frame"][thrower]) == int(ref_7734["action_frame"][thrower]) == 20
    assert int(out_7734["items"][1]["exists"]) == int(ref_7734["items"][1]["exists"]) == 0

    out_7735, ref_7735 = rows[7735]
    assert int(ref_7735["items"][1]["exists"]) == 1
    for field in ("exists", "type", "state", "owner", "instance_id"):
        assert int(out_7735["items"][1][field]) == int(ref_7735["items"][1][field])
    assert float(out_7735["items"][1]["pos_x"]) == pytest.approx(
        float(ref_7735["items"][1]["pos_x"]), abs=1.0e-3
    )
    assert float(out_7735["items"][1]["pos_y"]) == pytest.approx(
        float(ref_7735["items"][1]["pos_y"]), abs=1.0e-3
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiMidPulseCommandCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=1251,
            thrower_port=0,
            positive_item_slot=2,
            note="ThrowHi frame-20 command emits second article when combo owner has only first pulse",
        ),
        _ThrowHiMidPulseCommandCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.msl"
            ),
            target_record=464,
            thrower_port=0,
            positive_item_slot=None,
            note="negative sentinel: combo owner already represented frame-20 pulse",
        ),
    ],
)
def test_throwhi_mid_pulse_command_uses_combo_hitlist_ordinal_gate(
    case: _ThrowHiMidPulseCommandCase,
) -> None:
    # Replay-real lock for the ThrowHi frame-20 command/hitlist discriminator:
    # - ftAction_80073354 advances command script time and ftFx_Throw_Anim consumes one pending
    #   throw_flags_b0 pulse into it_8029C6CC.
    # - ftColl_8007646C / ftColl_800763C0 advance item-domain combo bookkeeping for item hits.
    # - The runtime emits the second ThrowHi article only while the live state1 article count and
    #   combo_count both show that the frame-20 pulse has not already been represented.
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    assert int(samples.shape[0]) > target_record, f"dataset too short for lock row: record={target_record}"

    seed = samples[target_record]["seed_t"]
    thrower = int(case.thrower_port)
    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    assert int(seed["throw_command_pending_pulse_frame"][thrower]) == 20, case.note
    assert int(seed["combo_count"][thrower]) in (1, 2), case.note

    owner_state1_count = 0
    for item in seed["items"]:
        if int(item["exists"]) == 0:
            continue
        if int(item["owner"]) == thrower and int(item["state"]) == 1:
            owner_state1_count += 1
    assert owner_state1_count == 1, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, target_record, thrower)
    if case.positive_item_slot is not None:
        slot = int(case.positive_item_slot)
        assert int(seed["combo_count"][thrower]) == 1, case.note
        assert int(out_row["items"][slot]["exists"]) == int(ref_row["items"][slot]["exists"]) == 1
        assert int(out_row["items"][slot]["state"]) == int(ref_row["items"][slot]["state"]) == 1
    else:
        assert int(seed["combo_count"][thrower]) >= 2, case.note
    for p in (0, 1):
        _assert_transition_lock_fields_match_ref(
            out_row=out_row,
            ref_row=ref_row,
            record=target_record,
            p=p,
        )


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiConsumedPulseCountCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl",
            target_record=2963,
            thrower_port=0,
            item_slot=3,
            expect_item_exists=False,
            note="Falco ThrowHi frame-20 consumed pulse does not emit a third state1 article",
        ),
        _ThrowHiConsumedPulseCountCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl",
            target_record=6738,
            thrower_port=0,
            item_slot=3,
            expect_item_exists=False,
            note="Falco ThrowHi frame-20 consumed pulse max-count guard PRH",
        ),
        _ThrowHiConsumedPulseCountCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl",
            target_record=267,
            thrower_port=1,
            item_slot=3,
            expect_item_exists=False,
            note="Falco ThrowHi frame-20 consumed pulse max-count guard TCH",
        ),
        _ThrowHiConsumedPulseCountCase(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            target_record=3091,
            thrower_port=0,
            item_slot=2,
            expect_item_exists=True,
            note="negative sentinel: frame-20 consumed pulse still emits the second state1 article",
        ),
    ],
)
def test_throwhi_consumed_pulse_respects_existing_state1_article_count(
    case: _ThrowHiConsumedPulseCountCase,
) -> None:
    # Replay-real lock for the ThrowHi consumed-pulse shot-count guard in src/items.c.
    #
    # Decomp/data refs:
    # - ftAction_80071974 emits one throw_flags_b0 pulse per set_throw_spawn_projectile command.
    # - ftAction_80073354 advances the command timer/cursor.
    # - ftFx_Throw_Anim consumes at most one throw_flags_b0 pulse in its Anim callback.
    # - data/moves/falco.json has ThrowHi set_throw_spawn_projectile events at 18/20/24.
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    target = ds.samples[case.target_record]
    seed = target["seed_t"]
    thrower = int(case.thrower_port)

    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    assert int(seed["char_id"][thrower]) == 22, case.note  # Falco
    assert int(seed["throw_pulse_consumed"][thrower]) == 1, case.note
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == 20, case.note

    seed_state1_count = 0
    for item in seed["items"]:
        if (
            int(item["exists"]) != 0
            and int(item["owner"]) == thrower
            and int(item["type"]) == 55
            and int(item["state"]) == 1
        ):
            seed_state1_count += 1
    if case.expect_item_exists:
        assert seed_state1_count == 1, case.note
    else:
        assert seed_state1_count >= 2, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, case.target_record, thrower)
    slot = int(case.item_slot)
    assert int(ref_row["items"][slot]["exists"]) == int(case.expect_item_exists), case.note
    assert int(out_row["items"][slot]["exists"]) == int(case.expect_item_exists), case.note
    assert int(out_row["items"][slot]["type"]) == int(ref_row["items"][slot]["type"]), case.note
    assert int(out_row["items"][slot]["state"]) == int(ref_row["items"][slot]["state"]), case.note
    assert int(out_row["items"][slot]["owner"]) == int(ref_row["items"][slot]["owner"]), case.note


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _FalcoThrowHiPrev18SecondArticleCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl",
            target_record=6737,
            thrower_port=0,
            item_slot=2,
            note="Falco ThrowHi crossed-prev frame-18 emits second state1 article PRH",
        ),
        _FalcoThrowHiPrev18SecondArticleCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl",
            target_record=2962,
            thrower_port=0,
            item_slot=2,
            note="Falco ThrowHi crossed-prev frame-18 emits second state1 article HVG",
        ),
        _FalcoThrowHiPrev18SecondArticleCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl",
            target_record=266,
            thrower_port=1,
            item_slot=2,
            note="Falco ThrowHi crossed-prev frame-18 emits second state1 article TCH",
        ),
    ],
)
def test_falco_throwhi_crossed_prev_frame18_emits_second_article(
    case: _FalcoThrowHiPrev18SecondArticleCase,
) -> None:
    # Event-probe-backed Falco ThrowHi crossed-prev second article:
    # - PRH:6737 seeds one live state1 Falco throw laser with crossed-prev frame 18.
    # - Vanilla emits another it_8029C6CC spawn request in the next frame and carries it through
    #   post-frame; no same-frame hb0 destroy occurs for the newly emitted article.
    # - Keep this scoped to Falco, crossed-prev frame 18, exactly one live state1 shot, and
    #   same-source victim provenance. QGD primary ThrowLw controls remain covered separately.
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    assert int(samples.shape[0]) > target_record, f"dataset too short for lock row: record={target_record}"

    seed = samples[target_record]["seed_t"]
    thrower = int(case.thrower_port)
    victim = 1 - thrower
    assert int(seed["char_id"][thrower]) == 22, case.note  # Falco
    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == 18, case.note
    assert int(seed["throw_command_pending_pulse_frame"][thrower]) == 0, case.note
    assert int(seed["hitstun"][victim]) > 0, case.note
    assert int(seed["last_hit_by"][victim]) in (thrower, thrower + 1), case.note
    assert int(seed["instance_hit_by"][victim]) == int(seed["instance_id"][thrower]), case.note
    seed_state1_count = sum(
        1
        for item in seed["items"]
        if int(item["exists"]) != 0
        and int(item["owner"]) == thrower
        and int(item["type"]) == 55
        and int(item["state"]) == 1
    )
    assert seed_state1_count == 1, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, target_record, thrower)
    slot = int(case.item_slot)
    for field in ("exists", "type", "state", "owner", "instance_id"):
        assert int(out_row["items"][slot][field]) == int(ref_row["items"][slot][field]), case.note
    assert int(ref_row["items"][slot]["exists"]) == 1, case.note


@pytest.mark.integration
def test_falco_throwhi_crossed_prev_frame18_rollout_emits_second_article_pec() -> None:
    # Rollout-real lock for the same command owner as the one-step crossed-prev locks above:
    # - PEC:8136 starts with one live state1 Falco ThrowHi laser and the prior frame-18 pulse.
    # - The next rollout frame must still emit the frame-20 command article; this is owned by the
    #   extracted ThrowHi set_throw_spawn_projectile sequence, not by a victim-weight rate proxy.
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/yoshis_story_recent/PhysicalElectricCapybara.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[8136]["seed_t"]
    thrower = 0
    victim = 1
    assert int(seed["char_id"][thrower]) == 22
    assert int(seed["action_id"][thrower]) == 221
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == 18
    assert int(seed["throw_command_pending_pulse_frame"][thrower]) == 0
    assert int(seed["hitstun"][victim]) > 0
    assert int(seed["last_hit_by"][victim]) == 0
    assert int(seed["instance_hit_by"][victim]) == int(seed["instance_id"][thrower])
    seed_state1_count = sum(
        1
        for item in seed["items"]
        if int(item["exists"]) != 0
        and int(item["owner"]) == thrower
        and int(item["type"]) == 55
        and int(item["state"]) == 1
    )
    assert seed_state1_count == 1

    rows = _run_rollout_rows(dataset_path, 8136, (8137, 8139))
    for record, slot in ((8137, 6), (8139, 7)):
        out_row, ref_row = rows[record]
        assert int(ref_row["items"][slot]["exists"]) == 1
        for field in ("exists", "type", "state", "owner", "instance_id"):
            assert int(out_row["items"][slot][field]) == int(ref_row["items"][slot][field])


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowHiFrame24CarryCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.msl"
            ),
            target_record=3426,
            thrower_port=1,
            positive=True,
            expect_matches_ref=True,
            expected_victim_hitlag_min=3,
            note="Falco ThrowHi frame-24 current-pulse carried BODY row",
        ),
        _ThrowHiFrame24CarryCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "TreasuredBackKangaroo.msl"
            ),
            target_record=5090,
            thrower_port=1,
            positive=True,
            expect_matches_ref=True,
            expected_victim_hitlag_min=3,
            note="Falco ThrowHi frame-24 crossed-prev carried BODY row",
        ),
        _ThrowHiFrame24CarryCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.msl"
            ),
            target_record=3427,
            thrower_port=1,
            positive=False,
            expect_matches_ref=True,
            expected_victim_hitlag_min=0,
            note="Falco frame-24 lower-hitlag handoff consumes normally",
        ),
        _ThrowHiFrame24CarryCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            target_record=1208,
            thrower_port=0,
            positive=False,
            expect_matches_ref=True,
            expected_victim_hitlag_min=0,
            note="Fox frame-20 control matches through shared throw lifecycle, not Falco frame-24 carry",
        ),
    ],
)
def test_falco_throwhi_frame24_carried_body_locks(case: _ThrowHiFrame24CarryCase) -> None:
    # Replay-real locks for the narrow Falco ThrowHi frame-24 carried BODY lane:
    # - ftAction_80071974 sets throw_flags_b0 from set_throw_spawn_projectile.
    # - ftFx_Throw_Anim consumes the pulse and spawns the state1 throw-side laser.
    # - The retained runtime branch is Falco-only, frame-24-only, and requires the victim to be
    #   in same-owner hitlag with cleared replay damage provenance.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # data/moves/falco.json moves["ftCo_SM_ThrowHi"].events
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    target_record = int(case.target_record)
    assert int(samples.shape[0]) > target_record, f"dataset too short for lock row: record={target_record}"

    seed = samples[target_record]["seed_t"]
    thrower = int(case.thrower_port)
    victim = 1 - thrower
    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    if case.positive:
        assert int(seed["char_id"][thrower]) == 22, case.note  # Falco
        assert int(seed["hitlag"][victim]) >= int(case.expected_victim_hitlag_min), case.note
        assert int(seed["last_hit_by"][victim]) == thrower, case.note
        assert int(seed["last_attack_landed"][victim]) == 0, case.note
    else:
        assert (
            int(seed["char_id"][thrower]) != 22
            or int(seed["hitlag"][victim]) < 3
            or int(seed["throw_pulse_crossed_prev_frame"][thrower]) != 24
        )

    _, ref_row, out_row = _run_one_step_row(dataset_path, target_record, thrower)
    if case.expect_matches_ref:
        for p in (0, 1):
            for field in ("hitlag", "hitstun", "combo_count"):
                got = int(out_row[field][p])
                exp = int(ref_row[field][p])
                assert got == exp, f"{case.note}: p={p} {field} expected={exp} got={got}"
        ref_live = sum(1 for item in ref_row["items"] if int(item["exists"]) != 0)
        out_live = sum(1 for item in out_row["items"] if int(item["exists"]) != 0)
        assert out_live == ref_live, f"{case.note}: live item count expected={ref_live} got={out_live}"
    else:
        ref_live = sum(1 for item in ref_row["items"] if int(item["exists"]) != 0)
        out_live = sum(1 for item in out_row["items"] if int(item["exists"]) != 0)
        assert out_live != ref_live, f"{case.note}: negative unexpectedly matched live item count"


@pytest.mark.integration
def test_falco_throwhi_frame24_projectile_side_body_consumes_and_compacts_mgs() -> None:
    # Replay-real lock for the projectile-side half of the Falco ThrowHi frame-24 callback split:
    # - ftFx_Throw_Anim has emitted the final set_throw_spawn_projectile pulse.
    # - The older state1 article is on the projectile side of the crossed-prev segment and is consumed
    #   by it_8029C4D4 / it_80272460 BODY, leaving the newer article compacted into slot 1.
    # - This is the FoD float-error outlier owner: stale slot-1 carry falsely leaves the newer
    #   projectile in slot 2 and amplifies item_vel_y in the normalized report.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    # refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}
    # data/moves/falco.json moves["ftCo_SM_ThrowHi"].events
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    seed, ref_row, out_row = _run_one_step_row(dataset_path, 1815, 0)
    thrower = 0
    victim = 1
    assert int(seed["char_id"][thrower]) == 22  # Falco
    assert int(seed["action_id"][thrower]) == 221  # ThrowHi
    assert int(seed["action_frame"][thrower]) == 24
    assert int(seed["throw_pulse_crossed_prev_frame"][thrower]) == 24
    assert int(seed["throw_command_pending_pulse_frame"][thrower]) == 24
    assert int(seed["hitlag"][victim]) == 4
    assert int(seed["last_attack_landed"][victim]) == 0
    assert int(seed["last_hit_by"][victim]) == thrower
    projectile_side_x = (float(seed["pos_x"][victim]) - float(seed["pos_x"][thrower])) * float(
        seed["items"][1]["vel_x"]
    )
    assert projectile_side_x > 0.0

    assert int(seed["items"][1]["exists"]) == 1
    assert int(seed["items"][2]["exists"]) == 1
    assert int(ref_row["items"][1]["exists"]) == 1
    assert int(ref_row["items"][2]["exists"]) == 0
    for field in ("exists", "type", "state", "owner", "instance_id", "timer"):
        assert out_row["items"][1][field] == ref_row["items"][1][field], field
        assert out_row["items"][2][field] == ref_row["items"][2][field], field
    assert float(out_row["items"][1]["vel_y"]) == pytest.approx(float(ref_row["items"][1]["vel_y"]), abs=1e-6)
    assert int(out_row["combo_count"][thrower]) == int(ref_row["combo_count"][thrower]) == 3
    assert float(out_row["percent"][victim]) == pytest.approx(float(ref_row["percent"][victim]), abs=1e-6)
    assert int(out_row["hitlag"][victim]) == int(ref_row["hitlag"][victim]) == 4


@pytest.mark.integration
def test_throwhi_same_frame_laser_topoff_merges_kb_without_second_damage_entry() -> None:
    # PRH 6744 has two live Falco ThrowHi state1 laser articles hitting the same victim in one
    # item pass. Both contribute percent, but only the first owns the Damage entry/x2088 bump; the
    # second contributes the ftCo_Damage_CalcVel merge without replacing the larger existing Y KB.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_CalcVel,ftCo_8008DCE0}
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    target_record = 6744
    thrower = 0
    victim = 1
    seed, ref_row, out_row = _run_one_step_row(dataset_path, target_record, thrower)
    assert int(seed["action_id"][thrower]) == 221  # ThrowHi
    assert int(seed["char_id"][thrower]) == 22  # Falco
    assert int(seed["action_id"][victim]) == 90  # DamageFlyTop
    assert int(seed["hitlag"][victim]) == 0
    assert int(seed["hitstun"][victim]) > 0

    for field in ("action_id", "action_frame", "hitlag", "hitstun", "percent", "instance_id"):
        assert out_row[field][victim] == ref_row[field][victim], field
    assert int(out_row["facing"][victim]) == int(ref_row["facing"][victim])
    assert float(out_row["speed_x_attack"][victim]) == pytest.approx(
        float(ref_row["speed_x_attack"][victim]), abs=1e-6
    )
    assert float(out_row["speed_y_attack"][victim]) == pytest.approx(
        float(ref_row["speed_y_attack"][victim]), abs=1e-6
    )

    def clear_second_topoff_article(seed_t: np.ndarray) -> None:
        item = seed_t[0]["items"][2]
        item["exists"] = np.uint8(0)
        item["type"] = np.uint16(0)
        item["state"] = np.uint8(0)
        item["owner"] = np.int8(-1)
        item["instance_id"] = np.uint16(0)
        item["attack_id"] = np.uint16(1)
        item["attack_instance"] = np.uint16(0)

    _, ref_mut, out_mut = _run_one_step_row(
        dataset_path, target_record, thrower, seed_mutator=clear_second_topoff_article
    )
    assert int(out_mut["instance_id"][victim]) == int(ref_mut["instance_id"][victim])
    assert float(out_mut["speed_y_attack"][victim]) == pytest.approx(
        float(ref_mut["speed_y_attack"][victim]), abs=1e-6
    )
    assert float(out_mut["percent"][victim]) < float(ref_mut["percent"][victim])
