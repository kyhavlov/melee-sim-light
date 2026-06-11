"""Live-path stage-clip fuzzer (DEV-ONLY harness, not part of the test suite).

Drives the real engine the way the webplay viewer does - ONE reseed at episode start, then
hundreds of `msl_binding.step_input` frames - and judges the resulting trajectory with a
generic stage-hull oracle. Reseed-per-row harnesses manufacture clean CollData lanes, which
is exactly the state space where the clip-through class lived; episodes here keep the lanes
live.

Modes:
  sweep  (default): deterministic scenario grids covering the three observed clip classes:
    - ledgedash: ledge grab -> hang -> release -> double jump inward -> airdodge, over a
      grid of hang lengths, dj delays/drifts, dodge timings and angles, both ledges.
    - fall-into-stage: airborne misc states (DamageFly family, DamageFall, Fall, EscapeAir)
      seeded ABOVE the stage across an x grid with a velocity grid, then stepped live
      through the descent into the stage surface (the long live approach re-evolves the
      CollData lanes before boundary contact).
    - boundary-approach: positions outside the hull (below the ledges, beside the walls,
      under the belly) with live drift/jump/dodge approaches INTO the boundary over an
      angle/timing grid.
  random: the original randomized edge-play input policy (kept for soak coverage and the
    locked regression seeds).

Oracle (every supported stage, from data/stages/*.json segment graphs):
  hull-interior - an interior run of the root (ray cast inside the fighter-solid
  non-platform hull, depth > 2 below the local top floor, >= 2 airborne frames) that does
  NOT end in a landing within 10 frames. High-diamond dips and under-lip corner rounding
  are legal interior runs that end in landings; real clips exit the hull airborne (usually
  to a blast-zone death) or end the episode interior. Pokemon Stadium runs its base
  geometry (live-sim episodes never trigger transforms).

Usage:
  python -m tools.eval.fuzz_live_clip --mode sweep --char marth --stage fd
  python -m tools.eval.fuzz_live_clip --mode sweep --matrix          # all chars x stages
  python -m tools.eval.fuzz_live_clip --mode random --episodes 600 --char fox --seed 7

This tool deliberately reuses the test suite's seed/step helpers via a tests/ path import -
it is a triage/eval harness, never imported by runtime code or the build. If these helpers
gain a second durable consumer, move them into a tools/eval utility module instead.
"""

from __future__ import annotations

import argparse
import json
import random
import sys
from dataclasses import dataclass, field

import numpy as np

sys.path.insert(0, "tests")
from test_char_common_action_coverage import _mk_inputs, _run, _seed_base  # noqa: E402

from tools.extraction.char_registry import CHARS as _REGISTRY_CHARS  # noqa: E402

BTN_X = 0x0400
BTN_L = 0x0040
BTN_B = 0x0200

ACT_FALL = 0x001D
ACT_ESCAPE_AIR = 0x00EC
ACT_DAMAGE_FALL = 0x0026
ACT_DAMAGE_FLY_HI = 0x0058
ACT_DAMAGE_FLY_N = 0x0057
ACT_DAMAGE_FLY_LW = 0x0059
ACT_DAMAGE_FLY_TOP = 0x005A
ACT_DAMAGE_FLY_ROLL = 0x005B

# Slippi stage ids for the six supported stages.
STAGES = {
    "fd": (32, "final_destination"),
    "bf": (31, "battlefield"),
    "dl": (28, "dream_land_n64"),
    "ys": (8, "yoshis_story"),
    "fod": (2, "fountain_of_dreams"),
    "ps": (3, "pokemon_stadium"),
}


