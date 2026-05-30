from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE
from tools.modelplay.sim_env import CHAR_FALCO, CHAR_FOX, MATCH_CONFIG_DTYPE, build_match_config_array
from tools.modelplay.state_adapter import MSL_STAGE_FINAL_DESTINATION


GAMESTATE_PLAYER_DTYPE = np.dtype(
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
        ("invulnerable", "u1"),
        ("_pad0", "V1"),
    ],
    align=False,
)

GAMESTATE_RANDALL_DTYPE = np.dtype(
    [
        ("exists", "u1"),
        ("_pad0", "V3"),
        ("x", "<f4"),
        ("y", "<f4"),
    ],
    align=False,
)

GAMESTATE_STAGE_DTYPE = np.dtype(
    [
        ("randall", GAMESTATE_RANDALL_DTYPE),
        ("fod_platforms", [("left", "<f4"), ("right", "<f4")]),
    ],
    align=False,
)
GAMESTATE_ITEM_DTYPE = COMPARE_DTYPE["items"].subdtype[0]

GAMESTATE_DTYPE = np.dtype(
    [
        ("frame_id", "<i4"),
        ("frame_pre_random_seed", "<u4"),
        ("stage_id", "<u4"),
        ("num_players", "u1"),
        ("viewpoint_player", "u1"),
        ("is_teams", "u1"),
        ("_pad0", "V1"),
        ("stage", GAMESTATE_STAGE_DTYPE),
        ("slots", GAMESTATE_PLAYER_DTYPE, (4,)),
        ("items", GAMESTATE_ITEM_DTYPE, (15,)),
    ],
    align=False,
)

TERMINAL_DTYPE = np.dtype(
    [
        ("frame_id", "<i4"),
        ("stage_id", "<u4"),
        ("done", "u1"),
        ("match_ended", "u1"),
        ("stockout", "u1"),
        ("max_frame_reached", "u1"),
        ("alive_count", "u1"),
        ("alive_team_count", "u1"),
        ("team_alive_mask", "u1"),
        ("_pad0", "V1"),
    ],
    align=False,
)


def _binding_or_skip():
    return pytest.importorskip("msl_binding")


def _init_handle_or_skip(binding, *, batch_size: int = 1, num_players: int = 2):
    try:
        return binding.init(batch_size=batch_size, num_players=num_players)
    except (MemoryError, RuntimeError) as exc:
        pytest.skip(f"missing local C init artifacts for RL API test: {exc}")


def _config_rows(*rows: np.void) -> np.ndarray:
    out = np.zeros(len(rows), dtype=MATCH_CONFIG_DTYPE)
    for i, row in enumerate(rows):
        out[i] = row
    return out


def _config_bytes(config: np.ndarray) -> np.ndarray:
    return config.view(np.uint8).reshape(config.shape[0], -1)


def _compare_bytes(rows: int) -> np.ndarray:
    return np.zeros((rows, COMPARE_DTYPE.itemsize), dtype=np.uint8)


def _idle_input(rows: int) -> np.ndarray:
    return np.zeros((rows, INPUT_DTYPE.itemsize), dtype=np.uint8)


def _basic_seed(
    frame_id: int = 123,
    *,
    num_players: int = 2,
    is_teams: bool = False,
    team_ids: tuple[int, ...] | None = None,
) -> np.ndarray:
    seed = np.zeros(1, dtype=SEED_DTYPE)
    seed[0]["frame_id"] = np.int32(frame_id)
    seed[0]["frame_pre_random_seed"] = np.uint32(0x12345678)
    seed[0]["stage_id"] = np.uint32(MSL_STAGE_FINAL_DESTINATION)
    seed[0]["match_damage_ratio"] = np.float32(1.0)
    seed[0]["num_players"] = np.uint8(num_players)
    seed[0]["is_teams"] = np.uint8(1 if is_teams else 0)
    chars = [CHAR_FOX, CHAR_FALCO, CHAR_FOX, CHAR_FALCO]
    facing = [1, 0, 1, 0]
    teams = team_ids or tuple(range(num_players))
    seed[0]["char_id"][:num_players] = chars[:num_players]
    seed[0]["team_id"][:num_players] = list(teams)
    seed[0]["facing"][:num_players] = facing[:num_players]
    seed[0]["facing_dir1"][:num_players] = [1 if x else -1 for x in facing[:num_players]]
    seed[0]["action_id"][:num_players] = [0x000E] * num_players
    seed[0]["action_frame"][:num_players] = [0] * num_players
    seed[0]["animation_index"][:num_players] = [14] * num_players
    seed[0]["jumps_left"][:num_players] = [2] * num_players
    seed[0]["stocks"][:num_players] = [4] * num_players
    seed[0]["attack_ratio"][:num_players] = np.float32(1.0)
    seed[0]["defense_ratio"][:num_players] = np.float32(1.0)
    seed[0]["fighter_scale_y"][:num_players] = np.float32(1.0)
    seed[0]["ground_friction_mul"][:num_players] = np.float32(1.0)
    return seed


