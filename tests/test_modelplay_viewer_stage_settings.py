from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tools.modelplay.state_adapter import (
    MSL_STAGE_BATTLEFIELD,
    MSL_STAGE_POKEMON_STADIUM,
    SimFrameState,
    viewer_settings_from_state,
)


def _state(stage_id: int) -> SimFrameState:
    n = 2
    item_dtype, item_shape = COMPARE_DTYPE["items"].subdtype
    return SimFrameState(
        frame_id=0,
        stage_id=stage_id,
        num_players=n,
        is_teams=False,
        team_id=np.zeros(n, dtype=np.uint8),
        char_id=np.array([1, 22], dtype=np.uint8),
        pos_x=np.zeros(n, dtype=np.float32),
        pos_y=np.zeros(n, dtype=np.float32),
        speed_air_x_self=np.zeros(n, dtype=np.float32),
        speed_ground_x_self=np.zeros(n, dtype=np.float32),
        speed_y_self=np.zeros(n, dtype=np.float32),
        speed_x_attack=np.zeros(n, dtype=np.float32),
        speed_y_attack=np.zeros(n, dtype=np.float32),
        facing=np.ones(n, dtype=np.uint8),
        on_ground=np.ones(n, dtype=np.uint8),
        is_dead=np.zeros(n, dtype=np.uint8),
        action_id=np.zeros(n, dtype=np.uint16),
        action_frame=np.zeros(n, dtype=np.float32),
        jumps_left=np.full(n, 2, dtype=np.uint8),
        stocks=np.full(n, 4, dtype=np.uint8),
        percent=np.zeros(n, dtype=np.float32),
        shield_hp=np.full(n, 60.0, dtype=np.float32),
        state_flags=np.zeros((n, 5), dtype=np.uint8),
        hitlag=np.zeros(n, dtype=np.float32),
        hitstun=np.zeros(n, dtype=np.uint16),
        hurtbox_state=np.zeros(n, dtype=np.uint8),
        items=np.zeros(item_shape, dtype=item_dtype),
        frame_pre_random_seed=0,
    )


def test_modelplay_marks_pokemon_stadium_as_frozen_for_viewer_settings() -> None:
    settings = viewer_settings_from_state(_state(MSL_STAGE_POKEMON_STADIUM))
    assert settings["stageId"] == MSL_STAGE_POKEMON_STADIUM
    assert settings["isFrozenStadium"] is True


def test_modelplay_does_not_mark_other_legal_stages_as_frozen_stadium() -> None:
    settings = viewer_settings_from_state(_state(MSL_STAGE_BATTLEFIELD))
    assert settings["stageId"] == MSL_STAGE_BATTLEFIELD
    assert settings["isFrozenStadium"] is False
