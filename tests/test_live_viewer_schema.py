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


def _js_const_ints(source: str) -> dict[str, int]:
    return {
        name: int(value)
        for name, value in re.findall(r"export const ([A-Z0-9_]+) = (\d+);", source)
    }


def _live_supported_characters(schema: str) -> list[tuple[int, str]]:
    consts = _js_const_ints(schema)
    match = re.search(
        r"export const SUPPORTED_CHARACTERS = Object\.freeze\(\[(.*?)\]\);",
        schema,
        re.S,
    )
    assert match is not None, "missing SUPPORTED_CHARACTERS"
    characters: list[tuple[int, str]] = []
    for id_token, label in re.findall(
        r'id:\s*([A-Z0-9_]+|\d+),\s*label:\s*"([^"]+)"',
        match.group(1),
    ):
        char_id = int(id_token) if id_token.isdigit() else consts[id_token]
        characters.append((char_id, label))
    return characters


def _viewer_external_char_map(adapter_source: str) -> dict[int, int]:
    return {
        int(internal_id): int(external_id)
        for internal_id, external_id in re.findall(
            r"if \(internalCharId === (\d+)\) return (\d+);", adapter_source
        )
    }


def _viewer_zip_by_external_id(animation_cache: str) -> list[str]:
    match = re.search(
        r"const characterZipUrlByExternalId = \[(.*?)\];",
        animation_cache,
        re.S,
    )
    assert match is not None, "missing characterZipUrlByExternalId"
    return re.findall(r'"([^"]+\.zip)"', match.group(1))


def _asset_manifest_filenames(manifest: str) -> set[str]:
    filenames = []
    for line in manifest.splitlines():
        if not line or line.startswith("#"):
            continue
        filenames.append(line.split("\t", 1)[0])
    assert len(filenames) == len(set(filenames)), "duplicate viewer asset manifest filenames"
    return set(filenames)


def _viewer_wasm_required_chars(build_wasm: str) -> set[str]:
    match = re.search(r"viewer_chars=\(([^)]*)\)", build_wasm)
    assert match is not None, "missing build_wasm.sh viewer_chars"
    return set(match.group(1).split())


def _data_key_from_character_label(label: str) -> str:
    return label.lower().replace(" ", "_")


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


def test_live_viewer_hitbox_display_schema_matches_native_debug_rows() -> None:
    # Webplay renders Sheik Chain from msl_batch_debug_hitboxes_world rows:
    # [x, y, z, radius, damage, u16_0, u16_1, u16_3, bone_part_id, enabled].
    root = Path(__file__).resolve().parents[1]
    schema = (root / "tools/viewer/live/schema.js").read_text()
    build_wasm = (root / "tools/viewer/live/build_wasm.sh").read_text()

    assert _js_export_int(schema, "MAX_PLAYERS") == 4
    assert _js_export_int(schema, "MAX_HITBOXES") == 4
    assert _js_export_int(schema, "HITBOX_SIZE") == 40
    assert _js_export_int(schema, "HITBOX_PLAYER_SIZE") == 160
    assert _js_export_int(schema, "HITBOXES_SIZE") == 640
    assert _js_object_int(schema, "hitboxOffsets", "x") == 0
    assert _js_object_int(schema, "hitboxOffsets", "radius") == 12
    assert _js_object_int(schema, "hitboxOffsets", "damage") == 16
    assert _js_object_int(schema, "hitboxOffsets", "bonePartId") == 32
    assert _js_object_int(schema, "hitboxOffsets", "enabled") == 36
    assert "_msl_batch_debug_hitboxes_world" in build_wasm


def test_live_viewer_supported_characters_have_packaged_animation_zips() -> None:
    root = Path(__file__).resolve().parents[1]
    schema = (root / "tools/viewer/live/schema.js").read_text()
    viewer_adapter = (root / "tools/viewer/live/viewer_adapter.js").read_text()
    animation_cache = (root / "tools/viewer/slippi-viewer/src/viewer/animationCache.ts").read_text()
    manifest = (root / "tools/viewer/assets/character_zips.tsv").read_text()

    external_char_map = _viewer_external_char_map(viewer_adapter)
    zip_by_external_id = _viewer_zip_by_external_id(animation_cache)
    packaged_filenames = _asset_manifest_filenames(manifest)

    missing: list[str] = []
    for internal_id, label in _live_supported_characters(schema):
        external_id = external_char_map.get(internal_id, internal_id)
        assert external_id < len(zip_by_external_id), (
            f"{label} maps to unknown external id {external_id}"
        )
        zip_filename = Path(zip_by_external_id[external_id]).name
        if zip_filename not in packaged_filenames:
            missing.append(f"{label}: {zip_filename}")

    assert not missing, (
        "live viewer character dropdown has unpackaged animation zips: " + ", ".join(missing)
    )


def test_live_viewer_transform_characters_map_to_animation_assets() -> None:
    root = Path(__file__).resolve().parents[1]
    schema = (root / "tools/viewer/live/schema.js").read_text()
    viewer_adapter = (root / "tools/viewer/live/viewer_adapter.js").read_text()
    trace_adapter = (root / "tools/viewer/msltrace1.js").read_text()

    supported = dict(_live_supported_characters(schema))
    assert supported[7] == "Sheik"
    assert supported[19] == "Zelda"

    # Live Compare traces expose simulator internal ids. Slippi animation zips are keyed by
    # external ids, so Sheik/Zelda transform spans need both sides of the public id mapping.
    for adapter_source in (viewer_adapter, trace_adapter):
        external = _viewer_external_char_map(adapter_source)
        assert external[7] == 19
        assert external[19] == 18


def test_live_viewer_supported_characters_are_required_by_wasm_build() -> None:
    root = Path(__file__).resolve().parents[1]
    schema = (root / "tools/viewer/live/schema.js").read_text()
    build_wasm = (root / "tools/viewer/live/build_wasm.sh").read_text()

    required_chars = _viewer_wasm_required_chars(build_wasm)
    missing = [
        _data_key_from_character_label(label)
        for _internal_id, label in _live_supported_characters(schema)
        if _data_key_from_character_label(label) not in required_chars
    ]

    assert not missing, (
        "live viewer dropdown chars missing from WASM data preflight: " + ", ".join(missing)
    )
