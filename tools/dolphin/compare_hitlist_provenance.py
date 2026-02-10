from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def _timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def _load_json(path: Path) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"expected JSON object in {path}")
    return payload


def _resolve_rows_payload(path: Path) -> tuple[Path, dict[str, Any]]:
    payload = _load_json(path)
    if "rows" in payload:
        return path.resolve(), payload
    if "rows_json" in payload:
        rows_token = Path(str(payload["rows_json"]))
        if rows_token.is_absolute():
            rows_json = rows_token
        else:
            candidate_summary_rel = (path.parent / rows_token).resolve()
            candidate_repo_rel = (Path.cwd() / rows_token).resolve()
            rows_json = candidate_summary_rel if candidate_summary_rel.exists() else candidate_repo_rel
        rows_payload = _load_json(rows_json)
        if "rows" not in rows_payload:
            raise ValueError(f"summary rows_json does not contain rows: {rows_json}")
        return rows_json, rows_payload
    raise ValueError(f"unrecognized input format (expected extract rows JSON or forensic summary): {path}")


def _lane_owner(field: str) -> dict[str, str]:
    if field == "group":
        return {
            "owner": "ftColl_800768A0",
            "source": "refs/melee/src/melee/ft/ftcoll.c:285 (matches HitCapsule.x4 group for copy/clear)",
        }
    if field.startswith("victims1_"):
        return {
            "owner": "lbColl_8000ACFC / lbColl_80008688 / ftColl_80076CBC",
            "source": (
                "refs/melee/src/melee/lb/lbcollision.c:2672,1803 and "
                "refs/melee/src/melee/ft/ftcoll.c:434"
            ),
        }
    if field.startswith("victims2_"):
        return {
            "owner": "lbColl_80008820 / tip-log checks",
            "source": "refs/melee/src/melee/lb/lbcollision.c:1858 and refs/melee/src/melee/ft/ftcoll.c:523",
        }
    if field == "owner_gobj":
        return {
            "owner": "HitCapsule.owner lineage",
            "source": "refs/melee/src/melee/lb/types.h:81 and refs/melee/src/melee/ft/ftcoll.c:611",
        }
    return {"owner": "unknown", "source": ""}


def _coerce_hitlist(row: dict[str, Any]) -> list[dict[str, Any]]:
    raw = row.get("hitlist_provenance", [])
    if not isinstance(raw, list):
        return []
    out = sorted(raw, key=lambda v: int(v.get("hitbox_id", 0)))
    return out


def _compare_hitbox(
    left_hb: dict[str, Any], right_hb: dict[str, Any], *, hitbox_id: int
) -> list[dict[str, Any]]:
    deltas: list[dict[str, Any]] = []
    scalar_fields = ("group", "victims1_cursor", "victims2_cursor", "owner_gobj")
    for field in scalar_fields:
        lv = left_hb.get(field)
        rv = right_hb.get(field)
        if lv != rv:
            deltas.append(
                {
                    "hitbox_id": hitbox_id,
                    "field": field,
                    "left": lv,
                    "right": rv,
                    "decomp_owner": _lane_owner(field),
                }
            )

    vector_fields = ("victims1_ptr", "victims1_cooldown", "victims2_ptr", "victims2_cooldown")
    for field in vector_fields:
        lvals = [int(x) for x in left_hb.get(field, [])]
        rvals = [int(x) for x in right_hb.get(field, [])]
        n = min(len(lvals), len(rvals))
        for idx in range(n):
            if lvals[idx] != rvals[idx]:
                lane = f"{field}[{idx}]"
                deltas.append(
                    {
                        "hitbox_id": hitbox_id,
                        "field": lane,
                        "left": int(lvals[idx]),
                        "right": int(rvals[idx]),
                        "decomp_owner": _lane_owner(field),
                    }
                )
        if len(lvals) != len(rvals):
            deltas.append(
                {
                    "hitbox_id": hitbox_id,
                    "field": f"{field}_length",
                    "left": len(lvals),
                    "right": len(rvals),
                    "decomp_owner": _lane_owner(field),
                }
            )
    return deltas


