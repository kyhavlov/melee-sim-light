from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_DAMAGE_FLY_ROLL = 0x005B
ACT_THROW_HI = 0x00DD
FALCO_LASER_ITEM = 55
FALCO_BLASTER_GUN_ITEM = 75
ROLL_CLOCK_NONE = 0
ROLL_CLOCK_REPLAY_FRAME_SEED = 2


def _dataset_path(root: Path) -> Path:
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    return dataset_path


def _bytes(row_field: np.ndarray, stride: int) -> np.ndarray:
    return np.frombuffer(row_field.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, stride)


def _run_one_step(dataset_path: Path, record: int, seed: np.ndarray | None = None) -> tuple[np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    row = ds.samples[record : record + 1]
    seed_t = row["seed_t"].copy() if seed is None else seed
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    out = np.empty((1, compare_stride), dtype=np.uint8)
    try:
        binding.reseed_seed(handle, _bytes(seed_t, seed_stride))
        binding.step_input(
            handle,
            _bytes(row["prev_input_t"], input_stride),
            _bytes(row["input_t"], input_stride),
        )
        binding.write_compare(handle, out)
    finally:
        binding.destroy(handle)
    return out.view(COMPARE_DTYPE).reshape(1)[0].copy(), row["ref_t1"][0].copy()


def _run_rollout_to(dataset_path: Path, start_record: int, target_record: int) -> tuple[np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    binding = pytest.importorskip("msl_binding")
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
    out = np.empty((1, compare_stride), dtype=np.uint8)
    try:
        binding.reseed_seed_rollout(
            handle, _bytes(ds.samples[start_record : start_record + 1]["seed_t"], seed_stride)
        )
        for record in range(start_record, target_record + 1):
            row = ds.samples[record : record + 1]
            binding.step_input(
                handle,
                _bytes(row["prev_input_t"], input_stride),
                _bytes(row["input_t"], input_stride),
            )
        binding.write_compare(handle, out)
    finally:
        binding.destroy(handle)
    return out.view(COMPARE_DTYPE).reshape(1)[0].copy(), ds.samples[target_record]["ref_t1"].copy()


@pytest.mark.integration
def test_tbk_seeded_live_laser_keeps_frozen_stale_damage_2610() -> None:
    # TBK:2610 has an already-live Falco laser whose HitCapsule.damage was frozen when the shot
    # was created. The seed stale queue has since gained the previous blaster article's stale entry,
    # so reseed must rewind that concrete source insertion instead of recomputing from the later
    # queue shape.
    # refs/melee/src/melee/it/it_2725.c::{it_8027B070,it_802790C0}
    # refs/melee/src/melee/it/itcoll.c::it_80272460
    # refs/melee/src/melee/ft/ft_0881.c::{ft_80089118,ft_80089228}
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset_path(root)
    ds = read_dataset(str(dataset_path))
    seed = ds.samples[2610:2611]["seed_t"].copy()

    owner = 1
    victim = 0
    assert int(seed[0]["items"][0]["exists"]) == 1
    assert int(seed[0]["items"][0]["type"]) == FALCO_BLASTER_GUN_ITEM
    assert int(seed[0]["items"][0]["owner"]) == owner
    assert int(seed[0]["items"][1]["type"]) == FALCO_LASER_ITEM
    assert int(seed[0]["items"][1]["attack_instance"]) == 489
    assert int(seed[0]["stale_queue_index"][owner]) == 6
    assert int(seed[0]["stale_move_id"][owner, 5]) == int(seed[0]["items"][0]["attack_id"])
    assert int(seed[0]["stale_attack_instance"][owner, 5]) == int(seed[0]["items"][0]["attack_instance"])

    out, ref = _run_one_step(dataset_path, 2610, seed)
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim]) == 3
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim]) == 9
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]), abs=1e-6)

    no_blaster_article = seed.copy()
    no_blaster_article[0]["items"][0]["exists"] = np.uint8(0)
    out_without_provenance, _ = _run_one_step(dataset_path, 2610, no_blaster_article)
    assert float(out_without_provenance["percent"][victim]) == pytest.approx(130.8, abs=5e-6)
    assert float(out_without_provenance["percent"][victim]) < float(ref["percent"][victim])

    shot_already_staled = seed.copy()
    shot_already_staled[0]["stale_move_id"][owner, 5] = shot_already_staled[0]["items"][1]["attack_id"]
    shot_already_staled[0]["stale_attack_instance"][owner, 5] = shot_already_staled[0]["items"][1][
        "attack_instance"
    ]
    out_with_shot_in_queue, _ = _run_one_step(dataset_path, 2610, shot_already_staled)
    assert float(out_with_shot_in_queue["percent"][victim]) == pytest.approx(130.8, abs=5e-6)
    assert float(out_with_shot_in_queue["percent"][victim]) < float(ref["percent"][victim])

    ambiguous_same_source_victim = no_blaster_article.copy()
    ambiguous_same_source_victim[0]["hitstun"][1] = ambiguous_same_source_victim[0]["hitstun"][victim]
    ambiguous_same_source_victim[0]["last_hit_by"][1] = ambiguous_same_source_victim[0]["last_hit_by"][
        victim
    ]
    ambiguous_same_source_victim[0]["instance_hit_by"][1] = ambiguous_same_source_victim[0][
        "instance_hit_by"
    ][victim]
    out_with_ambiguous_victims, _ = _run_one_step(dataset_path, 2610, ambiguous_same_source_victim)
    assert float(out_with_ambiguous_victims["percent"][victim]) == pytest.approx(130.8, abs=5e-6)
    assert float(out_with_ambiguous_victims["percent"][victim]) < float(ref["percent"][victim])

    reflected_owner = seed.copy()
    reflected_owner[0]["items"][1]["owner"] = np.int8(0)
    out_reflected_owner, _ = _run_one_step(dataset_path, 2610, reflected_owner)
    assert int(out_reflected_owner["hitlag"][victim]) == 0
    assert float(out_reflected_owner["percent"][victim]) < float(ref["percent"][victim])

    non_laser_item = seed.copy()
    non_laser_item[0]["items"][1]["type"] = np.uint16(FALCO_BLASTER_GUN_ITEM)
    out_non_laser, _ = _run_one_step(dataset_path, 2610, non_laser_item)
    assert int(out_non_laser["hitlag"][victim]) == 0
    assert float(out_non_laser["percent"][victim]) < float(ref["percent"][victim])


