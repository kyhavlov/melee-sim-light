from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset
from tools.modelplay.state_adapter import (
    MSL_STAGE_FINAL_DESTINATION,
    STAGE_DEBUG_DTYPE,
    SimFrameState,
    build_slippi_ai_game,
    controller_to_input_player,
    controllers_to_input_array,
    empty_controller,
    frame_state_from_compare,
    frame_state_from_seed,
    frame_state_with_timebase,
    input_array_to_controllers,
)

CHAR_FOX = 1
CHAR_FALCO = 22
SIM_INIT_OPENING_FRAME_ID = -122

MATCH_PLAYER_CONFIG_DTYPE = np.dtype(
    [
        ("char_id", "u1"),
        ("team_id", "u1"),
        ("facing", "u1"),
        ("_pad0", "V1"),
    ],
    align=False,
)

MATCH_CONFIG_DTYPE = np.dtype(
    [
        ("stage_id", "<u4"),
        ("frame_id", "<i4"),
        ("frame_pre_random_seed", "<u4"),
        ("match_damage_ratio", "<f4"),
        ("num_players", "u1"),
        ("is_teams", "u1"),
        ("stock_count", "u1"),
        ("camera_mode", "u1"),
        ("players", MATCH_PLAYER_CONFIG_DTYPE, (4,)),
    ],
    align=False,
)

RL_PLAYER_OBS_DTYPE = np.dtype(
    [
        ("present", "u1"),
        ("source_player", "u1"),
        ("team_relation", "u1"),
        ("team_id", "u1"),
        ("pos_x", "<f4"),
        ("pos_y", "<f4"),
        ("speed_air_x_self", "<f4"),
        ("speed_ground_x_self", "<f4"),
        ("speed_y_self", "<f4"),
        ("speed_x_attack", "<f4"),
        ("speed_y_attack", "<f4"),
        ("percent", "<f4"),
        ("shield_hp", "<f4"),
        ("action_id", "<u2"),
        ("action_frame", "<i2"),
        ("hitlag", "<u2"),
        ("hitstun", "<u2"),
        ("char_id", "u1"),
        ("stocks", "u1"),
        ("facing", "u1"),
        ("on_ground", "u1"),
        ("jumps_left", "u1"),
        ("hurtbox_state", "u1"),
        ("_pad0", "V2"),
    ],
    align=False,
)

RL_OBS_DTYPE = np.dtype(
    [
        ("frame_id", "<i4"),
        ("frame_pre_random_seed", "<u4"),
        ("stage_id", "<u4"),
        ("num_players", "u1"),
        ("viewpoint_player", "u1"),
        ("is_teams", "u1"),
        ("_pad0", "V1"),
        ("slots", RL_PLAYER_OBS_DTYPE, (4,)),
    ],
    align=False,
)


@dataclass
class EnvOutput:
    gamestates: dict[int, object]
    needs_reset: bool


@dataclass
class BatchedEnvOutput:
    gamestates: list[dict[int, object]]
    needs_reset: np.ndarray


def build_match_config_array(
    *,
    num_players: int = 2,
    char_ids: Sequence[int] = (CHAR_FALCO, CHAR_FOX),
    team_ids: Sequence[int] | None = None,
    facing: Sequence[int] = (1, 0),
    stocks: int = 4,
    stage_id: int = MSL_STAGE_FINAL_DESTINATION,
    frame_id: int = 0,
    random_seed: int = 0,
    match_damage_ratio: float = 1.0,
    is_teams: bool = False,
) -> np.ndarray:
    if num_players not in (2, 4):
        raise ValueError(f"num_players must be 2 or 4, got {num_players}")
    if len(char_ids) != num_players:
        raise ValueError(f"expected {num_players} char ids, got {len(char_ids)}")
    if team_ids is not None and len(team_ids) != num_players:
        raise ValueError(f"expected {num_players} team ids, got {len(team_ids)}")
    if len(facing) != num_players:
        raise ValueError(f"expected {num_players} facing values, got {len(facing)}")
    if stocks <= 0:
        raise ValueError(f"stocks must be positive, got {stocks}")

    arr = np.zeros(1, dtype=MATCH_CONFIG_DTYPE)
    arr[0]["stage_id"] = np.uint32(stage_id)
    arr[0]["frame_id"] = np.int32(frame_id)
    arr[0]["frame_pre_random_seed"] = np.uint32(random_seed)
    arr[0]["match_damage_ratio"] = np.float32(match_damage_ratio)
    arr[0]["num_players"] = np.uint8(num_players)
    arr[0]["is_teams"] = np.uint8(1 if is_teams else 0)
    arr[0]["stock_count"] = np.uint8(stocks)
    for p in range(num_players):
        arr[0]["players"][p]["char_id"] = np.uint8(int(char_ids[p]))
        arr[0]["players"][p]["team_id"] = np.uint8(int(team_ids[p]) if team_ids is not None else p)
        arr[0]["players"][p]["facing"] = np.uint8(1 if int(facing[p]) else 0)
    return arr


