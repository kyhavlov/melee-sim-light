from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp


SELFPLAY_181413_SLP = Path("replays/validation/aggregate_recent/Game_20260514T181413.slpz")


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


def _run_one_step(dataset_path: Path, record: int, *, seed_mutator=None) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > int(record), f"dataset too short for record={record}"
    row = samples[int(record) : int(record) + 1]
    seed_t = row["seed_t"].copy()
    if seed_mutator is not None:
        seed_mutator(seed_t)

    seed_bytes = np.frombuffer(seed_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
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

    return seed_t[0], row["ref_t1"][0], out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


def _run_replay_rollout_row(dataset_path: Path, start_record: int, end_record: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > int(end_record), f"dataset too short for record={end_record}"
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(
            samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
        ).copy().reshape(1, seed_stride)
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, end_record + 1):
            row = samples[record : record + 1]
            frame_seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, seed_stride
            )
            prev_input_bytes = np.frombuffer(
                row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).copy().reshape(1, input_stride)
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            binding.step_input_replay_frame_rng(handle, frame_seed_bytes, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return samples[end_record]["ref_t1"], out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


def _run_one_step_from_slp(slp_path: Path, record: int, *, ports: list[int]) -> tuple[np.void, np.void, np.void]:
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    binding = pytest.importorskip("msl_binding")
    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=ports,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    samples = ds.samples
    assert int(samples.shape[0]) > int(record), f"dataset too short for record={record}"
    row = samples[int(record) : int(record) + 1]

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
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

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return row["seed_t"][0], row["ref_t1"][0], out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


@pytest.mark.integration
def test_specialairnloop_hidden_loop_latch_uses_same_action_restart_provenance_feh() -> None:
    # ftFx_SpecialAirNLoop_Anim reads mv.fx.SpecialN.isBlasterLoop before current-frame IASA can
    # set the next latch. The latch is live hidden runtime state: FEH:216 presses B inside cmd0,
    # FEH:228 consumes that earlier latch and restarts Loop, while FEH:2518 has the same terminal
    # held-B/x67D shape but no earlier live latch and enters End. Do not use the generic
    # motion_entry_instance_id_override lane as latch proof; landing/entry rows also use that lane.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialAirNLoop_Anim,ftFx_SpecialAirNLoop_IASA,ftFx_SpecialN_OnChangeAction}
    # refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_80089824
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/dream_land_recent/FlippantEnchantedHorse.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    restart_seed, restart_ref, restart_out = _run_one_step(dataset_path, 228)
    p = 1
    assert int(restart_seed["action_id"][p]) == 345  # SpecialAirNLoop
    assert int(restart_seed["x67D"][p]) == 1
    assert int(restart_seed["specialn_blaster_loop_requested"][p]) == 1
    assert int(restart_ref["action_id"][p]) == 345
    assert int(restart_ref["action_frame"][p]) == 0
    assert int(restart_out["action_id"][p]) == int(restart_ref["action_id"][p])
    assert int(restart_out["action_frame"][p]) == int(restart_ref["action_frame"][p])
    assert int(restart_out["instance_id"][p]) == int(restart_ref["instance_id"][p])

    rollout_ref, rollout_out = _run_replay_rollout_row(dataset_path, 216, 228)
    assert int(rollout_out["action_id"][p]) == int(rollout_ref["action_id"][p])
    assert int(rollout_out["action_frame"][p]) == int(rollout_ref["action_frame"][p])

    end_seed, end_ref, end_out = _run_one_step(dataset_path, 2518)
    assert int(end_seed["action_id"][p]) == 345
    assert int(end_seed["x67D"][p]) == 1
    assert int(end_seed["motion_entry_instance_id_override_u16"][p]) == 0
    assert int(end_seed["specialn_blaster_loop_requested"][p]) == 0
    assert int(end_ref["action_id"][p]) == 346
    assert int(end_out["action_id"][p]) == int(end_ref["action_id"][p])


@pytest.mark.integration
def test_specialnloop_seed_latch_uses_raw_cmd0_window_not_tail_rws() -> None:
    # Falco grounded Blaster Loop in a Marth-suite replay:
    # RWS:10773 starts on the terminal Loop frame after a late B edge. The runtime helper keeps a
    # short cmd0 latch-clear tail for live IASA convenience, but one-step replay reconstruction must
    # use the raw MSLFTSC1 cmd_var[0] interval. After the source clear frame, a B edge does not prove
    # mv.fx.SpecialN.isBlasterLoop was set before Loop_Anim reaches anim-end, so vanilla enters End.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialNLoop_Anim,ftFx_SpecialNLoop_IASA}
    # data/scripts/falco.bin (MSLFTSC1 msid 296 set_cmd_var idx=0 at frames 8..22).
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/marth/replays/validation/marth/RipeWealthySeahorse.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    seed, ref, out = _run_one_step(dataset_path, 10773)
    p = 1
    assert int(seed["char_id"][p]) == 22  # Falco, not the Marth player in this replay.
    assert int(seed["action_id"][p]) == 342  # ftFx_MS_SpecialNLoop / ftFc_MS_SpecialNLoop.
    assert int(seed["animation_index"][p]) == 296
    assert int(seed["action_frame"][p]) == 23
    assert int(seed["x67D"][p]) == 0
    assert int(seed["specialn_blaster_loop_requested"][p]) == 0

    assert int(ref["action_id"][p]) == 343
    assert int(ref["animation_index"][p]) == 297
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])


