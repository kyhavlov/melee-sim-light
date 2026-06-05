from __future__ import annotations

"""Collect top absolute float-error offenders for one-step or rollout suite evaluation.

This is a deterministic triage utility intended for guardrail checks and row-level debugging.
"""

import argparse
import heapq
import importlib
import json
import sys
from collections.abc import Iterable
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, Dataset, read_dataset, read_dataset_window
from tools.eval.discrete_compare_lanes import compile_discrete_compare_lanes, first_mismatch_field
from tools.eval.run_longest_rollout_streaks import _parse_players
from tools.eval.validation_profile import get_validation_profile, validation_profile_names
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp
from tools.slippi.slpz import resolve_replay_path
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


DEFAULT_FLOAT_FIELDS = (
    "pos_x",
    "pos_y",
    "speed_air_x_self",
    "speed_y_self",
    "speed_ground_x_self",
)
DEFAULT_DISCRETE_FIELDS = ("action_id", "animation_index", "on_ground", "hitlag", "hitstun", "state_flags")


@dataclass(frozen=True)
class FloatOffender:
    field: str
    abs_err: float
    dataset: str
    record: int
    p: int
    seed_frame: int
    ref_frame: int
    seed: float
    out: float
    ref: float
    seed_action_id: int
    out_action_id: int
    ref_action_id: int
    seed_action_frame: int
    out_action_frame: int
    ref_action_frame: int
    attempt: str | None = None
    seeded_retry: bool | None = None
    discrete_state_matches: bool | None = None
    streak_start_record: int | None = None
    streak_len: int | None = None


def _load_binding():
    return importlib.import_module("msl_binding")


def _dataset_rel(root: Path, dataset_path: Path) -> str:
    p = dataset_path.resolve()
    try:
        return str(p.relative_to(root).as_posix())
    except ValueError:
        return str(p.as_posix())


def _field_names(values: Iterable[str]) -> tuple[str, ...]:
    fields = tuple(dict.fromkeys(str(f).strip() for f in values if str(f).strip()))
    if not fields:
        raise SystemExit("error: at least one float field is required")
    missing = [field for field in fields if field not in COMPARE_DTYPE.fields]
    if missing:
        raise SystemExit(f"error: unknown compare fields: {', '.join(missing)}")
    not_float = []
    for field in fields:
        base = COMPARE_DTYPE.fields[field][0]
        if base.fields is not None or not np.issubdtype(base.base, np.floating):
            not_float.append(field)
    if not_float:
        raise SystemExit(f"error: non-float compare fields: {', '.join(not_float)}")
    return fields


def _would_enter_top(
    heap: list[tuple[float, str, int, int, str, int, float, float, float, FloatOffender]],
    *,
    top: int,
    abs_err: float,
) -> bool:
    if abs_err <= 0.0:
        return False
    if len(heap) < top:
        return True
    return abs_err > heap[0][0]


def _push_top(
    heap: list[tuple[float, str, int, int, str, int, float, float, float, FloatOffender]],
    *,
    top: int,
    row: FloatOffender,
) -> None:
    key = float(row.abs_err)
    # Deterministic tie-breakers avoid comparing dataclass objects in heap internals.
    item = (
        key,
        str(row.dataset),
        int(row.record),
        int(row.p),
        "" if row.attempt is None else str(row.attempt),
        0 if row.seeded_retry is None else int(bool(row.seeded_retry)),
        float(row.seed),
        float(row.out),
        float(row.ref),
        row,
    )
    if len(heap) < top:
        heapq.heappush(heap, item)
        return
    if key > heap[0][0]:
        heapq.heapreplace(heap, item)


def _sorted_top_rows(
    heaps: dict[str, list[tuple[float, str, int, int, str, int, float, float, float, FloatOffender]]],
) -> dict[str, list[FloatOffender]]:
    out: dict[str, list[FloatOffender]] = {}
    for field, heap in heaps.items():
        rows = [row for (_err, _ds, _rec, _p, _attempt, _seeded, _seed, _out, _ref, row) in heap]
        rows.sort(key=lambda r: (-r.abs_err, r.dataset, r.record, r.p))
        out[field] = rows
    return out