class SimSession:
    def __init__(
        self,
        *,
        dataset_path: Path | None,
        start_record: int = 0,
        char_ids: Sequence[int] | None = None,
        team_ids: Sequence[int] | None = None,
        is_teams: bool = False,
        start_mode: str = "replay",
        stocks: int = 4,
        stage_id: int = MSL_STAGE_FINAL_DESTINATION,
        facing: Sequence[int] | None = None,
        frame_id: int = 0,
        random_seed: int = 0,
    ):
        import msl_binding  # type: ignore

        self._binding = msl_binding
        if start_mode not in ("replay", "sim-init"):
            raise ValueError(f"unknown start_mode: {start_mode}")
        self._start_mode = start_mode
        self._record = int(start_record)
        self._dataset = None
        self._samples = None
        if start_mode == "replay":
            if dataset_path is None:
                raise ValueError("dataset_path is required for replay start_mode")
            self._dataset = read_dataset(str(dataset_path))
            self._samples = self._dataset.samples
            if start_record < 0 or start_record >= len(self._samples):
                raise ValueError(f"start_record out of range: {start_record}")
            self._num_players = int(self._dataset.header["num_players"])
            if char_ids is not None and len(char_ids) != self._num_players:
                raise ValueError(f"expected {self._num_players} char ids, got {len(char_ids)}")
            if team_ids is not None and len(team_ids) != self._num_players:
                raise ValueError(f"expected {self._num_players} team ids, got {len(team_ids)}")
            self._char_ids = None if char_ids is None else tuple(int(x) for x in char_ids)
            self._team_ids = None if team_ids is None else tuple(int(x) for x in team_ids)
            self._is_teams = bool(is_teams)
            self._match_config = None
        else:
            self._num_players = 2 if char_ids is None else len(char_ids)
            if self._num_players not in (2, 4):
                raise ValueError(f"num_players must be 2 or 4, got {self._num_players}")
            init_char_ids = tuple(int(x) for x in (char_ids or (CHAR_FALCO, CHAR_FOX)))
            init_team_ids = tuple(int(x) for x in (team_ids or tuple(range(self._num_players))))
            init_facing = tuple(int(x) for x in (facing or (1, 0, 1, 0)[: self._num_players]))
            self._char_ids = init_char_ids
            self._team_ids = init_team_ids
            self._is_teams = bool(is_teams)
            self._match_config = build_match_config_array(
                num_players=self._num_players,
                char_ids=init_char_ids,
                team_ids=init_team_ids,
                facing=init_facing,
                stocks=stocks,
                stage_id=stage_id,
                frame_id=frame_id,
                random_seed=random_seed,
                is_teams=is_teams,
            )
        self._handle = msl_binding.init(1, self._num_players)
        self._compare = np.zeros(1, dtype=COMPARE_DTYPE)
        self._compare_bytes = self._compare.view(np.uint8).reshape(1, -1)
        self._stage_debug = np.zeros(1, dtype=STAGE_DEBUG_DTYPE)
        self._stage_debug_bytes = self._stage_debug.view(np.uint8).reshape(1, -1)
        self._prev_input = np.zeros(1, dtype=INPUT_DTYPE)
        self._input = np.zeros(1, dtype=INPUT_DTYPE)
        self._processed_input = np.zeros(1, dtype=INPUT_DTYPE)
        self._last_controllers = {}
        self._state: SimFrameState | None = None
        self._needs_reset = True

    def close(self) -> None:
        self._binding.destroy(self._handle)

    def reset(self) -> EnvOutput:
        if self._start_mode == "sim-init":
            if self._match_config is None:
                raise RuntimeError("missing sim-init match config")
            config_bytes = self._match_config.view(np.uint8).reshape(1, -1)
            self._binding.init_match(self._handle, config_bytes)
            self._binding.write_compare(self._handle, self._compare_bytes)
            self._binding.debug_write_stage_state(self._handle, self._stage_debug_bytes)
            self._prev_input[...] = np.zeros(1, dtype=INPUT_DTYPE)
            self._input[...] = np.zeros(1, dtype=INPUT_DTYPE)
            self._refresh_processed_controllers()
            self._state = frame_state_with_timebase(
                frame_state_from_compare(self._compare[0], self._stage_debug[0]),
                self._binding.debug_timebase(self._handle, 0),
            )
            self._needs_reset = True
            return self.current_state()

        if self._samples is None:
            raise RuntimeError("missing replay dataset samples")
        row = self._samples[self._record]
        seed_arr = np.zeros(1, dtype=SEED_DTYPE)
        seed_arr[0] = row["seed_t"]
        if self._char_ids is not None:
            for p, char_id in enumerate(self._char_ids):
                seed_arr[0]["char_id"][p] = np.uint8(char_id)
        if self._team_ids is not None:
            seed_arr[0]["is_teams"] = np.uint8(1 if self._is_teams else 0)
            for p, team_id in enumerate(self._team_ids):
                seed_arr[0]["team_id"][p] = np.uint8(team_id)
        seed_bytes = seed_arr.view(np.uint8).reshape(1, -1)
        self._binding.reseed_seed(self._handle, seed_bytes)
        self._prev_input[0] = row["prev_input_t"]
        self._input[0] = row["input_t"]
        self._refresh_processed_controllers()
        self._state = frame_state_from_seed(seed_arr[0])
        self._needs_reset = True
        return self.current_state()

    @property
    def current_frame_state(self) -> SimFrameState:
        if self._state is None:
            raise RuntimeError("session not reset")
        return self._state

    @property
    def current_record(self) -> int:
        return self._record

    @property
    def last_controllers(self) -> dict[int, object]:
        return dict(self._last_controllers)

    def current_state(self) -> EnvOutput:
        state = self.current_frame_state
        games = {
            port: build_slippi_ai_game(state, self._last_controllers, viewpoint_port=port)
            for port in range(1, state.num_players + 1)
        }
        out = EnvOutput(gamestates=games, needs_reset=self._needs_reset)
        self._needs_reset = False
        return out

    def current_rl_observation(self, viewpoint_port: int) -> np.void:
        viewpoint = np.array([viewpoint_port - 1], dtype=np.uint8)
        out = np.zeros((1, RL_OBS_DTYPE.itemsize), dtype=np.uint8)
        self._binding.write_rl_observation(self._handle, viewpoint, out)
        return out.view(RL_OBS_DTYPE).reshape(1)[0]

    def step(self, controllers: Mapping[int, object]) -> EnvOutput:
        self._prev_input[...] = self._input
        self._input[...] = controllers_to_input_array(controllers, num_players=self._num_players)
        self._binding.step_input(
            self._handle,
            self._prev_input.view(np.uint8).reshape(1, -1),
            self._input.view(np.uint8).reshape(1, -1),
        )
        self._binding.write_compare(self._handle, self._compare_bytes)
        self._binding.debug_write_stage_state(self._handle, self._stage_debug_bytes)
        self._refresh_processed_controllers()
        self._state = frame_state_with_timebase(
            frame_state_from_compare(self._compare[0], self._stage_debug[0]),
            self._binding.debug_timebase(self._handle, 0),
        )
        self._record += 1
        return self.current_state()

    def _refresh_processed_controllers(self) -> None:
        self._binding.debug_write_processed_input(
            self._handle, self._processed_input.view(np.uint8).reshape(1, -1)
        )
        if self._num_players == 2:
            self._last_controllers = input_array_to_controllers(self._processed_input)
        else:
            self._last_controllers = input_array_to_controllers(
                self._processed_input, num_players=self._num_players
            )


