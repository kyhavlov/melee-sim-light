from __future__ import annotations

import hashlib
import numpy as np
import pytest

from tools.slippi.validation_buffer_builder import build_validation_buffers_from_slp


_REPLAYS = (
    (
        "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
        ("item_hitlist_victim_port", "guard_setoff_hitlag_damage_min", "combat_hitlist_cd"),
        (7295, 2),
        {
            "seed_t": "253b64d1f4c6372801cff8cc1a6be0d9",
            "prev_input_t": "2b6be58ab8bb6987eb8c70e192e1e061",
            "input_t": "17d7d3a24676270a1df7a6d7a4af4b74",
            "ref_t1": "1d2cdd0e1dd7bdb540b67c918faaf77f",
        },
    ),
    (
        "replays/validation/sheik/StiffLustrousZebra.slpz",
        ("capture_grab_timer_f32", "guard_reflect_timer_x14", "stale_attack_instance"),
        (9679, 2),
        {
            "seed_t": "62ca5da8a2823241de4869e505b03846",
            "prev_input_t": "4c534a65894eed17f4d032806450576b",
            "input_t": "5ce03af38ee16f0157b7d9731f1df95b",
            "ref_t1": "d8dd448157d008e114bcb5f584b4c129",
        },
    ),
    (
        "replays/validation/dream_land_recent/FlippantEnchantedHorse.slpz",
        ("stage_dream_whispy_wind_valid_u8", "stage_dream_whispy_wind_timer_u16"),
        (12471, 2),
        {
            "seed_t": "0c2c98555dd7d4e72d85b38ed3606d25",
            "prev_input_t": "bb15583915bccdc23fe0a4643014264b",
            "input_t": "dcfe3ac41d3a21c8eb1b1f96709503c6",
            "ref_t1": "82e62c74248a30d99839446ab6f7cb9a",
        },
    ),
    (
        "replays/validation/marth/WellWornSmallGoshawk.slpz",
        ("stage_dream_whispy_wind_valid_u8", "guard_special_enable_timer_x1c"),
        (13272, 2),
        {
            "seed_t": "be732b5adf80703133a8c858069551d3",
            "prev_input_t": "c94a05b021295bc217b2779b0ac6da71",
            "input_t": "4ed5701d19a0bef1936e321cdd1b80a4",
            "ref_t1": "6f355c0c0455ea73fbdd9ea8524dadb9",
        },
    ),
    (
        "replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.slpz",
        ("stage_fod_platform_height_valid_u8", "floor_skip_segment_valid_u8"),
        (5921, 2),
        {
            "seed_t": "45748b139ad36ef1d892f07b9f455634",
            "prev_input_t": "664369b710a4411fa8377c0a58b37340",
            "input_t": "025ee4e26f59fbabae08972ec2b6f3e2",
            "ref_t1": "dc8978204b6216f86a65ffcecdc72816",
        },
    ),
    (
        "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slpz",
        ("item_reflect_damage_mul",),
        (11918, 2),
        {
            "seed_t": "42d93733c13e21af02c5e176fe6df9f1",
            "prev_input_t": "93de41527f0d482497cbde739960759d",
            "input_t": "959d3abc039a38e5fd3cc1e96f2be8a6",
            "ref_t1": "44699d3fa431d07288fdeeef53a82d69",
        },
    ),
    (
        "replays/validation/yoshis_story_recent/CheeryNumbMonkey.slpz",
        ("stage_yoshi_shyguy_valid_u8", "item_shyguy_hitlag_valid_u8"),
        (8459, 2),
        {
            "seed_t": "3329dff7db79f533cb80f9c1464c00ca",
            "prev_input_t": "dcf01da1e89a5602394152b388b867f2",
            "input_t": "7a68112375241015539bc07f88509cd1",
            "ref_t1": "ca0776567391b30dfdf3f0e492c97c28",
        },
    ),
    (
        "replays/validation/yoshis_story_recent/PhysicalElectricCapybara.slpz",
        ("item_hitlist_victim_iid", "item_hitlist_victim_hitbox_mask"),
        (9439, 2),
        {
            "seed_t": "1dbca299495f68f5813f26b7818a77e9",
            "prev_input_t": "64c0dff19de83308cecd727461f8d09c",
            "input_t": "cbda5bc05c9f40ddbf347b823063ba7f",
            "ref_t1": "f39d1318101d1a93aa0e30d8eb8212da",
        },
    ),
    (
        "replays/validation/doubles_recent/Game_20260509T152622.slpz",
        ("guard_x10", "combat_hitlist_hb_valid"),
        (9560, 4),
        {
            "seed_t": "39f679258ff32aaab29040b51982192b",
            "prev_input_t": "46e7df1f24b517cdce48f7e8eeab9213",
            "input_t": "d633bfc4ccf8f861f85f9fc330d39450",
            "ref_t1": "cd15eed0977cd64b023ba057e3e4ac9a",
        },
    ),
)


def _digest(arr: np.ndarray) -> str:
    return hashlib.blake2b(arr.view(np.uint8), digest_size=16).hexdigest()


@pytest.mark.parametrize(("replay", "expected_live_fields", "expected_shape", "expected_digests"), _REPLAYS)
def test_validation_buffers_build_representative_replays(
    replay: str,
    expected_live_fields: tuple[str, ...],
    expected_shape: tuple[int, int],
    expected_digests: dict[str, str],
) -> None:
    buffers = build_validation_buffers_from_slp(
        slp_path=replay,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=False,
    )
    assert (buffers.num_records, buffers.num_players) == expected_shape
    assert buffers.num_records == buffers.prev_input_t.shape[0]
    assert buffers.num_records == buffers.input_t.shape[0]
    assert buffers.num_records == buffers.ref_t1.shape[0]
    assert _digest(buffers.seed_t) == expected_digests["seed_t"]
    assert _digest(buffers.prev_input_t) == expected_digests["prev_input_t"]
    assert _digest(buffers.input_t) == expected_digests["input_t"]
    assert _digest(buffers.ref_t1) == expected_digests["ref_t1"]
    np.testing.assert_array_equal(buffers.prev_input_t[1:], buffers.input_t[:-1])
    # SendFrameStart's priority-0 GObj records the HSD stream before destination-frame fighter
    # callbacks. A post-frame-i validation seed predicts i+1, so its RNG lane equals ref_t1's.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s::Macro_SendFrameStart
    np.testing.assert_array_equal(
        buffers.seed_t["frame_pre_random_seed"], buffers.ref_t1["frame_pre_random_seed"]
    )
    for field in expected_live_fields:
        assert np.count_nonzero(buffers.seed_t[field]) > 0, field


def test_validation_buffer_builder_has_no_legacy_coordinator() -> None:
    import tools.slippi.validation_buffer_builder as validation_buffer_builder

    assert not hasattr(validation_buffer_builder, "_main_impl")
    assert not hasattr(validation_buffer_builder, "_build_replay_outputs")
    buffers = build_validation_buffers_from_slp(
        slp_path=_REPLAYS[0][0],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=False,
    )
    assert buffers.num_records > 0
