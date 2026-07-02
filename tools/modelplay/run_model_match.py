from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

from tools.modelplay.sim_env import SimSession
from tools.modelplay.slippi_ai_bridge import build_model_agent, stop_model_agent
from tools.modelplay.state_adapter import SimFrameState
from tools.viewer.msltrace1 import MslTraceWriter


CHAR_IDS = {
    "fox": 1,
    "falco": 22,
}

DEFAULT_DOUBLES_CHARS = ("fox", "falco", "fox", "falco")
DEFAULT_DOUBLES_TEAMS = (0, 0, 1, 1)
DEFAULT_DOUBLES_FACING = (1, 1, 0, 0)


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def _static_signature(state: SimFrameState) -> tuple:
    players = []
    for idx in range(state.num_players):
        players.append(
            (
                int(state.action_id[idx]),
                float(state.action_frame[idx]),
                round(float(state.pos_x[idx]), 6),
                round(float(state.pos_y[idx]), 6),
                int(state.facing[idx]),
                int(state.on_ground[idx]),
                int(state.stocks[idx]),
                round(float(state.percent[idx]), 6),
                round(float(state.shield_hp[idx]), 6),
                int(state.hitlag[idx]),
                int(state.hitstun[idx]),
                int(state.jumps_left[idx]),
                int(state.hurtbox_state[idx]),
            )
        )

    items = []
    for item in state.items:
        if int(item["exists"]) == 0:
            continue
        items.append(
            (
                int(item["type"]),
                int(item["owner"]),
                int(item["state"]),
                int(item["spawn_id"]),
                round(float(item["pos_x"]), 6),
                round(float(item["pos_y"]), 6),
                round(float(item["vel_x"]), 6),
                round(float(item["vel_y"]), 6),
                round(float(item["timer"]), 6),
            )
        )

    return tuple(players), tuple(items)


def _player_static_signature(state: SimFrameState, player_idx: int) -> tuple:
    return (
        int(state.action_id[player_idx]),
        float(state.action_frame[player_idx]),
        round(float(state.pos_x[player_idx]), 6),
        round(float(state.pos_y[player_idx]), 6),
        int(state.facing[player_idx]),
        int(state.on_ground[player_idx]),
        int(state.stocks[player_idx]),
        round(float(state.percent[player_idx]), 6),
        round(float(state.shield_hp[player_idx]), 6),
        int(state.hitlag[player_idx]),
        int(state.hitstun[player_idx]),
        int(state.jumps_left[player_idx]),
        int(state.hurtbox_state[player_idx]),
    )


def _parse_int_tuple(value: str, *, expected_len: int, name: str) -> tuple[int, ...]:
    out = tuple(int(part.strip()) for part in value.split(",") if part.strip())
    if len(out) != expected_len:
        raise ValueError(f"expected {expected_len} comma-separated values for {name}, got {value!r}")
    return out


