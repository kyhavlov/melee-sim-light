"""Visual-only state: viewer track locks (agent_docs/VIEWER_TRACK.md).

A recording carries gameplay, not what a viewer draws. A match run with
``MSL_VISUAL_TRACK`` (a build with MSL_VISUAL_TRACK_BUILD=1) keeps a viewer track of that state, and
``replays/suites/visual_state_locks.json`` stores, for each recording of
``replays/suites/visual_state.json``, digests of the track it wrote: per kind
of line and per minute of game. Validation replays each recording with the
track on, requires the strict gameplay pass, and compares the digests.

    python -m tools.validation.visual_state            # check
    python -m tools.validation.visual_state --write    # record (certified builds only)
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

SUITE = ROOT / "replays" / "suites" / "visual_state.json"
LOCKS = ROOT / "replays" / "suites" / "visual_state_locks.json"
ALGORITHM = "blake2b-64"
WINDOW_FRAMES = 3600
# Kinds the track writes that no perfect recording proves yet: left out of
# every digest (the whole track's, each kind's, each minute's) until one does,
# so a change to them fails nothing (agent_docs/VIEWER_TRACK.md, "Unlocked
# kinds").
UNLOCKED_KINDS = frozenset({
    "fdrot",
    "send",
    "stadscreen",
})


def _hash(parts: list[bytes]) -> str:
    digest = hashlib.blake2b(digest_size=8)
    for part in parts:
        digest.update(part)
    return digest.hexdigest()


def digest(track: bytes) -> dict[str, object]:
    """Digests of a viewer track: every line in order, each kind's lines, and
    each minute's (by the line's frame, the game's numbering). Lines of the
    unlocked kinds are not counted."""
    kinds: dict[str, list[bytes]] = {}
    windows: dict[int, list[bytes]] = {}
    kept: list[bytes] = []
    for line in (line + b"\n" for line in track.split(b"\n") if line):
        # [frame,"kind",...]
        comma = line.index(b",")
        frame = int(line[1:comma])
        kind = line[comma + 2:line.index(b'"', comma + 2)].decode("ascii")
        if kind in UNLOCKED_KINDS:
            continue
        kept.append(line)
        kinds.setdefault(kind, []).append(line)
        windows.setdefault((frame + 123) // WINDOW_FRAMES, []).append(line)
    return {
        "lines": len(kept),
        "fingerprint": _hash(kept),
        "kinds": {kind: {"lines": len(rows), "fingerprint": _hash(rows)}
                  for kind, rows in sorted(kinds.items())},
        "minutes": [_hash(windows.get(i, [])) for i in range(max(windows, default=-1) + 1)],
    }


def differences(expected: dict[str, object], actual: dict[str, object]) -> list[str]:
    """What differs between two digests, kind by kind and minute by minute."""
    out = []
    want = expected.get("kinds", {})
    have = actual.get("kinds", {})
    for kind in sorted(set(want) | set(have)):
        if want.get(kind) != have.get(kind):
            out.append("%s: expected %s, got %s" % (kind, want.get(kind), have.get(kind)))
    for i, (a, b) in enumerate(zip(expected.get("minutes", []), actual.get("minutes", []))):
        if a != b:
            out.append("minute %d differs" % i)
    if len(expected.get("minutes", [])) != len(actual.get("minutes", [])):
        out.append("minutes: expected %d, got %d" % (len(expected.get("minutes", [])),
                                                     len(actual.get("minutes", []))))
    return out


def load_locks(path: Path = LOCKS) -> dict[str, dict[str, object]]:
    data = json.loads(path.read_text())
    if data.get("version") != 1 or data.get("algorithm") != ALGORITHM:
        raise ValueError(f"{path}: unsupported visual-state lock manifest")
    locks = {}
    for entry in data.get("locks", []):
        replay = str(entry.get("replay", ""))
        if not replay or Path(replay).is_absolute() or ".." in Path(replay).parts:
            raise ValueError(f"{path}: lock replay must be repo-relative")
        if replay in locks:
            raise ValueError(f"{path}: duplicate lock: {replay}")
        locks[replay] = entry
    return locks


def write_locks(entries: list[dict[str, object]], path: Path = LOCKS) -> None:
    payload = {
        "version": 1,
        "algorithm": ALGORITHM,
        "window_frames": WINDOW_FRAMES,
        "locks": sorted(entries, key=lambda entry: str(entry["replay"])),
    }
    path.write_text(json.dumps(payload, indent=2) + "\n")


def run_case(native, case, *, timeout: float = 120.0,
             visual_track: bool = True) -> dict[str, object]:
    """One recording with the track on: the strict result, with its track's
    digest under "visual_state" (none without the track)."""
    from tools.validation.validate_replay import validate_one

    result = validate_one(
        native,
        case.replay,
        frames=0,
        start_frame=None,
        timeout=timeout,
        backend="native",
        ucf_cardinals_1_0_enabled=case.ucf_cardinals_1_0_enabled,
        ucf_shield_sdi_enabled=case.ucf_shield_sdi_enabled,
        ucf_sdi_enabled=case.ucf_sdi_enabled,
        ucf_shield_drop_extended_enabled=case.ucf_shield_drop_extended_enabled,
        ucf_shield_drop_084_enabled=case.ucf_shield_drop_084_enabled,
        played_on=case.played_on,
        fnmsubs_profile=case.fnmsubs_profile,
        jit_arithmetic=case.jit_arithmetic,
        visual_track=visual_track,
    )
    if visual_track:
        result["visual_state"] = digest(result.pop("visual_track"))
    return result


def suite_cases():
    from tools.validation import validate_replay as v

    _, cases = v.load_suite_cases(SUITE, characters=v._parse_characters(v.DEFAULT_CHARACTERS),
                                  stages=v._parse_stages(v.DEFAULT_STAGES))
    return cases


def main() -> int:
    from tools.validation.validate_replay import load_native

    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--write", action="store_true", help="record the locks")
    args = parser.parse_args()
    native = load_native()
    cases = suite_cases()
    locks = {} if args.write else load_locks()
    entries = []
    failed = 0
    for case in cases:
        result = run_case(native, case)
        name = case.display_path
        state = result["visual_state"]
        if not result["pass"]:
            print(f"FAIL {name}: gameplay differs from frame {result['first_mismatch_frame']}")
            failed += 1
            continue
        entry = {"replay": name, "frames": int(result["frames"]), **state}
        if args.write:
            entries.append(entry)
            print(f"LOCK {name}: {state['lines']} lines")
            continue
        expected = locks.get(name)
        if expected is None:
            print(f"FAIL {name}: no lock")
            failed += 1
            continue
        diff = differences(expected, state)
        if diff or expected.get("frames") != entry["frames"]:
            print(f"FAIL {name}: " + "; ".join(diff or ["frames differ"]))
            failed += 1
        else:
            print(f"PASS {name}: {state['lines']} lines")
    if args.write and not failed:
        write_locks(entries)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