class StageHull:
    """Stage-body model from the extracted segment graph: fighter-solid non-platform
    segments form the closed hull; interior testing is a ray cast against that segment soup
    (no link-walking, no per-stage constants)."""

    def __init__(self, json_name: str):
        st = json.load(open(f"data/stages/{json_name}.json"))
        # The extracted JSON stores UNSCALED dat coordinates; world coords are
        # json * unit_scale (mpLibLoad semantics - the engine's bins apply this once).
        # BF world edge = 85.5 * 0.8 = 68.4, YS = 80 * 0.7 = 56, FoD = 84.46 * 0.75 = 63.3.
        scale = float(st.get("unit_scale", 1.0) or 1.0)
        segs = []
        for s in st["segments"]:
            s = dict(s)
            for k in ("x0", "y0", "x1", "y1"):
                s[k] = float(s[k]) * scale
            segs.append(s)
        self.hull = [s for s in segs if s.get("fighter_solid") and not s.get("platform")]
        self.floors = [s for s in self.hull if s.get("kind") == "floor"]
        self.min_x = min(min(s["x0"], s["x1"]) for s in self.floors)
        self.max_x = max(max(s["x0"], s["x1"]) for s in self.floors)
        self.min_y = min(min(s["y0"], s["y1"]) for s in self.hull)

    def top_floor_at(self, x: float):
        best = None
        for s in self.floors:
            lo, hi = (s["x0"], s["x1"]) if s["x0"] <= s["x1"] else (s["x1"], s["x0"])
            if lo - 1e-3 <= x <= hi + 1e-3:
                dx = s["x1"] - s["x0"]
                t = (x - s["x0"]) / dx if abs(dx) > 1e-6 else 0.0
                y = s["y0"] + t * (s["y1"] - s["y0"])
                if best is None or y > best:
                    best = y
        return best

    def inside(self, x: float, y: float) -> bool:
        crossings = 0
        for s in self.hull:
            y0, y1 = s["y0"], s["y1"]
            if (y0 > y) == (y1 > y):
                continue
            t = (y - y0) / (y1 - y0)
            ix = s["x0"] + t * (s["x1"] - s["x0"])
            if ix > x:
                crossings += 1
        return (crossings % 2) == 1


_HULLS: dict = {}


def stage_hull(stage: str) -> StageHull:
    if stage not in _HULLS:
        _HULLS[stage] = StageHull(STAGES[stage][1])
    return _HULLS[stage]


def _judge(stage: str, outs: list) -> list:
    # Trajectory-aware oracle. Two legal ways for the ROOT to be inside the hull while the
    # collision diamond is not: (a) high-diamond poses (incl. live-JObj damage tumbles) dip
    # the root below the floor until the diamond touches and the landing/DownBound snaps;
    # (b) the under-lip corner rounding transits the lip region and pops out on top. BOTH
    # legal cases END IN A LANDING within a few frames. A REAL clip's interior run ends with
    # the fighter exiting the hull while still airborne (out the bottom or side, usually to
    # a blast-zone death) or the episode ending interior. So: flag interior runs (>= 2
    # frames, depth > 2 below the local top floor) whose end is NOT a landing within
    # LAND_GRACE frames.
    MIN_RUN = 2
    LAND_GRACE = 16
    GAP_MERGE = 6  # a corner transit can blip above the depth band mid-run; merge across it
    hull = stage_hull(stage)
    runs = []
    run_start = None
    run_len = 0
    gap = 0
    for i, o in enumerate(outs):
        grounded = int(o["on_ground"][0])
        x = float(o["pos_x"][0])
        y = float(o["pos_y"][0])
        top = hull.top_floor_at(x)
        act = int(o["action_id"][0])
        # Cliff-family actions (CliffCatch..CliffJump escapes, 0xFC-0x10A) are ledge-anchored:
        # their root legally sits inside the shallow band at the lip.
        ledge_anchored = 0x00FC <= act <= 0x010A
        interior = ((not grounded) and not ledge_anchored and top is not None and
                    y < top - 2.0 and hull.inside(x, y))
        if interior:
            if run_start is None:
                run_start = i
            run_len += 1
            gap = 0
        elif run_start is not None:
            gap += 1
            if grounded or gap >= GAP_MERGE:
                if run_len >= MIN_RUN:
                    runs.append((run_start, i - gap + 1))
                run_start = None
                run_len = 0
                gap = 0
    # A run still open at episode end is INDETERMINATE (the landing may simply lie past the
    # input script) - the sweep families end with long neutral tails so real clips manifest
    # before truncation; do not flag truncated dips.
    if run_start is not None and run_len >= MIN_RUN:
        runs.append((run_start, len(outs)))  # episode ended interior
    for start, end in runs:
        # Resolution = a landing OR a ledge grab (CliffCatch/CliffWait) within the grace
        # window. NOTE (random mode): a run truncated by episode end, or one resolved by a
        # grab the grace window misses, can read as a violation - the deterministic sweep
        # families end with long neutral tails and are artifact-free; treat random-mode hits
        # as leads to triage, not verdicts.
        resolved = any(
            int(outs[j]["on_ground"][0]) or int(outs[j]["action_id"][0]) in (0x00FC, 0x00FD)
            for j in range(end, min(len(outs), end + LAND_GRACE)))
        if end >= len(outs) or not resolved:
            o = outs[min(end, len(outs) - 1) - 1]
            return [("hull-interior", start, round(float(o["pos_x"][0]), 2),
                     round(float(o["pos_y"][0]), 2), int(o["action_id"][0]))]
    return []


