from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Iterable, Iterator


@dataclass(frozen=True)
class FloorLine:
    segment_id: int
    x0: float
    y0: float
    x1: float
    y1: float
    is_ledge: bool


@dataclass(frozen=True)
class FloorClipCandidate:
    trace: str
    stage_id: int
    player: int
    point: str
    start_frame: int
    end_frame: int
    min_y: float
    floor_y_at_min: float
    action_ids: tuple[int, ...]


def _line_y_at_x(line: FloorLine, x: float) -> float:
    dx = line.x1 - line.x0
    if abs(dx) <= 1.0e-6:
        return max(line.y0, line.y1)
    t = (x - line.x0) / dx
    return line.y0 + (line.y1 - line.y0) * t


def _line_contains_x(line: FloorLine, x: float, *, margin: float = 0.05) -> bool:
    lo = min(line.x0, line.x1) - margin
    hi = max(line.x0, line.x1) + margin
    return lo <= x <= hi


def stage_floor_lines(stage_id: int) -> list[FloorLine]:
    import msl_binding

    lines: list[FloorLine] = []
    for segment_id in range(256):
        seg = msl_binding.stage_floor_segment(stage_id, segment_id)
        if seg is None:
            continue
        if int(seg.get("fighter_solid", 0)) == 0 or int(seg.get("is_platform", 0)) != 0:
            continue
        lines.append(
            FloorLine(
                segment_id=int(seg["segment_i"]),
                x0=float(seg["x0"]),
                y0=float(seg["y0"]),
                x1=float(seg["x1"]),
                y1=float(seg["y1"]),
                is_ledge=bool(int(seg.get("is_ledge", 0))),
            )
        )
    return lines


def _floor_y_for_x(lines: Iterable[FloorLine], x: float) -> float | None:
    best: float | None = None
    for line in lines:
        if not _line_contains_x(line, x):
            continue
        y = _line_y_at_x(line, x)
        if best is None or y > best:
            best = y
    return best


_OWNER_TABLE_BY_INTERNAL_CHAR = {
    1: "fox",
    22: "falco",
}


@lru_cache(maxsize=None)
def _submotion_by_action(internal_char_id: int) -> tuple[int, ...] | None:
    name = _OWNER_TABLE_BY_INTERNAL_CHAR.get(int(internal_char_id))
    if name is None:
        return None
    from tools.slippi.motion_state_owners import read_mslmso01_v1

    path = Path("data/motion_state/owners") / f"{name}.bin"
    if not path.exists():
        return None
    owners = read_mslmso01_v1(path)
    return tuple(int(v) for v in owners.submotion_id)


def _state_sample_y(state: dict, point: str) -> float | None:
    root_y = float(state.get("yPosition", 0.0))
    if point == "root":
        return root_y
    if point != "ecb-bottom":
        raise ValueError(f"unsupported sample point: {point}")

    table = _submotion_by_action(int(state.get("internalCharacterId", -1)))
    if table is None:
        return None
    action_id = int(state.get("actionStateId", -1))
    if action_id < 0 or action_id >= len(table):
        return None
    submotion_id = int(table[action_id])
    if submotion_id < 0 or submotion_id >= 0xFFFF:
        return None

    # Modelplay traces publish the same action-frame counter the simulator reports. ECB sampling
    # uses generated MSLMSO01 action->submotion data plus extracted ECB tables, matching runtime
    # collision ownership instead of root-position-only viewer geometry.
    action_frame = int(float(state.get("actionStateFrameCounter", 0.0)))
    import msl_binding

    return root_y + float(
        msl_binding.ecb_bottom_rel_y(int(state["internalCharacterId"]), submotion_id, action_frame)
    )


def _state_is_clip_candidate(
    state: dict, lines: list[FloorLine], threshold: float, ignore_below: float, point: str
) -> tuple[bool, float]:
    if bool(state.get("isDead", False)) or bool(state.get("isOffscreen", False)):
        return False, 0.0
    if bool(state.get("isGrounded", False)):
        return False, 0.0
    action_id = int(state.get("actionStateId", -1))
    if action_id in {0, 1, 2, 322, 323}:
        return False, 0.0
    x = float(state.get("xPosition", 0.0))
    y = _state_sample_y(state, point)
    if y is None:
        return False, 0.0
    if y < ignore_below:
        return False, 0.0
    floor_y = _floor_y_for_x(lines, x)
    if floor_y is None:
        return False, 0.0
    return y < floor_y - threshold, floor_y


