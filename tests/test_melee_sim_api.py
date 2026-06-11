from __future__ import annotations

import json
import os
from pathlib import Path

import numpy as np
import pytest

import melee_sim as msl
import melee_sim.dtypes as msl_dtypes
from melee_sim import env_batch as env_batch_module
from melee_sim.env_batch import _check_data_manifest, _resolve_data_dir


ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(autouse=True)
def _pin_repo_data_root(monkeypatch):
    # EnvBatch(data_dir=...) writes MSL_DATA_DIR into os.environ as a side effect,
    # so no-arg EnvBatch tests here were order-dependent on that leak. Pin the repo
    # data root per-test (monkeypatch restores it) to make each test standalone.
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))


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
    monkeypatch.delenv("MSL_DATA_DIR", raising=False)
    (tmp_path / ".msl").mkdir()

    assert _resolve_data_dir(None) == str(tmp_path / ".msl")
    assert os.environ["MSL_DATA_DIR"] == str(tmp_path / ".msl")
    os.environ.pop("MSL_DATA_DIR", None)


def test_missing_default_data_dir_error_is_actionable(monkeypatch, tmp_path) -> None:
    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("MSL_DATA_DIR", raising=False)

    with pytest.raises(FileNotFoundError) as excinfo:
        _resolve_data_dir(None)

    msg = str(excinfo.value)
    assert "melee_sim data directory not found" in msg
    assert str(tmp_path / ".msl") in msg
    assert "python -m melee_sim.extract_data --iso /path/to/SSBM.iso" in msg
    assert "MSL_DATA_DIR=/path/to/.msl python your_script.py" in msg


def test_data_manifest_schema_mismatch_error_is_actionable(tmp_path) -> None:
    runtime = env_batch_module._native.data_schema_versions()
    manifest = {
        "magic": "MSLDATA1",
        "version": 1,
        "schemas": dict(runtime),
    }
    manifest["schemas"]["motion_state_owners"] = int(runtime["motion_state_owners"]) + 1
    (tmp_path / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    with pytest.raises(RuntimeError) as excinfo:
        _check_data_manifest(tmp_path)

    msg = str(excinfo.value)
    assert "data/runtime schema mismatch" in msg
    assert "motion_state_owners" in msg
    assert "python -m pip install --force-reinstall --no-cache-dir /path/to/melee-sim-light" in msg
    assert "python -m melee_sim.extract_data --iso /path/to/SSBM.iso --force" in msg


def test_envbatch_uses_mslftsc1_without_legacy_script_owner_splits(tmp_path) -> None:
    data_dir = tmp_path / "data"
    _populate_data_overlay_without_legacy_script_owner_splits(data_dir)
    assert not (data_dir / "hit_status").exists()
    assert not (data_dir / "hurtbox_states").exists()
    assert (data_dir / "scripts" / "fox.bin").exists()
    assert (data_dir / "scripts" / "falco.bin").exists()

    with msl.EnvBatch(batch_size=1, length=2, data_dir=data_dir) as env:
        buffers = env.allocate_buffers()
        env.configure_match(buffers)
        controller = msl.neutral_controller((buffers.length, buffers.batch_size))
        msl.write_controller(buffers.controller_action_view, controller, player=0)
        msl.write_controller(buffers.controller_action_view, controller, player=1)
        env.bind(buffers)
        env.reset_all()
        env.step()


def test_buffers_controller_api_steps_and_writes_done() -> None:
    with msl.EnvBatch(batch_size=4, length=8) as env:
        buffers = env.allocate_buffers()
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


def test_envbatch_owned_buffers_cover_common_step_path() -> None:
    with msl.EnvBatch(batch_size=3, length=8) as env:
        env.configure_match(stage=msl.Stage.BATTLEFIELD)
        controller = msl.neutral_controller((env.length, env.batch_size))
        msl.write_controller(env.controller_action_view, controller, player=0)
        msl.write_controller(env.controller_action_view, controller, player=1)

        env.reset_all()
        assert np.shares_memory(env.current_frame, env.gamestate_view[0])
        env.current_reset_mask[:] = 0
        env.step()

        assert np.shares_memory(env.current_frame, env.gamestate_view[1])
        assert np.shares_memory(env.current_action_frame, env.controller_action_view[1])
        assert np.all(env.done_at(0) == 0)
        assert env.terminal_at(0).dtype == msl.terminal_dtype()
        assert np.all(env.current_frame["stage_id"] == msl.Stage.BATTLEFIELD)


def test_configure_matches_defaults_to_owned_buffers() -> None:
    with msl.EnvBatch(batch_size=2, length=4) as env:
        env.configure_matches(configs=[
            msl.MatchConfig(stage=msl.Stage.FINAL_DESTINATION),
            msl.MatchConfig(stage=msl.Stage.YOSHIS_STORY),
        ])

        assert int(env.match_config_view["stage_id"][0]) == msl.Stage.FINAL_DESTINATION
        assert int(env.match_config_view["stage_id"][1]) == msl.Stage.YOSHIS_STORY


def test_step_cursor_raises_at_length_and_can_rewind() -> None:
    with msl.EnvBatch(batch_size=1, length=1) as env:
        buffers = env.allocate_buffers()
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
        buffers = env.allocate_buffers()
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
        buffers = env.allocate_buffers()
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
        buffers = env.allocate_buffers()
        env.configure_match(buffers)
        env.configure_matches(
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
            buffers=buffers,
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
        buffers = env.allocate_buffers()
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
        buffers = env.allocate_buffers()
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
