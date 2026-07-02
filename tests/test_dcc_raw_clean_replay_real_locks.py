from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views


_DCC = Path("replays/validation/aggregate_recent/DistinctCaringCobra.slpz")
_EWT = Path(
    "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slpz"
)
_FSP = Path("replays/validation/aggregate_recent/FavorableSuperficialPig.slpz")
_MVP = Path(
    "replays/validation/battlefield_recent/MediumVirtualPig.slpz"
)

ACT_DAMAGE_FLY_HI = 0x0057
ACT_FX_SPECIAL_AIR_LW_START = 0x016D


def _dataset_path() -> Path:
    return _dataset_path_for(_DCC)


def _dataset_path_for(rel_path: Path) -> Path:
    root = Path(__file__).resolve().parents[1]
    path = root / rel_path
    if not path.exists():
        pytest.skip(f"missing validation dataset: {rel_path}")
    return path


def _run_one_step(ds, record: int, *, rollout_reseed: bool = False) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    row = ds.rows[record : record + 1]
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).copy().reshape(1, input_stride)
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        if rollout_reseed:
            binding.reseed_seed_rollout(handle, seed_bytes)
        else:
            binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)


def _run_rollout(ds, start_record: int, target_record: int) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    samples = ds.rows

    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes[0, :] = seed_u8[start_record, :seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, target_record + 1):
            prev_input_bytes[0, :] = prev_input_u8[record, :input_stride]
            input_bytes[0, :] = input_u8[record, :input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)


def _run_rollout_to_pre_combat_resolve(
    ds, start_record: int, target_record: int, mutator=None
) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    samples = ds.rows

    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes[0, :] = seed_u8[start_record, :seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, target_record):
            prev_input_bytes[0, :] = prev_input_u8[record, :input_stride]
            input_bytes[0, :] = input_u8[record, :input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
        prev_input_bytes[0, :] = prev_input_u8[target_record, :input_stride]
        input_bytes[0, :] = input_u8[target_record, :input_stride]
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        if mutator is not None:
            mutator(binding, handle)
        binding.debug_combat_resolve(handle)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    finally:
        binding.destroy(handle)


def _assert_fields_match(out: np.void, ref: np.void, *, players: tuple[int, ...] = (0, 1)) -> None:
    for p in players:
        for field in (
            "action_id",
            "animation_index",
            "hitlag",
            "hitstun",
            "instance_id",
            "instance_hit_by",
        ):
            assert int(out[field][p]) == int(ref[field][p]), f"p={p} field={field}"
        assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=0.001)


@pytest.mark.integration
def test_dcc_turn_kneebend_hidden_facing_rollout_closes_3296() -> None:
    # Turn -> KneeBend hidden facing owner:
    # `ftCo_Turn_IASA` can flip facing for attack checks, then enter KneeBend on the same callback.
    # The rollout must preserve the source Turn-facing lane rather than treating the temporary
    # attack-facing flip as visible facing.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::fn_800C9C2C
    ds = load_replay_buffers(str(_dataset_path()))
    out = _run_rollout(ds, 3200, 3296)
    ref = ds.rows[3296]["ref_t1"]

    assert int(ds.rows[3296]["seed_t"]["action_id"][1]) == 14  # Wait.
    assert int(ref["action_id"][1]) == 20  # Dash.
    _assert_fields_match(out, ref, players=(1,))


@pytest.mark.integration
def test_turn_kneebend_tap_jump_control_stays_normal_facing() -> None:
    # DCC's hidden-facing runtime fallback is button-jump owned. Tap-jump Turn -> KneeBend rows
    # remain on the ordinary restored-facing path from ftCo_Turn_IASA / ftCo_Jump_CheckInput.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_CheckInput
    ds = load_replay_buffers(str(_dataset_path_for(_FSP)))
    turn_record = 5340
    jump_record = 5343
    seed = ds.rows[turn_record]["seed_t"]
    input_t = ds.rows[turn_record]["input_t"]["p"][0]

    assert int(seed["action_id"][0]) == 18  # Turn.
    assert int(seed["turn_kneebend_facing_override_u8"][0]) == 0
    assert int(input_t["buttons"]) == 0
    assert int(input_t["main_y"]) > 0

    out = _run_rollout(ds, 5200, jump_record)
    ref = ds.rows[jump_record]["ref_t1"]
    assert int(ref["action_id"][0]) == 26  # JumpB.
    assert int(out["action_id"][0]) == int(ref["action_id"][0])
    assert int(out["facing"][0]) == int(ref["facing"][0])


