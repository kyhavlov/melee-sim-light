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
    ("dataset_rel", "record", "p", "expect_hitlist_contains"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/QuerulousGrandDinosaur.msl",
            2485,
            1,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            2219,
            0,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            9848,
            1,
            1,
        ),
    ],
)
def test_guardsetoff_cluster_rows_have_replay_real_shield_hit_context(
    dataset_rel: str,
    record: int,
    p: int,
    expect_hitlist_contains: int,
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
    # Re-run once to query hitlist containment while handle is alive.
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        hitlist_contains = int(
            binding.debug_hitlist_fighter_contains(
                handle, 0, int(chosen["attacker"]), int(chosen["hitbox_id"]), int(p)
            )
        )
    finally:
        binding.destroy(handle)
    # TODO(combat-ownership, decomp-backed): TBK 2219:0 and GAT 9848:1 still carry
    # hitlist_contains=1 (victims_1 entry with cd==0) at pre-combat while replay enters
    # GuardSetOff; expected governing ownership is the ftColl_800768A0 clear/copy lane before
    # lbColl_8000ACFC acceptance on ftColl_80078C70.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
    assert hitlist_contains == int(expect_hitlist_contains), (
        f"record={record} p={p} expected hitlist_contains={int(expect_hitlist_contains)} "
        f"got={hitlist_contains}"
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "attacker"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            5614,
            0,
            1,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            8297,
            1,
            0,
        ),
    ],
)
def test_guardsetoff_cluster_rows_keep_no_fighter_shield_contact_discriminator(
    dataset_rel: str,
    record: int,
    p: int,
    attacker: int,
) -> None:
    # TODO(combat-ownership, decomp-backed): keep these rows context-only until we can
    # decomp-explain why replay enters GuardSetOff despite no fighter SHIELD candidate in the
    # current sim pre-combat snapshot.
    #
    # Discriminator family:
    # - replay reference transitions Guard -> GuardSetOff with shield HP drop and hitlag>0,
    # - no active item lanes in seed/ref snapshots,
    # - pre-combat classified SHIELD contacts for defender p are 0 while attacker hitboxes are active.
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
    assert int(np.count_nonzero(seed["items"]["exists"])) == 0
    assert int(np.count_nonzero(ref["items"]["exists"])) == 0

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
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        raw, count = binding.debug_combat_contacts_classified(handle, 0, 64)

        active_hitboxes = 0
        for hb_id in range(4):
            timing_raw = binding.debug_hitbox_event_timing(handle, 0, attacker, hb_id)
            timing = timing_raw.reshape(-1).view(_DEBUG_HITBOX_EVENT_TIMING_DTYPE)[0]
            if int(timing["enabled_cur"]) != 0:
                active_hitboxes += 1
    finally:
        binding.destroy(handle)

    assert active_hitboxes > 0, f"record={record} p={p} attacker={attacker} expected active hitboxes"
    assert int(raw.shape[1]) == int(_DEBUG_CONTACT_CLASSIFIED_DTYPE.itemsize)
    contacts = raw.reshape(-1).view(_DEBUG_CONTACT_CLASSIFIED_DTYPE)[:count]
    shield_contacts_on_p = [c for c in contacts if int(c["defender"]) == int(p) and int(c["contact_kind"]) == 1]
    assert not shield_contacts_on_p, (
        f"record={record} p={p} expected no fighter SHIELD contacts in pre-combat classifier; "
        f"got count={len(shield_contacts_on_p)} of total={int(count)}"
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


@pytest.mark.integration
def test_guardsetoff_tbk_2219_signature_is_reseed_only_in_short_rollout_window() -> None:
    # Classification lock for the remaining TBK hard row:
    # - one-step teacher-forced reseed at rec=2219,p=0 shows action_id seed/ref/out=179/181/179,
    # - short continuous rollout crossing the same row resolves GuardSetOff (action out==181),
    #   so this signature is treated as reseed/materialization-specific, not a runtime patch target.
    #
    # Seed-provenance follow-up plan (decomp-backed ownership target):
    # - Missing seed lanes to materialize hitlist ownership:
    #   1) per-hitbox victims_1 lineage (not only dense per-group cd/iid),
    #   2) enable-edge clear/copy provenance for ftColl_800768A0 paths,
    #   3) per-victim cooldown provenance (x4 age/source) for lbColl_8000ACFC acceptance.
    # - Replay-visible derivation candidates:
    #   action_id/action_frame/animation_index + hitbox_event_timing enable-edge + shield_hp/hitlag
    #   transition context + instance_id transitions.
    # - Requires Dolphin dump schema extension for full proof:
    #   export per-hitbox HitCapsule victims_1/victims_2 + ring indices (+ x40_b4 where possible).
    #
    # Decomp anchors:
    # - refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70,ftColl_80076CBC}
    # - refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    record = 2219
    p = 0
    window_before = 24
    window_after = 24

    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    # One-step reseed-at-row signature (current hard-row discriminator).
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    one_step_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[record, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[record, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
        binding.reseed_seed(one_step_handle, seed_bytes)
        binding.step_input(one_step_handle, prev_input_bytes, input_bytes)
        binding.write_compare(one_step_handle, out_compare_bytes)
    finally:
        binding.destroy(one_step_handle)

    seed_row = samples["seed_t"][record]
    ref_row = samples["ref_t1"][record]
    assert int(seed_row["action_id"][p]) == 0x00B3
    assert int(ref_row["action_id"][p]) == 0x00B5
    assert int(out["action_id"][0, p]) == 0x00B3
    assert int(seed_row["hitlag"][p]) == 0
    assert int(ref_row["hitlag"][p]) > 0
    assert int(out["hitlag"][0, p]) == 0
    assert int(seed_row["state_flags"][p, 1]) == 0x01
    assert int(ref_row["state_flags"][p, 1]) == 0x21
    assert int(out["state_flags"][0, p, 1]) == 0x01

    # Continuous rollout crossing the same row: no per-frame reseed.
    start = max(0, record - window_before)
    end = min(int(samples.shape[0]) - 1, record + window_after)
    rollout_rows: list[dict[str, int]] = []

    rollout_handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[start, seed_off : seed_off + seed_stride]
        binding.reseed_seed(rollout_handle, seed_bytes)
        for j in range(start, end + 1):
            prev_input_bytes[0, :] = samples_u8[j, prev_input_off : prev_input_off + input_stride]
            input_bytes[0, :] = samples_u8[j, input_off : input_off + input_stride]
            binding.step_input(rollout_handle, prev_input_bytes, input_bytes)
            binding.write_compare(rollout_handle, out_compare_bytes)

            seed_j = samples["seed_t"][j]
            ref_j = samples["ref_t1"][j]
            rollout_rows.append(
                {
                    "record": int(j),
                    "seed_action": int(seed_j["action_id"][p]),
                    "ref_action": int(ref_j["action_id"][p]),
                    "out_action": int(out["action_id"][0, p]),
                    "seed_hitlag": int(seed_j["hitlag"][p]),
                    "ref_hitlag": int(ref_j["hitlag"][p]),
                    "out_hitlag": int(out["hitlag"][0, p]),
                    "seed_sf1": int(seed_j["state_flags"][p, 1]),
                    "ref_sf1": int(ref_j["state_flags"][p, 1]),
                    "out_sf1": int(out["state_flags"][0, p, 1]),
                }
            )
    finally:
        binding.destroy(rollout_handle)

    target = next(r for r in rollout_rows if int(r["record"]) == int(record))
    assert int(target["out_action"]) == int(target["ref_action"]) == 0x00B5
    assert int(target["out_sf1"]) == int(target["ref_sf1"]) == 0x21

    # The hard one-step signature must not appear in this short no-reseed rollout window.
    signature_rows = [
        r
        for r in rollout_rows
        if (
            int(r["seed_action"]) == 0x00B3
            and int(r["ref_action"]) == 0x00B5
            and int(r["out_action"]) == 0x00B3
            and int(r["seed_hitlag"]) == 0
            and int(r["ref_hitlag"]) > 0
            and int(r["out_hitlag"]) == 0
            and int(r["seed_sf1"]) == 0x01
            and int(r["ref_sf1"]) == 0x21
            and int(r["out_sf1"]) == 0x01
        )
    ]
    assert not signature_rows, (
        "TBK rec=2219 one-step hard signature should be absent in short no-reseed rollout window; "
        f"got records={[int(r['record']) for r in signature_rows]}"
    )
