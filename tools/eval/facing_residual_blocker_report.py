from __future__ import annotations

import argparse
import importlib
import json
import re
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


@dataclass(frozen=True)
class FacingResidualRow:
    dataset: str
    record: int
    seed_frame: int
    ref_frame: int
    p: int
    seed_action_id: int
    ref_action_id: int
    out_action_id: int
    prev_action_id: int
    on_ground: int
    hitlag: int
    hitstun: int
    seed_facing: int
    ref_facing: int
    out_facing: int
    x2228_b7: int
    last_hit_by: int
    combo_count: int


@dataclass(frozen=True)
class FacingResidualCluster:
    seed_action_id: int
    ref_action_id: int
    out_action_id: int
    prev_action_id: int
    on_ground: int
    hitlag: int
    hitstun: int
    ref_out: str


def _load_binding():
    return importlib.import_module("msl_binding")


def load_action_id_names(action_ids_path: Path | None = None) -> dict[int, str]:
    path = action_ids_path or (repo_root() / "src" / "action_ids.h")
    names: dict[int, str] = {}
    pat = re.compile(r"^\s*MSL_ACT_([A-Z0-9_]+)\s*=\s*0x([0-9A-Fa-f]+)\b")
    for line in path.read_text(encoding="utf-8").splitlines():
        m = pat.match(line)
        if m is None:
            continue
        names[int(m.group(2), 16)] = m.group(1)
    return names


