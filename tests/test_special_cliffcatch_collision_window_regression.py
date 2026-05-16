from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp


_STRICT_TRANSITION_FIELDS = (
    "action_id",
    "action_frame",
    "animation_index",
    "hitlag",
    "hitstun",
    "instance_id",
    "on_ground",
    "ground_id",
)

SELFPLAY_182447_SLP = Path("replays/validation/aggregate_recent/Game_20260515T182447_frozenps.slpz")


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _run_one_step_with_rollout(
    *, dataset_rel: str, record: int, p: int, window_before: int = 24
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short: num_records={num_records} record={record}"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(num_records, sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    one_step_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[record, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[record, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
        binding.reseed_seed(one_step_handle, seed_bytes)
        binding.step_input(one_step_handle, prev_input_bytes, input_bytes)
        binding.write_compare(one_step_handle, out_compare_bytes)
        out_one = out_view[0].copy()
    finally:
        binding.destroy(one_step_handle)

    start = max(0, int(record) - int(window_before))
    rollout_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[start, seed_off : seed_off + seed_stride]
        binding.reseed_seed(rollout_handle, seed_bytes)
        for j in range(start, int(record) + 1):
            prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
            input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
            binding.step_input(rollout_handle, prev_input_bytes, input_bytes)
            if j == int(record):
                binding.write_compare(rollout_handle, out_compare_bytes)
        out_roll = out_view[0].copy()
    finally:
        binding.destroy(rollout_handle)

    ref = samples["ref_t1"][record].copy()
    return out_one, ref, out_roll


def _run_one_step_from_dataset(
    ds, record: int, *, input_main_y_override: tuple[int, int] | None = None
) -> tuple[np.void, np.void, np.void]:
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
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes[0, :] = samples_u8[record, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[record, prev_input_off : prev_input_off + input_stride]
        if input_main_y_override is None:
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
        else:
            player, main_y = input_main_y_override
            input_row = np.empty((1,), dtype=samples["input_t"].dtype)
            input_row[0] = samples["input_t"][record]
            input_row["p"][0, int(player)]["main_y"] = np.int8(main_y)
            input_bytes[0, :] = input_row.view(np.uint8).reshape(-1)
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out = out_view[0].copy()
    finally:
        binding.destroy(handle)

    return samples["seed_t"][record].copy(), samples["ref_t1"][record].copy(), out


def _run_rollout_from_dataset(ds, start_record: int, target_record: int) -> tuple[np.void, np.void]:
    samples = ds.samples
    assert 0 <= start_record <= target_record < int(samples.shape[0])
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
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, target_record + 1):
            prev_input_bytes[0, :] = samples_u8[
                record, prev_input_off : prev_input_off + input_stride
            ]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out = out_view[0].copy()
    finally:
        binding.destroy(handle)

    return samples["ref_t1"][target_record].copy(), out


def _assert_strict_transition_fields_match_ref_all_players(
    *, out_row: np.void, ref_row: np.void, record: int
) -> None:
    num_players = int(ref_row["num_players"])
    for p in range(num_players):
        for field in _STRICT_TRANSITION_FIELDS:
            got = int(out_row[field][p])
            exp = int(ref_row[field][p])
            assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
        got_sf = out_row["state_flags"][p].tolist()
        exp_sf = ref_row["state_flags"][p].tolist()
        assert got_sf == exp_sf, f"record={record} p={p} field=state_flags expected={exp_sf} got={got_sf}"


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action", "ref_action_frame"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            2959,
            0,
            35,   # FallSpecial
            252,  # CliffCatch
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            2960,
            0,
            252,  # CliffCatch continuation
            252,
            2,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            2961,
            0,
            252,  # CliffCatch continuation
            252,
            3,
        ),
        (
            "datasets/aggregate_recent/replays/debug/cardinal_1.0_recent/"
            "Game_20250211T211709.msl",
            9024,
            0,
            358,  # SpecialHiFall
            252,  # CliffCatch
            1,
        ),
        (
            "datasets/aggregate_recent/replays/debug/cardinal_1.0_recent/"
            "HungryImportantSnake.msl",
            8669,
            1,
            358,  # SpecialHiFall
            252,  # CliffCatch
            1,
        ),
        (
            "datasets/aggregate_recent/replays/debug/fd_mixed_recent/"
            "TubbyCurlyHerring.msl",
            6998,
            1,
            358,  # SpecialHiFall
            252,  # CliffCatch
            1,
        ),
    ],
)
def test_special_cliffcatch_runtime_strict_rows(
    dataset_rel: str, record: int, p: int, seed_action: int, ref_action: int, ref_action_frame: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == int(seed_action)
    assert int(row["ref_t1"]["action_id"][p]) == int(ref_action)
    assert int(row["ref_t1"]["action_frame"][p]) == int(ref_action_frame)
    assert int(row["seed_t"]["on_ground"][p]) == 0
    assert int(row["ref_t1"]["on_ground"][p]) == 0

    out, ref, out_roll = _run_one_step_with_rollout(dataset_rel=dataset_rel, record=record, p=p)

    # Decomp ownership:
    # - mpColl ledge-grab flags are resolved from collision-step prev/cur position pairs.
    # - Special fall / spacie special-air collision callbacks include ftCliffCommon_80081298.
    # - SpecialHiFall runs ft_CheckGroundAndLedge first, then can still enter CliffCatch when mpColl
    #   produced Collide_LedgeGrabMask and no floor/ledge landing transition was taken.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    #   ftFx_SpecialHiHoldAir_Coll,ftFx_SpecialHiFall_Coll}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == int(ref_action)
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p]) == 216
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p]) == int(ref_action_frame)
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0

    # Tight float lock for the lead offender row; continuation rows keep action/frame parity strict.
    if int(record) == 2959:
        assert abs(float(out["pos_x"][p]) - float(ref["pos_x"][p])) <= 0.2
        assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= 0.05

    # Runtime-dominant strict check for this lane: one-step and rollout agree on cliffcatch action.
    assert int(out_roll["action_id"][p]) == int(out["action_id"][p]) == int(ref_action)
    assert int(out_roll["on_ground"][p]) == int(out["on_ground"][p]) == 0


