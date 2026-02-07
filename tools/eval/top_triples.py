from __future__ import annotations

"""Print top mismatch transitions for one dataset or a suite.

Triples are reported as `(seed,ref,out)`.
Locate TSV rows are sourced by running:
`python -m tools.eval.locate_discrete_mismatches --format tsv`.
"""

import argparse
import json
import subprocess
import sys
from collections import Counter
from pathlib import Path
from typing import Any

from tools.eval.locate_tsv import LocateRow, parse_locate_tsv_text
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


def count_triples(rows: list[LocateRow]) -> tuple[Counter[tuple[int, int, int]], dict[tuple[int, int, int], LocateRow]]:
    counts: Counter[tuple[int, int, int]] = Counter()
    examples: dict[tuple[int, int, int], LocateRow] = {}
    for row in rows:
        key = (row.seed, row.ref, row.out)
        counts[key] += 1
        if key not in examples:
            examples[key] = row
    return counts, examples


def count_ref_out(rows: list[LocateRow]) -> tuple[Counter[tuple[int, int]], dict[tuple[int, int], LocateRow]]:
    counts: Counter[tuple[int, int]] = Counter()
    examples: dict[tuple[int, int], LocateRow] = {}
    for row in rows:
        key = (row.ref, row.out)
        counts[key] += 1
        if key not in examples:
            examples[key] = row
    return counts, examples


def _format_example(row: LocateRow) -> str:
    return f"{Path(row.dataset).name} rec={row.record} p={row.p} frames={row.seed_frame}->{row.ref_frame}"


def _top_items(counter: Counter[Any], top_n: int) -> list[tuple[Any, int]]:
    return sorted(counter.items(), key=lambda it: (-it[1], it[0]))[:top_n]


def _print_top_triples(
    *,
    title: str,
    counts: Counter[tuple[int, int, int]],
    examples: dict[tuple[int, int, int], LocateRow],
    top_n: int,
) -> None:
    print(title)
    if not counts:
        print("  (no mismatches)")
        return
    for i, (triple, count) in enumerate(_top_items(counts, top_n), start=1):
        seed, ref, out = triple
        print(
            f"  {i:>2}. count={count:<5d} triple(seed/ref/out)={seed}/{ref}/{out} "
            f"example={_format_example(examples[triple])}"
        )


def _print_top_ref_out(
    *,
    title: str,
    counts: Counter[tuple[int, int]],
    examples: dict[tuple[int, int], LocateRow],
    top_n: int,
) -> None:
    print(title)
    if not counts:
        print("  (no mismatches)")
        return
    for i, (pair, count) in enumerate(_top_items(counts, top_n), start=1):
        ref, out = pair
        print(
            f"  {i:>2}. count={count:<5d} ref->out={ref}->{out} "
            f"example={_format_example(examples[pair])}"
        )


def _run_locate_tsv_text(
    *,
    dataset_path: Path,
    field: str,
    chunk: int,
    only_seed_equals_ref: bool,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
) -> str:
    cmd = [
        sys.executable,
        "-m",
        "tools.eval.locate_discrete_mismatches",
        "--dataset",
        str(dataset_path),
        "--field",
        str(field),
        "--chunk",
        str(int(chunk)),
        "--format",
        "tsv",
    ]
    if only_seed_equals_ref:
        cmd.append("--only-seed-equals-ref")
    if ucf_enabled is not None:
        cmd.extend(["--ucf-enabled", "1" if ucf_enabled else "0"])
    if ucf_cardinals_1_0_enabled is not None:
        cmd.extend(["--ucf-cardinals-1-0-enabled", "1" if ucf_cardinals_1_0_enabled else "0"])

    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        stderr = proc.stderr.strip()
        raise SystemExit(f"locate_discrete_mismatches failed for {dataset_path}: {stderr}")
    return proc.stdout


def _json_item_from_triple(
    triple: tuple[int, int, int],
    count: int,
    example: LocateRow,
) -> dict[str, Any]:
    seed, ref, out = triple
    return {
        "seed": seed,
        "ref": ref,
        "out": out,
        "count": int(count),
        "example": {
            "dataset": example.dataset,
            "record": int(example.record),
            "p": int(example.p),
            "seed_frame": int(example.seed_frame),
            "ref_frame": int(example.ref_frame),
        },
    }


