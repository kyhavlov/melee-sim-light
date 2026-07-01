from __future__ import annotations

import numpy as np
import pytest

from tools.slippi.make_dataset_from_slp import build_dataset_from_slp, build_validation_buffers_from_slp


_REPLAYS = (
    (
        "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
        ("item_shield_bounce_valid", "guard_setoff_hitlag_damage_min", "combat_hitlist_cd"),
    ),
    (
        "replays/validation/sheik/StiffLustrousZebra.slpz",
        ("capture_grab_timer_f32", "guard_reflect_timer_x14", "stale_attack_instance"),
    ),
    (
        "replays/validation/dream_land_recent/FlippantEnchantedHorse.slpz",
        ("stage_dream_whispy_wind_valid_u8", "stage_dream_whispy_wind_timer_u16"),
    ),
    (
        "replays/validation/marth/WellWornSmallGoshawk.slpz",
        ("stage_dream_whispy_wind_valid_u8", "guard_special_enable_timer_x1c"),
    ),
    (
        "replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.slpz",
        ("stage_fod_platform_height_valid_u8", "floor_skip_segment_valid_u8"),
    ),
    (
        "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slpz",
        ("item_hidden_callback_flags", "item_reflect_transfer_iid"),
    ),
    (
        "replays/validation/yoshis_story_recent/CheeryNumbMonkey.slpz",
        ("stage_yoshi_shyguy_valid_u8", "item_shyguy_hitlag_valid_u8"),
    ),
    (
        "replays/validation/yoshis_story_recent/PhysicalElectricCapybara.slpz",
        ("item_hitlist_victim_iid", "item_hitlist_victim_hitbox_mask"),
    ),
    (
        "replays/validation/doubles_recent/Game_20260509T152622.slpz",
        ("guard_x10", "combat_hitlist_hb_valid"),
    ),
)


def _row_bytes(a: np.ndarray) -> np.ndarray:
    arr = np.ascontiguousarray(a)
    return arr.view(np.uint8).reshape(arr.shape[0], arr.dtype.itemsize)


@pytest.mark.parametrize(("replay", "expected_live_fields"), _REPLAYS)
def test_validation_buffers_match_legacy_dataset_oracle(
    replay: str, expected_live_fields: tuple[str, ...]
) -> None:
    buffers = build_validation_buffers_from_slp(
        slp_path=replay,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=False,
    )
    legacy = build_dataset_from_slp(
        slp_path=replay,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=False,
    ).samples

    assert _row_bytes(buffers.seed_t).tobytes() == _row_bytes(legacy["seed_t"]).tobytes()
    assert _row_bytes(buffers.prev_input_t).tobytes() == _row_bytes(legacy["prev_input_t"]).tobytes()
    assert _row_bytes(buffers.input_t).tobytes() == _row_bytes(legacy["input_t"]).tobytes()
    assert _row_bytes(buffers.ref_t1).tobytes() == _row_bytes(legacy["ref_t1"]).tobytes()
    for field in expected_live_fields:
        assert np.count_nonzero(buffers.seed_t[field]) > 0, field
    if "doubles_recent" in replay:
        assert np.max(buffers.seed_t["num_players"]) == 4
