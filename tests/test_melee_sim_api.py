from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

import melee_sim as msl
from melee_sim import dtypes


ROOT = Path(__file__).resolve().parents[1]


def test_envbatch_constructs_three_and_four_player_teams(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    for count in (3, 4):
        with msl.EnvBatch(batch_size=2, length=4, num_players=count) as env:
            for character in msl.Character:
                env.configure_matches([msl.MatchConfig(
                    players=tuple(msl.PlayerConfig(character, team_id=t, start_percent=p)
                                  for t, p in zip((0, 1, 1, 0)[:count], (0, 37, 100, 62))),
                    stocks=1, is_teams=True)] * 2)
                env.reset_all()
                assert np.all(env.current_frame["num_players"] == count)
                assert np.all(env.current_frame["slots"]["present"].sum(axis=1) == count)
                for row in env.current_frame:
                    slots = row['slots'][row['slots']['present'] != 0]
                    slots = slots[np.argsort(slots['source_player'])]
                    np.testing.assert_array_equal(slots['percent'], (0, 37, 100, 62)[:count])
                    np.testing.assert_array_equal(slots['stocks'], 1)
                env.step()
                assert np.all(env.current_frame["frame_id"] == -122)


@pytest.mark.parametrize("viewpoint", range(4))
def test_followers_pair_with_their_leaders_in_teams(monkeypatch, viewpoint: int) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    ics, fox = msl.Character.ICE_CLIMBERS, msl.Character.FOX
    with msl.EnvBatch(batch_size=1, length=4, num_players=4) as env:
        env.configure_matches([msl.MatchConfig(
            players=tuple(msl.PlayerConfig(c, team_id=t)
                          for c, t in ((ics, 0), (fox, 0), (ics, 1), (fox, 1))),
            is_teams=True, viewpoint_player=viewpoint)])
        env.reset_all()
        row = env.current_frame[0]
        assert row["slots"][0]["source_player"] == viewpoint
        assert sorted(row["slots"]["team_relation"]) == [0, 1, 2, 2]
        for slot, follower in zip(row["slots"], row["followers"]):
            if slot["char_id"] == ics:
                # Nana's own fighter kind; the rest is her leader's.
                assert (follower["present"], follower["char_id"]) == (1, 11)
                for field in ("source_player", "team_relation", "team_id", "stocks"):
                    assert follower[field] == slot[field], field
            else:
                assert not follower.tobytes().strip(b"\0")


def _nothing_stored(stored) -> bool:
    return (not stored["charge"].any() and not stored["gauge"].any()
            and np.all(stored["copied_char"] == 255)
            and not stored["spent"].any() and not stored["wall_jumps"].any()
            and np.all(stored["judge"] == 255) and not stored["_pad0"].any())


@pytest.mark.parametrize("viewpoint", range(4))
def test_stored_charges_stay_with_their_players_in_teams(monkeypatch, viewpoint: int) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    dk, fox, sheik, samus = (msl.Character.DONKEY_KONG, msl.Character.FOX,
                             msl.Character.SHEIK, msl.Character.SAMUS)
    # A full Giant Punch, no charge, six needles, a full Charge Shot.
    full = {dk: 10, fox: 0, sheik: 6, samus: 7}
    with msl.EnvBatch(batch_size=1, length=64, num_players=4) as env:
        env.configure_matches([msl.MatchConfig(
            players=tuple(msl.PlayerConfig(c, team_id=t)
                          for c, t in ((dk, 0), (fox, 0), (sheik, 1), (samus, 1))),
            is_teams=True, viewpoint_player=viewpoint)])
        env.reset_all()
        assert _nothing_stored(env.current_frame["stored"])
        for tick in range(700):
            if env.t == env.length:
                env.reset_cursor()
            pressed = env.controller_action_view[env.t]["players"]["buttons"]["B"]
            pressed[:] = 0
            # Donkey Kong and Samus start the move and it charges by itself;
            # Sheik holds the button. Fox stands still.
            pressed[0, 0] = pressed[0, 3] = tick == 150
            pressed[0, 2] = tick >= 150
            env.step()
            row = env.current_frame[0]
            order = row["slots"]["source_player"]
            assert sorted(order) == [0, 1, 2, 3] and order[0] == viewpoint
            charges = dict(zip(row["slots"]["char_id"], row["stored"]["charge"]))
            assert np.all(row["stored"]["copied_char"] == 255)
            assert all(charges[c] <= full[c] for c in full), (tick, charges)
        assert charges == full
        # A reset clears them; the restored match publishes them again.
        published = env.current_frame.copy()
        snapshot = env.save(0)
        env.reset_matches([0])
        assert _nothing_stored(env.current_frame["stored"])
        env.restore(0, snapshot)
        env.observe()
        np.testing.assert_array_equal(env.current_frame["stored"], published["stored"])


@pytest.mark.parametrize("viewpoint", range(2))
def test_oil_panic_stores_the_caught_damage_beside_the_count(monkeypatch, viewpoint: int) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=1, length=64) as env:
        env.configure_matches([msl.MatchConfig(
            players=(msl.PlayerConfig(msl.Character.GAMEWATCH),
                     msl.PlayerConfig(msl.Character.FALCO)),
            viewpoint_player=viewpoint)])
        env.reset_all()
        for tick in range(900):
            if env.t == env.length:
                env.reset_cursor()
            action = env.controller_action_view[env.t]["players"]
            # The bucket held out; Falco fires a laser into it now and then.
            action["main_stick_y"][0, 0] = 0.0 if tick >= 150 else 0.5
            action["buttons"]["B"][0, 0] = tick >= 150
            action["buttons"]["B"][0, 1] = tick >= 200 and tick % 60 < 2
            env.step()
            row = env.current_frame[0]
            bucket = row["stored"][list(row["slots"]["source_player"]).index(0)]
            # Each of Falco's lasers would have dealt 3.
            assert tuple(bucket["gauge"]) == (3 * bucket["charge"], 0), tick
            if bucket["charge"] == 3:
                break
        else:
            pytest.fail("the bucket never filled")
        other = row["stored"][list(row["slots"]["source_player"]).index(1)]
        assert _nothing_stored(other)


