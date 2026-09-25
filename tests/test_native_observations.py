from pathlib import Path

import numpy as np
import pytest

import melee_sim as msl


ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize("teams,ports,expected", [
    (None, (0, 1), (1, 0)),
    (None, (3, 0, 2), (1, 1, 0)),
    (None, (0, 1, 2, 3), (1, 0, 1, 0)),
    ((0, 1, 1), (0, 1, 2), (1, 0, 0)),
    ((0, 1, 1, 0), (3, 0, 2, 1), (1, 0, 0, 1)),
    ((0, 0, 0, 0), (3, 0, 2, 1), (1, 1, 1, 1)),
])
def test_initial_facing_uses_opponents_and_physical_ports(monkeypatch, teams, ports, expected):
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    players = tuple(msl.PlayerConfig(msl.Character.FOX, controller_port=port,
                                    team_id=None if teams is None else teams[i])
                    for i, port in enumerate(ports))
    with msl.EnvBatch(1, length=1, num_players=len(players)) as env:
        env.configure_match(config=msl.MatchConfig(players=players, is_teams=teams is not None))
        env.reset_all()
        slots = env.current_frame[0]['slots']
        roster = slots[slots['source_player'] != 255]
        np.testing.assert_array_equal(roster[np.argsort(roster['source_player'])]['facing'], expected)
        assert np.all(slots[slots['source_player'] == 255]['present'] == 0)


def test_explicit_facing_and_masked_startup_reset(monkeypatch):
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    players = tuple(msl.PlayerConfig(msl.Character.FOX, facing=i % 2,
                                    team_id=(0, 1, 1, 0)[i]) for i in range(4))
    with msl.EnvBatch(2, length=4, num_players=4) as env:
        env.configure_match(config=msl.MatchConfig(players=players, is_teams=True))
        env.reset_all()
        initial = env.current_frame.copy()
        slots = initial[0]['slots']
        np.testing.assert_array_equal(slots[np.argsort(slots['source_player'])]['facing'], [0, 1, 0, 1])
        assert np.all(initial['frame_id'] == -123)
        env.controller_action_view[0]['players']['main_stick_x'] = 1
        env.step()
        untouched = env.current_frame[1].copy()
        env.reset_matches([0])
        np.testing.assert_array_equal(env.current_frame[0], initial[0])
        np.testing.assert_array_equal(env.current_frame[1], untouched)


def test_stockout_disappears_after_ko_and_returns_on_stock_share(monkeypatch):
    monkeypatch.setenv("MSL_DATA_DIR", str(ROOT / "data"))
    players = tuple(msl.PlayerConfig(msl.Character.FOX, team_id=t) for t in (0, 1, 1, 0))
    with msl.EnvBatch(1, length=1, num_players=4, action_format='raw') as env:
        env.configure_match(config=msl.MatchConfig(players=players, is_teams=True, stocks=2))
        env.reset_all()
        saw_ko = False
        for _ in range(1200):
            env.reset_cursor()
            env.raw_action_view[0]['players']['main_x'][0, 0] = -80
            env.step()
            slot = env.current_frame[0]['slots'][0]
            if slot['stocks'] == 0:
                saw_ko |= bool(slot['present'])
                if not slot['present']:
                    break
        else:
            pytest.fail('fighter never finished its final KO')
        assert saw_ko
        assert (slot['source_player'], slot['team_id'], slot['char_id'], slot['stocks']) == (0, 0, 1, 0)
        for name in slot.dtype.names:
            if name not in ('source_player', 'team_relation', 'team_id', 'char_id', 'stocks'):
                assert np.all(slot[name] == 0), name
        # Observing and restoring preserve the absence contract and roster.
        snapshot = env.save(0)
        env.reset_cursor()
        absent = env.current_frame.copy()
        env.observe()
        np.testing.assert_array_equal(env.current_frame, absent)
        env.restore(0, snapshot)
        env.observe()
        np.testing.assert_array_equal(env.current_frame, absent)

        env.reset_cursor()
        env.raw_action_view[0]['players'] = 0
        env.raw_action_view[0]['players']['buttons'][0, 0] = 0x1000
        env.step()
        slots = env.current_frame[0]['slots']
        assert slots[0]['present'] == 1
        assert slots[0]['stocks'] == 1
        assert slots[1]['source_player'] == 3
        assert slots[1]['stocks'] == 1
