from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import read_dataset
from tools.eval.run_one_step_eval import COMPARE_DTYPE, _load_binding
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


def _field_at(seed_row: np.void, field: str, p: int, default: int = 0) -> int:
    if field not in seed_row.dtype.names:
        return int(default)
    arr = seed_row[field]
    try:
        return int(arr[p])
    except Exception:
        return int(arr)


def _state_flags_221f(seed_row: np.void, p: int) -> int:
    if "state_flags" not in seed_row.dtype.names:
        return 0
    flags = seed_row["state_flags"]
    try:
        return int(flags[p, 4])
    except Exception:
        return 0


@dataclass(frozen=True)
class ResidualSignature:
    char_id: int
    action_id: int
    action_frame: int
    on_ground: int
    timer_x18c8: int
    owner_set_phase: int
    owner_transition_epoch: int
    terminal_phase: int
    grounded_clear_phase: int
    hitlag: int
    hitstun: int
    combo_count: int
    last_attack_landed: int
    state_flags_221f: int
    seed_last_hit_by: int


@dataclass(frozen=True)
class ResidualRow:
    dataset: str
    record: int
    p: int
    ref: int
    out: int
    sig: ResidualSignature


def _signature(seed_row: np.void, p: int) -> ResidualSignature:
    return ResidualSignature(
        char_id=_field_at(seed_row, "char_id", p),
        action_id=_field_at(seed_row, "action_id", p),
        action_frame=_field_at(seed_row, "action_frame", p),
        on_ground=_field_at(seed_row, "on_ground", p),
        timer_x18c8=_field_at(seed_row, "source_clear_timer_x18c8", p),
        owner_set_phase=_field_at(seed_row, "source_clear_owner_set_phase", p),
        owner_transition_epoch=_field_at(seed_row, "source_clear_owner_transition_epoch", p),
        terminal_phase=_field_at(seed_row, "source_clear_terminal_phase", p),
        grounded_clear_phase=_field_at(seed_row, "source_clear_grounded_damage_clear_phase", p),
        hitlag=_field_at(seed_row, "hitlag", p),
        hitstun=_field_at(seed_row, "hitstun", p),
        combo_count=_field_at(seed_row, "combo_count", p),
        last_attack_landed=_field_at(seed_row, "last_attack_landed", p),
        state_flags_221f=_state_flags_221f(seed_row, p),
        seed_last_hit_by=_field_at(seed_row, "last_hit_by", p),
    )