@pytest.mark.integration
def test_dcc_same_frame_aerial_trade_damageflyroll_current_processhit_source_4968() -> None:
    # Reciprocal AttackAirN/AttackAirB BODY hits share one global RNG stream. The ProcessHit source
    # payload, not stale last-hit state or a character-pair branch, admits the same-frame
    # DamageFlyRoll gates: p0 carries one Fighter_8006CDA4 pre-gate consume, p1 carries the
    # zero-consume gate marker.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
    # data/moves/{fox,falco}.json::moves.ftCo_SM_{AttackAirN,AttackAirB}.events.create_hitbox
    ds = load_replay_buffers(str(_dataset_path()))
    record = 4968
    seed = ds.rows[record]["seed_t"]
    ref = ds.rows[record]["ref_t1"]

    assert int(seed["action_id"][0]) == 65  # AttackAirN.
    assert int(seed["action_id"][1]) == 67  # AttackAirB.
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][0]) == 1
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][1]) == 4

    out = _run_one_step(ds, record, rollout_reseed=True)
    _assert_fields_match(out, ref)


@pytest.mark.integration
def test_dcc_grounded_shine_terminal_dense_hitlist_rollout_closes_6431() -> None:
    # Grounded SpecialLwStart carries a source HitCapsule victim-ring owner even when the serialized
    # dense hitlist row was seeded before the hitbox instance change. The runtime proof is terminal
    # same-source DamageFlyTop state, not a replay row or stage band.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwStart_Pass
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    ds = load_replay_buffers(str(_dataset_path()))
    record = 6431
    seed = ds.rows[record]["seed_t"]
    ref = ds.rows[record]["ref_t1"]

    assert int(seed["action_id"][1]) == 39  # SquatWait, entering SpecialLwStart this frame.
    assert int(ref["action_id"][1]) == 360  # SpecialLwStart.
    assert int(seed["action_id"][0]) == 90  # DamageFlyTop.
    assert int(ref["hitlag"][0]) == 0
    assert int(ref["hitstun"][0]) == int(seed["hitstun"][0]) - 1

    out = _run_rollout(ds, 6300, record)
    _assert_fields_match(out, ref)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("name", "mutator", "expect_body"),
    (
        (
            "aerial_shine",
            lambda binding, handle: binding.debug_set_damage_phase(
                handle, 0, 1, ACT_FX_SPECIAL_AIR_LW_START, 0, 729, 0
            ),
            False,
        ),
        (
            "non_terminal_damagefly",
            lambda binding, handle: binding.debug_set_damage_phase(
                handle, 0, 0, ACT_DAMAGE_FLY_HI, 1, 52, 0
            ),
            True,
        ),
        (
            "no_live_source",
            lambda binding, handle: binding.debug_set_damage_source(handle, 0, 0, 6, 1277),
            True,
        ),
        (
            "same_instance_source",
            lambda binding, handle: binding.debug_set_damage_source(handle, 0, 0, 1, 1288),
            True,
        ),
        (
            "stale_dense_high_hitstun",
            lambda binding, handle: binding.debug_set_damage_phase(handle, 0, 0, 90, 3, 52, 0),
            True,
        ),
    ),
)
def test_shine_start_dense_hitlist_boundaries_do_not_suppress_real_body(
    name: str, mutator, expect_body: bool
) -> None:
    # SpecialLwStart dense-hitlist suppression is terminal, grounded, same-source DamageFlyTop
    # ownership only. These controls force each boundary off immediately before combat; the real
    # BODY hit must then be allowed through instead of being hidden by stale dense victims_1 state.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwStart_Pass
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    ds = load_replay_buffers(str(_dataset_path()))
    record = 6431
    ref = ds.rows[record]["ref_t1"]

    out = _run_rollout_to_pre_combat_resolve(ds, 6300, record, mutator)
    if expect_body:
        assert float(out["percent"][0]) > float(ref["percent"][0]), name
        assert int(out["hitlag"][0]) > int(ref["hitlag"][0]), name
        assert int(out["hitstun"][0]) > int(ref["hitstun"][0]), name
    else:
        assert float(out["percent"][0]) == pytest.approx(float(ref["percent"][0]), abs=0.001)
        assert int(out["hitlag"][0]) == int(ref["hitlag"][0]), name
        assert int(out["hitstun"][0]) == int(ref["hitstun"][0]), name


