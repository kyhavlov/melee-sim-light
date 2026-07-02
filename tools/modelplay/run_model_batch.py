from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

from tools.modelplay.run_model_match import (
    _game_over,
    _parse_int_tuple,
    _player_static_signature,
    _static_signature,
)
from tools.modelplay.sim_env import BatchedSimSession
from tools.modelplay.slippi_ai_bridge import build_fused_batch_model_agent, stop_model_agent
from tools.modelplay.state_adapter import (
    MSL_STAGE_BATTLEFIELD,
    MSL_STAGE_DREAM_LAND_N64,
    MSL_STAGE_FINAL_DESTINATION,
    MSL_STAGE_FOUNTAIN_OF_DREAMS,
    MSL_STAGE_POKEMON_STADIUM,
    MSL_STAGE_YOSHIS_STORY,
    SimFrameState,
)
from tools.viewer.msltrace1 import MslTraceWriter


CHAR_IDS = {
    "fox": 1,
    "falco": 22,
}

STAGE_IDS = {
    "battlefield": MSL_STAGE_BATTLEFIELD,
    "dreamland": MSL_STAGE_DREAM_LAND_N64,
    "fd": MSL_STAGE_FINAL_DESTINATION,
    "fod": MSL_STAGE_FOUNTAIN_OF_DREAMS,
    "pokemon": MSL_STAGE_POKEMON_STADIUM,
    "yoshi": MSL_STAGE_YOSHIS_STORY,
}

STAGE_NAMES_BY_ID = {stage_id: name for name, stage_id in STAGE_IDS.items()}
LEGAL_STAGE_ORDER = ("fd", "battlefield", "pokemon", "yoshi", "dreamland", "fod")
SUPPORTED_MATCHUPS = (("fox", "fox"), ("fox", "falco"), ("falco", "falco"))


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="Run multiple slippi-ai 1v1 modelplay traces with fused batched inference."
    )
    ap.add_argument("--slippi-ai-root", type=Path, required=True)
    ap.add_argument("--model", type=Path, required=True, help="Shared checkpoint for both ports.")
    ap.add_argument("--num-traces", type=int, default=5)
    ap.add_argument("--max-frames", type=int, default=30000)
    ap.add_argument(
        "--static-frame-threshold",
        type=int,
        default=600,
        help="stop an env early if the full state signature repeats this many consecutive frames",
    )
    ap.add_argument("--out", type=Path, default=Path("reports/modelplay") / f"{_timestamp()}_modelplay_batch")
    ap.add_argument("--p1-char", choices=sorted(CHAR_IDS), default=None)
    ap.add_argument("--p2-char", choices=sorted(CHAR_IDS), default=None)
    ap.add_argument("--stage", choices=sorted(STAGE_IDS), default="fd")
    ap.add_argument(
        "--stages",
        default=None,
        help="Comma-separated stage names, one per trace. Example: battlefield,pokemon,yoshi,dreamland,fod",
    )
    ap.add_argument(
        "--matchups",
        default=None,
        help="Comma-separated p1-vs-p2 pairs, one per trace. Example: fox-fox,fox-falco,falco-falco",
    )
    ap.add_argument(
        "--legal-stage-matchup-matrix",
        action="store_true",
        help="Run every supported legal stage with fox-fox, fox-falco, and falco-falco in one fused batch.",
    )
    ap.add_argument("--team-ids", default=None, help="Internal debug hook; comma-separated 2-player team ids")
    return ap.parse_args()


def _parse_matchup(value: str) -> tuple[str, str]:
    parts = value.replace("_vs_", "-").replace("vs", "-").split("-")
    if len(parts) != 2:
        raise ValueError(f"matchup must look like fox-falco, got {value!r}")
    p1, p2 = (part.strip() for part in parts)
    if p1 not in CHAR_IDS or p2 not in CHAR_IDS:
        raise ValueError(f"unknown matchup {value!r}")
    return p1, p2


def _env_dir(out_dir: Path, env: int, label: str | None, state: SimFrameState) -> Path:
    if label is not None:
        return out_dir / f"env_{env:03d}_{label}"
    return out_dir / f"env_{env:03d}_{STAGE_NAMES_BY_ID.get(int(state.stage_id), state.stage_id)}"