@pytest.mark.integration
def test_cliff_owned_damage_entry_cooldown_blocks_false_missfoot_regrab() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / SELFPLAY_182447_SLP
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    p = 1
    seed, ref, out = _run_one_step_from_dataset(ds, 1436)
    assert int(seed["action_id"][p]) == 251  # MissFoot after cliff-owned DamageHi2.
    assert int(seed["ledge_cooldown"][p]) > 0

    # Source owner:
    # - p1 was interrupted from CliffClimbQuick into DamageHi2.
    # - ftCo_8008E908 sets x2064_ledgeCooldown while old x221D_b7 is still live, before the
    #   Damage* motion-state change clears cliff ownership.
    # - ft_CheckGroundAndLedge therefore uses the no-ledge mpColl path and MissFoot must not
    #   immediately re-enter CliffCatch at the Pokemon Stadium right ledge.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008E908
    # refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
    assert int(ref["action_id"][p]) == 251
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0


@pytest.mark.integration
def test_cliff_owned_damage_entry_runtime_cooldown_blocks_rollout_false_regrab() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / SELFPLAY_182447_SLP
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    p = 1
    ref, out = _run_rollout_from_dataset(ds, 0, 1436)

    # Runtime counterpart to the direct seed lane above:
    # p1 is interrupted from a cliff option into DamageHi2, then reaches MissFoot near the ledge.
    # ftCo_8008E908 must have installed x2064_ledgeCooldown when Damage* started, otherwise the
    # later MissFoot_Coll/ft_CheckGroundAndLedge path can falsely enter CliffCatch in free-run.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008E908
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::ftCo_MissFoot_Coll
    assert int(ref["action_id"][p]) == 251
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0