@pytest.mark.integration
def test_dcc_strong_attackairlw_terminal_damageflytop_hitstun_rollout_closes_7083() -> None:
    # Strong DAir meteor payload into terminal DamageFlyTop consumes the prior DamageFlyTop callback
    # tick before the new Damage state is serialized. The selected HitCapsule payload proves this
    # source owner; hitlag, percent, action, and KB stay on the ordinary ProcessHit path.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # data/moves/falco.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
    ds = load_replay_buffers(str(_dataset_path()))
    record = 7083
    seed = ds.rows[record]["seed_t"]
    ref = ds.rows[record]["ref_t1"]

    assert int(seed["action_id"][1]) == 69  # AttackAirLw.
    assert int(seed["action_id"][0]) == 90  # terminal DamageFlyTop.
    assert int(ref["hitstun"][0]) == 55

    out = _run_rollout(ds, 7000, record)
    _assert_fields_match(out, ref)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("rel_path", "record"),
    (
        (_EWT, 8696),
        (_MVP, 1163),
    ),
)
def test_first_create_attackairlw_damageflytop_keeps_full_hitstun(
    rel_path: Path, record: int
) -> None:
    # The DCC hitstun subtract is a carried-HitCapsule owner after AttackAirLw's extracted
    # create-hitbox frame. First-create strong DAir rows keep the ordinary ftCo_ScaleBy154
    # hitstun result.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # data/moves/falco.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
    ds = load_replay_buffers(str(_dataset_path_for(rel_path)))
    seed = ds.rows[record]["seed_t"]
    ref = ds.rows[record]["ref_t1"]

    assert int(seed["action_id"][1]) == 69  # AttackAirLw.
    assert int(seed["action_frame"][1]) == 4
    assert not any(int(v) for v in seed["combat_hitbox_prev_valid"][1])

    out = _run_one_step(ds, record)
    assert int(out["hitstun"][0]) == int(ref["hitstun"][0])
    assert float(out["percent"][0]) == pytest.approx(float(ref["percent"][0]), abs=0.001)


@pytest.mark.integration
def test_dcc_terminal_damageflytop_phantom_expiry_rollout_closes_9367_and_9685() -> None:
    # DamageFlyTop owns ftCo_Damage_OnExitHitlag, and fighter.c ticks x189C before ProcessHit damage
    # resolution. A rollout seeded before the hidden phantom/tip-log timer can lose the serialized
    # x1898/x189C lanes; the terminal x18AC/hitstun phase plus live source episode admits only the
    # minimum ftColl_8007BE3C phantom damage, then later hitstun math sees the correct percent.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006D10C}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007BE3C
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
    ds = load_replay_buffers(str(_dataset_path()))

    phantom_record = 9367
    phantom_seed = ds.rows[phantom_record]["seed_t"]
    phantom_ref = ds.rows[phantom_record]["ref_t1"]
    assert int(phantom_seed["action_id"][1]) == 90  # DamageFlyTop.
    assert int(phantom_seed["hitstun"][1]) == 4
    assert int(phantom_seed["damage_time_since_hit_x18ac"][1]) == 83
    assert float(phantom_seed["phantom_damage_pending_x1898"][1]) == 0.0
    assert int(phantom_seed["phantom_damage_timer_x189c"][1]) == 0
    assert float(phantom_ref["percent"][1]) == pytest.approx(
        float(phantom_seed["percent"][1]) + 1.0, abs=0.001
    )

    phantom_out = _run_rollout(ds, 9000, phantom_record)
    _assert_fields_match(phantom_out, phantom_ref, players=(1,))

    later_record = 9685
    later_ref = ds.rows[later_record]["ref_t1"]
    later_out = _run_rollout(ds, 9000, later_record)
    _assert_fields_match(later_out, later_ref, players=(1,))


@pytest.mark.integration
def test_terminal_damageflytop_phantom_expiry_seed_frame_does_not_fabricate_damage() -> None:
    # The terminal phantom fallback is rollout-after-reseed only. Exact one-step seed frames with
    # empty x1898/x189C lanes must not invent hidden phantom damage.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    ds = load_replay_buffers(str(_dataset_path()))
    record = 9367
    seed = ds.rows[record]["seed_t"]

    out = _run_one_step(ds, record, rollout_reseed=True)
    assert float(out["percent"][1]) == pytest.approx(float(seed["percent"][1]), abs=0.001)


@pytest.mark.integration
@pytest.mark.parametrize("record", (9365, 9366, 9368))
def test_terminal_damageflytop_phantom_expiry_adjacent_phase_does_not_fabricate_damage(
    record: int,
) -> None:
    # Adjacent terminal phases are not the x18AC/hitstun callback boundary used by the fallback.
    ds = load_replay_buffers(str(_dataset_path()))
    ref = ds.rows[record]["ref_t1"]

    out = _run_rollout_to_pre_combat_resolve(ds, 9000, record)
    assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]), abs=0.001)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("name", "mutator"),
    (
        (
            "no_live_source",
            lambda binding, handle: binding.debug_set_damage_source(handle, 0, 1, 6, 1900),
        ),
        (
            "self_source",
            lambda binding, handle: binding.debug_set_damage_source(handle, 0, 1, 1, 1903),
        ),
    ),
)
def test_terminal_damageflytop_phantom_expiry_source_boundaries_do_not_double_apply(
    name: str, mutator
) -> None:
    # At the DCC terminal callback boundary, removing the live non-self source episode or keeping
    # the explicit empty x1898/x189C lane must prevent extra fabricated phantom damage.
    ds = load_replay_buffers(str(_dataset_path()))
    record = 9367
    ref = ds.rows[record]["ref_t1"]

    out = _run_rollout_to_pre_combat_resolve(ds, 9000, record, mutator)
    assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]), abs=0.001), name
