from __future__ import annotations

import argparse
import time

import numpy as np

import melee_sim as msl

SUPPORTED_STAGES = {
    "fd": 32,
    "battlefield": 31,
    "pokemon": 3,
    "yoshi": 8,
    "dreamland": 28,
    "fod": 2,
}
DEFAULT_STAGES = [32, 31, 3, 8, 28, 2]

MSL_BUTTON_A = 0x0100
MSL_BUTTON_B = 0x0200
MSL_BUTTON_X = 0x0400
MSL_BUTTON_Y = 0x0800
MSL_BUTTON_Z = 0x0010
MSL_BUTTON_L = 0x0040
MSL_BUTTON_R = 0x0020


def _xorshift32(state: int) -> int:
    x = state & 0xFFFFFFFF
    x ^= (x << 13) & 0xFFFFFFFF
    x ^= x >> 17
    x ^= (x << 5) & 0xFFFFFFFF
    x &= 0xFFFFFFFF
    return x if x else 0x9E3779B9


def _stick_axis_from_rng(r: int) -> int:
    v = (r & 0xFF) - 128
    return np.int8(v).item()


def _random_buttons(r: int) -> int:
    buttons = 0
    if r & (1 << 0):
        buttons |= MSL_BUTTON_A
    if r & (1 << 3):
        buttons |= MSL_BUTTON_B
    if r & (1 << 6):
        buttons |= MSL_BUTTON_X
    if r & (1 << 9):
        buttons |= MSL_BUTTON_Y
    if r & (1 << 12):
        buttons |= MSL_BUTTON_Z
    if r & (1 << 15):
        buttons |= MSL_BUTTON_L
    if r & (1 << 18):
        buttons |= MSL_BUTTON_R
    return buttons


def _parse_stages(value: str) -> list[int]:
    if value == "all":
        return list(DEFAULT_STAGES)
    out: list[int] = []
    for token in value.split(","):
        if token in SUPPORTED_STAGES:
            out.append(SUPPORTED_STAGES[token])
        else:
            out.append(int(token))
    if not out:
        raise ValueError("stage list cannot be empty")
    return out


def fill_match_configs(env: msl.EnvBatch, buffers: msl.Buffers, stages: list[int]) -> None:
    env.configure_matches(
        buffers,
        [
            msl.MatchConfig(
                stage=stages[bi % len(stages)],
                players=(
                    msl.PlayerConfig(character=msl.Character.FOX),
                    msl.PlayerConfig(character=msl.Character.FALCO),
                ),
                frame_pre_random_seed=(0x319E1C7D ^ (bi * 0x9E3779B9)) & 0xFFFFFFFF,
            )
            for bi in range(buffers.batch_size)
        ],
    )


def fill_raw_actions(buffers: msl.Buffers) -> None:
    if buffers.action_format != "raw":
        raise ValueError("fill_rollout_actions expects action_format='raw'")
    action = buffers.action_view
    action[...] = 0
    rng = 0xC0FFEE11
    for frame in range(buffers.length):
        for bi in range(buffers.batch_size):
            for p in range(2):
                r0 = _xorshift32(rng)
                rng = r0
                r1 = _xorshift32(rng)
                rng = r1
                r2 = _xorshift32(rng)
                rng = r2
                action["p"]["buttons"][frame, bi, p] = _random_buttons(r0)
                action["p"]["main_x"][frame, bi, p] = _stick_axis_from_rng(r1)
                action["p"]["main_y"][frame, bi, p] = _stick_axis_from_rng(r1 >> 8)
                action["p"]["c_x"][frame, bi, p] = _stick_axis_from_rng(r2)
                action["p"]["c_y"][frame, bi, p] = _stick_axis_from_rng(r2 >> 8)
                action["p"]["l"][frame, bi, p] = (r1 >> 16) & 0xFF
                action["p"]["r"][frame, bi, p] = (r2 >> 16) & 0xFF


