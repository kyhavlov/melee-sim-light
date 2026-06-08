from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


_LIM = (
    "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
    "LawfulInsistentMeerkat.msl"
)
_FEH = (
    "datasets/aggregate_recent/replays/validation/dream_land_recent/"
    "FlippantEnchantedHorse.msl"
)


def _dataset_path(rel_path: str = _LIM) -> Path:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    path = root / rel_path
    if not path.exists():
        pytest.skip(f"missing local dataset artifact: {rel_path}")
    return path


def _field_bytes(samples, record: int, field: str, stride: int) -> np.ndarray:
    off = int(samples.dtype.fields[field][1])
    raw = samples[record : record + 1].view(np.uint8).reshape(1, -1)
    return np.array(raw[:, off : off + stride], dtype=np.uint8, order="C", copy=True)


def _step_one(record: int, rel_path: str = _LIM) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(_dataset_path(rel_path)))
    samples = ds.samples
    row = samples[record : record + 1]
    assert int(row.shape[0]) == 1

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
        binding.reseed_seed(
            handle,
            np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .copy()
            .reshape(1, seed_stride),
        )
        binding.step_input(
            handle,
            np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
            .copy()
            .reshape(1, input_stride),
            np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
            .copy()
            .reshape(1, input_stride),
        )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return row["seed_t"][0], out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy(), row["ref_t1"][0]


def _rollout(start: int, stop: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(_dataset_path()))
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
        binding.reseed_seed_rollout(handle, _field_bytes(samples, start, "seed_t", seed_stride))
        for record in range(start, stop + 1):
            binding.step_input_replay_frame_rng(
                handle,
                _field_bytes(samples, record, "seed_t", seed_stride),
                _field_bytes(samples, record, "prev_input_t", input_stride),
                _field_bytes(samples, record, "input_t", input_stride),
            )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy(), samples[stop]["ref_t1"]


def _assert_fields_exact(out: np.void, ref: np.void, player: int, fields: tuple[str, ...]) -> None:
    for field in fields:
        got = out[field][player]
        want = ref[field][player]
        if np.issubdtype(np.asarray(got).dtype, np.floating):
            assert float(got) == pytest.approx(float(want), abs=2e-6), field
        else:
            np.testing.assert_array_equal(got, want, err_msg=field)


@pytest.mark.integration
def test_dash_iasa_same_action_reentry_keeps_later_attackairhi_contact_on_time() -> None:
    # LIM rec5327 is the causal Dash -> Dash re-entry row for the later rec5433 aerial hit timing:
    # Dash_IASA calls ftCo_Dash_Enter(gobj, 1) without changing action_id. Runtime must still let
    # Dash_Phys consume the entry x0 lane so the visible self_vel/position use the callback-local
    # Dash scalar and the later AttackAirHi contact is not one frame early.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{
    #   ftCo_Dash_IASA,ftCo_Dash_CheckInput,ftCo_Dash_Enter,ftCo_Dash_Phys}
    out, ref = _rollout(5310, 5433)
    p = 0
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 65  # AttackAirHi, no early hit.
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0

    hit_out, hit_ref = _rollout(5310, 5435)
    assert int(hit_out["hitlag"][p]) == int(hit_ref["hitlag"][p]) == 6
    assert int(hit_out["action_id"][p]) == int(hit_ref["action_id"][p]) == 86


@pytest.mark.integration
def test_jumpaerial_attackair_entry_static_platform_uses_source_ecb_lim_4967() -> None:
    # JumpAerial_IASA can enter AttackAir before Fighter_procMap. The following AttackAir_Coll
    # source callback consumes the frame-start JumpAerial CollData/ECB bottom for the static
    # platform sweep, not the newly published AttackAir pose alone.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    seed, out, ref = _step_one(4967)
    p = 0
    assert int(seed["action_id"][p]) == 27  # JumpAerialF
    assert int(ref["action_id"][p]) == 42  # Landing
    _assert_fields_exact(out, ref, p, ("action_id", "animation_index", "on_ground", "ground_id"))
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-6)


@pytest.mark.integration
@pytest.mark.parametrize("record", [1258, 6834])
def test_jumpaerial_attackair_entry_static_platform_respects_active_ecb_lock_feh(record: int) -> None:
    # Active CollData_X130 locks remain source-owned across first-frame JumpAerial -> AttackAir
    # entries. The static-platform source-ECB handoff is admitted only after that lock is clear;
    # otherwise vanilla stays airborne in the entered aerial instead of publishing Landing.
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
    seed, out, ref = _step_one(record, _FEH)
    p = 1
    assert int(seed["action_id"][p]) == 27  # JumpAerialF
    assert int(seed["ecb_lock_timer"][p]) > 0
    assert int(ref["on_ground"][p]) == 0
    _assert_fields_exact(out, ref, p, ("action_id", "animation_index", "on_ground", "ground_id"))


