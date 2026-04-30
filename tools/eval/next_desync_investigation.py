from __future__ import annotations

"""Build a compact investigation packet for the next disruptive rollout desync."""

import argparse
import csv
import importlib
import json
import subprocess
import sys
from collections import defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable

import numpy as np

from tools.eval import disruptive_rollout_desyncs as disruptive
from tools.eval.dataset import COMPARE_DTYPE, MAX_ITEMS, read_dataset
from tools.eval.facing_residual_blocker_report import load_action_id_names
from tools.eval.validation_profile import ValidationProfile, get_validation_profile, validation_profile_names
from tools.slippi.known_data_artifacts import (
    read_mslftsc1_v1,
    read_mslitar1,
    read_mslpart1_v1,
    read_mslstg01_v1,
)
from tools.slippi.motion_state_owners import read_callback_manifest, read_mslmso01_v1
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


DEFAULT_OUT_DIR = Path("reports/triage/next_desync")
DEFAULT_HORIZONS = "10,20,60"
DEFAULT_TOP = 80
DEFAULT_BATCH_SIZE = 512
DEFAULT_CHUNK_RECORDS = 2048


@dataclass(frozen=True)
class NextCluster:
    cluster_id: str
    rank: int
    suite: str
    dataset: str
    player: int
    horizon: int
    family_id: str
    action_family: str
    field_cluster: str
    frequency: int
    score_sum: float
    score_max: float
    score_avg: float
    record_min: int
    record_max: int
    example_rank: int
    example_record: int
    example_seed_frame: int
    example_first_field: str


def _parse_csv(text: str) -> tuple[str, ...]:
    return tuple(part.strip() for part in str(text).split(",") if part.strip())


def _repo_relative(path: Path, root: Path) -> str:
    try:
        return str(path.resolve().relative_to(root))
    except ValueError:
        return str(path)


def _normalized_horizons(value: Iterable[Any] | str) -> list[int]:
    if isinstance(value, str):
        raw = _parse_csv(value)
    else:
        raw = tuple(value)
    return sorted({int(part) for part in raw})


def _resolve_from_repo(path: str | Path) -> Path:
    p = Path(str(path))
    if not p.is_absolute():
        p = repo_root() / p
    return p.resolve()


def _default_disruptive_candidates(root: Path, suite_name: str, out_dir: Path) -> list[Path]:
    names = [suite_name]
    if "aggregate" in suite_name and "aggregate" not in names:
        names.append("aggregate")
    if "fox_falco" in suite_name or "primary" in suite_name:
        names.append("primary")
    candidates = [out_dir / "disruptive" / "rows.tsv"]
    candidates.extend(root / "reports" / "triage" / f"disruptive_rollout_desyncs_{name}" / "rows.tsv" for name in names)
    triage = root / "reports" / "triage"
    if triage.exists():
        candidates.extend(sorted(triage.glob(f"*{suite_name}*/rows.tsv")))
        if "aggregate" in suite_name:
            candidates.extend(sorted(triage.glob("*aggregate*/rows.tsv")))
        if "fox_falco" in suite_name:
            candidates.extend(sorted(triage.glob("*primary*/rows.tsv")))
    seen: set[Path] = set()
    out: list[Path] = []
    for path in candidates:
        p = path.resolve()
        if p in seen:
            continue
        seen.add(p)
        out.append(path)
    return out


def _dataset_paths_for_suite(root: Path, suite_path: Path, datasets_dir: str) -> list[Path]:
    suite = load_suite(suite_path)
    out: list[Path] = []
    for entry in suite.replays:
        path = dataset_path_for_suite_replay(
            suite_name=suite.name, replay_rel_path=entry.replay, datasets_dir=datasets_dir
        )
        if not path.is_absolute():
            path = root / path
        out.append(path)
    return out


def _summary_matches_request(
    rows_path: Path,
    *,
    suite_path: Path,
    suite_name: str,
    datasets_dir: str,
    profile: str,
    horizons: str,
    max_records: int,
    stride: int,
) -> bool:
    summary_path = rows_path.with_name("summary.json")
    if not summary_path.exists():
        return True
    try:
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return False
    requested_horizons = _normalized_horizons(horizons)
    checks = (
        str(summary.get("suite", "")) == str(suite_name),
        str(summary.get("profile", "")) == str(profile),
        _normalized_horizons(summary.get("horizons", [])) == requested_horizons,
        int(summary.get("stride", -1)) == int(stride),
        int(summary.get("max_records", -1)) == int(max_records),
    )
    if not all(checks):
        return False
    summary_suite_path = summary.get("suite_path")
    if summary_suite_path is not None and Path(str(summary_suite_path)).resolve() != suite_path.resolve():
        return False
    summary_datasets_dir = summary.get("datasets_dir")
    if summary_datasets_dir is not None and _resolve_from_repo(summary_datasets_dir) != _resolve_from_repo(
        datasets_dir
    ):
        return False
    return True


def _current_enough(
    rows_path: Path,
    suite_path: Path,
    dataset_paths: Iterable[Path],
    *,
    suite_name: str,
    datasets_dir: str,
    profile: str,
    horizons: str,
    max_records: int,
    stride: int,
) -> bool:
    if not rows_path.exists():
        return False
    if not _summary_matches_request(
        rows_path,
        suite_path=suite_path,
        suite_name=suite_name,
        datasets_dir=datasets_dir,
        profile=profile,
        horizons=horizons,
        max_records=max_records,
        stride=stride,
    ):
        return False
    rows_mtime = rows_path.stat().st_mtime
    if rows_mtime < suite_path.stat().st_mtime:
        return False
    for path in dataset_paths:
        if path.exists() and rows_mtime < path.stat().st_mtime:
            return False
    return True


def _run_disruptive(
    *,
    suite: Path,
    datasets_dir: str,
    out_dir: Path,
    profile: str,
    horizons: str,
    top: int,
    batch_size: int,
    workers: int,
    chunk_records: int,
    max_records: int,
    stride: int,
) -> Path:
    disruptive_dir = out_dir / "disruptive"
    cmd = [
        sys.executable,
        "-m",
        "tools.eval.disruptive_rollout_desyncs",
        "--suite",
        str(suite),
        "--datasets-dir",
        datasets_dir,
        "--out-dir",
        str(disruptive_dir),
        "--profile",
        profile,
        "--horizons",
        horizons,
        "--top",
        str(top),
        "--batch-size",
        str(batch_size),
        "--workers",
        str(workers),
        "--chunk-records",
        str(chunk_records),
        "--stride",
        str(stride),
    ]
    if max_records > 0:
        cmd.extend(["--max-records", str(max_records)])
    subprocess.run(cmd, check=True)
    return disruptive_dir / "rows.tsv"