@pytest.mark.parametrize("viewpoint", range(2))
def test_fire_breath_drains_and_recovers(monkeypatch, viewpoint: int) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=1, length=64) as env:
        env.configure_matches([msl.MatchConfig(
            players=(msl.PlayerConfig(msl.Character.BOWSER),
                     msl.PlayerConfig(msl.Character.FOX)),
            viewpoint_player=viewpoint)])
        env.reset_all()
        lowest = (360.0, 380.0)
        for tick in range(1200):
            if env.t == env.length:
                env.reset_cursor()
            # 200 frames of breath, then nothing.
            env.controller_action_view[env.t]["players"]["buttons"]["B"][0, 0] = 150 <= tick < 350
            env.step()
            row = env.current_frame[0]
            bowser = row["stored"][list(row["slots"]["source_player"]).index(0)]
            fuel, size = (float(value) for value in bowser["gauge"])
            assert bowser["charge"] == 0 and 40 <= fuel <= 360 and 60 <= size <= 380, tick
            if tick < 150:
                assert (fuel, size) == (360, 380)
            lowest = min(lowest, (fuel, size))
        # Something under 200 frames of it drained, 1 a frame from each.
        assert 160 < lowest[0] < 220 and lowest[1] == lowest[0] + 20
        assert (fuel, size) == (360, 380)
        fox = row["stored"][list(row["slots"]["source_player"]).index(1)]
        assert _nothing_stored(fox)