def _base_seed(char: str, stage: str, *, grounded: bool = True, pos_x: float = 0.0,
               pos_y: float = 0.0) -> np.ndarray:
    # _seed_base's char map inherits the coverage-matrix exclusion (falco); set char and
    # stage directly from the registry / stage table.
    seed = _seed_base("marth", grounded=grounded, pos_y=pos_y)
    seed["char_id"][0, 0] = np.uint8(_REGISTRY_CHARS[char].internal_id)
    seed["stage_id"][0] = np.uint32(STAGES[stage][0])
    seed["pos_x"][0, 0] = np.float32(pos_x)
    if not grounded:
        # An airborne spawn carries NO floor: the zero-initialized ground_id lane would fake
        # "carried segment 0" (a strip on FD), which combined with cold runtime lanes is a
        # reseed-only state no live play reaches.
        seed["ground_id"][0, 0] = np.uint16(0xFFFF)
    return seed


SETTLE_FRAMES = 14


def _settled_ground_seed(char: str, stage: str, pos_x: float):
    """Stage-agnostic grounded spawn: the FD-shaped grounded seed lanes do not validate on
    other stages (the engine un-grounds the fighter). Spawn airborne just above the local
    floor instead and let the engine LAND live during a short settle prefix - the landing
    assigns the correct ground state on every stage. Returns (seed, settle_script)."""
    hull = stage_hull(stage)
    top = hull.top_floor_at(pos_x)
    if top is None:
        top = 0.0
    seed = _base_seed(char, stage, grounded=False, pos_x=pos_x, pos_y=top + 1.5)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
    return seed, [_mk_inputs()] * SETTLE_FRAMES


def _run_case(seed: np.ndarray, script: list, stage: str) -> list:
    return _judge(stage, _run(seed, script))


# ---------------------------------------------------------------------------
# Family 1: ledgedash (grab -> hang -> release -> dj inward -> airdodge grid)
# ---------------------------------------------------------------------------


def sweep_ledgedash(char: str, stage: str) -> list:
    hull = stage_hull(stage)
    found = []
    for side in (1, -1):
        edge_x = hull.max_x if side > 0 else hull.min_x
        for hang in (4, 18, 35):
            for dj_delay in (1, 4):
                for dodge_af_delay in (0, 4, 8, 13):
                    for ax_mag, ay in ((127, -100), (116, -106), (90, -90), (127, -40),
                                       (60, -120), (127, 0)):
                        # spawn just above the stage near the edge, settle-land, then run
                        # off toward it and drift to grab
                        seed, script = _settled_ground_seed(char, stage,
                                                            edge_x - side * 10.0)
                        script = script + [_mk_inputs(main_x=side * 127)] * 12
                        script += [_mk_inputs(main_x=side * 40)] * 10  # fall, drift to grab
                        script += [_mk_inputs()] * hang
                        script.append(_mk_inputs(main_x=-side * 35))  # release
                        script += [_mk_inputs()] * dj_delay
                        dj_x = -side * 90
                        script.append(_mk_inputs(buttons=BTN_X, main_x=dj_x, main_y=-60))
                        script += [_mk_inputs(main_x=dj_x, main_y=-60)] * dodge_af_delay
                        script.append(_mk_inputs(buttons=BTN_L, l=255, main_x=-side * ax_mag,
                                                 main_y=ay))
                        script += [_mk_inputs(main_x=-side * ax_mag, main_y=ay)] * 70
                        v = _run_case(seed, script, stage)
                        if v:
                            found.append({"family": "ledgedash", "char": char, "stage": stage,
                                          "case": (side, hang, dj_delay, dodge_af_delay,
                                                   ax_mag, ay), "violation": v})
    return found


