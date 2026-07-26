from __future__ import annotations

from pathlib import Path

import numpy as np

import melee_sim as msl
from melee_sim import dtypes


ROOT = Path(__file__).resolve().parents[1]


def test_python_wire_layout_matches_public_c_api() -> None:
    assert dtypes.controller_input_dtype().itemsize == 112
    assert dtypes.input_dtype().itemsize == 32
    assert dtypes.match_config_dtype().itemsize == 48
    assert dtypes.gamestate_dtype().itemsize == 980
    assert dtypes.terminal_dtype().itemsize == 16

    buffers = msl.Buffers.empty(length=2, batch_size=3)
    players = buffers.controller_action_view["players"]
    assert np.all(players["main_stick_x"] == 0.5)
    assert np.all(players["main_stick_y"] == 0.5)


def test_supported_character_and_stage_enums() -> None:
    assert [int(character) for character in msl.Character] == [
        1,
        2,
        7,
        9,
        13,
        15,
        17,
        0,
        21,
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
        env.step()
        expected = env.current_frame[0].copy()

        env.restore(1, state)
        env.observe()
        assert env.current_frame[1]["frame_id"] == -123
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
                    seed=99,
                ),
            ]
        )
        config = env.match_config_view
        assert config[0]["stage"] == msl.Stage.BATTLEFIELD
        assert config[1]["stage"] == msl.Stage.YOSHIS_STORY
        assert config[1]["random_seed"] == 99
        assert np.array_equal(
            config[1]["players"]["character"][:2],
            [msl.Character.PEACH, msl.Character.JIGGLYPUFF],
        )


def test_reset_cursor_carries_current_frame(monkeypatch) -> None:
    # Regression test: reset_cursor used to only rewind the ring index, so a
    # masked reset right after the wrap exposed stale slot-0 rows (from
    # reset_all) for every non-reset match.
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=2, length=4) as env:
        neutral = msl.neutral_controller((env.length, env.batch_size))
        msl.write_controller(env.controller_action_view, neutral, player=0)
        msl.write_controller(env.controller_action_view, neutral, player=1)

        env.reset_all()
        for _ in range(env.length):
            env.step()
        assert env.t == env.length
        before = env.current_frame.copy()
        assert np.all(before["frame_id"] == -123 + env.length)

        # A partial reset is valid even on the final ring slot and must only
        # touch the selected row.
        env.reset_matches([0])
        assert env.current_frame[0]["frame_id"] == -123
        assert env.current_frame[1].tobytes() == before[1].tobytes()

        patched = env.current_frame.copy()
        env.reset_cursor()
        assert env.t == 0
        assert env.current_frame.tobytes() == patched.tobytes()


def test_step_and_reset_defers_resets_one_step(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=2, length=4) as env:
        neutral = msl.neutral_controller((env.length, env.batch_size))
        msl.write_controller(env.controller_action_view, neutral, player=0)
        msl.write_controller(env.controller_action_view, neutral, player=1)
        env.configure_matches(
            [
                msl.MatchConfig(max_frame=0),
                msl.MatchConfig(),
            ]
        )
        env.reset_all()

        # Match 0 hits max_frame after 123 steps (frame_id counts up from
        # -123); the ring wraps many times along the way.
        for _ in range(123):
            is_resetting, terminal = env.step_and_reset()
        # The terminal frame itself is published before any reset happens.
        assert not is_resetting.any()
        assert terminal["done"].tolist() == [1, 0]
        assert env.current_frame[0]["frame_id"] == 0
        assert env.current_frame[1]["frame_id"] == 0

        # The following call resets match 0 (ignoring its queued action) and
        # publishes its entry frame while match 1 steps normally.
        is_resetting, terminal = env.step_and_reset()
        assert is_resetting.tolist() == [True, False]
        assert terminal["done"].tolist() == [0, 0]
        assert env.current_frame[0]["frame_id"] == -123
        assert env.current_frame[1]["frame_id"] == 1


def test_masked_step_republishes_unstepped_matches(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=2, length=4) as env:
        neutral = msl.neutral_controller((env.length, env.batch_size))
        msl.write_controller(env.controller_action_view, neutral, player=0)
        msl.write_controller(env.controller_action_view, neutral, player=1)
        env.reset_all()
        env.step()
        before = env.current_frame.copy()

        env.step(np.array([1, 0], dtype=np.uint8))
        assert env.current_frame[0]["frame_id"] == before[0]["frame_id"] + 1
        assert env.current_frame[1].tobytes() == before[1].tobytes()