@pytest.mark.integration
@pytest.mark.parametrize("record,player", [(867, 1), (986, 0)])
def test_specialhifall_landing_carries_self_velocity_to_ground_speed_selfplay_181413(
    record: int, player: int
) -> None:
    # SpecialHiFall_Coll -> ftFx_SpecialHiFall_Enter calls ftCommon_8007D7FC before entering
    # SpecialHiLanding. That helper delegates to ftCommon_8007D6A4, which sets gr_vel from the
    # current self_vel.x on the landing frame.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiFall_Coll,ftFx_SpecialHiFall_Enter}
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
    seed, ref, out = _run_one_step_from_slp(SELFPLAY_181413_SLP, record, ports=[1, 2])
    p = int(player)
    assert int(seed["action_id"][p]) == 358  # SpecialHiFall
    assert int(ref["action_id"][p]) == 357  # SpecialHiLanding
    assert int(seed["on_ground"][p]) == 0
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1

    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
    assert float(out["speed_air_x_self"][p]) == pytest.approx(float(ref["speed_air_x_self"][p]), abs=1e-6)
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=1e-6
    )


@pytest.mark.integration
@pytest.mark.parametrize("record,player", [(868, 1), (987, 0)])
def test_specialhilanding_phys_clears_small_landing_ground_velocity_selfplay_181413(
    record: int, player: int
) -> None:
    # The following SpecialHiLanding Phys frame applies ftFox_DatAttrs.x7C as ground friction.
    # For these carried landing velocities, that friction exceeds |gr_vel| and clears it before
    # ground movement.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Phys
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_ApplyFrictionGround
    seed, ref, out = _run_one_step_from_slp(SELFPLAY_181413_SLP, record, ports=[1, 2])
    p = int(player)
    assert int(seed["action_id"][p]) == 357  # SpecialHiLanding
    assert int(ref["action_id"][p]) == 357
    assert float(seed["speed_ground_x_self"][p]) != pytest.approx(0.0, abs=1e-7)

    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(0.0, abs=1e-7)
    assert float(out["speed_ground_x_self"][p]) == pytest.approx(
        float(ref["speed_ground_x_self"][p]), abs=1e-6
    )