@pytest.mark.parametrize("viewpoint", range(2))
def test_kirby_copied_ability_names_the_stored_move(monkeypatch, viewpoint: int) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=1, length=64) as env:
        env.configure_matches([msl.MatchConfig(
            players=(msl.PlayerConfig(msl.Character.KIRBY),
                     msl.PlayerConfig(msl.Character.SAMUS)),
            viewpoint_player=viewpoint)])
        env.reset_all()
        copied_at = None
        for tick in range(1500):
            if env.t == env.length:
                env.reset_cursor()
            row = env.current_frame[0]
            slots = list(row["slots"]["source_player"])
            kirby, samus = row["slots"][slots.index(0)], row["slots"][slots.index(1)]
            stored = row["stored"][slots.index(0)]
            action = env.controller_action_view[env.t]["players"]
            action["main_stick_x"][0, 0] = 0.5
            action["buttons"]["B"][0, 0] = 0
            if copied_at is None:
                assert (stored["copied_char"], stored["charge"]) == (255, 0)
                if tick >= 150 and abs(samus["pos_x"] - kirby["pos_x"]) > 16:
                    # Walk up to Samus.
                    action["main_stick_x"][0, 0] = 1.0 if samus["pos_x"] > kirby["pos_x"] else 0.0
                elif tick >= 150:
                    # Inhale, then B again to swallow.
                    action["buttons"]["B"][0, 0] = tick % 4 < 2
            else:
                # The copied Charge Shot charges by itself to Samus's full count.
                action["buttons"]["B"][0, 0] = tick == copied_at + 120
            env.step()
            stored = env.current_frame[0]["stored"][slots.index(0)]
            if copied_at is None and stored["copied_char"] != 255:
                copied_at = tick
            if copied_at is not None:
                assert stored["copied_char"] == samus["char_id"] == msl.Character.SAMUS
                if stored["charge"] == 7:
                    break
        else:
            pytest.fail("Kirby never held a full copied Charge Shot")
        assert _nothing_stored(env.current_frame[0]["stored"][slots.index(1)])


@pytest.mark.parametrize("viewpoint", range(2))
def test_judge_history_is_the_two_numbers_the_next_roll_avoids(monkeypatch, viewpoint: int) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=1, length=64) as env:
        env.configure_matches([msl.MatchConfig(
            players=(msl.PlayerConfig(msl.Character.GAMEWATCH),
                     msl.PlayerConfig(msl.Character.FOX)),
            viewpoint_player=viewpoint)])
        env.reset_all()
        pair, rolls = (1, 0), 0
        for tick in range(2000):
            if env.t == env.length:
                env.reset_cursor()
            action = env.controller_action_view[env.t]["players"]
            # A Judge every 100 frames.
            pressed = tick >= 150 and tick % 100 == 0
            action["main_stick_x"][0, 0] = 1.0 if pressed else 0.5
            action["buttons"]["B"][0, 0] = pressed
            env.step()
            row = env.current_frame[0]
            slots = list(row["slots"]["source_player"])
            now = tuple(int(number) for number in row["stored"][slots.index(0)]["judge"])
            if now != pair:
                # The new number is neither of the two before it, and the
                # newer of those is now the older.
                assert pressed and now[1] == pair[0] and now[0] not in pair and 0 <= now[0] < 9
                pair, rolls = now, rolls + 1
            fox = row["stored"][slots.index(1)]
            assert tuple(fox["judge"]) == (255, 255) and fox["spent"] == 0
        assert rolls == len(range(200, 2000, 100))