def _load_or_refresh_rows(
    *,
    root: Path,
    suite_path: Path,
    suite_name: str,
    datasets_dir: str,
    out_dir: Path,
    profile: str,
    horizons: str,
    top: int,
    batch_size: int,
    workers: int,
    chunk_records: int,
    max_records: int,
    stride: int,
    refresh: bool,
    rows_in: Path | None,
) -> tuple[Path, list[disruptive.DisruptiveRow], str]:
    if rows_in is not None:
        path = rows_in if rows_in.is_absolute() else root / rows_in
        return path, disruptive._read_rows_tsv(path), "explicit --rows-in"
    dataset_paths = _dataset_paths_for_suite(root, suite_path, datasets_dir)
    if not refresh:
        for candidate in _default_disruptive_candidates(root, suite_name, out_dir):
            path = candidate if candidate.is_absolute() else root / candidate
            if _current_enough(
                path,
                suite_path,
                dataset_paths,
                suite_name=suite_name,
                datasets_dir=datasets_dir,
                profile=profile,
                horizons=horizons,
                max_records=max_records,
                stride=stride,
            ):
                return path, disruptive._read_rows_tsv(path), "reused current disruptive rows"
    path = _run_disruptive(
        suite=suite_path,
        datasets_dir=datasets_dir,
        out_dir=out_dir,
        profile=profile,
        horizons=horizons,
        top=top,
        batch_size=batch_size,
        workers=workers,
        chunk_records=chunk_records,
        max_records=max_records,
        stride=stride,
    )
    return path, disruptive._read_rows_tsv(path), "refreshed disruptive rows"


def _action_family(row: disruptive.DisruptiveRow, action_names: dict[int, str]) -> str:
    name = action_names.get(int(row.seed_action_id), f"Action{int(row.seed_action_id)}")
    for prefix in (
        "AttackAir",
        "Attack",
        "DamageFly",
        "Damage",
        "Capture",
        "Throw",
        "Thrown",
        "Guard",
        "Special",
        "Landing",
        "Fall",
        "Run",
        "Dash",
        "Wait",
    ):
        if prefix in name:
            return prefix
    return name


def _cluster_rows(
    rows: list[disruptive.DisruptiveRow], action_names: dict[int, str]
) -> tuple[list[dict[str, Any]], list[NextCluster]]:
    ranked = sorted(
        rows,
        key=lambda r: (-float(r.score_total), -float(r.score_discrete), -float(r.score_float), r.dataset, r.record),
    )
    ranked_dicts: list[dict[str, Any]] = []
    groups: dict[str, list[tuple[int, disruptive.DisruptiveRow]]] = defaultdict(list)
    for rank, row in enumerate(ranked, start=1):
        action_family = _action_family(row, action_names)
        record_bucket = int(row.record) // 180
        cluster_id = (
            f"{Path(row.dataset).name}|p{row.player}|h{row.horizon}|{row.family_id}|"
            f"{action_family}|{row.field_cluster}|b{record_bucket}"
        )
        d = asdict(row)
        d.update({"rank": rank, "next_cluster_id": cluster_id, "action_family": action_family})
        ranked_dicts.append(d)
        groups[cluster_id].append((rank, row))

    clusters: list[NextCluster] = []
    for cluster_id, group in groups.items():
        rows_only = [row for _rank, row in group]
        example_rank, example = max(group, key=lambda pair: pair[1].score_total)
        score_sum = sum(float(row.score_total) for row in rows_only)
        clusters.append(
            NextCluster(
                cluster_id=cluster_id,
                rank=0,
                suite=example.suite,
                dataset=example.dataset,
                player=int(example.player),
                horizon=int(example.horizon),
                family_id=example.family_id,
                action_family=_action_family(example, action_names),
                field_cluster=example.field_cluster,
                frequency=len(rows_only),
                score_sum=float(score_sum),
                score_max=max(float(row.score_total) for row in rows_only),
                score_avg=float(score_sum / float(len(rows_only))),
                record_min=min(int(row.record) for row in rows_only),
                record_max=max(int(row.record) for row in rows_only),
                example_rank=int(example_rank),
                example_record=int(example.record),
                example_seed_frame=int(example.seed_frame),
                example_first_field=example.first_mismatch_field,
            )
        )
    clusters = sorted(clusters, key=lambda c: (-c.score_sum, -c.frequency, c.cluster_id))
    clusters = [NextCluster(**{**asdict(c), "rank": i}) for i, c in enumerate(clusters, start=1)]
    return ranked_dicts, clusters


