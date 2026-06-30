from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path

import numpy as np

from tools.eval.dataset import read_dataset
from tools.slippi.suite_io import dataset_path_for_suite_replay, load_suite, repo_root


def _count_action_ids(arr: np.ndarray, *, num_players: int) -> Counter[int]:
    # arr: [n, MAX_PLAYERS] u16
    ids = arr[:, :num_players].reshape(-1)
    uniq, counts = np.unique(ids, return_counts=True)
    return Counter({int(k): int(v) for k, v in zip(uniq, counts)})


def main() -> None:
    ap = argparse.ArgumentParser(
        description="List unique GALE01 action_ids present in a replay suite's preprocessed .msl datasets."
    )
    ap.add_argument("--suite", required=True, help="Suite JSON (e.g. replays/suites/...)")
    ap.add_argument(
        "--datasets-dir",
        default="datasets",
        help="Legacy datasets cache dir for this dataset-only triage helper.",
    )
    ap.add_argument(
        "--seed-only",
        action="store_true",
        help="Only count action_ids in seed_t (exclude ref_t1)",
    )
    ap.add_argument(
        "--top",
        type=int,
        default=0,
        help="If >0, print only the top-N most frequent action_ids",
    )
    args = ap.parse_args()

    root = repo_root()
    suite_path = (root / args.suite).resolve()
    suite = load_suite(suite_path)

    total = Counter()
    missing = 0
    n_files = 0
    for entry in suite.replays:
        ds_path = dataset_path_for_suite_replay(
            suite_name=suite.name,
            replay_rel_path=entry.replay,
            datasets_dir=args.datasets_dir,
        )
        if not ds_path.exists():
            print(f"missing dataset: {ds_path.relative_to(root)}")
            missing += 1
            continue
        n_files += 1
        ds = read_dataset(str(ds_path))
        num_players = int(ds.header["num_players"])
        total += _count_action_ids(ds.samples["seed_t"]["action_id"], num_players=num_players)
        if not args.seed_only:
            total += _count_action_ids(ds.samples["ref_t1"]["action_id"], num_players=num_players)

    if missing:
        raise SystemExit(2)

    pairs = sorted(total.items(), key=lambda kv: (-kv[1], kv[0]))
    if args.top and args.top > 0:
        pairs = pairs[: args.top]

    print(f"suite: {suite.name}")
    print(f"datasets: {n_files}  unique_action_ids: {len(total)}")
    print("action_id  count")
    for aid, cnt in pairs:
        print(f"{aid:5d} (0x{aid:04X})  {cnt}")


if __name__ == "__main__":
    main()