@pytest.mark.parametrize("viewpoint", range(4))
def test_spent_bits_stay_with_their_players_in_teams(monkeypatch, viewpoint: int) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    peach, marth, mario, luigi = (msl.Character.PEACH, msl.Character.MARTH,
                                  msl.Character.MARIO, msl.Character.LUIGI)
    # Side special lift, down special lift, float.
    side, down, floated = 2, 4, 8
    with msl.EnvBatch(batch_size=1, length=64, num_players=4) as env:
        env.configure_matches([msl.MatchConfig(
            players=tuple(msl.PlayerConfig(c, team_id=t)
                          for c, t in ((peach, 0), (marth, 0), (mario, 1), (luigi, 1))),
            is_teams=True, viewpoint_player=viewpoint)])
        env.reset_all()
        assert _nothing_stored(env.current_frame["stored"])
        seen = {c: set() for c in (peach, marth, mario, luigi)}
        longest = 0.0
        for tick in range(600):
            if env.t == env.length:
                env.reset_cursor()
            action = env.controller_action_view[env.t]["players"]
            action["buttons"]["X"][0] = 0
            action["buttons"]["B"][0] = 0
            action["main_stick_x"][0] = 0.5
            action["main_stick_y"][0] = 0.5
            # Everyone jumps. Peach keeps the button down and floats; at the
            # top Marth and Mario use the side special, Luigi the down one.
            action["buttons"]["X"][0, 0] = 150 <= tick < 300
            action["buttons"]["X"][0, 1:] = 150 <= tick < 156
            if tick == 172:
                action["buttons"]["B"][0, 1:] = 1
                action["main_stick_x"][0, 1:3] = 1.0
                action["main_stick_y"][0, 3] = 0.0
            if 172 < tick < 260:
                action["buttons"]["B"][0, 3] = tick % 2
            env.step()
            row = env.current_frame[0]
            order = row["slots"]["source_player"]
            assert sorted(order) == [0, 1, 2, 3] and order[0] == viewpoint
            assert not row["stored"]["wall_jumps"].any() and np.all(row["stored"]["judge"] == 255)
            for char, stored in zip(row["slots"]["char_id"], row["stored"]):
                seen[char].add(int(stored["spent"]))
                if char == peach:
                    # The float's frames left, only while she floats.
                    assert (stored["gauge"][0] > 0) <= (stored["spent"] == floated)
                    longest = max(longest, float(stored["gauge"][0]))
                else:
                    assert stored["gauge"][0] == 0
        assert seen[peach] == {0, floated} and longest > 100
        assert seen[marth] == {0, side} and seen[mario] == {0, side}
        assert seen[luigi] == {0, down}
        # Back on the ground: only Luigi's is still spent.
        final = dict(zip(row["slots"]["char_id"], row["stored"]["spent"]))
        assert final == {peach: 0, marth: 0, mario: 0, luigi: down}


def test_peach_pull_throw_reserves_runtime_items(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=64, length=128, num_players=4) as env:
        env.configure_matches([msl.MatchConfig(
            stage=tuple(msl.Stage)[i % 6], seed=i,
            players=tuple(msl.PlayerConfig(msl.Character.PEACH, team_id=t)
                          for t in (0, 1, 1, 0)),
            is_teams=True, friendly_fire=True) for i in range(env.batch_size)])
        env.reset_all()
        peak_items = 0
        for tick in range(8000):
            if env.t == env.length:
                env.reset_cursor()
            action = env.controller_action_view[env.t]["players"]
            action["main_stick_y"] = 0.0 if tick % 80 < 40 else 0.5
            action["buttons"]["B"] = tick % 80 < 20
            action["buttons"]["A"] = 40 <= tick % 80 < 60
            env.step_and_reset()
            peak_items = max(peak_items, int(env.current_frame["items"]["exists"].sum(axis=1).max()))
        # The old JObj reserve failed near frame 300; the old item-object
        # reserve failed near 6500 even after enlarging only the JObj pool.
        assert peak_items == 15


