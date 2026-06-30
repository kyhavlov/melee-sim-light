from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, SEED_DTYPE
from tools.eval.mismatch_taxonomy import ITEM_FIELD_TO_SUBFIELD, NATIVE_FIELD_CODE_TO_NAME, PLAYER_FIELDS, _load_binding
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite
from tests.replay_dataset_loader import load_replay_dataset as read_dataset


def _reference_events(seed: np.ndarray, ref: np.ndarray, out: np.ndarray, num_players: int) -> list[tuple[int, ...]]:
    code_by_name = {name: code for code, name in NATIVE_FIELD_CODE_TO_NAME.items()}
    events: list[tuple[int, ...]] = []
    n = int(seed.shape[0])

    def native_i32(value) -> int:
        # collect_mismatch_events publishes scalar values through int32 arrays. Match that ABI for
        # unsigned sentinel fields such as animation_index == 0xFFFFFFFF.
        return int(np.asarray(value).astype(np.int32))

    for i in range(n):
        for p in range(num_players):
            for field in PLAYER_FIELDS:
                if field == "state_flags":
                    for sub in np.argwhere(out[field][i, p] != ref[field][i, p]).flatten().tolist():
                        sub_i = int(sub)
                        events.append(
                            (
                                0,
                                i,
                                p,
                                code_by_name[field],
                                sub_i,
                                int(seed["state_flags"][i, p, sub_i]),
                                int(ref["state_flags"][i, p, sub_i]),
                                int(out["state_flags"][i, p, sub_i]),
                            )
                        )
                    continue
                if out[field][i, p] == ref[field][i, p]:
                    continue
                seed_v = 1 if field == "is_dead" and int(seed["stocks"][i, p]) == 0 else int(seed[field][i, p])
                events.append(
                    (
                        0,
                        i,
                        p,
                        code_by_name[field],
                        -1,
                        native_i32(seed_v),
                        native_i32(ref[field][i, p]),
                        native_i32(out[field][i, p]),
                    )
                )
        for slot in range(out["items"].shape[1]):
            for field, subfield in ITEM_FIELD_TO_SUBFIELD.items():
                if out["items"][subfield][i, slot] == ref["items"][subfield][i, slot]:
                    continue
                events.append(
                    (
                        1,
                        i,
                        slot,
                        code_by_name[field],
                        -1,
                        native_i32(seed["items"][subfield][i, slot]),
                        native_i32(ref["items"][subfield][i, slot]),
                        native_i32(out["items"][subfield][i, slot]),
                    )
                )
    return events


def _as_native_bytes(rows: np.ndarray, stride: int) -> np.ndarray:
    return np.frombuffer(rows.tobytes(order="C"), dtype=np.uint8).reshape(int(rows.shape[0]), stride).copy()


def _event_tuples(native: dict[str, np.ndarray]) -> list[tuple[int, ...]]:
    return [
        (
            int(native["kind"][i]),
            int(native["record"][i]),
            int(native["subject"][i]),
            int(native["field_code"][i]),
            int(native["subindex"][i]),
            int(native["seed_value"][i]),
            int(native["ref_value"][i]),
            int(native["out_value"][i]),
        )
        for i in range(len(native["kind"]))
    ]


