from __future__ import annotations

import json
from pathlib import Path

import pytest

from tools.eval.run_forensic_rows import _parse_row_spec
from tools.eval.run_guardrail_preflight import _read_float_keyset


def test_parse_row_spec_supports_paths_with_colons() -> None:
    spec = _parse_row_spec("datasets/foo:bar.msl:123:1")
    assert spec.dataset == "datasets/foo:bar.msl"
    assert spec.record == 123
    assert spec.p == 1


def test_parse_row_spec_rejects_bad_shape() -> None:
    with pytest.raises(ValueError):
        _parse_row_spec("datasets/foo.msl:12")


def test_read_float_keyset_parses_dataset_record_p(tmp_path: Path) -> None:
    payload = {
        "keys": {
            "pos_x": [
                {"dataset": "datasets/a.msl", "record": 10, "p": 0},
                {"dataset": "datasets/b.msl", "record": 20, "p": 1},
            ],
            "pos_y": [
                {"dataset": "datasets/a.msl", "record": 11, "p": 0},
            ],
        }
    }
    path = tmp_path / "float_keys.json"
    path.write_text(json.dumps(payload), encoding="utf-8")

    got = _read_float_keyset(path)
    assert got["pos_x"] == {("datasets/a.msl", 10, 0), ("datasets/b.msl", 20, 1)}
    assert got["pos_y"] == {("datasets/a.msl", 11, 0)}