def test_python_wire_layout_matches_public_c_api() -> None:
    assert dtypes.controller_input_dtype().itemsize == 112
    assert dtypes.input_dtype().itemsize == 32
    assert dtypes.match_config_dtype().itemsize == 52
    assert dtypes.gamestate_dtype().itemsize == 1272
    assert dtypes.gamestate_stored_dtype().itemsize == 16
    assert dtypes.gamestate_stored_dtype().fields["copied_char"][1] == 1
    assert dtypes.gamestate_stored_dtype().fields["spent"][1] == 2
    assert dtypes.gamestate_stored_dtype().fields["wall_jumps"][1] == 3
    assert dtypes.gamestate_stored_dtype().fields["judge"][1] == 4
    assert dtypes.gamestate_stored_dtype().fields["gauge"][1] == 8
    assert dtypes.gamestate_stage_dtype().itemsize == 24
    assert dtypes.gamestate_stage_dtype().fields["whispy"][1] == 20
    assert dtypes.terminal_dtype().itemsize == 16

    buffers = msl.Buffers.empty(length=2, batch_size=3)
    players = buffers.controller_action_view["players"]
    assert np.all(players["main_stick_x"] == 0.5)
    assert np.all(players["main_stick_y"] == 0.5)


def test_whispy_observes_live_wind_and_clears_on_reset(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    wind = msl.WhispyBlowDirection
    assert (int(wind.NONE), int(wind.LEFT), int(wind.RIGHT)) == (0, 1, 2)
    with msl.EnvBatch(batch_size=2, length=128) as env:
        env.configure_matches([
            msl.MatchConfig(stage=msl.Stage.DREAM_LAND_N64, seed=1),
            msl.MatchConfig(stage=msl.Stage.FINAL_DESTINATION, seed=1),
        ])
        env.reset_all()
        assert np.all(env.current_frame["stage"]["whispy"] == wind.NONE)
        for _ in range(4000):
            if env.t == env.length:
                env.reset_cursor()
            env.step()
            directions = env.current_frame["stage"]["whispy"].copy()
            assert directions[1] == wind.NONE
            if directions[0] != wind.NONE:
                break
        else:
            pytest.fail("Whispy never started blowing")
        assert directions[0] in (wind.LEFT, wind.RIGHT)
        env.observe()
        np.testing.assert_array_equal(env.current_frame["stage"]["whispy"], directions)
        env.configure_match(stage=msl.Stage.FINAL_DESTINATION, env_ids=[0])
        env.reset_matches([0])
        assert np.all(env.current_frame["stage"]["whispy"] == wind.NONE)


@pytest.mark.parametrize("characters", [
    (msl.Character.YOSHI, msl.Character.BOWSER),
    (msl.Character.NESS, msl.Character.LINK),
    (msl.Character.YOUNG_LINK, msl.Character.SAMUS),
    (msl.Character.MEWTWO, msl.Character.FOX),
    (msl.Character.GAMEWATCH, msl.Character.FOX),
    (msl.Character.GAMEWATCH, msl.Character.MEWTWO),
])
def test_character_articles_restore_at_another_batch_index(monkeypatch, characters) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=2, length=128, num_players=4) as env:
        for stage in msl.Stage:
            config = msl.MatchConfig(stage=stage, is_teams=True, players=tuple(
                msl.PlayerConfig(character, team_id=team)
                for character, team in zip(
                    characters * 2,
                    (0, 1, 1, 0))))
            env.configure_matches([config, config])
            env.reset_all()

            def step(special=False):
                if env.t == env.length:
                    env.reset_cursor()
                actions = env.controller_action_view[env.t]["players"]
                actions["buttons"] = 0
                actions["main_stick_x"] = 0.5
                actions["main_stick_y"] = 0.5
                if special:
                    actions["buttons"]["B"] = True
                    actions["main_stick_y"][:, ::2] = 1.0
                env.step()

            for _ in range(150):
                step()
            for _ in range(30):
                step(special=True)
            assert np.any(env.current_frame[0]["items"]["exists"])
            saved = env.save(0)
            for _ in range(64):
                step()
            expected = env.current_frame[0].tobytes()
            env.restore(1, saved)
            for _ in range(64):
                step()
            assert env.current_frame[1].tobytes() == expected


ITEM_MEWTWO_SHADOW_BALL = 112


