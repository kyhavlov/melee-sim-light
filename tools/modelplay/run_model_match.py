from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

from tools.modelplay.sim_env import SimSession
from tools.modelplay.slippi_ai_bridge import build_model_agent, stop_model_agent
from tools.modelplay.state_adapter import SimFrameState
from tools.modelplay.viewer_trace import ViewerTrace


CHAR_IDS = {
    "fox": 1,
    "falco": 22,
}


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


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Run a slippi-ai model-vs-model match inside melee-sim-light.")
    ap.add_argument("--slippi-ai-root", type=Path, required=True)
    ap.add_argument("--p1-model", type=Path, required=True)
    ap.add_argument("--p2-model", type=Path, required=True)
    ap.add_argument(
        "--dataset",
        type=Path,
        default=Path(
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
        ),
    )
    ap.add_argument("--start-record", type=int, default=0)
    ap.add_argument(
        "--start-mode",
        choices=("replay", "sim-init"),
        default="replay",
        help="replay restores --dataset/--start-record; sim-init starts from C-owned match init",
    )
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
    ap.add_argument("--p1-char", choices=sorted(CHAR_IDS), default=None)
    ap.add_argument("--p2-char", choices=sorted(CHAR_IDS), default=None)
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    out_dir = args.out.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    char_ids = None
    if args.p1_char is not None or args.p2_char is not None:
        char_ids = (
            CHAR_IDS[args.p1_char or "falco"],
            CHAR_IDS[args.p2_char or "fox"],
        )

    session = SimSession(
        dataset_path=args.dataset,
        start_record=args.start_record,
        char_ids=char_ids,
        start_mode=args.start_mode,
    )
    p1 = build_model_agent(slippi_ai_root=args.slippi_ai_root, model_path=args.p1_model, name=args.p1_name)
    p2 = build_model_agent(slippi_ai_root=args.slippi_ai_root, model_path=args.p2_model, name=args.p2_name)
    trace = ViewerTrace()

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
            p1_game = env_out.gamestates[1]
            p2_game = env_out.gamestates[2]
            p1_controller = p1.step(p1_game, needs_reset=env_out.needs_reset)
            p2_controller = p2.step(p2_game, needs_reset=env_out.needs_reset)
            env_out = session.step({1: p1_controller, 2: p2_controller})
            trace.add_frame(session.current_frame_state, session.last_controllers)
            frames_run += 1
            state = session.current_frame_state
            signature = _static_signature(state)
            current_frame = len(trace.frames) - 1
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

            if int(state.stocks[0]) == 0 or int(state.stocks[1]) == 0:
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

        trace_path = out_dir / "trace.json"
        trace.write_json(trace_path)
        trace_to_failure_path = None
        termination_reason = "max_frames"
        if int(session.current_frame_state.stocks[0]) == 0 or int(session.current_frame_state.stocks[1]) == 0:
            termination_reason = "game_over"
        elif static_failure is not None:
            termination_reason = "static_failure"
            trace_to_failure_path = out_dir / "trace_to_failure.json"
            trace.write_json(trace_to_failure_path, frame_limit=static_failure["start_frame"] + 1)
        elif player_static_failure is not None:
            termination_reason = "player_static_failure"
            trace_to_failure_path = out_dir / "trace_to_failure.json"
            trace.write_json(trace_to_failure_path, frame_limit=player_static_failure["start_frame"] + 1)

        summary = {
            "dataset": str(args.dataset),
            "start_record": args.start_record,
            "start_mode": args.start_mode,
            "frames_run": frames_run,
            "final_frame_id": session.current_frame_state.frame_id,
            "final_stocks": session.current_frame_state.stocks[:2].tolist(),
            "final_percent": [float(x) for x in session.current_frame_state.percent[:2]],
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
                    "dataset": str(args.dataset),
                    "start_record": args.start_record,
                    "start_mode": args.start_mode,
                    "max_frames": args.max_frames,
                    "static_frame_threshold": args.static_frame_threshold,
                    "p1_name": args.p1_name,
                    "p2_name": args.p2_name,
                    "p1_char": args.p1_char,
                    "p2_char": args.p2_char,
                },
                indent=2,
            )
            + "\n"
        )
        print(f"wrote trace: {trace_path}")
        print(json.dumps(summary, indent=2))
        return 0
    finally:
        stop_model_agent(p1)
        stop_model_agent(p2)
        session.close()


if __name__ == "__main__":
    raise SystemExit(main())
