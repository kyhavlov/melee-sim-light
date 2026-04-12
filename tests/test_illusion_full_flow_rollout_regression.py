from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

from tests.test_items_spawn_joint_replay_real_locks import _skip_if_required_artifacts_missing


_PLAYER_FIELDS = (
    "action_id",
    "action_frame",
    "animation_index",
    "on_ground",
    "hitlag",
    "hitstun",
)

_PLAYER_FLOAT_FIELDS = (
    "percent",
    "shield_hp",
    "pos_x",
    "pos_y",
)

_ITEM_FIELDS = (
    "exists",
    "type",
    "state",
    "owner",
    "instance_id",
)

_ITEM_FLOAT_FIELDS = (
    "pos_x",
    "pos_y",
    "timer",
)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    start_record: int
    length: int
    slot: int
    note: str


def _rollout_window(dataset_path: Path, start_record: int, length: int) -> list[tuple[np.void, np.void]]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    row = samples[start_record : start_record + 1]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = (
        np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    history: list[tuple[np.void, np.void]] = []
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_in = prev_input_bytes
        for rec in range(start_record, start_record + length):
            inp = (
                np.frombuffer(samples[rec : rec + 1]["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            binding.step_input(handle, prev_in, inp)
            binding.write_compare(handle, out_compare_bytes)
            out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            ref = samples[rec : rec + 1]["ref_t1"][0].copy()
            history.append((out, ref))
            prev_in = inp
    finally:
        binding.destroy(handle)
    return history


def _rollout_window_with_seed_override(
    dataset_path: Path,
    start_record: int,
    length: int,
    override_fn,
) -> list[tuple[np.void, np.void]]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    row = samples[start_record : start_record + 1].copy()
    override_fn(row["seed_t"])

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = (
        np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    history: list[tuple[np.void, np.void]] = []
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        prev_in = prev_input_bytes
        for rec in range(start_record, start_record + length):
            inp = (
                np.frombuffer(samples[rec : rec + 1]["input_t"].tobytes(order="C"), dtype=np.uint8)
                .copy()
                .reshape(1, input_stride)
            )
            binding.step_input(handle, prev_in, inp)
            binding.write_compare(handle, out_compare_bytes)
            out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            ref = samples[rec : rec + 1]["ref_t1"][0].copy()
            history.append((out, ref))
            prev_in = inp
    finally:
        binding.destroy(handle)
    return history


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            start_record=1428,
            length=6,
            slot=0,
            note="grounded side-B shield flow stays replay-real across spawn, travel, shield hit, and end",
        ),
        _Case(
            dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            start_record=6514,
            length=4,
            slot=0,
            note="airborne side-B body-hit flow stays replay-real across spawn, travel, and hit",
        ),
    ],
)
def test_illusion_rollout_windows_match_replay_real(case: _Case) -> None:
    # End-to-end Side-B/Illusion flow lock:
    # - grounded AGN raw frames 1306..1311 show spawn on main frame 2, shield hit on main frame 3,
    #   then the article stays frozen at the shield-contact point through GuardSetOff hitlag.
    # - airborne AGN raw frames 6393..6396 show spawn on air-main frame 1 and body hit on air-main
    #   frame 2 from the older ghost ring position.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    #   ftFox_SpecialS_SetVars,ftFox_SpecialS_SetPhys,ftFx_SpecialS_CreateGhostItem}
    # refs/melee/src/melee/it/items/itfoxillusion.c::{
    #   itFoxillusion_UnkMotion0_Anim,itFoxillusion_UnkMotion0_Phys,
    #   itFoxillusion_UnkMotion1_Anim,itFoxillusion_UnkMotion1_Phys}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    history = _rollout_window(dataset_path, case.start_record, case.length)
    slot = int(case.slot)

    for step_i, (out, ref) in enumerate(history):
        for p in range(2):
            for field in _PLAYER_FIELDS:
                assert int(out[field][p]) == int(ref[field][p]), (
                    f"{case.note}: step={step_i} p={p} field={field}"
                )
            for field in _PLAYER_FLOAT_FIELDS:
                assert float(out[field][p]) == pytest.approx(float(ref[field][p]), abs=5e-6), (
                    f"{case.note}: step={step_i} p={p} field={field}"
                )

        for field in _ITEM_FIELDS:
            assert int(out["items"][slot][field]) == int(ref["items"][slot][field]), (
                f"{case.note}: step={step_i} slot={slot} field={field}"
            )

        if int(ref["items"][slot]["exists"]):
            for field in _ITEM_FLOAT_FIELDS:
                assert float(out["items"][slot][field]) == pytest.approx(
                    float(ref["items"][slot][field]), abs=5e-6
                ), f"{case.note}: step={step_i} slot={slot} field={field}"


@pytest.mark.integration
def test_illusion_main_entry_resets_ground_ring_before_spawn_progression() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    if not dataset_path.exists():
        pytest.skip("missing local dataset")

    def _poison(seed_t: np.ndarray) -> None:
        seed_t["illusion_ghost_pos0_x"][0, 0] = np.float32(-999.0)
        seed_t["illusion_ghost_pos0_y"][0, 0] = np.float32(-999.0)
        seed_t["illusion_ghost_pos1_x"][0, 0] = np.float32(-999.0)
        seed_t["illusion_ghost_pos1_y"][0, 0] = np.float32(-999.0)

    history = _rollout_window_with_seed_override(dataset_path, start_record=1427, length=3, override_fn=_poison)
    for step_i, (out, ref) in enumerate(history):
        for field in _PLAYER_FIELDS:
            assert int(out[field][0]) == int(ref[field][0]), f"ground start->main ring reset: step={step_i} field={field}"
        if int(ref["items"][0]["exists"]):
            assert float(out["items"][0]["pos_x"]) == pytest.approx(float(ref["items"][0]["pos_x"]), abs=5e-6)
            assert float(out["items"][0]["pos_y"]) == pytest.approx(float(ref["items"][0]["pos_y"]), abs=5e-6)


@pytest.mark.integration
def test_illusion_main_entry_resets_air_ring_before_first_hit_frame() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    if not dataset_path.exists():
        pytest.skip("missing local dataset")

    def _poison(seed_t: np.ndarray) -> None:
        seed_t["illusion_ghost_pos0_x"][0, 0] = np.float32(-999.0)
        seed_t["illusion_ghost_pos0_y"][0, 0] = np.float32(-999.0)
        seed_t["illusion_ghost_pos1_x"][0, 0] = np.float32(-999.0)
        seed_t["illusion_ghost_pos1_y"][0, 0] = np.float32(-999.0)

    history = _rollout_window_with_seed_override(dataset_path, start_record=6514, length=3, override_fn=_poison)
    target_out, target_ref = history[2]
    assert int(target_out["action_id"][0]) == int(target_ref["action_id"][0])
    assert int(target_out["hitlag"][0]) == int(target_ref["hitlag"][0])
    assert float(target_out["percent"][0]) == pytest.approx(float(target_ref["percent"][0]), abs=5e-6)
    assert int(target_ref["items"][0]["exists"]) == 1
    assert float(target_out["items"][0]["pos_x"]) == pytest.approx(float(target_ref["items"][0]["pos_x"]), abs=5e-6)
    assert float(target_out["items"][0]["pos_y"]) == pytest.approx(float(target_ref["items"][0]["pos_y"]), abs=5e-6)
