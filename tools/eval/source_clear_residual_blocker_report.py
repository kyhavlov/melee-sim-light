from __future__ import annotations

import argparse
import importlib
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _Row:
    dataset: str
    record: int
    p: int
    features: dict[str, int]
    ref: int
    out: int
    good: int
    bad: int


@dataclass(frozen=True)
class _RuleScore:
    rule: str
    good: int
    bad: int

    @property
    def net(self) -> int:
        return int(self.good - self.bad)


def _score_rows(rows: list[_Row], pred) -> _RuleScore:
    g = 0
    b = 0
    for r in rows:
        if pred(r.features):
            g += int(r.good)
            b += int(r.bad)
    return _RuleScore(rule="", good=g, bad=b)


def _search_simple_rules(rows: list[_Row]) -> tuple[list[_RuleScore], list[_RuleScore]]:
    if not rows:
        return [], []

    feature_keys = sorted(rows[0].features.keys())
    single: list[_RuleScore] = []
    pair: list[_RuleScore] = []

    def _append_single(rule: str, pred) -> None:
        s = _score_rows(rows, pred)
        if s.net > 0:
            single.append(_RuleScore(rule=rule, good=s.good, bad=s.bad))

    for k in feature_keys:
        vals = sorted({r.features[k] for r in rows})
        for v in vals:
            _append_single(f"{k}=={v}", lambda f, kk=k, vv=v: f[kk] == vv)
        if vals:
            for v in vals:
                _append_single(f"{k}<={v}", lambda f, kk=k, vv=v: f[kk] <= vv)
                _append_single(f"{k}>={v}", lambda f, kk=k, vv=v: f[kk] >= vv)

    # Pairwise conjunction from top-N single rules only, to keep runtime bounded.
    single_sorted = sorted(single, key=lambda s: (s.net, s.good), reverse=True)
    top_rules = [s.rule for s in single_sorted[:64]]
    if not top_rules:
        return [], []

    def _parse(rule: str):
        if "==" in rule:
            k, v = rule.split("==", 1)
            v_i = int(v)
            return lambda f, kk=k, vv=v_i: f[kk] == vv
        if "<=" in rule:
            k, v = rule.split("<=", 1)
            v_i = int(v)
            return lambda f, kk=k, vv=v_i: f[kk] <= vv
        if ">=" in rule:
            k, v = rule.split(">=", 1)
            v_i = int(v)
            return lambda f, kk=k, vv=v_i: f[kk] >= vv
        raise ValueError(f"unsupported rule: {rule}")

    parsed = [(r, _parse(r)) for r in top_rules]
    for i in range(len(parsed)):
        r1, p1 = parsed[i]
        for j in range(i + 1, len(parsed)):
            r2, p2 = parsed[j]
            s = _score_rows(rows, lambda f, pp1=p1, pp2=p2: pp1(f) and pp2(f))
            if s.net > 0:
                pair.append(_RuleScore(rule=f"{r1} AND {r2}", good=s.good, bad=s.bad))

    return single_sorted, sorted(pair, key=lambda s: (s.net, s.good), reverse=True)


def _load_binding():
    return importlib.import_module("msl_binding")


def _suite_dataset_paths(suite_path: Path, datasets_dir: Path) -> list[Path]:
    suite = json.loads(suite_path.read_text())
    suite_name = suite_path.stem
    out: list[Path] = []
    for r in suite["replays"]:
        rel = Path(r["replay"]).with_suffix(".msl")
        out.append(datasets_dir / suite_name / rel)
    return out


