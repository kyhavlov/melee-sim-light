from __future__ import annotations

import argparse
import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

from tools.modelplay.puffer_bridge import IdleAgent, PufferAgent, RandomAgent
from tools.modelplay.sim_env import CHAR_FALCO, CHAR_FOX, SimSession
from tools.modelplay.viewer_trace import ViewerTrace


CHAR_IDS = {
    "fox": CHAR_FOX,
    "falco": CHAR_FALCO,
}


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Run a Puffer checkpoint inside melee-sim-light modelplay.")
    ap.add_argument("--ssbm-puffer-root", type=Path, required=True)
    ap.add_argument("--checkpoint", type=Path, required=True)
    ap.add_argument(
        "--slippi-ai-root",
        type=Path,
        default=Path("/media/kyle/Windows/Users/kyleh/git/slippi-ai"),
    )
    ap.add_argument("--p2-checkpoint", type=Path, default=None)
    ap.add_argument("--p2-mode", choices=("checkpoint", "random", "idle"), default="random")
    ap.add_argument("--start-mode", choices=("sim-init",), default="sim-init")
    ap.add_argument("--max-frames", type=int, default=1200)
    ap.add_argument("--stocks", type=int, default=1)
    ap.add_argument("--out", type=Path, default=Path("reports/modelplay") / f"{_timestamp()}_puffer")
    ap.add_argument("--p1-char", choices=sorted(CHAR_IDS), default="fox")
    ap.add_argument("--p2-char", choices=sorted(CHAR_IDS), default="fox")
    ap.add_argument("--hidden-size", type=int, default=256)
    ap.add_argument("--num-layers", type=int, default=4)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--sample-actions", action=argparse.BooleanOptionalAction, default=True)
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[2]
    main_repo_data = (repo_root.parent / "melee-sim-light" / "data").resolve()
    local_data = (repo_root / "data").resolve()
    if "MSL_DATA_DIR" not in os.environ:
        os.environ["MSL_DATA_DIR"] = str(main_repo_data if main_repo_data.exists() else local_data)
    slippi_ai_root = args.slippi_ai_root.resolve()
    if str(slippi_ai_root) not in sys.path:
        sys.path.insert(0, str(slippi_ai_root))
    out_dir = args.out.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    char_ids = (CHAR_IDS[args.p1_char], CHAR_IDS[args.p2_char])
    session = SimSession(
        dataset_path=None,
        char_ids=char_ids,
        start_mode=args.start_mode,
        stocks=args.stocks,
        frame_id=0,
        random_seed=args.seed,
    )
    p1 = PufferAgent(
        ssbm_puffer_root=args.ssbm_puffer_root,
        checkpoint=args.checkpoint,
        hidden_size=args.hidden_size,
        num_layers=args.num_layers,
        stochastic=args.sample_actions,
        seed=args.seed + 101,
    )
    if args.p2_mode == "checkpoint":
        p2 = PufferAgent(
            ssbm_puffer_root=args.ssbm_puffer_root,
            checkpoint=args.p2_checkpoint or args.checkpoint,
            hidden_size=args.hidden_size,
            num_layers=args.num_layers,
            stochastic=args.sample_actions,
            seed=args.seed + 202,
        )
    elif args.p2_mode == "idle":
        p2 = IdleAgent()
    else:
        p2 = RandomAgent(np.random.default_rng(args.seed + 1))

    trace = ViewerTrace()
    try:
        env_out = session.reset()
        trace.add_frame(session.current_frame_state, session.last_controllers)

        frames_run = 0
        while frames_run < args.max_frames:
            p1_obs = session.current_rl_observation(1)
            p2_obs = session.current_rl_observation(2)
            p1_controller = p1.step(p1_obs, needs_reset=env_out.needs_reset)
            p2_controller = (
                p2.step(rl_obs=p2_obs, needs_reset=env_out.needs_reset)
                if isinstance(p2, PufferAgent)
                else p2.step(needs_reset=env_out.needs_reset)
            )
            env_out = session.step({1: p1_controller, 2: p2_controller})
            trace.add_frame(session.current_frame_state, session.last_controllers)
            frames_run += 1
            state = session.current_frame_state
            if int(state.stocks[0]) == 0 or int(state.stocks[1]) == 0:
                break

        trace_path = out_dir / "trace.json"
        trace.write_json(trace_path)
        summary = {
            "checkpoint": str(args.checkpoint),
            "p2_mode": args.p2_mode,
            "p2_checkpoint": None if args.p2_checkpoint is None else str(args.p2_checkpoint),
            "start_mode": args.start_mode,
            "frames_run": frames_run,
            "final_frame_id": session.current_frame_state.frame_id,
            "final_stocks": session.current_frame_state.stocks[:2].tolist(),
            "final_percent": [float(x) for x in session.current_frame_state.percent[:2]],
            "trace": str(trace_path),
        }
        (out_dir / "summary.txt").write_text(json.dumps(summary, indent=2) + "\n")
        (out_dir / "config.json").write_text(
            json.dumps(
                {
                    "ssbm_puffer_root": str(args.ssbm_puffer_root),
                    "checkpoint": str(args.checkpoint),
                    "slippi_ai_root": str(args.slippi_ai_root),
                    "p2_checkpoint": None if args.p2_checkpoint is None else str(args.p2_checkpoint),
                    "p2_mode": args.p2_mode,
                    "start_mode": args.start_mode,
                    "max_frames": args.max_frames,
                    "stocks": args.stocks,
                    "p1_char": args.p1_char,
                    "p2_char": args.p2_char,
                    "hidden_size": args.hidden_size,
                    "num_layers": args.num_layers,
                    "seed": args.seed,
                    "sample_actions": args.sample_actions,
                },
                indent=2,
            )
            + "\n"
        )
        print(f"wrote trace: {trace_path}")
        print(json.dumps(summary, indent=2))
        return 0
    finally:
        p1.close()
        if isinstance(p2, PufferAgent):
            p2.close()
        session.close()


if __name__ == "__main__":
    raise SystemExit(main())
