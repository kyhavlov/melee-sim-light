from __future__ import annotations

"""Select and audit SlippiLab Sheik replays for the staged Sheik port.

This is a suite-construction tool, not a gameplay burndown tool. It writes an
official Sheik suite plus a disjoint held-out telemetry suite, and it records
why every downloaded/audited candidate was accepted or rejected.
"""

import argparse
import hashlib
import json
import shutil
import time
import urllib.request
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from peppi_py import _read_slippi

from tools.extraction.char_registry import CHARS
from tools.slippi.slpz import compress_path
from tools.slippi.suite_io import repo_root


USER_AGENT = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "MeleeSimSheikIntake/1.0"
)

CHAR_BY_EXTERNAL_ID: dict[int, str] = {2: "Fox", 9: "Marth", 19: "Sheik", 20: "Falco"}
INTERNAL_BY_EXTERNAL_ID: dict[int, int] = {
    info.external_id: info.internal_id
    for name, info in CHARS.items()
    if name in {"fox", "falco", "marth", "sheik"}
}
SHEIK_EXTERNAL_ID = 19
SUPPORTED_STAGES: dict[int, str] = {
    2: "fountain_of_dreams",
    3: "frozen_pokemon_stadium",
    8: "yoshis_story",
    28: "dream_land",
    31: "battlefield",
    32: "final_destination",
}
STAGE_ORDER: tuple[int, ...] = (31, 28, 32, 2, 3, 8)
OPPONENT_ORDER: tuple[str, ...] = ("Fox", "Marth", "Falco", "Sheik")
HELDOUT_OPPONENT_ORDER: tuple[str, ...] = ("Fox", "Marth", "Falco")


@dataclass(frozen=True)
class Candidate:
    row: dict[str, Any]
    audit: dict[str, Any]
    cache_path: Path

    @property
    def file_name(self) -> str:
        return str(self.row["file_name"])

    @property
    def stage_id(self) -> int:
        return int(self.row["external_stage_id"])

    @property
    def played_on(self) -> str:
        return str(self.row.get("played_on") or "")

    @property
    def opponent(self) -> str:
        return _metadata_opponent(self.row)


def _metadata_rows(raw: Any) -> list[dict[str, Any]]:
    if isinstance(raw, dict) and isinstance(raw.get("data"), list):
        raw = raw["data"]
    if not isinstance(raw, list):
        raise ValueError("SlippiLab replay metadata must be a list or {'data': list}")
    return [row for row in raw if isinstance(row, dict)]


def _http_json(url: str) -> Any:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as resp:
        return json.loads(resp.read().decode("utf-8"))


def _fetch_metadata(path: Path, *, refresh: bool) -> list[dict[str, Any]]:
    if path.exists() and not refresh:
        return _metadata_rows(json.loads(path.read_text()))
    rows = _metadata_rows(_http_json("https://slippilab.com/api/replays"))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(rows, indent=2, sort_keys=True) + "\n")
    return rows


def _players(row: dict[str, Any]) -> list[dict[str, Any]]:
    players = row.get("players") or []
    return players if isinstance(players, list) else []


def _played_date(row: dict[str, Any]) -> str:
    return str(row.get("played_on") or "")[:10]


def _metadata_opponent(row: dict[str, Any]) -> str:
    chars = [CHAR_BY_EXTERNAL_ID[int(p["external_character_id"])] for p in _players(row)]
    non_sheik = [c for c in chars if c != "Sheik"]
    return non_sheik[0] if non_sheik else "Sheik"


def _download_replay(row: dict[str, Any], replay_dir: Path) -> Path:
    replay_dir.mkdir(parents=True, exist_ok=True)
    name = str(row["file_name"])
    out = replay_dir / name
    if out.exists():
        return out
    req = urllib.request.Request(
        f"https://slippilab.com/api/replay/{name}",
        headers={"User-Agent": USER_AGENT},
    )
    with urllib.request.urlopen(req, timeout=60) as resp:
        out.write_bytes(resp.read())
    time.sleep(0.08)
    return out


