from __future__ import annotations

"""Build and score temporary SlippiLab held-out suites.

This is a measurement tool, not a burndown target generator. It writes only under
`reports/triage` by default, keeps selected replay rows out of committed
validation reports, and summarizes held-out movement by character, stage, and
action family.
"""

import argparse
import csv
import json
import re
import subprocess
import sys
import time
import urllib.request
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from typing import Any

from peppi_py import _read_slippi

from tools.eval.dataset import read_dataset
from tools.slippi.suite_io import dataset_path_for_suite_replay, repo_root


CHAR_BY_EXTERNAL_ID: dict[int, str] = {2: "Fox", 9: "Marth", 20: "Falco"}
EXTERNAL_ID_BY_CHAR: dict[str, int] = {v: k for k, v in CHAR_BY_EXTERNAL_ID.items()}
STAGE_NAME: dict[int, str] = {
    2: "Fountain of Dreams",
    3: "Pokemon Stadium",
    8: "Yoshi's Story",
    28: "Dream Land",
    31: "Battlefield",
    32: "Final Destination",
}
STAGE_ORDER: tuple[int, ...] = (31, 28, 32, 2, 3, 8)
CHAR_ORDER: tuple[str, ...] = ("Fox", "Falco", "Marth")
DEFAULT_FIELDS = "action_id,animation_index,on_ground,hitlag,hitstun,state_flags"
USER_AGENT = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "MeleeSimSlippiLabHoldoutMeasure/1.0"
)


@dataclass(frozen=True)
class Selection:
    row: dict[str, Any]
    focal_char: str | None = None
    focal_stage: int | None = None


def _http_json(url: str) -> Any:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as resp:
        return json.loads(resp.read().decode("utf-8"))


def _fetch_metadata(path: Path, *, refresh: bool) -> list[dict[str, Any]]:
    if path.exists() and not refresh:
        return json.loads(path.read_text())
    data = _http_json("https://slippilab.com/api/replays")
    if not isinstance(data, list):
        raise ValueError("SlippiLab /api/replays did not return a list")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    return data


def _load_excludes(paths: list[Path]) -> set[str]:
    out: set[str] = set()
    for path in paths:
        if not path.exists():
            continue
        data = json.loads(path.read_text())
        if isinstance(data, list):
            out.update(str(x) for x in data)
        elif isinstance(data, dict):
            out.update(str(x) for x in data.get("file_names", ()))
    return out


def _played_date(row: dict[str, Any]) -> str:
    raw = str(row.get("played_on") or "")
    return raw[:10]


def _players(row: dict[str, Any]) -> list[dict[str, Any]]:
    players = row.get("players") or []
    return players if isinstance(players, list) else []


def _port_from_player(player: dict[str, Any]) -> int:
    return int(player["player_index"]) + 1


def _row_is_supported(
    row: dict[str, Any],
    *,
    min_frames: int,
    max_frames: int,
    played_after: str,
    played_before: str,
    excludes: set[str],
) -> bool:
    if str(row.get("file_name") or "") in excludes:
        return False
    if bool(row.get("is_teams")):
        return False
    stage_id = int(row.get("external_stage_id") or -1)
    if stage_id not in STAGE_NAME:
        return False
    frames = int(row.get("num_frames") or 0)
    if frames < min_frames or frames > max_frames:
        return False
    played = _played_date(row)
    if played and played < played_after:
        return False
    if played and played > played_before:
        return False
    players = _players(row)
    if len(players) != 2:
        return False
    return all(p.get("external_character_id") in CHAR_BY_EXTERNAL_ID for p in players)


def _download_replay(row: dict[str, Any], replay_dir: Path) -> Path:
    name = str(row["file_name"])
    out = replay_dir / name
    if out.exists():
        return out
    req = urllib.request.Request(
        f"https://slippilab.com/api/replay/{name}",
        headers={"User-Agent": USER_AGENT},
    )
    with urllib.request.urlopen(req, timeout=60) as resp:
        out.write_bytes(resp.read())
    time.sleep(0.08)
    return out


