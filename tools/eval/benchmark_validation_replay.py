from __future__ import annotations

import argparse
import cProfile
import contextlib
import json
import pstats
import time
from collections import defaultdict
from dataclasses import asdict
from pathlib import Path
from types import SimpleNamespace
from typing import Callable

from tools.eval.run_longest_rollout_streaks import (
    _parse_players,
    _scan_dataset_streaks,
    _validate_discrete_fields,
)
from tools.eval.run_one_step_eval import (
    Reporter,
    create_one_step_eval_runtime,
    evaluate_dataset,
)
from tools.eval.validation_profile import get_validation_profile
from tools.slippi.slpz import resolve_replay_path
from tools.slippi.suite_io import load_suite, repo_root


DEFAULT_CASES = (
    ("normal_fd", "replays/suites/fox_falco_fd_ucf084_recent.json", "AttachedGoodNaturedGuanaco"),
    ("sheik", "replays/suites/sheik.json", "StiffLustrousZebra"),
    ("dream_land", "replays/suites/dream_land_recent.json", "FlippantEnchantedHorse"),
)


class CaptureReporter:
    def __init__(self) -> None:
        self.lines: list[str] = []

    def print(self, *args) -> None:
        self.lines.append(" ".join(str(a) for a in args))

    def close(self) -> None:
        return None


class PhaseTimer:
    def __init__(self) -> None:
        self.wall: dict[str, float] = defaultdict(float)
        self.cpu: dict[str, float] = defaultdict(float)
        self._stack: list[dict[str, float]] = []

    @contextlib.contextmanager
    def phase(self, name: str):
        frame = {"wall_child": 0.0, "cpu_child": 0.0}
        self._stack.append(frame)
        wall0 = time.perf_counter()
        cpu0 = time.process_time()
        try:
            yield
        finally:
            wall = time.perf_counter() - wall0
            cpu = time.process_time() - cpu0
            popped = self._stack.pop()
            self.wall[name] += max(0.0, wall - popped["wall_child"])
            self.cpu[name] += max(0.0, cpu - popped["cpu_child"])
            if self._stack:
                self._stack[-1]["wall_child"] += wall
                self._stack[-1]["cpu_child"] += cpu

    def wrap(self, name: str, fn: Callable) -> Callable:
        def inner(*args, **kwargs):
            with self.phase(name):
                return fn(*args, **kwargs)

        inner.__name__ = getattr(fn, "__name__", "wrapped")
        inner.__doc__ = getattr(fn, "__doc__", None)
        return inner


@contextlib.contextmanager
def _patched_dataset_timers(timer: PhaseTimer):
    import tools.slippi.anim_timebase as anim_timebase
    import tools.slippi.combat_history as combat_history
    import tools.slippi.combo_history as combo_history
    import tools.slippi.damage_history as damage_history
    import tools.slippi.make_dataset_from_slp as msl
    import tools.slippi.seed_history as seed_history
    import tools.slippi.staling_history as staling_history

    patches: list[tuple[object, str, object]] = []

    def patch(obj, name: str, category: str) -> None:
        if not hasattr(obj, name):
            return
        old = getattr(obj, name)
        if not callable(old):
            return
        patches.append((obj, name, old))
        setattr(obj, name, timer.wrap(category, old))

    orig_replay_path_for_peppi = msl.replay_path_for_peppi

    @contextlib.contextmanager
    def timed_replay_path_for_peppi(path):
        cm = orig_replay_path_for_peppi(path)
        with timer.phase("replay_parse_decompress"):
            peppi_path = cm.__enter__()
        try:
            yield peppi_path
        finally:
            with timer.phase("replay_parse_decompress"):
                cm.__exit__(None, None, None)

    patches.append((msl, "replay_path_for_peppi", orig_replay_path_for_peppi))
    msl.replay_path_for_peppi = timed_replay_path_for_peppi
    patch(msl, "_read_slippi", "replay_parse_peppi")
    patch(msl, "_to_numpy", "arrow_numpy_extract")
    patch(msl, "_fill_items_fixed", "item_fixed_slot_materialization")

    for name in dir(msl):
        if name in {"_read_slippi", "_to_numpy", "_fill_items_fixed"}:
            continue
        if name.startswith(("read_", "load_")):
            patch(msl, name, "data_load")
        elif name.startswith(("_derive_", "derive_", "_fill_")):
            patch(msl, name, "hidden_lane_derivation")

    for mod in (seed_history, combat_history, staling_history, combo_history, damage_history, anim_timebase):
        for name in dir(mod):
            if name.startswith(("derive_", "compute_", "process_")):
                patch(mod, name, "hidden_lane_derivation")
            elif name.startswith(("read_", "load_")):
                patch(mod, name, "data_load")

    try:
        yield
    finally:
        for obj, name, old in reversed(patches):
            setattr(obj, name, old)