def _prelim_reject_reason(
    row: dict[str, Any], *, min_frames: int, max_frames: int, played_after: str
) -> str | None:
    if bool(row.get("is_teams")):
        return "teams"
    if int(row.get("external_stage_id") or -1) not in SUPPORTED_STAGES:
        return "unsupported_stage"
    frames = int(row.get("num_frames") or 0)
    if frames < min_frames:
        return "too_short"
    if frames > max_frames:
        return "too_long"
    played = _played_date(row)
    if played and played < played_after:
        return "too_old"
    players = _players(row)
    if len(players) != 2:
        return "not_singles_two_player"
    char_ids = [int(p.get("external_character_id") or -1) for p in players]
    if SHEIK_EXTERNAL_ID not in char_ids:
        return "no_sheik"
    if any(cid not in CHAR_BY_EXTERNAL_ID for cid in char_ids):
        return "unsupported_character"
    return None


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _port_num(port: str) -> int:
    if port.startswith("P"):
        return int(port[1:])
    return int(port)


def _scan_character_changes(game: Any, expected_by_port: dict[str, int]) -> dict[str, Any]:
    bad: dict[str, Counter[int]] = {port: Counter() for port in expected_by_port}
    for frame in game.frames:
        ports = frame["ports"]
        for port, expected in expected_by_port.items():
            try:
                char = ports[port]["leader"]["post"]["character"]
            except (KeyError, TypeError):
                continue
            if char is not None and int(char) != expected:
                bad[port][int(char)] += 1
    return {port: dict(counter) for port, counter in bad.items() if counter}


def _audit_candidate(row: dict[str, Any], cache_dir: Path) -> Candidate:
    cache_path = _download_replay(row, cache_dir)
    audit: dict[str, Any] = {
        "file_name": row["file_name"],
        "slippilab_id": row.get("id"),
        "played_on": row.get("played_on"),
        "num_frames_metadata": row.get("num_frames"),
        "stage_id_metadata": row.get("external_stage_id"),
        "sha256": _sha256(cache_path),
        "players_metadata": row.get("players"),
        "accepted": False,
        "rejection_reason": "",
    }
    try:
        game = _read_slippi(str(cache_path), False)
    except Exception as exc:
        audit["rejection_reason"] = f"parse_error:{str(exc)[:160]}"
        return Candidate(row=row, audit=audit, cache_path=cache_path)

    start = game.start
    audit["stage_id_start"] = start.get("stage")
    audit["is_frozen_ps"] = bool(start.get("is_frozen_ps"))
    audit["is_teams_start"] = bool(start.get("is_teams"))
    audit["num_frames_peppi"] = len(game.frames)
    start_players = start.get("players") or []
    audit["players_start"] = start_players
    if bool(start.get("is_teams")):
        audit["rejection_reason"] = "start_teams"
        return Candidate(row=row, audit=audit, cache_path=cache_path)
    if int(start.get("stage") or -1) != int(row.get("external_stage_id") or -1):
        audit["rejection_reason"] = "stage_metadata_start_mismatch"
        return Candidate(row=row, audit=audit, cache_path=cache_path)
    if int(start.get("stage") or -1) == 3 and bool(start.get("is_frozen_ps")) is not True:
        audit["rejection_reason"] = "pokemon_stadium_not_frozen"
        return Candidate(row=row, audit=audit, cache_path=cache_path)
    if len(start_players) != 2:
        audit["rejection_reason"] = "start_not_two_players"
        return Candidate(row=row, audit=audit, cache_path=cache_path)

    expected_by_port: dict[str, int] = {}
    start_chars: dict[str, str] = {}
    for player in start_players:
        port = str(player["port"])
        ext_id = int(player.get("character") or -1)
        if ext_id not in CHAR_BY_EXTERNAL_ID:
            audit["rejection_reason"] = "start_unsupported_character"
            return Candidate(row=row, audit=audit, cache_path=cache_path)
        if str(player.get("type")) != "Human":
            audit["rejection_reason"] = "non_human_player"
            return Candidate(row=row, audit=audit, cache_path=cache_path)
        ucf = player.get("ucf") or {}
        if ucf.get("dash_back") != "Ucf" or ucf.get("shield_drop") != "Ucf":
            audit["rejection_reason"] = "ucf_not_enabled"
            return Candidate(row=row, audit=audit, cache_path=cache_path)
        expected_by_port[port] = INTERNAL_BY_EXTERNAL_ID[ext_id]
        start_chars[str(_port_num(port))] = CHAR_BY_EXTERNAL_ID[ext_id]

    if "Sheik" not in start_chars.values():
        audit["rejection_reason"] = "start_no_sheik"
        return Candidate(row=row, audit=audit, cache_path=cache_path)

    char_changes = _scan_character_changes(game, expected_by_port)
    audit["unexpected_post_frame_characters"] = char_changes
    if char_changes:
        audit["rejection_reason"] = "character_changed_or_transformed"
        return Candidate(row=row, audit=audit, cache_path=cache_path)

    audit["accepted"] = True
    audit["rejection_reason"] = ""
    audit["ports"] = sorted(int(port) for port in start_chars)
    audit["characters"] = start_chars
    return Candidate(row=row, audit=audit, cache_path=cache_path)