@pytest.mark.integration
def test_missfoot_anim_end_enters_damagefall_from_floor_loss_replay_real_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / SELFPLAY_182447_SLP
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    p = 1
    seed, ref, out = _run_one_step_from_dataset(ds, 1453)

    assert int(seed["action_id"][p]) == 251  # MissFoot.
    assert int(seed["action_frame"][p]) == 25
    assert int(ref["action_id"][p]) == 38  # DamageFall.

    # Source owner:
    # - p1 reaches MissFoot after a cliff-owned Damage floor-loss path.
    # - MissFoot_Anim exits through ftCo_80090780 once ftAnim_IsFramesRemaining is false.
    # - ftCo_80090780 enters DamageFall and bumps the motion-state instance id.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::ftCo_MissFoot_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_80090780
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["instance_id"][p]) == int(ref["instance_id"][p])
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0


@pytest.mark.integration
def test_missfoot_before_anim_end_stays_missfoot_replay_real_negative() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / SELFPLAY_182447_SLP
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    p = 1
    seed, ref, out = _run_one_step_from_dataset(ds, 1452)

    assert int(seed["action_id"][p]) == 251
    assert int(seed["action_frame"][p]) == 24
    assert int(ref["action_id"][p]) == 251
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["instance_id"][p]) == int(ref["instance_id"][p])


@pytest.mark.integration
def test_missfoot_anim_end_runtime_rollout_reaches_damagefall() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / SELFPLAY_182447_SLP
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    p = 1
    ref, out = _run_rollout_from_dataset(ds, 0, 1453)

    # Free-running counterpart to the direct MissFoot_Anim lock above. This guards against a seed-
    # only repair: the live MissFoot state reached from the cliff-owned damage floor-loss path must
    # run its Anim callback and enter DamageFall on the terminal frame.
    assert int(ref["action_id"][p]) == 38
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["instance_id"][p]) == int(ref["instance_id"][p])


@pytest.mark.integration
def test_fallspecial_platform_lands_when_callback_stick_accepts_soft_floor() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / SELFPLAY_182447_SLP
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    p = 0
    seed, ref, out = _run_one_step_from_dataset(ds, 1672)

    assert int(seed["action_id"][p]) == 35  # FallSpecial.
    assert int(seed["action_frame"][p]) == 0
    assert int(ref["action_id"][p]) == 43  # LandingFallSpecial on PS soft platform.

    # Source owner:
    # FallSpecial_Coll uses ft_80083090 -> mpColl_80047E14 with ftCo_80096CC8. For soft
    # platforms, that callback accepts the floor when the callback-visible stick is above
    # p_ftCommonData->x25C; this replay row has neutral stick and must publish LandingFallSpecial.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    #   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])


@pytest.mark.integration
def test_fallspecial_platform_downheld_callback_rejects_first_soft_floor_crossing() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / SELFPLAY_182447_SLP
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    p = 0
    seed, _ref, out = _run_one_step_from_dataset(ds, 1672, input_main_y_override=(p, -80))

    assert int(seed["action_id"][p]) == 35
    # Negative boundary for the same source callback: a held-down raw stick below x25C rejects the
    # soft-platform floor and keeps the first FallSpecial crossing airborne.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
    assert int(out["action_id"][p]) == 35
    assert int(out["on_ground"][p]) == 0


@pytest.mark.integration
def test_sustained_fallspecial_platform_waits_for_source_bottom_precondition() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        / "CornyDelayedOkapi.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 0
    record = 12282
    seed, ref, out = _run_one_step_from_dataset(ds, record)

    assert int(seed["action_id"][p]) == 35  # sustained FallSpecial
    assert int(seed["action_frame"][p]) == 6
    assert int(seed["seed_prev_action_id"][p]) == 35
    assert int(ref["action_id"][p]) == 35
    assert int(ref["on_ground"][p]) == 0

    # Negative boundary for the static-platform FallSpecial owner:
    # when this is not the same-frame SpecialHi end -> FallSpecial handoff and the callback-local
    # ECB bottom remains above the soft platform, source has not yet reached the
    # mpColl_80044628_Floor precondition needed before mpColl_80044838_Floor may publish
    # LandingFallSpecial. The following deeper FallSpecial frame lands normally.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    #   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == 0