def _select_case(case: tuple[str, str, str]) -> dict:
    root = repo_root()
    label, suite_rel, replay_key = case
    suite = load_suite(root / suite_rel)
    for entry in suite.replays:
        replay = str(entry.replay)
        if replay_key in Path(replay).stem:
            return {
                "label": label,
                "suite": suite_rel,
                "suite_name": suite.name,
                "replay": str(resolve_replay_path((root / replay).resolve())),
                "ports": [int(p) for p in entry.ports],
                "ucf_enabled": bool(suite.ucf_enabled),
                "ucf_cardinals_1_0_enabled": bool(suite.ucf_cardinals_1_0_enabled),
            }
    raise SystemExit(f"case replay {replay_key!r} not found in {suite_rel}")


def _profile_top(profile: cProfile.Profile, *, limit: int) -> list[dict]:
    stats = pstats.Stats(profile)
    root = repo_root().resolve()
    rows = []
    for (filename, line, func), data in stats.stats.items():
        cc, nc, tt, ct, _callers = data
        profile_file = filename
        path = Path(filename)
        if path.is_absolute():
            try:
                profile_file = str(path.resolve().relative_to(root))
            except ValueError:
                continue
        elif not filename.startswith(("bindings/", "melee_sim/", "src/", "tools/")):
            continue
        rows.append(
            {
                "file": profile_file,
                "line": int(line),
                "function": func,
                "calls": int(nc),
                "primitive_calls": int(cc),
                "tottime_s": float(tt),
                "cumtime_s": float(ct),
            }
        )
    rows.sort(key=lambda r: (-r["tottime_s"], -r["cumtime_s"], r["file"], r["line"]))
    return rows[:limit]


def _native_profile_times(profile: cProfile.Profile) -> dict[str, float]:
    stats = pstats.Stats(profile)
    out: dict[str, float] = defaultdict(float)
    names = {
        "reseed_seed": "one_step_native_reseed",
        "step_input": "one_step_native_step",
        "write_compare": "native_write_compare",
        "reseed_seed_rollout": "rollout_native_reseed",
        "step_input_replay_frame_rng": "rollout_native_step",
    }
    for (_filename, _line, func), data in stats.stats.items():
        if func in names:
            out[names[func]] += float(data[2])
    return dict(out)


@contextlib.contextmanager
def _patched_native_timers(timer: PhaseTimer, mapping: dict[str, str]):
    import msl_binding  # type: ignore

    patches: list[tuple[str, object]] = []
    for name, category in mapping.items():
        if not hasattr(msl_binding, name):
            continue
        old = getattr(msl_binding, name)
        if not callable(old):
            continue
        patches.append((name, old))
        setattr(msl_binding, name, timer.wrap(category, old))
    try:
        yield
    finally:
        for name, old in reversed(patches):
            setattr(msl_binding, name, old)


def _dataset_build(case: dict) -> tuple[object, dict]:
    from tools.slippi.make_dataset_from_slp import build_dataset_from_slp

    timer = PhaseTimer()
    profile = cProfile.Profile()
    wall0 = time.perf_counter()
    cpu0 = time.process_time()
    with _patched_dataset_timers(timer):
        profile.enable()
        dataset = build_dataset_from_slp(
            slp_path=str(case["replay"]),
            ports=list(case["ports"]),
            ucf_enabled=bool(case["ucf_enabled"]),
            ucf_cardinals_1_0_enabled=bool(case["ucf_cardinals_1_0_enabled"]),
        )
        profile.disable()
    wall = time.perf_counter() - wall0
    cpu = time.process_time() - cpu0
    known = sum(timer.wall.values())
    return dataset, {
        "wall_s": wall,
        "cpu_s": cpu,
        "records": int(dataset.header["num_records"]),
        "num_players": int(dataset.header["num_players"]),
        "phase_wall_s": dict(sorted(timer.wall.items())),
        "phase_cpu_s": dict(sorted(timer.cpu.items())),
        "dataset_other_wall_s": max(0.0, wall - known),
        "profile_top_tottime": _profile_top(profile, limit=25),
    }


