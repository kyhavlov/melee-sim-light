from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

from tools.modelplay.sim_env import SimSession
from tools.modelplay.slippi_ai_bridge import build_model_agent, stop_model_agent
from tools.modelplay.viewer_trace import ViewerTrace


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


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
    ap.add_argument("--max-frames", type=int, default=30000)
    ap.add_argument("--out", type=Path, default=Path("reports/triage") / f"{_timestamp()}_modelplay")
    ap.add_argument("--p1-name", default="P1")
    ap.add_argument("--p2-name", default="P2")
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    out_dir = args.out.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    session = SimSession(dataset_path=args.dataset, start_record=args.start_record)
    p1 = build_model_agent(slippi_ai_root=args.slippi_ai_root, model_path=args.p1_model, name=args.p1_name)
    p2 = build_model_agent(slippi_ai_root=args.slippi_ai_root, model_path=args.p2_model, name=args.p2_name)
    trace = ViewerTrace()

    try:
        env_out = session.reset()
        trace.add_frame(session.current_frame_state, session.last_controllers)

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
            if int(state.stocks[0]) == 0 or int(state.stocks[1]) == 0:
                break

        trace_path = out_dir / "trace.json"
        trace.write_json(trace_path)
        summary = {
            "dataset": str(args.dataset),
            "start_record": args.start_record,
            "frames_run": frames_run,
            "final_frame_id": session.current_frame_state.frame_id,
            "final_stocks": session.current_frame_state.stocks[:2].tolist(),
            "final_percent": [float(x) for x in session.current_frame_state.percent[:2]],
            "game_over": bool(
                int(session.current_frame_state.stocks[0]) == 0
                or int(session.current_frame_state.stocks[1]) == 0
            ),
            "trace": str(trace_path),
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
                    "max_frames": args.max_frames,
                    "p1_name": args.p1_name,
                    "p2_name": args.p2_name,
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