def _candidate_sort_key(candidate: Candidate) -> tuple[str, int, str]:
    return (candidate.played_on, int(candidate.row.get("id") or 0), candidate.file_name)


def _pick_for_stage(
    candidates: list[Candidate],
    *,
    count: int,
    used_names: set[str],
    used_sha256: set[str],
    opponent_order: tuple[str, ...] = OPPONENT_ORDER,
) -> list[Candidate]:
    by_opponent: dict[str, list[Candidate]] = defaultdict(list)
    for candidate in candidates:
        if candidate.file_name not in used_names and str(candidate.audit["sha256"]) not in used_sha256:
            by_opponent[candidate.opponent].append(candidate)
    for values in by_opponent.values():
        values.sort(key=_candidate_sort_key, reverse=True)

    picked: list[Candidate] = []
    while len(picked) < count:
        progressed = False
        for opponent in opponent_order:
            bucket = by_opponent.get(opponent, [])
            while bucket and (
                bucket[0].file_name in used_names or str(bucket[0].audit["sha256"]) in used_sha256
            ):
                bucket.pop(0)
            if not bucket:
                continue
            candidate = bucket.pop(0)
            if candidate.file_name in used_names or str(candidate.audit["sha256"]) in used_sha256:
                continue
            picked.append(candidate)
            used_names.add(candidate.file_name)
            used_sha256.add(str(candidate.audit["sha256"]))
            progressed = True
            if len(picked) >= count:
                break
        if not progressed:
            break
    if len(picked) < count:
        raise RuntimeError(f"needed {count} candidates, got {len(picked)}")
    return picked


def _rotated_heldout_opponent_order(stage_index: int, count: int) -> tuple[str, ...]:
    if not HELDOUT_OPPONENT_ORDER:
        return OPPONENT_ORDER
    offset = (stage_index * count) % len(HELDOUT_OPPONENT_ORDER)
    primary = tuple(
        HELDOUT_OPPONENT_ORDER[(offset + i) % len(HELDOUT_OPPONENT_ORDER)]
        for i in range(len(HELDOUT_OPPONENT_ORDER))
    )
    extras = tuple(opponent for opponent in OPPONENT_ORDER if opponent not in primary)
    return primary + extras


def _split_candidates(
    candidates: list[Candidate], *, official_per_stage: int, heldout_per_stage: int
) -> tuple[list[Candidate], list[Candidate]]:
    by_stage: dict[int, list[Candidate]] = defaultdict(list)
    for candidate in candidates:
        by_stage[candidate.stage_id].append(candidate)
    used: set[str] = set()
    used_sha256: set[str] = set()
    official: list[Candidate] = []
    heldout: list[Candidate] = []
    for stage_index, stage_id in enumerate(STAGE_ORDER):
        stage_candidates = by_stage.get(stage_id, [])
        official.extend(
            _pick_for_stage(
                stage_candidates,
                count=official_per_stage,
                used_names=used,
                used_sha256=used_sha256,
            )
        )
        heldout.extend(
            _pick_for_stage(
                stage_candidates,
                count=heldout_per_stage,
                used_names=used,
                used_sha256=used_sha256,
                opponent_order=_rotated_heldout_opponent_order(stage_index, heldout_per_stage),
            )
        )
    return official, heldout


def _stage_audit_rows(rows: list[dict[str, Any]], *, limit: int) -> list[dict[str, Any]]:
    by_opponent: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        by_opponent[_metadata_opponent(row)].append(row)
    for values in by_opponent.values():
        values.sort(
            key=lambda r: (_played_date(r), int(r.get("id") or 0), str(r["file_name"])),
            reverse=True,
        )

    selected: list[dict[str, Any]] = []
    while len(selected) < limit:
        progressed = False
        for opponent in OPPONENT_ORDER:
            bucket = by_opponent.get(opponent, [])
            if not bucket:
                continue
            selected.append(bucket.pop(0))
            progressed = True
            if len(selected) >= limit:
                break
        if not progressed:
            break
    return selected


