from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


ACT_FALL = 29
ACT_DAMAGE_N_2 = 79
SM_FALL = 20
SM_DAMAGE_N_2 = 169


def _step_one(row: np.ndarray, *, num_players: int) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, seed_stride
    ).copy()
    prev_input_bytes = np.frombuffer(
        row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
    ).reshape(1, input_stride).copy()
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
        1, input_stride
    ).copy()
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=num_players,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


def _cheery_samples() -> tuple[np.ndarray, int]:
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/yoshis_story_recent/CheeryNumbMonkey.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = load_replay_buffers(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    return ds.rows, int(ds.num_players)


@pytest.mark.integration
def test_airborne_common_damage_anim_end_commits_visible_fall_timebase_cnm_1697() -> None:
    # Replay-real Yoshi's Story common-Damage anim-end lock:
    # - Slippi can expose grounded damage motion ids while the fighter is airborne.
    # - ftCo_Damage_Anim still branches on fp->ground_or_air and enters ftCo_Fall_Enter when the
    #   animation and x221C_b6 gates clear.
    # - The same-frame collision pass may still need the previous Damage pose, but the post-combat
    #   visible state must publish Fall's timebase/submotion rather than stale DamageN2.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
    samples, num_players = _cheery_samples()
    row = samples[1697:1698]
    p = 1

    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_N_2
    assert int(row["seed_t"]["animation_index"][0, p]) == SM_DAMAGE_N_2
    assert int(row["seed_t"]["on_ground"][0, p]) == 0
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_FALL
    assert int(row["ref_t1"]["animation_index"][0, p]) == SM_FALL
    assert int(row["ref_t1"]["action_frame"][0, p]) == 0

    out = _step_one(row, num_players=num_players)

    for field in ("action_id", "animation_index", "action_frame", "hitlag", "hitstun"):
        got = int(out[field][p])
        exp = int(row["ref_t1"][field][0, p])
        assert got == exp, f"field={field} expected={exp} got={got}"


@pytest.mark.integration
def test_airborne_common_damage_before_anim_end_keeps_damage_cnm_1696() -> None:
    # Adjacent negative boundary: this owner is only the terminal Damage_Anim handoff. The preceding
    # airborne DamageN2 frame keeps the common Damage timebase and must not be pulled into Fall early.
    samples, num_players = _cheery_samples()
    row = samples[1696:1697]
    p = 1

    assert int(row["seed_t"]["action_id"][0, p]) == ACT_DAMAGE_N_2
    assert int(row["seed_t"]["animation_index"][0, p]) == SM_DAMAGE_N_2
    assert int(row["ref_t1"]["action_id"][0, p]) == ACT_DAMAGE_N_2
    assert int(row["ref_t1"]["animation_index"][0, p]) == SM_DAMAGE_N_2

    out = _step_one(row, num_players=num_players)

    for field in ("action_id", "animation_index", "action_frame", "hitlag", "hitstun"):
        got = int(out[field][p])
        exp = int(row["ref_t1"][field][0, p])
        assert got == exp, f"field={field} expected={exp} got={got}"
