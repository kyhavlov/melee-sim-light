from __future__ import annotations

from pathlib import Path

import numpy as np

import melee_sim as msl
from melee_sim import dtypes


ROOT = Path(__file__).resolve().parents[1]


def test_python_wire_layout_matches_public_c_api() -> None:
    assert dtypes.controller_input_dtype().itemsize == 112
    assert dtypes.input_dtype().itemsize == 52
    assert dtypes.match_config_dtype().itemsize == 52
    assert dtypes.compare_dtype().itemsize == 1022
    assert dtypes.gamestate_dtype().itemsize == 980
    assert dtypes.terminal_dtype().itemsize == 16


def test_supported_character_and_stage_enums() -> None:
    assert [int(character) for character in msl.Character] == [
        1,
        2,
        7,
        9,
        15,
        18,
        19,
        22,
    ]
    assert [int(stage) for stage in msl.Stage] == [2, 3, 8, 28, 31, 32]


def test_envbatch_controller_step_and_arbitrary_restore(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=2, length=4) as env:
        neutral = msl.neutral_controller((env.length, env.batch_size))
        msl.write_controller(env.controller_action_view, neutral, player=0)
        msl.write_controller(env.controller_action_view, neutral, player=1)

        env.reset_all()
        assert np.array_equal(env.current_frame["frame_id"], [-123, -123])
        state = env.save(0)
        env.step(write_compare=True)
        expected = env.current_frame[0].copy()

        env.restore(1, state)
        env.step()
        assert env.current_frame[1].tobytes() == expected.tobytes()
        assert np.all(env.done_at(1) == 0)


def test_match_config_preserves_per_environment_values(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=2, length=1) as env:
        env.configure_matches(
            [
                msl.MatchConfig(stage=msl.Stage.BATTLEFIELD),
                msl.MatchConfig(
                    stage=msl.Stage.YOSHIS_STORY,
                    players=(
                        msl.PlayerConfig(msl.Character.PEACH),
                        msl.PlayerConfig(msl.Character.JIGGLYPUFF),
                    ),
                    frame_pre_random_seed=99,
                ),
            ]
        )
        config = env.match_config_view
        assert config[0]["stage_id"] == msl.Stage.BATTLEFIELD
        assert config[1]["stage_id"] == msl.Stage.YOSHIS_STORY
        assert config[1]["frame_pre_random_seed"] == 99
        assert np.array_equal(
            config[1]["players"]["char_id"][:2],
            [msl.Character.PEACH, msl.Character.JIGGLYPUFF],
        )
