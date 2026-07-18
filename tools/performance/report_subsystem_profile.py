#!/usr/bin/env python3
"""Render the replay benchmark's cold-path subsystem counters."""

from __future__ import annotations

import argparse
import bisect
import re
import subprocess
from pathlib import Path


BUCKET_RE = re.compile(r"^subsystem_bucket name=(\S+) calls=(\d+) cycles=(\d+)$")
OWNER_RE = re.compile(r"^subsystem_owner address=(0x[0-9a-f]+) calls=(\d+) cycles=(\d+)$")


def load_symbols(binary: Path) -> tuple[list[int], dict[int, str]]:
    result = subprocess.run(
        ["nm", "-n", "--defined-only", str(binary)],
        check=True,
        capture_output=True,
        text=True,
    )
    symbols: dict[int, str] = {}
    for line in result.stdout.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) != 3:
            continue
        try:
            address = int(fields[0], 16)
        except ValueError:
            continue
        symbols[address] = fields[2]
    return sorted(symbols), symbols


def symbol_name(address: int, addresses: list[int], symbols: dict[int, str]) -> str:
    index = bisect.bisect_right(addresses, address) - 1
    if index < 0:
        return f"0x{address:x}"
    base = addresses[index]
    name = symbols[base]
    return name if base == address else f"{name}+0x{address - base:x}"


def row(name: str, calls: int, cycles: int, denominator: int) -> None:
    share = 100.0 * cycles / denominator if denominator else 0.0
    print(f"| `{name}` | {calls:,} | {cycles:,} | {share:.2f}% |")


def table(
    title: str,
    names: list[str],
    buckets: dict[str, tuple[int, int]],
    denominator: int,
) -> None:
    print(f"\n### {title}\n")
    print("| Owner | Calls | Net cycles | Contract share |")
    print("|---|---:|---:|---:|")
    for name in names:
        calls, cycles = buckets[name]
        row(name, calls, cycles, denominator)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--owner-limit", type=int, default=20)
    args = parser.parse_args()

    buckets: dict[str, tuple[int, int]] = {}
    owners: list[tuple[int, int, int]] = []
    timer_overhead: int | None = None
    benchmark_lines: list[str] = []
    for line in args.input.read_text().splitlines():
        if line.startswith("subsystem_profile timer_overhead="):
            timer_overhead = int(line.rsplit("=", 1)[1])
        elif match := BUCKET_RE.match(line):
            buckets[match.group(1)] = (int(match.group(2)), int(match.group(3)))
        elif match := OWNER_RE.match(line):
            owners.append((int(match.group(1), 16), int(match.group(2)), int(match.group(3))))
        elif line.startswith(("replay_benchmark ", "production ", "step_only ")):
            benchmark_lines.append(line)

    contract_names = ["step", "observation", "terminal"]
    required = set(contract_names) | {
        "prepare",
        "scheduler",
        "finish",
        "fighter_animation",
        "action_anim_callback",
        "input_action_callback",
        "physics_callback",
        "stage_collision_callback",
        "contact_publication",
        "finish_fighter_visibility",
        "finish_item_matrices",
        "pose_animation",
        "action_script",
        "secondary_pose",
        "capture_pose",
    }
    missing = sorted(required - buckets.keys())
    if timer_overhead is None or missing:
        parser.error(f"incomplete profile (missing: {', '.join(missing) or 'timer overhead'})")

    denominator = sum(buckets[name][1] for name in contract_names)
    addresses, symbols = load_symbols(args.binary)

    print("## Subsystem profile\n")
    print(f"RDTSCP pair overhead subtracted from each sample: {timer_overhead} cycles.\n")
    print("```text")
    print("\n".join(benchmark_lines))
    print("```")
    table("Public production contract", contract_names, buckets, denominator)
    table("Step phases", ["prepare", "scheduler", "finish"], buckets, denominator)
    table(
        "Fighter and finish drill-down",
        [
            "fighter_animation",
            "action_anim_callback",
            "input_action_callback",
            "physics_callback",
            "stage_collision_callback",
            "contact_publication",
            "finish_fighter_visibility",
            "finish_item_matrices",
        ],
        buckets,
        denominator,
    )
    table(
        "Animation drill-down",
        ["pose_animation", "action_script", "secondary_pose", "capture_pose"],
        buckets,
        denominator,
    )

    sorted_owners = sorted(owners, key=lambda item: item[2], reverse=True)
    displayed_owners = (
        sorted_owners[: args.owner_limit] if args.owner_limit > 0 else sorted_owners
    )
    print(f"\n### Scheduled callback owners (top {len(displayed_owners)} of {len(owners)})\n")
    print("| Owner | Calls | Net cycles | Contract share |")
    print("|---|---:|---:|---:|")
    for address, calls, cycles in displayed_owners:
        row(symbol_name(address, addresses, symbols), calls, cycles, denominator)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
