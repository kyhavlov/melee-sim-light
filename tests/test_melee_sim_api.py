from __future__ import annotations

import os
from pathlib import Path

import numpy as np
import pytest

import melee_sim as msl
import melee_sim.dtypes as msl_dtypes
from melee_sim.env_batch import _resolve_data_dir


ROOT = Path(__file__).resolve().parents[1]


def _populate_data_overlay_without_legacy_script_owner_splits(dst_data_dir: Path) -> None:
    src_data_dir = ROOT / "data"
    dst_data_dir.mkdir(parents=True, exist_ok=True)
    exclude_roots = {"airborne_state_events", "hit_status", "hurtbox_states", "state_flags_221c_y"}

    for src in src_data_dir.rglob("*"):
        rel = src.relative_to(src_data_dir)
        if rel.parts and rel.parts[0] in exclude_roots:
            continue
        dst = dst_data_dir / rel
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.link(src, dst)
        except OSError:
            dst.write_bytes(src.read_bytes())


def test_default_data_dir_is_dot_msl(monkeypatch, tmp_path) -> None:
    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("MELEE_SIM_DATA", raising=False)
    monkeypatch.delenv("MSL_DATA_DIR", raising=False)
    (tmp_path / ".msl").mkdir()

    assert _resolve_data_dir(None) == str(tmp_path / ".msl")
    assert os.environ["MSL_DATA_DIR"] == str(tmp_path / ".msl")
    os.environ.pop("MSL_DATA_DIR", None)


def test_source_checkout_data_dir_fallback(monkeypatch, tmp_path) -> None:
    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("MELEE_SIM_DATA", raising=False)
    monkeypatch.delenv("MSL_DATA_DIR", raising=False)
    (tmp_path / "data").mkdir()

    assert _resolve_data_dir(None) == str(tmp_path / "data")
    assert os.environ["MSL_DATA_DIR"] == str(tmp_path / "data")
    os.environ.pop("MSL_DATA_DIR", None)


def test_envbatch_uses_mslftsc1_without_legacy_script_owner_splits(tmp_path) -> None:
    data_dir = tmp_path / "data"
    _populate_data_overlay_without_legacy_script_owner_splits(data_dir)
    assert not (data_dir / "hit_status").exists()
    assert not (data_dir / "hurtbox_states").exists()
    assert (data_dir / "scripts" / "fox.bin").exists()
    assert (data_dir / "scripts" / "falco.bin").exists()

    with msl.EnvBatch(batch_size=1, length=2, data_dir=data_dir) as env:
        buffers = env.buffers()
        env.configure_match(buffers)
        controller = msl.neutral_controller((buffers.length, buffers.batch_size))
        msl.write_controller(buffers.controller_action_view, controller, player=0)
        msl.write_controller(buffers.controller_action_view, controller, player=1)
        env.bind(buffers)
        env.reset_all()
        env.step()


def test_buffers_controller_api_steps_and_writes_done() -> None:
    with msl.EnvBatch(batch_size=4, length=8) as env:
        buffers = env.buffers()
        env.configure_match(buffers)
        controller = msl.neutral_controller((buffers.length, buffers.batch_size))
        msl.write_controller(buffers.controller_action_view, controller, player=0)
        msl.write_controller(buffers.controller_action_view, controller, player=1)

        env.bind(buffers)
        env.reset_all()
        env.step()

        assert buffers.action_format == "controller"
        assert buffers.controller_action_view.shape == (8, 4)
        assert buffers.done.shape == (8, 4)
        assert np.all(buffers.done[0] == 0)
        assert env.t == 1


def test_step_cursor_raises_at_length_and_can_rewind() -> None:
    with msl.EnvBatch(batch_size=1, length=1) as env:
        buffers = env.buffers()
        env.configure_match(buffers)
        controller = msl.neutral_controller((env.length, env.batch_size))
        msl.write_controller(buffers.controller_action_view, controller, player=0)
        msl.write_controller(buffers.controller_action_view, controller, player=1)
        env.bind(buffers)
        env.reset_all()

        env.step()
        assert env.t == 1
        with pytest.raises(RuntimeError, match="buffer length exhausted"):
            env.step()

        env.reset_cursor()
        assert env.t == 0


def test_configure_match_populates_default_fox_falco_match() -> None:
    with msl.EnvBatch(batch_size=4, length=8) as env:
        buffers = env.buffers()
        env.configure_match(buffers)

        cfg = buffers.match_config_view
        assert np.all(cfg["stage_id"] == msl.Stage.FINAL_DESTINATION)
        assert np.all(cfg["frame_id"] == -123)
        assert np.array_equal(cfg["frame_pre_random_seed"], np.arange(env.batch_size, dtype=np.uint32))
        assert np.all(cfg["match_damage_ratio"] == 1.0)
        assert np.all(cfg["num_players"] == 2)
        assert np.all(cfg["stock_count"] == 4)
        assert np.all(cfg["players"]["char_id"][:, 0] == msl.Character.FOX)
        assert np.all(cfg["players"]["char_id"][:, 1] == msl.Character.FALCO)
        assert np.all(cfg["players"]["team_id"][:, 0] == 0)
        assert np.all(cfg["players"]["team_id"][:, 1] == 1)
        assert np.all(cfg["players"]["facing"][:, 0] == 1)
        assert np.all(cfg["players"]["facing"][:, 1] == 0)


