from __future__ import annotations

"""Locate row-level rollout first-break candidates over a replay suite."""

import argparse
import csv
import importlib
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, Dataset, read_dataset
from tools.eval.discrete_compare_lanes import compile_discrete_compare_lanes, first_mismatch_values
from tools.eval.rollout_locate_tsv import (
    ROLLOUT_LOCATE_COLUMNS,
    RolloutLocateRow,
    cluster_key_for,
    row_to_tsv_values,
    summarize_clusters,
    write_rollout_locate_tsv,
)
from tools.eval.run_longest_rollout_streaks import _parse_csv, _parse_players, _validate_discrete_fields
from tools.eval.validation_profile import get_validation_profile, validation_profile_names
from tools.slippi.known_data_artifacts import (
    read_mslstg01_v7,
    stage_metadata_path_for_stage_id,
)
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


def _load_binding():
    # Built by `uv pip install -e python` (or similar).
    return importlib.import_module("msl_binding")


@dataclass(frozen=True)
class FirstMismatch:
    player: int
    field: str
    subindex: int
    seed: int
    out: int
    ref: int
    seed_ground_id: int = -1
    out_ground_id: int = -1
    ref_ground_id: int = -1


STAGE_SEGMENT_COLUMNS: tuple[str, ...] = (
    "seed_ground_id",
    "seed_ground_kind",
    "seed_ground_platform",
    "seed_ground_transform",
    "seed_ground_slope",
    "seed_ground_ledge",
    "seed_ground_material",
    "current_ground_id",
    "current_ground_kind",
    "current_ground_platform",
    "current_ground_transform",
    "current_ground_slope",
    "current_ground_ledge",
    "current_ground_material",
    "ref_ground_id",
    "ref_ground_kind",
    "ref_ground_platform",
    "ref_ground_transform",
    "ref_ground_slope",
    "ref_ground_ledge",
    "ref_ground_material",
)

_SEGMENT_KIND_NAMES = {0: "floor", 1: "ceiling", 2: "right_wall", 3: "left_wall", 4: "dynamic"}
_TRANSFORM_KIND_NAMES = {1: "height", 2: "static_y", 3: "randall"}


def _stage_id_for_row(ds: Dataset, row: RolloutLocateRow) -> int | None:
    if "stage_id" not in (ds.samples.dtype.fields["seed_t"][0].names or ()):
        return None
    return int(ds.samples["seed_t"][int(row.record)]["stage_id"])


def _segment_lookup_for_stage(
    *, root: Path, stage_id: int, cache: dict[int, dict[int, dict[str, object]]]
) -> dict[int, dict[str, object]]:
    if stage_id in cache:
        return cache[stage_id]
    stage_path = stage_metadata_path_for_stage_id(stage_id, root / "data")
    if stage_path is None or not stage_path.exists():
        cache[stage_id] = {}
        return cache[stage_id]
    stage = read_mslstg01_v7(stage_path)
    transforms = {int(rec.line_id): _TRANSFORM_KIND_NAMES.get(int(rec.kind_id), str(rec.kind_id)) for rec in stage.platform_transforms}
    lookup: dict[int, dict[str, object]] = {}
    for seg in stage.segments:
        y_delta = float(seg.y1) - float(seg.y0)
        lookup[int(seg.line_id)] = {
            "kind": _SEGMENT_KIND_NAMES.get(int(seg.kind_id), str(seg.kind_id)),
            "platform": "1" if (int(seg.flags) & 0x01) else "0",
            "transform": transforms.get(int(seg.line_id), ""),
            "slope": "1" if abs(y_delta) > 1.0e-6 else "0",
            "ledge": "1" if (int(seg.flags) & 0x02) else "0",
            "material": str(int(seg.lo_flags) & 0xFF),
        }
    cache[stage_id] = lookup
    return lookup


def _segment_values(segment_id: int, lookup: dict[int, dict[str, object]]) -> tuple[object, ...]:
    if segment_id < 0 or segment_id == 0xFFFF:
        return (segment_id, "<none>", "", "", "", "", "")
    seg = lookup.get(int(segment_id))
    if seg is None:
        return (segment_id, "<missing>", "", "", "", "", "")
    return (
        int(segment_id),
        seg["kind"],
        seg["platform"],
        seg["transform"],
        seg["slope"],
        seg["ledge"],
        seg["material"],
    )


