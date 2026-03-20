from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

from tools.eval.dataset import read_dataset


ACT_CATCH_WAIT = 0x00D8
ACT_CATCH_ATTACK = 0x00D9
ACT_CAPTURE_WAIT_HI = 0x00E0
ACT_CAPTURE_WAIT_LW = 0x00E3


@dataclass(frozen=True)
class CaptureWaitOwnerTickCase:
    record: int
    owner_p: int
    victim_p: int
    owner_seed_action: int
    owner_seed_action_frame: int
    owner_ref_action: int
    owner_ref_action_frame: int
    victim_seed_action: int
    victim_seed_action_frame: int
    victim_ref_action: int
    victim_ref_action_frame: int
    slot_order: str
    classification: str


def classify_capturewait_owner_tick_case(
    *,
    owner_p: int,
    victim_p: int,
    owner_seed_action: int,
    owner_seed_action_frame: int,
    owner_ref_action: int,
    owner_ref_action_frame: int,
    victim_seed_action: int,
    victim_seed_action_frame: int,
    victim_ref_action: int,
    victim_ref_action_frame: int,
) -> str | None:
    if owner_seed_action not in (ACT_CATCH_WAIT, ACT_CATCH_ATTACK):
        return None
    if owner_ref_action not in (ACT_CATCH_WAIT, ACT_CATCH_ATTACK):
        return None
    if victim_seed_action not in (ACT_CAPTURE_WAIT_HI, ACT_CAPTURE_WAIT_LW):
        return None
    if victim_ref_action not in (ACT_CAPTURE_WAIT_HI, ACT_CAPTURE_WAIT_LW):
        return None

    if (
        owner_p > victim_p
        and owner_seed_action_frame == 1
        and owner_ref_action_frame == 2
        and victim_seed_action_frame == 1
        and victim_ref_action_frame == 3
    ):
        return "lower_slot_extra_tick_modeled"

    if (
        owner_seed_action_frame in (0, 1)
        and owner_ref_action_frame == owner_seed_action_frame + 1
        and victim_seed_action_frame == 1
        and victim_ref_action_frame == 2
    ):
        return "steady_capturewait_no_extra_tick_blocker"

    return None


def collect_capturewait_owner_tick_cases(dataset_path: str | Path) -> list[CaptureWaitOwnerTickCase]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(ds.header["num_records"])
    num_players = int(ds.header["num_players"])
    out: list[CaptureWaitOwnerTickCase] = []

    for rec in range(num_records):
        seed_t = samples[rec]["seed_t"]
        ref_t1 = samples[rec]["ref_t1"]
        for owner_p in range(num_players):
            for victim_p in range(num_players):
                if owner_p == victim_p:
                    continue
                if int(seed_t["grab_owner_port"][victim_p]) != owner_p:
                    continue
                classification = classify_capturewait_owner_tick_case(
                    owner_p=owner_p,
                    victim_p=victim_p,
                    owner_seed_action=int(seed_t["action_id"][owner_p]),
                    owner_seed_action_frame=int(seed_t["action_frame"][owner_p]),
                    owner_ref_action=int(ref_t1["action_id"][owner_p]),
                    owner_ref_action_frame=int(ref_t1["action_frame"][owner_p]),
                    victim_seed_action=int(seed_t["action_id"][victim_p]),
                    victim_seed_action_frame=int(seed_t["action_frame"][victim_p]),
                    victim_ref_action=int(ref_t1["action_id"][victim_p]),
                    victim_ref_action_frame=int(ref_t1["action_frame"][victim_p]),
                )
                if classification is None:
                    continue
                out.append(
                    CaptureWaitOwnerTickCase(
                        record=rec,
                        owner_p=owner_p,
                        victim_p=victim_p,
                        owner_seed_action=int(seed_t["action_id"][owner_p]),
                        owner_seed_action_frame=int(seed_t["action_frame"][owner_p]),
                        owner_ref_action=int(ref_t1["action_id"][owner_p]),
                        owner_ref_action_frame=int(ref_t1["action_frame"][owner_p]),
                        victim_seed_action=int(seed_t["action_id"][victim_p]),
                        victim_seed_action_frame=int(seed_t["action_frame"][victim_p]),
                        victim_ref_action=int(ref_t1["action_id"][victim_p]),
                        victim_ref_action_frame=int(ref_t1["action_frame"][victim_p]),
                        slot_order="owner_later" if owner_p > victim_p else "owner_earlier",
                        classification=classification,
                    )
                )
    return out


def _write_tsv(cases: Iterable[CaptureWaitOwnerTickCase]) -> None:
    print(
        "\t".join(
            (
                "record",
                "owner_p",
                "victim_p",
                "owner_seed_action",
                "owner_seed_action_frame",
                "owner_ref_action",
                "owner_ref_action_frame",
                "victim_seed_action",
                "victim_seed_action_frame",
                "victim_ref_action",
                "victim_ref_action_frame",
                "slot_order",
                "classification",
            )
        )
    )
    for case in cases:
        print(
            "\t".join(
                str(v)
                for v in (
                    case.record,
                    case.owner_p,
                    case.victim_p,
                    case.owner_seed_action,
                    case.owner_seed_action_frame,
                    case.owner_ref_action,
                    case.owner_ref_action_frame,
                    case.victim_seed_action,
                    case.victim_seed_action_frame,
                    case.victim_ref_action,
                    case.victim_ref_action_frame,
                    case.slot_order,
                    case.classification,
                )
            )
        )


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Classify CaptureWait owner/victim callback-order families from replay seed/ref rows."
    )
    ap.add_argument("--dataset", required=True, help="dataset .msl path")
    ap.add_argument("--format", choices=("tsv", "json"), default="tsv")
    args = ap.parse_args()

    cases = collect_capturewait_owner_tick_cases(args.dataset)
    if args.format == "json":
        print(json.dumps([asdict(case) for case in cases], indent=2, sort_keys=True))
        return
    _write_tsv(cases)


if __name__ == "__main__":
    main()
