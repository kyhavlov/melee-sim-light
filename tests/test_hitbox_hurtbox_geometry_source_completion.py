from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _read(path: str) -> str:
    return (ROOT / path).read_text()


def _walk_events(obj):
    if isinstance(obj, dict):
        kind = obj.get("kind")
        if isinstance(kind, str):
            yield kind
        for value in obj.values():
            yield from _walk_events(value)
    elif isinstance(obj, list):
        for value in obj:
            yield from _walk_events(value)


def test_hitbox_hurtbox_geometry_inventory_is_closed_or_retained_policy():
    doc = _read("agent_docs/systems/hitbox_hurtbox_geometry.md")

    assert "ftAction_8007121C" in doc
    assert "ftAction_8007162C" in doc
    assert "MSLHITB1" in doc
    assert "MSLHURT1" in doc
    assert "MSLPART1" in doc
    assert "lb_8000B1CC" in doc
    assert "HitVictim*" in doc

    for line in doc.splitlines():
        if not line.startswith("|") or line.startswith("|---"):
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if len(cells) < 5 or cells[0] in {"Area", "Source/Data Owner"}:
            continue
        status = cells[1] if cells[0] in {"HitCapsule create/clear/restart lifecycle",
                                           "HitCapsule world geometry",
                                           "HurtCapsule geometry and BODY mask",
                                           "Dynamic pose/source-part provenance",
                                           "HitCapsule victim-pointer lifetime at seed/reseed",
                                           "Hitbox size and interaction mutation opcodes"} else cells[4]
        assert status in {"CLOSED", "RETAINED SOURCE-POLICY"}, line


def test_progress_marks_hitbox_hurtbox_geometry_closed():
    progress = _read("agent_docs/systems/PROGRESS.md")
    row = next(
        line for line in progress.splitlines() if line.startswith("| Hitbox / Hurtbox Geometry |")
    )
    assert "| CLOSED |" in row
    assert "INVENTORY NEEDED" not in row


def test_set_hitbox_damage_source_citation_is_not_scale_opcode():
    checked = [
        "tools/extraction/extract_fighter_hitboxes.py",
        "src/hitboxes.c",
        "agent_docs/DATA_CONTRACT.md",
        "agent_docs/systems/movescript_events.md",
        "agent_docs/systems/hitbox_hurtbox_geometry.md",
    ]
    for path in checked:
        text = _read(path)
        assert "set HitCapsule damage" not in text or "ftAction_8007162C" in text
        if path in {"agent_docs/DATA_CONTRACT.md", "agent_docs/systems/hitbox_hurtbox_geometry.md"}:
            assert "ftAction_8007162C" in text
            assert "damage mutation" in text
        assert "ftAction_8007169C`) mutates" not in text
        assert "ftAction_8007169C` set HitCapsule damage" not in text


def test_supported_scripts_do_not_emit_deferred_hitbox_geometry_mutations():
    deferred = {"set_hitbox_size", "set_hitbox_interaction"}
    for char in ("fox", "falco"):
        data = json.loads((ROOT / "data" / "moves" / f"{char}.json").read_text())
        found = sorted(deferred.intersection(_walk_events(data)))
        assert not found, (
            f"{char} now emits deferred hitbox geometry mutations {found}; "
            "promote them to distinct MSLHITB1 active-slot mutation records."
        )


def test_hitbox_geometry_contract_uses_root_facing_rotation_not_mirror_proxy():
    contract = _read("agent_docs/DATA_CONTRACT.md")
    hitboxes_c = _read("src/hitboxes.c")
    assert "center_local = (pose_mtx * (x,y,z)) * fighter_scale_y * co_attrs.model_scaling" in contract
    assert "radius does **not** get `co_attrs.model_scaling`" in contract
    assert "center_local = (pose_mtx * offset) * scale_y * co_attrs.model_scaling" in hitboxes_c
    assert "they do not multiply by co_attrs.model_scaling" in hitboxes_c
    assert "center_local = rotY90(center_local, facing_dir)" in contract
    assert "local.x *= facing_dir" not in contract
    assert "we do not apply a true facing" not in contract
