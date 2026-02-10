from __future__ import annotations

"""Summarize shield-candidate reject reasons from forensic_rows.json."""

import argparse
import json
from collections import Counter
from pathlib import Path


REASON_LABEL = {
    0: "ACCEPT_SHIELD",
    1: "REJECT_ATTACKER_STOCKS_ZERO",
    2: "REJECT_DEFENDER_STOCKS_ZERO",
    3: "REJECT_TEAMS_FRIENDLY",
    4: "REJECT_HITLAG_GATE",
    5: "REJECT_SHIELD_INACTIVE",
    6: "REJECT_HITBOX_DISABLED",
    7: "REJECT_GROUND_AIR_FLAGS",
    8: "REJECT_HITLIST_CONTAINS",
    9: "REJECT_SHIELD_GEOM_NO_OVERLAP",
    10: "REJECT_INERT_ELEMENT",
    11: "REJECT_NONPOS_DAMAGE",
}

DECOMP_GATE = {
    4: "ftColl_80078C70 pair hitlag gate",
    5: "ftColl_80078C70 shield descriptor active gate",
    7: "ftColl_80078C70 hitcapsule ground/air eligibility",
    8: "lbColl_8000ACFC rehit gate (pre-lbColl_80007BCC)",
    9: "lbColl_80007BCC shield overlap geometry",
    10: "ftColl_80078C70 inert branch (skip ftColl_80076CBC)",
    11: "ftColl_80076CBC damaging-hit path precondition",
}


def _reason_label(code: int) -> str:
    return REASON_LABEL.get(code, f"UNKNOWN_{code}")


def _gate_label(code: int) -> str:
    return DECOMP_GATE.get(code, "n/a")


def _hypothesis(first_reason: int) -> str:
    if first_reason == 8:
        return "Runtime hitlist ownership/order mismatch before shield geometry gate."
    if first_reason == 9:
        return "Runtime shield-geometry/timebase ordering mismatch on lbColl_80007BCC equivalent."
    if first_reason == 7:
        return "Runtime ground/air eligibility timing mismatch in ftColl_80078C70 path."
    if first_reason == 6:
        return "Runtime hitbox enable-edge timing mismatch before shield eligibility path."
    if first_reason == 4:
        return "Runtime hitlag latch/decrement ordering mismatch suppressing shield candidates."
    return "Inspect first failing gate lanes for minimal ftColl_80078C70/lbColl_80007BCC ordering fix."


def _row_name(row: dict[str, object]) -> str:
    hdr = row["row"]
    return f"{Path(str(hdr['dataset'])).name}:{int(hdr['record'])}:{int(hdr['p'])}"


def _summarize_row(row: dict[str, object]) -> dict[str, object]:
    target_p = int(row["row"]["p"])
    decisions = row["pre_combat"]["shield_candidate_decisions"]
    defender_rows = [d for d in decisions if int(d["defender"]) == target_p]
    pair_rows = [d for d in defender_rows if int(d["source_kind"]) == 1]
    hb_rows = [d for d in defender_rows if int(d["source_kind"]) == 0]

    pair_counts = Counter(int(d["reject_reason"]) for d in pair_rows)
    hb_counts = Counter(int(d["reject_reason"]) for d in hb_rows)

    first_pair_fail = next((d for d in pair_rows if int(d["reject_reason"]) != 0), None)
    first_hb_fail = next((d for d in hb_rows if int(d["reject_reason"]) != 0), None)

    if first_pair_fail is not None:
        first_fail = first_pair_fail
    else:
        first_fail = first_hb_fail

    first_reason = int(first_fail["reject_reason"]) if first_fail is not None else 0
    first_attacker = int(first_fail["attacker"]) if first_fail is not None else -1
    first_hitbox = int(first_fail["hitbox_id"]) if first_fail is not None else -1

    return {
        "row": _row_name(row),
        "pair_reject_counts": {_reason_label(k): int(v) for k, v in sorted(pair_counts.items())},
        "hitbox_reject_counts": {_reason_label(k): int(v) for k, v in sorted(hb_counts.items())},
        "first_failing_reason": _reason_label(first_reason),
        "first_failing_gate": _gate_label(first_reason),
        "first_failing_attacker": first_attacker,
        "first_failing_hitbox_id": first_hitbox,
        "recommendation": _hypothesis(first_reason),
    }


def _as_markdown(rows: list[dict[str, object]]) -> str:
    lines: list[str] = []
    lines.append("# shield_reject_reasons")
    lines.append("")
    lines.append("| row | first failing reason | decomp gate | recommendation |")
    lines.append("| --- | --- | --- | --- |")
    for r in rows:
        lines.append(
            f"| `{r['row']}` | `{r['first_failing_reason']}` | {r['first_failing_gate']} | {r['recommendation']} |"
        )
    lines.append("")
    lines.append("## reject counts")
    lines.append("")
    for r in rows:
        lines.append(f"### `{r['row']}`")
        lines.append(f"- pair_reject_counts: {json.dumps(r['pair_reject_counts'], sort_keys=True)}")
        lines.append(f"- hitbox_reject_counts: {json.dumps(r['hitbox_reject_counts'], sort_keys=True)}")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def main() -> None:
    ap = argparse.ArgumentParser(description="Summarize shield-candidate reject reasons.")
    ap.add_argument("--forensic-json", type=Path, required=True, help="Path to forensic_rows.json")
    ap.add_argument(
        "--out-dir",
        type=Path,
        required=True,
        help="Output directory (writes shield_reject_summary.json/.md)",
    )
    args = ap.parse_args()

    payload = json.loads(args.forensic_json.read_text(encoding="utf-8"))
    rows = [_summarize_row(r) for r in payload.get("rows", [])]
    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    out_json = out_dir / "shield_reject_summary.json"
    out_md = out_dir / "shield_reject_summary.md"
    out_json.write_text(json.dumps({"rows": rows}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    out_md.write_text(_as_markdown(rows), encoding="utf-8")
    print(f"wrote {out_json}")
    print(f"wrote {out_md}")


if __name__ == "__main__":
    main()