def test_mewtwo_held_shadow_ball_restores_at_another_batch_index(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=2, length=128) as env:
        config = msl.MatchConfig(
            stage=msl.Stage.FINAL_DESTINATION,
            players=(msl.PlayerConfig(msl.Character.MEWTWO),
                     msl.PlayerConfig(msl.Character.FOX)))
        env.configure_matches([config, config])
        env.reset_all()

        def step(b=False):
            if env.t == env.length:
                env.reset_cursor()
            actions = env.controller_action_view[env.t]["players"]
            actions["buttons"] = 0
            actions["main_stick_x"] = 0.5
            actions["main_stick_y"] = 0.5
            actions["buttons"]["B"] = b
            env.step()

        def held_ball(index):
            items = env.current_frame[index]["items"]
            ball = items[(items["exists"] != 0) & (items["type"] == ITEM_MEWTWO_SHADOW_BALL)]
            return ball[ball["state"] == 0]

        def thrown_ball(index):
            items = env.current_frame[index]["items"]
            ball = items[(items["exists"] != 0) & (items["type"] == ITEM_MEWTWO_SHADOW_BALL)]
            return ball[(ball["state"] >= 1) & (ball["state"] <= 8)]

        for _ in range(150):
            step()
        # Tap B to start charging; Mewtwo keeps charging until B is tapped again.
        step(b=True)
        for _ in range(40):
            step()
        assert len(held_ball(0)) == 1
        saved = env.save(0)

        def fox_percent(index):
            slots = env.current_frame[index]["slots"]
            return float(slots["percent"][slots["source_player"] == 1][0])

        def fire_and_observe(index):
            step(b=True)
            thrown = False
            for _ in range(90):
                step()
                thrown |= len(thrown_ball(index)) == 1
            assert thrown
            return fox_percent(index)

        for _ in range(64):
            step()
        expected = env.current_frame[0].tobytes()
        expected_percent = fire_and_observe(0)
        assert expected_percent > 0
        expected_after_throw = env.current_frame[0].tobytes()

        env.restore(1, saved)
        for i in range(64):
            step()
            if i == 0:
                assert len(held_ball(1)) == 1
        assert env.current_frame[1].tobytes() == expected
        assert fire_and_observe(1) == expected_percent
        assert env.current_frame[1].tobytes() == expected_after_throw


def test_doubles_continues_until_entire_team_is_eliminated(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=1, length=128, num_players=4) as env:
        env.configure_matches([msl.MatchConfig(
            stage=msl.Stage.FINAL_DESTINATION,
            players=tuple(msl.PlayerConfig(msl.Character.FOX, team_id=t)
                          for t in (0, 1, 1, 0)), is_teams=True)])
        env.reset_all()
        first_elimination = None
        for tick in range(10000):
            if env.t == env.length:
                env.reset_cursor()
            actions = env.controller_action_view[env.t]["players"]
            actions["main_stick_x"] = 0.5
            target = 1 if first_elimination is None else 2
            actions[0, target]["main_stick_x"] = 1.0
            _, terminal = env.step_and_reset()
            slots = env.current_frame["slots"][0]
            stocks = slots["stocks"][np.argsort(slots["source_player"])]
            if stocks[1] == 0 and first_elimination is None:
                first_elimination = tick
            if terminal["done"][0]:
                assert stocks[1] == stocks[2] == 0
                assert stocks[0] == stocks[3] == 4
                assert terminal["alive_team_count"][0] == 1
                assert tick > first_elimination + 120
                break
        else:
            raise AssertionError("scripted team stockout never completed")


def test_supported_character_and_stage_enums() -> None:
    assert [int(character) for character in msl.Character] == [
        1,
        16,
        24,
        2,
        3,
        25,
        14,
        5,
        7,
        9,
        10,
        12,
        23,
        4,
        13,
        8,
        6,
        20,
        15,
        17,
        0,
        21,
        18,
        26,
        19,
        22,
    ]
    assert [int(stage) for stage in msl.Stage] == [2, 3, 8, 28, 31, 32]


