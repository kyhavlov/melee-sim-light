from __future__ import annotations

import json
import math
import os
import struct
import tempfile
from pathlib import Path

import numpy as np

from tests.test_hitbox_scale_flags import _populate_data_dir, _write_minimal_mslftsc1
from tools.eval.validation_dtypes import INPUT_DTYPE, SEED_DTYPE
from tools.extraction.extract_fighter_hitboxes import _records_from_events
from tools.extraction.extract_fighter_script_timeline import EVENT_IDS, _encode_payload
from tools.extraction.known_data_artifacts import _decode_script_payload


def _create_hitbox_event(frame: int, hitbox_id: int, damage: float) -> dict:
    return {
        "frame": frame,
        "kind": "create_hitbox",
        "data": {
            "hitbox": {
                "hitbox_id": hitbox_id,
                "bone": 4,
                "hit_group": 1,
                "element": 0,
                "sfx_kind": 1,
                "sfx_severity": 1,
                "shield_damage": 0,
                "rehit_frames": 0,
                "angle": 45,
                "kbg": 100,
                "wsk": 0,
                "bkb": 30,
                "damage": damage,
                "size": 3.0,
                "x_offset": 0.0,
                "y_offset": 2.0,
                "z_offset": 0.0,
            },
        },
    }


def _write_repeated_ssanim_v5(
    path: Path, *, msid: int, part_id: int, frame_count: int, mtx34: list[float]
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    buf = bytearray()
    buf += b"SSANIM01"
    buf += struct.pack("<IHH", 5, 1, 1)
    buf += struct.pack("<B", int(part_id) & 0xFF)
    buf += struct.pack("<HH", int(msid) & 0xFFFF, int(frame_count) & 0xFFFF)
    for _ in range(frame_count):
        buf += struct.pack("<12f", *[float(x) for x in mtx34])
    for _ in range(frame_count):
        buf += struct.pack("<3f", 0.0, 0.0, 0.0)
    path.write_bytes(bytes(buf))


_DEBUG_HITBOX_EVENT_TIMING_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("hb_id", "u1"),
        ("char_id", "u1"),
        ("_pad0", "u1"),
        ("msid", "<u2"),
        ("pose_frame", "<u2"),
        ("anim_frame_f32", "<f4"),
        ("frame_speed_mul_f32", "<f4"),
        ("start_frame", "<i2"),
        ("end_frame", "<i2"),
        ("enabled_prev", "u1"),
        ("enabled_cur", "u1"),
        ("prev_hit_group", "u1"),
        ("cur_hit_group", "u1"),
        ("pose_create_count", "u1"),
        ("pose_clear_count", "u1"),
        ("pose_clear_all_count", "u1"),
        ("enable_edge", "u1"),
        ("last_affect_kind_le", "u1"),
        ("last_affect_kind_eq", "u1"),
        ("last_affect_frame_le", "<u2"),
        ("last_affect_frame_eq", "<u2"),
        ("last_affect_u16_7_le", "<u2"),
        ("last_affect_u16_7_eq", "<u2"),
    ],
    align=False,
)


def test_mslftsc1_encodes_set_hitbox_damage_payload() -> None:
    payload = _encode_payload("set_hitbox_damage", {"idx": 2, "damage": 7.0})

    assert EVENT_IDS["set_hitbox_damage"] == 2
    assert len(payload) == 8
    decoded = _decode_script_payload(EVENT_IDS["set_hitbox_damage"], payload)
    assert decoded["idx"] == 2
    assert decoded["damage"] == 7.0


def test_mslftsc1_encodes_set_hitbox_interaction_payload() -> None:
    payload = _encode_payload("set_hitbox_interaction", {"idx": 2, "type": 0, "value": 0})

    assert EVENT_IDS["set_hitbox_interaction"] == 4
    assert len(payload) == 4
    assert _decode_script_payload(EVENT_IDS["set_hitbox_interaction"], payload) == {
        "idx": 2,
        "type": 0,
        "value": 0,
    }


def test_fox_throwhi_preserves_asynchronous_timer_ownership() -> None:
    root = Path(__file__).resolve().parents[1]
    moves = json.loads((root / "data" / "moves" / "fox.json").read_text())
    events = moves["moves"]["ftCo_SM_ThrowHi"]["events"]

    timers = [event for event in events if event["kind"] == "command_timer"]

    # PlFx.dat ThrowHi uses Command_02 at every one of these boundaries. In particular, target 20
    # replaces (rather than adds to) the f32 residual left after target 18; that distinction moves
    # the second laser pulse by one interpreter tick at a 4/3 animation rate.
    # refs/melee/src/melee/lb/lbcommand.c::Command_02
    assert [(event["timer_kind"], event["timer_value"]) for event in timers[:5]] == [
        (2, frame) for frame in (8, 13, 18, 20, 24)
    ]