def _write_tsv(path: Path, rows: list[dict[str, Any]], columns: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, delimiter="\t", fieldnames=columns, lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow({column: row.get(column, "") for column in columns})


def _np_scalar(value: Any) -> Any:
    if isinstance(value, np.generic):
        return value.item()
    return value


def _json_value(value: Any) -> Any:
    value = _np_scalar(value)
    if isinstance(value, (bytes, bytearray)):
        return value.decode("utf-8", errors="replace")
    if isinstance(value, np.ndarray):
        if value.dtype.fields is not None:
            return [_json_value(v) for v in value]
        return [_json_value(v) for v in value.tolist()]
    if isinstance(value, np.void):
        return {name: _json_value(value[name]) for name in value.dtype.names or ()}
    if isinstance(value, (np.integer,)):
        return int(value)
    if isinstance(value, (np.floating,)):
        return float(value)
    return value


PLAYER_FIELDS = (
    "frame_id",
    "stage_id",
    "action_id",
    "action_frame",
    "anim_frame_f32",
    "frame_speed_mul_f32",
    "animation_index",
    "pos_x",
    "pos_y",
    "speed_air_x_self",
    "speed_ground_x_self",
    "speed_y_self",
    "speed_x_attack",
    "speed_y_attack",
    "facing",
    "on_ground",
    "ground_id",
    "wall_id",
    "wall_kind",
    "ceiling_id",
    "ceiling_kind",
    "jumps_left",
    "hitlag",
    "hitstun",
    "percent",
    "shield_hp",
    "hurtbox_state",
    "instance_id",
    "instance_hit_by",
    "last_attack_landed",
    "combo_count",
    "last_hit_by",
    "state_flags",
)


def _compact_row(row: np.void, *, player: int, action_names: dict[int, str]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    names = set(row.dtype.names or ())
    for field in PLAYER_FIELDS:
        if field not in names:
            continue
        value = row[field]
        if hasattr(value, "ndim") and value.ndim > 0:
            if field == "state_flags":
                out[field] = [int(v) for v in value[player].tolist()]
            else:
                out[field] = _json_value(value[player])
        else:
            out[field] = _json_value(value)
    if "action_id" in out:
        out["action_name"] = action_names.get(int(out["action_id"]), f"Action{int(out['action_id'])}")
    return out


def _item_snapshot(row: np.void) -> list[dict[str, Any]]:
    if "items" not in (row.dtype.names or ()):
        return []
    out: list[dict[str, Any]] = []
    for slot, item in enumerate(row["items"]):
        if int(item["exists"]) == 0 and int(item["type"]) == 0 and int(item["instance_id"]) == 0:
            continue
        out.append(
            {
                "slot": int(slot),
                "exists": int(item["exists"]),
                "type": int(item["type"]),
                "state": int(item["state"]),
                "owner": int(item["owner"]),
                "instance_id": int(item["instance_id"]),
                "pos_x": float(item["pos_x"]),
                "pos_y": float(item["pos_y"]),
                "vel_x": float(item["vel_x"]),
                "vel_y": float(item["vel_y"]),
            }
        )
    return out[:8]


def _init_binding_for_suite(
    binding: Any,
    *,
    num_players: int,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
) -> Any:
    kwargs: dict[str, Any] = {"batch_size": 1, "num_players": int(num_players)}
    if ucf_enabled is not None:
        kwargs["ucf_enabled"] = int(bool(ucf_enabled))
    if ucf_cardinals_1_0_enabled is not None:
        kwargs["ucf_cardinals_1_0_enabled"] = int(bool(ucf_cardinals_1_0_enabled))
    return binding.init(**kwargs)


def _simulate_one_step(
    dataset: np.ndarray,
    record: int,
    num_players: int,
    *,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
) -> tuple[np.void | None, str | None]:
    try:
        binding = importlib.import_module("msl_binding")
        sizes = binding.sizes()
        seed_stride = int(sizes["seed"])
        input_stride = int(sizes["input"])
        compare_stride = int(sizes["compare"])
        handle = _init_binding_for_suite(
            binding,
            num_players=int(num_players),
            ucf_enabled=ucf_enabled,
            ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
        )
        try:
            seed = np.frombuffer(dataset[record]["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, seed_stride
            )
            prev_input = np.frombuffer(
                dataset[record]["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).copy().reshape(1, input_stride)
            cur_input = np.frombuffer(dataset[record]["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
            binding.reseed_seed(handle, seed)
            binding.step_input(handle, prev_input, cur_input)
            binding.write_compare(handle, out_bytes)
            return out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy(), None
        finally:
            try:
                binding.destroy(handle)
            except Exception:
                pass
    except Exception as exc:  # pragma: no cover - exercised by CLI smoke tests only when local data is invalid.
        return None, f"{type(exc).__name__}: {exc}"


def _simulate_rollout_to_offset(
    dataset: np.ndarray,
    record: int,
    offset: int,
    num_players: int,
    *,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
) -> tuple[np.void | None, str | None]:
    if offset <= 0:
        return None, f"invalid first_mismatch_offset {offset}"
    try:
        binding = importlib.import_module("msl_binding")
        sizes = binding.sizes()
        seed_stride = int(sizes["seed"])
        input_stride = int(sizes["input"])
        compare_stride = int(sizes["compare"])
        handle = _init_binding_for_suite(
            binding,
            num_players=int(num_players),
            ucf_enabled=ucf_enabled,
            ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
        )
        try:
            seed = np.frombuffer(dataset[record]["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, seed_stride
            )
            prev_input = np.empty((1, input_stride), dtype=np.uint8)
            cur_input = np.empty((1, input_stride), dtype=np.uint8)
            out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
            binding.reseed_seed_rollout(handle, seed)
            for step in range(int(offset)):
                j = int(record) + step
                if j < 0 or j >= int(dataset.shape[0]):
                    return None, f"rollout step record {j} outside dataset"
                prev_input[0, :] = np.frombuffer(
                    dataset[j]["prev_input_t"].tobytes(order="C"), dtype=np.uint8
                ).reshape(input_stride)
                cur_input[0, :] = np.frombuffer(dataset[j]["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                    input_stride
                )
                binding.step_input(handle, prev_input, cur_input)
            binding.write_compare(handle, out_bytes)
            return out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy(), None
        finally:
            try:
                binding.destroy(handle)
            except Exception:
                pass
    except Exception as exc:  # pragma: no cover - only depends on local binding/runtime availability.
        return None, f"{type(exc).__name__}: {exc}"


DIFF_DISCRETE_FIELDS = disruptive.DEFAULT_DISCRETE_FIELDS
DIFF_FLOAT_FIELDS = disruptive.DEFAULT_FLOAT_FIELDS
SUBSYSTEMS = {
    "action/timebase": {"action_id", "action_frame", "animation_index", "frame_id"},
    "position/velocity": {
        "pos_x",
        "pos_y",
        "speed_air_x_self",
        "speed_ground_x_self",
        "speed_y_self",
        "speed_x_attack",
        "speed_y_attack",
    },
    "collision/env": {"on_ground", "ground_id", "jumps_left", "hurtbox_state"},
    "combat/hitlag/hitstun": {
        "hitlag",
        "hitstun",
        "percent",
        "instance_hit_by",
        "last_attack_landed",
        "combo_count",
        "last_hit_by",
    },
    "items": {"item_exists", "item_type", "item_state", "item_owner", "item_instance_id"},
    "shield/guard": {"shield_hp"},
    "seed/provenance": {"instance_id", "state_flags"},
}


def _subsystem_for(field: str) -> str:
    base = field.split("[", 1)[0]
    for name, fields in SUBSYSTEMS.items():
        if base in fields:
            return name
    if "throw" in base or "capture" in base:
        return "throw/capture"
    return "other"


def _profile_diff(
    *,
    out_row: np.void,
    ref_row: np.void,
    players: tuple[int, ...],
    profile: ValidationProfile,
    float_epsilon: float = 0.05,
) -> tuple[list[dict[str, Any]], dict[str, list[dict[str, Any]]], dict[str, Any] | None]:
    diffs: list[dict[str, Any]] = []
    first: dict[str, Any] | None = None

    def append(player: int, field: str, subindex: int, out_v: Any, ref_v: Any, kind: str) -> None:
        nonlocal first
        row = {
            "player": int(player),
            "field": field,
            "subindex": int(subindex),
            "out": _json_value(out_v),
            "ref": _json_value(ref_v),
            "kind": kind,
            "subsystem": _subsystem_for(field),
        }
        if first is None:
            first = row
        diffs.append(row)

    for field in DIFF_DISCRETE_FIELDS:
        out = out_row[field]
        ref = ref_row[field]
        if not hasattr(out, "ndim") or out.ndim == 0:
            if profile.scored_values_differ(field, -1, int(out), int(ref)):
                append(-1, field, -1, out, ref, "discrete")
            continue
        if out.ndim == 1:
            for p in players:
                if profile.scored_values_differ(field, -1, int(out[p]), int(ref[p])):
                    append(int(p), field, -1, out[p], ref[p], "discrete")
            continue
        for p in players:
            out_slice = np.asarray(out[p]).reshape(-1)
            ref_slice = np.asarray(ref[p]).reshape(-1)
            for sub in range(out_slice.size):
                if profile.scored_values_differ(field, int(sub), int(out_slice[sub]), int(ref_slice[sub])):
                    append(int(p), f"{field}[{sub}]", int(sub), out_slice[sub], ref_slice[sub], "discrete")

    for field in DIFF_FLOAT_FIELDS:
        out = out_row[field]
        ref = ref_row[field]
        for p in players:
            if abs(float(out[p]) - float(ref[p])) > float_epsilon:
                append(int(p), field, -1, float(out[p]), float(ref[p]), "float")

    for slot in range(MAX_ITEMS):
        out_item = out_row["items"][slot]
        ref_item = ref_row["items"][slot]
        for field in ("exists", "type", "state", "owner", "instance_id"):
            if int(out_item[field]) != int(ref_item[field]):
                append(-1, f"item_{field}", int(slot), out_item[field], ref_item[field], "item")
        if int(out_item["exists"]) or int(ref_item["exists"]):
            for field in ("pos_x", "pos_y", "vel_x", "vel_y"):
                if abs(float(out_item[field]) - float(ref_item[field])) > float_epsilon:
                    append(-1, f"item_{field}", int(slot), float(out_item[field]), float(ref_item[field]), "item")

    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in diffs:
        grouped[str(row["subsystem"])].append(row)
    return diffs[:80], {key: value[:20] for key, value in sorted(grouped.items())}, first


def _char_slug(sim_char_id: int) -> str | None:
    return {1: "fox", 22: "falco"}.get(int(sim_char_id))


def _motion_state_context(root: Path, row: np.void, player: int, action_names: dict[int, str]) -> dict[str, Any]:
    char_id = int(row["char_id"][player])
    action_id = int(row["action_id"][player])
    slug = _char_slug(char_id)
    if slug is None:
        return {"status": "unavailable", "reason": f"unsupported sim char_id {char_id}"}
    try:
        table = read_mslmso01_v1(root / "data" / "motion_state" / "owners" / f"{slug}.bin")
        symbols = read_callback_manifest(root / "data" / "motion_state" / "owners" / "callback_symbols.json")
        manifest = json.loads((root / "data" / "motion_state" / "owners" / "callback_symbols.json").read_text())
        classes = {name: int(mask) for name, mask in dict(manifest.get("classes", {})).items()}
        bits = int(table.class_bits[action_id]) if action_id < len(table.class_bits) else 0
        class_names = [name for name, mask in sorted(classes.items()) if bits & mask]
        return {
            "status": "ok",
            "char": slug,
            "action_id": action_id,
            "action_name": action_names.get(action_id, f"Action{action_id}"),
            "submotion_id": int(table.submotion_id[action_id]),
            "x4_flags": int(table.x4_flags[action_id]),
            "motion_state_word": int(table.motion_state_word[action_id]),
            "anim_cb": symbols.get(int(table.anim_cb_id[action_id]), f"id:{int(table.anim_cb_id[action_id])}"),
            "iasa_cb": symbols.get(int(table.iasa_cb_id[action_id]), f"id:{int(table.iasa_cb_id[action_id])}"),
            "phys_cb": symbols.get(int(table.phys_cb_id[action_id]), f"id:{int(table.phys_cb_id[action_id])}"),
            "coll_cb": symbols.get(int(table.coll_cb_id[action_id]), f"id:{int(table.coll_cb_id[action_id])}"),
            "cam_cb": symbols.get(int(table.cam_cb_id[action_id]), f"id:{int(table.cam_cb_id[action_id])}"),
            "class_bits": bits,
            "classes": class_names,
        }
    except Exception as exc:
        return {"status": "unavailable", "reason": f"{type(exc).__name__}: {exc}"}


def _script_context(root: Path, motion_ctx: dict[str, Any], action_frame: int) -> dict[str, Any]:
    if motion_ctx.get("status") != "ok":
        return {"status": "unavailable", "reason": "MotionState submotion id unavailable"}
    try:
        slug = str(motion_ctx["char"])
        msid = int(motion_ctx["submotion_id"])
        table = read_mslftsc1_v1(root / "data" / "scripts" / f"{slug}.bin")
        manifest = json.loads((root / "data" / "scripts" / f"{slug}_manifest.json").read_text(encoding="utf-8"))
        kind_names = {int(row["id"]): str(row["name"]) for row in manifest.get("event_kinds", [])}
        entry = next((entry for entry in table.entries if int(entry.msid) == msid), None)
        if entry is None:
            return {"status": "unavailable", "reason": f"no MSLFTSC1 entry for msid {msid}"}
        events = table.events[entry.first_event : entry.first_event + entry.event_count]
        near = [
            {
                "frame": int(ev.frame),
                "kind_id": int(ev.kind_id),
                "kind": kind_names.get(int(ev.kind_id), f"kind:{int(ev.kind_id)}"),
                "payload": ev.payload,
            }
            for ev in events
            if abs(int(ev.frame) - int(action_frame)) <= 3
        ]
        crossed = [
            {
                "frame": int(ev.frame),
                "kind_id": int(ev.kind_id),
                "kind": kind_names.get(int(ev.kind_id), f"kind:{int(ev.kind_id)}"),
                "payload": ev.payload,
            }
            for ev in events
            if int(action_frame) - 1 < int(ev.frame) <= int(action_frame)
        ]
        return {"status": "ok", "msid": msid, "nearby_events": near[:20], "crossed_events": crossed[:20]}
    except Exception as exc:
        return {"status": "unavailable", "reason": f"{type(exc).__name__}: {exc}"}


def _stage_context(root: Path, row: np.void, player: int) -> dict[str, Any]:
    if "stage_id" not in (row.dtype.names or ()) or int(row["stage_id"]) != 32:
        return {"status": "unavailable", "reason": "only FD MSLSTG01 context is currently available"}
    ids: set[int] = set()
    for field in ("ground_id", "wall_id", "ceiling_id"):
        if field in (row.dtype.names or ()):
            value = int(row[field][player])
            if value != 0xFFFF:
                ids.add(value)
    if not ids:
        return {"status": "unavailable", "reason": "no current ground/wall/ceiling id on selected row"}
    try:
        stage = read_mslstg01_v1(root / "data" / "stages" / "bin" / "grnla.bin")
        kind_names = {0: "floor", 1: "ceiling", 2: "right_wall", 3: "left_wall", 4: "dynamic"}
        segments = [
            {
                "line_id": seg.line_id,
                "kind": kind_names.get(seg.kind_id, str(seg.kind_id)),
                "flags": seg.flags,
                "hi_flags": seg.hi_flags,
                "lo_flags": seg.lo_flags,
                "x0": seg.x0,
                "y0": seg.y0,
                "x1": seg.x1,
                "y1": seg.y1,
            }
            for seg in stage.segments
            if int(seg.line_id) in ids
        ]
        return {"status": "ok", "segments": segments}
    except Exception as exc:
        return {"status": "unavailable", "reason": f"{type(exc).__name__}: {exc}"}


def _item_context(root: Path, ref_row: np.void, out_row: np.void | None) -> dict[str, Any]:
    item_types: set[int] = set()
    for row in (ref_row, out_row):
        if row is None:
            continue
        for item in row["items"]:
            if int(item["exists"]) or int(item["type"]):
                item_types.add(int(item["type"]))
    if not item_types:
        return {"status": "unavailable", "reason": "no live item types in selected compare rows"}
    try:
        table = read_mslitar1(root / "data" / "items" / "articles" / "fox_falco.bin")
        manifest = json.loads((root / "data" / "items" / "articles" / "manifest.json").read_text(encoding="utf-8"))
        field_names = {int(row["id"]): str(row["name"]) for row in manifest.get("fields", [])}
        records = [
            {
                "char_id": int(rec.char_id),
                "field": field_names.get(int(rec.field_id), f"field:{int(rec.field_id)}"),
                "u32_value": int(rec.u32_value),
                "f32_value": float(rec.f32_value),
            }
            for rec in table.records
            if int(rec.u32_value) in item_types
        ]
        return {"status": "ok", "item_types": sorted(item_types), "matching_article_records": records[:20]}
    except Exception as exc:
        return {"status": "unavailable", "reason": f"{type(exc).__name__}: {exc}"}


def _part_context(root: Path, row: np.void, player: int, diffs: list[dict[str, Any]]) -> dict[str, Any]:
    relevant = any("throw" in d["field"] or "capture" in d["field"] or d["field"] in {"pos_x", "pos_y"} for d in diffs)
    if not relevant:
        return {"status": "unavailable", "reason": "no cheap attachment/anchor signal in selected diffs"}
    slug = _char_slug(int(row["char_id"][player]))
    if slug is None:
        return {"status": "unavailable", "reason": "unsupported character"}
    try:
        part = read_mslpart1_v1(root / "data" / "model_parts" / f"{slug}.bin")
        return {
            "status": "ok",
            "char": slug,
            "anchor_count": part.anchor_count,
            "anchors": [asdict(anchor) for anchor in part.anchors[:20]],
        }
    except Exception as exc:
        return {"status": "unavailable", "reason": f"{type(exc).__name__}: {exc}"}


def _candidate_owner_hints(
    *,
    first_scored: dict[str, Any] | None,
    grouped: dict[str, list[dict[str, Any]]],
    disruptive_row: disruptive.DisruptiveRow | None,
    motion_ctx: dict[str, Any],
    script_ctx: dict[str, Any],
    stage_ctx: dict[str, Any],
    item_ctx: dict[str, Any],
) -> list[dict[str, Any]]:
    hints: list[dict[str, Any]] = []
    if first_scored is not None:
        subsystem = str(first_scored.get("subsystem", "other"))
        owner = {
            "action/timebase": "Anim/IASA",
            "position/velocity": "Phys",
            "collision/env": "Coll/mpColl",
            "combat/hitlag/hitstun": "Combat",
            "items": "Item",
            "shield/guard": "Shield/Guard",
            "throw/capture": "Throw/Capture",
            "seed/provenance": "Seed/Provenance",
        }.get(subsystem, "Unknown")
        hints.append({"owner": owner, "evidence": f"first scored diff is {first_scored['field']} ({subsystem})"})
    elif disruptive_row is not None and disruptive_row.first_mismatch_field:
        field = disruptive_row.first_mismatch_field
        subsystem = _subsystem_for(field)
        owner = {
            "action/timebase": "Anim/IASA",
            "position/velocity": "Phys",
            "collision/env": "Coll/mpColl",
            "combat/hitlag/hitstun": "Combat",
            "items": "Item",
            "shield/guard": "Shield/Guard",
            "throw/capture": "Throw/Capture",
            "seed/provenance": "Seed/Provenance",
        }.get(subsystem, "Unknown")
        hints.append(
            {
                "owner": owner,
                "evidence": (
                    f"one-step diff is empty; disruptive rollout first diff is {field} "
                    f"at offset {disruptive_row.first_mismatch_offset} ({subsystem})"
                ),
            }
        )
    if motion_ctx.get("status") == "ok":
        classes = ", ".join(motion_ctx.get("classes", [])) or "none"
        hints.append(
            {
                "owner": "MotionState callbacks",
                "evidence": (
                    f"anim={motion_ctx.get('anim_cb')} phys={motion_ctx.get('phys_cb')} "
                    f"coll={motion_ctx.get('coll_cb')} classes={classes}"
                ),
            }
        )
    if script_ctx.get("status") == "ok" and script_ctx.get("crossed_events"):
        kinds = ", ".join(str(ev["kind"]) for ev in script_ctx["crossed_events"][:5])
        hints.append({"owner": "Script/Event", "evidence": f"script event crossed current frame: {kinds}"})
    if stage_ctx.get("status") == "ok":
        hints.append({"owner": "Coll/mpColl", "evidence": "current row has MSLSTG01 segment metadata"})
    if item_ctx.get("status") == "ok":
        hints.append({"owner": "Item", "evidence": "live item type maps to MSLITAR1 article/common data"})
    for subsystem, rows in sorted(grouped.items()):
        if len(rows) >= 3:
            hints.append({"owner": subsystem, "evidence": f"{len(rows)} nearby diffs in {subsystem}"})
    return hints[:10]


def _build_packet(
    *,
    root: Path,
    row: disruptive.DisruptiveRow,
    rows_path: Path,
    profile: ValidationProfile,
    action_names: dict[int, str],
    suite_ucf_enabled: bool | None = None,
    suite_ucf_cardinals_1_0_enabled: bool | None = None,
) -> dict[str, Any]:
    dataset_path = Path(row.dataset)
    if not dataset_path.is_absolute():
        dataset_path = root / dataset_path
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(row.record)
    if record < 0 or record >= int(samples.shape[0]):
        raise SystemExit(f"error: row record {record} out of range for {dataset_path}")
    player = int(row.player if row.player >= 0 else max(0, row.first_mismatch_player))
    num_players = int(ds.header["num_players"])
    players = tuple(range(num_players))
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    out, sim_error = _simulate_one_step(
        samples,
        record,
        num_players,
        ucf_enabled=suite_ucf_enabled,
        ucf_cardinals_1_0_enabled=suite_ucf_cardinals_1_0_enabled,
    )
    strict = get_validation_profile("strict")
    first_offset = int(row.first_mismatch_offset)
    rollout_ref_index = record + first_offset - 1
    rollout_out: np.void | None = None
    rollout_error: str | None = None
    rollout_ref: np.void | None = None
    rollout_profile_diffs: list[dict[str, Any]] = []
    rollout_strict_diffs: list[dict[str, Any]] = []
    rollout_grouped: dict[str, list[dict[str, Any]]] = {}
    rollout_first_scored: dict[str, Any] | None = None
    rollout_first_strict: dict[str, Any] | None = None
    if 0 <= rollout_ref_index < int(samples.shape[0]):
        rollout_ref = samples[rollout_ref_index]["ref_t1"]
        rollout_out, rollout_error = _simulate_rollout_to_offset(
            samples,
            record,
            first_offset,
            num_players,
            ucf_enabled=suite_ucf_enabled,
            ucf_cardinals_1_0_enabled=suite_ucf_cardinals_1_0_enabled,
        )
        if rollout_out is not None:
            rollout_profile_diffs, rollout_grouped, rollout_first_scored = _profile_diff(
                out_row=rollout_out, ref_row=rollout_ref, players=players, profile=profile
            )
            rollout_strict_diffs, _rollout_strict_grouped, rollout_first_strict = _profile_diff(
                out_row=rollout_out, ref_row=rollout_ref, players=players, profile=strict
            )
    else:
        rollout_error = f"rollout reference record {rollout_ref_index} outside dataset"

    if out is not None:
        profile_diffs, grouped, first_scored = _profile_diff(
            out_row=out, ref_row=ref, players=players, profile=profile
        )
        strict_diffs, _strict_grouped, first_strict = _profile_diff(
            out_row=out, ref_row=ref, players=players, profile=strict
        )
    else:
        profile_diffs, strict_diffs, grouped, first_scored, first_strict = [], [], {}, None, None

    motion_ctx = _motion_state_context(root, seed, player, action_names)
    script_ctx = _script_context(root, motion_ctx, int(seed["action_frame"][player]))
    stage_ctx = _stage_context(root, seed, player)
    item_ctx = _item_context(root, ref, out)
    part_ctx = _part_context(root, seed, player, profile_diffs)
    context_rows = {}
    for label, idx in (("previous", record - 1), ("current", record), ("next", record + 1)):
        if 0 <= idx < int(samples.shape[0]):
            context_rows[label] = {
                "record": int(idx),
                "seed": _compact_row(samples[idx]["seed_t"], player=player, action_names=action_names),
                "ref_t1": _compact_row(samples[idx]["ref_t1"], player=player, action_names=action_names),
                "items": _item_snapshot(samples[idx]["ref_t1"]),
            }
        else:
            context_rows[label] = {"status": "unavailable", "reason": "outside dataset bounds"}

    packet = {
        "tool": "tools.eval.next_desync_investigation",
        "rows_source": str(rows_path),
        "profile": profile.name,
        "candidate": asdict(row),
        "dataset_path": _repo_relative(dataset_path, root),
        "record": record,
        "player": player,
        "simulation_config": {
            "ucf_enabled": suite_ucf_enabled,
            "ucf_cardinals_1_0_enabled": suite_ucf_cardinals_1_0_enabled,
        },
        "frame_context": {
            "seed_frame": int(seed["frame_id"]),
            "ref_frame": int(ref["frame_id"]),
            "disruptive_horizon": int(row.horizon),
            "disruptive_ref_frame": int(row.ref_frame),
            "first_mismatch_offset": int(row.first_mismatch_offset),
        },
        "one_step": {
            "seed": _compact_row(seed, player=player, action_names=action_names),
            "reference": _compact_row(ref, player=player, action_names=action_names),
            "sim_output": (
                _compact_row(out, player=player, action_names=action_names)
                if out is not None
                else {"status": "unavailable", "reason": sim_error}
            ),
            "active_profile_diff": profile_diffs,
            "strict_profile_diff": strict_diffs,
            "first_differing_scored_field": first_scored or {"status": "none"},
            "first_differing_strict_field": first_strict or {"status": "none"},
            "changed_fields_by_subsystem": grouped,
        },
        "rollout_first_mismatch": {
            "offset": first_offset,
            "compare_record": int(rollout_ref_index),
            "next_seed_record": int(record + first_offset),
            "candidate_field": row.first_mismatch_field,
            "candidate_player": int(row.first_mismatch_player),
            "candidate_subindex": int(row.first_mismatch_subindex),
            "candidate_out": row.first_out,
            "candidate_ref": row.first_ref,
            "reference": (
                _compact_row(rollout_ref, player=player, action_names=action_names)
                if rollout_ref is not None
                else {"status": "unavailable", "reason": rollout_error}
            ),
            "sim_output": (
                _compact_row(rollout_out, player=player, action_names=action_names)
                if rollout_out is not None
                else {"status": "unavailable", "reason": rollout_error}
            ),
            "active_profile_diff": rollout_profile_diffs,
            "strict_profile_diff": rollout_strict_diffs,
            "first_differing_scored_field": rollout_first_scored
            or (
                {
                    "field": row.first_mismatch_field,
                    "player": int(row.first_mismatch_player),
                    "subindex": int(row.first_mismatch_subindex),
                    "out": row.first_out,
                    "ref": row.first_ref,
                    "source": "disruptive_rows",
                }
                if row.first_mismatch_field
                else {"status": "none"}
            ),
            "first_differing_strict_field": rollout_first_strict or {"status": "none"},
            "changed_fields_by_subsystem": rollout_grouped,
        },
        "replay_rows": context_rows,
        "data_backed_context": {
            "motion_state": motion_ctx,
            "script_events": script_ctx,
            "stage_segment": stage_ctx,
            "item_article": item_ctx,
            "part_anchor": part_ctx,
        },
        "candidate_owner_hints": _candidate_owner_hints(
            first_scored=first_scored or rollout_first_scored,
            grouped=grouped or rollout_grouped,
            disruptive_row=row,
            motion_ctx=motion_ctx,
            script_ctx=script_ctx,
            stage_ctx=stage_ctx,
            item_ctx=item_ctx,
        ),
    }
    return packet


def _packet_markdown(packet: dict[str, Any]) -> str:
    candidate = packet["candidate"]
    lines = [
        "# Next Desync Investigation Packet",
        "",
        f"- dataset: `{packet['dataset_path']}`",
        f"- record: `{packet['record']}` player: `{packet['player']}`",
        f"- disruptive rank/source: `{packet['rows_source']}`",
        f"- score: `{float(candidate['score_total']):.3f}` horizon: `{candidate['horizon']}`",
        f"- family: `{candidate['family_id']}` field_cluster: `{candidate['field_cluster']}`",
        f"- first rollout mismatch: `{candidate['first_mismatch_field']}` "
        f"offset `{candidate['first_mismatch_offset']}` out `{candidate['first_out']}` ref `{candidate['first_ref']}`",
        "",
        "## One-Step First Diffs",
        "",
        f"- scored: `{packet['one_step']['first_differing_scored_field']}`",
        f"- strict: `{packet['one_step']['first_differing_strict_field']}`",
        "",
        "## Rollout First Mismatch",
        "",
        f"- offset: `{packet['rollout_first_mismatch']['offset']}` "
        f"compare record: `{packet['rollout_first_mismatch']['compare_record']}`",
        f"- disruptive field: `{packet['rollout_first_mismatch']['candidate_field']}` "
        f"out `{packet['rollout_first_mismatch']['candidate_out']}` "
        f"ref `{packet['rollout_first_mismatch']['candidate_ref']}`",
        f"- scored: `{packet['rollout_first_mismatch']['first_differing_scored_field']}`",
        f"- strict: `{packet['rollout_first_mismatch']['first_differing_strict_field']}`",
        "",
        "## Candidate Owner Hints",
        "",
    ]
    for hint in packet.get("candidate_owner_hints", []):
        lines.append(f"- `{hint['owner']}`: {hint['evidence']}")
    lines.extend(["", "## Data-Backed Context", ""])
    ctx = packet["data_backed_context"]
    motion = ctx["motion_state"]
    if motion.get("status") == "ok":
        lines.append(
            f"- MotionState: action `{motion['action_name']}` msid `{motion['submotion_id']}` "
            f"anim `{motion['anim_cb']}` phys `{motion['phys_cb']}` coll `{motion['coll_cb']}` "
            f"classes `{', '.join(motion.get('classes', [])) or 'none'}`"
        )
    else:
        lines.append(f"- MotionState: unavailable ({motion.get('reason')})")
    script = ctx["script_events"]
    if script.get("status") == "ok":
        crossed = ", ".join(f"{ev['frame']}:{ev['kind']}" for ev in script.get("crossed_events", [])[:8])
        nearby = ", ".join(f"{ev['frame']}:{ev['kind']}" for ev in script.get("nearby_events", [])[:8])
        lines.append(f"- Script crossed: {crossed or 'none'}")
        lines.append(f"- Script nearby: {nearby or 'none'}")
    else:
        lines.append(f"- Script: unavailable ({script.get('reason')})")
    for key in ("stage_segment", "item_article", "part_anchor"):
        value = ctx[key]
        lines.append(f"- {key}: {value.get('status')} {value.get('reason', '')}".rstrip())
    lines.extend(["", "## Changed Fields By Subsystem", ""])
    for subsystem, rows in packet["one_step"].get("changed_fields_by_subsystem", {}).items():
        fields = ", ".join(f"{row['field']}[p{row['player']}]" for row in rows[:10])
        lines.append(f"- one-step `{subsystem}`: {fields}")
    for subsystem, rows in packet["rollout_first_mismatch"].get("changed_fields_by_subsystem", {}).items():
        fields = ", ".join(f"{row['field']}[p{row['player']}]" for row in rows[:10])
        lines.append(f"- rollout `{subsystem}`: {fields}")
    lines.append("")
    return "\n".join(lines)


def _summary_markdown(
    *,
    rows_source: Path,
    reuse_status: str,
    ranked: list[dict[str, Any]],
    clusters: list[NextCluster],
    packet_path: Path,
    profile: str,
) -> str:
    lines = [
        "# Next Desync Investigation Summary",
        "",
        f"- profile: `{profile}`",
        f"- rows source: `{rows_source}` ({reuse_status})",
        f"- ranked rows: `{len(ranked)}`",
        f"- clusters: `{len(clusters)}`",
        f"- top packet: `{packet_path}`",
        "",
        "## Top Clusters",
        "",
    ]
    for cluster in clusters[:10]:
        lines.append(
            f"{cluster.rank}. score `{cluster.score_sum:.1f}` freq `{cluster.frequency}` "
            f"`{cluster.family_id}` `{cluster.action_family}` `{Path(cluster.dataset).name}` "
            f"records `{cluster.record_min}..{cluster.record_max}` first `{cluster.example_first_field}`"
        )
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    ap = argparse.ArgumentParser(description="Build the next disruptive-desync investigation packet.")
    ap.add_argument("--suite", required=True, type=Path)
    ap.add_argument("--datasets-dir", default="datasets")
    ap.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    ap.add_argument("--refresh", action="store_true", help="Force rerun of disruptive rollout rows.")
    ap.add_argument("--rows-in", type=Path, default=None, help="Explicit existing disruptive rows.tsv.")
    ap.add_argument("--profile", default="rl1_gameplay", choices=validation_profile_names())
    ap.add_argument("--horizons", default=DEFAULT_HORIZONS)
    ap.add_argument("--top", type=int, default=DEFAULT_TOP)
    ap.add_argument("--top-packets", type=int, default=1)
    ap.add_argument("--batch-size", type=int, default=DEFAULT_BATCH_SIZE)
    ap.add_argument("--workers", type=int, default=1)
    ap.add_argument("--chunk-records", type=int, default=DEFAULT_CHUNK_RECORDS)
    ap.add_argument("--max-records", type=int, default=0)
    ap.add_argument("--stride", type=int, default=1)
    ap.add_argument("--fixture-skeleton", action="store_true")
    args = ap.parse_args()

    root = repo_root()
    suite_path = args.suite if args.suite.is_absolute() else root / args.suite
    suite = load_suite(suite_path)
    out_dir = args.out_dir if args.out_dir.is_absolute() else root / args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    profile = get_validation_profile(args.profile)
    action_names = load_action_id_names()

    rows_path, rows, reuse_status = _load_or_refresh_rows(
        root=root,
        suite_path=suite_path,
        suite_name=suite.name,
        datasets_dir=str(args.datasets_dir),
        out_dir=out_dir,
        profile=profile.name,
        horizons=str(args.horizons),
        top=max(1, int(args.top)),
        batch_size=max(1, int(args.batch_size)),
        workers=max(1, int(args.workers)),
        chunk_records=max(1, int(args.chunk_records)),
        max_records=int(args.max_records),
        stride=max(1, int(args.stride)),
        refresh=bool(args.refresh),
        rows_in=args.rows_in,
    )
    if not rows:
        raise SystemExit(f"error: no disruptive rows available from {rows_path}")

    ranked, clusters = _cluster_rows(rows, action_names)
    if not clusters:
        raise SystemExit("error: no clusters built")
    cluster_by_id = {cluster.cluster_id: cluster for cluster in clusters}
    for row in ranked:
        row["next_cluster_rank"] = cluster_by_id[row["next_cluster_id"]].rank

    ranked_cols = [
        "rank",
        "next_cluster_rank",
        "next_cluster_id",
        "score_total",
        "score_discrete",
        "score_float",
        "score_item",
        "suite",
        "dataset",
        "record",
        "seed_frame",
        "horizon",
        "ref_frame",
        "player",
        "family_id",
        "action_family",
        "field_cluster",
        "first_mismatch_offset",
        "first_mismatch_field",
        "first_out",
        "first_ref",
        "seed_action_id",
        "out_action_id",
        "ref_action_id",
    ]
    cluster_cols = list(asdict(clusters[0]).keys())
    _write_tsv(out_dir / "ranked.tsv", ranked, ranked_cols)
    _write_tsv(out_dir / "clusters.tsv", [asdict(c) for c in clusters], cluster_cols)

    packet_count = min(max(1, int(args.top_packets)), len(clusters))
    top_packet_path = out_dir / "top_packet.json"
    first_packet: dict[str, Any] | None = None
    for packet_idx, cluster in enumerate(clusters[:packet_count], start=1):
        top_ranked = next(row for row in ranked if row["rank"] == cluster.example_rank)
        top_row = disruptive.DisruptiveRow(
            **{column: top_ranked[column] for column in disruptive.ROW_COLUMNS}
        )
        packet = _build_packet(
            root=root,
            row=top_row,
            rows_path=rows_path,
            profile=profile,
            action_names=action_names,
            suite_ucf_enabled=suite.ucf_enabled,
            suite_ucf_cardinals_1_0_enabled=suite.ucf_cardinals_1_0_enabled,
        )
        packet["packet_index"] = int(packet_idx)
        packet["cluster"] = asdict(cluster)
        numbered_json = out_dir / f"packet_{packet_idx:03d}.json"
        numbered_md = out_dir / f"packet_{packet_idx:03d}.md"
        numbered_json.write_text(json.dumps(packet, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        numbered_md.write_text(_packet_markdown(packet), encoding="utf-8")
        if first_packet is None:
            first_packet = packet
            top_packet_path.write_text(json.dumps(packet, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            (out_dir / "top_packet.md").write_text(_packet_markdown(packet), encoding="utf-8")

    if args.fixture_skeleton:
        packet = first_packet
        if packet is None:
            raise SystemExit("error: no packet generated")
        skeleton = {
            "source": str(top_packet_path),
            "dataset": packet["dataset_path"],
            "record_window": [
                max(0, int(packet["record"]) - 2),
                int(packet["record"]),
                int(packet["record"]) + 2,
            ],
            "note": "Skeleton only. Fill compact seed/input/reference fields before committing a fixture.",
        }
        (out_dir / "fixture_skeleton.json").write_text(
            json.dumps(skeleton, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )

    summary = _summary_markdown(
        rows_source=rows_path,
        reuse_status=reuse_status,
        ranked=ranked,
        clusters=clusters,
        packet_path=top_packet_path,
        profile=profile.name,
    )
    (out_dir / "summary.md").write_text(summary, encoding="utf-8")

    print(f"wrote: {out_dir / 'summary.md'}")
    print(f"wrote: {out_dir / 'ranked.tsv'}")
    print(f"wrote: {out_dir / 'clusters.tsv'}")
    print(f"wrote: {top_packet_path}")
    print(f"wrote: {out_dir / 'top_packet.md'}")
    if packet_count > 1:
        print(f"wrote: {packet_count} numbered packets")


if __name__ == "__main__":
    main()
