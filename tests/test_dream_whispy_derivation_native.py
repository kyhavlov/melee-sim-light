import numpy as np
import pytest

import msl_binding
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tools.slippi.validation_buffer_builder import build_validation_buffers_from_slp
from tools.slippi.validation_buffer_items import (
    _dream_whispy_params,
    _structured_rows_as_bytes,
)
from tools.slippi.suite_io import load_suite, repo_root
from tools.slippi.slpz import resolve_replay_path


DREAM_LAND_REPLAYS = (
    ("replays/suites/aggregate_recent.json", "FlippantEnchantedHorse.slpz"),
    ("replays/suites/aggregate_recent.json", "ShadyDecimalStarling.slpz"),
    ("replays/suites/aggregate_recent.json", "QuestionableHarmfulPanther.slpz"),
    ("replays/suites/aggregate_recent.json", "WellWornSmallGoshawk.slpz"),
    ("replays/suites/aggregate_recent.json", "BreakableMundaneElephant.slpz"),
    ("replays/suites/aggregate_recent.json", "ColossalYellowishBison.slpz"),
    ("replays/suites/aggregate_recent.json", "MixedAllQuetzal.slpz"),
    ("replays/suites/aggregate_recent.json", "UnusedLivelyLouse.slpz"),
    ("replays/suites/aggregate_recent.json", "ToughOutlyingChicken.slpz"),
)


def _full_batch_whispy_oracle(
    *,
    seed_t: np.ndarray,
    prev_input_t: np.ndarray,
    input_t: np.ndarray,
    ref_t1: np.ndarray,
    num_players: int,
):
    params = _dream_whispy_params()
    n = int(seed_t.shape[0])
    out_dir = np.zeros(n, dtype=np.uint8)
    valid = np.zeros(n, dtype=np.uint8)
    timer = np.zeros(n, dtype=np.uint16)
    if n == 0:
        return out_dir, valid, timer

    seed_bytes = _structured_rows_as_bytes(seed_t)
    prev_input_bytes = _structured_rows_as_bytes(prev_input_t)
    input_bytes = _structured_rows_as_bytes(input_t)
    compare_stride = int(msl_binding.sizes()["compare"])
    out_bytes = np.empty((n, compare_stride), dtype=np.uint8)
    handle = msl_binding.init(
        batch_size=n,
        num_players=num_players,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_bytes)
    finally:
        msl_binding.destroy(handle)

    out = out_bytes.view(COMPARE_DTYPE).reshape(n)
    ref = ref_t1
    seed = seed_t
    stage_id = int(params.stage_id)
    wind_speed = float(params.wind_speed)
    eps = 0.025
    for i in range(n - 1):
        if (
            int(seed["stage_id"][i]) != stage_id
            or int(seed["stage_id"][i + 1]) != stage_id
            or int(seed["frame_id"][i + 1]) != int(seed["frame_id"][i]) + 1
        ):
            continue
        row_dir = 0
        conflict = False
        for p in range(num_players):
            diff = float(ref["pos_x"][i, p]) - float(out["pos_x"][i, p])
            if abs(abs(diff) - wind_speed) > eps:
                continue
            d = 1 if diff < 0.0 else 2
            if row_dir != 0 and row_dir != d:
                conflict = True
                break
            row_dir = d
        if row_dir != 0 and not conflict:
            out_dir[i + 1] = row_dir
            valid[i + 1] = 1

    prev_sparse_i = -1
    prev_sparse_dir = 0
    for i in range(n):
        d = int(out_dir[i])
        if valid[i] == 0 or d not in (1, 2):
            continue
        if prev_sparse_i >= 0 and d == prev_sparse_dir and i - prev_sparse_i <= 274:
            contiguous = True
            for j in range(prev_sparse_i, i):
                if (
                    int(seed["stage_id"][j]) != stage_id
                    or int(seed["stage_id"][j + 1]) != stage_id
                    or int(seed["frame_id"][j + 1]) != int(seed["frame_id"][j]) + 1
                ):
                    contiguous = False
                    break
            if contiguous:
                out_dir[prev_sparse_i + 1 : i] = d
                valid[prev_sparse_i + 1 : i] = 1
        prev_sparse_i = i
        prev_sparse_dir = d

    episode_dir = 0
    episode_age = 0
    for i in range(n):
        if valid[i] == 0 or out_dir[i] == 0:
            episode_dir = 0
            episode_age = 0
            continue
        if int(out_dir[i]) != episode_dir:
            episode_dir = int(out_dir[i])
            episode_age = 0
        timer[i] = 274 - episode_age if episode_age < 274 else 1
        if episode_age < 274:
            episode_age += 1
    return out_dir, valid, timer


@pytest.mark.parametrize(("suite_rel", "replay_name"), DREAM_LAND_REPLAYS)
def test_dream_whispy_chunked_sim_matches_full_batch_oracle(
    suite_rel: str, replay_name: str
) -> None:
    root = repo_root()
    suite = load_suite(root / suite_rel)
    entry = next(e for e in suite.replays if e.replay.endswith(replay_name))
    replay_path = resolve_replay_path(root / entry.replay)
    buffers = build_validation_buffers_from_slp(
        slp_path=str(replay_path),
        ports=[int(p) for p in entry.ports],
        ucf_enabled=bool(suite.ucf_enabled),
        ucf_cardinals_1_0_enabled=bool(suite.ucf_cardinals_1_0_enabled),
    )
    seed_t = buffers.seed_t.copy()
    prev_input_t = buffers.prev_input_t.copy()
    input_t = buffers.input_t.copy()
    ref_t1 = buffers.ref_t1.copy()
    for field in (
        "stage_dream_whispy_wind_dir_u8",
        "stage_dream_whispy_wind_valid_u8",
        "stage_dream_whispy_wind_timer_u16",
    ):
        seed_t[field] = 0

    params = _dream_whispy_params()
    native = msl_binding.derive_dream_whispy_wind_seed_lanes(
        _structured_rows_as_bytes(seed_t),
        _structured_rows_as_bytes(prev_input_t),
        _structured_rows_as_bytes(input_t),
        _structured_rows_as_bytes(ref_t1),
        int(buffers.num_players),
        int(params.stage_id),
        float(params.wind_speed),
        0.025,
    )
    oracle = _full_batch_whispy_oracle(
        seed_t=seed_t,
        prev_input_t=prev_input_t,
        input_t=input_t,
        ref_t1=ref_t1,
        num_players=int(buffers.num_players),
    )

    for actual, expected in zip(native, oracle, strict=True):
        np.testing.assert_array_equal(actual, expected)
    if replay_name == "WellWornSmallGoshawk.slpz":
        assert int(native[1][961]) == 1
        assert int(native[2][961]) == 206