def test_angled_side_attacks_extract_their_concrete_source_scripts() -> None:
    root = Path(__file__).resolve().parents[1]
    fox = json.loads((root / "data" / "moves" / "fox.json").read_text())["moves"]
    falcon = json.loads((root / "data" / "moves" / "falcon.json").read_text())["moves"]

    side_tilt_names = (
        "ftCo_SM_AttackS3Hi",
        "ftCo_SM_AttackS3HiS",
        "ftCo_SM_AttackS3",
        "ftCo_SM_AttackS3LwS",
        "ftCo_SM_AttackS3Lw",
    )
    assert [fox[name]["submotion_id"] for name in side_tilt_names] == list(range(53, 58))
    assert all(fox[name]["events"] == fox["ftCo_SM_AttackS3"]["events"] for name in side_tilt_names)

    # Captain authors distinct up/neutral/down scripts for this family. A runtime alias to the
    # neutral submotion would therefore be incorrect even though Fox happens to share its stream.
    # refs/melee/src/melee/ft/ftmotionstates.c (AttackS3* rows)
    assert falcon["ftCo_SM_AttackS3Hi"]["events"] != falcon["ftCo_SM_AttackS3"]["events"]
    assert falcon["ftCo_SM_AttackS3Lw"]["events"] != falcon["ftCo_SM_AttackS3"]["events"]


def test_sheik_vanish_preserves_timer_only_command_groups() -> None:
    root = Path(__file__).resolve().parents[1]
    moves = json.loads((root / "data" / "moves" / "sheik.json").read_text())
    events = moves["specials_by_msid"]["312"]["events"]
    cmd_index = next(i for i, event in enumerate(events) if event["kind"] == "set_cmd_var")

    # This script advances through ten synchronous one-frame control groups before publishing
    # cmd_vars[0]. The entry interpreter has already subtracted one rate, so the first wait expires
    # immediately and the command remains an audit-frame-9 event. Dropping timer-only groups made
    # the persistent interpreter fire it at entry.
    assert [(event["timer_kind"], event["timer_value"]) for event in events[:cmd_index]] == [
        (1, 1)
    ] * 10
    assert events[cmd_index] == {
        "frame": 9,
        "kind": "set_cmd_var",
        "data": {"idx": 0, "value": 1},
    }


def test_mslhitb1_folds_set_hitbox_damage_into_active_slot_record() -> None:
    records = _records_from_events(
        [
            _create_hitbox_event(frame=3, hitbox_id=0, damage=4.0),
            {"frame": 6, "kind": "set_hitbox_damage", "data": {"idx": 0, "damage": 9.0}},
            {"frame": 7, "kind": "remove_hitbox", "data": {"idx": 0}},
            {"frame": 8, "kind": "set_hitbox_damage", "data": {"idx": 0, "damage": 2.0}},
        ]
    )

    assert [(r.frame, r.kind, r.hitbox_id, r.damage) for r in records] == [
        (3, 0, 0, 4.0),
        (6, 2, 0, 9.0),
        (7, 1, 0, 0.0),
    ]
    assert math.isclose(records[1].radius, records[0].radius)
    assert records[1].u16_tail == records[0].u16_tail


def test_mslhitb1_folds_set_hitbox_interaction_into_active_slot_record() -> None:
    records = _records_from_events(
        [
            _create_hitbox_event(frame=3, hitbox_id=2, damage=4.0),
            {
                "frame": 6,
                "kind": "set_hitbox_interaction",
                "data": {"idx": 2, "type": 0, "value": 0},
            },
            {
                "frame": 7,
                "kind": "set_hitbox_interaction",
                "data": {"idx": 2, "type": 1, "value": 0},
            },
        ]
    )

    assert [(r.frame, r.kind, r.hitbox_id) for r in records] == [(3, 0, 2), (6, 3, 2), (7, 3, 2)]
    create_flags = records[0].u16_tail[6]
    fighter_off_flags = records[1].u16_tail[6]
    both_off_flags = records[2].u16_tail[6]
    assert create_flags & 0b111 == 0b111
    assert fighter_off_flags & 0b111 == 0b110
    assert both_off_flags & 0b111 == 0b100
    assert records[1].damage == records[0].damage
    assert records[1].radius == records[0].radius