def _is_frozen_stadium(row: dict[str, Any], replay_dir: Path) -> bool | None:
    path = _download_replay(row, replay_dir)
    game = _read_slippi(str(path), False)
    return bool(game.start.get("is_frozen_ps"))


def _candidate_sort_key(row: dict[str, Any]) -> tuple[str, int, str]:
    return (_played_date(row), int(row.get("id") or 0), str(row["file_name"]))


def _select_ditto(
    rows: list[dict[str, Any]],
    *,
    replay_dir: Path,
    per_cell: int,
) -> tuple[list[Selection], dict[str, Any]]:
    selected: list[Selection] = []
    log: dict[str, Any] = {}
    for char in CHAR_ORDER:
        cid = EXTERNAL_ID_BY_CHAR[char]
        for stage_id in STAGE_ORDER:
            candidates = [
                row
                for row in rows
                if int(row.get("external_stage_id") or -1) == stage_id
                and all(p.get("external_character_id") == cid for p in _players(row))
            ]
            candidates.sort(key=_candidate_sort_key)
            chosen: list[dict[str, Any]] = []
            tried: list[dict[str, Any]] = []
            for row in candidates:
                try:
                    frozen = _is_frozen_stadium(row, replay_dir) if stage_id == 3 else None
                except Exception as exc:
                    tried.append({"file_name": row["file_name"], "error": str(exc)[:200]})
                    continue
                tried.append(
                    {
                        "file_name": row["file_name"],
                        "played_on": row.get("played_on"),
                        "num_frames": row.get("num_frames"),
                        "is_frozen_ps": frozen,
                    }
                )
                if stage_id == 3 and frozen is not True:
                    continue
                _download_replay(row, replay_dir)
                chosen.append(row)
                if len(chosen) >= per_cell:
                    break
            key = f"{char}|{STAGE_NAME[stage_id]}"
            log[key] = {"chosen": [row["file_name"] for row in chosen], "tried": tried[:24]}
            if len(chosen) < per_cell:
                raise RuntimeError(f"needed {per_cell} replay(s) for {key}, got {len(chosen)}")
            selected.extend(Selection(row=row, focal_char=char, focal_stage=stage_id) for row in chosen)
    return selected, log


def _select_mixed(
    rows: list[dict[str, Any]],
    *,
    replay_dir: Path,
    per_cell: int,
) -> tuple[list[Selection], dict[str, Any]]:
    selected: list[Selection] = []
    selected_names: set[str] = set()
    coverage: Counter[tuple[str, int]] = Counter()
    log: dict[str, Any] = {}
    for char in CHAR_ORDER:
        cid = EXTERNAL_ID_BY_CHAR[char]
        for stage_id in STAGE_ORDER:
            key = (char, stage_id)
            candidates = [
                row
                for row in rows
                if int(row.get("external_stage_id") or -1) == stage_id
                and any(p.get("external_character_id") == cid for p in _players(row))
            ]
            candidates.sort(key=_candidate_sort_key)
            tried: list[dict[str, Any]] = []
            while coverage[key] < per_cell:
                chosen: dict[str, Any] | None = None
                for row in candidates:
                    name = str(row["file_name"])
                    if name in selected_names:
                        continue
                    try:
                        frozen = _is_frozen_stadium(row, replay_dir) if stage_id == 3 else None
                    except Exception as exc:
                        tried.append({"file_name": name, "error": str(exc)[:200]})
                        selected_names.add(name)
                        continue
                    tried.append(
                        {
                            "file_name": name,
                            "played_on": row.get("played_on"),
                            "num_frames": row.get("num_frames"),
                            "is_frozen_ps": frozen,
                        }
                    )
                    if stage_id == 3 and frozen is not True:
                        selected_names.add(name)
                        continue
                    _download_replay(row, replay_dir)
                    chosen = row
                    break
                if chosen is None:
                    raise RuntimeError(
                        f"needed {per_cell} appearance(s) for {char}|{STAGE_NAME[stage_id]}, "
                        f"got {coverage[key]}"
                    )
                selected.append(Selection(row=chosen, focal_char=char, focal_stage=stage_id))
                selected_names.add(str(chosen["file_name"]))
                for player in _players(chosen):
                    p_char = CHAR_BY_EXTERNAL_ID[int(player["external_character_id"])]
                    coverage[(p_char, stage_id)] += 1
            log[f"{char}|{STAGE_NAME[stage_id]}"] = {
                "coverage": coverage[key],
                "tried": tried[:24],
            }
    return selected, log


