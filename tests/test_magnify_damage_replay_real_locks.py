from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _run_one_step(ds, row, *, rollout: bool = False) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        if rollout:
            binding.reseed_seed_rollout(handle, seed_bytes)
        else:
            binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


def _field_bytes(samples: np.ndarray, record: int, field: str, stride: int) -> np.ndarray:
    return np.frombuffer(samples[record : record + 1][field].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, stride
    )


def _run_rollout(ds, start: int, stop: int, *, replay_frame_lanes: bool = False) -> np.void:
    binding = pytest.importorskip("msl_binding")
    samples = ds.samples
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        binding.reseed_seed_rollout(handle, _field_bytes(samples, start, "seed_t", seed_stride))
        for record in range(start, stop + 1):
            if replay_frame_lanes:
                binding.step_input_replay_frame_rng(
                    handle,
                    _field_bytes(samples, record, "seed_t", seed_stride),
                    _field_bytes(samples, record, "prev_input_t", input_stride),
                    _field_bytes(samples, record, "input_t", input_stride),
                )
            else:
                binding.step_input(
                    handle,
                    _field_bytes(samples, record, "prev_input_t", input_stride),
                    _field_bytes(samples, record, "input_t", input_stride),
                )
            binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


@pytest.mark.integration
def test_magnify_damage_counter_applies_offscreen_percent_tick_replay_real_lock() -> None:
    # Fighter_procUpdate increments fp->dmg.x1910 while the player is in the offscreen magnifying
    # display and applies p_ftCommonData->x7B4 damage every x7AC frames below x7B0 percent.
    # HilariousVillainousGiraffe rec614 is the 60th live offscreen Falco row: the seed counter is
    # 59, so this one-step must apply the 1% magnify tick without hitlag/action changes.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/if/ifmagnify.c::ifMagnify_802FC998
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 614
    p = 1
    row = ds.samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    assert int(seed["action_id"][p]) == 366  # Falco SpecialAirLwLoop
    assert int(seed["magnify_damage_counter_x1910"][p]) == 59
    assert int(seed["state_flags"][p, 4]) & 0x80
    assert float(seed["percent"][p]) == pytest.approx(7.0)
    assert float(ref["percent"][p]) == pytest.approx(8.0)

    out = _run_one_step(ds, row)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["hitlag"][p]) == 0
    assert int(out["hitstun"][p]) == 0
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)

    rollout_out = _run_one_step(ds, row, rollout=True)
    assert float(rollout_out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)

    # Starting earlier in the same backfilled magnify episode must carry the seeded hidden counter
    # forward until the real tick row.
    early_out = _run_rollout(ds, 588, 614)
    assert float(early_out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)


@pytest.mark.integration
def test_magnify_damage_counter_does_not_tick_match_flow_camera_bits() -> None:
    # The replay-visible x221F_b0 bit is also used by Rebirth/dead-flow camera-subject visibility.
    # It is not enough by itself to model ifMagnify offscreen damage.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    candidates = [
        i
        for i in range(int(samples.shape[0]))
        if int(samples[i]["seed_t"]["action_id"][1]) == 12
        and (int(samples[i]["seed_t"]["state_flags"][1, 4]) & 0x80) != 0
    ]
    if not candidates:
        pytest.skip("no Rebirth camera-bit control row in local dataset")
    record = candidates[0]
    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    p = 1
    assert int(seed["magnify_damage_counter_x1910"][p]) == 0

    out = _run_one_step(ds, row)
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)


@pytest.mark.integration
def test_magnify_rollout_does_not_start_from_unproven_camera_rows() -> None:
    # DCC has long x221F_b0/offscreen camera runs with no source-shaped 1% magnify tick in replay.
    # The seed derivation must not turn those rows into a rollout-startable magnify episode.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    start = 2582
    stop = 2641
    p = 1
    seed = ds.samples[start]["seed_t"]
    ref = ds.samples[stop]["ref_t1"]
    assert int(seed["state_flags"][p, 4]) & 0x80
    assert int(seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 0
    assert int(seed["magnify_damage_counter_x1910"][p]) == 0

    out = _run_rollout(ds, start, stop)
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)