def compare_rows(
    *,
    left_payload: dict[str, Any],
    right_payload: dict[str, Any],
    left_label: str,
    right_label: str,
) -> dict[str, Any]:
    left_rows = sorted(left_payload["rows"], key=lambda r: int(r["frame_index"]))
    right_rows = sorted(right_payload["rows"], key=lambda r: int(r["frame_index"]))
    compare_len = min(len(left_rows), len(right_rows))

    frame_deltas: list[dict[str, Any]] = []
    for idx in range(compare_len):
        left_row = left_rows[idx]
        right_row = right_rows[idx]
        left_hl = _coerce_hitlist(left_row)
        right_hl = _coerce_hitlist(right_row)
        if len(left_hl) != len(right_hl):
            frame_deltas.append(
                {
                    "frame_offset": idx,
                    "left_frame_index": int(left_row["frame_index"]),
                    "right_frame_index": int(right_row["frame_index"]),
                    "deltas": [
                        {
                            "hitbox_id": -1,
                            "field": "hitlist_length",
                            "left": len(left_hl),
                            "right": len(right_hl),
                            "decomp_owner": _lane_owner("group"),
                        }
                    ],
                }
            )
            continue

        deltas: list[dict[str, Any]] = []
        for hb_idx in range(len(left_hl)):
            deltas.extend(_compare_hitbox(left_hl[hb_idx], right_hl[hb_idx], hitbox_id=hb_idx))
        if deltas:
            frame_deltas.append(
                {
                    "frame_offset": idx,
                    "left_frame_index": int(left_row["frame_index"]),
                    "right_frame_index": int(right_row["frame_index"]),
                    "deltas": deltas,
                }
            )

    first_divergence = frame_deltas[0] if frame_deltas else None
    return {
        "left_label": left_label,
        "right_label": right_label,
        "rows_compared": compare_len,
        "left_row_count": len(left_rows),
        "right_row_count": len(right_rows),
        "divergent_frame_count": len(frame_deltas),
        "first_divergence": first_divergence,
        "frame_deltas": frame_deltas,
    }


def _markdown_report(payload: dict[str, Any]) -> str:
    lines: list[str] = []
    lines.append("# hitlist_provenance_compare")
    lines.append(
        f"- left: `{payload['left_label']}`  right: `{payload['right_label']}`"
    )
    lines.append(
        f"- rows_compared: {payload['rows_compared']} "
        f"(left={payload['left_row_count']} right={payload['right_row_count']})"
    )
    lines.append(f"- divergent_frame_count: {payload['divergent_frame_count']}")
    first = payload.get("first_divergence")
    if first is None:
        lines.append("- first_divergence: none")
        return "\n".join(lines).rstrip() + "\n"

    lines.append(
        f"- first_divergence: frame_offset={first['frame_offset']} "
        f"left_frame={first['left_frame_index']} right_frame={first['right_frame_index']}"
    )
    lines.append("")
    lines.append("## First Divergence Deltas")
    for delta in first["deltas"][:40]:
        owner = delta["decomp_owner"]["owner"]
        source = delta["decomp_owner"]["source"]
        lines.append(
            f"- hb={delta['hitbox_id']} {delta['field']}: {delta['left']} -> {delta['right']} "
            f"(owner={owner}; source={source})"
        )
    if len(first["deltas"]) > 40:
        lines.append(f"- ... {len(first['deltas']) - 40} additional deltas omitted")
    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Compare extracted hitlist-provenance rows frame-by-frame."
    )
    ap.add_argument(
        "--left",
        required=True,
        type=Path,
        help="left input JSON: engine_dump_rows.json or forensic_row_dump summary.json",
    )
    ap.add_argument(
        "--right",
        required=True,
        type=Path,
        help="right input JSON: engine_dump_rows.json or forensic_row_dump summary.json",
    )
    ap.add_argument("--left-label", default="left")
    ap.add_argument("--right-label", default="right")
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path("reports/triage") / f"{_timestamp()}_hitlist_provenance",
    )
    args = ap.parse_args()

    left_rows_path, left_payload = _resolve_rows_payload(args.left.resolve())
    right_rows_path, right_payload = _resolve_rows_payload(args.right.resolve())
    out = compare_rows(
        left_payload=left_payload,
        right_payload=right_payload,
        left_label=args.left_label,
        right_label=args.right_label,
    )
    out["left_rows_json"] = str(left_rows_path)
    out["right_rows_json"] = str(right_rows_path)

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    json_path = out_dir / "hitlist_provenance_compare.json"
    md_path = out_dir / "hitlist_provenance_compare.md"
    json_path.write_text(json.dumps(out, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    md_path.write_text(_markdown_report(out), encoding="utf-8")
    print(f"wrote {json_path}")
    print(f"wrote {md_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