def _write_suite(
    *,
    out_dir: Path,
    suite_name: str,
    selections: list[Selection],
    mode: str,
    per_cell: int,
) -> Path:
    unique: dict[str, dict[str, Any]] = {}
    for selection in selections:
        unique[str(selection.row["file_name"])] = selection.row
    suite = {
        "name": suite_name,
        "notes": (
            "Temporary SlippiLab held-out measurement suite generated under reports/triage. "
            f"mode={mode}; per_cell={per_cell}; Pokemon Stadium verified frozen via "
            "game-start is_frozen_ps. Read-only indicator, not a burndown target."
        ),
        "ucf_enabled": True,
        "ucf_cardinals_1_0_enabled": True,
        "replays": [],
    }
    appearances: list[dict[str, Any]] = []
    for row in unique.values():
        ports: list[int] = []
        characters: dict[str, str] = {}
        for player in _players(row):
            port = _port_from_player(player)
            char = CHAR_BY_EXTERNAL_ID[int(player["external_character_id"])]
            ports.append(port)
            characters[str(port)] = char
            appearances.append(
                {
                    "file_name": row["file_name"],
                    "player_index": int(player["player_index"]),
                    "port": port,
                    "focal_char": char,
                    "external_character_id": int(player["external_character_id"]),
                    "stage_id": int(row["external_stage_id"]),
                    "stage": STAGE_NAME[int(row["external_stage_id"])],
                }
            )
        suite["replays"].append(
            {
                "replay": f"{out_dir.as_posix()}/replays/{row['file_name']}",
                "ports": ports,
                "stage_id": int(row["external_stage_id"]),
                "characters": characters,
            }
        )
    out_dir.mkdir(parents=True, exist_ok=True)
    suite_path = out_dir / "suite.json"
    suite_path.write_text(json.dumps(suite, indent=2, sort_keys=True) + "\n")
    (out_dir / "selected_replays_metadata.json").write_text(
        json.dumps(list(unique.values()), indent=2, sort_keys=True) + "\n"
    )
    (out_dir / "selected_appearances.json").write_text(
        json.dumps(appearances, indent=2, sort_keys=True) + "\n"
    )
    return suite_path


def _run(cmd: list[str], *, root: Path) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=root, check=True)


def _run_eval(
    *,
    suite_path: Path,
    datasets_dir: Path,
    out_dir: Path,
    fields: str,
    workers: int,
    force: bool,
) -> None:
    root = repo_root()
    preprocess = [
        sys.executable,
        "-m",
        "tools.slippi.preprocess_suite",
        "--suite",
        str(suite_path),
        "--datasets-dir",
        str(datasets_dir),
        "--workers",
        str(workers),
    ]
    if force:
        preprocess.append("--force")
    _run(preprocess, root=root)
    _run(
        [
            sys.executable,
            "-m",
            "tools.eval.run_rollout_suite_eval",
            "--suite",
            str(suite_path),
            "--datasets-dir",
            str(datasets_dir),
            "--fields",
            fields,
            "--out",
            str(out_dir / "rollout_report.txt"),
        ],
        root=root,
    )
    _run(
        [
            sys.executable,
            "-m",
            "tools.eval.locate_rollout_desyncs",
            "--suite",
            str(suite_path),
            "--datasets-dir",
            str(datasets_dir),
            "--fields",
            fields,
            "--out",
            str(out_dir / "rollout_locate.tsv"),
        ],
        root=root,
    )