# ---------------------------------------------------------------------------
# Family 2: misc states falling from above INTO the stage surface
# ---------------------------------------------------------------------------


def sweep_fall_into_stage(char: str, stage: str) -> list:
    hull = stage_hull(stage)
    found = []
    actions = (
        (ACT_DAMAGE_FLY_HI, 28), (ACT_DAMAGE_FLY_N, 28), (ACT_DAMAGE_FLY_LW, 28),
        (ACT_DAMAGE_FLY_TOP, 28), (ACT_DAMAGE_FLY_ROLL, 28), (ACT_DAMAGE_FALL, 0),
        (ACT_FALL, 0), (ACT_ESCAPE_AIR, 0),
    )
    span = hull.max_x - hull.min_x
    xs = [hull.min_x + span * t for t in (0.02, 0.15, 0.35, 0.5, 0.65, 0.85, 0.98)]
    xs += [hull.min_x - 1.5, hull.max_x + 1.5]  # just outside the lips
    for act, hitstun in actions:
        for x0 in xs:
            for vx in (-2.5, -1.0, 0.0, 1.0, 2.5):
                for vy in (-1.0, -3.0, -5.0):
                    seed = _base_seed(char, stage, grounded=False, pos_x=x0, pos_y=45.0)
                    seed["action_id"][0, 0] = np.uint16(act)
                    seed["animation_index"][0, 0] = np.uint32(0xFFFFFFFF)
                    seed["hitstun"][0, 0] = np.uint8(hitstun)
                    if act in (ACT_DAMAGE_FLY_HI, ACT_DAMAGE_FLY_N, ACT_DAMAGE_FLY_LW,
                               ACT_DAMAGE_FLY_TOP, ACT_DAMAGE_FLY_ROLL):
                        seed["speed_x_attack"][0, 0] = np.float32(vx)
                        seed["speed_y_attack"][0, 0] = np.float32(vy)
                    else:
                        seed["speed_air_x_self"][0, 0] = np.float32(vx)
                        seed["speed_y_self"][0, 0] = np.float32(vy)
                    # long live descent: lanes re-evolve well before boundary contact
                    script = [_mk_inputs()] * 110
                    v = _run_case(seed, script, stage)
                    if v:
                        found.append({"family": "fall-into-stage", "char": char,
                                      "stage": stage,
                                      "case": (hex(act), round(x0, 1), vx, vy),
                                      "violation": v})
    return found


# ---------------------------------------------------------------------------
# Family 3: generic boundary approach from below / the sides
# ---------------------------------------------------------------------------


def sweep_boundary_approach(char: str, stage: str) -> list:
    """Boundary approaches built from LIVE run-off prefixes (settle -> run off the edge ->
    drift outward for a gridded fall time = depth proxy), so every case is a state real play
    reaches - raw airborne spawns can pre-penetrate hull features or fake unreachable lane
    combinations. The action grid then drives back INTO the boundary."""
    hull = stage_hull(stage)
    found = []
    for side in (1, -1):
        edge_x = hull.max_x if side > 0 else hull.min_x
        for fall_frames in (6, 14, 24, 36):
            for action in ("dj", "dj_dodge", "drift"):
                for drift in (60, 95, 127):
                    for dodge_delay in ((2, 6, 10, 14) if action == "dj_dodge" else (0,)):
                        seed, script = _settled_ground_seed(char, stage,
                                                            edge_x - side * 12.0)
                        script = script + [_mk_inputs(main_x=side * 127)] * 16  # run off
                        script += [_mk_inputs(main_x=side * 30)] * fall_frames  # fall outward
                        inward = -side * drift
                        if action in ("dj", "dj_dodge"):
                            script.append(_mk_inputs(buttons=BTN_X, main_x=inward, main_y=-50))
                            script += [_mk_inputs(main_x=inward, main_y=-50)] * (
                                dodge_delay if action == "dj_dodge" else 30)
                        if action == "dj_dodge":
                            script.append(_mk_inputs(buttons=BTN_L, l=255, main_x=inward,
                                                     main_y=-95))
                            script += [_mk_inputs(main_x=inward, main_y=-95)] * 60
                        else:
                            script += [_mk_inputs(main_x=inward)] * 60
                        v = _run_case(seed, script, stage)
                        if v:
                            found.append({"family": "boundary-approach", "char": char,
                                          "stage": stage,
                                          "case": (side, fall_frames, action, drift,
                                                   dodge_delay), "violation": v})
    return found