def test_specialhilanding_phys_large_ground_velocity_applies_friction_without_clearing() -> None:
    # Synthetic control for SpecialHiLanding_Phys:
    # - ftFx_SpecialHiLanding_Phys applies character x7C ground momentum friction through
    #   ftCommon_ApplyFrictionGround, then common ground movement.
    # - Small carried landing velocities clear to zero in replay-real rows above; velocities larger
    #   than x7C must remain nonzero after one friction step.
    # - Fox and Falco share the extracted `firefox_ground_momentum_end` char-param path.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiLanding_Phys
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_ApplyFrictionGround,ftCommon_ApplyGroundMovement}
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    for char_id in (1, 22):
        seed = np.zeros((1,), dtype=SEED_DTYPE)
        seed["stage_id"][0] = np.uint32(32)
        seed["num_players"][0] = np.uint8(2)
        seed["char_id"][0, :2] = np.uint8(char_id)
        seed["stocks"][0, :2] = np.uint8(4)
        seed["action_id"][0, 0] = np.uint16(357)  # SpecialHiLanding.
        seed["animation_index"][0, 0] = np.uint32(310)
        seed["action_frame"][0, 0] = np.int16(1)
        seed["anim_frame_f32"][0, 0] = np.float32(1.0)
        seed["frame_speed_mul_f32"][0, 0] = np.float32(1.0)
        seed["on_ground"][0, 0] = np.uint8(1)
        seed["ground_id"][0, 0] = np.uint16(0)
        seed["speed_ground_x_self"][0, 0] = np.float32(2.0)
        seed["speed_air_x_self"][0, 0] = np.float32(2.0)
        seed["action_id"][0, 1] = np.uint16(14)  # Wait.
        seed["animation_index"][0, 1] = np.uint32(2)
        seed["on_ground"][0, 1] = np.uint8(1)
        seed["ground_id"][0, 1] = np.uint16(0)

        prev_input = np.zeros((1,), dtype=INPUT_DTYPE)
        cur_input = np.zeros((1,), dtype=INPUT_DTYPE)
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
        handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
        try:
            binding.reseed_seed(handle, seed.view(np.uint8).reshape(1, seed_stride))
            binding.step_input(
                handle,
                prev_input.view(np.uint8).reshape(1, input_stride),
                cur_input.view(np.uint8).reshape(1, input_stride),
            )
            binding.write_compare(handle, out_bytes)
        finally:
            binding.destroy(handle)

        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        assert int(out["action_id"][0]) == 357
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.5, abs=1e-6)
    assert float(out["speed_air_x_self"][0]) == pytest.approx(0.5, abs=1e-6)


@pytest.mark.integration
def test_specialairhi_floor_jobj_ecb_bottom_crossing_enters_bound_selfplay_181413() -> None:
    # SpecialAirHi_Coll floor contact consumes the same live JObj ECB owner as the wall/ledge
    # SpecialHi paths. Self-play 181413 rec2560 starts with a stale ledge floor id, but the rotated
    # JObj bottom crosses FD's main floor during this frame, so ftFox_SpecialHi_IsBound enters
    # SpecialHiBound. The preceding frame is the negative boundary: the live bottom has not crossed
    # the floor yet and must stay in SpecialAirHi.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Coll,ftFx_SpecialHiBound_Enter}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044628_Floor}
    _seed_2559, ref_2559, out_2559 = _run_one_step_from_slp(
        SELFPLAY_181413_SLP, 2559, ports=[1, 2]
    )
    seed_2560, ref_2560, out_2560 = _run_one_step_from_slp(SELFPLAY_181413_SLP, 2560, ports=[1, 2])
    p = 1

    assert int(out_2559["action_id"][p]) == int(ref_2559["action_id"][p]) == 356
    assert int(out_2559["action_frame"][p]) == int(ref_2559["action_frame"][p]) == 10
    assert int(out_2559["on_ground"][p]) == int(ref_2559["on_ground"][p]) == 0

    assert int(seed_2560["action_id"][p]) == 356
    assert int(seed_2560["ground_id"][p]) == 0
    assert float(seed_2560["pos_y"][p]) < 0.0
    assert int(out_2560["action_id"][p]) == int(ref_2560["action_id"][p]) == 359
    assert int(out_2560["animation_index"][p]) == int(ref_2560["animation_index"][p]) == 312
    assert int(out_2560["ground_id"][p]) == int(ref_2560["ground_id"][p]) == 1
    assert float(out_2560["pos_x"][p]) == pytest.approx(float(ref_2560["pos_x"][p]), abs=1e-5)
    assert float(out_2560["pos_y"][p]) == pytest.approx(float(ref_2560["pos_y"][p]), abs=1e-6)


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
def test_landing_fallspecial_firefox_rate_seed_lane_enters_turn_pte_7232() -> None:
    # Replay-real lock for the hidden Up-B landing allow_interrupt carry:
    # PTE:7232 starts inside a LandingFallSpecial run whose AObj rate is the Firefox/Firebird x90
    # landing-lag rate. The preprocessed hidden allow_interrupt lane is the source owner that lets
    # ftCo_Landing_IASA consume Turn.
    #
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiFall_Anim,ftFx_SpecialHiBound_Anim}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096900
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
    #   ftCo_LandingFallSpecial_Enter,ftCo_Landing_IASA}
    dataset_path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 7232
    player = 0
    seed, ref, out = _run_one_step(dataset_path, record)

    assert int(seed["action_id"][player]) == 43  # LandingFallSpecial
    assert int(seed["landing_fallspecial_allow_interrupt"][player]) == 1
    assert float(seed["frame_speed_mul_f32"][player]) == pytest.approx(30.1 / 18.0, abs=5e-5)
    assert int(ref["action_id"][player]) == 18  # Turn
    assert int(out["action_id"][player]) == int(ref["action_id"][player])
    assert int(out["animation_index"][player]) == int(ref["animation_index"][player])
    assert int(out["action_frame"][player]) == int(ref["action_frame"][player])