def _iter_facing_residual_rows(suite_path: Path, datasets_dir: Path) -> list[FacingResidualRow]:
    suite = load_suite(suite_path)
    binding = _load_binding()
    rows: list[FacingResidualRow] = []

    for replay in suite.replays:
        ds_path = dataset_path_for_suite_replay(
            suite_name=suite.name,
            replay_rel_path=replay.replay,
            datasets_dir=datasets_dir,
        )
        ds = read_dataset(str(ds_path))
        samples = ds.samples
        n = int(samples.shape[0])
        num_players = int(ds.header["num_players"])

        sizes = binding.sizes()
        seed_stride = int(sizes["seed"])
        input_stride = int(sizes["input"])
        compare_stride = int(sizes["compare"])

        init_kwargs = {"batch_size": n, "num_players": num_players}
        if suite.ucf_enabled is not None:
            init_kwargs["ucf_enabled"] = int(bool(suite.ucf_enabled))
        if suite.ucf_cardinals_1_0_enabled is not None:
            init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(suite.ucf_cardinals_1_0_enabled))
        handle = binding.init(**init_kwargs)
        try:
            seed_b = (
                np.frombuffer(samples["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(n, seed_stride).copy()
            )
            prev_b = (
                np.frombuffer(samples["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
                .reshape(n, input_stride)
                .copy()
            )
            in_b = (
                np.frombuffer(samples["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(n, input_stride).copy()
            )
            out_b = np.empty((n, compare_stride), dtype=np.uint8)

            binding.reseed_seed(handle, seed_b)
            binding.step_input(handle, prev_b, in_b)
            binding.write_compare(handle, out_b)

            out = out_b.view(COMPARE_DTYPE).reshape(-1)
            seed = samples["seed_t"]
            ref = samples["ref_t1"]

            for i in range(n):
                for p in range(num_players):
                    ref_facing = int(ref["facing"][i, p])
                    out_facing = int(out["facing"][i, p])
                    if ref_facing == out_facing:
                        continue
                    prev_action_id = int(seed["seed_prev_action_id"][i, p])
                    rows.append(
                        FacingResidualRow(
                            dataset=str(ds_path),
                            record=int(i),
                            seed_frame=int(seed["frame_id"][i]),
                            ref_frame=int(ref["frame_id"][i]),
                            p=int(p),
                            seed_action_id=int(seed["action_id"][i, p]),
                            ref_action_id=int(ref["action_id"][i, p]),
                            out_action_id=int(out["action_id"][i, p]),
                            prev_action_id=prev_action_id,
                            on_ground=int(seed["on_ground"][i, p]),
                            hitlag=int(seed["hitlag"][i, p]),
                            hitstun=int(seed["hitstun"][i, p]),
                            seed_facing=int(seed["facing"][i, p]),
                            ref_facing=ref_facing,
                            out_facing=out_facing,
                            x2228_b7=int(seed["x2228_b7"][i, p]),
                            last_hit_by=int(seed["last_hit_by"][i, p]),
                            combo_count=int(seed["combo_count"][i, p]),
                        )
                    )
        finally:
            binding.destroy(handle)

    return rows


def _cluster_rows(rows: list[FacingResidualRow]) -> tuple[Counter[FacingResidualCluster], dict[FacingResidualCluster, FacingResidualRow]]:
    counts: Counter[FacingResidualCluster] = Counter()
    examples: dict[FacingResidualCluster, FacingResidualRow] = {}
    for row in rows:
        key = FacingResidualCluster(
            seed_action_id=row.seed_action_id,
            ref_action_id=row.ref_action_id,
            out_action_id=row.out_action_id,
            prev_action_id=row.prev_action_id,
            on_ground=row.on_ground,
            hitlag=row.hitlag,
            hitstun=row.hitstun,
            ref_out=f"{row.ref_facing}->{row.out_facing}",
        )
        counts[key] += 1
        examples.setdefault(key, row)
    return counts, examples


def _action_name(names: dict[int, str], action_id: int) -> str:
    return names.get(action_id, f"UNKNOWN_0x{action_id:04X}")


def _cluster_json_item(
    cluster: FacingResidualCluster,
    count: int,
    example: FacingResidualRow,
    action_names: dict[int, str],
) -> dict[str, object]:
    return {
        "count": int(count),
        "seed_action_id": int(cluster.seed_action_id),
        "seed_action_name": _action_name(action_names, cluster.seed_action_id),
        "ref_action_id": int(cluster.ref_action_id),
        "ref_action_name": _action_name(action_names, cluster.ref_action_id),
        "out_action_id": int(cluster.out_action_id),
        "out_action_name": _action_name(action_names, cluster.out_action_id),
        "prev_action_id": int(cluster.prev_action_id),
        "prev_action_name": _action_name(action_names, cluster.prev_action_id),
        "on_ground": int(cluster.on_ground),
        "hitlag": int(cluster.hitlag),
        "hitstun": int(cluster.hitstun),
        "ref_out": cluster.ref_out,
        "example": {
            "dataset": example.dataset,
            "record": int(example.record),
            "p": int(example.p),
            "seed_frame": int(example.seed_frame),
            "ref_frame": int(example.ref_frame),
            "seed_facing": int(example.seed_facing),
            "ref_facing": int(example.ref_facing),
            "out_facing": int(example.out_facing),
            "x2228_b7": int(example.x2228_b7),
            "last_hit_by": int(example.last_hit_by),
            "combo_count": int(example.combo_count),
        },
    }


def build_summary(
    rows: list[FacingResidualRow],
    *,
    action_names: dict[int, str] | None = None,
    top_n: int = 20,
) -> dict[str, object]:
    names = action_names or {}
    clusters, examples = _cluster_rows(rows)

    ref_out_counts: Counter[str] = Counter()
    action_ref_out_counts: Counter[tuple[int, int, str]] = Counter()
    for row in rows:
        ref_out_counts[f"{row.ref_facing}->{row.out_facing}"] += 1
        action_ref_out_counts[(row.ref_action_id, row.out_action_id, f"{row.ref_facing}->{row.out_facing}")] += 1

    top_clusters = sorted(
        clusters.items(),
        key=lambda it: (
            -it[1],
            it[0].seed_action_id,
            it[0].ref_action_id,
            it[0].out_action_id,
            it[0].prev_action_id,
            it[0].on_ground,
            it[0].hitlag,
            it[0].hitstun,
            it[0].ref_out,
        ),
    )
    top_ref_out = sorted(ref_out_counts.items(), key=lambda it: (-it[1], it[0]))
    top_action_ref_out = sorted(action_ref_out_counts.items(), key=lambda it: (-it[1], it[0]))

    blocker_rows = [
        {
            "dataset": row.dataset,
            "record": int(row.record),
            "p": int(row.p),
            "seed_frame": int(row.seed_frame),
            "ref_frame": int(row.ref_frame),
            "seed_action_id": int(row.seed_action_id),
            "seed_action_name": _action_name(names, row.seed_action_id),
            "ref_action_id": int(row.ref_action_id),
            "ref_action_name": _action_name(names, row.ref_action_id),
            "out_action_id": int(row.out_action_id),
            "out_action_name": _action_name(names, row.out_action_id),
            "prev_action_id": int(row.prev_action_id),
            "prev_action_name": _action_name(names, row.prev_action_id),
            "on_ground": int(row.on_ground),
            "hitlag": int(row.hitlag),
            "hitstun": int(row.hitstun),
            "seed_facing": int(row.seed_facing),
            "ref_facing": int(row.ref_facing),
            "out_facing": int(row.out_facing),
            "x2228_b7": int(row.x2228_b7),
            "last_hit_by": int(row.last_hit_by),
            "combo_count": int(row.combo_count),
        }
        for row in sorted(
            rows,
            key=lambda r: (
                Path(r.dataset).name,
                r.record,
                r.p,
            ),
        )[:top_n]
    ]

    return {
        "row_count": int(len(rows)),
        "top_ref_out": [{"ref_out": key, "count": int(count)} for key, count in top_ref_out[:top_n]],
        "top_ref_action_out": [
            {
                "ref_action_id": int(ref_action_id),
                "ref_action_name": _action_name(names, ref_action_id),
                "out_action_id": int(out_action_id),
                "out_action_name": _action_name(names, out_action_id),
                "ref_out": ref_out,
                "count": int(count),
            }
            for (ref_action_id, out_action_id, ref_out), count in top_action_ref_out[:top_n]
        ],
        "top_clusters": [
            _cluster_json_item(cluster, count, examples[cluster], names) for cluster, count in top_clusters[:top_n]
        ],
        "blocker_rows": blocker_rows,
        "blocker": (
            "top facing residuals are still concentrated in exact seed-visible action/transition clusters; "
            "use blocker_rows + top_clusters to drive the next decomp-backed runtime lane."
        ),
    }


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Summarize suite-wide facing residual clusters using seed-visible action/transition context."
    )
    ap.add_argument(
        "--suite",
        type=Path,
        default=Path("replays/suites/fox_falco_fd_ucf084_recent.json"),
    )
    ap.add_argument("--datasets-dir", type=Path, default=Path("datasets"))
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--json-out", type=Path, default=None)
    args = ap.parse_args()

    root = repo_root()
    suite_path = args.suite if args.suite.is_absolute() else root / args.suite
    datasets_dir = args.datasets_dir if args.datasets_dir.is_absolute() else root / args.datasets_dir

    rows = _iter_facing_residual_rows(suite_path, datasets_dir)
    summary = build_summary(rows, action_names=load_action_id_names(), top_n=max(1, int(args.top)))

    print(f"rows={summary['row_count']}")
    print("top_ref_out:")
    for item in summary["top_ref_out"]:
        print(f"  {item['ref_out']}: {item['count']}")
    print("top_clusters:")
    for item in summary["top_clusters"][:10]:
        print(
            "  "
            f"{item['count']}x "
            f"{item['seed_action_name']}->{item['ref_action_name']}/{item['out_action_name']} "
            f"prev={item['prev_action_name']} on_ground={item['on_ground']} "
            f"hitlag={item['hitlag']} hitstun={item['hitstun']} face={item['ref_out']}"
        )
    print(f"blocker: {summary['blocker']}")

    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(summary, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