def find_floor_clip_candidates(
    trace_path: Path,
    *,
    threshold: float = 4.0,
    min_frames: int = 1,
    ignore_below: float = -20.0,
    point: str = "ecb-bottom",
) -> list[FloorClipCandidate]:
    trace = json.loads(trace_path.read_text(encoding="utf-8"))
    stage_id = int(trace.get("settings", {}).get("stageId", trace.get("stage_id", -1)))
    if stage_id < 0:
        return []
    lines = stage_floor_lines(stage_id)
    if not lines:
        return []

    active: dict[int, dict[str, object]] = {}
    out: list[FloorClipCandidate] = []

    def close(player: int, end_frame: int) -> None:
        cur = active.pop(player, None)
        if cur is None:
            return
        start = int(cur["start"])
        if end_frame - start + 1 < min_frames:
            return
        out.append(
            FloorClipCandidate(
                trace=str(trace_path),
                stage_id=stage_id,
                player=player,
                point=point,
                start_frame=start,
                end_frame=end_frame,
                min_y=float(cur["min_y"]),
                floor_y_at_min=float(cur["floor_y"]),
                action_ids=tuple(sorted(cur["actions"])),  # type: ignore[arg-type]
            )
        )

    for frame in trace.get("frames", []):
        frame_no = int(frame.get("frameNumber", 0))
        seen: set[int] = set()
        for player_row in frame.get("players", []):
            state = player_row.get("state", {})
            player = int(state.get("playerIndex", player_row.get("playerIndex", -1)))
            if player < 0:
                continue
            seen.add(player)
            is_candidate, floor_y = _state_is_clip_candidate(
                state, lines, threshold, ignore_below, point
            )
            if not is_candidate:
                close(player, frame_no - 1)
                continue
            y = _state_sample_y(state, point)
            if y is None:
                close(player, frame_no - 1)
                continue
            action_id = int(state.get("actionStateId", -1))
            cur = active.setdefault(
                player,
                {"start": frame_no, "min_y": y, "floor_y": floor_y, "actions": set()},
            )
            cur["actions"].add(action_id)  # type: ignore[union-attr]
            if y < float(cur["min_y"]):
                cur["min_y"] = y
                cur["floor_y"] = floor_y
        for player in list(active):
            if player not in seen:
                close(player, frame_no - 1)

    last_frame = int(trace.get("frames", [{}])[-1].get("frameNumber", 0)) if trace.get("frames") else 0
    for player in list(active):
        close(player, last_frame)
    return out


def _iter_trace_paths(paths: Iterable[str]) -> Iterator[Path]:
    for raw in paths:
        path = Path(raw)
        if path.is_dir():
            yield from sorted(p for p in path.glob("*.json") if not p.name.endswith("_summary.json"))
        else:
            yield path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Find modelplay frames where an airborne fighter is below an in-span solid floor."
    )
    parser.add_argument("paths", nargs="+", help="Trace JSON files or directories containing traces.")
    parser.add_argument("--threshold", type=float, default=4.0)
    parser.add_argument("--min-frames", type=int, default=1)
    parser.add_argument(
        "--point",
        choices=("ecb-bottom", "root"),
        default="ecb-bottom",
        help="Sample ECB bottom by default; root mode is useful for viewer-pose audits only.",
    )
    parser.add_argument(
        "--ignore-below",
        type=float,
        default=-20.0,
        help="Ignore very low blastzone-like positions below this sampled Y.",
    )
    args = parser.parse_args(argv)

    rows: list[FloorClipCandidate] = []
    for path in _iter_trace_paths(args.paths):
        rows.extend(
            find_floor_clip_candidates(
                path,
                threshold=float(args.threshold),
                min_frames=max(1, int(args.min_frames)),
                ignore_below=float(args.ignore_below),
                point=str(args.point),
            )
        )

    try:
        print("trace\tstage\tplayer\tpoint\tstart\tend\tframes\tmin_y\tfloor_y\taction_ids")
        for row in rows:
            actions = ",".join(str(a) for a in row.action_ids)
            print(
                f"{row.trace}\t{row.stage_id}\t{row.player}\t{row.point}\t"
                f"{row.start_frame}\t{row.end_frame}\t"
                f"{row.end_frame - row.start_frame + 1}\t{row.min_y:.6g}\t"
                f"{row.floor_y_at_min:.6g}\t{actions}"
            )
    except BrokenPipeError:
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