@pytest.mark.integration
def test_landing_fallspecial_sideb_rate_does_not_reconstruct_firefox_allow_interrupt_pte_7232() -> None:
    # Adjacent negative: the same LandingFallSpecial row and Turn-capable input must not dispatch
    # if the source rate is the Illusion/Phantasm x50 landing-lag rate and the hidden
    # allow_interrupt lane is clear. Side-B collision passes allow_interrupt=false into
    # LandingFallSpecial, so speed rate by itself is not sufficient source authority.
    dataset_path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "ParallelTemptingElk.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    def _sideb_rate(seed_t: np.ndarray) -> None:
        seed_t["landing_fallspecial_allow_interrupt"][0, player] = np.uint8(0)
        seed_t["frame_speed_mul_f32"][0, player] = np.float32(30.1 / 20.0)

    record = 7232
    player = 0
    seed, _ref, out = _run_one_step(dataset_path, record, seed_mutator=_sideb_rate)

    assert int(seed["action_id"][player]) == 43  # LandingFallSpecial
    assert int(seed["landing_fallspecial_allow_interrupt"][player]) == 0
    assert float(seed["frame_speed_mul_f32"][player]) == pytest.approx(30.1 / 20.0, abs=5e-5)
    assert int(out["action_id"][player]) == 43
    assert int(out["animation_index"][player]) == 36


