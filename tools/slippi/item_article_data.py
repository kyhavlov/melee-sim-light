from __future__ import annotations

import json
from functools import lru_cache
from pathlib import Path
from typing import Any

from tools.slippi.known_data_artifacts import (
    ITEM_ARTICLE_CHAR_DOMAIN_SLIPPI_EXTERNAL_ID,
    ITEM_ARTICLE_VALUE_F32,
    ITEM_ARTICLE_VALUE_U16,
    ITEM_ARTICLE_VALUE_U32,
    read_mslitar1,
)


# Bridge between simulator internal ids (Fox=1, Falco=22, ...) and the Slippi/CSS external
# `character` ids (Fox=2, Falco=20, ...), registry-derived so every
# supported character is covered (the old hardcoded {1: 2, 22: 20} silently dropped others).
from tools.extraction.char_registry import CHARS as _REGISTRY_CHARS

SIM_CHAR_TO_SLIPPI_EXTERNAL_ID = {
    info.internal_id: info.external_id for info in _REGISTRY_CHARS.values()
}
SLIPPI_EXTERNAL_ID_TO_SIM_CHAR = {v: k for k, v in SIM_CHAR_TO_SLIPPI_EXTERNAL_ID.items()}


@lru_cache(maxsize=None)
def _load_values(data_dir_str: str) -> dict[tuple[int, str], int | float]:
    data_dir = Path(data_dir_str)
    table = read_mslitar1(data_dir / "items" / "articles" / "fox_falco.bin")
    manifest_path = data_dir / "items" / "articles" / "manifest.json"
    manifest: dict[str, Any] = json.loads(manifest_path.read_text(encoding="utf-8"))
    field_by_id = {int(row["id"]): str(row["name"]) for row in manifest.get("fields", [])}
    out: dict[tuple[int, str], int | float] = {}
    for rec in table.records:
        if rec.char_domain != ITEM_ARTICLE_CHAR_DOMAIN_SLIPPI_EXTERNAL_ID:
            raise ValueError(f"unsupported MSLITAR1 character domain: {rec.char_domain}")
        sim_char_id = SLIPPI_EXTERNAL_ID_TO_SIM_CHAR.get(int(rec.char_id))
        if sim_char_id is None:
            continue
        field_name = field_by_id.get(int(rec.field_id))
        if field_name is None:
            raise ValueError(f"MSLITAR1 field id {rec.field_id} missing from {manifest_path}")
        if rec.value_type in (ITEM_ARTICLE_VALUE_U16, ITEM_ARTICLE_VALUE_U32):
            value: int | float = int(rec.u32_value)
        elif rec.value_type == ITEM_ARTICLE_VALUE_F32:
            value = float(rec.f32_value)
        else:
            raise ValueError(f"unsupported MSLITAR1 value type: {rec.value_type}")
        out[(sim_char_id, field_name)] = value
    return out


def item_article_values_by_sim_char(data_dir: Path | str, field_name: str) -> dict[int, int | float]:
    values = _load_values(str(Path(data_dir)))
    out = {char_id: value for (char_id, name), value in values.items() if name == field_name}
    if not out:
        raise KeyError(f"MSLITAR1 field not found: {field_name}")
    return out


def item_article_kind_set(data_dir: Path | str, field_name: str) -> tuple[int, ...]:
    values = item_article_values_by_sim_char(data_dir, field_name)
    return tuple(sorted({int(v) for v in values.values()}))
