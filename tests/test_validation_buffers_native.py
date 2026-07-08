from __future__ import annotations

import hashlib
import importlib

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tools.slippi.validation_buffer_builder import build_validation_buffers_from_slp


_REPLAYS = (
    (
        "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
        ("item_shield_bounce_valid", "guard_setoff_hitlag_damage_min", "combat_hitlist_cd"),
        (7295, 2),
        {
            "seed_t": "095dc106e4b8eeb58477b73b60da8f3d",
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
            "seed_t": "215174dd3877012bdd051bd2a41c28b8",
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
            "seed_t": "b796d4bfff26caed32f36dfeec388619",
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
            "seed_t": "443aab9a183d8de8ed9003bad59a64f5",
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
            "seed_t": "5cf949c4f7a0deea3e40f5566eed2801",
            "prev_input_t": "664369b710a4411fa8377c0a58b37340",
            "input_t": "025ee4e26f59fbabae08972ec2b6f3e2",
            "ref_t1": "dc8978204b6216f86a65ffcecdc72816",
        },
    ),
    (
        "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slpz",
        ("item_hidden_callback_flags", "item_reflect_transfer_iid"),
        (11918, 2),
        {
            "seed_t": "61635576e2fcea92a1ef477f8b694f1a",
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
            "seed_t": "4e725ea7c14c1e0bef3e8ca87621ee65",
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
            "seed_t": "8d2b9b91f6cf1eb4ef30f726c840dbff",
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
            "seed_t": "a44ab371dc99ff37f7d2c53c910f871d",
            "prev_input_t": "46e7df1f24b517cdce48f7e8eeab9213",
            "input_t": "d633bfc4ccf8f861f85f9fc330d39450",
            "ref_t1": "cd15eed0977cd64b023ba057e3e4ac9a",
        },
    ),
)


def _digest(arr: np.ndarray) -> str:
    return hashlib.blake2b(arr.view(np.uint8), digest_size=16).hexdigest()


def _step_one_row(buffers, record: int) -> np.ndarray:
    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    compare_stride = int(sizes["compare"])
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(buffers.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=0,
    )
    try:
        binding.reseed_seed(handle, buffers.seed_u8()[record : record + 1])
        binding.step_input(
            handle,
            buffers.prev_input_u8()[record : record + 1],
            buffers.input_u8()[record : record + 1],
        )
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape(1)


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


@pytest.mark.parametrize(
    ("replay", "record", "player", "field", "subindex", "expected"),
    (
        ("replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz", 11164, 1, "action_id", None, 91),
        ("replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz", 4529, 0, "action_id", None, 20),
        ("replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz", 8293, 0, "action_id", None, 25),
        ("replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz", 9530, 1, "action_id", None, 29),
        ("replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz", 330, 1, "action_id", None, 79),
        ("replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz", 7173, 0, "hitlag", None, 0),
        ("replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz", 2221, 0, "hitlag", None, 0),
        ("replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz", 7173, 1, "hitstun", None, 0),
        ("replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz", 11033, 0, "state_flags", 1, 0),
    ),
)
def test_validation_buffers_port_hard_row_seedref_locks(
    replay: str, record: int, player: int, field: str, subindex: int | None, expected: int
) -> None:
    buffers = build_validation_buffers_from_slp(
        slp_path=replay,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=False,
    )
    seed_value = buffers.seed_t[field][record, player]
    ref_value = buffers.ref_t1[field][record, player]
    if subindex is not None:
        seed_value = seed_value[subindex]
        ref_value = ref_value[subindex]
    assert int(seed_value) == expected
    assert int(ref_value) == expected

    out = _step_one_row(buffers, int(record))
    out_value = out[field][0, player]
    if subindex is not None:
        out_value = out_value[subindex]
    assert int(out_value) == expected