@pytest.mark.integration
def test_landing_fallspecial_without_allow_interrupt_does_not_enter_shine() -> None:
    # Negative sentinel: LandingFallSpecial shares Landing_IASA only when the hidden
    # mv.co.landing.allow_interrupt lane is set by the source transition. A B+down row with the lane
    # clear must stay in LandingFallSpecial rather than reopening broad special dispatch.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
    #   ftCo_Landing_IASA,ftCo_LandingFallSpecial_Enter}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D68C0
    dataset_path = (
        Path(__file__).resolve().parents[1]
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    record = 9035
    player = 1
    seed, ref, out = _run_one_step(dataset_path, record)

    assert int(seed["action_id"][player]) == 43  # LandingFallSpecial
    assert int(seed["landing_fallspecial_allow_interrupt"][player]) == 0
    assert int(ref["action_id"][player]) == 43
    assert int(out["action_id"][player]) == int(ref["action_id"][player])
    assert int(out["animation_index"][player]) == int(ref["animation_index"][player])
    assert int(out["on_ground"][player]) == int(ref["on_ground"][player])


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
                "ImpassionedAlarmedTarsier.msl"
            ),
            record=10513,
            player=1,
            seed_action=359,
            ref_action=359,
            note="SpecialHiBound entry row remains airborne after rebound Enter",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "TubbyCurlyHerring.msl"
            ),
            record=840,
            player=0,
            seed_action=359,
            ref_action=359,
            note="SpecialHiBound mirrored entry row remains airborne after rebound Enter",
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
            record=2356,
            player=1,
            seed_action=43,
            ref_action=360,
            note="LandingFallSpecial allow_interrupt IASA enters grounded Shine Start",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "ImpassionedAlarmedTarsier.msl"
            ),
            record=4327,
            player=0,
            seed_action=369,
            ref_action=27,
            note="SpecialAirLwTurn destination JumpAerial IASA does not immediately reconsume B/up as Firefox",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "BlondHardHippopotamus.msl"
            ),
            record=3014,
            player=0,
            seed_action=27,
            ref_action=354,
            note="Later JumpAerial B/up row can still enter Firefox after Shine same-frame marker is clear",
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
                "DistinctCaringCobra.msl"
            ),
            record=6035,
            player=1,
            seed_action=345,
            ref_action=42,
            note="SpecialAirNLoop anim-end enters End and same-proc AirCatchHit landing",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "TubbyCurlyHerring.msl"
            ),
            record=6238,
            player=1,
            seed_action=345,
            ref_action=42,
            note="SpecialAirNLoop under-floor callback lands through entered End state",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "TubbyCurlyHerring.msl"
            ),
            record=7430,
            player=1,
            seed_action=345,
            ref_action=42,
            note="SpecialAirNLoop no-X-velocity row lands through entered End state",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "DistinctCaringCobra.msl"
            ),
            record=5095,
            player=1,
            seed_action=345,
            ref_action=346,
            note="SpecialAirNLoop terminal Anim callback observes pre-input loop latch and enters End",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "PositiveRevolvingHyena.msl"
            ),
            record=4835,
            player=1,
            seed_action=342,
            ref_action=343,
            note="SpecialNLoop terminal Anim callback observes pre-input loop latch and enters End",
        ),
        _Case(
            dataset_rel=(
                "datasets/aggregate_recent/replays/validation/aggregate_recent/"
                "BlondHardHippopotamus.msl"
            ),
            record=7876,
            player=0,
            seed_action=346,
            ref_action=346,
            note="Shallow SpecialAirNEnd row does not take the under-floor Landing projection",
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
    # - LandingFallSpecial shares Landing_IASA; when the hidden allow_interrupt lane is set,
    #   ftCo_800D68C0 can consume grounded Shine input.
    # - SpecialAirLw Loop/Turn/End IASA can consume jump into JumpAerial, but that callback does not
    #   then run destination JumpAerial special dispatch again on the same B/up input edge.
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
    # - SpecialHiBound_Enter itself does not call ftCommon_8007D7FC, so rebound entry rows remain
    #   airborne until Bound_Coll owns later ground conversion.
    # - SpecialHiHoldAir_Anim enters ftFx_SpecialAirHi_Enter, whose launch handler consumes all
    #   jumps through x1968_jumpsUsed=max_jumps.
    # - SpecialHiLanding_Anim enters Wait during the Anim callback, then destination Wait_IASA can
    #   consume grounded locomotion input later in the same proc.
    # - SpecialNEnd_Anim exits through ft_8008A2BC; the same destination Wait_IASA can consume the
    #   buttonless forward Dash_CheckInput branch in the same proc.
    # - SpecialAirNEnd_Anim exits through ftCo_Fall_Enter when blaster landing lag is zero; the
    #   destination Fall IASA can consume JumpAerial input later in the same proc.
    # - SpecialAirNLoop_Anim can enter SpecialAirNEnd, then the entered End collision callback still
    #   routes through AirCatchHit_Coll -> Landing_Enter_Basic in the same Fighter proc.
    # - SpecialN Loop Anim callbacks run before current-frame input; the hidden
    #   mv.fx.SpecialN.isBlasterLoop latch is inferred from the seeded button timer before
    #   Fighter_procUpdate can reset x67D with a fresh B press.
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
    #   ftFx_SpecialHiBound_Coll,ftFx_SpecialHiHoldAir_Anim,ftFx_SpecialAirHi_Enter}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNEnd_Anim
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    #   ftFx_SpecialAirNLoop_Anim,ftFx_SpecialAirNEnd_Anim,ftFx_SpecialAirNEnd_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ftCo_AirCatchHit_Coll
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialLwStart_GroundToAir,ftFx_SpecialLwLoop_GroundToAir,
    #   ftFx_SpecialLwHit_GroundToAir,ftFx_SpecialLwEnd_GroundToAir,
    #   ftFx_SpecialLwTurn_GroundToAir,ftFx_SpecialAirLwLoop_IASA,
    #   ftFx_SpecialAirLwTurn_IASA,ftFx_SpecialAirLwEnd_Anim}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
    #   ftCo_Landing_IASA,ftCo_LandingFallSpecial_Enter}
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