def _one_step(case: dict, dataset, *, chunk: int, profile_name: str) -> dict:
    runtime = create_one_step_eval_runtime(
        batch_size=min(max(1, chunk), max(1, int(dataset.header["num_records"]))),
        num_players=int(dataset.header["num_players"]),
        ucf_enabled=bool(case["ucf_enabled"]),
        ucf_cardinals_1_0_enabled=bool(case["ucf_cardinals_1_0_enabled"]),
    )
    capture = CaptureReporter()
    timer = PhaseTimer()
    profile = cProfile.Profile()
    wall0 = time.perf_counter()
    cpu0 = time.process_time()
    try:
        with _patched_native_timers(
            timer,
            {
                "reseed_seed": "one_step_native_reseed",
                "step_input": "one_step_native_step",
                "write_compare": "one_step_native_write_compare",
            },
        ):
            profile.enable()
            summary = evaluate_dataset(
                dataset_path=Path(case["replay"]),
                dataset=dataset,
                chunk=chunk,
                runtime=runtime,
                profile=profile_name,
                ucf_enabled=bool(case["ucf_enabled"]),
                ucf_cardinals_1_0_enabled=bool(case["ucf_cardinals_1_0_enabled"]),
                reporter=capture,  # type: ignore[arg-type]
                print_profile=False,
            )
            profile.disable()
    finally:
        runtime.close()
    wall = time.perf_counter() - wall0
    cpu = time.process_time() - cpu0
    fmt0 = time.perf_counter()
    _ = "\n".join(capture.lines) + "\n" + json.dumps(asdict(summary), sort_keys=True)
    fmt_wall = time.perf_counter() - fmt0
    native = _native_profile_times(profile)
    return {
        "wall_s": wall,
        "cpu_s": cpu,
        "report_format_wall_s": fmt_wall,
        "phase_wall_s": dict(sorted(timer.wall.items())),
        "phase_cpu_s": dict(sorted(timer.cpu.items())),
        "python_compare_other_wall_s": max(0.0, wall - sum(timer.wall.values())),
        "native_profile_tottime_s": native,
        "summary": asdict(summary),
        "profile_top_tottime": _profile_top(profile, limit=25),
    }


def _rollout(case: dict, dataset, *, fields: tuple[str, ...], profile_name: str) -> dict:
    players = _parse_players(None, num_players=int(dataset.header["num_players"]))
    profile_obj = get_validation_profile(profile_name)
    timer = PhaseTimer()
    profile = cProfile.Profile()
    wall0 = time.perf_counter()
    cpu0 = time.process_time()
    with _patched_native_timers(
        timer,
        {
            "reseed_seed_rollout": "rollout_native_reseed",
            "step_input_replay_frame_rng": "rollout_native_step",
            "write_compare": "rollout_native_write_compare",
        },
    ):
        profile.enable()
        result = _scan_dataset_streaks(
            dataset_path=Path(case["replay"]),
            ds=dataset,
            fields=fields,
            players=players,
            max_records=0,
            ucf_enabled=bool(case["ucf_enabled"]),
            ucf_cardinals_1_0_enabled=bool(case["ucf_cardinals_1_0_enabled"]),
            profile=profile_obj,
        )
        profile.disable()
    wall = time.perf_counter() - wall0
    cpu = time.process_time() - cpu0
    fmt0 = time.perf_counter()
    _ = json.dumps(asdict(result), sort_keys=True)
    fmt_wall = time.perf_counter() - fmt0
    native = _native_profile_times(profile)
    return {
        "wall_s": wall,
        "cpu_s": cpu,
        "report_format_wall_s": fmt_wall,
        "phase_wall_s": dict(sorted(timer.wall.items())),
        "phase_cpu_s": dict(sorted(timer.cpu.items())),
        "python_scan_other_wall_s": max(0.0, wall - sum(timer.wall.values())),
        "native_profile_tottime_s": native,
        "summary": asdict(result),
        "profile_top_tottime": _profile_top(profile, limit=25),
    }