def _json_item_from_ref_out(
    pair: tuple[int, int],
    count: int,
    example: LocateRow,
) -> dict[str, Any]:
    ref, out = pair
    return {
        "ref": ref,
        "out": out,
        "count": int(count),
        "example": {
            "dataset": example.dataset,
            "record": int(example.record),
            "p": int(example.p),
            "seed_frame": int(example.seed_frame),
            "ref_frame": int(example.ref_frame),
        },
    }


def _json_section(
    *,
    triples: Counter[tuple[int, int, int]],
    triple_examples: dict[tuple[int, int, int], LocateRow],
    ref_out: Counter[tuple[int, int]],
    ref_out_examples: dict[tuple[int, int], LocateRow],
    top_n: int,
) -> dict[str, Any]:
    top_triples = _top_items(triples, top_n)
    top_ref_out = _top_items(ref_out, top_n)
    return {
        "num_rows": int(sum(triples.values())),
        "top_triples": [
            _json_item_from_triple(key, count, triple_examples[key]) for key, count in top_triples
        ],
        "top_ref_out": [
            _json_item_from_ref_out(key, count, ref_out_examples[key]) for key, count in top_ref_out
        ],
    }


def main() -> None:
    epilog = (
        "Examples:\n"
        "  uv run python -m tools.eval.top_triples --suite "
        "replays/suites/fox_falco_fd_ucf084_recent.json --datasets-dir datasets "
        "--field action_id --only-seed-equals-ref --top 20\n"
        "  uv run python -m tools.eval.top_triples --dataset "
        "datasets/AttachedGoodNaturedGuanaco.msl --field action_id --top 20\n\n"
        "Note: locate TSV columns are seed,out,ref; this tool reports seed,ref,out."
    )
    ap = argparse.ArgumentParser(
        description="Show top discrete mismatch triples by (seed,ref,out).",
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=epilog,
    )
    target = ap.add_mutually_exclusive_group(required=True)
    target.add_argument("--suite", type=Path, help="Suite JSON path (e.g. replays/suites/...).")
    target.add_argument("--dataset", type=Path, help="Single dataset .msl path.")
    ap.add_argument("--datasets-dir", default="datasets", help="Datasets root under repo for suite mode.")
    ap.add_argument("--field", required=True, help="Discrete compare field (e.g. action_id).")
    ap.add_argument("--only-seed-equals-ref", action="store_true", help="Only include rows where seed==ref.")
    ap.add_argument("--top", type=int, default=20, help="Top-N rows to print.")
    ap.add_argument("--chunk", type=int, default=4096, help="Chunk size for locate command.")
    ap.add_argument("--ucf-enabled", type=int, default=None, help="Override UCF enabled for dataset mode (0/1).")
    ap.add_argument(
        "--ucf-cardinals-1-0-enabled",
        type=int,
        default=None,
        help="Override UCF 1.0 cardinals for dataset mode (0/1).",
    )
    ap.add_argument("--json-out", type=Path, default=None, help="Optional JSON summary output path.")
    args = ap.parse_args()

    top_n = max(1, int(args.top))
    root = repo_root()
    json_payload: dict[str, Any] = {
        "field": str(args.field),
        "only_seed_equals_ref": bool(args.only_seed_equals_ref),
        "top": int(top_n),
    }

    if args.dataset is not None:
        ds_path = args.dataset.resolve()
        if not ds_path.exists():
            raise SystemExit(f"missing dataset: {ds_path}")
        ucf = None if args.ucf_enabled is None else bool(int(args.ucf_enabled))
        ucf_card = None if args.ucf_cardinals_1_0_enabled is None else bool(int(args.ucf_cardinals_1_0_enabled))
        text = _run_locate_tsv_text(
            dataset_path=ds_path,
            field=str(args.field),
            chunk=int(args.chunk),
            only_seed_equals_ref=bool(args.only_seed_equals_ref),
            ucf_enabled=ucf,
            ucf_cardinals_1_0_enabled=ucf_card,
        )
        rows = parse_locate_tsv_text(text, source=str(ds_path))
        triples, triple_examples = count_triples(rows)
        ref_out, ref_out_examples = count_ref_out(rows)
        _print_top_triples(
            title=f"== {ds_path} ==",
            counts=triples,
            examples=triple_examples,
            top_n=top_n,
        )
        _print_top_ref_out(
            title="== top ref->out ==",
            counts=ref_out,
            examples=ref_out_examples,
            top_n=top_n,
        )
        json_payload["mode"] = "dataset"
        json_payload["dataset"] = str(ds_path)
        json_payload["dataset_summary"] = _json_section(
            triples=triples,
            triple_examples=triple_examples,
            ref_out=ref_out,
            ref_out_examples=ref_out_examples,
            top_n=top_n,
        )
    else:
        suite_path = (root / args.suite).resolve()
        suite = load_suite(suite_path)
        print(
            f"suite={suite.name} field={args.field} only_seed_equals_ref={bool(args.only_seed_equals_ref)} top={top_n}"
        )
        suite_triples: Counter[tuple[int, int, int]] = Counter()
        suite_triple_examples: dict[tuple[int, int, int], LocateRow] = {}
        suite_ref_out: Counter[tuple[int, int]] = Counter()
        suite_ref_out_examples: dict[tuple[int, int], LocateRow] = {}
        per_dataset: list[dict[str, Any]] = []
        for entry in suite.replays:
            ds_path = dataset_path_for_suite_replay(
                suite_name=suite.name,
                replay_rel_path=entry.replay,
                datasets_dir=args.datasets_dir,
            )
            if not ds_path.exists():
                raise SystemExit(f"missing dataset (run preprocess): {ds_path}")
            text = _run_locate_tsv_text(
                dataset_path=ds_path,
                field=str(args.field),
                chunk=int(args.chunk),
                only_seed_equals_ref=bool(args.only_seed_equals_ref),
                ucf_enabled=suite.ucf_enabled,
                ucf_cardinals_1_0_enabled=suite.ucf_cardinals_1_0_enabled,
            )
            rows = parse_locate_tsv_text(text, source=str(ds_path))
            triples, triple_examples = count_triples(rows)
            ref_out, ref_out_examples = count_ref_out(rows)

            _print_top_triples(
                title=f"== {ds_path.relative_to(root)} ==",
                counts=triples,
                examples=triple_examples,
                top_n=top_n,
            )
            _print_top_ref_out(
                title="== top ref->out ==",
                counts=ref_out,
                examples=ref_out_examples,
                top_n=top_n,
            )

            suite_triples.update(triples)
            suite_ref_out.update(ref_out)
            for key, row in triple_examples.items():
                if key not in suite_triple_examples:
                    suite_triple_examples[key] = row
            for key, row in ref_out_examples.items():
                if key not in suite_ref_out_examples:
                    suite_ref_out_examples[key] = row
            per_dataset.append(
                {
                    "dataset": str(ds_path),
                    **_json_section(
                        triples=triples,
                        triple_examples=triple_examples,
                        ref_out=ref_out,
                        ref_out_examples=ref_out_examples,
                        top_n=top_n,
                    ),
                }
            )

        print()
        _print_top_triples(
            title="== suite aggregate triples ==",
            counts=suite_triples,
            examples=suite_triple_examples,
            top_n=top_n,
        )
        _print_top_ref_out(
            title="== suite aggregate ref->out ==",
            counts=suite_ref_out,
            examples=suite_ref_out_examples,
            top_n=top_n,
        )
        json_payload["mode"] = "suite"
        json_payload["suite"] = str(suite_path)
        json_payload["suite_name"] = suite.name
        json_payload["datasets"] = per_dataset
        json_payload["suite_summary"] = _json_section(
            triples=suite_triples,
            triple_examples=suite_triple_examples,
            ref_out=suite_ref_out,
            ref_out_examples=suite_ref_out_examples,
            top_n=top_n,
        )

    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(json_payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