def test_gamestate_dtype_sizes_match_c() -> None:
    binding = _binding_or_skip()
    sizes = binding.sizes()
    assert int(sizes["gamestate"]) == GAMESTATE_DTYPE.itemsize
    assert int(sizes["terminal"]) == TERMINAL_DTYPE.itemsize


def test_masked_match_init_resets_row1_without_changing_row0() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding, batch_size=2)
    try:
        row0 = build_match_config_array(
            char_ids=(CHAR_FOX, CHAR_FALCO),
            facing=(1, 0),
            stocks=4,
            frame_id=10,
        )[0]
        row1 = build_match_config_array(
            char_ids=(CHAR_FALCO, CHAR_FOX),
            facing=(0, 1),
            stocks=3,
            frame_id=20,
        )[0]
        config = _config_rows(row0, row1)
        out = _compare_bytes(2)

        binding.init_match(handle, _config_bytes(config))
        binding.write_compare(handle, out)
        before = out.view(COMPARE_DTYPE).reshape(2).copy()

        changed_row1 = row1.copy()
        changed_row1["stock_count"] = np.uint8(2)
        changed_row1["frame_id"] = np.int32(77)
        masked_config = _config_rows(row0, changed_row1)
        mask = np.array([0, 1], dtype=np.uint8)
        binding.init_match_masked(handle, _config_bytes(masked_config), mask)
        binding.write_compare(handle, out)
        after = out.view(COMPARE_DTYPE).reshape(2).copy()
    finally:
        binding.destroy(handle)

    assert bytes(before[0]) == bytes(after[0])
    assert int(after[1]["frame_id"]) == 77
    assert after[1]["stocks"][:2].tolist() == [2, 2]
    assert after[1]["char_id"][:2].tolist() == [CHAR_FALCO, CHAR_FOX]


def test_match_init_zeroed_new_game_config_uses_defaults() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding, batch_size=1)
    try:
        config = np.zeros(1, dtype=MATCH_CONFIG_DTYPE)
        config[0]["stage_id"] = np.uint32(MSL_STAGE_FINAL_DESTINATION)
        config[0]["frame_id"] = np.int32(-123)
        config[0]["players"]["char_id"][:2] = [CHAR_FOX, CHAR_FALCO]
        out = _compare_bytes(1)

        binding.init_match(handle, _config_bytes(config))
        binding.write_compare(handle, out)
        row = out.view(COMPARE_DTYPE).reshape(1)[0].copy()
    finally:
        binding.destroy(handle)

    assert int(row["num_players"]) == 2
    assert row["stocks"][:2].tolist() == [4, 4]
    assert row["char_id"][:2].tolist() == [CHAR_FOX, CHAR_FALCO]
    assert row["facing"][:2].tolist() == [1, 0]