def test_native_mismatch_taxonomy_synthetic_binding_edges() -> None:
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    code_by_name = {name: code for code, name in NATIVE_FIELD_CODE_TO_NAME.items()}

    seed = np.zeros(1, dtype=SEED_DTYPE)
    ref = np.zeros(1, dtype=COMPARE_DTYPE)
    out = np.zeros(1, dtype=COMPARE_DTYPE)

    seed["frame_id"][0] = 10
    ref["frame_id"][0] = 11
    seed["action_id"][0, 0] = 7
    ref["action_id"][0, 0] = 8
    out["action_id"][0, 0] = 9
    seed["seed_prev_action_id"][0, 0] = 6
    seed["action_frame"][0, 0] = 4
    ref["action_frame"][0, 0] = 5
    out["action_frame"][0, 0] = 6
    seed["on_ground"][0, 0] = 1
    seed["hitlag"][0, 0] = 2
    seed["hitstun"][0, 0] = 3

    seed["stocks"][0, 0] = 0
    ref["stocks"][0, 0] = 2
    out["stocks"][0, 0] = 2
    ref["is_dead"][0, 0] = 0
    out["is_dead"][0, 0] = 1

    seed["state_flags"][0, 0, 3] = 0x12
    ref["state_flags"][0, 0, 3] = 0x34
    out["state_flags"][0, 0, 3] = 0x56

    slot = 2
    seed["items"]["exists"][0, slot] = 1
    seed["items"]["type"][0, slot] = 54
    seed["items"]["state"][0, slot] = 3
    seed["items"]["owner"][0, slot] = -1
    seed["items"]["instance_id"][0, slot] = 100
    ref["items"]["exists"][0, slot] = 0
    ref["items"]["type"][0, slot] = 0
    ref["items"]["state"][0, slot] = 0
    ref["items"]["owner"][0, slot] = -1
    ref["items"]["instance_id"][0, slot] = 0
    out["items"]["exists"][0, slot] = 1
    out["items"]["type"][0, slot] = 55
    out["items"]["state"][0, slot] = 4
    out["items"]["owner"][0, slot] = 0
    out["items"]["instance_id"][0, slot] = 101

    native = binding.collect_mismatch_events(
        _as_native_bytes(seed, seed_stride),
        _as_native_bytes(ref, compare_stride),
        _as_native_bytes(out, compare_stride),
        1,
    )

    assert _event_tuples(native) == [
        (0, 0, 0, code_by_name["action_id"], -1, 7, 8, 9),
        (0, 0, 0, code_by_name["action_frame"], -1, 4, 5, 6),
        (0, 0, 0, code_by_name["is_dead"], -1, 1, 0, 1),
        (0, 0, 0, code_by_name["state_flags"], 3, 0x12, 0x34, 0x56),
        (1, 0, slot, code_by_name["item_exists"], -1, 1, 0, 1),
        (1, 0, slot, code_by_name["item_type"], -1, 54, 0, 55),
        (1, 0, slot, code_by_name["item_state"], -1, 3, 0, 4),
        (1, 0, slot, code_by_name["item_owner"], -1, -1, -1, 0),
        (1, 0, slot, code_by_name["item_instance_id"], -1, 100, 0, 101),
    ]
    assert int(native["seed_frame"][0]) == 10
    assert int(native["ref_frame"][0]) == 11
    assert int(native["prev_action_id"][0]) == 6


def test_native_mismatch_taxonomy_error_contracts() -> None:
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])

    seed = _as_native_bytes(np.zeros(1, dtype=SEED_DTYPE), seed_stride)
    compare = _as_native_bytes(np.zeros(1, dtype=COMPARE_DTYPE), compare_stride)

    with pytest.raises(ValueError, match="num_players out of range"):
        binding.collect_mismatch_events(seed, compare, compare, 0)
    with pytest.raises(ValueError, match="num_players out of range"):
        binding.collect_mismatch_events(seed, compare, compare, 5)
    with pytest.raises(ValueError, match="must be at least 2D"):
        binding.collect_mismatch_events(seed.reshape(-1), compare, compare, 1)
    with pytest.raises(ValueError, match="byte strides must match native structs"):
        binding.collect_mismatch_events(seed[:, :-1].copy(), compare, compare, 1)


def test_native_mismatch_taxonomy_event_scan_matches_python_reference() -> None:
    suite = load_suite(Path("replays/suites/aggregate_recent.json"))
    ds_path = dataset_path_for_suite_replay(
        suite_name=suite.name,
        replay_rel_path=suite.replays[0].replay,
        datasets_dir=Path("datasets"),
    )
    ds = read_dataset(str(ds_path))
    samples = ds.samples[:4096]
    n = int(samples.shape[0])
    num_players = int(ds.header["num_players"])

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_b = np.frombuffer(samples["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(n, seed_stride).copy()
    prev_b = np.frombuffer(samples["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(n, input_stride).copy()
    in_b = np.frombuffer(samples["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(n, input_stride).copy()
    ref_b = np.frombuffer(samples["ref_t1"].tobytes(order="C"), dtype=np.uint8).reshape(n, compare_stride).copy()
    out_b = np.empty((n, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=n, num_players=num_players)
    try:
        binding.reseed_seed(handle, seed_b)
        binding.step_input(handle, prev_b, in_b)
        binding.write_compare(handle, out_b)
    finally:
        binding.destroy(handle)

    native = binding.collect_mismatch_events(seed_b, ref_b, out_b, num_players)
    got = [
        (
            int(native["kind"][i]),
            int(native["record"][i]),
            int(native["subject"][i]),
            int(native["field_code"][i]),
            int(native["subindex"][i]),
            int(native["seed_value"][i]),
            int(native["ref_value"][i]),
            int(native["out_value"][i]),
        )
        for i in range(len(native["kind"]))
    ]
    out = out_b.view(COMPARE_DTYPE).reshape(-1)
    want = _reference_events(samples["seed_t"], samples["ref_t1"], out, num_players)

    assert got == want
    assert len(got) > 0
