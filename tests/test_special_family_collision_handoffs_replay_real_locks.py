from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    player: int
    seed_action: int
    ref_action: int
    note: str


@dataclass(frozen=True)
class _FieldCase:
    dataset_rel: str
    record: int
    player: int
    seed_action: int
    field: str
    note: str


def _run_one_step(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > int(record), f"dataset too short for record={record}"
    row = samples[int(record) : int(record) + 1]

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return row["seed_t"][0], row["ref_t1"][0], out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _FieldCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "BlondHardHippopotamus.msl"
            ),
            record=3116,
            player=0,
            seed_action=360,
            field="hurtbox_state",
            note="grounded Shine Start preserves explicit x1990 intangible timer through hitlag",
        ),
        _FieldCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "DistinctCaringCobra.msl"
            ),
            record=6824,
            player=0,
            seed_action=365,
            field="hurtbox_state",
            note="aerial Shine Start preserves explicit x1990 intangible timer through hitlag",
        ),
        _FieldCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "AttachedGoodNaturedGuanaco.msl"
            ),
            record=3145,
            player=0,
            seed_action=360,
            field="hurtbox_state",
            note="grounded Shine Start frame-1 seed preserves hidden x198C after x1988 clears",
        ),
        _FieldCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "AttachedGoodNaturedGuanaco.msl"
            ),
            record=3157,
            player=0,
            seed_action=365,
            field="hurtbox_state",
            note="aerial Shine Start frame-1 seed preserves hidden x198C after x1988 clears",
        ),
        _FieldCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.msl"
            ),
            record=1091,
            player=1,
            seed_action=360,
            field="hurtbox_state",
            note="Run-dispatched Shine Start frame-1 seed preserves hidden x198C after x1988 clears",
        ),
        _FieldCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "AttachedGoodNaturedGuanaco.msl"
            ),
            record=4602,
            player=0,
            seed_action=365,
            field="hurtbox_state",
            note="aerial Shine Start preserves explicit x1990/x1994 hidden timer provenance",
        ),
        _FieldCase(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.msl"
            ),
            record=9599,
            player=0,
            seed_action=50,
            field="state_flags[0]",
            note="AttackDash IASA -> grounded Shine preserves source allow_interrupt bit",
        ),
        _FieldCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "BlondHardHippopotamus.msl"
            ),
            record=7828,
            player=1,
            seed_action=358,
            field="state_flags[1]",
            note="SpecialHiFall anim-end FallSpecial entry preserves Ft_MF_KeepFastFall",
        ),
        _FieldCase(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "TubbyCurlyHerring.msl"
            ),
            record=3367,
            player=1,
            seed_action=358,
            field="state_flags[1]",
            note="SpecialHiFall fastfall flag survives ftCo_80096900 on another replay-real row",
        ),
    ],
    ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.record}-p{c.player}-{c.field}",
)
def test_spacie_special_hurtbox_seed_rows_match_replay(case: _FieldCase) -> None:
    # Decomp owner paths:
    # - Shine Start entry scripts set hit-status and seed x198C/x1990 timer ownership.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    seed, ref, out = _run_one_step(dataset_path, case.record)
    p = int(case.player)

    assert int(seed["action_id"][p]) == int(case.seed_action), case.note
    if case.field.startswith("state_flags["):
        flag_i = int(case.field.removeprefix("state_flags[").removesuffix("]"))
        got = int(out["state_flags"][p, flag_i])
        want = int(ref["state_flags"][p, flag_i])
    else:
        got = int(out[case.field][p])
        want = int(ref[case.field][p])
    assert got == want, (
        f"{case.note}: record={case.record} p={p} field={case.field} expected={want} got={got}"
    )


