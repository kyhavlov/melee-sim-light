from __future__ import annotations

"""Locate per-record discrete mismatches for a single `.msl` dataset.

This is a debugging tool: it runs the sim on the dataset and prints the exact records where a
discrete compare field differs from `ref_t1` (optionally only where `seed_t == ref_t1`).

Example:
  uv run python -m tools.eval.locate_discrete_mismatches \\
    --dataset datasets/.../TreasuredBackKangaroo.msl \\
    --field combo_count --field last_attack_landed --only-seed-equals-ref --format tsv
"""

import argparse
import importlib
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _load_binding():
    # Built by `uv pip install -e python` (or similar).
    return importlib.import_module("msl_binding")


ITEM_FIELD_TO_SUBFIELD: dict[str, str] = {
    "item_exists": "exists",
    "item_type": "type",
    "item_state": "state",
    "item_owner": "owner",
    "item_instance_id": "instance_id",
}


@dataclass(frozen=True)
class MismatchRow:
    dataset: str
    record: int
    seed_frame: int
    ref_frame: int
    p: int
    field: str
    seed: int
    out: int
    ref: int


def _iter_dataset_mismatches(
    *,
    dataset_path: Path,
    fields: tuple[str, ...],
    chunk: int,
    ucf_enabled: bool | None,
    ucf_cardinals_1_0_enabled: bool | None,
    max_rows: int | None,
    only_seed_equals_ref: bool,
) -> list[MismatchRow]:
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

    # Preallocated buffers (bytes) that C reads/writes.
    seed_bytes = np.empty((min(chunk, num_records), seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((min(chunk, num_records), input_stride), dtype=np.uint8)
    input_bytes = np.empty((min(chunk, num_records), input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((min(chunk, num_records), compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    rows: list[MismatchRow] = []
    offset = 0
    while offset < num_records:
        chunk_n = min(chunk, num_records - offset)
        if chunk_n != seed_bytes.shape[0]:
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

        active = slice(0, num_players)

        for field in fields:
            item_subfield = ITEM_FIELD_TO_SUBFIELD.get(field)
            if item_subfield is not None:
                out_arr = out_view["items"][item_subfield]
                ref_arr = ref["items"][item_subfield]
                diff = out_arr != ref_arr
                if not np.any(diff):
                    continue

                pairs = np.argwhere(diff)
                for ri, item_slot in pairs:
                    r = int(ri)
                    slot = int(item_slot)
                    gi = int(offset + r)

                    seed_v = int(seed["items"][item_subfield][r, slot])
                    out_v = int(out_arr[r, slot])
                    ref_v = int(ref_arr[r, slot])
                    if only_seed_equals_ref and seed_v != ref_v:
                        continue

                    rows.append(
                        MismatchRow(
                            dataset=str(dataset_path),
                            record=gi,
                            seed_frame=int(seed["frame_id"][r]),
                            ref_frame=int(ref["frame_id"][r]),
                            p=slot,
                            field=field,
                            seed=seed_v,
                            out=out_v,
                            ref=ref_v,
                        )
                    )
                    if max_rows is not None and len(rows) >= max_rows:
                        return rows
                continue

            out_arr = out_view[field][:, active]
            ref_arr = ref[field][:, active]
            diff = out_arr != ref_arr
            if not np.any(diff):
                continue

            # Most discrete fields are shaped (records, players). Some fields (e.g. state_flags)
            # are shaped (records, players, K). For these, emit per-subindex mismatches with a
            # suffix like "state_flags[3]" so the output stays scalar per row.
            if diff.ndim == 2:
                pairs = np.argwhere(diff)
                for ri, p in pairs:
                    r = int(ri)
                    pp = int(p)
                    gi = int(offset + r)

                    seed_v = int(seed[field][r, pp])
                    out_v = int(out_view[field][r, pp])
                    ref_v = int(ref[field][r, pp])
                    if only_seed_equals_ref and seed_v != ref_v:
                        continue

                    rows.append(
                        MismatchRow(
                            dataset=str(dataset_path),
                            record=gi,
                            seed_frame=int(seed["frame_id"][r]),
                            ref_frame=int(ref["frame_id"][r]),
                            p=pp,
                            field=field,
                            seed=seed_v,
                            out=out_v,
                            ref=ref_v,
                        )
                    )
                    if max_rows is not None and len(rows) >= max_rows:
                        return rows
            else:
                pairs = np.argwhere(diff)
                for ri, p, sub in pairs:
                    r = int(ri)
                    pp = int(p)
                    ss = int(sub)
                    gi = int(offset + r)

                    seed_v = int(seed[field][r, pp, ss])
                    out_v = int(out_view[field][r, pp, ss])
                    ref_v = int(ref[field][r, pp, ss])
                    if only_seed_equals_ref and seed_v != ref_v:
                        continue

                    rows.append(
                        MismatchRow(
                            dataset=str(dataset_path),
                            record=gi,
                            seed_frame=int(seed["frame_id"][r]),
                            ref_frame=int(ref["frame_id"][r]),
                            p=pp,
                            field=f"{field}[{ss}]",
                            seed=seed_v,
                            out=out_v,
                            ref=ref_v,
                        )
                    )
                    if max_rows is not None and len(rows) >= max_rows:
                        return rows

        offset += chunk_n

    return rows


def main() -> None:
    ap = argparse.ArgumentParser(description="Locate per-record discrete mismatches for a dataset.")
    ap.add_argument("--dataset", type=Path, required=True, help="Path to a .msl dataset file.")
    ap.add_argument(
        "--field",
        action="append",
        default=[],
        help=(
            "Discrete compare field to locate (repeatable). For multi-dimensional fields like "
            "`state_flags`, output rows use a suffix like `state_flags[3]`. "
            "Item fields are supported via aliases: item_exists,item_type,item_state,item_owner,item_instance_id "
            "(with `p` interpreted as item slot index)."
        ),
    )
    ap.add_argument("--chunk", type=int, default=4096, help="Batch size for evaluation.")
    ap.add_argument("--max", type=int, default=0, help="Max rows to print (0 = no limit).")
    ap.add_argument(
        "--only-seed-equals-ref",
        action="store_true",
        help="Only emit rows where seed value equals ref value (useful for baseline carry-through diffs).",
    )
    ap.add_argument("--ucf-enabled", type=int, default=None, help="Override UCF enabled (0/1).")
    ap.add_argument("--ucf-cardinals-1-0-enabled", type=int, default=None, help="Override UCF 1.0 cardinals (0/1).")
    ap.add_argument("--format", choices=("jsonl", "tsv"), default="jsonl")
    args = ap.parse_args()

    dataset_path: Path = args.dataset
    fields = tuple(str(f) for f in args.field if str(f))
    if not fields:
        raise SystemExit("error: at least one --field is required")

    max_rows = int(args.max)
    if max_rows <= 0:
        max_rows = None

    ucf_enabled = None if args.ucf_enabled is None else bool(int(args.ucf_enabled))
    ucf_cardinals_1_0_enabled = None if args.ucf_cardinals_1_0_enabled is None else bool(
        int(args.ucf_cardinals_1_0_enabled)
    )

    rows = _iter_dataset_mismatches(
        dataset_path=dataset_path,
        fields=fields,
        chunk=int(args.chunk),
        ucf_enabled=ucf_enabled,
        ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
        max_rows=max_rows,
        only_seed_equals_ref=bool(args.only_seed_equals_ref),
    )

    if args.format == "jsonl":
        for r in rows:
            print(json.dumps(r.__dict__, sort_keys=True))
    else:
        print("\t".join(MismatchRow.__dataclass_fields__.keys()))
        for r in rows:
            print(
                f"{r.dataset}\t{r.record}\t{r.seed_frame}\t{r.ref_frame}\t{r.p}\t{r.field}\t{r.seed}\t{r.out}\t{r.ref}"
            )


if __name__ == "__main__":
    main()
