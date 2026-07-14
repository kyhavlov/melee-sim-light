from __future__ import annotations

import json
import subprocess
from pathlib import Path

import pytest

from tools.extraction.build_data import _git_provenance, _write_data_manifest
from tools.slippi.validation_buffer_common import _checkout_extraction_tree, manifest_registry_chars

_REPO_ROOT = Path(__file__).resolve().parents[1]


def _git_rev(spec: str) -> str | None:
    try:
        proc = subprocess.run(
            ["git", "-C", str(_REPO_ROOT), "rev-parse", spec],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    return proc.stdout.strip() or None


def _write_manifest(path: Path, **overrides) -> None:
    payload = {
        "magic": "MSLDATA1",
        "version": 1,
        "schemas": {},
        "chars": ["fox"],
        "stages": [],
        "git_revision": "0" * 40,
        "extraction_tree": "0" * 40,
    }
    payload.update(overrides)
    (path / "manifest.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")


def test_git_provenance_matches_checkout() -> None:
    head = _git_rev("HEAD")
    if head is None:
        pytest.skip("git unavailable")
    provenance = _git_provenance()
    assert provenance["git_revision"] == head
    assert provenance["extraction_tree"] == _git_rev("HEAD:tools/extraction")


def test_write_data_manifest_records_provenance(tmp_path: Path) -> None:
    _write_data_manifest(tmp_path, chars=["fox"], stages=["grnba"])
    payload = json.loads((tmp_path / "manifest.json").read_text())
    assert "git_revision" in payload
    assert "extraction_tree" in payload
    head = _git_rev("HEAD")
    if head is not None:
        assert payload["git_revision"] == head
        assert payload["extraction_tree"] == _git_rev("HEAD:tools/extraction")


def test_manifest_registry_chars_warns_on_stale_extraction_tree(tmp_path: Path) -> None:
    if _checkout_extraction_tree() is None:
        pytest.skip("git unavailable")
    _write_manifest(tmp_path, git_revision="feedface" * 5)
    with pytest.warns(RuntimeWarning, match=r"feedface.*may be stale.*build_data"):
        chars = manifest_registry_chars(tmp_path)
    assert [name for _, name in chars] == ["fox"]


def test_manifest_registry_chars_quiet_when_extraction_tree_current(tmp_path: Path) -> None:
    current = _checkout_extraction_tree()
    if current is None:
        pytest.skip("git unavailable")
    _write_manifest(tmp_path, extraction_tree=current)
    import warnings as _warnings

    with _warnings.catch_warnings():
        _warnings.simplefilter("error", RuntimeWarning)
        chars = manifest_registry_chars(tmp_path)
    assert [name for _, name in chars] == ["fox"]


def test_manifest_registry_chars_unknown_char_error_suggests_regen(tmp_path: Path) -> None:
    _write_manifest(tmp_path, chars=["fox", "nonexistent_char"], git_revision="cafef00d" * 5)
    with pytest.raises(ValueError, match=r"nonexistent_char.*cafef00d.*build_data"):
        manifest_registry_chars(tmp_path)


def test_manifest_registry_chars_tolerates_missing_provenance(tmp_path: Path) -> None:
    payload = {"magic": "MSLDATA1", "version": 1, "schemas": {}, "chars": ["fox"], "stages": []}
    (tmp_path / "manifest.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    chars = manifest_registry_chars(tmp_path)
    assert [name for _, name in chars] == ["fox"]