def _dataset_label(path: Path, *, root: Path) -> str:
    resolved = path.resolve()
    try:
        return str(resolved.relative_to(root))
    except ValueError:
        return str(resolved)


def _dataset_path_for_summary_entry(
    *, suite_name: str, replay: str, datasets_dir: Path
) -> Path:
    return dataset_path_for_suite_replay(
        suite_name=suite_name,
        replay_rel_path=replay,
        datasets_dir=datasets_dir,
    )


def _enum_names(path: Path, enum_name: str, prefix: str, *, start_value: int = 0) -> dict[int, str]:
    text = path.read_text()
    m = re.search(r"typedef enum\s+" + re.escape(enum_name) + r"\s*\{(?P<body>.*?)\}\s*\w+\s*;", text, re.S)
    if not m:
        raise ValueError(f"could not find enum {enum_name} in {path}")
    out: dict[int, str] = {}
    value = start_value
    for raw in m.group("body").splitlines():
        line = raw.split("//", 1)[0].split("///<", 1)[0].strip().rstrip(",")
        if not line:
            continue
        if "=" in line:
            name, expr = [part.strip() for part in line.split("=", 1)]
            if name.endswith("_Count") or name.endswith("_SelfCount") or name.endswith("_None"):
                continue
            if expr == "-1":
                value = -1
            elif expr == "ftCo_MS_Count":
                value = start_value
            elif expr.startswith(prefix):
                rev = {v: k for k, v in out.items()}
                value = rev[expr[len(prefix) :]]
            else:
                try:
                    value = int(expr, 0)
                except ValueError:
                    continue
        else:
            name = line
        if name.endswith("_Count") or name.endswith("_SelfCount") or name.endswith("_None"):
            if value >= 0:
                value += 1
            continue
        if name.startswith(prefix):
            out[value] = name[len(prefix) :]
        if value >= 0:
            value += 1
    return out


def _action_name_tables(root: Path) -> dict[str, dict[int, str]]:
    common_path = root / "refs/melee/src/melee/ft/chara/ftCommon/forward.h"
    marth_path = root / "refs/melee/src/melee/ft/chara/ftMars/forward.h"
    fox_path = root / "refs/melee/src/melee/ft/chara/ftFox/forward.h"
    common = _enum_names(common_path, "ftCommon_MotionState", "ftCo_MS_")
    count = max(common) + 1
    marth_specials = _enum_names(marth_path, "ftMars_MotionState", "ftMs_MS_", start_value=count)
    fox_specials = _enum_names(fox_path, "ftFox_MotionState", "ftFx_MS_", start_value=count)
    return {
        "Fox": common | fox_specials,
        "Falco": common | fox_specials,
        "Marth": common | marth_specials,
    }


def _family_for_name(name: str) -> str:
    checks = (
        ("Special", "Special"),
        ("AttackAir", "AttackAir"),
        ("LandingAir", "LandingAir"),
        ("Attack", "AttackGround"),
        ("Damage", "Damage"),
        ("Landing", "Landing"),
        ("Fall", "Fall"),
        ("Jump", "Jump"),
        ("Guard", "Guard"),
        ("Down", "Down"),
        ("Passive", "Passive"),
        ("Catch", "ThrowCapture"),
        ("Throw", "ThrowCapture"),
        ("Capture", "ThrowCapture"),
        ("Thrown", "ThrowCapture"),
        ("Cliff", "Cliff"),
        ("Escape", "Escape"),
        ("Dead", "MatchFlow"),
        ("Rebirth", "MatchFlow"),
        ("Entry", "MatchFlow"),
    )
    for prefix, family in checks:
        if name.startswith(prefix):
            return family
    if name == "Wait" or name.startswith(("Turn", "Dash", "Run", "Squat")):
        return "GroundMove"
    return "Other"