def test_runtime_set_hitbox_damage_does_not_create_edge() -> None:
    import msl_binding

    exclude = {
        Path("anims/fox.bin"),
        Path("anims/falco.bin"),
        Path("scripts/fox.bin"),
        Path("scripts/falco.bin"),
    }

    build_dir = Path("build")
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=build_dir) as tmp_s:
        tmp = Path(tmp_s)
        data_dir = tmp / "data"
        _populate_data_dir(data_dir, exclude=exclude)

        ident = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]
        _write_repeated_ssanim_v5(data_dir / "anims/fox.bin", msid=0, part_id=0, frame_count=8, mtx34=ident)
        _write_repeated_ssanim_v5(data_dir / "anims/falco.bin", msid=0, part_id=0, frame_count=8, mtx34=ident)

        create = _create_hitbox_event(frame=3, hitbox_id=0, damage=4.0)
        create["data"]["hitbox"]["bone"] = 0
        events = [
            create,
            {"frame": 6, "kind": "set_hitbox_damage", "data": {"idx": 0, "damage": 9.0}},
            {
                "frame": 7,
                "kind": "set_hitbox_interaction",
                "data": {"idx": 0, "type": 0, "value": 0},
            },
        ]
        _write_minimal_mslftsc1(data_dir / "scripts/fox.bin", msid=0, events=events)
        _write_minimal_mslftsc1(data_dir / "scripts/falco.bin", msid=0, events=events)

        old_data_dir = os.environ.get("MSL_DATA_DIR")
        try:
            os.environ["MSL_DATA_DIR"] = str(data_dir)
            msl_binding.debug_reset_pose_and_hitboxes_tables()

            sizes = msl_binding.sizes()
            seed_stride = int(sizes["seed"])
            input_stride = int(sizes["input"])
            assert input_stride == INPUT_DTYPE.itemsize

            seed = np.zeros((1,), dtype=SEED_DTYPE)
            seed["stage_id"][0] = np.uint32(32)
            seed["num_players"][0] = np.uint8(2)
            seed["stocks"][0, :2] = np.uint8(4)
            seed["char_id"][0, :2] = np.uint8(1)
            seed["action_id"][0, :2] = np.uint16(0xFFFF)
            seed["pos_x"][0, :2] = np.float32(100.0)
            seed["pos_y"][0, :2] = np.float32(-50.0)
            seed["fighter_scale_y"][0, :2] = np.float32(1.0)
            seed["facing"][0, :2] = np.uint8(1)
            seed["action_frame"][0, :2] = np.int16(6)
            seed["anim_frame_f32"][0, :2] = np.float32(6.0)
            seed["animation_index"][0, :2] = np.uint32(0)
            seed["hitlag"][0, :2] = np.uint16(2)

            prev_inp = np.zeros((1, input_stride), dtype=np.uint8)
            inp = np.zeros((1, input_stride), dtype=np.uint8)

            handle = msl_binding.init(batch_size=1, num_players=2)
            try:
                msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
                msl_binding.step_input(handle, prev_inp, inp)

                hitboxes, count = msl_binding.hitboxes_world(handle, 0, 0)
                seed["action_frame"][0, :2] = np.int16(7)
                seed["anim_frame_f32"][0, :2] = np.float32(7.0)
                msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
                msl_binding.step_input(handle, prev_inp, inp)
                interaction_hitboxes, interaction_count = msl_binding.hitboxes_world_full(
                    handle, 0, 0
                )
            finally:
                msl_binding.destroy(handle)
        finally:
            if old_data_dir is None:
                os.environ.pop("MSL_DATA_DIR", None)
            else:
                os.environ["MSL_DATA_DIR"] = old_data_dir
            msl_binding.debug_reset_pose_and_hitboxes_tables()

    assert int(count) == 1
    assert hitboxes[0, 4] == np.float32(9.0)
    assert int(interaction_count) == 1
    assert int(interaction_hitboxes[0, 13]) & 0b111 == 0b110