def fill_controller_actions(buffers: msl.Buffers) -> None:
    if buffers.action_format != "controller":
        raise ValueError("fill_controller_actions expects action_format='controller'")
    action = buffers.controller_action_view
    action[...] = 0
    rng = 0xC0FFEE11
    for frame in range(buffers.length):
        for bi in range(buffers.batch_size):
            for p in range(2):
                r0 = _xorshift32(rng)
                rng = r0
                r1 = _xorshift32(rng)
                rng = r1
                r2 = _xorshift32(rng)
                rng = r2
                player = action["p"][frame, bi, p]
                player["buttons"]["A"] = 1 if r0 & (1 << 0) else 0
                player["buttons"]["B"] = 1 if r0 & (1 << 3) else 0
                player["buttons"]["X"] = 1 if r0 & (1 << 6) else 0
                player["buttons"]["Y"] = 1 if r0 & (1 << 9) else 0
                player["buttons"]["Z"] = 1 if r0 & (1 << 12) else 0
                player["buttons"]["L"] = 1 if r0 & (1 << 15) else 0
                player["buttons"]["R"] = 1 if r0 & (1 << 18) else 0
                player["main_stick_x"] = ((r1 & 0xFF) % 161) / 160.0
                player["main_stick_y"] = (((r1 >> 8) & 0xFF) % 161) / 160.0
                player["c_stick_x"] = ((r2 & 0xFF) % 161) / 160.0
                player["c_stick_y"] = (((r2 >> 8) & 0xFF) % 161) / 160.0
                player["shoulder"] = (((r1 >> 16) & 0xFF) % 141) / 140.0


def _mix(h: int, v: int) -> int:
    h ^= v & 0xFFFFFFFFFFFFFFFF
    h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def checksum_compare(compare: np.ndarray) -> int:
    rows = compare
    h = 1469598103934665603
    for row in rows:
        h = _mix(h, int(np.uint32(row["frame_id"])))
        h = _mix(h, int(row["frame_pre_random_seed"]))
        h = _mix(h, int(row["stage_id"]))
        h = _mix(h, int(row["num_players"]))
        for p in range(4):
            h = _mix(h, int(row["action_id"][p]))
            h = _mix(h, int(np.uint16(row["action_frame"][p])))
            h = _mix(h, int(row["animation_index"][p]))
            h = _mix(h, int(row["stocks"][p]))
    return h


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--batch", type=int, default=1024)
    parser.add_argument("--frames", type=int, default=5000)
    parser.add_argument("--warmup", type=int, default=2000)
    parser.add_argument("--length", type=int, default=256)
    parser.add_argument("--stages", default="all")
    parser.add_argument("--api", choices=("buffers",), default="buffers")
    parser.add_argument("--action-format", choices=("raw", "controller"), default="raw")
    parser.add_argument("--mode", choices=("step", "step_compare"), default="step")
    parser.add_argument("--write-outputs", action="store_true")
    parser.add_argument("--neutral-initial-prev", action="store_true")
    args = parser.parse_args()

    stages = _parse_stages(args.stages)
    buffers = msl.Buffers.empty(
        args.length,
        args.batch,
        num_players=2,
        action_format=args.action_format,
    )
    with msl.EnvBatch(args.batch, length=args.length, num_players=2) as env:
        fill_match_configs(env, buffers, stages)
        if args.action_format == "raw":
            fill_raw_actions(buffers)
        else:
            fill_controller_actions(buffers)

        env.bind(buffers)
        env.reset_all()
        if not args.neutral_initial_prev:
            env.set_previous_input(0)
        for frame in range(args.warmup):
            env._step_at(
                frame % args.length,
                write_outputs=args.write_outputs,
                write_compare=args.mode == "step_compare",
            )

        env.reset_all()
        if not args.neutral_initial_prev:
            env.set_previous_input(0)
        start = time.perf_counter_ns()
        for frame in range(args.frames):
            env._step_at(
                frame % args.length,
                write_outputs=args.write_outputs,
                write_compare=args.mode == "step_compare",
            )
        elapsed_ns = time.perf_counter_ns() - start
        env.write_compare()

    compare_view = buffers.compare_view
    input_ring_label = str(args.length)

    env_steps = args.frames * args.batch
    seconds = elapsed_ns / 1_000_000_000.0
    env_steps_per_sec = env_steps / seconds
    ns_per_env_step = elapsed_ns / env_steps
    checksum = checksum_compare(compare_view)

    print(
        "mode=env_batch_{}_{}_{} batch={} frames={} warmup={} input_ring={} stages={}".format(
            args.api,
            args.action_format,
            args.mode,
            args.batch,
            args.frames,
            args.warmup,
            input_ring_label,
            ",".join(str(s) for s in stages),
        )
    )
    print(
        "elapsed_sec={:.9f} env_steps={} env_steps_per_sec={:.3f} ns_per_env_step={:.3f}".format(
            seconds, env_steps, env_steps_per_sec, ns_per_env_step
        )
    )
    print(f"checksum={checksum}")


if __name__ == "__main__":
    main()