def test_envbatch_controller_step_and_arbitrary_restore(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=2, length=4) as env:
        neutral = msl.neutral_controller((env.length, env.batch_size))
        msl.write_controller(env.controller_action_view, neutral, player=0)
        msl.write_controller(env.controller_action_view, neutral, player=1)

        env.reset_all()
        assert np.array_equal(env.current_frame["frame_id"], [-123, -123])
        state = env.save(0)
        env.step()
        expected = env.current_frame[0].copy()

        env.restore(1, state)
        env.observe()
        assert env.current_frame[1]["frame_id"] == -123
        env.step()
        assert env.current_frame[1].tobytes() == expected.tobytes()
        assert np.all(env.done_at(1) == 0)


def test_match_config_preserves_per_environment_values(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=2, length=1) as env:
        for percent in (-1, 101, 12.5, float('nan')):
            with np.testing.assert_raises(ValueError):
                env.configure_match(config=msl.MatchConfig(players=(
                    msl.PlayerConfig(msl.Character.FOX, start_percent=percent),
                    msl.PlayerConfig(msl.Character.FOX))))
        env.configure_matches(
            [
                msl.MatchConfig(stage=msl.Stage.BATTLEFIELD),
                msl.MatchConfig(
                    stage=msl.Stage.YOSHIS_STORY,
                    players=(
                        msl.PlayerConfig(msl.Character.PEACH),
                        msl.PlayerConfig(msl.Character.JIGGLYPUFF),
                    ),
                    seed=99,
                ),
            ]
        )
        config = env.match_config_view
        assert config[0]["stage"] == msl.Stage.BATTLEFIELD
        assert config[1]["stage"] == msl.Stage.YOSHIS_STORY
        assert config[1]["random_seed"] == 99
        assert np.array_equal(
            config[1]["players"]["character"][:2],
            [msl.Character.PEACH, msl.Character.JIGGLYPUFF],
        )


def test_reset_cursor_carries_current_frame(monkeypatch) -> None:
    # Regression test: reset_cursor used to only rewind the ring index, so a
    # masked reset right after the wrap exposed stale slot-0 rows (from
    # reset_all) for every non-reset match.
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=2, length=4) as env:
        neutral = msl.neutral_controller((env.length, env.batch_size))
        msl.write_controller(env.controller_action_view, neutral, player=0)
        msl.write_controller(env.controller_action_view, neutral, player=1)

        env.reset_all()
        for _ in range(env.length):
            env.step()
        assert env.t == env.length
        before = env.current_frame.copy()
        assert np.all(before["frame_id"] == -123 + env.length)

        # A partial reset is valid even on the final ring slot and must only
        # touch the selected row.
        env.reset_matches([0])
        assert env.current_frame[0]["frame_id"] == -123
        assert env.current_frame[1].tobytes() == before[1].tobytes()

        patched = env.current_frame.copy()
        env.reset_cursor()
        assert env.t == 0
        assert env.current_frame.tobytes() == patched.tobytes()


def test_step_and_reset_defers_resets_one_step(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=2, length=4) as env:
        neutral = msl.neutral_controller((env.length, env.batch_size))
        msl.write_controller(env.controller_action_view, neutral, player=0)
        msl.write_controller(env.controller_action_view, neutral, player=1)
        env.configure_matches(
            [
                msl.MatchConfig(max_frame=0),
                msl.MatchConfig(),
            ]
        )
        env.reset_all()

        # Match 0 hits max_frame after 123 steps (frame_id counts up from
        # -123); the ring wraps many times along the way.
        for _ in range(123):
            is_resetting, terminal = env.step_and_reset()
        # The terminal frame itself is published before any reset happens.
        assert not is_resetting.any()
        assert terminal["done"].tolist() == [1, 0]
        assert env.current_frame[0]["frame_id"] == 0
        assert env.current_frame[1]["frame_id"] == 0

        # The following call resets match 0 (ignoring its queued action) and
        # publishes its entry frame while match 1 steps normally.
        is_resetting, terminal = env.step_and_reset()
        assert is_resetting.tolist() == [True, False]
        assert terminal["done"].tolist() == [0, 0]
        assert env.current_frame[0]["frame_id"] == -123
        assert env.current_frame[1]["frame_id"] == 1