def main() -> None:
    ap = argparse.ArgumentParser(description="One-replay validation performance benchmark.")
    ap.add_argument("--out-dir", type=Path, default=Path("reports/triage/validation_perf"))
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument(
        "--fields",
        default="action_id,animation_index,on_ground,hitlag,hitstun,state_flags",
    )
    ap.add_argument("--profile", default="rl1_gameplay")
    ap.add_argument("--case", action="append", default=[])
    args = ap.parse_args()

    fields = _validate_discrete_fields(tuple(x.strip() for x in args.fields.split(",") if x.strip()))
    cases = []
    if args.case:
        for raw in args.case:
            label, suite, replay_key = raw.split(":", 2)
            cases.append((label, suite, replay_key))
    else:
        cases = list(DEFAULT_CASES)

    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    for case_def in cases:
        case = _select_case(case_def)
        dataset, build = _dataset_build(case)
        one = _one_step(case, dataset, chunk=int(args.chunk), profile_name=str(args.profile))
        rollout = _rollout(case, dataset, fields=fields, profile_name=str(args.profile))
        payload = {
            "case": case,
            "dataset_build": build,
            "one_step": one,
            "rollout": rollout,
        }
        out_path = out_dir / f"{case['label']}.json"
        out_path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")
        rows.append(
            {
                "label": case["label"],
                "replay": str(Path(case["replay"]).relative_to(repo_root())),
                "records": build["records"],
                "build_wall_s": build["wall_s"],
                "parse_decompress_s": build["phase_wall_s"].get("replay_parse_decompress", 0.0),
                "parse_peppi_s": build["phase_wall_s"].get("replay_parse_peppi", 0.0),
                "arrow_numpy_s": build["phase_wall_s"].get("arrow_numpy_extract", 0.0),
                "hidden_derivation_s": build["phase_wall_s"].get("hidden_lane_derivation", 0.0),
                "item_materialization_s": build["phase_wall_s"].get("item_fixed_slot_materialization", 0.0),
                "data_load_s": build["phase_wall_s"].get("data_load", 0.0),
                "build_other_s": build["dataset_other_wall_s"],
                "one_step_wall_s": one["wall_s"],
                "one_step_native_reseed_s": one["phase_wall_s"].get("one_step_native_reseed", 0.0),
                "one_step_native_step_s": one["phase_wall_s"].get("one_step_native_step", 0.0),
                "one_step_native_write_s": one["phase_wall_s"].get("one_step_native_write_compare", 0.0),
                "one_step_python_compare_s": one["python_compare_other_wall_s"],
                "rollout_wall_s": rollout["wall_s"],
                "rollout_native_reseed_s": rollout["phase_wall_s"].get("rollout_native_reseed", 0.0),
                "rollout_native_step_s": rollout["phase_wall_s"].get("rollout_native_step", 0.0),
                "rollout_native_write_s": rollout["phase_wall_s"].get("rollout_native_write_compare", 0.0),
                "rollout_python_scan_s": rollout["python_scan_other_wall_s"],
                "report_format_s": one["report_format_wall_s"] + rollout["report_format_wall_s"],
            }
        )

    tsv = out_dir / "summary.tsv"
    columns = tuple(rows[0].keys()) if rows else ()
    with tsv.open("w", encoding="utf-8") as f:
        f.write("\t".join(columns) + "\n")
        for row in rows:
            f.write("\t".join(str(row[col]) for col in columns) + "\n")
    print(tsv)
    for row in rows:
        print(
            f"{row['label']}: records={row['records']} build={row['build_wall_s']:.3f}s "
            f"one_step={row['one_step_wall_s']:.3f}s rollout={row['rollout_wall_s']:.3f}s"
        )


if __name__ == "__main__":
    main()