class BatchedSimSession:
    def __init__(
        self,
        *,
        batch_size: int,
        dataset_path: Path | None,
        start_record: int = 0,
        char_ids: Sequence[int] | None = None,
        team_ids: Sequence[int] | None = None,
        is_teams: bool = False,
        start_mode: str = "replay",
        stocks: int = 4,
        stage_id: int = MSL_STAGE_FINAL_DESTINATION,
        stage_ids: Sequence[int] | None = None,
        char_ids_by_env: Sequence[Sequence[int]] | None = None,
        facing: Sequence[int] | None = None,
        frame_id: int = 0,
        random_seed: int = 0,
    ):
        import msl_binding  # type: ignore

        if batch_size <= 0:
            raise ValueError(f"batch_size must be positive, got {batch_size}")
        if start_mode not in ("replay", "sim-init"):
            raise ValueError(f"unknown start_mode: {start_mode}")

        self._binding = msl_binding
        self._batch_size = int(batch_size)
        self._start_mode = start_mode
        self._record = int(start_record)
        self._dataset = None
        self._samples = None

        if start_mode == "replay":
            if dataset_path is None:
                raise ValueError("dataset_path is required for replay start_mode")
            self._dataset = read_dataset(str(dataset_path))
            self._samples = self._dataset.samples
            if start_record < 0 or start_record >= len(self._samples):
                raise ValueError(f"start_record out of range: {start_record}")
            self._num_players = int(self._dataset.header["num_players"])
            if char_ids is not None and len(char_ids) != self._num_players:
                raise ValueError(f"expected {self._num_players} char ids, got {len(char_ids)}")
            if team_ids is not None and len(team_ids) != self._num_players:
                raise ValueError(f"expected {self._num_players} team ids, got {len(team_ids)}")
            self._char_ids = None if char_ids is None else tuple(int(x) for x in char_ids)
            self._team_ids = None if team_ids is None else tuple(int(x) for x in team_ids)
            self._is_teams = bool(is_teams)
            self._match_config = None
        else:
            if char_ids_by_env is not None and len(char_ids_by_env) != self._batch_size:
                raise ValueError(f"expected {self._batch_size} char-id rows, got {len(char_ids_by_env)}")
            self._num_players = (
                len(char_ids_by_env[0])
                if char_ids_by_env is not None
                else 2 if char_ids is None else len(char_ids)
            )
            if self._num_players not in (2, 4):
                raise ValueError(f"num_players must be 2 or 4, got {self._num_players}")
            if char_ids_by_env is not None:
                for env, row in enumerate(char_ids_by_env):
                    if len(row) != self._num_players:
                        raise ValueError(
                            f"expected {self._num_players} char ids for env {env}, got {len(row)}"
                        )
            if stage_ids is not None and len(stage_ids) != self._batch_size:
                raise ValueError(f"expected {self._batch_size} stage ids, got {len(stage_ids)}")
            init_char_ids = tuple(int(x) for x in (char_ids or (CHAR_FALCO, CHAR_FOX)))
            init_team_ids = tuple(int(x) for x in (team_ids or tuple(range(self._num_players))))
            init_facing = tuple(int(x) for x in (facing or (1, 0, 1, 0)[: self._num_players]))
            self._char_ids = init_char_ids
            self._team_ids = init_team_ids
            self._is_teams = bool(is_teams)
            one = build_match_config_array(
                num_players=self._num_players,
                char_ids=init_char_ids,
                team_ids=init_team_ids,
                facing=init_facing,
                stocks=stocks,
                stage_id=stage_id,
                frame_id=frame_id,
                random_seed=random_seed,
                is_teams=is_teams,
            )[0]
            self._match_config = np.zeros(self._batch_size, dtype=MATCH_CONFIG_DTYPE)
            self._match_config[:] = one
            if stage_ids is not None:
                self._match_config["stage_id"] = np.asarray(stage_ids, dtype=np.uint32)
            if char_ids_by_env is not None:
                for env, row in enumerate(char_ids_by_env):
                    for port, char_id in enumerate(row):
                        self._match_config["players"][env, port]["char_id"] = np.uint8(int(char_id))
            self._match_config["frame_pre_random_seed"] = (
                np.uint32(random_seed) + np.arange(self._batch_size, dtype=np.uint32)
            )

        self._handle = msl_binding.init(self._batch_size, self._num_players)
        self._compare = np.zeros(self._batch_size, dtype=COMPARE_DTYPE)
        self._compare_bytes = self._compare.view(np.uint8).reshape(self._batch_size, -1)
        self._stage_debug = np.zeros(self._batch_size, dtype=STAGE_DEBUG_DTYPE)
        self._stage_debug_bytes = self._stage_debug.view(np.uint8).reshape(self._batch_size, -1)
        self._prev_input = np.zeros(self._batch_size, dtype=INPUT_DTYPE)
        self._input = np.zeros(self._batch_size, dtype=INPUT_DTYPE)
        self._processed_input = np.zeros(self._batch_size, dtype=INPUT_DTYPE)
        self._last_controllers: list[dict[int, object]] = [{} for _ in range(self._batch_size)]
        self._states: list[SimFrameState] | None = None
        self._needs_reset = np.ones(self._batch_size, dtype=np.bool_)

    @property
    def batch_size(self) -> int:
        return self._batch_size

    @property
    def num_players(self) -> int:
        return self._num_players

    @property
    def current_frame_states(self) -> list[SimFrameState]:
        if self._states is None:
            raise RuntimeError("session not reset")
        return self._states

    @property
    def last_controllers(self) -> list[dict[int, object]]:
        return [dict(row) for row in self._last_controllers]

    def close(self) -> None:
        self._binding.destroy(self._handle)

    def reset(self) -> BatchedEnvOutput:
        if self._start_mode == "sim-init":
            if self._match_config is None:
                raise RuntimeError("missing sim-init match config")
            config_bytes = self._match_config.view(np.uint8).reshape(self._batch_size, -1)
            self._binding.init_match(self._handle, config_bytes)
            self._binding.write_compare(self._handle, self._compare_bytes)
            self._binding.debug_write_stage_state(self._handle, self._stage_debug_bytes)
            self._prev_input[...] = np.zeros(self._batch_size, dtype=INPUT_DTYPE)
            self._input[...] = np.zeros(self._batch_size, dtype=INPUT_DTYPE)
            self._refresh_processed_controllers()
            self._refresh_states_from_compare()
            self._needs_reset[:] = True
            return self.current_state()

        if self._samples is None:
            raise RuntimeError("missing replay dataset samples")
        row = self._samples[self._record]
        seed_arr = np.zeros(self._batch_size, dtype=SEED_DTYPE)
        seed_arr[:] = row["seed_t"]
        if self._char_ids is not None:
            for p, char_id in enumerate(self._char_ids):
                seed_arr["char_id"][:, p] = np.uint8(char_id)
        if self._team_ids is not None:
            seed_arr["is_teams"][:] = np.uint8(1 if self._is_teams else 0)
            for p, team_id in enumerate(self._team_ids):
                seed_arr["team_id"][:, p] = np.uint8(team_id)
        self._binding.reseed_seed(self._handle, seed_arr.view(np.uint8).reshape(self._batch_size, -1))
        self._prev_input[:] = row["prev_input_t"]
        self._input[:] = row["input_t"]
        self._refresh_processed_controllers()
        self._states = [frame_state_from_seed(seed_arr[i]) for i in range(self._batch_size)]
        self._needs_reset[:] = True
        return self.current_state()

    def current_state(self) -> BatchedEnvOutput:
        states = self.current_frame_states
        games = [
            {
                port: build_slippi_ai_game(state, self._last_controllers[env], viewpoint_port=port)
                for port in range(1, state.num_players + 1)
            }
            for env, state in enumerate(states)
        ]
        out = BatchedEnvOutput(gamestates=games, needs_reset=self._needs_reset.copy())
        self._needs_reset[:] = False
        return out

    def step(self, controllers: Sequence[Mapping[int, object]]) -> BatchedEnvOutput:
        if len(controllers) != self._batch_size:
            raise ValueError(f"expected {self._batch_size} controller rows, got {len(controllers)}")
        self._prev_input[...] = self._input
        self._input[...] = np.zeros(self._batch_size, dtype=INPUT_DTYPE)
        for env, row in enumerate(controllers):
            for port in range(self._num_players):
                self._input["p"][env, port] = controller_to_input_player(row.get(port + 1, empty_controller()))
        self._binding.step_input(
            self._handle,
            self._prev_input.view(np.uint8).reshape(self._batch_size, -1),
            self._input.view(np.uint8).reshape(self._batch_size, -1),
        )
        self._binding.write_compare(self._handle, self._compare_bytes)
        self._binding.debug_write_stage_state(self._handle, self._stage_debug_bytes)
        self._refresh_processed_controllers()
        self._refresh_states_from_compare()
        self._record += 1
        return self.current_state()

    def _refresh_processed_controllers(self) -> None:
        self._binding.debug_write_processed_input(
            self._handle, self._processed_input.view(np.uint8).reshape(self._batch_size, -1)
        )
        self._last_controllers = [
            input_array_to_controllers(self._processed_input[env : env + 1], num_players=self._num_players)
            for env in range(self._batch_size)
        ]

    def _refresh_states_from_compare(self) -> None:
        self._states = [
            frame_state_with_timebase(
                frame_state_from_compare(self._compare[env], self._stage_debug[env]),
                self._binding.debug_timebase(self._handle, env),
            )
            for env in range(self._batch_size)
        ]