def test_masked_step_republishes_unstepped_matches(monkeypatch) -> None:
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    with msl.EnvBatch(batch_size=2, length=4) as env:
        neutral = msl.neutral_controller((env.length, env.batch_size))
        msl.write_controller(env.controller_action_view, neutral, player=0)
        msl.write_controller(env.controller_action_view, neutral, player=1)
        env.reset_all()
        env.step()
        before = env.current_frame.copy()

        env.step(np.array([1, 0], dtype=np.uint8))
        assert env.current_frame[0]["frame_id"] == before[0]["frame_id"] + 1
        assert env.current_frame[1].tobytes() == before[1].tobytes()


def test_game_data_root_identity(monkeypatch, tmp_path) -> None:
    import os

    lib = msl._native.library()
    baseline = lib.msl_game_data_references()
    monkeypatch.chdir(ROOT)
    assert lib.msl_game_data_acquire(b"data") == 0
    try:
        alias = tmp_path / "alias"
        alias.symlink_to(ROOT / "data", target_is_directory=True)
        for root in (ROOT / "data", ROOT / "data/raw", alias, "./data/", "data/raw/"):
            assert lib.msl_game_data_acquire(os.fsencode(root)) == 0
            lib.msl_game_data_release()
        monkeypatch.chdir(tmp_path)
        assert lib.msl_game_data_acquire(b"data") == 3
        (tmp_path / "data/raw").mkdir(parents=True)
        assert lib.msl_game_data_acquire(b"data") == 3
        assert lib.msl_game_data_references() == baseline + 1
    finally:
        lib.msl_game_data_release()
    assert lib.msl_game_data_references() == baseline


def test_game_data_is_shared_across_batches_and_forks(monkeypatch, tmp_path) -> None:
    import os

    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    lib = msl._native.library()
    baseline = lib.msl_game_data_references()
    # A preload holds one reference; repeating it is a no-op.
    assert msl.preload_game_data() == (ROOT / "data").resolve()
    assert msl.preload_game_data() == (ROOT / "data").resolve()
    assert msl.game_data_loaded()
    assert lib.msl_game_data_references() == baseline + 1
    # Batches share the loaded data instead of loading their own copy.
    with msl.EnvBatch(batch_size=1, length=32) as first:
        assert lib.msl_game_data_references() == baseline + 2
        with msl.EnvBatch(batch_size=1, length=32) as second:
            assert lib.msl_game_data_references() == baseline + 3
            for env in (first, second):
                env.configure_matches([msl.MatchConfig(seed=7)])
                env.reset_all()
                for _ in range(30):
                    env.step()
            np.testing.assert_array_equal(first.current_frame, second.current_frame)
        assert lib.msl_game_data_references() == baseline + 2
    assert lib.msl_game_data_references() == baseline + 1
    # Symlinks to the loaded raw directory identify the same data.
    (tmp_path / "raw").symlink_to(ROOT / "data" / "raw")
    assert msl.preload_game_data(tmp_path) == tmp_path.resolve()
    assert lib.msl_game_data_references() == baseline + 1
    # A forked child inherits the loaded data and can build batches on it.
    pid = os.fork()
    if pid == 0:
        code = 1
        try:
            with msl.EnvBatch(batch_size=1, length=4) as env:
                env.configure_matches([msl.MatchConfig(seed=7)])
                env.reset_all()
                env.step()
                code = 0 if int(env.current_frame["frame_id"][0]) == -122 else 2
        finally:
            os._exit(code)
    _, status = os.waitpid(pid, 0)
    assert os.waitstatus_to_exitcode(status) == 0