@pytest.mark.integration
def test_landing_fallspecial_allow_interrupt_seed_lane_replay_real_lock() -> None:
    # Replay-real lock for the hidden FallSpecial -> LandingFallSpecial interrupt carry:
    # TubbyCurlyHerring:865 is already in LandingFallSpecial and consumes grounded Turn through
    # ftCo_Landing_IASA because FallSpecial_Coll forwarded allow_interrupt=true.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096D28
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
    #   ftCo_LandingFallSpecial_Enter,ftCo_Landing_IASA}
    dataset_path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 865
    player = 0
    seed, ref, out = _run_one_step(dataset_path, record)

    assert int(seed["action_id"][player]) == 43  # LandingFallSpecial
    assert int(seed["landing_fallspecial_allow_interrupt"][player]) == 1
    assert int(ref["action_id"][player]) == 18  # Turn
    assert int(out["action_id"][player]) == int(ref["action_id"][player])
    assert int(out["instance_id"][player]) == int(ref["instance_id"][player])


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "AttachedGoodNaturedGuanaco.msl"
            ),
            record=3159,
            player=0,
            seed_action=365,
            ref_action=366,
            note="SpecialAirLwStart -> Loop handoff stays airborne until the loop owner owns the frame",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "AttachedGoodNaturedGuanaco.msl"
            ),
            record=3160,
            player=0,
            seed_action=366,
            ref_action=361,
            note="SpecialAirLwLoop air-to-ground handoff during ECB lock enters grounded Shine loop",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.msl"
            ),
            record=5895,
            player=1,
            seed_action=358,
            ref_action=357,
            note="SpecialHiFall landing refreshes grounded jumps through ftCommon_8007D7FC",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "FavorableSuperficialPig.msl"
            ),
            record=288,
            player=0,
            seed_action=363,
            ref_action=18,
            note="SpecialLwEnd -> Wait destination consumes backward dash flick as TurnSmash",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "TubbyCurlyHerring.msl"
            ),
            record=752,
            player=0,
            seed_action=368,
            ref_action=27,
            note="SpecialAirLwEnd -> Fall destination consumes tap-jump as JumpAerialF",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PriceyPartialAlbatross.msl"
            ),
            record=4178,
            player=0,
            seed_action=366,
            ref_action=367,
            note="aerial Shine Loop reflector callback enters SpecialAirLwHit on reflected laser",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "MotionlessAggressiveJay.msl"
            ),
            record=2005,
            player=0,
            seed_action=361,
            ref_action=362,
            note="grounded Shine Loop reflector item-origin overlap enters SpecialLwHit",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PriceyPartialAlbatross.msl"
            ),
            record=3861,
            player=0,
            seed_action=367,
            ref_action=367,
            note="owned reflected laser does not re-enter SpecialAirLwHit on overlap",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PutridJoyousOryx.msl"
            ),
            record=112,
            player=1,
            seed_action=361,
            ref_action=361,
            note="Shine Loop B-release latch uses pre-input Anim callback ownership",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "BlondHardHippopotamus.msl"
            ),
            record=9887,
            player=1,
            seed_action=20,
            ref_action=20,
            note="Dash IASA does not route Neutral-B through ftCo_800D6824",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "FavorableSuperficialPig.msl"
            ),
            record=7606,
            player=0,
            seed_action=20,
            ref_action=20,
            note="Dash keeps Neutral-B B-edge blocked outside the Side-B branch",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "QuerulousGrandDinosaur.msl"
            ),
            record=1090,
            player=1,
            seed_action=21,
            ref_action=360,
            note="Run IASA consumes SpecialLw before terminal RunBrake",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/debug/fd_mixed_recent/"
                "ImpassionedAlarmedTarsier.msl"
            ),
            record=5809,
            player=0,
            seed_action=236,
            ref_action=236,
            note="sustained EscapeAir ECB-lock floor-hug stays airborne on the floor-bias row",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "DistinctCaringCobra.msl"
            ),
            record=1568,
            player=1,
            seed_action=236,
            ref_action=236,
            note="sustained no-lock EscapeAir current-ECB sampling avoids an early LandingFallSpecial",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "HungryImportantSnake.msl"
            ),
            record=578,
            player=1,
            seed_action=35,
            ref_action=43,
            note="FallSpecial shallow root penetration enters LandingFallSpecial on the post-entry floor row",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "AttachedGoodNaturedGuanaco.msl"
            ),
            record=2378,
            player=0,
            seed_action=236,
            ref_action=43,
            note="EscapeAir prev-ECB-bottom penetration enters LandingFallSpecial on the persisted floor",
        ),
        _Case(
            dataset_rel=(
                "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
                "GracefulAttachedTurtle.msl"
            ),
            record=5224,
            player=1,
            seed_action=236,
            ref_action=43,
            note="locked EscapeAir prev-ECB-bottom penetration lands without broad active-lock grounding",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "HungryImportantSnake.msl"
            ),
            record=1582,
            player=1,
            seed_action=354,
            ref_action=356,
            note="SpecialHiHoldAir anim-end enters aerial launch and consumes all jumps",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "HungryImportantSnake.msl"
            ),
            record=1764,
            player=0,
            seed_action=354,
            ref_action=356,
            note="SpecialHiHoldAir launch jump consumption is replay-exact for the mirrored player",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "HungryImportantSnake.msl"
            ),
            record=559,
            player=1,
            seed_action=356,
            ref_action=359,
            note="SpecialAirHi collision enters SpecialHiBound and remains airborne on the rebound entry row",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "HungryImportantSnake.msl"
            ),
            record=573,
            player=1,
            seed_action=359,
            ref_action=35,
            note="SpecialHiBound anim end enters FallSpecial and consumes jumps while airborne",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "DistinctCaringCobra.msl"
            ),
            record=5790,
            player=0,
            seed_action=357,
            ref_action=15,
            note="SpecialHiLanding anim-end Wait destination consumes Walk IASA in the same proc",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "HilariousVillainousGiraffe.msl"
            ),
            record=6550,
            player=1,
            seed_action=357,
            ref_action=39,
            note="SpecialHiLanding anim-end Wait destination consumes Squat IASA in the same proc",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "ImpassionedAlarmedTarsier.msl"
            ),
            record=8431,
            player=1,
            seed_action=357,
            ref_action=18,
            note="SpecialHiLanding anim-end Wait destination consumes Turn IASA in the same proc",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "BlondHardHippopotamus.msl"
            ),
            record=666,
            player=1,
            seed_action=343,
            ref_action=20,
            note="SpecialNEnd anim-end Wait destination consumes buttonless forward Dash IASA",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PriceyPartialAlbatross.msl"
            ),
            record=916,
            player=0,
            seed_action=203,
            ref_action=365,
            note="PassiveWallJump IASA consumes aerial down-B through SpecialAir before AttackAir/item checks",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "ImpassionedAlarmedTarsier.msl"
            ),
            record=4005,
            player=0,
            seed_action=360,
            ref_action=365,
            note="SpecialLwStart ground-to-air collision consumes one jump through ftCommon_8007D5D4",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "TubbyCurlyHerring.msl"
            ),
            record=7232,
            player=1,
            seed_action=27,
            ref_action=344,
            note="JumpAerialF A+B row consumes SpecialAirN before AttackAirN in common aerial IASA",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "ImpassionedAlarmedTarsier.msl"
            ),
            record=140,
            player=1,
            seed_action=346,
            ref_action=28,
            note="SpecialAirNEnd anim-end Fall destination consumes same-proc JumpAerialB IASA",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "BlondHardHippopotamus.msl"
            ),
            record=4880,
            player=1,
            seed_action=358,
            ref_action=358,
            note="SpecialHiFall cannot CliffCatch an occupied slow-climb ledge",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "TubbyCurlyHerring.msl"
            ),
            record=6743,
            player=1,
            seed_action=358,
            ref_action=252,
            note="SpecialHiFall still CliffCatches an unoccupied ledge",
        ),
    ],
    ids=lambda c: f"{Path(c.dataset_rel).stem}-rec{c.record}-p{c.player}",
)
def test_spacie_special_collision_handoff_rows_match_replay(case: _Case) -> None:
    # Decomp owner paths:
    # - Shine: ftFx_SpecialAirLwLoop_Coll -> ft_80081D0C ->
    #   ftFx_SpecialAirLwLoop_AirToGround. SpecialAirLwStart_Anim can enter Loop before collision,
    #   so the locked-bottom floor path is limited to rows that started the frame in Loop/End.
    # - Firefox: ftFx_SpecialHiFall_Coll -> ftFx_SpecialHiFall_Enter, which calls
    #   ftCommon_8007D7FC before entering SpecialHiLanding.
    # - Shine End: ftFx_Special{Air,}LwEnd_Anim calls ftCommon_8007D92C, and the destination
    #   Wait/Fall IASA can consume TurnSmash or JumpAerial input later in the same fighter proc.
    # - Shine reflector contact: ftColl_80077464 sets ReflectAttr.x1A2C_reflectHitDirection, then
    #   Fighter_ProcessHit calls ftFx_SpecialLwHit_Enter through reflect_hit_cb. The overlap is
    #   item-owned and includes the projectile-origin segment, not only laser attack offsets.
    # - Shine B-release latch is an Anim-callback side effect and reads the pre-input snapshot.
    # - EscapeAir floor-hug lock: Dolphin capture
    #   reports/triage/20260418T081729Z_dolphin_forensic_row confirms vanilla keeps
    #   ground_or_air=Air while root Y is on the floor bias under the active ECB lock.
    # - FallSpecial_Coll uses ft_80083090 -> ftCo_80096D28 to enter LandingFallSpecial on shallow
    #   same-floor penetration.
    # - EscapeAir_Coll uses ft_80082C74 with CollData floor.index; one-step reseeds that start
    #   after prev ECB bottom has already crossed the persisted floor can enter LandingFallSpecial,
    #   while active-lock rows whose prev ECB bottom is still above the floor remain airborne.
    # - SpecialAirHi_Coll can enter SpecialHiBound, and ftFx_SpecialHiBound_Anim enters FallSpecial
    #   while consuming all jumps on airborne anim end.
    # - SpecialHiHoldAir_Anim enters ftFx_SpecialAirHi_Enter, whose launch handler consumes all
    #   jumps through x1968_jumpsUsed=max_jumps.
    # - SpecialHiLanding_Anim enters Wait during the Anim callback, then destination Wait_IASA can
    #   consume grounded locomotion input later in the same proc.
    # - SpecialNEnd_Anim exits through ft_8008A2BC; the same destination Wait_IASA can consume the
    #   buttonless forward Dash_CheckInput branch in the same proc.
    # - SpecialAirNEnd_Anim exits through ftCo_Fall_Enter when blaster landing lag is zero; the
    #   destination Fall IASA can consume JumpAerial input later in the same proc.
    # - Ledge occupancy blocks another fighter's CliffCatch through ftCliffCommon_80081298;
    #   slow and quick ledge options both set the occupancy bit, while CliffJump2 no longer uses
    #   the attach snap.
    # - Shine ground->air collision handoffs call ftCommon_8007D5D4 and consume one jump while
    #   preserving the current Shine phase/frame.
    # - Common aerial IASA owners run ftCo_SpecialAir_CheckInput before AttackAir/item checks, so
    #   JumpAerial/PassiveWallJump B-edge rows remain available for Shine/Blaster dispatch.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLwLoop_Coll
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLwEnd_Anim,ftFx_SpecialAirLwEnd_Anim,ftFx_SpecialLwHit_Enter}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiFall_Coll,ftFx_SpecialHiFall_Enter,ftFx_SpecialAirHi_Coll,
    #   ftFx_SpecialHiBound_Enter,ftFx_SpecialHiBound_Anim,ftFx_SpecialHiLanding_Anim,
    #   ftFx_SpecialHiHoldAir_Anim,ftFx_SpecialAirHi_Enter}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNEnd_Anim
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNEnd_Anim
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLwStart_GroundToAir,ftFx_SpecialLwLoop_GroundToAir,
    #   ftFx_SpecialLwHit_GroundToAir,ftFx_SpecialLwEnd_GroundToAir,
    #   ftFx_SpecialLwTurn_GroundToAir}
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter_Basic
    # refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffJump.c
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    #   ftCo_FallSpecial_Coll,ftCo_80096D28}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    seed, ref, out = _run_one_step(dataset_path, case.record)
    p = int(case.player)

    assert int(seed["action_id"][p]) == int(case.seed_action), case.note
    assert int(ref["action_id"][p]) == int(case.ref_action), case.note
    assert int(seed["hitlag"][p]) == 0, case.note
    assert int(seed["hitstun"][p]) == 0, case.note

    for field in ("action_id", "action_frame", "animation_index", "on_ground", "ground_id", "jumps_left"):
        assert int(out[field][p]) == int(ref[field][p]), (
            f"{case.note}: record={case.record} p={p} field={field} "
            f"expected={int(ref[field][p])} got={int(out[field][p])}"
        )
