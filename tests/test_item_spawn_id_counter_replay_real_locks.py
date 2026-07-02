from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _dataset_path() -> Path:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "replays/validation/aggregate_recent/HungryImportantSnake.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")
    return dataset_path


def _gat_dataset_path() -> Path:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")
    return dataset_path


def _live_item_keys(row) -> list[tuple[int, int, int, int, int, int]]:
    out: list[tuple[int, int, int, int, int, int]] = []
    for slot, item in enumerate(row["items"]):
        if int(item["exists"]) == 0:
            continue
        out.append(
            (
                slot,
                int(item["type"]),
                int(item["state"]),
                int(item["owner"]),
                int(item["instance_id"]),
                int(item["spawn_id"]),
            )
        )
    return out


def test_item_spawn_counter_survives_itemless_gap_for_next_blaster_item() -> None:
#Slippi exposes item->x1C on live items, but the vanilla owner is a global counter
#(`it_804D6D10`) consumed by Item_80267AA8.Record 6584 is seeded after an itemless gap; the
#next blaster gun must still receive spawn_id = 34, not restart from zero.
#refs / melee / src / melee / it / item.c::Item_80267AA8
    dataset_path = _dataset_path()
    seed, ref, out = _run_one_step_row(dataset_path, 6584, 0)
    assert int(seed["item_spawn_id_counter"]) == 34
    assert _live_item_keys(ref) == [(0, 74, 3, 0, 1530, 34)]
    assert _live_item_keys(out) == _live_item_keys(ref)

    def _force_stale_counter(seed_t: np.ndarray) -> None:
        seed_t["item_spawn_id_counter"] = np.uint32(0)

    _, _, stale_out = _run_one_step_row(dataset_path, 6584, 0, seed_mutator=_force_stale_counter)
    assert _live_item_keys(stale_out) != _live_item_keys(ref)
    assert _live_item_keys(stale_out)[0][-1] == 0


def test_rollout_keeps_overlapping_specialn_lasers_after_hidden_spawn_counter_gap() -> None:
#Rollout regression lock for the HIS disruptive item cluster:
#- The seed begins before an itemless gap, so future item IDs must be owned by the seeded
#global counter rather than reconstructed from currently - live items.
#- The older SpecialN laser overlaps a disabled - contact JumpF target at the same time the next
#laser spawns; the disabled - contact - only path must not use the fully scaled damaging BODY
#beam extension and falsely consume the carried shot.
#refs / melee / src / melee / it / item.c::Item_80267AA8
#refs / melee / src / melee / ft / ftcoll.c::ftColl_8007925C
#refs / melee / src / melee / it / itcoll.c::it_80272460
    dataset_path = _dataset_path()
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    start = 6548
    target = 6604

    seed_bytes = (
        np.frombuffer(samples[start : start + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, seed_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for rec in range(start, target + 1):
            prev_input_bytes = (
                np.frombuffer(samples[rec : rec + 1]["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            input_bytes = (
                np.frombuffer(samples[rec : rec + 1]["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    ref = samples[target]["ref_t1"]
    assert int(samples[start]["seed_t"]["item_spawn_id_counter"]) == 32
    assert _live_item_keys(ref) == [
        (0, 54, 0, 0, 1530, 35),
        (1, 54, 0, 0, 1533, 36),
    ]
    assert _live_item_keys(out) == _live_item_keys(ref)


def test_rollout_specialn_gun_laser_identity_and_metadata_follow_source_episode() -> None:
    # GAT rollout regression lock for the top F00 item cluster:
    # - Falco's SpecialN blaster gun is a parented item spawn and must copy fighter attack identity.
    # - The laser's Slippi metadata low bytes are owned by live foxlaser.scale/angle, not by a
    #   teacher-forced seed-only bridge.
    # The next ShieldBounced step still carries a hidden xC58 normal residual, so this lock stops at
    # the final pre-bounce laser row rather than treating the remaining normal as closed.
    # refs/melee/src/melee/it/it_2725.c::it_8027B070
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,itFoxlaser_UnkMotion1_Anim}
    # refs/slippi-ssbm-asm/Recording/SendItemInfo.s
    dataset_path = _gat_dataset_path()
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    start = 5257
    target = 5279

    seed_bytes = (
        np.frombuffer(samples[start : start + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, seed_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for rec in range(start, target + 1):
            prev_input_bytes = (
                np.frombuffer(samples[rec : rec + 1]["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            input_bytes = (
                np.frombuffer(samples[rec : rec + 1]["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    ref = samples[target]["ref_t1"]
    out_laser = out["items"][0]
    ref_laser = ref["items"][0]
    assert int(ref_laser["type"]) == 55
    assert int(out_laser["type"]) == int(ref_laser["type"])
    assert int(out_laser["attack_id"]) == int(ref_laser["attack_id"])
    assert int(out_laser["attack_instance"]) == int(ref_laser["attack_instance"])
    assert int(out_laser["misc0"]) == int(ref_laser["misc0"])
    assert int(out_laser["misc1"]) == int(ref_laser["misc1"])
    assert float(out_laser["vel_x"]) == pytest.approx(float(ref_laser["vel_x"]), abs=0.0)
    assert float(out_laser["vel_y"]) == pytest.approx(float(ref_laser["vel_y"]), abs=0.0)