SWEEP_FAMILIES = {
    "ledgedash": sweep_ledgedash,
    "fall-into-stage": sweep_fall_into_stage,
    "boundary-approach": sweep_boundary_approach,
}


# ---------------------------------------------------------------------------
# Random mode (the original policy; the locked regression seeds replay through this)
# ---------------------------------------------------------------------------


@dataclass
class Episode:
    rng_seed: int
    char: str
    start_x: float
    stage: str = "fd"
    script: list = field(default_factory=list)


def _policy_script(rng: random.Random, frames: int) -> list:
    script = []
    side = rng.choice((1, -1))
    while len(script) < frames:
        phase = rng.random()
        if phase < 0.30:
            mag = rng.choice((40, 70, 127))
            for _ in range(rng.randint(6, 40)):
                script.append(_mk_inputs(main_x=side * mag))
        elif phase < 0.45:
            drift = rng.choice((-110, -60, -20, 0, 20, 60, 110))
            for _ in range(rng.randint(8, 45)):
                script.append(_mk_inputs(main_x=drift))
        elif phase < 0.62:
            drift = rng.choice((-127, -90, -50, 0, 50, 90, 127))
            dy = rng.choice((-90, -50, 0, 0))
            script.append(_mk_inputs(buttons=BTN_X, main_x=drift, main_y=dy))
            for _ in range(rng.randint(4, 18)):
                script.append(_mk_inputs(main_x=drift, main_y=dy))
            if rng.random() < 0.6:
                script.append(_mk_inputs(buttons=BTN_X, main_x=drift, main_y=dy))
                for _ in range(rng.randint(4, 16)):
                    script.append(_mk_inputs(main_x=drift, main_y=dy))
        elif phase < 0.80:
            ax = rng.choice((-127, -116, -90, -60, 0, 60, 90, 116, 127))
            ay = rng.choice((-127, -106, -95, -60, -30, 0, 30, 95))
            script.append(_mk_inputs(buttons=BTN_L, l=255, main_x=ax, main_y=ay))
            for _ in range(rng.randint(10, 40)):
                script.append(_mk_inputs(main_x=ax, main_y=ay))
        elif phase < 0.95:
            edge = 1 if side > 0 else -1
            for _ in range(rng.randint(14, 30)):
                script.append(_mk_inputs(main_x=edge * 127))
            for _ in range(rng.randint(2, 12)):
                script.append(_mk_inputs(main_x=edge * 40))
            for _ in range(rng.randint(4, 30)):
                script.append(_mk_inputs())
            script.append(_mk_inputs(main_x=-edge * rng.choice((25, 35, 60))))
            for _ in range(rng.randint(1, 6)):
                script.append(_mk_inputs())
            dj_x = -edge * rng.choice((60, 90, 120))
            dj_y = rng.choice((-90, -60, 0))
            script.append(_mk_inputs(buttons=BTN_X, main_x=dj_x, main_y=dj_y))
            for _ in range(rng.randint(6, 20)):
                script.append(_mk_inputs(main_x=dj_x, main_y=dj_y))
            ax = -edge * rng.choice((90, 110, 127))
            ay = rng.choice((-110, -95, -70, -40))
            script.append(_mk_inputs(buttons=BTN_L, l=255, main_x=ax, main_y=ay))
            for _ in range(rng.randint(15, 50)):
                script.append(_mk_inputs(main_x=ax, main_y=ay))
        else:
            if rng.random() < 0.25:
                bx = rng.choice((-127, 0, 127))
                by = rng.choice((-127, 0, 127))
                script.append(_mk_inputs(buttons=BTN_B, main_x=bx, main_y=by))
            for _ in range(rng.randint(3, 20)):
                script.append(_mk_inputs())
    return script[:frames]