def test_masked_match_init_does_not_refresh_unmasked_state_flags() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding, batch_size=2)
    try:
        seeds = np.zeros(2, dtype=SEED_DTYPE)
        seeds[0] = _basic_seed(frame_id=101)[0]
        seeds[1] = _basic_seed(frame_id=202)[0]
        # state_flags[1] bit 0x20 is the hitlag flag. With hitlag=0, a full-row refresh would clear
        # it, so this catches masked init accidentally refreshing unmasked rows.
        seeds[0]["state_flags"][0, 1] = np.uint8(0x20)
        out = _compare_bytes(2)
        binding.reseed_seed(handle, seeds.view(np.uint8).reshape(2, -1))
        binding.write_compare(handle, out)
        before = out.view(COMPARE_DTYPE).reshape(2).copy()

        config = _config_rows(
            build_match_config_array(frame_id=10)[0],
            build_match_config_array(frame_id=20)[0],
        )
        mask = np.array([0, 1], dtype=np.uint8)
        binding.init_match_masked(handle, _config_bytes(config), mask)
        binding.write_compare(handle, out)
        after = out.view(COMPARE_DTYPE).reshape(2).copy()
    finally:
        binding.destroy(handle)

    assert bytes(before[0]) == bytes(after[0])
    assert int(after[0]["state_flags"][0, 1]) == 0x20
    assert int(after[1]["frame_id"]) == 20


def test_sim_init_rollout_advances_frame_id_and_is_deterministic() -> None:
    binding = _binding_or_skip()
    config = build_match_config_array(
        char_ids=(CHAR_FOX, CHAR_FALCO),
        facing=(1, 0),
        frame_id=5,
        random_seed=0xA5A5,
    )
    prev_inp = _idle_input(1)
    inp = _idle_input(1)

    outputs: list[bytes] = []
    frame_ids: list[int] = []
    for _ in range(2):
        handle = _init_handle_or_skip(binding)
        try:
            out = _compare_bytes(1)
            binding.init_match(handle, _config_bytes(config))
            for _step in range(7):
                binding.step_input(handle, prev_inp, inp)
            binding.write_compare(handle, out)
            row = out.view(COMPARE_DTYPE).reshape(1)[0].copy()
            outputs.append(bytes(out.reshape(-1)))
            frame_ids.append(int(row["frame_id"]))
        finally:
            binding.destroy(handle)

    assert frame_ids == [12, 12]
    assert outputs[0] == outputs[1]


def test_replay_reseed_step_preserves_seed_owned_frame_metadata() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding)
    try:
        seed = _basic_seed(frame_id=123)
        out = _compare_bytes(1)
        binding.reseed_seed(handle, seed.view(np.uint8).reshape(1, -1))
        binding.step_input(handle, _idle_input(1), _idle_input(1))
        binding.write_compare(handle, out)
        row = out.view(COMPARE_DTYPE).reshape(1)[0].copy()
    finally:
        binding.destroy(handle)

    assert int(row["frame_id"]) == 123
    assert int(row["frame_pre_random_seed"]) == 0x12345678


def test_replay_rollout_advances_frame_id_without_advancing_seed_rng() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding)
    try:
        seed = _basic_seed(frame_id=123)
        out = _compare_bytes(1)
        binding.reseed_seed_rollout(handle, seed.view(np.uint8).reshape(1, -1))
        for _ in range(3):
            binding.step_input(handle, _idle_input(1), _idle_input(1))
        binding.write_compare(handle, out)
        row = out.view(COMPARE_DTYPE).reshape(1)[0].copy()
    finally:
        binding.destroy(handle)

    # Replay rollouts need a live frame clock for frame-indexed stage-object owners such as
    # Randall, but ordinary rows must not imply a replay-frame RNG stream owner.
    assert int(row["frame_id"]) == 126
    assert int(row["frame_pre_random_seed"]) == 0x12345678