def _copy_compress(candidate: Candidate, out_dir: Path) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    raw = out_dir / candidate.file_name
    if raw.resolve() != candidate.cache_path.resolve():
        shutil.copy2(candidate.cache_path, raw)
    compressed = raw.with_suffix(".slpz")
    compress_path(raw, compressed, force=True)
    raw.unlink()
    return compressed


def _suite_entry(path: Path, candidate: Candidate, *, root: Path) -> dict[str, Any]:
    rel = path.relative_to(root).as_posix()
    chars = {str(k): v for k, v in candidate.audit["characters"].items()}
    return {
        "replay": rel,
        "ports": list(candidate.audit["ports"]),
        "stage_id": int(candidate.stage_id),
        "characters": chars,
    }


def _write_suite(
    *,
    path: Path,
    name: str,
    notes: str,
    candidates: list[Candidate],
    replay_dir: Path,
    root: Path,
) -> None:
    replays = [
        _suite_entry(_copy_compress(candidate, replay_dir), candidate, root=root)
        for candidate in candidates
    ]
    suite = {
        "name": name,
        "notes": notes,
        "ucf_enabled": True,
        "ucf_cardinals_1_0_enabled": True,
        "replays": replays,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(suite, indent=2, sort_keys=True) + "\n")


def _manifest_entry(candidate: Candidate, split: str) -> dict[str, Any]:
    out = dict(candidate.audit)
    out["split"] = split
    out["stage"] = SUPPORTED_STAGES[int(candidate.stage_id)]
    out["opponent"] = candidate.opponent
    return out


def _write_audit(
    *,
    path: Path,
    candidates: list[Candidate],
    official: list[Candidate],
    heldout: list[Candidate],
    prelim_rejected: list[dict[str, Any]],
    eligible_not_audited: list[dict[str, Any]],
) -> None:
    official_names = {c.file_name for c in official}
    heldout_names = {c.file_name for c in heldout}
    manifest = []
    for candidate in candidates:
        if not bool(candidate.audit["accepted"]):
            split = "rejected"
        elif candidate.file_name in official_names:
            split = "official_train_eval"
        elif candidate.file_name in heldout_names:
            split = "held_out"
        else:
            split = "accepted_unused"
        manifest.append(_manifest_entry(candidate, split))
    manifest.extend(eligible_not_audited)
    manifest.extend(prelim_rejected)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")


def _write_summary(path: Path, *, official: list[Candidate], heldout: list[Candidate]) -> None:
    def counts(candidates: list[Candidate]) -> dict[str, Any]:
        return {
            "count": len(candidates),
            "by_stage": dict(Counter(SUPPORTED_STAGES[c.stage_id] for c in candidates)),
            "by_opponent": dict(Counter(c.opponent for c in candidates)),
            "frames": sum(int(c.audit["num_frames_peppi"]) for c in candidates),
        }

    summary = {
        "official_train_eval": counts(official),
        "held_out": counts(heldout),
        "overlap_file_names": sorted({c.file_name for c in official} & {c.file_name for c in heldout}),
        "overlap_sha256": sorted({c.audit["sha256"] for c in official} & {c.audit["sha256"] for c in heldout}),
    }
    path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")


def _parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--metadata-cache", type=Path, default=Path("reports/triage/newchar_sheik/slippilab_replays_metadata.json"))
    ap.add_argument("--refresh-metadata", action="store_true")
    ap.add_argument("--cache-dir", type=Path, default=Path("reports/triage/newchar_sheik/replay_pool_cache"))
    ap.add_argument("--played-after", default="2025-01-01")
    ap.add_argument("--min-frames", type=int, default=2400)
    ap.add_argument("--max-frames", type=int, default=12000)
    ap.add_argument("--official-per-stage", type=int, default=3)
    ap.add_argument("--heldout-per-stage", type=int, default=2)
    ap.add_argument("--max-audit-per-stage", type=int, default=24)
    ap.add_argument("--official-suite", type=Path, default=Path("replays/suites/sheik.json"))
    ap.add_argument("--heldout-suite", type=Path, default=Path("replays/suites/sheik_heldout.json"))
    ap.add_argument("--official-replay-dir", type=Path, default=Path("replays/validation/sheik"))
    ap.add_argument("--heldout-replay-dir", type=Path, default=Path("replays/heldout/sheik"))
    ap.add_argument("--manifest", type=Path, default=Path("reports/triage/newchar_sheik/replay_manifest.json"))
    ap.add_argument("--summary", type=Path, default=Path("reports/triage/newchar_sheik/replay_selection_audit.json"))
    return ap.parse_args()