def _iter_rows(*, suite_path: Path, datasets_dir: str) -> tuple[list[ResidualRow], Counter[tuple[int, int]], Counter[ResidualSignature]]:
    suite = load_suite(suite_path)
    ds_paths = [
        dataset_path_for_suite_replay(
            suite_name=suite.name,
            replay_rel_path=entry.replay,
            datasets_dir=datasets_dir,
        )
        for entry in suite.replays
    ]

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    residual_rows: list[ResidualRow] = []
    family_counts: Counter[tuple[int, int]] = Counter()
    matched_signature_counts: Counter[ResidualSignature] = Counter()

    for ds_path in ds_paths:
        ds = read_dataset(str(ds_path))
        samples = ds.samples
        num_records = int(samples.shape[0])
        num_players = int(ds.header["num_players"])

        chunk_n = min(4096, num_records)
        handle = binding.init(
            batch_size=chunk_n,
            num_players=num_players,
            ucf_enabled=int(bool(suite.ucf_enabled)),
            ucf_cardinals_1_0_enabled=int(bool(suite.ucf_cardinals_1_0_enabled)),
        )
        seed_bytes = np.empty((chunk_n, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((chunk_n, input_stride), dtype=np.uint8)
        input_bytes = np.empty((chunk_n, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((chunk_n, compare_stride), dtype=np.uint8)

        offset = 0
        while offset < num_records:
            cn = min(chunk_n, num_records - offset)
            if cn != seed_bytes.shape[0]:
                handle = binding.init(
                    batch_size=cn,
                    num_players=num_players,
                    ucf_enabled=int(bool(suite.ucf_enabled)),
                    ucf_cardinals_1_0_enabled=int(bool(suite.ucf_cardinals_1_0_enabled)),
                )
                seed_bytes = np.empty((cn, seed_stride), dtype=np.uint8)
                prev_input_bytes = np.empty((cn, input_stride), dtype=np.uint8)
                input_bytes = np.empty((cn, input_stride), dtype=np.uint8)
                out_compare_bytes = np.empty((cn, compare_stride), dtype=np.uint8)

            chunk = samples[offset : offset + cn]
            seed_bytes[:] = np.frombuffer(chunk["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                cn, seed_stride
            )
            prev_input_bytes[:] = np.frombuffer(
                chunk["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).reshape(cn, input_stride)
            input_bytes[:] = np.frombuffer(chunk["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                cn, input_stride
            )
            binding.reseed_seed(handle, seed_bytes)
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)

            out = out_compare_bytes.view(COMPARE_DTYPE).reshape(cn)
            ref = chunk["ref_t1"]
            seed = chunk["seed_t"]

            for i in range(cn):
                for p in range(num_players):
                    r = int(ref["last_hit_by"][i, p])
                    o = int(out["last_hit_by"][i, p])
                    sig = _signature(seed[i], p)
                    family_counts[(r, o)] += 1
                    if r == o:
                        matched_signature_counts[sig] += 1
                    if (r, o) in ((6, 1), (6, 0), (1, 6), (0, 6)):
                        residual_rows.append(
                            ResidualRow(
                                dataset=ds_path.name,
                                record=offset + i,
                                p=p,
                                ref=r,
                                out=o,
                                sig=sig,
                            )
                        )
            offset += cn

    return residual_rows, family_counts, matched_signature_counts


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--suite",
        default="replays/suites/fox_falco_fd_ucf084_recent.json",
        help="Path to suite JSON under repo root",
    )
    ap.add_argument("--datasets-dir", default="datasets")
    ap.add_argument("--top", type=int, default=12, help="Top signature rows to print per family")
    args = ap.parse_args()

    root = repo_root()
    suite_path = (root / args.suite).resolve()
    residual_rows, family_counts, matched_signature_counts = _iter_rows(
        suite_path=suite_path, datasets_dir=str(args.datasets_dir)
    )

    print(f"suite={suite_path.relative_to(root)} rows={len(residual_rows)}")
    print("target families (ref->out):")
    for fam in ((6, 1), (6, 0), (1, 6), (0, 6)):
        print(f"  {fam[0]}->{fam[1]}: {family_counts.get(fam, 0)}")

    by_family: defaultdict[tuple[int, int], list[ResidualRow]] = defaultdict(list)
    for row in residual_rows:
        by_family[(row.ref, row.out)].append(row)

    print()
    print("signature separability evidence (mismatch rows vs matched rows with same causal signature):")
    for fam in ((6, 1), (6, 0), (1, 6), (0, 6)):
        rows = by_family.get(fam, [])
        if not rows:
            continue
        sig_counts: Counter[ResidualSignature] = Counter(r.sig for r in rows)
        print(f"== family {fam[0]}->{fam[1]} ==")
        for sig, mm_count in sig_counts.most_common(int(args.top)):
            matched_count = int(matched_signature_counts.get(sig, 0))
            print(
                "  mm=%d matched_same_sig=%d sig=%s"
                % (
                    mm_count,
                    matched_count,
                    sig,
                )
            )
        sample = rows[0]
        print(
            f"  sample_row dataset={sample.dataset} record={sample.record} p={sample.p} sig={sample.sig}"
        )

    print()
    print("next modeled lane:")
    print(
        "  source_clear_owner_transition_epoch (t-1->t causal): seed a write-epoch/edge lane for "
        "dmg.x18C4 source-owner set/clear events so runtime can disambiguate valid terminal/start ownership "
        "without broad timer-only clearing."
    )


if __name__ == "__main__":
    main()