def test_gamestate_viewpoint_swap_matches_compare_fields() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding, batch_size=2)
    try:
        row0 = build_match_config_array(char_ids=(CHAR_FOX, CHAR_FALCO), facing=(1, 0))[0]
        row1 = build_match_config_array(char_ids=(CHAR_FOX, CHAR_FALCO), facing=(1, 0))[0]
        config = _config_rows(row0, row1)
        cmp_bytes = _compare_bytes(2)
        obs_bytes = np.zeros((2, GAMESTATE_DTYPE.itemsize), dtype=np.uint8)
        viewpoints = np.array([0, 1], dtype=np.uint8)

        binding.init_match(handle, _config_bytes(config))
        binding.write_compare(handle, cmp_bytes)
        binding.write_gamestate(handle, viewpoints, obs_bytes)
        cmp_rows = cmp_bytes.view(COMPARE_DTYPE).reshape(2).copy()
        obs_rows = obs_bytes.view(GAMESTATE_DTYPE).reshape(2).copy()
    finally:
        binding.destroy(handle)

    assert int(obs_rows[0]["viewpoint_player"]) == 0
    assert int(obs_rows[1]["viewpoint_player"]) == 1

    for obs_idx, self_p, opp_p in ((0, 0, 1), (1, 1, 0)):
        obs = obs_rows[obs_idx]
        cmp_row = cmp_rows[obs_idx]
        assert int(obs["frame_id"]) == int(cmp_row["frame_id"])
        assert int(obs["stage_id"]) == int(cmp_row["stage_id"])
        assert obs["slots"]["present"].tolist() == [1, 1, 0, 0]
        assert obs["slots"]["source_player"].tolist() == [self_p, opp_p, 0, 0]
        assert obs["slots"]["team_relation"].tolist() == [0, 2, 0, 0]
        assert int(obs["slots"][0]["char_id"]) == int(cmp_row["char_id"][self_p])
        assert int(obs["slots"][1]["char_id"]) == int(cmp_row["char_id"][opp_p])
        assert float(obs["slots"][0]["pos_x"]) == float(cmp_row["pos_x"][self_p])
        assert float(obs["slots"][1]["pos_x"]) == float(cmp_row["pos_x"][opp_p])
        assert int(obs["slots"][0]["action_id"]) == int(cmp_row["action_id"][self_p])
        assert int(obs["slots"][1]["action_id"]) == int(cmp_row["action_id"][opp_p])
        assert int(obs["slots"][0]["stocks"]) == int(cmp_row["stocks"][self_p])
        assert int(obs["slots"][1]["stocks"]) == int(cmp_row["stocks"][opp_p])
        assert int(obs["slots"][0]["invulnerable"]) == int(cmp_row["hurtbox_state"][self_p] != 0)
        assert obs["items"].tobytes() == cmp_row["items"].tobytes()
        zero_slot = np.zeros((), dtype=GAMESTATE_PLAYER_DTYPE).tobytes()
        assert obs["slots"][2].tobytes() == zero_slot
        assert obs["slots"][3].tobytes() == zero_slot


def test_gamestate_four_player_teams_slot_order_is_deterministic() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding, num_players=4)
    try:
        seed = _basic_seed(num_players=4, is_teams=True, team_ids=(0, 0, 1, 1))
        seed[0]["pos_x"][:4] = np.array([10.0, 20.0, 30.0, 40.0], dtype=np.float32)
        obs_bytes = np.zeros((1, GAMESTATE_DTYPE.itemsize), dtype=np.uint8)
        viewpoints = np.array([1], dtype=np.uint8)
        binding.reseed_seed(handle, seed.view(np.uint8).reshape(1, -1))
        binding.write_gamestate(handle, viewpoints, obs_bytes)
        obs = obs_bytes.view(GAMESTATE_DTYPE).reshape(1)[0].copy()
    finally:
        binding.destroy(handle)

    assert int(obs["num_players"]) == 4
    assert int(obs["is_teams"]) == 1
    assert int(obs["viewpoint_player"]) == 1
    assert obs["slots"]["present"].tolist() == [1, 1, 1, 1]
    assert obs["slots"]["source_player"].tolist() == [1, 0, 2, 3]
    assert obs["slots"]["team_id"].tolist() == [0, 0, 1, 1]
    assert obs["slots"]["team_relation"].tolist() == [0, 1, 2, 2]
    assert obs["slots"]["pos_x"].tolist() == [20.0, 10.0, 30.0, 40.0]