def _cluster_family(
    *,
    row: dict[str, str],
    ds_samples: Any,
    char: str,
    action_names: dict[str, dict[int, str]],
) -> tuple[str, str]:
    record = int(row["record"])
    player = int(row["player"])
    if row["field"] == "action_id":
        action = int(row["ref"])
    else:
        action = int(ds_samples["seed_t"][record]["action_id"][player])
    name = action_names.get(char, {}).get(action, f"Action{action}")
    return name, _family_for_name(name)


def _summarize(
    *,
    out_dir: Path,
    suite_path: Path,
    datasets_dir: Path,
) -> None:
    suite = json.loads(suite_path.read_text())
    suite_name = str(suite["name"])
    root = repo_root()
    action_names = _action_name_tables(root)
    char_by_ds_player: dict[tuple[str, int], str] = {}
    stage_by_ds: dict[str, str] = {}
    frames_by_ds: dict[str, int] = {}
    samples_by_ds: dict[str, Any] = {}
    for entry in suite["replays"]:
        ds = _dataset_path_for_summary_entry(
            suite_name=suite_name,
            replay=str(entry["replay"]),
            datasets_dir=datasets_dir,
        )
        data = read_dataset(str(ds))
        ds_key = _dataset_label(ds, root=root)
        frames_by_ds[ds_key] = int(data.header["num_records"])
        samples_by_ds[ds_key] = data.samples
        stage_by_ds[ds_key] = STAGE_NAME[int(entry["stage_id"])]
        for i, port in enumerate(int(p) for p in entry["ports"]):
            char_by_ds_player[(ds_key, i)] = entry["characters"][str(port)]

    mismatches: Counter[tuple[str, int]] = Counter()
    seeded: Counter[tuple[str, int]] = Counter()
    char_stats: dict[str, dict[str, Any]] = defaultdict(lambda: _new_stat())
    stage_stats: dict[tuple[str, str], dict[str, Any]] = defaultdict(lambda: _new_stat())
    family_stats: Counter[tuple[str, str, str]] = Counter()
    symptom_stats: Counter[tuple[str, str, str, str, str]] = Counter()

    locate_path = out_dir / "rollout_locate.tsv"
    if locate_path.exists():
        with locate_path.open(newline="") as f:
            for row in csv.DictReader(f, delimiter="\t"):
                key = (row["dataset"], int(row["player"]))
                char = char_by_ds_player.get(key)
                if char is None:
                    continue
                if int(row["seeded_break"]) != 0:
                    seeded[key] += 1
                    continue
                mismatches[key] += 1
                action_name, family = _cluster_family(
                    row=row,
                    ds_samples=samples_by_ds[row["dataset"]],
                    char=char,
                    action_names=action_names,
                )
                stage = stage_by_ds[row["dataset"]]
                family_stats[(char, stage, family)] += 1
                symptom_stats[(char, stage, family, row["field"], action_name)] += 1

    appearances: list[dict[str, Any]] = []
    for key, char in sorted(char_by_ds_player.items()):
        ds, player = key
        frames = frames_by_ds[ds]
        first = mismatches[key]
        seed_first = seeded[key]
        rate = 1000.0 * first / frames if frames else 0.0
        seed_rate = 1000.0 * seed_first / frames if frames else 0.0
        stage = stage_by_ds[ds]
        _add_stat(char_stats[char], frames=frames, first=first, seeded_first=seed_first, rate=rate)
        _add_stat(stage_stats[(char, stage)], frames=frames, first=first, seeded_first=seed_first, rate=rate)
        appearances.append(
            {
                "replay": Path(ds).name,
                "player": player,
                "char": char,
                "stage": stage,
                "frames": frames,
                "first_mismatches": first,
                "per_1k": rate,
                "seeded_first_mismatches": seed_first,
                "seeded_per_1k": seed_rate,
            }
        )

    summary = {
        "summary": {char: _finish_stat(stat) for char, stat in sorted(char_stats.items())},
        "by_stage": {
            f"{char}|{stage}": _finish_stat(stat)
            for (char, stage), stat in sorted(stage_stats.items())
        },
        "by_action_family": [
            {"char": c, "stage": s, "family": f, "first_mismatches": n}
            for (c, s, f), n in family_stats.most_common()
        ],
        "top_symptom_families": [
            {
                "char": c,
                "stage": s,
                "family": fam,
                "field": field,
                "action": action,
                "first_mismatches": n,
            }
            for (c, s, fam, field, action), n in symptom_stats.most_common(80)
        ],
        "appearances": sorted(appearances, key=lambda a: (-a["per_1k"], a["char"], a["replay"])),
    }
    (out_dir / "char_rollout_rate_summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n"
    )
    (out_dir / "char_rollout_rate_summary.md").write_text(_summary_markdown(summary) + "\n")