def _iter_rows_for_suite(suite_path: Path, datasets_dir: Path) -> list[_Row]:
    binding = _load_binding()
    out_rows: list[_Row] = []
    for ds_path in _suite_dataset_paths(suite_path, datasets_dir):
        ds = read_dataset(str(ds_path))
        samples = ds.samples
        n = int(samples.shape[0])
        num_players = int(ds.header["num_players"])

        sizes = binding.sizes()
        seed_stride = int(sizes["seed"])
        input_stride = int(sizes["input"])
        compare_stride = int(sizes["compare"])
        h = binding.init(batch_size=n, num_players=num_players)

        seed_b = (
            np.frombuffer(samples["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(n, seed_stride)
            .copy()
        )
        prev_b = (
            np.frombuffer(samples["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(n, input_stride)
            .copy()
        )
        in_b = (
            np.frombuffer(samples["input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(n, input_stride)
            .copy()
        )
        out_b = np.empty((n, compare_stride), dtype=np.uint8)

        binding.reseed_seed(h, seed_b)
        binding.step_input(h, prev_b, in_b)
        binding.write_compare(h, out_b)

        out = out_b.view(COMPARE_DTYPE).reshape(-1)
        seed = samples["seed_t"]
        ref = samples["ref_t1"]
        for i in range(1, n):
            for p in range(num_players):
                if int(seed["source_clear_timer_x18c8"][i, p]) <= 0:
                    continue
                if int(seed["source_clear_owner_set_phase"][i, p]) == 0:
                    continue
                if int(seed["on_ground"][i, p]) == 0:
                    continue
                if int(seed["hitlag"][i, p]) != 0 or int(seed["hitstun"][i, p]) != 0:
                    continue
                if int(seed["state_flags"][i, p, 4]) & 0x10:
                    continue
                r = int(ref["last_hit_by"][i, p])
                o = int(out["last_hit_by"][i, p])
                feats = {
                    "char_id": int(seed["char_id"][i, p]),
                    "action_id": int(seed["action_id"][i, p]),
                    "action_frame": int(np.int16(seed["action_frame"][i, p])),
                    "timer_x18c8": int(seed["source_clear_timer_x18c8"][i, p]),
                    "combo_count": int(seed["combo_count"][i, p]),
                    "last_attack_landed": int(seed["last_attack_landed"][i, p]),
                    "seed_last_hit_by": int(seed["last_hit_by"][i, p]),
                    "prev_action_id": int(seed["action_id"][i - 1, p]),
                    "prev_action_frame": int(np.int16(seed["action_frame"][i - 1, p])),
                    "prev_timer_x18c8": int(seed["source_clear_timer_x18c8"][i - 1, p]),
                }
                out_rows.append(
                    _Row(
                        dataset=str(ds_path),
                        record=i,
                        p=p,
                        features=feats,
                        ref=r,
                        out=o,
                        good=1 if (r == 6 and o != 6) else 0,
                        bad=1 if (r != 6 and o == r) else 0,
                    )
                )
    return out_rows


def _build_summary(rows: list[_Row]) -> dict:
    family_counts: dict[str, int] = {}
    mismatch_family_counts: dict[str, int] = {}
    for r in rows:
        key = f"{r.ref}->{r.out}"
        family_counts[key] = int(family_counts.get(key, 0) + 1)
        if r.ref != r.out:
            mismatch_family_counts[key] = int(mismatch_family_counts.get(key, 0) + 1)
    single, pair = _search_simple_rules(rows)
    top_single = [{"rule": s.rule, "good": s.good, "bad": s.bad, "net": s.net} for s in single[:20]]
    top_pair = [{"rule": s.rule, "good": s.good, "bad": s.bad, "net": s.net} for s in pair[:20]]
    good_total = int(sum(r.good for r in rows))
    bad_total = int(sum(r.bad for r in rows))
    return {
        "row_count": int(len(rows)),
        "family_counts": family_counts,
        "mismatch_family_counts": mismatch_family_counts,
        "good_total": good_total,
        "bad_total": bad_total,
        "top_single_positive_rules": top_single,
        "top_pair_positive_rules": top_pair,
        "blocker": (
            "no positive-net single/pair causal predicates from existing seed-visible lanes; "
            "next modeled lane should promote source_clear_processhit_damage_pending_phase"
        ),
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--suite",
        type=Path,
        default=Path("replays/suites/fox_falco_fd_ucf084_recent.json"),
    )
    ap.add_argument("--datasets-dir", type=Path, default=Path("datasets"))
    ap.add_argument("--json-out", type=Path, default=None)
    args = ap.parse_args()

    rows = _iter_rows_for_suite(args.suite, args.datasets_dir)
    summary = _build_summary(rows)

    print(
        f"rows={summary['row_count']} good_total={summary['good_total']} bad_total={summary['bad_total']}"
    )
    print("families:")
    for k, v in sorted(summary["family_counts"].items(), key=lambda kv: kv[1], reverse=True):
        print(f"  {k}: {v}")
    print(
        f"top_single_positive_rules={len(summary['top_single_positive_rules'])} "
        f"top_pair_positive_rules={len(summary['top_pair_positive_rules'])}"
    )
    print(f"blocker: {summary['blocker']}")

    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(summary, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
