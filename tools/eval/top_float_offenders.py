from __future__ import annotations

"""Collect top absolute float-error offenders for one-step suite evaluation.

This is a deterministic triage utility intended for guardrail checks and row-level debugging.
"""

import argparse
import importlib
import json
import heapq
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


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


def _load_binding():
    return importlib.import_module("msl_binding")


def _dataset_rel(root: Path, dataset_path: Path) -> str:
    p = dataset_path.resolve()
    try:
        return str(p.relative_to(root).as_posix())
    except ValueError:
        return str(p.as_posix())


def _push_top(
    heap: list[tuple[float, str, int, int, FloatOffender]],
    *,
    top: int,
    row: FloatOffender,
) -> None:
    key = float(row.abs_err)
    # Deterministic tie-breakers avoid comparing dataclass objects in heap internals.
    item = (key, str(row.dataset), int(row.record), int(row.p), row)
    if len(heap) < top:
        heapq.heappush(heap, item)
        return
    if key > heap[0][0]:
        heapq.heapreplace(heap, item)


def collect_dataset_top_float_offenders(
    *,
    dataset_path: Path,
    fields: tuple[str, ...],
    chunk: int,
    top: int,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
) -> dict[str, list[FloatOffender]]:
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

    heaps: dict[str, list[tuple[float, str, int, int, FloatOffender]]] = {f: [] for f in fields}
    offset = 0
    ds_rel = _dataset_rel(repo_root(), dataset_path)
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
                    row = FloatOffender(
                        field=field,
                        abs_err=float(abs_err[r, pp]),
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

    out: dict[str, list[FloatOffender]] = {}
    for field, heap in heaps.items():
        rows = [row for (_err, _ds, _rec, _p, row) in heap]
        rows.sort(key=lambda r: (-r.abs_err, r.dataset, r.record, r.p))
        out[field] = rows
    return out


def collect_suite_top_float_offenders(
    *,
    suite: Path,
    datasets_dir: str,
    fields: tuple[str, ...],
    chunk: int,
    top: int,
) -> dict[str, object]:
    root = repo_root()
    suite_path = (root / suite).resolve()
    suite_obj = load_suite(suite_path)

    heaps: dict[str, list[tuple[float, str, int, int, FloatOffender]]] = {f: [] for f in fields}

    for entry in suite_obj.replays:
        ds = dataset_path_for_suite_replay(
            suite_name=suite_obj.name,
            replay_rel_path=entry.replay,
            datasets_dir=datasets_dir,
        )
        per_ds = collect_dataset_top_float_offenders(
            dataset_path=ds,
            fields=fields,
            chunk=chunk,
            top=top,
            ucf_enabled=bool(suite_obj.ucf_enabled),
            ucf_cardinals_1_0_enabled=bool(suite_obj.ucf_cardinals_1_0_enabled),
        )
        for field in fields:
            for row in per_ds[field]:
                _push_top(heaps[field], top=top, row=row)

    top_rows: dict[str, list[dict[str, object]]] = {}
    keys: dict[str, list[dict[str, object]]] = {}
    for field in fields:
        rows = [row for (_err, _ds, _rec, _p, row) in heaps[field]]
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
        "fields": list(fields),
        "top_n": int(top),
        "top_rows": top_rows,
        "keys": keys,
    }


def main() -> None:
    ap = argparse.ArgumentParser(description="Collect suite top absolute float offenders.")
    ap.add_argument("--suite", type=Path, required=True)
    ap.add_argument("--datasets-dir", default="datasets")
    ap.add_argument("--field", action="append", default=["pos_x", "pos_y"])
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--json-out", type=Path, default=None)
    args = ap.parse_args()

    fields = tuple(dict.fromkeys([str(f).strip() for f in args.field if str(f).strip()]))
    if not fields:
        raise SystemExit("error: at least one --field is required")

    payload = collect_suite_top_float_offenders(
        suite=args.suite,
        datasets_dir=str(args.datasets_dir),
        fields=fields,
        chunk=int(args.chunk),
        top=max(1, int(args.top)),
    )

    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(
        f"suite={payload['suite']} fields={','.join(fields)} top={payload['top_n']} "
        f"datasets_dir={payload['datasets_dir']}"
    )
    for field in fields:
        rows = payload["top_rows"][field]
        print(f"== {field} top {len(rows)} ==")
        for i, row in enumerate(rows, start=1):
            print(
                f" {i:>2}. abs_err={float(row['abs_err']):.6f} dataset={Path(str(row['dataset'])).name} "
                f"rec={int(row['record'])} p={int(row['p'])} seed/ref/out={float(row['seed']):.6f}/"
                f"{float(row['ref']):.6f}/{float(row['out']):.6f} "
                f"action(seed/ref/out)={int(row['seed_action_id'])}/{int(row['ref_action_id'])}/{int(row['out_action_id'])}"
            )


if __name__ == "__main__":
    main()
