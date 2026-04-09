from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Mapping

from tools.modelplay.state_adapter import SimFrameState, viewer_frame_from_state, viewer_settings_from_state


@dataclass
class ViewerTrace:
    settings: dict | None = None
    frames: list[dict] = field(default_factory=list)
    _frame_index: int = 0

    def add_frame(self, state: SimFrameState, controllers: Mapping[int, object]) -> None:
        if self.settings is None:
            self.settings = viewer_settings_from_state(state)
        frame = viewer_frame_from_state(state, controllers)
        frame["frameNumber"] = self._frame_index
        frame["randomSeed"] = int(state.frame_pre_random_seed)
        for player in frame["players"]:
            player["frameNumber"] = self._frame_index
            player["inputs"]["frameNumber"] = self._frame_index
            player["state"]["frameNumber"] = self._frame_index
        for item in frame["items"]:
            item["frameNumber"] = self._frame_index
        frame["stage"]["frameNumber"] = self._frame_index
        self.frames.append(frame)
        self._frame_index += 1

    def to_payload(self) -> dict:
        if self.settings is None:
            raise RuntimeError("trace has no frames")
        return {
            "settings": self.settings,
            "frames": self.frames,
            "ending": {
                "gameEndMethod": "GAME!",
                "quitInitiator": -1,
            },
        }

    def write_json(self, path: Path, *, frame_limit: int | None = None) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        payload = self.to_payload()
        if frame_limit is not None:
            payload["frames"] = payload["frames"][:frame_limit]
        path.write_text(json.dumps(payload))