@pytest.mark.integration
def test_magnify_rollout_mgs_damagefly_hi_n_source_visible_episode_ticks() -> None:
    # MGS carries two source-owned magnify damage episodes through FoD edge camera rows:
    # - DamageFlyN -> SpecialAirNLoop reaches the 60th offscreen frame at rec2919.
    # - DamageFlyHi -> SpecialLwEnd/AttackAirN reaches the later hit with the +1 percent already
    #   applied, which is the rec5687 hitstun boundary.
    # DamageFly fresh starts require the source-visible x221F_b0 bit and a horizontal root exit
    # through the point-only Camera_80030CD8 lane; this is not an action/row proxy.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/ftlib.c::ftLib_80086A8C
    # refs/melee/src/melee/cm/camera.c::{Camera_80030CD8,Camera_80030CFC}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
        "MilkyGracefulStingray.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 1

    first_tick = 2919
    first_seed = ds.samples[first_tick]["seed_t"]
    first_ref = ds.samples[first_tick]["ref_t1"]
    assert int(first_seed["action_id"][p]) == 345  # SpecialAirNLoop.
    assert int(first_seed["magnify_damage_counter_x1910"][p]) == 59
    assert int(first_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 0
    assert int(first_seed["state_flags"][p, 4]) & 0x80
    first_out = _run_rollout(ds, 2855, first_tick)
    assert float(first_out["percent"][p]) == pytest.approx(float(first_ref["percent"][p]), abs=1e-5)

    hit_record = 5687
    hit_ref = ds.samples[hit_record]["ref_t1"]
    second_seed = ds.samples[5621]["seed_t"]
    assert int(second_seed["action_id"][p]) == 354  # SpecialLwTurn.
    assert int(second_seed["magnify_damage_counter_x1910"][p]) == 59
    assert int(second_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 0
    hit_out = _run_rollout(ds, 5564, hit_record)
    assert float(hit_out["percent"][p]) == pytest.approx(float(hit_ref["percent"][p]), abs=1e-5)
    assert int(hit_out["hitstun"][p]) == int(hit_ref["hitstun"][p]) == 61


@pytest.mark.integration
def test_magnify_rollout_ppa_damageflyn_horizontal_trajectory_starts_counter() -> None:
    # PPA starts a DamageFlyN magnify episode when the camera target leaves the camera while the
    # current horizontal knockback trajectory is already carrying the fighter toward the right
    # camera bound. Root x is still inside on the first offscreen-camera-target row, so this guards
    # against reducing ifMagnify ownership to root-only bounds. The later SpecialAirHi hitstun
    # boundary proves the +1 percent tick was applied before damage scaling.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/if/ifmagnify.c::{ifMagnify_802FC7C0,ifMagnify_802FC998}
    # refs/melee/src/melee/cm/camera.c::{Camera_80030CD8,Camera_80030CFC}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 0
    start = 7585
    first_offscreen = 7590
    tick_record = 7652
    hit_record = 7808

    first_seed = ds.samples[first_offscreen]["seed_t"]
    assert int(first_seed["action_id"][p]) == 88  # DamageFlyN.
    assert int(first_seed["magnify_damage_counter_x1910"][p]) == 0
    assert int(first_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 0
    assert int(first_seed["state_flags"][p, 4]) & 0x80

    tick_ref = ds.samples[tick_record]["ref_t1"]
    tick_out = _run_rollout(ds, start, tick_record)
    assert float(tick_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]), abs=1e-5)

    hit_ref = ds.samples[hit_record]["ref_t1"]
    hit_out = _run_rollout(ds, start, hit_record)
    assert float(hit_out["percent"][p]) == pytest.approx(float(hit_ref["percent"][p]), abs=1e-5)
    assert int(hit_out["hitstun"][p]) == int(hit_ref["hitstun"][p]) == 73


@pytest.mark.integration
def test_magnify_rollout_mvp_damageflylw_horizontal_root_exit_starts_counter() -> None:
    # MVP starts a magnifying-glass damage episode while DamageFlyLw's live root crosses the right
    # camera bound. The x1910 counter then carries through SpecialHiHoldAir -> SpecialAirHi and
    # reaches the 1% tick before the later SpecialHiFall hitstun boundary. The owner is the
    # root-horizontal ifMagnify source bound, not the later SpecialHi action shape.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/if/ifmagnify.c::{ifMagnify_802FC7C0,ifMagnify_802FC998}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 0
    start = 1600
    visible_record = 1614
    tick_record = 1672
    hit_record = 1718

    visible_seed = ds.samples[visible_record]["seed_t"]
    assert int(visible_seed["action_id"][p]) == 89  # DamageFlyLw.
    assert int(visible_seed["magnify_damage_counter_x1910"][p]) == 1
    assert int(ds.samples[visible_record - 1]["seed_t"]["magnify_damage_counter_x1910"][p]) == 0
    assert int(visible_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 0
    assert int(visible_seed["state_flags"][p, 4]) & 0x80

    tick_ref = ds.samples[tick_record]["ref_t1"]
    tick_out = _run_rollout(ds, start, tick_record)
    assert float(tick_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]), abs=1e-5)

    hit_ref = ds.samples[hit_record]["ref_t1"]
    hit_out = _run_rollout(ds, start, hit_record)
    assert float(hit_out["percent"][p]) == pytest.approx(float(hit_ref["percent"][p]), abs=1e-5)
    assert int(hit_out["hitstun"][p]) == int(hit_ref["hitstun"][p]) == 65