def test_terminal_flags_support_optional_max_frame() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding)
    try:
        config = build_match_config_array(frame_id=9)
        out = np.zeros((1, TERMINAL_DTYPE.itemsize), dtype=np.uint8)
        binding.init_match(handle, _config_bytes(config))
        binding.write_terminal(handle, out)
        no_limit = out.view(TERMINAL_DTYPE).reshape(1)[0].copy()
        binding.write_terminal(handle, out, 9)
        at_limit = out.view(TERMINAL_DTYPE).reshape(1)[0].copy()
        stockout_seed = _basic_seed(frame_id=10)
        stockout_seed[0]["stocks"][:2] = [4, 0]
        binding.reseed_seed(handle, stockout_seed.view(np.uint8).reshape(1, -1))
        binding.write_terminal(handle, out)
        stockout = out.view(TERMINAL_DTYPE).reshape(1)[0].copy()
    finally:
        binding.destroy(handle)

    assert int(no_limit["done"]) == 0
    assert int(no_limit["alive_count"]) == 2
    assert int(at_limit["done"]) == 1
    assert int(at_limit["max_frame_reached"]) == 1
    assert int(at_limit["match_ended"]) == 0
    assert int(stockout["done"]) == 1
    assert int(stockout["match_ended"]) == 1
    assert int(stockout["stockout"]) == 1
    assert int(stockout["alive_count"]) == 1
    assert int(stockout["alive_team_count"]) == 1


def test_terminal_four_player_teams_waits_for_team_elimination() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding, num_players=4)
    try:
        out = np.zeros((1, TERMINAL_DTYPE.itemsize), dtype=np.uint8)
        seed = _basic_seed(num_players=4, is_teams=True, team_ids=(0, 0, 1, 1))
        seed[0]["stocks"][:4] = [0, 4, 4, 4]
        binding.reseed_seed(handle, seed.view(np.uint8).reshape(1, -1))
        binding.write_terminal(handle, out)
        teammate_alive = out.view(TERMINAL_DTYPE).reshape(1)[0].copy()

        seed[0]["stocks"][:4] = [0, 0, 4, 4]
        binding.reseed_seed(handle, seed.view(np.uint8).reshape(1, -1))
        binding.write_terminal(handle, out)
        team_eliminated = out.view(TERMINAL_DTYPE).reshape(1)[0].copy()

        binding.write_terminal(handle, out, 10)
        max_frame = out.view(TERMINAL_DTYPE).reshape(1)[0].copy()
    finally:
        binding.destroy(handle)

    assert int(teammate_alive["stockout"]) == 1
    assert int(teammate_alive["alive_count"]) == 3
    assert int(teammate_alive["alive_team_count"]) == 2
    assert int(teammate_alive["team_alive_mask"]) == 0b11
    assert int(teammate_alive["done"]) == 0
    assert int(teammate_alive["match_ended"]) == 0

    assert int(team_eliminated["stockout"]) == 1
    assert int(team_eliminated["alive_count"]) == 2
    assert int(team_eliminated["alive_team_count"]) == 1
    assert int(team_eliminated["team_alive_mask"]) == 0b10
    assert int(team_eliminated["done"]) == 1
    assert int(team_eliminated["match_ended"]) == 1
    assert int(max_frame["done"]) == 1
    assert int(max_frame["max_frame_reached"]) == 1


def test_new_gamestate_api_no_allocations_after_init() -> None:
    binding = _binding_or_skip()
    handle = _init_handle_or_skip(binding, batch_size=2)
    try:
        config = _config_rows(
            build_match_config_array(frame_id=0)[0],
            build_match_config_array(frame_id=100)[0],
        )
        mask = np.array([0, 1], dtype=np.uint8)
        prev_inp = _idle_input(2)
        inp = _idle_input(2)
        obs = np.zeros((2, GAMESTATE_DTYPE.itemsize), dtype=np.uint8)
        term = np.zeros((2, TERMINAL_DTYPE.itemsize), dtype=np.uint8)
        viewpoints = np.array([0, 1], dtype=np.uint8)

        binding.init_match(handle, _config_bytes(config))
        binding.alloc_reset()
        binding.init_match_masked(handle, _config_bytes(config), mask)
        binding.step_input(handle, prev_inp, inp)
        binding.write_gamestate(handle, viewpoints, obs)
        binding.write_terminal(handle, term, 120)
        stats = binding.alloc_stats()
    finally:
        binding.destroy(handle)

    assert int(stats["calls"]) == 0
    assert int(stats["bytes"]) == 0