@pytest.mark.integration
def test_expired_lightshield_guardon_same_frame_shine_stays_shield_owned_lim_3561() -> None:
    # A no-submotion GuardOn row with x10 expired can still carry the live lightshield/tilt
    # ShieldDesc owner into same-frame grounded Shine. The collision resolves as shield contact
    # instead of BODY damage; adjacent GuardReflect/Shine BODY controls live in the shared shield
    # test suite.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_GuardOn_Anim,ftCo_800925A4,ftCo_80091E78,ftCo_800928CC}
    seed, out, ref = _step_one(3561)
    defender = 0
    attacker = 1
    assert int(seed["action_id"][defender]) == 178  # GuardOn
    assert int(seed["guard_x10"][defender]) == 0
    assert float(seed["lightshield_amount"][defender]) > 0.0
    _assert_fields_exact(out, ref, defender, ("action_id", "hitlag", "hitstun", "percent"))
    _assert_fields_exact(out, ref, attacker, ("action_id", "hitlag", "hitstun"))


@pytest.mark.integration
def test_jumpaerial_escapeair_no_lock_static_platform_handoff_lim_1085() -> None:
    # LIM rec1085 is the no-lock JumpAerial -> EscapeAir static platform handoff: the source
    # EscapeAir_Coll sweep consumes the carried JumpAerial bottom and accepts the static Yoshi side
    # platform without needing the locked-ECB seed lane.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    out, ref = _rollout(962, 1085)
    p = 1
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 43  # LandingFallSpecial
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-6)


@pytest.mark.integration
@pytest.mark.parametrize(("record", "player"), [(2081, 1), (4896, 0)])
def test_common_damage_landing_slope_initializes_ground_kb_scalar(record: int, player: int) -> None:
    # Common Damage_Coll can enter Landing without the down-damage CCE8 path. On the following
    # procUpdate, source initializes xF0 ground_kb_vel from the live horizontal KB projected through
    # the current floor tangent, keeping Landing's first grounded integration exact on slopes.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    seed, out, ref = _step_one(record)
    assert int(seed["action_id"][player]) == 42  # Landing
    _assert_fields_exact(out, ref, player, ("action_id", "on_ground", "ground_id", "hitlag"))
    assert float(out["pos_x"][player]) == pytest.approx(float(ref["pos_x"][player]), abs=2e-6)
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=2e-6)


@pytest.mark.integration
def test_guardon_release_anim_x10_boundary_enters_guardoff_before_jump_lim_2379() -> None:
    # GuardOn_Anim clears x10 before inlineC0 handles the latched release. With same-frame jump
    # input present, vanilla still exits to GuardOff rather than consuming the jump fallback first.
    # The adjacent previous row keeps GuardOn, proving this is not a stale no-submotion release.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,inlineC0,ftCo_80092C54}
    pre_seed, pre_out, pre_ref = _step_one(2378)
    p = 0
    assert int(pre_seed["action_id"][p]) == 178
    assert int(pre_out["action_id"][p]) == int(pre_ref["action_id"][p]) == 178

    seed, out, ref = _step_one(2379)
    assert int(seed["action_id"][p]) == 178
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 180  # GuardOff
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0


@pytest.mark.integration
def test_attackairn_late_hb0_guardoff_source_clear_suppresses_full_body_lim_2384() -> None:
    # Full LIM rollout used to hit GuardOff with late AttackAirN hb0 after the visible GuardOff
    # instance id advanced. Source still has the same fighter object in HitCapsule.victims_1:
    # AttackAirN preserves x914 through Ft_MF_SkipHit, and the defender's live x18C8 source-clear
    # lane proves this is the same no-new-hit episode. The lock runs from record 0 because shorter
    # reseeds can carry a dense seed lane; the full rollout proves the runtime source-clear fallback.
    # refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_AttackAirN
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_ProcessHit_8006D1EC}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70}
    out, ref = _rollout(0, 2384)
    p = 0
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 180  # GuardOff, no BODY hit.
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=2e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "player", "seed_action", "ref_action"),
    [
        (141, 1, 21, 76),    # strong DAir selected high-cap owner enters DamageAir2.
        (263, 1, 24, 365),   # DamageFly hitlag-exit terminal ledge owner before Firebird.
        (1039, 0, 72, 72),   # DownBound/Guard->Pass floor-owner position remains exact.
        (2449, 1, 212, 213), # CapturePulled clears allow-interrupt before CaptureWait.
        (2787, 0, 87, 87),   # LandingFallSpecial current-floor normal preserves fall timing.
        (2803, 0, 1, 1),     # MissFoot self-velocity carry reaches CliffCatch exactness.
    ],
)
def test_lim_source_owner_boundary_rows_are_replay_exact(
    record: int, player: int, seed_action: int, ref_action: int
) -> None:
    seed, out, ref = _step_one(record)
    assert int(seed["action_id"][player]) == seed_action
    assert int(ref["action_id"][player]) == ref_action
    _assert_fields_exact(
        out,
        ref,
        player,
        ("action_id", "animation_index", "on_ground", "hitlag", "hitstun", "percent"),
    )
    assert float(out["pos_x"][player]) == pytest.approx(float(ref["pos_x"][player]), abs=2e-6)
    assert float(out["pos_y"][player]) == pytest.approx(float(ref["pos_y"][player]), abs=2e-6)
