from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


_DEBUG_CONTACT_CLASSIFIED_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("contact_kind", "u1"),  # 0=BODY, 1=SHIELD
        ("hurtcap_id", "u1"),
        ("_pad0", "u1", (3,)),
        ("attacker_msid", "<u2"),
        ("attacker_action_frame", "<i2"),
        ("hitbox_x", "<f4"),
        ("hitbox_y", "<f4"),
        ("hitbox_z", "<f4"),
        ("hitbox_radius", "<f4"),
        ("hitbox_damage", "<f4"),
        ("hurtcap_ax", "<f4"),
        ("hurtcap_ay", "<f4"),
        ("hurtcap_az", "<f4"),
        ("hurtcap_bx", "<f4"),
        ("hurtcap_by", "<f4"),
        ("hurtcap_bz", "<f4"),
        ("hurtcap_radius", "<f4"),
        ("shield_x", "<f4"),
        ("shield_y", "<f4"),
        ("shield_z", "<f4"),
        ("shield_radius", "<f4"),
    ],
    align=False,
)


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


def _shield_contact_without_body_conflict(contacts: np.ndarray, defender: int) -> np.void | None:
    for c in contacts:
        if int(c["defender"]) != int(defender):
            continue
        if int(c["contact_kind"]) != 1:
            continue
        hb_id = int(c["hitbox_id"])
        attacker = int(c["attacker"])
        body_conflict = False
        for b in contacts:
            if int(b["defender"]) != int(defender):
                continue
            if int(b["contact_kind"]) != 0:
                continue
            if int(b["attacker"]) != attacker or int(b["hitbox_id"]) != hb_id:
                continue
            body_conflict = True
            break
        if not body_conflict:
            return c
    return None


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            784,
            0,
            0x00B3,  # Guard hold snapshot that previously drifted to GuardOff.
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            115,
            1,
            0x00B6,  # GuardReflect powershield snapshot that previously dropped to GuardSetOff.
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            2275,
            0,
            0x00B6,  # GuardReflect no-submotion lane.
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            5167,
            0,
            0x0019,  # Hitlist stale-latch row that previously synthesized hitlag/hitstun.
        ),
    ],
) 
def test_seed_eq_guardrail_rows_stay_replay_exact(
    dataset_rel: str,
    record: int,
    p: int,
    seed_action: int,
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    assert int(row["seed_t"]["action_id"][0, p]) == int(seed_action)
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    ref = row["ref_t1"][0]
    for field in ("action_id", "hitlag", "hitstun", "instance_id", "on_ground", "ground_id"):
        got = int(out[field][0, p])
        exp = int(ref[field][p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
    assert out["state_flags"][0, p].tolist() == ref["state_flags"][p].tolist(), (
        f"record={record} p={p} field=state_flags expected={ref['state_flags'][p].tolist()} "
        f"got={out['state_flags'][0, p].tolist()}"
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            2485,
            1,
        ),
        # TODO(combat-ownership, decomp-backed): keep this row context-only until we land a
        # runtime ownership fix for remaining 179/181/179.
        # Current discriminator (replay-real pre-combat):
        # - SHIELD contact candidate exists,
        # - no same-hitbox BODY conflict on the chosen candidate,
        # - hitlist containment blocks acceptance (victim present in victims_1).
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            2219,
            0,
        ),
    ],
)
def test_guardsetoff_cluster_rows_have_replay_real_shield_hit_context(
    dataset_rel: str,
    record: int,
    p: int,
) -> None:
    # Representative lock rows for remaining action_id cluster 179/181/179.
    #
    # These locks intentionally verify replay-real GuardSetOff context (reference transition into
    # shieldstun + shield contact candidate) before any C-side transition fix is implemented.
    #
    # Decomp anchors for expected ownership:
    # - Shield hit resolution path: refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    # - GuardSetOff entry bundle: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"
    row = samples[record : record + 1]

    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == 0x00B3  # Guard
    assert int(ref["action_id"][p]) == 0x00B5  # GuardSetOff
    assert int(seed["hitlag"][p]) == 0
    assert int(seed["hitstun"][p]) == 0
    assert int(ref["hitlag"][p]) > 0
    assert int(ref["hitstun"][p]) == 0
    assert float(ref["shield_hp"][p]) < float(seed["shield_hp"][p])
    assert int(seed["state_flags"][p, 1]) == 0x01
    assert int(ref["state_flags"][p, 1]) == 0x21

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(
        row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, input_stride)
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        # Classify shield/body contacts on the pre-combat snapshot for this replay row.
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        raw, count = binding.debug_combat_contacts_classified(handle, 0, 64)
    finally:
        binding.destroy(handle)

    assert int(raw.shape[1]) == int(_DEBUG_CONTACT_CLASSIFIED_DTYPE.itemsize)
    contacts = raw.reshape(-1).view(_DEBUG_CONTACT_CLASSIFIED_DTYPE)[:count]
    shield_contacts_on_p = [c for c in contacts if int(c["defender"]) == int(p) and int(c["contact_kind"]) == 1]
    assert shield_contacts_on_p, (
        f"record={record} p={p} expected at least one SHIELD contact candidate before combat; "
        f"got count={int(count)}"
    )
    chosen = _shield_contact_without_body_conflict(contacts, p)
    assert chosen is not None, (
        f"record={record} p={p} expected SHIELD contact context without same-hitbox BODY conflict; "
        f"got contacts={int(count)}"
    )
    assert float(chosen["hitbox_damage"]) > 0.0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            2485,
            1,
        ),
    ],
)
def test_guardsetoff_cluster_rows_resolve_to_reference_outputs(
    dataset_rel: str,
    record: int,
    p: int,
) -> None:
    # Cluster lock: action_id (seed/ref/out)=179/181/179 must resolve to GuardSetOff.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"
    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == 0x00B3
    assert int(ref["action_id"][p]) == 0x00B5
    assert int(seed["hitlag"][p]) == 0
    assert int(ref["hitlag"][p]) > 0
    assert float(ref["shield_hp"][p]) < float(seed["shield_hp"][p])

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        attacker = 1 - p
        timing_raw = binding.debug_hitbox_event_timing(handle, 0, attacker, 0)
        timing = timing_raw.reshape(-1).view(_DEBUG_HITBOX_EVENT_TIMING_DTYPE)[0]
        assert int(timing["enabled_cur"]) == 1
        assert int(timing["last_affect_u16_7_le"]) & 0xFF == 0  # rehit_frames=0 lane

        # Decomp-corroborated stale-latch context:
        # - active window age is shorter than shield-hit hitlag (so a real prior hit would still
        #   leave defender in hitlag),
        # - yet seed hitlag/hitstun are neutral.
        # refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
        # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
        window_age = int(timing["pose_frame"]) - int(timing["start_frame"])
        assert window_age >= 0
        assert window_age < int(ref["hitlag"][p])

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    assert int(out["action_id"][0, p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][0, p]) > 0
    assert int(out["hitstun"][0, p]) == int(ref["hitstun"][p])
    assert out["state_flags"][0, p].tolist() == ref["state_flags"][p].tolist()
    assert int(out["instance_id"][0, p]) == int(ref["instance_id"][p])
    assert int(out["on_ground"][0, p]) == int(ref["on_ground"][p])
    assert int(out["ground_id"][0, p]) == int(ref["ground_id"][p])