@pytest.mark.integration
def test_tbk_runtime_spawned_laser_freezes_stale_damage_before_previous_shot_stales() -> None:
    # The same source owner must hold in free rollout: the shot born at TBK:2591 freezes damage with
    # stale multiplier 0.91, before the previous live blaster shot later inserts attack instance 487
    # into the owner's stale queue. Without the runtime freeze, rec 2610 applies 2.52 instead of 2.73.
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C504
    # refs/melee/src/melee/it/it_2725.c::it_802790C0
    # refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset_path(root)
    out, ref = _run_rollout_to(dataset_path, start_record=2588, target_record=2610)

    victim = 0
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim]) == 3
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim]) == 9
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "victim"),
    [
        ("datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl", 2131, 1),
        ("datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl", 1109, 0),
    ],
)
def test_aggregate_live_laser_body_damage_controls_remain_exact(
    dataset_rel: str, record: int, victim: int
) -> None:
    # Aggregate controls for the generalized fighter-spawned laser stale freeze. These rows exercise
    # ordinary live Falco laser BODY damage outside TBK and must remain exact.
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    out, ref = _run_one_step(dataset_path, record)
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim])
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim])
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim])
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]), abs=1e-6)


@pytest.mark.integration
def test_tbk_post_detach_throw_blaster_rollout_clock_reaches_damageflyroll_gate() -> None:
    # TBK 2748 starts after Falco ThrowHi has detached Fox into same-source throw-laser hitstun.
    # Rollout must keep the replay frame-start RNG clock moving until the delayed ftCo_8008DCE0
    # DamageFlyRoll gate, while the ThrowHi 4/3 anim-rate owner keeps action_frame aligned.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset_path(root)
    ds = read_dataset(str(dataset_path))
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    start_record = 2748
    target_record = 2752
    thrower = 1
    victim = 0
    seed = ds.samples[start_record : start_record + 1]["seed_t"]
    assert int(seed[0]["action_id"][thrower]) == ACT_THROW_HI
    assert int(seed[0]["hitstun"][victim]) > 0
    assert int(seed[0]["last_hit_by"][victim]) == int(seed[0]["source_port0"][thrower])
    assert int(seed[0]["instance_hit_by"][victim]) == int(seed[0]["instance_id"][thrower])

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    out = np.empty((1, compare_stride), dtype=np.uint8)
    try:
        binding.reseed_seed_rollout(handle, _bytes(seed, seed_stride))
        assert int(binding.debug_get_rollout_clock_mode(handle, 0)) == ROLL_CLOCK_REPLAY_FRAME_SEED
        for record in range(start_record, target_record + 1):
            row = ds.samples[record : record + 1]
            binding.step_input(
                handle,
                _bytes(row["prev_input_t"], input_stride),
                _bytes(row["input_t"], input_stride),
            )
        binding.write_compare(handle, out)
    finally:
        binding.destroy(handle)

    actual = out.view(COMPARE_DTYPE).reshape(1)[0]
    ref = ds.samples[target_record]["ref_t1"]
    assert int(actual["frame_pre_random_seed"]) == int(ref["frame_pre_random_seed"])
    assert int(actual["action_frame"][thrower]) == int(ref["action_frame"][thrower])
    assert int(actual["action_id"][victim]) == int(ref["action_id"][victim]) == ACT_DAMAGE_FLY_ROLL


@pytest.mark.integration
def test_tbk_throw_blaster_rollout_clock_requires_live_same_source_article() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset_path(root)
    ds = read_dataset(str(dataset_path))
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])

    seed = ds.samples[2748:2749]["seed_t"].copy()
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, _bytes(seed, seed_stride))
        assert int(binding.debug_get_rollout_clock_mode(handle, 0)) == ROLL_CLOCK_REPLAY_FRAME_SEED

        no_article = seed.copy()
        no_article[0]["items"]["exists"] = np.uint8(0)
        binding.reseed_seed_rollout(handle, _bytes(no_article, seed_stride))
        assert int(binding.debug_get_rollout_clock_mode(handle, 0)) == ROLL_CLOCK_NONE

        no_source_instance = seed.copy()
        no_source_instance[0]["instance_hit_by"][0] = np.uint16(0)
        binding.reseed_seed_rollout(handle, _bytes(no_source_instance, seed_stride))
        assert int(binding.debug_get_rollout_clock_mode(handle, 0)) == ROLL_CLOCK_NONE

        normal_reseed = seed.copy()
        binding.reseed_seed(handle, _bytes(normal_reseed, seed_stride))
        assert int(binding.debug_get_rollout_clock_mode(handle, 0)) == ROLL_CLOCK_NONE
    finally:
        binding.destroy(handle)