def stage_segment_tsv_values(
    *, row: RolloutLocateRow, ds: Dataset, root: Path, cache: dict[int, dict[int, dict[str, object]]]
) -> tuple[object, ...]:
    stage_id = _stage_id_for_row(ds, row)
    lookup = {} if stage_id is None else _segment_lookup_for_stage(root=root, stage_id=stage_id, cache=cache)
    return (
        *_segment_values(int(row.seed_ground_id), lookup),
        *_segment_values(int(row.out_ground_id), lookup),
        *_segment_values(int(row.ref_ground_id), lookup),
    )


def _row_from_mismatch(
    *,
    dataset: str,
    record: int,
    seed_frame: int,
    ref_frame: int,
    mismatch: FirstMismatch,
    streak_start_record: int,
    streak_len: int,
    seeded_break: bool,
) -> RolloutLocateRow:
    key = cluster_key_for(
        field=mismatch.field,
        subindex=mismatch.subindex,
        seed=mismatch.seed,
        out=mismatch.out,
        ref=mismatch.ref,
    )
    return RolloutLocateRow(
        dataset=dataset,
        record=int(record),
        seed_frame=int(seed_frame),
        ref_frame=int(ref_frame),
        player=int(mismatch.player),
        field=mismatch.field,
        subindex=int(mismatch.subindex),
        seed=int(mismatch.seed),
        out=int(mismatch.out),
        ref=int(mismatch.ref),
        streak_start_record=int(streak_start_record),
        streak_len=int(streak_len),
        seeded_break=bool(seeded_break),
        cluster_key=key,
        seed_ground_id=int(mismatch.seed_ground_id),
        out_ground_id=int(mismatch.out_ground_id),
        ref_ground_id=int(mismatch.ref_ground_id),
    )


def _scan_rollout_desync_rows(
    *,
    n: int,
    attempt_from_current,
    attempt_seeded_at_record,
    row_from_mismatch,
    limit: int | None,
) -> list[RolloutLocateRow]:
    rows: list[RolloutLocateRow] = []
    cur_start = 0
    cur_len = 0
    needs_seed = True

    def append_row(row: RolloutLocateRow) -> bool:
        rows.append(row)
        return limit is not None and len(rows) >= limit

    j = 0
    while j < n:
        mm = attempt_from_current(j, seed_record=cur_start if needs_seed else None)
        needs_seed = False
        if mm is None:
            cur_len += 1
            j += 1
            continue

        if append_row(
            row_from_mismatch(
                record=j,
                mismatch=mm,
                streak_start_record=cur_start,
                streak_len=cur_len,
                seeded_break=False,
            )
        ):
            return rows

        cur_start = j
        cur_len = 0

        mm2 = attempt_seeded_at_record(j)
        if mm2 is None:
            cur_len = 1
            j += 1
            continue

        if append_row(
            row_from_mismatch(
                record=j,
                mismatch=mm2,
                streak_start_record=j,
                streak_len=0,
                seeded_break=True,
            )
        ):
            return rows

        cur_start = j + 1
        cur_len = 0
        j += 1
        needs_seed = True

    return rows


