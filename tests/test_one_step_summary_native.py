from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

import msl_binding
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tools.eval.one_step_report import DISCRETE_FIELDS, FLOAT_FIELDS
from tools.eval.validation_profile import get_validation_profile
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views
from tools.slippi.suite_io import load_suite, repo_root


def _suite_dataset(*, suite_rel: str, replay_name: str, limit: int):
    root = repo_root()
    suite = load_suite(root / suite_rel)
    for entry in suite.replays:
        if Path(entry.replay).name == replay_name:
            ds = load_replay_buffers(
                slp_path=str(root / entry.replay),
                ports=[int(p) for p in entry.ports],
                ucf_enabled=bool(suite.ucf_enabled),
                ucf_cardinals_1_0_enabled=bool(suite.ucf_cardinals_1_0_enabled),
            )
            return suite, ds.rows[:limit], int(ds.num_players)
    raise AssertionError(f"{replay_name} not found in {suite_rel}")


def _group_u8(arr: np.ndarray) -> np.ndarray:
    contiguous = np.ascontiguousarray(arr)
    return contiguous.view(np.uint8).reshape(int(contiguous.shape[0]), int(contiguous.dtype.itemsize))


def _simulate_outputs(samples, *, num_players: int, chunk: int, ucf_enabled: bool, ucf_cardinals: bool):
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    handle = msl_binding.init(
        batch_size=min(int(chunk), max(1, int(samples.shape[0]))),
        num_players=int(num_players),
        ucf_enabled=int(bool(ucf_enabled)),
        ucf_cardinals_1_0_enabled=int(bool(ucf_cardinals)),
    )
    out = np.empty((int(samples.shape[0]), compare_stride), dtype=np.uint8)
    seed_bytes = np.empty((min(int(chunk), max(1, int(samples.shape[0]))), seed_stride), dtype=np.uint8)
    prev_bytes = np.empty((seed_bytes.shape[0], input_stride), dtype=np.uint8)
    input_bytes = np.empty((seed_bytes.shape[0], input_stride), dtype=np.uint8)
    chunk_out = np.empty((seed_bytes.shape[0], compare_stride), dtype=np.uint8)
    seed_u8 = _group_u8(samples["seed_t"])
    prev_u8 = _group_u8(samples["prev_input_t"])
    input_u8 = _group_u8(samples["input_t"])
    try:
        for off in range(0, int(samples.shape[0]), int(chunk)):
            n = min(int(chunk), int(samples.shape[0]) - off)
            seed_bytes[:n] = seed_u8[off : off + n, :seed_stride]
            prev_bytes[:n] = prev_u8[off : off + n, :input_stride]
            input_bytes[:n] = input_u8[off : off + n, :input_stride]
            if n < seed_bytes.shape[0]:
                seed_bytes[n:] = seed_bytes[0]
                prev_bytes[n:] = prev_bytes[0]
                input_bytes[n:] = input_bytes[0]
            msl_binding.reseed_seed(handle, seed_bytes)
            msl_binding.step_input(handle, prev_bytes, input_bytes)
            msl_binding.write_compare(handle, chunk_out)
            out[off : off + n] = chunk_out[:n]
    finally:
        msl_binding.destroy(handle)
    return out


def _python_summary(out_bytes: np.ndarray, samples: np.ndarray, *, num_players: int, profile_name: str):
    profile = get_validation_profile(profile_name)
    out = out_bytes.view(COMPARE_DTYPE).reshape(-1)
    ref = samples["ref_t1"]
    active = slice(0, int(num_players))
    mismatches = {field: 0 for field in DISCRETE_FIELDS}
    strict = {field: 0 for field in DISCRETE_FIELDS}
    for field in DISCRETE_FIELDS[:18]:
        count = int((out[field][:, active] != ref[field][:, active]).sum())
        mismatches[field] = count
        strict[field] = count

    state_xor = out["state_flags"][:, active, :].astype(np.uint16) ^ ref["state_flags"][
        :, active, :
    ].astype(np.uint16)
    strict["state_flags"] = int((state_xor != 0).sum())
    scored_xor = state_xor.copy()
    ignored = {lane.label: 0 for lane in profile.ignored_lanes}
    for lane in profile.ignored_lanes:
        if lane.field != "state_flags":
            continue
        if lane.bitmask is None:
            lane_diff = state_xor[:, :, lane.subindex] != 0
            ignored[lane.label] = int(lane_diff.sum())
            scored_xor[:, :, lane.subindex] = 0
        else:
            mask = int(lane.bitmask) & 0xFF
            lane_diff = (state_xor[:, :, lane.subindex] & mask) != 0
            ignored[lane.label] = int(lane_diff.sum())
            scored_xor[:, :, lane.subindex] &= np.uint16(~mask & 0xFF)
    mismatches["state_flags"] = int((scored_xor != 0).sum())

    out_items = out["items"]
    ref_items = ref["items"]
    for field, subfield in (
        ("item_exists", "exists"),
        ("item_type", "type"),
        ("item_state", "state"),
        ("item_owner", "owner"),
        ("item_instance_id", "instance_id"),
    ):
        count = int((out_items[subfield] != ref_items[subfield]).sum())
        mismatches[field] = count
        strict[field] = count

    float_metrics = {}
    norm_sum = 0.0
    norm_count = 0
    for field in FLOAT_FIELDS:
        if field.startswith("item_"):
            sub = field.replace("item_", "")
            mask = ref_items["exists"].astype(bool)
            err = out_items[sub].astype(np.float32) - ref_items[sub].astype(np.float32)
            err = err[mask].reshape(-1)
            ref_abs = np.abs(ref_items[sub].astype(np.float32)[mask].reshape(-1))
        else:
            err = out[field][:, active].astype(np.float32) - ref[field][:, active].astype(np.float32)
            err = err.reshape(-1)
            ref_abs = np.abs(ref[field][:, active].astype(np.float32)).reshape(-1)
        abs_err = np.abs(err)
        if abs_err.size == 0:
            float_metrics[field] = ("0.000000", "0.000000", "0.000000")
            continue
        scale = float(np.quantile(ref_abs, 0.95))
        if scale <= 0.0:
            scale = 1e-6
        norm_sum += float(abs_err.sum()) / scale
        norm_count += int(abs_err.size)
        float_metrics[field] = (
            f"{float(abs_err.mean()):.6f}",
            f"{float(np.quantile(abs_err, 0.95)):.6f}",
            f"{float(abs_err.max()):.6f}",
        )
    return mismatches, strict, ignored, float_metrics, f"{(norm_sum / norm_count):.8f}"