def _new_stat() -> dict[str, Any]:
    return {"appearances": 0, "frames": 0, "first": 0, "seeded_first": 0, "rates": []}


def _add_stat(stat: dict[str, Any], *, frames: int, first: int, seeded_first: int, rate: float) -> None:
    stat["appearances"] += 1
    stat["frames"] += int(frames)
    stat["first"] += int(first)
    stat["seeded_first"] += int(seeded_first)
    stat["rates"].append(float(rate))


def _trimmed_mean(values: list[float]) -> float:
    if not values:
        return 0.0
    if len(values) < 5:
        return sum(values) / len(values)
    vals = sorted(values)
    k = max(1, int(len(vals) * 0.1))
    vals = vals[k:-k] or vals
    return sum(vals) / len(vals)


def _finish_stat(stat: dict[str, Any]) -> dict[str, Any]:
    rates = list(stat["rates"])
    return {
        "appearances": int(stat["appearances"]),
        "frames": int(stat["frames"]),
        "first_mismatches": int(stat["first"]),
        "pooled_per_1k": 1000.0 * int(stat["first"]) / int(stat["frames"]) if stat["frames"] else 0.0,
        "median_per_1k": _median(rates),
        "trimmed_mean_per_1k": _trimmed_mean(rates),
        "seeded_first_mismatches": int(stat["seeded_first"]),
        "seeded_pooled_per_1k": 1000.0 * int(stat["seeded_first"]) / int(stat["frames"])
        if stat["frames"]
        else 0.0,
    }


def _median(values: list[float]) -> float:
    if not values:
        return 0.0
    vals = sorted(values)
    mid = len(vals) // 2
    if len(vals) % 2:
        return vals[mid]
    return (vals[mid - 1] + vals[mid]) / 2.0


