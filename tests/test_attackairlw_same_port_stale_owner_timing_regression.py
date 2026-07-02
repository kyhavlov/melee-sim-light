from __future__ import annotations

from pathlib import Path
import subprocess
import textwrap

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE, SEED_DTYPE
from tests.replay_buffers_loader import load_replay_buffers
from tests.replay_buffers_loader import load_replay_buffers

HIT_GROUNDED = 1 << 9
HIT_AERIAL = 1 << 10

ACT_WAIT = 0x000E
ACT_ATTACK_HI3 = 0x0038
ACT_ATTACK_HI4 = 0x003F
ACT_DAMAGE_FLY_HI = 0x0057
ACT_DAMAGE_FLY_N = 0x0058
ACT_DAMAGE_FLY_LW = 0x0059
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_DAMAGE_FLY_ROLL = 0x005B

SM_WAIT1_0 = 2
SM_ATTACK_HI3 = 58
SM_DAMAGE_FLY_HI = 177
SM_DAMAGE_FLY_N = 178
SM_DAMAGE_FLY_LW = 179
SM_DAMAGE_FLY_TOP = 180
SM_DAMAGE_FLY_ROLL = 181

CHAR_FOX = 1
CHAR_FALCO = 22
STAGE_FD = 32
FOX_TAIL_CAP_ID = 12
FOX_NON_TAIL_CAP_ID = 0


def _step_one_sample(row: np.ndarray, *, num_players: int) -> tuple[np.void, np.void]:
    return _step_one_sample_with_reseed(row, num_players=num_players, rollout_reseed=False)


def _step_one_sample_rollout_reseed(row: np.ndarray, *, num_players: int) -> tuple[np.void, np.void]:
    return _step_one_sample_with_reseed(row, num_players=num_players, rollout_reseed=True)


def _step_one_sample_with_reseed(
    row: np.ndarray, *, num_players: int, rollout_reseed: bool
) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert int(row.shape[0]) == 1

    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        ).copy()
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        if rollout_reseed:
            binding.reseed_seed_rollout(handle, seed_bytes)
        else:
            binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        ref = row["ref_t1"][0]
        return out, ref
    finally:
        binding.destroy(handle)


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void]:
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    return _step_one_sample(row, num_players=int(ds.num_players))