def test_runtime_async_timer_replaces_residual_at_nonunit_rate() -> None:
    import msl_binding

    exclude = {
        Path("anims/fox.bin"),
        Path("anims/falco.bin"),
        Path("scripts/fox.bin"),
        Path("scripts/falco.bin"),
    }
    msid = 249  # ftCo_SM_ThrowHi
    rate = np.float32(4.0 / 3.0)

    build_dir = Path("build")
    build_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=build_dir) as tmp_s:
        data_dir = Path(tmp_s) / "data"
        _populate_data_dir(data_dir, exclude=exclude)

        ident = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]
        for char in ("fox", "falco"):
            _write_repeated_ssanim_v5(
                data_dir / "anims" / f"{char}.bin",
                msid=msid,
                part_id=0,
                frame_count=48,
                mtx34=ident,
            )

        create = _create_hitbox_event(frame=18, hitbox_id=0, damage=4.0)
        create["timer_kind"] = 2
        create["timer_value"] = 18
        create["data"]["hitbox"]["bone"] = 0
        clear = {
            "frame": 20,
            "kind": "clear_hitboxes",
            "timer_kind": 2,
            "timer_value": 20,
            "data": {},
        }
        for char in ("fox", "falco"):
            _write_minimal_mslftsc1(
                data_dir / "scripts" / f"{char}.bin", msid=msid, events=[create, clear]
            )

        old_data_dir = os.environ.get("MSL_DATA_DIR")
        try:
            os.environ["MSL_DATA_DIR"] = str(data_dir)
            msl_binding.debug_reset_pose_and_hitboxes_tables()

            sizes = msl_binding.sizes()
            seed_stride = int(sizes["seed"])
            input_stride = int(sizes["input"])
            seed = np.zeros((1,), dtype=SEED_DTYPE)
            seed["stage_id"][0] = np.uint32(32)
            seed["num_players"][0] = np.uint8(2)
            seed["stocks"][0, :2] = np.uint8(4)
            seed["char_id"][0, :2] = np.uint8(1)
            seed["action_id"][0, 0] = np.uint16(0x00DD)  # ftCo_MS_ThrowHi
            seed["action_id"][0, 1] = np.uint16(0x000E)  # ftCo_MS_Wait
            seed["animation_index"][0, 0] = np.uint32(msid)
            seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)
            seed["action_frame"][0, 0] = np.int16(19)
            seed["anim_frame_f32"][0, 0] = np.float32(14.0) * rate
            seed["frame_speed_mul_f32"][0, 0] = rate
            seed["frame_speed_mul_f32"][0, 1] = np.float32(1.0)
            seed["fighter_scale_y"][0, :2] = np.float32(1.0)
            seed["facing"][0, :2] = np.uint8(1)
            seed["on_ground"][0, :2] = np.uint8(1)
            seed["pos_x"][0, :] = np.float32([100.0, -100.0, 0.0, 0.0])

            inp = np.zeros((1, input_stride), dtype=np.uint8)
            handle = msl_binding.init(batch_size=1, num_players=2)
            try:
                msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
                _, count_at_18 = msl_binding.hitboxes_world(handle, 0, 0)
                msl_binding.step_input(handle, inp, inp)
                _, count_at_20 = msl_binding.hitboxes_world(handle, 0, 0)
                msl_binding.step_input(handle, inp, inp)
                _, count_after_20 = msl_binding.hitboxes_world(handle, 0, 0)
            finally:
                msl_binding.destroy(handle)
        finally:
            if old_data_dir is None:
                os.environ.pop("MSL_DATA_DIR", None)
            else:
                os.environ["MSL_DATA_DIR"] = old_data_dir
            msl_binding.debug_reset_pose_and_hitboxes_tables()

    assert int(count_at_18) == 1
    assert int(count_at_20) == 1
    assert int(count_after_20) == 0


def test_generated_script_manifests_have_no_runtime_owner_unsupported_events() -> None:
    root = Path(__file__).resolve().parents[1]
    allowed_deferred = {"set_hitbox_size", "remove_hitbox"}
    for char in ("fox", "falco", "marth", "falcon", "sheik", "zelda"):
        manifest = json.loads((root / "data" / "scripts" / f"{char}_manifest.json").read_text())
        unsupported = set((manifest.get("unsupported_event_counts") or {}).keys())
        assert "set_hitbox_damage" not in unsupported
        assert "set_hitbox_interaction" not in unsupported
        assert unsupported <= allowed_deferred


def test_movescript_events_system_doc_is_closed_without_todo_rows() -> None:
    root = Path(__file__).resolve().parents[1]
    doc = (root / "agent_docs" / "systems" / "movescript_events.md").read_text()
    progress = (root / "agent_docs" / "systems" / "PROGRESS.md").read_text()

    assert "| Movescript Events | CLOSED |" in progress
    assert "MSLFTSC1 extraction and binary schema" in doc
    assert "Hitbox create/clear/mutation lifecycle" in doc
    assert "Throw script flags, hitboxes, and projectile pulses" in doc
    assert "| TODO" not in doc
    assert "INVENTORY NEEDED" not in doc