@pytest.mark.integration
def test_magnify_rollout_tvr_damageflyhi_visible_edge_local_episode_ticks() -> None:
    # TVR exposes a DamageFlyHi right-edge camera-box publication where the point-only
    # Camera_80030CD8 lane is still inside, but the replay-fed x221F_b0 rising edge proves the
    # source magnifying-glass subject is visible. The local episode must carry through SpecialAirHi
    # until the x1910 tick before the later BAir hit.
    # refs/melee/src/melee/ft/ftlib.c::{ftLib_80086A8C,ftLib_80086B64,ftLib_80086B90}
    # refs/melee/src/melee/if/ifmagnify.c::{ifMagnify_802FBBDC,ifMagnify_802FC998}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 0
    visible_record = 3126
    tick_record = 3185
    visible_seed = ds.samples[visible_record]["seed_t"]
    assert int(visible_seed["action_id"][p]) == 87  # DamageFlyHi.
    assert int(visible_seed["camera_box_visible_x221f_b0"][p]) == 1
    assert int(ds.samples[visible_record - 1]["seed_t"]["camera_box_visible_x221f_b0"][p]) == 0
    assert int(visible_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 1
    assert (int(visible_seed["state_flags"][p, 0]) & 0xC4) == 0xC4
    assert (int(visible_seed["state_flags"][p, 0]) & 0x30) == 0

    tick_ref = ds.samples[tick_record]["ref_t1"]
    plain_out = _run_rollout(ds, visible_record - 1, tick_record)
    assert float(plain_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]) - 1.0, abs=1e-5)

    tick_out = _run_rollout(ds, visible_record - 1, tick_record, replay_frame_lanes=True)
    assert float(tick_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]), abs=1e-5)


@pytest.mark.integration
def test_magnify_rollout_tvr_damageflyn_inside_visible_edge_carries_to_damagefall_tick() -> None:
    # TVR's second magnify episode starts from a DamageFlyN replay-fed x221F_b0 rising edge while
    # the camera target point is still inside stage camera bounds and live hitstun is active. The
    # source camera-box publication, not root/offscreen stage bounds, owns the x1910 start; the
    # local episode then carries through the DamageFlyN -> DamageFall handoff until the +1% tick.
    # refs/melee/src/melee/ft/ftlib.c::{ftLib_80086A8C,ftLib_80086B64,ftLib_80086B90}
    # refs/melee/src/melee/if/ifmagnify.c::{ifMagnify_802FBBDC,ifMagnify_802FC998}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 1
    visible_record = 3605
    tick_record = 3664
    visible_seed = ds.samples[visible_record]["seed_t"]
    assert int(visible_seed["action_id"][p]) == 88  # DamageFlyN.
    assert int(visible_seed["camera_box_visible_x221f_b0"][p]) == 1
    assert int(ds.samples[visible_record - 1]["seed_t"]["camera_box_visible_x221f_b0"][p]) == 0
    assert int(visible_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 1
    assert int(visible_seed["hitstun"][p]) > 0
    assert (int(visible_seed["state_flags"][p, 3]) & 0x62) == 0x22

    tick_ref = ds.samples[tick_record]["ref_t1"]
    plain_out = _run_rollout(ds, visible_record - 1, tick_record)
    assert float(plain_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]) - 1.0, abs=1e-5)

    tick_out = _run_rollout(ds, visible_record - 1, tick_record, replay_frame_lanes=True)
    assert float(tick_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]), abs=1e-5)