def _debug_attackhi3_terminal_damagefly_body_hit_applied(
    *,
    defender_char: int = CHAR_FOX,
    defender_action: int = ACT_DAMAGE_FLY_TOP,
    defender_motion: int = SM_DAMAGE_FLY_TOP,
    defender_cap_id: int = FOX_TAIL_CAP_ID,
    enable_edge: bool = True,
    same_attacker_action: bool = True,
) -> bool:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["match_damage_ratio"][0] = np.float32(1.0)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, 0] = np.uint8(CHAR_FALCO)
    seed["char_id"][0, 1] = np.uint8(defender_char)
    seed["attack_ratio"][0, :2] = np.float32(1.0)
    seed["defense_ratio"][0, :2] = np.float32(1.0)
    seed["fighter_scale_y"][0, :2] = np.float32(1.0)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["on_ground"][0, 1] = np.uint8(0)
    seed["ground_id"][0, 0] = np.uint16(0)
    seed["ground_id"][0, 1] = np.uint16(0xFFFF)
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_HI3)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_ATTACK_HI3 if same_attacker_action else ACT_WAIT)
    seed["animation_index"][0, 0] = np.uint32(SM_ATTACK_HI3)
    seed["action_frame"][0, 0] = np.int16(4)
    seed["anim_frame_f32"][0, 0] = np.float32(4.0)
    seed["action_id"][0, 1] = np.uint16(defender_action)
    seed["seed_prev_action_id"][0, 1] = np.uint16(defender_action)
    seed["animation_index"][0, 1] = np.uint32(defender_motion)
    seed["action_frame"][0, 1] = np.int16(54)
    seed["anim_frame_f32"][0, 1] = np.float32(54.0)
    seed["frame_speed_mul_f32"][0, :2] = np.float32(1.0)
    seed["instance_id"][0, 0] = np.uint16(111)
    seed["instance_id"][0, 1] = np.uint16(222)
    seed["instance_hit_by"][0, 1] = np.uint16(77)

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        binding.debug_set_prev_action_id(
            handle, 0, 0, ACT_ATTACK_HI3 if same_attacker_action else ACT_WAIT
        )
        binding.debug_set_prev_action_id(handle, 0, 1, defender_action)
        binding.debug_clear_hitboxes_world(handle, 0, 0)
        binding.debug_clear_hurtcaps_world(handle, 0, 1)
        binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 9.0, 1)
        binding.debug_set_hitbox_flags(handle, 0, 0, 0, HIT_GROUNDED | HIT_AERIAL)
        binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
        binding.debug_set_hitbox_enable_edge(handle, 0, 0, 0, 1 if enable_edge else 0)
        binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 90, 100, 0, 20)
        binding.debug_set_hurtcap_world(
            handle, 0, 1, defender_cap_id, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5
        )
        binding.debug_set_hurtcap_height(handle, 0, 1, defender_cap_id, 1)
        binding.debug_combat_resolve(handle)
        out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
        return int(out["hitlag"][0]) > 0 and int(out["hitlag"][1]) > 0
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_attackairlw_same_port_stale_owner_rehit_lands_on_replay_frame() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/"
        / "AttachedGoodNaturedGuanaco.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))

    # Replay-real timing lock for the AttackAirLw stale-owner bridge:
    # - Falco Dair keeps same-group hitboxes live across the active window while hitlist ownership
    #   is rewired on create/copy lanes in ftAction_8007121C / ftColl_800768A0.
    # - Teacher-forced reseed only carries dense victim presence + instance_hit_by, so same-port
    #   stale suppression must not fire a frame early on the continuing DamageFlyTop victim.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
    cases = [
        (7047, 1, 90, 0, 8, [192, 0, 0, 2, 0]),
        (7048, 1, 86, 7, 28, [192, 48, 0, 2, 0]),
        (7049, 1, 86, 6, 28, [192, 48, 0, 2, 0]),
    ]

    for record, p, exp_action, exp_hitlag, exp_hitstun, exp_flags in cases:
        assert int(ds.rows.shape[0]) > record, f"replay too short for rec={record}"
        out, ref = _step_one_row(dataset_path, record)

        assert int(ref["action_id"][p]) == exp_action
        assert int(ref["hitlag"][p]) == exp_hitlag
        assert int(ref["hitstun"][p]) == exp_hitstun
        assert [int(x) for x in ref["state_flags"][p]] == exp_flags

        assert int(out["action_id"][p]) == exp_action
        assert int(out["hitlag"][p]) == exp_hitlag
        assert int(out["hitstun"][p]) == exp_hitstun
        assert [int(x) for x in out["state_flags"][p]] == exp_flags


@pytest.mark.integration
def test_attackairlw_damageflytop_fox_tail_pose_rejects_cdo_terminal_hit() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "replays/validation/pokemon_stadium_recent/"
        / "CornyDelayedOkapi.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    record = 5241
    assert int(ds.rows.shape[0]) > record, f"replay too short for rec={record}"
    row = ds.rows[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]

    # CDO:5241 is a terminal Fox DamageFlyTop frame under p1 AttackAirLw. The filtered BODY
    # candidate is Fox cap12 on FtPart 18; the dynamic tail-chain pose owner rejects that stale
    # tail contact instead of using a broad dense-hitlist latch that would also suppress valid Falco
    # DamageFlyTop contacts.
    assert int(seed["action_id"][1]) == 69  # AttackAirLw
    assert int(seed["action_id"][0]) == 90  # DamageFlyTop
    assert int(seed["hitstun"][0]) > 0
    assert int(seed["hitlag"][0]) == 0
    assert int(seed["combat_hitlist_cd"][1, 0, 0]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][1, 0, 0]) == int(seed["instance_id"][0])
    assert int(seed["last_hit_by"][0]) == 1
    assert int(seed["instance_hit_by"][0]) != int(seed["instance_id"][1])
    assert int(ref["action_id"][0]) == 90
    assert int(ref["hitlag"][0]) == 0
    assert int(ref["hitstun"][0]) == int(seed["hitstun"][0]) - 1

    out, ref = _step_one_row(dataset_path, record)
    for p in (0, 1):
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
        assert int(out["instance_hit_by"][p]) == int(ref["instance_hit_by"][p])
        assert int(out["last_attack_landed"][p]) == int(ref["last_attack_landed"][p])