def run_episode(ep: Episode) -> list:
    if ep.stage == "fd":
        # FD keeps the historical direct grounded spawn so the locked regression seeds stay
        # byte-reproducible.
        seed = _base_seed(ep.char, ep.stage, pos_x=ep.start_x, pos_y=0.0)
        return _run_case(seed, ep.script, ep.stage)
    seed, settle = _settled_ground_seed(ep.char, ep.stage, ep.start_x)
    return _run_case(seed, settle + ep.script, ep.stage)


def run_random(char: str, stage: str, episodes: int, frames: int, master_seed: int) -> list:
    hull = stage_hull(stage)
    span = hull.max_x - hull.min_x
    start_choices = tuple(hull.min_x + span * t for t in (0.03, 0.15, 0.35, 0.65, 0.85, 0.97))
    master = random.Random(master_seed)
    found = []
    for n in range(episodes):
        ep_seed = master.randrange(1 << 31)
        rng = random.Random(ep_seed)
        # FD keeps the historical start set so the locked regression seeds stay reproducible.
        start_x = rng.choice(
            (55.0, 70.0, 78.0, 83.0, -70.0, -83.0)) if stage == "fd" else rng.choice(
                start_choices)
        ep = Episode(rng_seed=ep_seed, char=char, start_x=start_x, stage=stage)
        # Neutral resolution tail: without it, a dip in the final frames reads as an
        # unresolved interior run (episode-truncation false positives).
        ep.script = _policy_script(rng, frames) + [_mk_inputs()] * 60
        v = run_episode(ep)
        if v:
            found.append({"family": "random", "char": char, "stage": stage,
                          "rng_seed": ep_seed, "start_x": start_x, "violation": v})
            print(f"VIOLATION random ep={n} seed={ep_seed} start_x={start_x} {v}")
        if (n + 1) % 200 == 0:
            print(f"... {n + 1}/{episodes} random episodes, {len(found)} violations")
    return found


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=("sweep", "random"), default="sweep")
    ap.add_argument("--char", default="marth")
    ap.add_argument("--stage", default="fd", choices=tuple(STAGES))
    ap.add_argument("--matrix", action="store_true",
                    help="sweep all chars x all stages (ignores --char/--stage)")
    ap.add_argument("--family", default=None, choices=tuple(SWEEP_FAMILIES))
    ap.add_argument("--episodes", type=int, default=500, help="random mode only")
    ap.add_argument("--frames", type=int, default=420, help="random mode only")
    ap.add_argument("--seed", type=int, default=7, help="random mode only")
    ap.add_argument("--save-violations", default=None)
    args = ap.parse_args()

    found = []
    if args.mode == "random":
        found = run_random(args.char, args.stage, args.episodes, args.frames, args.seed)
    else:
        chars = ("marth", "fox", "falco") if args.matrix else (args.char,)
        stages = tuple(STAGES) if args.matrix else (args.stage,)
        families = (args.family,) if args.family else tuple(SWEEP_FAMILIES)
        for ch in chars:
            for st in stages:
                for fam in families:
                    hits = SWEEP_FAMILIES[fam](ch, st)
                    for h in hits:
                        print(f"VIOLATION {fam} {ch}/{st} case={h['case']} {h['violation']}")
                    found.extend(hits)
                    print(f"[{ch}/{st}/{fam}] done, {len(hits)} violations")
    print(f"TOTAL: {len(found)} violations")
    if args.save_violations and found:
        with open(args.save_violations, "w") as f:
            json.dump(found, f, indent=1, default=str)
        print(f"saved to {args.save_violations}")
    return 1 if found else 0


if __name__ == "__main__":
    raise SystemExit(main())