def _game_over(state: SimFrameState) -> bool:
    if state.is_teams:
        teams_with_stocks = {
            int(state.team_id[idx])
            for idx in range(state.num_players)
            if int(state.stocks[idx]) > 0
        }
        return len(teams_with_stocks) <= 1
    alive_count = sum(1 for idx in range(state.num_players) if int(state.stocks[idx]) > 0)
    return alive_count <= 1 or any(int(state.stocks[idx]) == 0 for idx in range(state.num_players))


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Run a slippi-ai model-vs-model match inside melee-sim-light.")
    ap.add_argument("--slippi-ai-root", type=Path, required=True)
    ap.add_argument("--p1-model", type=Path, required=True)
    ap.add_argument("--p2-model", type=Path, required=True)
    ap.add_argument("--p3-model", type=Path, default=None)
    ap.add_argument("--p4-model", type=Path, default=None)
    ap.add_argument("--max-frames", type=int, default=30000)
    ap.add_argument(
        "--static-frame-threshold",
        type=int,
        default=600,
        help="stop early if the full state signature repeats this many consecutive frames",
    )
    ap.add_argument("--out", type=Path, default=Path("reports/triage") / f"{_timestamp()}_modelplay")
    ap.add_argument("--p1-name", default="P1")
    ap.add_argument("--p2-name", default="P2")
    ap.add_argument("--p3-name", default="P3")
    ap.add_argument("--p4-name", default="P4")
    ap.add_argument("--p1-char", choices=sorted(CHAR_IDS), default=None)
    ap.add_argument("--p2-char", choices=sorted(CHAR_IDS), default=None)
    ap.add_argument("--p3-char", choices=sorted(CHAR_IDS), default=None)
    ap.add_argument("--p4-char", choices=sorted(CHAR_IDS), default=None)
    ap.add_argument("--doubles", action="store_true", help="Run a 4-player 2v2 sim-init game")
    ap.add_argument("--team-ids", default="0,0,1,1", help="Comma-separated team ids for --doubles")
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    out_dir = args.out.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.doubles:
        char_names = (
            args.p1_char or DEFAULT_DOUBLES_CHARS[0],
            args.p2_char or DEFAULT_DOUBLES_CHARS[1],
            args.p3_char or DEFAULT_DOUBLES_CHARS[2],
            args.p4_char or DEFAULT_DOUBLES_CHARS[3],
        )
        char_ids = tuple(CHAR_IDS[name] for name in char_names)
        team_ids = _parse_int_tuple(args.team_ids, expected_len=4, name="--team-ids")
        facing = DEFAULT_DOUBLES_FACING
        start_mode = "sim-init"
    else:
        char_ids = None
        if args.p1_char is not None or args.p2_char is not None:
            char_ids = (
                CHAR_IDS[args.p1_char or "falco"],
                CHAR_IDS[args.p2_char or "fox"],
            )
        team_ids = None
        facing = None
        start_mode = "sim-init"

    session = SimSession(
        start_record=0,
        char_ids=char_ids,
        team_ids=team_ids,
        is_teams=args.doubles,
        start_mode=start_mode,
        facing=facing,
    )
    model_specs = {
        1: (args.p1_model, args.p1_name),
        2: (args.p2_model, args.p2_name),
    }
    if args.doubles:
        model_specs[3] = (args.p3_model or args.p1_model, args.p3_name)
        model_specs[4] = (args.p4_model or args.p2_model, args.p4_name)
    agents = {
        port: build_model_agent(slippi_ai_root=args.slippi_ai_root, model_path=model_path, name=name)
        for port, (model_path, name) in model_specs.items()
    }
    trace = MslTraceWriter(
        metadata={"model": {"runner": "tools.modelplay.run_model_match"}},
        match_start=session.trace_start_info(),
    )

    try:
        env_out = session.reset()
        trace.add_frame(session.current_frame_state, session.last_controllers)
        prev_signature = _static_signature(session.current_frame_state)
        static_repeat_count = 0
        static_start_frame = 0
        static_failure: dict | None = None
        global_change_count = 0
        player_prev_signatures = [
            _player_static_signature(session.current_frame_state, idx)
            for idx in range(session.current_frame_state.num_players)
        ]
        player_static_repeat_counts = [0 for _ in range(session.current_frame_state.num_players)]
        player_static_start_frames = [0 for _ in range(session.current_frame_state.num_players)]
        player_static_start_global_changes = [0 for _ in range(session.current_frame_state.num_players)]
        player_static_failure: dict | None = None

        frames_run = 0
        while frames_run < args.max_frames:
            controllers = {
                port: agent.step(env_out.gamestates[port], needs_reset=env_out.needs_reset)
                for port, agent in agents.items()
            }
            env_out = session.step(controllers)
            trace.add_frame(session.current_frame_state, session.last_controllers)
            frames_run += 1
            state = session.current_frame_state
            signature = _static_signature(state)
            current_frame = trace.frame_count - 1
            if signature == prev_signature:
                static_repeat_count += 1
            else:
                prev_signature = signature
                static_repeat_count = 0
                static_start_frame = current_frame
                global_change_count += 1

            for idx in range(state.num_players):
                player_signature = _player_static_signature(state, idx)
                if player_signature == player_prev_signatures[idx]:
                    player_static_repeat_counts[idx] += 1
                else:
                    player_prev_signatures[idx] = player_signature
                    player_static_repeat_counts[idx] = 0
                    player_static_start_frames[idx] = current_frame
                    player_static_start_global_changes[idx] = global_change_count

            if _game_over(state):
                break
            if static_repeat_count >= args.static_frame_threshold:
                static_failure = {
                    "start_frame": static_start_frame,
                    "end_frame": current_frame,
                    "repeat_count": static_repeat_count + 1,
                    "action_ids": [int(x) for x in state.action_id[: state.num_players]],
                    "stocks": [int(x) for x in state.stocks[: state.num_players]],
                    "percent": [float(x) for x in state.percent[: state.num_players]],
                }
                break
            for idx in range(state.num_players):
                if player_static_repeat_counts[idx] < args.static_frame_threshold:
                    continue
                if global_change_count <= player_static_start_global_changes[idx]:
                    continue
                player_static_failure = {
                    "player_index": idx,
                    "start_frame": player_static_start_frames[idx],
                    "end_frame": current_frame,
                    "repeat_count": player_static_repeat_counts[idx] + 1,
                    "player_action_id": int(state.action_id[idx]),
                    "player_stocks": int(state.stocks[idx]),
                    "player_percent": float(state.percent[idx]),
                    "all_action_ids": [int(x) for x in state.action_id[: state.num_players]],
                    "all_stocks": [int(x) for x in state.stocks[: state.num_players]],
                    "all_percent": [float(x) for x in state.percent[: state.num_players]],
                }
                break
            if player_static_failure is not None:
                break

        trace_path = out_dir / "trace.msltrace.json"
        trace.write_json(trace_path)
        trace_to_failure_path = None
        termination_reason = "max_frames"
        if _game_over(session.current_frame_state):
            termination_reason = "game_over"
        elif static_failure is not None:
            termination_reason = "static_failure"
            trace_to_failure_path = out_dir / "trace_to_failure.msltrace.json"
            trace.write_json(trace_to_failure_path, frame_limit=static_failure["start_frame"] + 1)
        elif player_static_failure is not None:
            termination_reason = "player_static_failure"
            trace_to_failure_path = out_dir / "trace_to_failure.msltrace.json"
            trace.write_json(trace_to_failure_path, frame_limit=player_static_failure["start_frame"] + 1)

        summary = {
            "start_mode": start_mode,
            "doubles": args.doubles,
            "team_ids": None if team_ids is None else list(team_ids),
            "frames_run": frames_run,
            "final_frame_id": session.current_frame_state.frame_id,
            "final_stocks": session.current_frame_state.stocks[: session.current_frame_state.num_players].tolist(),
            "final_percent": [
                float(x) for x in session.current_frame_state.percent[: session.current_frame_state.num_players]
            ],
            "game_over": termination_reason == "game_over",
            "termination_reason": termination_reason,
            "static_failure": static_failure,
            "player_static_failure": player_static_failure,
            "trace": str(trace_path),
            "trace_to_failure": None if trace_to_failure_path is None else str(trace_to_failure_path),
        }
        (out_dir / "summary.txt").write_text(json.dumps(summary, indent=2) + "\n")
        (out_dir / "config.json").write_text(
            json.dumps(
                {
                    "slippi_ai_root": str(args.slippi_ai_root),
                    "p1_model": str(args.p1_model),
                    "p2_model": str(args.p2_model),
                    "p3_model": None if args.p3_model is None else str(args.p3_model),
                    "p4_model": None if args.p4_model is None else str(args.p4_model),
                    "start_mode": start_mode,
                    "doubles": args.doubles,
                    "team_ids": None if team_ids is None else list(team_ids),
                    "max_frames": args.max_frames,
                    "static_frame_threshold": args.static_frame_threshold,
                    "p1_name": args.p1_name,
                    "p2_name": args.p2_name,
                    "p3_name": args.p3_name,
                    "p4_name": args.p4_name,
                    "p1_char": args.p1_char,
                    "p2_char": args.p2_char,
                    "p3_char": args.p3_char,
                    "p4_char": args.p4_char,
                },
                indent=2,
            )
            + "\n"
        )
        print(f"wrote trace: {trace_path}")
        print(json.dumps(summary, indent=2))
        return 0
    finally:
        for agent in agents.values():
            stop_model_agent(agent)
        session.close()


if __name__ == "__main__":
    raise SystemExit(main())