@pytest.mark.integration
def test_attackairlw_damageflytop_falco_tail_neighbor_still_hits_hvg() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "replays/validation/aggregate_recent/HilariousVillainousGiraffe.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    record = 9277
    assert int(ds.rows.shape[0]) > record, f"replay too short for rec={record}"
    row = ds.rows[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]
    attacker = 1
    defender = 0

    assert int(seed["char_id"][defender]) == 22  # Falco, not Fox part-18 tail chain.
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw
    assert int(seed["action_id"][defender]) == 90  # DamageFlyTop
    assert int(seed["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][attacker, 0, defender]) == int(seed["instance_id"][defender])
    assert all(int(seed["combat_hitlist_hb_valid"][attacker, hb]) == 0 for hb in range(4))
    assert int(ref["action_id"][defender]) == 88  # DamageFlyLw from the fresh DAir BODY hit.
    assert int(ref["hitlag"][attacker]) > 0
    assert int(ref["hitlag"][defender]) > 0

    out, ref = _step_one_row(dataset_path, record)
    for p in (attacker, defender):
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
        assert int(out["instance_hit_by"][p]) == int(ref["instance_hit_by"][p])
        assert int(out["last_attack_landed"][p]) == int(ref["last_attack_landed"][p])


@pytest.mark.parametrize(
    ("defender_action", "defender_motion"),
    [
        (ACT_DAMAGE_FLY_HI, SM_DAMAGE_FLY_HI),
        (ACT_DAMAGE_FLY_N, SM_DAMAGE_FLY_N),
        (ACT_DAMAGE_FLY_LW, SM_DAMAGE_FLY_LW),
        (ACT_DAMAGE_FLY_TOP, SM_DAMAGE_FLY_TOP),
        (ACT_DAMAGE_FLY_ROLL, SM_DAMAGE_FLY_ROLL),
    ],
)
def test_attackhi3_create_edge_terminal_damagefly_family_tail_suppresses_body(
    defender_action: int, defender_motion: int
) -> None:
    # Source-shaped synthetic lock for the broader MSLMSO01 DamageFly family:
    # terminal damage callback ownership is not Top-specific, but the current runtime correction is
    # limited to Fox's part-18 dynamic tail chain and a same-action ftAction_8007121C enable edge.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008F744,ftCo_DamageFly_IASA}
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    assert (
        _debug_attackhi3_terminal_damagefly_body_hit_applied(
            defender_action=defender_action,
            defender_motion=defender_motion,
            defender_cap_id=FOX_TAIL_CAP_ID,
            enable_edge=True,
            same_attacker_action=True,
        )
        is False
    )


@pytest.mark.parametrize(
    ("case", "kwargs"),
    [
        ("already-live same hitbox", {"enable_edge": False}),
        ("non-Fox target", {"defender_char": CHAR_FALCO}),
        ("non-tail Fox cap", {"defender_cap_id": FOX_NON_TAIL_CAP_ID}),
        ("same-frame new action entry", {"same_attacker_action": False}),
    ],
)
def test_attackhi3_terminal_damagefly_boundary_controls_stay_on_body_path(
    case: str, kwargs: dict[str, object]
) -> None:
    assert _debug_attackhi3_terminal_damagefly_body_hit_applied(**kwargs) is True, case


def test_terminal_fall_from_damage_owner_does_not_require_concrete_cap(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[1]
    source = tmp_path / "damage_terminal_owner_capless_test.c"
    exe = tmp_path / "damage_terminal_owner_capless_test"
    source.write_text(
        textwrap.dedent(
            """
            #include <assert.h>
            #include <string.h>

            #include "batch_internal.h"
            #include "damage_terminal_owner.h"

            static MslBatch batch;
            static uint16_t action_id[MSL_MAX_PLAYERS];
            static uint16_t prev_action_id[MSL_MAX_PLAYERS];
            static uint16_t hitlag[MSL_MAX_PLAYERS];
            static uint16_t hitstun[MSL_MAX_PLAYERS];
            static uint16_t instance_hit_by[MSL_MAX_PLAYERS];
            static uint8_t char_id[MSL_MAX_PLAYERS];
            static uint8_t hitbox_enable_edge[MSL_MAX_PLAYERS * MSL_MAX_HITBOXES];

            uint8_t msl_motion_state_common_class_has(uint16_t action, uint32_t class_bit) {
              if (class_bit != MSL_MS_CLASS_DAMAGE_FLY) {
                return 0u;
              }
              return (action >= (uint16_t)MSL_ACT_DAMAGE_FLY_HI &&
                      action <= (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL)
                         ? 1u
                         : 0u;
            }

            static void bind_state(void) {
              memset(&batch, 0, sizeof(batch));
              batch.state.action_id = action_id;
              batch.state.prev_action_id = prev_action_id;
              batch.state.hitlag = hitlag;
              batch.state.hitstun = hitstun;
              batch.state.instance_hit_by = instance_hit_by;
              batch.state.char_id = char_id;
              batch.state.hitbox_enable_edge = hitbox_enable_edge;
            }

            int main(void) {
              bind_state();
              batch.state.hitbox_enable_edge[0] = 1u;
              batch.state.action_id[0] = (uint16_t)MSL_ACT_ATTACK_HI3;
              batch.state.prev_action_id[0] = (uint16_t)MSL_ACT_ATTACK_HI3;
              batch.state.action_id[1] = (uint16_t)MSL_ACT_FALL;
              batch.state.prev_action_id[1] = (uint16_t)MSL_ACT_DAMAGE_FALL;

              assert(msl_damage_owner_terminal_state_blocks_enable_edge_body(
                         &batch, 0u, 0u, 1u, 0u, 0u) == 1u);

              batch.state.prev_action_id[1] = (uint16_t)MSL_ACT_DAMAGE_FLY_TOP;
              assert(msl_damage_owner_terminal_state_blocks_enable_edge_body(
                         &batch, 0u, 0u, 1u, 0u, 0u) == 1u);

              batch.state.prev_action_id[1] = (uint16_t)MSL_ACT_WAIT;
              assert(msl_damage_owner_terminal_state_blocks_enable_edge_body(
                         &batch, 0u, 0u, 1u, 0u, 0u) == 0u);

              batch.state.action_id[1] = (uint16_t)MSL_ACT_DAMAGE_FLY_TOP;
              batch.state.prev_action_id[1] = (uint16_t)MSL_ACT_DAMAGE_FLY_TOP;
              batch.state.char_id[1] = (uint8_t)MSL_CHAR_ID_FOX;
              batch.state.instance_hit_by[1] = 77u;

              assert(msl_damage_owner_terminal_state_blocks_enable_edge_body(
                         &batch, 0u, 0u, 1u, 0u, 0u) == 0u);
              assert(msl_damage_owner_terminal_state_blocks_enable_edge_body(
                         &batch, 0u, 0u, 1u, MSL_DAMAGE_OWNER_FOX_DYNAMIC_TAIL_PART_ID, 1u) == 1u);

              return 0;
            }
            """
        ),
        encoding="utf-8",
    )

    subprocess.run(
        ["cc", "-std=c11", "-I", str(root / "src"), str(source), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)


@pytest.mark.integration
def test_falco_attackairlw_same_slot_late_payload_preserves_victim_latch_pec_rollout_reseed() -> None:
    # Falco DAir's late hit payload (MSLFTSC1 frame 15) updates already-enabled hitbox slots with
    # the same hit_group. Decomp `ftAction_8007121C` calls `ftColl_800768A0` only when a slot is
    # disabled or changes hit_group, so this same-slot payload must preserve the existing
    # HitCapsule.victims_1 latch. Legacy dense group seeds initialize that hidden HitCapsule
    # provenance during reseed when the script payload is not replayed in the step; the later
    # authoritative-empty per-HitCapsule seed owns the real hit boundary.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
    # data/scripts/falco.bin (MSLFTSC1 ftCo_SM_AttackAirLw frame-15 create payload)
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/yoshis_story_recent/PhysicalElectricCapybara.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = load_replay_buffers(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    samples = ds.rows
    num_players = int(ds.num_players)
    attacker = 0
    defender = 1

    for record in (341, 342, 343):
        seed_t = samples[record]["seed_t"]
        ref_t1 = samples[record]["ref_t1"]
        assert int(seed_t["char_id"][attacker]) == CHAR_FALCO
        assert int(seed_t["action_id"][attacker]) == 69  # AttackAirLw
        assert int(seed_t["action_id"][defender]) == 63  # AttackHi4
        assert int(seed_t["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
        assert all(int(v) == 0 for v in seed_t["combat_hitlist_hb_valid"][attacker])
        assert int(ref_t1["hitlag"][defender]) == 0
        assert int(ref_t1["hitstun"][defender]) == 0

        out, ref = _step_one_sample_rollout_reseed(
            samples[record : record + 1], num_players=num_players
        )
        for field in ("action_id", "hitlag", "hitstun", "percent"):
            assert out[field][defender] == ref[field][defender], f"record={record} field={field}"

    hit_record = 344
    seed_t = samples[hit_record]["seed_t"]
    assert any(int(seed_t["combat_hitlist_hb_valid"][attacker, hb]) != 0 for hb in (0, 1))
    for hb in (0, 1):
        assert int(seed_t["combat_hitlist_hb_cd"][attacker, hb, defender]) == 0

    out, ref = _step_one_sample_rollout_reseed(
        samples[hit_record : hit_record + 1], num_players=num_players
    )
    assert int(ref["hitlag"][attacker]) > 0
    assert int(ref["hitlag"][defender]) > 0
    # This lock is for the HitCapsule victims_1 boundary: the hit must be suppressed through the
    # no-clear same-slot payload frames above, then admitted at the first per-HitCapsule empty-seed
    # frame with the same damage/hitlag/hitstun. The exact grounded Damage* direction on the hit
    # frame is owned by the BODY/hurtcap selection path, not by this HitCapsule-lifetime test.
    assert int(out["action_id"][defender]) != ACT_ATTACK_HI4
    assert int(ref["action_id"][defender]) != ACT_ATTACK_HI4
    for field in ("hitlag", "hitstun", "percent", "last_attack_landed"):
        assert out[field][defender] == ref[field][defender], f"hit field={field}"


@pytest.mark.integration
def test_attackhi3_create_edge_waits_one_frame_on_terminal_damageflytop_qgd() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/"
        / "QuerulousGrandDinosaur.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    no_hit_record = 7173
    next_hit_record = 7174
    assert int(ds.rows.shape[0]) > next_hit_record, "replay too short for QGD AttackHi3 lock"

    no_hit = ds.rows[no_hit_record]
    assert int(no_hit["seed_t"]["action_id"][0]) == 56  # AttackHi3
    assert int(no_hit["seed_t"]["action_frame"][0]) == 4
    assert int(no_hit["seed_t"]["action_id"][1]) == 90  # DamageFlyTop
    assert int(no_hit["seed_t"]["hitlag"][1]) == 0
    assert int(no_hit["seed_t"]["hitstun"][1]) == 0
    assert int(no_hit["seed_t"]["instance_hit_by"][1]) != 0
    assert int(no_hit["ref_t1"]["hitlag"][0]) == 0
    assert int(no_hit["ref_t1"]["hitlag"][1]) == 0
    assert int(no_hit["ref_t1"]["hitstun"][1]) == 0

    out, ref = _step_one_row(dataset_path, no_hit_record)
    assert int(out["action_id"][1]) == int(ref["action_id"][1])
    assert int(out["hitlag"][0]) == 0
    assert int(out["hitlag"][1]) == 0
    assert int(out["hitstun"][1]) == 0
    assert float(out["percent"][1]) == float(ref["percent"][1])

    # On the following replay-visible frame the same up-tilt capsules are already live, so the
    # terminal DamageFlyTop target returns to the ordinary ftColl_80078C70 BODY path.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    next_hit = ds.rows[next_hit_record]
    assert int(next_hit["seed_t"]["action_id"][0]) == 56  # AttackHi3
    assert int(next_hit["seed_t"]["action_frame"][0]) == 5
    assert int(next_hit["seed_t"]["action_id"][1]) == 90  # DamageFlyTop
    assert int(next_hit["seed_t"]["hitlag"][1]) == 0
    assert int(next_hit["seed_t"]["hitstun"][1]) == 0
    assert int(next_hit["ref_t1"]["hitlag"][0]) > 0
    assert int(next_hit["ref_t1"]["hitlag"][1]) > 0
    assert int(next_hit["ref_t1"]["hitstun"][1]) > 0

    out, ref = _step_one_row(dataset_path, next_hit_record)
    assert int(out["hitlag"][0]) == int(ref["hitlag"][0])
    assert int(out["hitlag"][1]) == int(ref["hitlag"][1])
    assert int(out["hitstun"][1]) == int(ref["hitstun"][1])
    assert float(out["percent"][1]) == float(ref["percent"][1])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "attacker", "defender", "defender_action"),
    [
        (
            "replays/validation/aggregate_recent/"
            "BlondHardHippopotamus.slpz",
            5412,
            0,
            1,
            76,  # DamageHi3
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz",
            1848,
            0,
            1,
            76,  # DamageHi3
        ),
    ],
)
def test_attackairlw_authoritative_empty_hb_seed_allows_same_source_damage_followup(
    dataset_rel: str, record: int, attacker: int, defender: int, defender_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    assert int(ds.rows.shape[0]) > record, f"replay too short for rec={record}"
    row = ds.rows[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]

    # The dense group lane still names the defender from the previous same-source hit, but the
    # per-HitCapsule lane is authoritative-empty for the live DAir slots. That empty seed owns the
    # boundary and must not be backfilled from BODY attribution.
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw
    assert int(seed["action_id"][defender]) == defender_action
    assert int(seed["hitstun"][defender]) > 0
    assert int(seed["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][attacker, 0, defender]) == int(seed["instance_id"][defender])
    assert int(seed["last_hit_by"][defender]) == int(seed["source_port0"][attacker])
    assert any(int(seed["combat_hitlist_hb_valid"][attacker, hb]) != 0 for hb in range(4))
    for hb in range(4):
        if int(seed["combat_hitlist_hb_valid"][attacker, hb]) != 0:
            assert int(seed["combat_hitlist_hb_cd"][attacker, hb, defender]) == 0
    assert int(ref["hitlag"][attacker]) > 0
    assert int(ref["hitlag"][defender]) > 0

    out, ref = _step_one_row(dataset_path, record)
    for p in (attacker, defender):
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
        assert int(out["combo_count"][p]) == int(ref["combo_count"][p])
        assert [int(x) for x in out["state_flags"][p]] == [int(x) for x in ref["state_flags"][p]]