def collect_dataset_top_float_offenders(
    *,
    dataset_path: Path,
    ds: Dataset | None = None,
    dataset_label: str | None = None,
    fields: tuple[str, ...],
    chunk: int,
    top: int,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
) -> dict[str, list[FloatOffender]]:
    if ds is None:
        ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    num_players = int(ds.header["num_players"])

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    init_kwargs = {"batch_size": min(chunk, num_records), "num_players": num_players}
    if ucf_enabled is not None:
        init_kwargs["ucf_enabled"] = int(bool(ucf_enabled))
    if ucf_cardinals_1_0_enabled is not None:
        init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(ucf_cardinals_1_0_enabled))

    handle = binding.init(**init_kwargs)

    seed_bytes = np.empty((min(chunk, num_records), seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((min(chunk, num_records), input_stride), dtype=np.uint8)
    input_bytes = np.empty((min(chunk, num_records), input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((min(chunk, num_records), compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    heaps: dict[str, list[tuple[float, str, int, int, str, int, float, float, float, FloatOffender]]] = {
        f: [] for f in fields
    }
    offset = 0
    ds_rel = dataset_label if dataset_label is not None else _dataset_rel(repo_root(), dataset_path)
    active = slice(0, num_players)

    try:
        while offset < num_records:
            chunk_n = min(chunk, num_records - offset)
            if chunk_n != seed_bytes.shape[0]:
                binding.destroy(handle)
                init_kwargs = {"batch_size": chunk_n, "num_players": num_players}
                if ucf_enabled is not None:
                    init_kwargs["ucf_enabled"] = int(bool(ucf_enabled))
                if ucf_cardinals_1_0_enabled is not None:
                    init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(ucf_cardinals_1_0_enabled))
                handle = binding.init(**init_kwargs)
                seed_bytes = np.empty((chunk_n, seed_stride), dtype=np.uint8)
                prev_input_bytes = np.empty((chunk_n, input_stride), dtype=np.uint8)
                input_bytes = np.empty((chunk_n, input_stride), dtype=np.uint8)
                out_compare_bytes = np.empty((chunk_n, compare_stride), dtype=np.uint8)
                out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

            chunk_view = samples[offset : offset + chunk_n]
            seed_bytes[:] = np.frombuffer(chunk_view["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                chunk_n, seed_stride
            )
            prev_input_bytes[:] = np.frombuffer(
                chunk_view["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(chunk_n, input_stride)
            input_bytes[:] = np.frombuffer(chunk_view["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                chunk_n, input_stride
            )

            binding.reseed_seed(handle, seed_bytes)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)

            seed = chunk_view["seed_t"]
            ref = chunk_view["ref_t1"]

            for field in fields:
                out_arr = out_view[field][:, active]
                ref_arr = ref[field][:, active]
                abs_err = np.abs(out_arr - ref_arr)
                nz = np.argwhere(abs_err > 0.0)
                for ri, p in nz:
                    r = int(ri)
                    pp = int(p)
                    gi = int(offset + r)
                    err = float(abs_err[r, pp])
                    if not _would_enter_top(heaps[field], top=top, abs_err=err):
                        continue
                    row = FloatOffender(
                        field=field,
                        abs_err=err,
                        dataset=ds_rel,
                        record=gi,
                        p=pp,
                        seed_frame=int(seed["frame_id"][r]),
                        ref_frame=int(ref["frame_id"][r]),
                        seed=float(seed[field][r, pp]),
                        out=float(out_arr[r, pp]),
                        ref=float(ref_arr[r, pp]),
                        seed_action_id=int(seed["action_id"][r, pp]),
                        out_action_id=int(out_view["action_id"][r, pp]),
                        ref_action_id=int(ref["action_id"][r, pp]),
                        seed_action_frame=int(seed["action_frame"][r, pp]),
                        out_action_frame=int(out_view["action_frame"][r, pp]),
                        ref_action_frame=int(ref["action_frame"][r, pp]),
                    )
                    _push_top(heaps[field], top=top, row=row)

            offset += chunk_n
    finally:
        binding.destroy(handle)

    return _sorted_top_rows(heaps)


def collect_dataset_top_rollout_float_offenders(
    *,
    dataset_path: Path,
    ds: Dataset | None = None,
    dataset_label: str | None = None,
    fields: tuple[str, ...],
    players: tuple[int, ...] | None = None,
    top: int,
    max_records: int,
    threshold: float,
    discrete_fields: tuple[str, ...],
    profile: str,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
) -> dict[str, list[FloatOffender]]:
    if ds is None:
        ds = read_dataset_window(str(dataset_path), 0, sys.maxsize)
    samples = ds.samples
    num_records_total = int(samples.shape[0])
    num_players = int(ds.header["num_players"])
    if players is None:
        players = tuple(range(num_players))

    n = num_records_total
    if max_records > 0:
        n = min(n, int(max_records))

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    init_kwargs = {"batch_size": 1, "num_players": num_players}
    if ucf_enabled is not None:
        init_kwargs["ucf_enabled"] = int(bool(ucf_enabled))
    if ucf_cardinals_1_0_enabled is not None:
        init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(ucf_cardinals_1_0_enabled))
    handle = binding.init(**init_kwargs)

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(num_records_total, sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    seed = samples["seed_t"]
    ref = samples["ref_t1"]
    compare_lanes = compile_discrete_compare_lanes(
        discrete_fields,
        players,
        profile=get_validation_profile(profile),
    )
    ds_rel = dataset_label if dataset_label is not None else _dataset_rel(repo_root(), dataset_path)
    heaps: dict[str, list[tuple[float, str, int, int, str, int, float, float, float, FloatOffender]]] = {
        f: [] for f in fields
    }

    def reseed_at(j: int) -> None:
        seed_bytes[0, :] = samples_u8[j, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)

    def step(j: int) -> None:
        seed_bytes[0, :] = samples_u8[j, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
        binding.step_input_replay_frame_rng(handle, seed_bytes, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

    def discrete_matches(j: int) -> bool:
        return (
            first_mismatch_field(out_row=out_view[0], ref_row=ref[j], lanes=compare_lanes)
            is None
        )

    def inspect_float_offenders(
        j: int,
        *,
        attempt: str,
        seeded_retry: bool,
        row_discrete_matches: bool,
        streak_start_record: int,
        streak_len: int,
    ) -> None:
        for field in fields:
            out_arr = out_view[0][field]
            ref_arr = ref[j][field]
            seed_arr = seed[j][field]
            for p in players:
                pp = int(p)
                err = abs(float(out_arr[pp]) - float(ref_arr[pp]))
                if err < threshold or not _would_enter_top(heaps[field], top=top, abs_err=err):
                    continue
                row = FloatOffender(
                    field=field,
                    abs_err=err,
                    dataset=ds_rel,
                    record=int(j),
                    p=pp,
                    seed_frame=int(seed["frame_id"][j]),
                    ref_frame=int(ref["frame_id"][j]),
                    seed=float(seed_arr[pp]),
                    out=float(out_arr[pp]),
                    ref=float(ref_arr[pp]),
                    seed_action_id=int(seed["action_id"][j, pp]),
                    out_action_id=int(out_view[0]["action_id"][pp]),
                    ref_action_id=int(ref["action_id"][j, pp]),
                    seed_action_frame=int(seed["action_frame"][j, pp]),
                    out_action_frame=int(out_view[0]["action_frame"][pp]),
                    ref_action_frame=int(ref["action_frame"][j, pp]),
                    attempt=str(attempt),
                    seeded_retry=bool(seeded_retry),
                    discrete_state_matches=bool(row_discrete_matches),
                    streak_start_record=int(streak_start_record),
                    streak_len=int(streak_len),
                )
                _push_top(heaps[field], top=top, row=row)

    try:
        cur_start = 0
        cur_len = 0
        needs_seed = True
        for j in range(n):
            if needs_seed:
                reseed_at(cur_start)
                needs_seed = False

            step(j)
            row_discrete_matches = discrete_matches(j)
            inspect_float_offenders(
                j,
                attempt="free_run",
                seeded_retry=False,
                row_discrete_matches=row_discrete_matches,
                streak_start_record=cur_start,
                streak_len=cur_len,
            )
            if row_discrete_matches:
                cur_len += 1
                continue

            # Keep rollout-float triage aligned with canonical rollout streaking: report the
            # failed free-run row, then retry from the real seed at the same record for the next
            # streak. If that retry also fails, advance to j+1.
            cur_start = j
            cur_len = 0
            reseed_at(j)
            step(j)
            retry_discrete_matches = discrete_matches(j)
            inspect_float_offenders(
                j,
                attempt="seeded_retry",
                seeded_retry=True,
                row_discrete_matches=retry_discrete_matches,
                streak_start_record=cur_start,
                streak_len=0,
            )
            if retry_discrete_matches:
                cur_len = 1
            else:
                cur_start = j + 1
                cur_len = 0
                needs_seed = True
    finally:
        try:
            binding.destroy(handle)
        except Exception:
            pass

    return _sorted_top_rows(heaps)


def collect_suite_top_float_offenders(
    *,
    suite: Path,
    datasets_dir: str,
    fields: tuple[str, ...],
    chunk: int,
    top: int,
    mode: str = "one-step",
    players_csv: str | None = None,
    max_records: int = 0,
    threshold: float = 0.0,
    profile: str = "rl1_gameplay",
    in_memory_preprocess: bool = False,
    dataset_filter: str = "",
) -> dict[str, object]:
    root = repo_root()
    suite_path = (root / suite).resolve()
    suite_obj = load_suite(suite_path)

    heaps: dict[str, list[tuple[float, str, int, int, str, int, float, float, float, FloatOffender]]] = {
        f: [] for f in fields
    }

    for entry in suite_obj.replays:
        ds = dataset_path_for_suite_replay(
            suite_name=suite_obj.name,
            replay_rel_path=entry.replay,
            datasets_dir=datasets_dir,
        )
        ds_rel = _dataset_rel(root, ds)
        if dataset_filter and dataset_filter not in ds_rel and dataset_filter not in entry.replay:
            continue
        dataset_obj: Dataset | None = None
        if in_memory_preprocess:
            dataset_obj = build_dataset_from_slp(
                slp_path=str(resolve_replay_path((root / entry.replay).resolve())),
                ports=[int(p) for p in entry.ports],
                ucf_enabled=bool(suite_obj.ucf_enabled),
                ucf_cardinals_1_0_enabled=bool(suite_obj.ucf_cardinals_1_0_enabled),
            )
        elif not ds.exists():
            raise SystemExit(
                f"error: missing dataset {ds_rel}; run preprocess or pass --in-memory-preprocess"
            )
        if mode == "one-step":
            per_ds = collect_dataset_top_float_offenders(
                dataset_path=ds,
                ds=dataset_obj,
                dataset_label=ds_rel,
                fields=fields,
                chunk=chunk,
                top=top,
                ucf_enabled=bool(suite_obj.ucf_enabled),
                ucf_cardinals_1_0_enabled=bool(suite_obj.ucf_cardinals_1_0_enabled),
            )
        elif mode == "rollout":
            num_players = (
                int(dataset_obj.header["num_players"])
                if dataset_obj is not None
                else int(read_dataset_window(str(ds), 0, 1).header["num_players"])
            )
            per_ds = collect_dataset_top_rollout_float_offenders(
                dataset_path=ds,
                ds=dataset_obj,
                dataset_label=ds_rel,
                fields=fields,
                players=_parse_players(players_csv, num_players=num_players),
                top=top,
                max_records=max_records,
                threshold=threshold,
                discrete_fields=DEFAULT_DISCRETE_FIELDS,
                profile=profile,
                ucf_enabled=bool(suite_obj.ucf_enabled),
                ucf_cardinals_1_0_enabled=bool(suite_obj.ucf_cardinals_1_0_enabled),
            )
        else:
            raise ValueError(f"unknown mode {mode!r}")
        for field in fields:
            for row in per_ds[field]:
                _push_top(heaps[field], top=top, row=row)

    top_rows: dict[str, list[dict[str, object]]] = {}
    keys: dict[str, list[dict[str, object]]] = {}
    for field in fields:
        rows = [row for (_err, _ds, _rec, _p, _attempt, _seeded, _seed, _out, _ref, row) in heaps[field]]
        rows.sort(key=lambda r: (-r.abs_err, r.dataset, r.record, r.p))
        top_rows[field] = [asdict(r) for r in rows]
        keys[field] = [
            {"dataset": r.dataset, "record": int(r.record), "p": int(r.p)}
            for r in rows
        ]

    return {
        "suite": suite_obj.name,
        "suite_path": str(suite.as_posix()),
        "datasets_dir": str(datasets_dir),
        "mode": str(mode),
        "fields": list(fields),
        "top_n": int(top),
        "top_rows": top_rows,
        "keys": keys,
    }


def _write_tsv(path: Path, payload: dict[str, object]) -> None:
    triage_root = (repo_root() / "reports" / "triage").resolve()
    out_path = path.resolve()
    try:
        out_path.relative_to(triage_root)
    except ValueError as exc:
        raise SystemExit(f"error: --tsv-out must be under {triage_root}") from exc
    path.parent.mkdir(parents=True, exist_ok=True)
    cols = [
        "dataset",
        "record",
        "player",
        "field",
        "abs_err",
        "seed",
        "out",
        "ref",
        "seed_action_id",
        "out_action_id",
        "ref_action_id",
        "seed_action_frame",
        "out_action_frame",
        "ref_action_frame",
        "attempt",
        "seeded_retry",
        "discrete_state_matches",
        "streak_start_record",
        "streak_len",
    ]
    rows: list[dict[str, object]] = []
    top_rows = payload["top_rows"]
    assert isinstance(top_rows, dict)
    for field_rows in top_rows.values():
        assert isinstance(field_rows, list)
        rows.extend(field_rows)
    rows.sort(key=lambda r: (-float(r["abs_err"]), str(r["dataset"]), int(r["record"]), int(r["p"])))
    with path.open("w", encoding="utf-8", newline="") as f:
        f.write("\t".join(cols) + "\n")
        for row in rows:
            values = {
                "dataset": row["dataset"],
                "record": row["record"],
                "player": row["p"],
                "field": row["field"],
                "abs_err": f"{float(row['abs_err']):.9g}",
                "seed": f"{float(row['seed']):.9g}",
                "out": f"{float(row['out']):.9g}",
                "ref": f"{float(row['ref']):.9g}",
                "seed_action_id": row["seed_action_id"],
                "out_action_id": row["out_action_id"],
                "ref_action_id": row["ref_action_id"],
                "seed_action_frame": row["seed_action_frame"],
                "out_action_frame": row["out_action_frame"],
                "ref_action_frame": row["ref_action_frame"],
                "attempt": row.get("attempt"),
                "seeded_retry": row.get("seeded_retry"),
                "discrete_state_matches": row.get("discrete_state_matches"),
                "streak_start_record": row.get("streak_start_record"),
                "streak_len": row.get("streak_len"),
            }
            f.write("\t".join("" if values[c] is None else str(values[c]) for c in cols) + "\n")


def main() -> None:
    ap = argparse.ArgumentParser(description="Collect suite top absolute float offenders.")
    ap.add_argument("--suite", type=Path, required=True)
    ap.add_argument("--datasets-dir", default="datasets")
    ap.add_argument("--mode", choices=("one-step", "rollout"), default="one-step")
    ap.add_argument("--field", action="append", default=[])
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--players", default=None, help="Rollout 0-based player indices; default all players.")
    ap.add_argument("--max-records", type=int, default=0, help="Rollout record cap; 0 means no cap.")
    ap.add_argument("--threshold", type=float, default=0.0, help="Minimum absolute float error to report.")
    ap.add_argument("--profile", default="rl1_gameplay", choices=validation_profile_names())
    ap.add_argument("--dataset-filter", default="", help="Optional substring filter on dataset/replay path.")
    ap.add_argument(
        "--in-memory-preprocess",
        action="store_true",
        help="Build suite replays directly from .slp/.slpz instead of reading cached .msl datasets.",
    )
    ap.add_argument("--json-out", type=Path, default=None)
    ap.add_argument("--tsv-out", type=Path, default=None)
    args = ap.parse_args()

    fields = _field_names(args.field if args.field else DEFAULT_FLOAT_FIELDS)

    payload = collect_suite_top_float_offenders(
        suite=args.suite,
        datasets_dir=str(args.datasets_dir),
        fields=fields,
        chunk=int(args.chunk),
        top=max(1, int(args.top)),
        mode=str(args.mode),
        players_csv=args.players,
        max_records=int(args.max_records),
        threshold=float(args.threshold),
        profile=str(args.profile),
        in_memory_preprocess=bool(args.in_memory_preprocess),
        dataset_filter=str(args.dataset_filter),
    )

    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.tsv_out is not None:
        _write_tsv(args.tsv_out, payload)

    print(
        f"suite={payload['suite']} mode={payload['mode']} fields={','.join(fields)} top={payload['top_n']} "
        f"datasets_dir={payload['datasets_dir']}"
    )
    for field in fields:
        rows = payload["top_rows"][field]
        print(f"== {field} top {len(rows)} ==")
        for i, row in enumerate(rows, start=1):
            disc = row.get("discrete_state_matches")
            disc_text = "" if disc is None else f" discrete_match={int(bool(disc))}"
            attempt = row.get("attempt")
            attempt_text = "" if attempt is None else f" attempt={attempt}"
            streak_text = (
                ""
                if row.get("streak_start_record") is None
                else f" streak={int(row['streak_start_record'])}+{int(row['streak_len'])}"
            )
            print(
                f" {i:>2}. abs_err={float(row['abs_err']):.6f} dataset={Path(str(row['dataset'])).name} "
                f"rec={int(row['record'])} p={int(row['p'])} seed/ref/out={float(row['seed']):.6f}/"
                f"{float(row['ref']):.6f}/{float(row['out']):.6f} "
                f"action(seed/ref/out)={int(row['seed_action_id'])}/{int(row['ref_action_id'])}/{int(row['out_action_id'])} "
                f"af(seed/ref/out)={int(row['seed_action_frame'])}/{int(row['ref_action_frame'])}/{int(row['out_action_frame'])}"
                f"{disc_text}{attempt_text}{streak_text}"
            )


if __name__ == "__main__":
    main()
