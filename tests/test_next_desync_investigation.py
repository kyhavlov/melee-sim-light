from __future__ import annotations

import csv
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import SAMPLE_DTYPE, write_dataset
from tools.eval.disruptive_rollout_desyncs import ROW_COLUMNS, DisruptiveRow
from tools.eval.validation_profile import get_validation_profile
from tools.eval import next_desync_investigation as next_desync


def _write_tiny_dataset(path: Path) -> None:
    samples = np.zeros(5, dtype=SAMPLE_DTYPE)
    samples["seed_t"]["stage_id"] = 32
    samples["seed_t"]["match_damage_ratio"] = np.float32(1.0)
    samples["seed_t"]["num_players"] = 2
    samples["seed_t"]["char_id"][:, 0] = 1
    samples["seed_t"]["char_id"][:, 1] = 22
    samples["seed_t"]["attack_ratio"][:, :2] = np.float32(1.0)
    samples["seed_t"]["defense_ratio"][:, :2] = np.float32(1.0)
    samples["seed_t"]["fighter_scale_y"][:, :2] = np.float32(1.0)
    samples["seed_t"]["stocks"][:, :2] = 4
    samples["seed_t"]["frame_id"] = np.arange(5, dtype=np.int32)
    samples["seed_t"]["action_id"][:, :2] = 0x000E
    samples["seed_t"]["action_frame"][:, :2] = 1
    samples["seed_t"]["ground_id"][:, :2] = 1
    for name in (
        "frame_id",
        "stage_id",
        "num_players",
        "char_id",
        "team_id",
        "pos_x",
        "pos_y",
        "speed_air_x_self",
        "speed_ground_x_self",
        "speed_y_self",
        "speed_x_attack",
        "speed_y_attack",
        "facing",
        "on_ground",
        "action_id",
        "action_frame",
        "jumps_left",
        "stocks",
        "percent",
        "shield_hp",
        "hitlag",
        "hitstun",
        "hurtbox_state",
        "ground_id",
        "animation_index",
        "instance_hit_by",
        "instance_id",
        "last_attack_landed",
        "combo_count",
        "last_hit_by",
        "state_flags",
        "items",
    ):
        samples["ref_t1"][name] = samples["seed_t"][name]
    samples["ref_t1"]["action_id"][1, 0] = 0x0014
    samples["ref_t1"]["instance_id"][2, 0] = 77
    write_dataset(str(path), num_players=2, samples=samples)


def _row(dataset_path: Path, *, record: int, score: float, family: str, field: str, offset: int) -> dict[str, object]:
    ref_action = 20 if field == "action_id" else 14
    out = "14" if field == "action_id" else "0"
    ref = "20" if field == "action_id" else "77"
    return {
        "suite": "tiny_next_desync",
        "dataset": str(dataset_path),
        "record": record,
        "seed_frame": record,
        "horizon": 10,
        "ref_frame": record + offset,
        "player": 0,
        "family_id": family,
        "action_state": "seed=Wait|out=Wait|ref=WalkSlow|ground=1|hitlag=False|hitstun=False",
        "field_cluster": field.split("[", 1)[0],
        "first_mismatch_offset": offset,
        "first_mismatch_field": field,
        "first_mismatch_subindex": -1,
        "first_mismatch_player": 0,
        "first_out": out,
        "first_ref": ref,
        "score_total": score,
        "score_discrete": score,
        "score_float": 0.0,
        "score_item": 0.0,
        "seed_action_id": 14,
        "out_action_id": 14,
        "ref_action_id": ref_action,
        "seed_action_frame": 1,
        "out_action_frame": 1,
        "ref_action_frame": 2,
        "on_ground": 1,
        "hitlag": 0,
        "hitstun": 0,
        "cluster_key": f"tiny-{family}-{record}",
    }


def _write_rows(path: Path, dataset_path: Path, *, count: int = 1) -> None:
    row = {
        1: [_row(dataset_path, record=1, score=140.0, family="F00_test", field="action_id", offset=1)],
        2: [
            _row(dataset_path, record=1, score=140.0, family="F00_test", field="action_id", offset=1),
            _row(dataset_path, record=2, score=120.0, family="F01_other", field="instance_id", offset=2),
        ],
    }[count]
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, delimiter="\t", fieldnames=ROW_COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(row)


def test_next_desync_investigation_writes_packet_from_rows_in(tmp_path: Path) -> None:
    dataset_path = tmp_path / "tiny.msl"
    rows_path = tmp_path / "rows.tsv"
    suite_path = tmp_path / "suite.json"
    out_dir = tmp_path / "next_desync"
    _write_tiny_dataset(dataset_path)
    _write_rows(rows_path, dataset_path)
    suite_path.write_text(
        json.dumps(
            {
                "name": "tiny_next_desync",
                "ucf_enabled": True,
                "ucf_cardinals_1_0_enabled": True,
                "replays": [{"replay": "replays/tiny.slp", "ports": [1, 2], "stage_id": 32}],
            }
        ),
        encoding="utf-8",
    )

    subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.eval.next_desync_investigation",
            "--suite",
            str(suite_path),
            "--datasets-dir",
            str(tmp_path),
            "--rows-in",
            str(rows_path),
            "--out-dir",
            str(out_dir),
        ],
        check=True,
    )

    for name in ("summary.md", "ranked.tsv", "clusters.tsv", "top_packet.json", "top_packet.md"):
        assert (out_dir / name).exists(), name
    packet = json.loads((out_dir / "top_packet.json").read_text(encoding="utf-8"))
    assert packet["tool"] == "tools.eval.next_desync_investigation"
    assert packet["record"] == 1
    assert packet["simulation_config"] == {
        "ucf_enabled": True,
        "ucf_cardinals_1_0_enabled": True,
    }
    assert packet["candidate"]["first_mismatch_field"] == "action_id"
    assert "one_step" in packet
    assert "active_profile_diff" in packet["one_step"]
    assert packet["rollout_first_mismatch"]["candidate_field"] == "action_id"
    assert "data_backed_context" in packet
    assert "candidate_owner_hints" in packet