def _locate_dataset_rollout_desyncs(
    *,
    dataset_path: Path,
    dataset_label: str,
    ds: Dataset,
    fields: tuple[str, ...],
    players: tuple[int, ...],
    max_records: int,
    row_limit: int | None,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
    profile: str,
) -> list[RolloutLocateRow]:
    samples = ds.samples
    num_records_total = int(samples.shape[0])
    num_players = int(ds.header["num_players"])

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
        fields,
        players,
        profile=get_validation_profile(profile),
    )

    def reseed_at(j: int) -> None:
        seed_bytes[0, :] = samples_u8[j, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)

    def step_and_compare(j: int) -> FirstMismatch | None:
        seed_bytes[0, :] = samples_u8[j, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
        binding.step_input_replay_frame_rng(handle, seed_bytes, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        mm = first_mismatch_values(
            seed_row=seed[j],
            out_row=out_view[0],
            ref_row=ref[j],
            lanes=compare_lanes,
        )
        if mm is None:
            return None
        return FirstMismatch(
            player=mm.player,
            field=mm.field,
            subindex=mm.subindex,
            seed=mm.seed,
            out=mm.out,
            ref=mm.ref,
            seed_ground_id=int(seed[j]["ground_id"][mm.player]) if "ground_id" in seed.dtype.names else -1,
            out_ground_id=int(out_view[0]["ground_id"][mm.player])
            if "ground_id" in out_view.dtype.names
            else -1,
            ref_ground_id=int(ref[j]["ground_id"][mm.player]) if "ground_id" in ref.dtype.names else -1,
        )

    def attempt_from_current(j: int, *, seed_record: int | None) -> FirstMismatch | None:
        if seed_record is not None:
            reseed_at(seed_record)
        return step_and_compare(j)

    def attempt_seeded_at_record(j: int) -> FirstMismatch | None:
        reseed_at(j)
        return step_and_compare(j)

    def row_from_mismatch(
        *,
        record: int,
        mismatch: FirstMismatch,
        streak_start_record: int,
        streak_len: int,
        seeded_break: bool,
    ) -> RolloutLocateRow:
        return _row_from_mismatch(
            dataset=dataset_label,
            record=record,
            seed_frame=int(seed["frame_id"][record]),
            ref_frame=int(ref["frame_id"][record]),
            mismatch=mismatch,
            streak_start_record=streak_start_record,
            streak_len=streak_len,
            seeded_break=seeded_break,
        )

    try:
        return _scan_rollout_desync_rows(
            n=n,
            attempt_from_current=attempt_from_current,
            attempt_seeded_at_record=attempt_seeded_at_record,
            row_from_mismatch=row_from_mismatch,
            limit=row_limit,
        )
    finally:
        try:
            binding.destroy(handle)
        except Exception:
            pass


def _validate_seed_fields(fields: tuple[str, ...]) -> None:
    # Keep the runtime error close to argument parsing and clearer than a NumPy field lookup.
    from tools.eval.dataset import SEED_DTYPE

    bad = [field for field in fields if field not in SEED_DTYPE.fields]
    if bad:
        raise SystemExit(
            "error: rollout locate reports seed values; these compare fields are absent from seed_t: "
            + ", ".join(bad)
        )


def _suite_dataset_paths(*, suite_name: str, replay_rel_paths: list[str], datasets_dir: str) -> list[Path]:
    return [
        dataset_path_for_suite_replay(
            suite_name=suite_name,
            replay_rel_path=replay,
            datasets_dir=datasets_dir,
        )
        for replay in replay_rel_paths
    ]


def _print_summary(rows: list[RolloutLocateRow], *, top: int) -> None:
    summaries = summarize_clusters(rows)
    print(f"rows={len(rows)} clusters={len(summaries)}")
    if not summaries:
        return
    print("top_clusters:")
    for i, row in enumerate(summaries[: max(1, int(top))], start=1):
        ds_preview = ",".join(Path(ds).name for ds in row.datasets[:3])
        if len(row.datasets) > 3:
            ds_preview += f",+{len(row.datasets) - 3}"
        ex = row.example
        print(
            f"{i:>2}. impact={row.impact:<5d} freq={row.frequency:<5d} seeded={row.seeded_breaks:<5d} "
            f"{row.field}[{row.subindex}] seed/out/ref={row.seed}/{row.out}/{row.ref} "
            f"example={Path(ex.dataset).name}:rec={ex.record}:p={ex.player} datasets={ds_preview}"
        )


def main() -> None:
    ap = argparse.ArgumentParser(description="Locate row-level rollout first-break candidates over a suite.")
    ap.add_argument("--suite", required=True, help="Suite JSON path under repo root.")
    ap.add_argument("--datasets-dir", default="datasets", help="Datasets directory under repo root.")
    ap.add_argument(
        "--fields",
        default="action_id,animation_index,on_ground,hitlag,hitstun,state_flags",
        help="Comma-separated discrete compare fields (default: %(default)s).",
    )
    ap.add_argument(
        "--players",
        default=None,
        help="Comma-separated player indices to compare (default: all players in dataset).",
    )
    ap.add_argument("--dataset-filter", default="", help="Optional substring filter on dataset relative path.")
    ap.add_argument("--max-records", type=int, default=0, help="Optional per-dataset record cap (0 = no cap).")
    ap.add_argument("--limit", type=int, default=0, help="Optional total output row cap (0 = no cap).")
    ap.add_argument("--top", type=int, default=12, help="Top-N clusters for the optional summary preview.")
    ap.add_argument("--out", type=Path, default=None, help="Optional TSV output path.")
    ap.add_argument(
        "--include-stage-segments",
        action="store_true",
        help=(
            "Append seed/current/ref ground_id metadata joined from MSLSTG01. "
            "Report/triage-only; canonical locate columns are unchanged by default."
        ),
    )
    ap.add_argument(
        "--profile",
        choices=validation_profile_names(),
        default="rl1_gameplay",
        help="Validation profile used to mask compare lanes (default: %(default)s).",
    )
    ap.add_argument(
        "--print-summary",
        action="store_true",
        help="Print a cluster summary after writing TSV. If --out is omitted, TSV is still printed to stdout first.",
    )
    args = ap.parse_args()

    root = repo_root()
    suite_path = (root / args.suite).resolve()
    suite = load_suite(suite_path)
    fields = _validate_discrete_fields(_parse_csv(str(args.fields)))
    _validate_seed_fields(fields)

    replay_rel_paths = [entry.replay for entry in suite.replays]
    dataset_paths = _suite_dataset_paths(
        suite_name=suite.name,
        replay_rel_paths=replay_rel_paths,
        datasets_dir=str(args.datasets_dir),
    )

    dataset_filter = str(args.dataset_filter)
    filtered: list[tuple[Path, str]] = []
    missing: list[str] = []
    for ds_path in dataset_paths:
        rel = str(ds_path.resolve().relative_to(root))
        if dataset_filter and dataset_filter not in rel:
            continue
        if not ds_path.exists():
            missing.append(rel)
        else:
            filtered.append((ds_path, rel))

    if missing:
        print(f"Missing {len(missing)} preprocessed dataset files for suite {suite.name}:")
        for p in missing:
            print(f"  {p}")
        print("Run preprocessing first:")
        print(f"  uv run python -m tools.slippi.preprocess_suite --suite {args.suite} --datasets-dir {args.datasets_dir}")
        raise SystemExit(2)
    if not filtered:
        raise SystemExit("error: no datasets selected")

    row_limit = int(args.limit)
    if row_limit <= 0:
        row_limit = None

    rows: list[RolloutLocateRow] = []
    datasets_by_label: dict[str, Dataset] = {}
    for ds_path, rel in filtered:
        if row_limit is not None and len(rows) >= row_limit:
            break
        ds = read_dataset(str(ds_path))
        datasets_by_label[rel] = ds
        num_players = int(ds.header["num_players"])
        players = _parse_players(args.players, num_players=num_players)
        remaining = None if row_limit is None else max(0, row_limit - len(rows))
        rows.extend(
            _locate_dataset_rollout_desyncs(
                dataset_path=ds_path,
                dataset_label=rel,
                ds=ds,
                fields=fields,
                players=players,
                max_records=int(args.max_records),
                row_limit=remaining,
                ucf_enabled=suite.ucf_enabled,
                ucf_cardinals_1_0_enabled=suite.ucf_cardinals_1_0_enabled,
                profile=str(args.profile),
            )
        )

    if args.include_stage_segments:
        columns = (*ROLLOUT_LOCATE_COLUMNS, *STAGE_SEGMENT_COLUMNS)
        stage_cache: dict[int, dict[int, dict[str, object]]] = {}
        if args.out is not None:
            args.out.parent.mkdir(parents=True, exist_ok=True)
        out_file = args.out.open("w", encoding="utf-8", newline="") if args.out is not None else sys.stdout
        try:
            writer = csv.writer(out_file, delimiter="\t", lineterminator="\n")
            writer.writerow(columns)
            for row in rows:
                ds = datasets_by_label[row.dataset]
                writer.writerow(
                    (
                        *row_to_tsv_values(row),
                        *stage_segment_tsv_values(row=row, ds=ds, root=root, cache=stage_cache),
                    )
                )
        finally:
            if args.out is not None:
                out_file.close()
                print(f"wrote: {args.out}")
    elif args.out is not None:
        write_rollout_locate_tsv(args.out, rows)
        print(f"wrote: {args.out}")
    else:
        writer = csv.writer(sys.stdout, delimiter="\t", lineterminator="\n")
        writer.writerow(ROLLOUT_LOCATE_COLUMNS)
        for row in rows:
            writer.writerow(row_to_tsv_values(row))

    if args.print_summary:
        _print_summary(rows, top=int(args.top))


if __name__ == "__main__":
    main()
