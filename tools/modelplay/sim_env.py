from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset
from tools.modelplay.state_adapter import (
    SimFrameState,
    build_slippi_ai_game,
    controllers_to_input_array,
    frame_state_from_compare,
    frame_state_from_seed,
    input_array_to_controllers,
)


@dataclass
class EnvOutput:
    gamestates: dict[int, object]
    needs_reset: bool


class SimSession:
    def __init__(
        self,
        *,
        dataset_path: Path,
        start_record: int = 0,
        char_ids: Sequence[int] | None = None,
    ):
        import msl_binding  # type: ignore

        self._binding = msl_binding
        self._dataset = read_dataset(str(dataset_path))
        self._samples = self._dataset.samples
        if start_record < 0 or start_record >= len(self._samples):
            raise ValueError(f"start_record out of range: {start_record}")
        self._record = int(start_record)
        self._num_players = int(self._dataset.header["num_players"])
        if char_ids is not None and len(char_ids) != self._num_players:
            raise ValueError(f"expected {self._num_players} char ids, got {len(char_ids)}")
        self._char_ids = None if char_ids is None else tuple(int(x) for x in char_ids)
        self._handle = msl_binding.init(1, self._num_players)
        self._compare = np.zeros(1, dtype=COMPARE_DTYPE)
        self._compare_bytes = self._compare.view(np.uint8).reshape(1, -1)
        self._prev_input = np.zeros(1, dtype=INPUT_DTYPE)
        self._input = np.zeros(1, dtype=INPUT_DTYPE)
        self._last_controllers = {}
        self._state: SimFrameState | None = None
        self._needs_reset = True

    def close(self) -> None:
        self._binding.destroy(self._handle)

    def reset(self) -> EnvOutput:
        row = self._samples[self._record]
        seed_arr = np.zeros(1, dtype=SEED_DTYPE)
        seed_arr[0] = row["seed_t"]
        if self._char_ids is not None:
            for p, char_id in enumerate(self._char_ids):
                seed_arr[0]["char_id"][p] = np.uint8(char_id)
        seed_bytes = seed_arr.view(np.uint8).reshape(1, -1)
        self._binding.reseed_seed(self._handle, seed_bytes)
        self._prev_input[0] = row["prev_input_t"]
        self._input[0] = row["input_t"]
        self._last_controllers = input_array_to_controllers(self._input)
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
            1: build_slippi_ai_game(state, self._last_controllers, viewpoint_port=1),
            2: build_slippi_ai_game(state, self._last_controllers, viewpoint_port=2),
        }
        out = EnvOutput(gamestates=games, needs_reset=self._needs_reset)
        self._needs_reset = False
        return out

    def step(self, controllers: Mapping[int, object]) -> EnvOutput:
        self._prev_input[...] = self._input
        self._input[...] = controllers_to_input_array(controllers, num_players=self._num_players)
        self._binding.step_input(
            self._handle,
            self._prev_input.view(np.uint8).reshape(1, -1),
            self._input.view(np.uint8).reshape(1, -1),
        )
        self._binding.write_compare(self._handle, self._compare_bytes)
        self._last_controllers = dict(controllers)
        self._state = frame_state_from_compare(self._compare[0])
        self._record += 1
        return self.current_state()