@pytest.mark.integration
def test_magnify_rollout_tvr_damageflyn_hitstun_only_visible_edge_repeats_after_tick() -> None:
    # TVR's later DamageFlyN camera publication has x2218_b1 plus x221C hitstun, but not the
    # stronger x221C_b2 lane. That still proves the source ftLib_80086A8C camera-box publication
    # that starts a replay-local x1910 episode. After the first 60-frame +1% tick resets x1910 to
    # zero, the same source-visible x221F lane remains live through Fall/SpecialAirHi, so the next
    # 60-frame interval must begin without a new broad zero-counter camera admission.
    # refs/melee/src/melee/ft/ftlib.c::{ftLib_80086A8C,ftLib_80086B64,ftLib_80086B90}
    # refs/melee/src/melee/if/ifmagnify.c::{ifMagnify_802FBBDC,ifMagnify_802FC998}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 0
    visible_record = 6769
    first_tick_record = 6828
    second_tick_record = 6888
    visible_seed = ds.samples[visible_record]["seed_t"]
    assert int(visible_seed["action_id"][p]) == 88  # DamageFlyN.
    assert int(visible_seed["camera_box_visible_x221f_b0"][p]) == 1
    assert int(ds.samples[visible_record - 1]["seed_t"]["camera_box_visible_x221f_b0"][p]) == 0
    assert int(visible_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 1
    assert (int(visible_seed["state_flags"][p, 0]) & 0x40) == 0x40
    assert (int(visible_seed["state_flags"][p, 0]) & 0xB4) == 0
    assert (int(visible_seed["state_flags"][p, 3]) & 0xE2) == 0x02

    first_ref = ds.samples[first_tick_record]["ref_t1"]
    second_ref = ds.samples[second_tick_record]["ref_t1"]
    plain_out = _run_rollout(ds, visible_record - 1, second_tick_record)
    assert float(plain_out["percent"][p]) == pytest.approx(float(second_ref["percent"][p]) - 2.0, abs=1e-5)

    first_out = _run_rollout(ds, visible_record - 1, first_tick_record, replay_frame_lanes=True)
    assert float(first_out["percent"][p]) == pytest.approx(float(first_ref["percent"][p]), abs=1e-5)

    second_out = _run_rollout(ds, visible_record - 1, second_tick_record, replay_frame_lanes=True)
    assert float(second_out["percent"][p]) == pytest.approx(float(second_ref["percent"][p]), abs=1e-5)


@pytest.mark.integration
def test_magnify_rollout_tvr_damageflyhi_hitstun_only_visible_edge_ticks() -> None:
    # TVR has a DamageFlyHi replay-fed x221F rising edge where the point-only camera target is still
    # inside and the raw source state is hitstun-only (`x221C_b6`) without reflect/damage-script
    # owner bits. That source camera-box publication starts a bounded local x1910 episode; generic
    # DamageFlyHi camera-visible rows without the rising edge still do not start from zero.
    # refs/melee/src/melee/ft/ftlib.c::{ftLib_80086A8C,ftLib_80086B64,ftLib_80086B90}
    # refs/melee/src/melee/if/ifmagnify.c::{ifMagnify_802FBBDC,ifMagnify_802FC998}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 0
    visible_record = 9413
    stale_visible_record = 9438
    tick_record = 9472
    visible_seed = ds.samples[visible_record]["seed_t"]
    assert int(visible_seed["action_id"][p]) == 87  # DamageFlyHi.
    assert int(visible_seed["camera_box_visible_x221f_b0"][p]) == 1
    assert int(ds.samples[visible_record - 1]["seed_t"]["camera_box_visible_x221f_b0"][p]) == 0
    assert int(visible_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 1
    assert (int(visible_seed["state_flags"][p, 0]) & 0xF4) == 0
    assert (int(visible_seed["state_flags"][p, 3]) & 0xF7) == 0x02

    stale_seed = ds.samples[stale_visible_record]["seed_t"]
    assert int(stale_seed["camera_box_visible_x221f_b0"][p]) == 1
    assert int(ds.samples[stale_visible_record - 1]["seed_t"]["camera_box_visible_x221f_b0"][p]) == 1

    tick_ref = ds.samples[tick_record]["ref_t1"]
    plain_out = _run_rollout(ds, visible_record - 1, tick_record)
    assert float(plain_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]) - 1.0, abs=1e-5)

    tick_out = _run_rollout(ds, visible_record - 1, tick_record, replay_frame_lanes=True)
    assert float(tick_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]), abs=1e-5)

    stale_out = _run_rollout(ds, stale_visible_record - 1, tick_record, replay_frame_lanes=True)
    assert float(stale_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]) - 1.0, abs=1e-5)