def _termination_summary(
    *,
    state: SimFrameState,
    frames_run: int,
    termination_reason: str,
    static_failure: dict | None,
    player_static_failure: dict | None,
    trace_path: Path,
    trace_to_failure_path: Path | None,
) -> dict:
    return {
        "frames_run": int(frames_run),
        "final_frame_id": int(state.frame_id),
        "stage_id": int(state.stage_id),
        "stage": STAGE_NAMES_BY_ID.get(int(state.stage_id), str(int(state.stage_id))),
        "final_stocks": state.stocks[: state.num_players].tolist(),
        "final_percent": [float(x) for x in state.percent[: state.num_players]],
        "game_over": termination_reason == "game_over",
        "termination_reason": termination_reason,
        "static_failure": static_failure,
        "player_static_failure": player_static_failure,
        "trace": str(trace_path),
        "trace_to_failure": None if trace_to_failure_path is None else str(trace_to_failure_path),
    }


def main() -> int:
    args = parse_args()

    out_dir = args.out.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    matrix_specs: tuple[tuple[str, tuple[str, str]], ...] | None = None
    if args.legal_stage_matchup_matrix:
        if args.stages is not None or args.matchups is not None:
            raise ValueError("--legal-stage-matchup-matrix cannot be combined with --stages or --matchups")
        matrix_specs = tuple((stage, matchup) for stage in LEGAL_STAGE_ORDER for matchup in SUPPORTED_MATCHUPS)
        args.num_traces = len(matrix_specs)

    if args.num_traces <= 0:
        raise ValueError(f"--num-traces must be positive, got {args.num_traces}")

    char_ids = None
    if args.p1_char is not None or args.p2_char is not None:
        char_ids = (
            CHAR_IDS[args.p1_char or "falco"],
            CHAR_IDS[args.p2_char or "fox"],
        )
    team_ids = None if args.team_ids is None else _parse_int_tuple(args.team_ids, expected_len=2, name="--team-ids")
    stage_ids = None
    matchup_names = None
    char_ids_by_env = None
    env_labels: list[str | None] = [None for _ in range(args.num_traces)]
    if matrix_specs is not None:
        stage_names = tuple(stage for stage, _matchup in matrix_specs)
        matchup_names = tuple(matchup for _stage, matchup in matrix_specs)
        stage_ids = tuple(STAGE_IDS[name] for name in stage_names)
        char_ids_by_env = tuple(tuple(CHAR_IDS[name] for name in matchup) for matchup in matchup_names)
        env_labels = [
            f"{stage}_{p1}_vs_{p2}"
            for stage, (p1, p2) in matrix_specs
        ]
    elif args.stages is not None:
        stage_names = tuple(part.strip() for part in args.stages.split(",") if part.strip())
        unknown = [name for name in stage_names if name not in STAGE_IDS]
        if unknown:
            raise ValueError(f"unknown stage names: {', '.join(unknown)}")
        if len(stage_names) != args.num_traces:
            raise ValueError(f"--stages has {len(stage_names)} entries but --num-traces is {args.num_traces}")
        stage_ids = tuple(STAGE_IDS[name] for name in stage_names)
    if args.matchups is not None:
        matchup_names = tuple(_parse_matchup(part.strip()) for part in args.matchups.split(",") if part.strip())
        if len(matchup_names) != args.num_traces:
            raise ValueError(f"--matchups has {len(matchup_names)} entries but --num-traces is {args.num_traces}")
        char_ids_by_env = tuple(tuple(CHAR_IDS[name] for name in matchup) for matchup in matchup_names)
        char_ids = None
        for env, (p1, p2) in enumerate(matchup_names):
            prefix = env_labels[env] + "_" if env_labels[env] is not None else ""
            env_labels[env] = f"{prefix}{p1}_vs_{p2}"
    stage_id = STAGE_IDS[args.stage]

    session = BatchedSimSession(
        batch_size=args.num_traces,
        start_record=0,
        char_ids=char_ids,
        team_ids=team_ids,
        is_teams=False,
        start_mode="sim-init",
        stage_id=stage_id,
        stage_ids=stage_ids,
        char_ids_by_env=char_ids_by_env,
    )
    # Port-major order mirrors slippi_ai.evaluators fused inference: all envs for port 1,
    # then all envs for port 2. One DelayedAgent owns recurrent state for every lane.
    agent = build_fused_batch_model_agent(
        slippi_ai_root=args.slippi_ai_root,
        model_path=args.model,
        batch_size=args.num_traces * 2,
        name=[f"P{port}-E{env:03d}" for port in (1, 2) for env in range(args.num_traces)],
    )

    traces = [
        MslTraceWriter(
            metadata={"model": {"runner": "tools.modelplay.run_model_batch"}},
            match_start=session.trace_start_info(env),
        )
        for env in range(args.num_traces)
    ]
    done = np.zeros(args.num_traces, dtype=np.bool_)
    frames_run = np.zeros(args.num_traces, dtype=np.int32)
    summaries: list[dict | None] = [None for _ in range(args.num_traces)]

    try:
        env_out = session.reset()
        for env, trace in enumerate(traces):
            trace.add_frame(session.current_frame_states[env], session.last_controllers[env])

        prev_signatures = [_static_signature(state) for state in session.current_frame_states]
        static_repeat_counts = [0 for _ in range(args.num_traces)]
        static_start_frames = [0 for _ in range(args.num_traces)]
        static_failures: list[dict | None] = [None for _ in range(args.num_traces)]
        global_change_counts = [0 for _ in range(args.num_traces)]
        player_prev_signatures = [
            [_player_static_signature(state, idx) for idx in range(state.num_players)]
            for state in session.current_frame_states
        ]
        player_static_repeat_counts = [
            [0 for _ in range(state.num_players)]
            for state in session.current_frame_states
        ]
        player_static_start_frames = [
            [0 for _ in range(state.num_players)]
            for state in session.current_frame_states
        ]
        player_static_start_global_changes = [
            [0 for _ in range(state.num_players)]
            for state in session.current_frame_states
        ]
        player_static_failures: list[dict | None] = [None for _ in range(args.num_traces)]

        for _step in range(args.max_frames):
            if bool(np.all(done)):
                break

            games = [
                env_out.gamestates[env][port]
                for port in (1, 2)
                for env in range(args.num_traces)
            ]
            needs_reset = np.concatenate([env_out.needs_reset, env_out.needs_reset], axis=0)
            flat_controllers = agent.step(games, needs_reset=needs_reset)
            controllers = [
                {
                    1: flat_controllers[env],
                    2: flat_controllers[args.num_traces + env],
                }
                for env in range(args.num_traces)
            ]
            for env in range(args.num_traces):
                if bool(done[env]):
                    controllers[env] = {}

            env_out = session.step(controllers)

            for env, state in enumerate(session.current_frame_states):
                if bool(done[env]):
                    continue

                trace = traces[env]
                trace.add_frame(state, session.last_controllers[env])
                frames_run[env] += 1
                current_frame = trace.frame_count - 1

                signature = _static_signature(state)
                if signature == prev_signatures[env]:
                    static_repeat_counts[env] += 1
                else:
                    prev_signatures[env] = signature
                    static_repeat_counts[env] = 0
                    static_start_frames[env] = current_frame
                    global_change_counts[env] += 1

                for idx in range(state.num_players):
                    player_signature = _player_static_signature(state, idx)
                    if player_signature == player_prev_signatures[env][idx]:
                        player_static_repeat_counts[env][idx] += 1
                    else:
                        player_prev_signatures[env][idx] = player_signature
                        player_static_repeat_counts[env][idx] = 0
                        player_static_start_frames[env][idx] = current_frame
                        player_static_start_global_changes[env][idx] = global_change_counts[env]

                termination_reason = None
                if _game_over(state):
                    termination_reason = "game_over"
                elif static_repeat_counts[env] >= args.static_frame_threshold:
                    static_failures[env] = {
                        "start_frame": static_start_frames[env],
                        "end_frame": current_frame,
                        "repeat_count": static_repeat_counts[env] + 1,
                        "action_ids": [int(x) for x in state.action_id[: state.num_players]],
                        "stocks": [int(x) for x in state.stocks[: state.num_players]],
                        "percent": [float(x) for x in state.percent[: state.num_players]],
                    }
                    termination_reason = "static_failure"
                else:
                    for idx in range(state.num_players):
                        if player_static_repeat_counts[env][idx] < args.static_frame_threshold:
                            continue
                        if global_change_counts[env] <= player_static_start_global_changes[env][idx]:
                            continue
                        player_static_failures[env] = {
                            "player_index": idx,
                            "start_frame": player_static_start_frames[env][idx],
                            "end_frame": current_frame,
                            "repeat_count": player_static_repeat_counts[env][idx] + 1,
                            "player_action_id": int(state.action_id[idx]),
                            "player_stocks": int(state.stocks[idx]),
                            "player_percent": float(state.percent[idx]),
                            "all_action_ids": [int(x) for x in state.action_id[: state.num_players]],
                            "all_stocks": [int(x) for x in state.stocks[: state.num_players]],
                            "all_percent": [float(x) for x in state.percent[: state.num_players]],
                        }
                        termination_reason = "player_static_failure"
                        break

                if termination_reason is None:
                    continue

                env_dir = _env_dir(out_dir, env, env_labels[env], state)
                trace_path = env_dir / "trace.msltrace.json"
                trace.write_json(trace_path)
                trace_to_failure_path = None
                if static_failures[env] is not None:
                    trace_to_failure_path = env_dir / "trace_to_failure.msltrace.json"
                    trace.write_json(trace_to_failure_path, frame_limit=static_failures[env]["start_frame"] + 1)
                elif player_static_failures[env] is not None:
                    trace_to_failure_path = env_dir / "trace_to_failure.msltrace.json"
                    trace.write_json(
                        trace_to_failure_path,
                        frame_limit=player_static_failures[env]["start_frame"] + 1,
                    )
                summaries[env] = _termination_summary(
                    state=state,
                    frames_run=int(frames_run[env]),
                    termination_reason=termination_reason,
                    static_failure=static_failures[env],
                    player_static_failure=player_static_failures[env],
                    trace_path=trace_path,
                    trace_to_failure_path=trace_to_failure_path,
                )
                (env_dir / "summary.txt").write_text(json.dumps(summaries[env], indent=2) + "\n")
                done[env] = True

        for env, state in enumerate(session.current_frame_states):
            if summaries[env] is not None:
                continue
            env_dir = _env_dir(out_dir, env, env_labels[env], state)
            trace_path = env_dir / "trace.msltrace.json"
            traces[env].write_json(trace_path)
            summaries[env] = _termination_summary(
                state=state,
                frames_run=int(frames_run[env]),
                termination_reason="max_frames",
                static_failure=static_failures[env],
                player_static_failure=player_static_failures[env],
                trace_path=trace_path,
                trace_to_failure_path=None,
            )
            (env_dir / "summary.txt").write_text(json.dumps(summaries[env], indent=2) + "\n")

        aggregate = {
            "slippi_ai_root": str(args.slippi_ai_root),
            "model": str(args.model),
            "start_mode": "sim-init",
            "stage": args.stage,
            "stage_ids": None if stage_ids is None else list(stage_ids),
            "matchups": None if matchup_names is None else [list(matchup) for matchup in matchup_names],
            "num_traces": int(args.num_traces),
            "max_frames": int(args.max_frames),
            "static_frame_threshold": int(args.static_frame_threshold),
            "p1_char": args.p1_char,
            "p2_char": args.p2_char,
            "summaries": summaries,
        }
        (out_dir / "summary.txt").write_text(json.dumps(aggregate, indent=2) + "\n")
        (out_dir / "config.json").write_text(json.dumps(aggregate, indent=2) + "\n")
        print(f"wrote batch traces: {out_dir}")
        print(json.dumps(aggregate, indent=2))
        return 0
    finally:
        stop_model_agent(agent)
        session.close()


if __name__ == "__main__":
    raise SystemExit(main())