def _summary_markdown(summary: dict[str, Any]) -> str:
    lines = [
        "# SlippiLab Held-Out Rollout Rates",
        "",
        "Scratch held-out measurement under `reports/triage`; not a committed validation suite and not a burndown target.",
        "",
        "Counts use `rollout_locate.tsv` rows with `seeded_break=0` for first mismatches. Seeded-break rows are reported separately.",
        "",
        "| char | appearances | frames | first mismatches | pooled / 1k | median / 1k | trimmed mean / 1k | seeded pooled / 1k |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for char in ("Marth", "Fox", "Falco"):
        stat = summary["summary"].get(char)
        if not stat:
            continue
        lines.append(
            f"| {char} | {stat['appearances']} | {stat['frames']} | {stat['first_mismatches']} | "
            f"{stat['pooled_per_1k']:.2f} | {stat['median_per_1k']:.2f} | "
            f"{stat['trimmed_mean_per_1k']:.2f} | {stat['seeded_pooled_per_1k']:.2f} |"
        )
    lines.extend(["", "## Top Symptom Families", ""])
    lines.append("| char | stage | family | field | action | first mismatches |")
    lines.append("| --- | --- | --- | --- | --- | ---: |")
    for row in summary["top_symptom_families"][:20]:
        lines.append(
            f"| {row['char']} | {row['stage']} | {row['family']} | {row['field']} | "
            f"{row['action']} | {row['first_mismatches']} |"
        )
    lines.extend(["", "## Worst Evaluated Appearances", ""])
    lines.append("| replay | p | char | stage | frames | first mismatches | / 1k | seeded / 1k |")
    lines.append("| --- | ---: | --- | --- | ---: | ---: | ---: | ---: |")
    for row in summary["appearances"][:20]:
        lines.append(
            f"| {row['replay']} | {row['player']} | {row['char']} | {row['stage']} | "
            f"{row['frames']} | {row['first_mismatches']} | {row['per_1k']:.2f} | "
            f"{row['seeded_per_1k']:.2f} |"
        )
    return "\n".join(lines)


def _parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mode", choices=("ditto", "mixed"), default="ditto")
    ap.add_argument("--per-cell", type=int, default=1)
    ap.add_argument("--out-dir", type=Path, default=Path("reports/triage/slippilab_holdout_measure"))
    ap.add_argument(
        "--metadata-cache",
        type=Path,
        default=Path("reports/triage/slippilab_replays_metadata.json"),
    )
    ap.add_argument("--refresh-metadata", action="store_true")
    ap.add_argument("--exclude-file", action="append", type=Path, default=[])
    ap.add_argument("--min-frames", type=int, default=2400)
    ap.add_argument("--max-frames", type=int, default=12000)
    ap.add_argument("--played-after", default="2024-01-01")
    ap.add_argument("--played-before", default=date.today().isoformat())
    ap.add_argument("--evaluate", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--fields", default=DEFAULT_FIELDS)
    return ap.parse_args()


def main() -> None:
    args = _parse_args()
    root = repo_root()
    out_dir = args.out_dir
    replay_dir = out_dir / "replays"
    replay_dir.mkdir(parents=True, exist_ok=True)
    excludes = _load_excludes(args.exclude_file)
    rows = _fetch_metadata(args.metadata_cache, refresh=bool(args.refresh_metadata))
    supported = [
        row
        for row in rows
        if _row_is_supported(
            row,
            min_frames=int(args.min_frames),
            max_frames=int(args.max_frames),
            played_after=str(args.played_after),
            played_before=str(args.played_before),
            excludes=excludes,
        )
    ]
    if args.mode == "ditto":
        selections, log = _select_ditto(supported, replay_dir=replay_dir, per_cell=int(args.per_cell))
    else:
        selections, log = _select_mixed(supported, replay_dir=replay_dir, per_cell=int(args.per_cell))
    suite_name = f"slippilab_{args.mode}_holdout_measure"
    suite_path = _write_suite(
        out_dir=out_dir,
        suite_name=suite_name,
        selections=selections,
        mode=str(args.mode),
        per_cell=int(args.per_cell),
    )
    (out_dir / "selection_log.json").write_text(json.dumps(log, indent=2, sort_keys=True) + "\n")
    datasets_dir = out_dir / "datasets"
    if args.evaluate:
        _run_eval(
            suite_path=suite_path,
            datasets_dir=datasets_dir,
            out_dir=out_dir,
            fields=str(args.fields),
            workers=int(args.workers),
            force=bool(args.force),
        )
        _summarize(out_dir=out_dir, suite_path=suite_path, datasets_dir=datasets_dir)
    else:
        display_suite = suite_path
        if suite_path.is_absolute():
            display_suite = suite_path.relative_to(root)
        print(f"wrote {display_suite}")
        print("rerun with --evaluate to build datasets and summarize rollout first mismatches")


if __name__ == "__main__":
    main()