@pytest.mark.integration
def test_magnify_rollout_tvr_damageflyroll_visible_edge_carries_through_downstand_tick() -> None:
    # TVR starts a DamageFlyRoll x221F camera-box episode from a hitstun-only source row, then the
    # same local x1910 episode carries across DownBoundD/DownStandD until the +1% tick. The start
    # still requires the replay-fed x221F rising edge; stale already-visible DownBoundD rows cannot
    # invent the hidden counter.
    # refs/melee/src/melee/ft/ftlib.c::{ftLib_80086A8C,ftLib_80086B64,ftLib_80086B90}
    # refs/melee/src/melee/if/ifmagnify.c::{ifMagnify_802FBBDC,ifMagnify_802FC998}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 0
    visible_record = 11036
    stale_visible_record = 11059
    tick_record = 11095
    downstream_record = 11504
    visible_seed = ds.samples[visible_record]["seed_t"]
    assert int(visible_seed["action_id"][p]) == 91  # DamageFlyRoll.
    assert int(visible_seed["camera_box_visible_x221f_b0"][p]) == 1
    assert int(ds.samples[visible_record - 1]["seed_t"]["camera_box_visible_x221f_b0"][p]) == 0
    assert int(visible_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 1
    assert (int(visible_seed["state_flags"][p, 0]) & 0xF4) == 0
    assert (int(visible_seed["state_flags"][p, 3]) & 0xF7) == 0x02

    stale_seed = ds.samples[stale_visible_record]["seed_t"]
    assert int(stale_seed["action_id"][p]) == 191  # DownBoundD.
    assert int(stale_seed["camera_box_visible_x221f_b0"][p]) == 1
    assert int(ds.samples[stale_visible_record - 1]["seed_t"]["camera_box_visible_x221f_b0"][p]) == 1

    tick_ref = ds.samples[tick_record]["ref_t1"]
    plain_out = _run_rollout(ds, visible_record - 1, tick_record)
    assert float(plain_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]) - 1.0, abs=1e-5)

    tick_out = _run_rollout(ds, visible_record - 1, tick_record, replay_frame_lanes=True)
    assert float(tick_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]), abs=1e-5)

    stale_out = _run_rollout(ds, stale_visible_record - 1, tick_record, replay_frame_lanes=True)
    assert float(stale_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]) - 1.0, abs=1e-5)

    downstream_ref = ds.samples[downstream_record]["ref_t1"]
    downstream_out = _run_rollout(ds, visible_record - 1, downstream_record, replay_frame_lanes=True)
    assert int(downstream_out["action_id"][p]) == int(downstream_ref["action_id"][p]) == 29


