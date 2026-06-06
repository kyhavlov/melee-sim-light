from __future__ import annotations

import re
from pathlib import Path


def _js_export_int(source: str, name: str) -> int:
    match = re.search(rf"export const {re.escape(name)} = (\d+);", source)
    assert match is not None, f"missing JS export: {name}"
    return int(match.group(1))


def _js_object_int(source: str, object_name: str, field: str) -> int:
    match = re.search(rf"export const {re.escape(object_name)} = \{{(.*?)\}};", source, re.S)
    assert match is not None, f"missing JS object: {object_name}"
    body = match.group(1)
    field_match = re.search(rf"\b{re.escape(field)}:\s*(\d+)", body)
    assert field_match is not None, f"missing JS field: {object_name}.{field}"
    return int(field_match.group(1))


def test_live_viewer_stage_state_schema_matches_native_binding() -> None:
    # Webplay passes STAGE_STATE_SIZE as the C out_stride for
    # msl_batch_debug_write_stage_state. If this gets stale, native returns ENOSPC (28) when the
    # match starts.
    root = Path(__file__).resolve().parents[1]
    schema = (root / "tools/viewer/live/schema.js").read_text()

    msl_binding = __import__("msl_binding")
    assert _js_export_int(schema, "STAGE_STATE_SIZE") == int(msl_binding.sizes()["stage_state"])

    assert _js_object_int(schema, "stageStateOffsets", "randallExists") == 28
    assert _js_object_int(schema, "stageStateOffsets", "randallX") == 30
    assert _js_object_int(schema, "stageStateOffsets", "randallY") == 34