def _native_summary(samples, *, num_players: int, profile_name: str, ucf_enabled: bool, ucf_cardinals: bool):
    handle = msl_binding.init(
        batch_size=512,
        num_players=int(num_players),
        ucf_enabled=int(bool(ucf_enabled)),
        ucf_cardinals_1_0_enabled=int(bool(ucf_cardinals)),
    )
    try:
        return msl_binding.one_step_eval_buffers(
            handle,
            _group_u8(samples["seed_t"]),
            _group_u8(samples["prev_input_t"]),
            _group_u8(samples["input_t"]),
            _group_u8(samples["ref_t1"]),
            int(num_players),
            int(profile_name == "rl1_gameplay"),
        )
    finally:
        msl_binding.destroy(handle)


@pytest.mark.parametrize(
    ("label", "suite_rel", "replay_name", "limit", "profile_name"),
    [
        ("normal_rl1", "replays/suites/aggregate_recent.json", "AttachedGoodNaturedGuanaco.slpz", 1024, "rl1_gameplay"),
        ("normal_strict", "replays/suites/aggregate_recent.json", "AttachedGoodNaturedGuanaco.slpz", 1024, "strict"),
        ("sheik_rl1", "replays/suites/aggregate_recent.json", "StiffLustrousZebra.slpz", 2048, "rl1_gameplay"),
        ("dream_land_rl1", "replays/suites/aggregate_recent.json", "WellWornSmallGoshawk.slpz", 2048, "rl1_gameplay"),
        ("doubles_rl1", "replays/suites/doubles_recent.json", "Game_20260509T152622.slpz", 2048, "rl1_gameplay"),
        ("doubles_strict", "replays/suites/doubles_recent.json", "Game_20260509T152622.slpz", 2048, "strict"),
    ],
)
def test_native_one_step_summary_matches_python_oracle(
    label: str, suite_rel: str, replay_name: str, limit: int, profile_name: str
) -> None:
    suite, samples, num_players = _suite_dataset(suite_rel=suite_rel, replay_name=replay_name, limit=limit)
    out = _simulate_outputs(
        samples,
        num_players=num_players,
        chunk=512,
        ucf_enabled=bool(suite.ucf_enabled),
        ucf_cardinals=bool(suite.ucf_cardinals_1_0_enabled),
    )
    native = _native_summary(
        samples,
        num_players=num_players,
        profile_name=profile_name,
        ucf_enabled=bool(suite.ucf_enabled),
        ucf_cardinals=bool(suite.ucf_cardinals_1_0_enabled),
    )
    py_mismatch, py_strict, py_ignored, py_float, py_norm = _python_summary(
        out, samples, num_players=num_players, profile_name=profile_name
    )

    assert dict(zip(DISCRETE_FIELDS, native["mismatches"], strict=True)) == py_mismatch, label
    assert dict(zip(DISCRETE_FIELDS, native["strict_mismatches"], strict=True)) == py_strict, label
    for ignored_label, value in py_ignored.items():
        if ignored_label == "state_flags[4]&0x80":
            assert int(native["ignored_state_flags_4_0x80"]) == value
    for field in FLOAT_FIELDS:
        metrics = native["float_metrics"][field]
        got = (f"{float(metrics['mae']):.6f}", f"{float(metrics['p95']):.6f}", f"{float(metrics['max']):.6f}")
        assert got == py_float[field], (label, field)
    native_norm = float(native["float_norm_sum"]) / int(native["float_norm_count"])
    assert f"{native_norm:.8f}" == py_norm, label