def test_next_desync_investigation_top_packets_writes_numbered_packets(tmp_path: Path) -> None:
    dataset_path = tmp_path / "tiny.msl"
    rows_path = tmp_path / "rows.tsv"
    suite_path = tmp_path / "suite.json"
    out_dir = tmp_path / "next_desync"
    _write_tiny_dataset(dataset_path)
    _write_rows(rows_path, dataset_path, count=2)
    suite_path.write_text(
        json.dumps(
            {
                "name": "tiny_next_desync",
                "replays": [{"replay": "replays/tiny.slp", "ports": [1, 2], "stage_id": 32}],
            }
        ),
        encoding="utf-8",
    )

    subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.eval.next_desync_investigation",
            "--suite",
            str(suite_path),
            "--datasets-dir",
            str(tmp_path),
            "--rows-in",
            str(rows_path),
            "--out-dir",
            str(out_dir),
            "--top-packets",
            "2",
        ],
        check=True,
    )

    assert (out_dir / "packet_001.json").exists()
    assert (out_dir / "packet_002.json").exists()
    first = json.loads((out_dir / "top_packet.json").read_text(encoding="utf-8"))
    numbered = json.loads((out_dir / "packet_001.json").read_text(encoding="utf-8"))
    assert first["candidate"]["family_id"] == numbered["candidate"]["family_id"]


def test_rollout_first_mismatch_falls_back_to_disruptive_context_when_one_step_is_empty(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    dataset_path = tmp_path / "tiny.msl"
    rows_path = tmp_path / "rows.tsv"
    _write_tiny_dataset(dataset_path)
    _write_rows(rows_path, dataset_path)
    dataset = next_desync.read_dataset(str(dataset_path)).samples
    row = DisruptiveRow(**_row(dataset_path, record=1, score=140.0, family="F00_test", field="instance_id", offset=2))

    def fake_one_step(samples, record, num_players, *, ucf_enabled, ucf_cardinals_1_0_enabled):
        assert ucf_enabled is True
        assert ucf_cardinals_1_0_enabled is False
        return samples[record]["ref_t1"], None

    def fake_rollout(samples, record, offset, num_players, *, ucf_enabled, ucf_cardinals_1_0_enabled):
        assert ucf_enabled is True
        assert ucf_cardinals_1_0_enabled is False
        return samples[record + offset - 1]["ref_t1"], None

    monkeypatch.setattr(next_desync, "_simulate_one_step", fake_one_step)
    monkeypatch.setattr(
        next_desync,
        "_simulate_rollout_to_offset",
        fake_rollout,
    )

    packet = next_desync._build_packet(
        root=Path.cwd(),
        row=row,
        rows_path=rows_path,
        profile=get_validation_profile("rl1_gameplay"),
        action_names={14: "Wait"},
        suite_ucf_enabled=True,
        suite_ucf_cardinals_1_0_enabled=False,
    )

    assert packet["one_step"]["first_differing_scored_field"] == {"status": "none"}
    assert packet["rollout_first_mismatch"]["candidate_field"] == "instance_id"
    assert packet["rollout_first_mismatch"]["first_differing_scored_field"]["field"] == "instance_id"
    assert any("disruptive rollout first diff is instance_id" in hint["evidence"] for hint in packet["candidate_owner_hints"])


def test_mismatched_disruptive_summary_is_not_current_enough(tmp_path: Path) -> None:
    dataset_path = tmp_path / "tiny.msl"
    rows_path = tmp_path / "rows.tsv"
    suite_path = tmp_path / "suite.json"
    _write_tiny_dataset(dataset_path)
    _write_rows(rows_path, dataset_path)
    suite_path.write_text("{}", encoding="utf-8")
    (tmp_path / "summary.json").write_text(
        json.dumps(
            {
                "suite": "tiny_next_desync",
                "suite_path": str(suite_path),
                "datasets_dir": str(tmp_path),
                "profile": "strict",
                "horizons": [10, 20, 60],
                "stride": 1,
                "max_records": 0,
            }
        ),
        encoding="utf-8",
    )

    assert not next_desync._current_enough(
        rows_path,
        suite_path,
        [dataset_path],
        suite_name="tiny_next_desync",
        datasets_dir=str(tmp_path),
        profile="rl1_gameplay",
        horizons="10,20,60",
        max_records=0,
        stride=1,
    )


def test_disruptive_summary_reuse_normalizes_horizons_and_datasets_dir(tmp_path: Path) -> None:
    rows_path = tmp_path / "rows.tsv"
    suite_path = tmp_path / "suite.json"
    rows_path.write_text("", encoding="utf-8")
    suite_path.write_text("{}", encoding="utf-8")
    (tmp_path / "summary.json").write_text(
        json.dumps(
            {
                "suite": "tiny_next_desync",
                "suite_path": str(suite_path),
                "datasets_dir": str(next_desync.repo_root() / "datasets"),
                "profile": "rl1_gameplay",
                "horizons": [60, 10, 20, 10],
                "stride": 1,
                "max_records": 0,
            }
        ),
        encoding="utf-8",
    )

    assert next_desync._summary_matches_request(
        rows_path,
        suite_path=suite_path,
        suite_name="tiny_next_desync",
        datasets_dir="datasets",
        profile="rl1_gameplay",
        horizons="20,10,60,20",
        max_records=0,
        stride=1,
    )