@pytest.mark.integration
def test_magnify_rollout_lim_damageflyhi_local_episode_resets_when_camera_inside() -> None:
    # LIM's early DamageFlyHi episode publishes x221F_b0 for the magnifying-glass camera subject,
    # but Slippi's hidden x1910 counter remains zero and the source visibility clears before the
    # 60-frame damage interval. Fighter_procUpdate resets fp->dmg.x1910 whenever
    # ifMagnify_802FC998 is false, so a runtime-started local counter must not force x221F_b0 across
    # the later Jump/SpecialHiHoldAir rows after the replay camera lane has returned inside.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/if/ifmagnify.c::ifMagnify_802FC998
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "LawfulInsistentMeerkat.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 0
    source_record = 296
    inside_record = 320
    no_tick_record = 353

    source_seed = ds.samples[source_record]["seed_t"]
    assert int(source_seed["action_id"][p]) == 88  # DamageFlyHi.
    assert int(source_seed["magnify_damage_counter_x1910"][p]) == 0
    assert int(source_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 0
    assert int(ds.samples[source_record]["ref_t1"]["state_flags"][p, 4]) & 0x80

    inside_seed = ds.samples[inside_record]["seed_t"]
    assert int(inside_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 1
    assert (int(inside_seed["state_flags"][p, 4]) & 0x80) == 0

    no_tick_ref = ds.samples[no_tick_record]["ref_t1"]
    no_tick_out = _run_rollout(ds, 0, no_tick_record)
    assert float(no_tick_out["percent"][p]) == pytest.approx(
        float(no_tick_ref["percent"][p]), abs=1e-5
    )
    assert (int(no_tick_out["state_flags"][p, 4]) & 0x80) == 0


@pytest.mark.integration
def test_magnify_rollout_tvr_damageflytop_reflect_behavior_visible_edge_ticks() -> None:
    # TVR's reflected DamageFlyTop episode starts from a replay-fed x221F_b0 rising edge while the
    # raw fp+0x2218 reflect-behavior lane is live, then carries through GuardOff/GuardReflect even
    # after state_flags refresh also publishes fp->reflecting. This is not a generic x221F carry:
    # the existing LIM and DCC controls reject non-reflect and stale visible rows.
    # refs/melee/src/melee/ft/ftlib.c::{ftLib_80086A8C,ftLib_80086B64,ftLib_80086B90}
    # refs/melee/src/melee/if/ifmagnify.c::{ifMagnify_802FBBDC,ifMagnify_802FC998}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        "ThisVioletRaccoon.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 1
    visible_record = 2638
    tick_record = 2697
    visible_seed = ds.samples[visible_record]["seed_t"]
    assert int(visible_seed["action_id"][p]) == 90  # DamageFlyTop.
    assert int(visible_seed["camera_box_visible_x221f_b0"][p]) == 1
    assert int(ds.samples[visible_record - 1]["seed_t"]["camera_box_visible_x221f_b0"][p]) == 0
    assert int(visible_seed["state_flags"][p, 0]) & 0x04  # fp+0x2218 reflect-behavior.
    assert int(visible_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 1

    tick_ref = ds.samples[tick_record]["ref_t1"]
    plain_out = _run_rollout(ds, visible_record - 1, tick_record)
    assert float(plain_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]) - 1.0, abs=1e-5)

    tick_out = _run_rollout(ds, visible_record - 1, tick_record, replay_frame_lanes=True)
    assert float(tick_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]), abs=1e-5)


@pytest.mark.integration
def test_magnify_rollout_feh_damageflytop_reflect_b1_visible_edge_does_not_tick() -> None:
    # FEH exposes a DamageFlyTop replay-visible camera-box row whose raw x2218 byte carries both
    # reflect-behavior and b1. That is not TVR's bounded reflect-behavior-only source publication,
    # so replay playback must not start a local x1910 episode or add a hidden +1% before the later
    # AttackAirB hitstun boundary.
    # refs/melee/src/melee/ft/ftlib.c::{ftLib_80086A8C,ftLib_80086B64,ftLib_80086B90}
    # refs/melee/src/melee/if/ifmagnify.c::{ifMagnify_802FBBDC,ifMagnify_802FC998}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/dream_land_recent/"
        "FlippantEnchantedHorse.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 1
    visible_record = 6284
    hit_record = 6429
    visible_seed = ds.samples[visible_record]["seed_t"]
    assert int(visible_seed["action_id"][p]) == 90  # DamageFlyTop.
    assert int(visible_seed["camera_box_visible_x221f_b0"][p]) == 1
    assert int(ds.samples[visible_record - 1]["seed_t"]["camera_box_visible_x221f_b0"][p]) == 0
    assert (int(visible_seed["state_flags"][p, 0]) & 0x44) == 0x44
    assert int(visible_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 1

    hit_ref = ds.samples[hit_record]["ref_t1"]
    hit_out = _run_rollout(ds, 5966, hit_record, replay_frame_lanes=True)
    assert float(hit_out["percent"][p]) == pytest.approx(float(hit_ref["percent"][p]), abs=1e-5)
    assert int(hit_out["hitstun"][p]) == int(hit_ref["hitstun"][p]) == 70


@pytest.mark.integration
def test_magnify_rollout_lim_damageflytop_early_visible_episode_ticks() -> None:
    # LIM exposes a source camera-box visibility start on DamageFlyTop frame 8: ftLib_80086A8C has
    # already set fp->x221F_b0, but the replay-derived camera-target-inside lane is still true and
    # the hidden x1910 seed lane remains zero. Replay playback feeds the replay-visible x221F_b0
    # source bit each frame; the fresh start is bounded by that bit plus the raw x2218_b2
    # DamageFlyTop source-episode provenance. This is the create-edge visibility owner, not a
    # generic DamageFlyTop top-exit bridge.
    # refs/melee/src/melee/ft/ftlib.c::{ftLib_80086A8C,ftLib_80086B64,ftLib_80086B90}
    # refs/melee/src/melee/if/ifmagnify.c::{ifMagnify_802FBBDC,ifMagnify_802FC998}
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/yoshis_story_recent/"
        "LawfulInsistentMeerkat.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    start = 2326
    visible_record = 2559
    tick_record = 2618
    p = 0

    visible_seed = ds.samples[visible_record]["seed_t"]
    assert int(visible_seed["action_id"][p]) == 90  # DamageFlyTop.
    assert int(visible_seed["action_frame"][p]) == 8
    assert int(visible_seed["magnify_damage_counter_x1910"][p]) == 0
    assert int(visible_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 1
    assert int(visible_seed["camera_box_visible_x221f_b0"][p]) == 1
    assert int(visible_seed["state_flags"][p, 0]) & 0x20
    assert int(visible_seed["state_flags"][p, 4]) & 0x80

    tick_ref = ds.samples[tick_record]["ref_t1"]
    plain_out = _run_rollout(ds, start, tick_record)
    assert float(plain_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]) - 1.0, abs=1e-5)

    tick_out = _run_rollout(ds, start, tick_record, replay_frame_lanes=True)
    assert float(tick_out["percent"][p]) == pytest.approx(float(tick_ref["percent"][p]), abs=1e-5)