@pytest.mark.integration
def test_grounded_common_damageair_phys_applies_ft80084f3c_ground_friction() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / SELFPLAY_182447_SLP
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = build_dataset_from_slp(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    p = 1
    seed, ref, out = _run_one_step_from_dataset(ds, 3910)

    assert int(seed["action_id"][p]) == 85  # DamageAir2.
    assert int(seed["on_ground"][p]) == 1
    assert float(seed["speed_ground_x_self"][p]) > float(ref["speed_ground_x_self"][p])

    # Source owner:
    # common DamageHi/N/Lw/Air Phys delegates grounded states to ft_80084F3C, so grounded
    # DamageAir2 hitstun must apply the ordinary ground-friction step before the next frame.
    # DownDamage keeps its separate downed state machine and is not admitted through this helper.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Phys
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
    assert int(ref["action_id"][p]) == 85
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    np.testing.assert_allclose(
        float(out["speed_ground_x_self"][p]), float(ref["speed_ground_x_self"][p]), atol=1e-6
    )
    np.testing.assert_allclose(float(out["pos_x"][p]), float(ref["pos_x"][p]), atol=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "target_record", "p_target", "seed_action", "ref_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            6761,
            1,
            354,  # SpecialHiHoldAir
            252,  # CliffCatch in reference
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            6629,
            1,
            354,  # SpecialHiHoldAir
            252,
        ),
    ],
)
def test_special_cliffcatch_seed_lock_target_pm1_both_players(
    dataset_rel: str, target_record: int, p_target: int, seed_action: int, ref_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    target = ds.samples[target_record]
    assert int(target["seed_t"]["action_id"][p_target]) == int(seed_action)
    assert int(target["ref_t1"]["action_id"][p_target]) == int(ref_action)
    assert int(target["seed_t"]["on_ground"][p_target]) == 0
    assert int(target["ref_t1"]["on_ground"][p_target]) == 0

    # Strict replay-real lock coverage for target-1 / target / target+1 on both players.
    for record in (target_record - 1, target_record, target_record + 1):
        out, ref, _ = _run_one_step_with_rollout(dataset_rel=dataset_rel, record=record, p=p_target)
        _assert_strict_transition_fields_match_ref_all_players(out_row=out, ref_row=ref, record=record)
        assert np.isfinite(float(out["pos_x"][p_target]))
        assert np.isfinite(float(out["pos_y"][p_target]))


@pytest.mark.integration
@pytest.mark.parametrize(
    (
        "dataset_rel",
        "target_record",
        "p_target",
        "seed_action",
        "ref_action",
        "seed_on_ground",
        "ref_on_ground",
    ),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            2879,
            1,
            42,  # EscapeF
            29,  # DamageFall
            1,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            5120,
            1,
            42,
            29,
            1,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "QuerulousGrandDinosaur.msl",
            7765,
            1,
            42,
            29,
            1,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            4038,
            0,
            24,  # FallAerial
            43,  # EscapeB
            1,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.msl",
            5270,
            0,
            24,  # FallAerial
            43,  # EscapeB
            1,
            1,
        ),
    ],
)
def test_escapeair_landing_release_seed_lock_target_pm1_both_players(
    dataset_rel: str,
    target_record: int,
    p_target: int,
    seed_action: int,
    ref_action: int,
    seed_on_ground: int,
    ref_on_ground: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    target = ds.samples[target_record]
    assert int(target["seed_t"]["action_id"][p_target]) == int(seed_action)
    assert int(target["ref_t1"]["action_id"][p_target]) == int(ref_action)
    assert int(target["seed_t"]["on_ground"][p_target]) == int(seed_on_ground)
    assert int(target["ref_t1"]["on_ground"][p_target]) == int(ref_on_ground)

    # Lock target-1 / target / target+1 with strict transition parity on both players.
    for record in (target_record - 1, target_record, target_record + 1):
        out, ref, _ = _run_one_step_with_rollout(dataset_rel=dataset_rel, record=record, p=p_target)
        _assert_strict_transition_fields_match_ref_all_players(out_row=out, ref_row=ref, record=record)