def test_configure_match_accepts_explicit_constants_and_seed() -> None:
    with msl.EnvBatch(batch_size=2, length=4) as env:
        buffers = env.buffers()
        env.configure_match(
            buffers,
            stage=msl.Stage.BATTLEFIELD,
            players=[
                msl.PlayerConfig(character=msl.Character.FALCO, team_id=7, facing=0),
                msl.PlayerConfig(character=msl.Character.FOX, team_id=9, facing=1),
            ],
            frame_pre_random_seed=np.array([123, 456], dtype=np.uint32),
        )

        cfg = buffers.match_config_view
        assert np.all(cfg["stage_id"] == msl.Stage.BATTLEFIELD)
        assert np.array_equal(cfg["frame_pre_random_seed"], np.array([123, 456], dtype=np.uint32))
        assert np.all(cfg["players"]["char_id"][:, 0] == msl.Character.FALCO)
        assert np.all(cfg["players"]["char_id"][:, 1] == msl.Character.FOX)
        assert np.all(cfg["players"]["team_id"][:, 0] == 7)
        assert np.all(cfg["players"]["team_id"][:, 1] == 9)
        assert np.all(cfg["players"]["facing"][:, 0] == 0)
        assert np.all(cfg["players"]["facing"][:, 1] == 1)


def test_configure_matches_accepts_per_lane_configs_and_partial_update() -> None:
    with msl.EnvBatch(batch_size=4, length=8) as env:
        buffers = env.buffers()
        env.configure_match(buffers)
        env.configure_matches(
            buffers,
            [
                msl.MatchConfig(
                    stage=msl.Stage.BATTLEFIELD,
                    players=(
                        msl.PlayerConfig(character=msl.Character.FALCO),
                        msl.PlayerConfig(character=msl.Character.FOX),
                    ),
                    frame_pre_random_seed=100,
                ),
                msl.MatchConfig(
                    stage=msl.Stage.YOSHIS_STORY,
                    players=(
                        msl.PlayerConfig(character=msl.Character.FOX),
                        msl.PlayerConfig(character=msl.Character.FALCO),
                    ),
                    frame_pre_random_seed=200,
                ),
            ],
            env_ids=[1, 3],
        )

        cfg = buffers.match_config_view
        assert np.all(cfg["stage_id"][[0, 2]] == msl.Stage.FINAL_DESTINATION)
        assert int(cfg["stage_id"][1]) == msl.Stage.BATTLEFIELD
        assert int(cfg["stage_id"][3]) == msl.Stage.YOSHIS_STORY
        assert int(cfg["frame_pre_random_seed"][1]) == 100
        assert int(cfg["frame_pre_random_seed"][3]) == 200
        assert int(cfg["players"]["char_id"][1, 0]) == msl.Character.FALCO
        assert int(cfg["players"]["char_id"][1, 1]) == msl.Character.FOX


def test_masked_reset_accepts_frame_mask() -> None:
    with msl.EnvBatch(batch_size=4, length=8) as env:
        buffers = env.buffers()
        env.configure_match(buffers)
        env.bind(buffers)
        env.reset_all()

        buffers.reset_mask[3, [1, 3]] = 1
        env.t = 3
        env.reset_masked()

        assert np.array_equal(buffers.reset_mask[3], np.array([0, 1, 0, 1], dtype=np.uint8))


def test_raw_action_view_is_available_for_benchmark_parity() -> None:
    buffers = msl.Buffers.empty(length=2, batch_size=3, action_format="raw")
    assert buffers.raw_action_view.shape == (2, 3)
    assert buffers.raw_action_view.dtype == msl.input_dtype()


def test_gamestate_dtype_exposes_items_randall_and_invulnerability() -> None:
    assert msl.gamestate_dtype().itemsize == msl_dtypes.sizes()["gamestate"]
    assert msl.gamestate_dtype()["items"].subdtype[0] == msl.item_dtype()

    with msl.EnvBatch(batch_size=2, length=2) as env:
        buffers = env.buffers()
        env.configure_match(buffers, stage=msl.Stage.YOSHIS_STORY)
        env.bind(buffers)
        env.reset_all()
        env.step()

        obs = buffers.gamestate_view[1]
        assert obs.dtype == msl.gamestate_dtype()
        assert obs["items"]["exists"].shape == (2, 15)
        assert np.array_equal(
            obs["slots"]["invulnerable"],
            (obs["slots"]["hurtbox_state"] != 0).astype(np.uint8),
        )
        assert np.all(obs["stage"]["randall"]["exists"] == 1)
        assert np.all(np.isfinite(obs["stage"]["randall"]["x"]))
        assert np.all(np.isfinite(obs["stage"]["randall"]["y"]))