@pytest.mark.integration
def test_magnify_rollout_dcc_damagefly_top_visible_rows_do_not_start_episode() -> None:
    # DCC has later source-visible DamageFlyTop rows with camera point outside, but it does not
    # expose the early frame-8 x221F_b0 create-edge that owns LIM's x1910 start. The later terminal
    # percent change in this segment is the DCC phantom/tip-log source owner, not a magnifying-glass
    # runtime episode, so this rollout must not start a generic DamageFlyTop top-exit counter.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/if/ifmagnify.c::ifMagnify_802FC998
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/DistinctCaringCobra.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    p = 1
    early_top = ds.samples[9291]["seed_t"]
    assert int(early_top["action_id"][p]) == 90  # DamageFlyTop.
    assert int(early_top["action_frame"][p]) == 8
    assert int(early_top["magnify_damage_counter_x1910"][p]) == 0
    assert (int(early_top["state_flags"][p, 0]) & 0x20) == 0
    assert (int(early_top["state_flags"][p, 4]) & 0x80) == 0

    visible_top = ds.samples[9362]["seed_t"]
    assert int(visible_top["action_id"][p]) == 90  # DamageFlyTop.
    assert int(visible_top["magnify_damage_counter_x1910"][p]) == 0
    assert int(visible_top["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 0
    assert int(visible_top["state_flags"][p, 4]) & 0x80

    hit_record = 9685
    hit_ref = ds.samples[hit_record]["ref_t1"]
    hit_out = _run_rollout(ds, 9300, hit_record)
    assert float(hit_out["percent"][p]) == pytest.approx(float(hit_ref["percent"][p]), abs=1e-5)
    assert int(hit_out["hitstun"][p]) == int(hit_ref["hitstun"][p]) == 122


@pytest.mark.integration
def test_magnify_rollout_bhh_damageflyn_top_exit_does_not_start_zero_counter_episode() -> None:
    # BHH has visible x221F_b0/offscreen rows during DamageFlyN, but this episode leaves through the
    # top/vertical camera boundary and does not run an ifMagnify damage tick. Fresh zero-counter
    # DamageFly starts are limited to source-visible horizontal exits; vertical exits stay
    # seed-owned until a nonzero x1910 episode is proven.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/if/ifmagnify.c::ifMagnify_802FC998
    # refs/melee/src/melee/cm/camera.c::{Camera_80030CD8,Camera_80030CFC}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    start = 5609
    visible_record = 5620
    stop = 5671
    p = 0
    visible_seed = ds.samples[visible_record]["seed_t"]
    assert int(visible_seed["action_id"][p]) == 88  # DamageFlyN.
    assert int(visible_seed["magnify_damage_counter_x1910"][p]) == 0
    assert int(visible_seed["camera_target_point_inside_stage_cam_bounds_u8"][p]) == 0
    assert int(visible_seed["state_flags"][p, 4]) & 0x80

    ref = ds.samples[stop]["ref_t1"]
    out = _run_rollout(ds, start, stop)
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-5)