def main() -> None:
    args = _parse_args()
    root = repo_root()
    rows = _fetch_metadata(args.metadata_cache, refresh=bool(args.refresh_metadata))
    prelim_rejected: list[dict[str, Any]] = []
    eligible_by_stage: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        reason = _prelim_reject_reason(
            row,
            min_frames=int(args.min_frames),
            max_frames=int(args.max_frames),
            played_after=str(args.played_after),
        )
        if reason is not None:
            prelim_rejected.append(
                {
                    "file_name": row.get("file_name"),
                    "split": "rejected",
                    "rejection_reason": reason,
                    "slippilab_id": row.get("id"),
                    "played_on": row.get("played_on"),
                    "num_frames_metadata": row.get("num_frames"),
                    "stage_id_metadata": row.get("external_stage_id"),
                    "players_metadata": row.get("players"),
                }
            )
            continue
        eligible_by_stage[int(row["external_stage_id"])].append(row)

    audited_all: list[Candidate] = []
    accepted: list[Candidate] = []
    eligible_not_audited: list[dict[str, Any]] = []
    for stage_id in STAGE_ORDER:
        eligible_rows = list(eligible_by_stage.get(stage_id, ()))
        rows_for_stage = _stage_audit_rows(eligible_rows, limit=int(args.max_audit_per_stage))
        audited_names = {str(row["file_name"]) for row in rows_for_stage}
        for row in eligible_rows:
            if str(row["file_name"]) in audited_names:
                continue
            eligible_not_audited.append(
                {
                    "file_name": row.get("file_name"),
                    "split": "eligible_not_audited",
                    "rejection_reason": "not_audited_due_to_max_audit_per_stage",
                    "slippilab_id": row.get("id"),
                    "played_on": row.get("played_on"),
                    "num_frames_metadata": row.get("num_frames"),
                    "stage_id_metadata": row.get("external_stage_id"),
                    "players_metadata": row.get("players"),
                }
            )
        for row in rows_for_stage:
            candidate = _audit_candidate(row, args.cache_dir)
            if bool(candidate.audit["accepted"]):
                accepted.append(candidate)
            audited_all.append(candidate)

    official, heldout = _split_candidates(
        accepted,
        official_per_stage=int(args.official_per_stage),
        heldout_per_stage=int(args.heldout_per_stage),
    )
    _write_suite(
        path=root / args.official_suite,
        name="sheik",
        notes=(
            "Official Sheik training/eval suite from SlippiLab. Human singles, supported "
            "legal stages, UCF enabled, frozen Pokemon Stadium only, supported characters "
            "only, and no Zelda transform episodes under the Stage 0 policy."
        ),
        candidates=official,
        replay_dir=root / args.official_replay_dir,
        root=root,
    )
    _write_suite(
        path=root / args.heldout_suite,
        name="sheik_heldout",
        notes=(
            "Held-out Sheik telemetry suite from SlippiLab. Do not use rows from this suite "
            "as direct lock targets or implementation selectors."
        ),
        candidates=heldout,
        replay_dir=root / args.heldout_replay_dir,
        root=root,
    )
    _write_audit(
        path=root / args.manifest,
        candidates=audited_all,
        official=official,
        heldout=heldout,
        prelim_rejected=prelim_rejected,
        eligible_not_audited=eligible_not_audited,
    )
    _write_summary(path=root / args.summary, official=official, heldout=heldout)
    print(f"official: {len(official)} -> {args.official_suite}")
    print(f"heldout:  {len(heldout)} -> {args.heldout_suite}")
    print(f"manifest: {args.manifest}")
    print(f"audit:    {args.summary}")


if __name__ == "__main__":
    main()
