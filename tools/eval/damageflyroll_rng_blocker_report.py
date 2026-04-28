from __future__ import annotations

import argparse
import csv
import importlib
import json
import os
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class Case:
    dataset_rel: str
    record: int
    p: int
    role: str
    note: str


@dataclass(frozen=True)
class RngRowObservation:
    dataset: str
    record: int
    p: int
    role: str
    note: str
    seed_action_id: int
    ref_action_id: int
    out_action_id: int
    attacker_seed_action_id: int
    attacker_seed_action_frame: int
    seed_last_hit_by: int
    seed_frame_pre_random_seed: int
    site1_count: int
    site1_roll: float | None
    site1_roll_window: tuple[float, ...]
    phase_advance_to_lt_threshold: int | None
    modeled_pre_gate_site_counts: tuple[int, ...]
    requires_unmodeled_pre_gate_consumer: bool
    compatible_fighter_8006cda4_total_consumes: tuple[int, ...]
    fighter_8006cda4_compatible_families: tuple[str, ...]
    roll_threshold: float

    @property
    def matches_ref(self) -> bool:
        return int(self.out_action_id) == int(self.ref_action_id)

    @property
    def roll_bucket(self) -> str:
        if self.site1_roll is None:
            return "no_pulse"
        return "lt_threshold" if float(self.site1_roll) < float(self.roll_threshold) else "ge_threshold"


DEFAULT_CASES: tuple[Case, ...] = (
    # Curated replay-real closure/control rows for the DamageFlyRoll gate site:
    # - severe airborne damage entry evaluates the HSD_Randf gate in ftCo_8008DCE0 block_33,
    # - the site-1 trace in this runtime corresponds to that gate.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randf
    Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl",
        record=2694,
        p=0,
        role="resolved_control",
        note="AttackAirB subset resolved by seeded Fighter_8006CDA4 carry",
    ),
    Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl",
        record=5717,
        p=0,
        role="resolved_control",
        note="ThrownF hitlag carry resolved by explicit Fighter_8006CDA4 consume count",
    ),
    Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl",
        record=6929,
        p=1,
        role="resolved_control",
        note="DamageFlyTop carry resolved by explicit Fighter_8006CDA4 consume count",
    ),
    Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl",
        record=6020,
        p=0,
        role="positive_control",
        note="same site-1 pulse resolves to DamageFlyRoll when roll is below threshold",
    ),
    Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl",
        record=6019,
        p=0,
        role="negative_control",
        note="adjacent pre-target control stays no-pulse and replay exact",
    ),
    Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.msl",
        record=6021,
        p=0,
        role="negative_control",
        note="adjacent post-target control stays no-pulse and replay exact",
    ),
    Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl",
        record=5716,
        p=0,
        role="negative_control",
        note="adjacent pre-target rollout control stays no-pulse and replay exact",
    ),
    Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.msl",
        record=5718,
        p=0,
        role="negative_control",
        note="adjacent post-target rollout control stays no-pulse and replay exact",
    ),
    Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl",
        record=6928,
        p=1,
        role="negative_control",
        note="adjacent pre-target carry control stays no-pulse and replay exact",
    ),
    Case(
        dataset_rel="datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl",
        record=6930,
        p=1,
        role="negative_control",
        note="adjacent post-target carry control stays no-pulse and replay exact",
    ),
    Case(
        dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
        record=7185,
        p=0,
        role="resolved_control",
        note="SpecialHiFall <- AttackAirB enable-edge admits DamageFlyRoll without extra pre-gate consumes",
    ),
    Case(
        dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl",
        record=7019,
        p=0,
        role="negative_control",
        note="SpecialHiFall <- steady AttackAirB contact does not admit the DamageFlyRoll gate",
    ),
)


def _load_binding():
    return importlib.import_module("msl_binding")


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _resolve_dataset_path(dataset_rel: str, datasets_dir: Path) -> Path:
    rel = Path(dataset_rel)
    parts = rel.parts
    base = datasets_dir if datasets_dir.is_absolute() else (_repo_root() / datasets_dir)
    if parts and parts[0] == "datasets":
        return base / Path(*parts[1:])
    return _repo_root() / rel


def _site1_roll_from_seed_in(seed_in: int) -> float:
    # HSD_Randf consumes one LCG step and returns upper16/65536.0f.
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randf
    seed = (int(seed_in) * 214013 + 2531011) & 0xFFFFFFFF
    return float((seed >> 16) & 0xFFFF) * (1.0 / 65536.0)


def _randf_roll_window_from_seed_in(seed_in: int, count: int) -> tuple[float, ...]:
    seed = int(seed_in)
    out: list[float] = []
    for _ in range(max(0, int(count))):
        seed = (seed * 214013 + 2531011) & 0xFFFFFFFF
        out.append(float((seed >> 16) & 0xFFFF) * (1.0 / 65536.0))
    return tuple(out)


def _phase_advance_to_lt_threshold(roll_window: tuple[float, ...], threshold: float) -> int | None:
    for phase, roll in enumerate(roll_window):
        if float(roll) < float(threshold):
            return int(phase)
    return None


def _compatible_fighter_8006cda4_total_consumes(phase_advance: int | None) -> tuple[int, ...]:
    # Decomp candidate before ftCo_8008DCE0 block_33:
    # - Fighter_8006CDA4 can consume up to three HSD_Randi calls before the DamageFlyRoll gate:
    #   * held-item branch always consumes one x418 sample when entered,
    #   * held-item subtype branch can consume a second x41C sample,
    #   * x197C branch consumes one x418 sample when present.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
    if phase_advance is None or phase_advance <= 0:
        return tuple()
    return (int(phase_advance),)


def _fighter_8006cda4_compatible_families(phase_advance: int | None) -> tuple[str, ...]:
    # Fighter_8006CDA4 pre-gate RNG families:
    # - `hold_item_bool` branch always consumes one x418 sample when entered.
    # - The held-item subtype clause can consume a second x41C sample.
    # - The x197C branch consumes one x418 sample when present.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
    if phase_advance is None or phase_advance <= 0:
        return tuple()
    if phase_advance == 1:
        return (
            "x197c_x418",
            "held_item_primary_x418",
        )
    if phase_advance == 2:
        return (
            "held_item_primary_x418_plus_x197c_x418",
            "held_item_type3_x418_plus_x41c",
        )
    if phase_advance == 3:
        return ("held_item_type3_x418_plus_x41c_plus_x197c_x418",)
    return tuple()


def _fighter_8006cda4_family_details() -> dict[str, dict[str, object]]:
    # Minimal consume-critical seed fields for the decomp-owned branch families in Fighter_8006CDA4.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008E984
    return {
        "x197c_x418": {
            "consume_count": 1,
            "consume_critical_fields": (
                "fighter.x197C_presence",
                "fighter.x2226_b2",
            ),
            "side_effect_only_fields": tuple(),
            "note": "x197C auxiliary item branch consumes one x418 sample when present.",
        },
        "held_item_primary_x418": {
            "consume_count": 1,
            "consume_critical_fields": (
                "fighter.item_gobj_presence",
                "fighter.item_hold_is_nonheavy",
                "fighter.x2220_b3",
                "fighter.x2220_b4",
                "fighter.ftCo_8008E984_guard",
                "fighter.x2226_b2",
            ),
            "side_effect_only_fields": ("fighter.x1978_presence",),
            "note": "held-item branch entered; first x418 sample decides the drop path.",
        },
        "held_item_primary_x418_plus_x197c_x418": {
            "consume_count": 2,
            "consume_critical_fields": (
                "fighter.item_gobj_presence",
                "fighter.item_hold_is_nonheavy",
                "fighter.x197C_presence",
                "fighter.x2220_b3",
                "fighter.x2220_b4",
                "fighter.ftCo_8008E984_guard",
                "fighter.x2226_b2",
            ),
            "side_effect_only_fields": ("fighter.x1978_presence",),
            "note": "held-item primary x418 consume plus the separate x197C x418 consume.",
        },
        "held_item_type3_x418_plus_x41c": {
            "consume_count": 2,
            "consume_critical_fields": (
                "fighter.item_gobj_presence",
                "fighter.item_hold_is_nonheavy",
                "fighter.item_hold_subtype3",
                "fighter.item_hold_projectile_empty",
                "fighter.x2220_b3",
                "fighter.x2220_b4",
                "fighter.ftCo_8008E984_guard",
                "fighter.x2226_b2",
            ),
            "side_effect_only_fields": ("fighter.x1978_presence",),
            "note": "held-item branch consumes x418, then the subtype-3 projectile-empty clause consumes x41C.",
        },
        "held_item_type3_x418_plus_x41c_plus_x197c_x418": {
            "consume_count": 3,
            "consume_critical_fields": (
                "fighter.item_gobj_presence",
                "fighter.item_hold_is_nonheavy",
                "fighter.item_hold_subtype3",
                "fighter.item_hold_projectile_empty",
                "fighter.x197C_presence",
                "fighter.x2220_b3",
                "fighter.x2220_b4",
                "fighter.ftCo_8008E984_guard",
                "fighter.x2226_b2",
            ),
            "side_effect_only_fields": ("fighter.x1978_presence",),
            "note": "full held-item type-3 path plus the independent x197C consume.",
        },
    }


def _fighter_8006cda4_phase_family_details(phase_advance: int | None) -> list[dict[str, object]]:
    families = _fighter_8006cda4_compatible_families(phase_advance)
    details = _fighter_8006cda4_family_details()
    return [
        {
            "family": family,
            **details[family],
        }
        for family in families
    ]


def observe_case(case: Case, *, datasets_dir: Path = Path("datasets")) -> RngRowObservation:
    binding = _load_binding()
    ds_path = _resolve_dataset_path(case.dataset_rel, datasets_dir)
    ds = read_dataset(str(ds_path))
    row = ds.samples[case.record : case.record + 1]
    if int(row.shape[0]) != 1:
        raise ValueError(f"dataset too short for case: {case}")

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = (
        np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    trace_dir = _repo_root() / "reports" / "triage"
    trace_dir.mkdir(parents=True, exist_ok=True)
    trace_path = trace_dir / f"damageflyroll_rng_blocker_{ds_path.stem}_{case.record}_{case.p}.tsv"

    prev_trace_env = os.environ.get("MSL_RNG_TRACE_PATH")
    prev_gate_env = os.environ.get("MSL_RNG_ENABLE_DAMAGE_FLY_ROLL_GATE")
    os.environ["MSL_RNG_TRACE_PATH"] = str(trace_path)
    os.environ.pop("MSL_RNG_ENABLE_DAMAGE_FLY_ROLL_GATE", None)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
        if prev_trace_env is None:
            os.environ.pop("MSL_RNG_TRACE_PATH", None)
        else:
            os.environ["MSL_RNG_TRACE_PATH"] = prev_trace_env
        if prev_gate_env is None:
            os.environ.pop("MSL_RNG_ENABLE_DAMAGE_FLY_ROLL_GATE", None)
        else:
            os.environ["MSL_RNG_ENABLE_DAMAGE_FLY_ROLL_GATE"] = prev_gate_env

    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]

    victim = int(case.p)
    attacker = int(seed["last_hit_by"][victim])
    if attacker not in (0, 1):
        attacker_action = -1
        attacker_action_frame = -1
    else:
        attacker_action = int(seed["action_id"][attacker])
        attacker_action_frame = int(np.int16(seed["action_frame"][attacker]))

    site_counts = {site_id: 0 for site_id in range(1, 9)}
    site1_seed_in: int | None = None
    if trace_path.exists():
        with trace_path.open("r", encoding="utf-8") as fh:
            reader = csv.DictReader(fh, delimiter="\t")
            for row_trace in reader:
                site_id = int(row_trace["site_id"])
                if site_id not in site_counts:
                    continue
                site_counts[site_id] += int(row_trace["call_count"])
                if site_id == 1 and site1_seed_in is None:
                    site1_seed_in = int(row_trace["seed_in"])

    common = json.loads((_repo_root() / "data/common/ft_common_data.json").read_text())
    site1_count = int(site_counts[1])
    site1_roll = _site1_roll_from_seed_in(site1_seed_in) if site1_count > 0 and site1_seed_in is not None else None
    roll_threshold = float(common["damagefly_roll_prob"])
    site1_roll_window = (
        _randf_roll_window_from_seed_in(site1_seed_in, 4)
        if site1_count > 0 and site1_seed_in is not None
        else tuple()
    )
    phase_advance_to_lt_threshold = (
        _phase_advance_to_lt_threshold(site1_roll_window, roll_threshold)
        if site1_count > 0 and site1_seed_in is not None
        else None
    )
    # Current runtime-owned pre-gate RNG sites before the DamageFlyRoll gate:
    # - site 2: electric clank SFX lane
    #   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007646C
    #   refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
    # - site 3: Wait idle anim variant lane
    #   refs/melee/src/melee/ft/ftwaitanim.c::{ftCo_8008A7A8,getAnimID}
    #   refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
    # - site 4: action-script pseudo-random SFX command lane
    #   refs/melee/src/melee/ft/ftaction.c::ftAction_80071FC8
    #   refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
    # - site 5: Fighter_8006CDA4 primary pre-gate consume
    # - site 6: Fighter_8006CDA4 secondary pre-gate consume
    # - site 7: Fighter_8006CDA4 tertiary pre-gate consume
    # - site 8: JumpAerialF/B <- AttackAirB admission carry
    #   refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    #   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    modeled_pre_gate_site_counts = tuple(int(site_counts[site_id]) for site_id in (2, 3, 4, 5, 6, 7, 8))
    return RngRowObservation(
        dataset=ds_path.name,
        record=int(case.record),
        p=victim,
        role=str(case.role),
        note=str(case.note),
        seed_action_id=int(seed["action_id"][victim]),
        ref_action_id=int(ref["action_id"][victim]),
        out_action_id=int(out["action_id"][victim]),
        attacker_seed_action_id=int(attacker_action),
        attacker_seed_action_frame=int(attacker_action_frame),
        seed_last_hit_by=int(seed["last_hit_by"][victim]),
        seed_frame_pre_random_seed=int(seed["frame_pre_random_seed"]),
        site1_count=int(site1_count),
        site1_roll=site1_roll,
        site1_roll_window=site1_roll_window,
        phase_advance_to_lt_threshold=phase_advance_to_lt_threshold,
        modeled_pre_gate_site_counts=modeled_pre_gate_site_counts,
        requires_unmodeled_pre_gate_consumer=(
            site1_count > 0
            and phase_advance_to_lt_threshold is not None
            and phase_advance_to_lt_threshold > 0
            and sum(modeled_pre_gate_site_counts) == 0
        ),
        compatible_fighter_8006cda4_total_consumes=
        _compatible_fighter_8006cda4_total_consumes(phase_advance_to_lt_threshold),
        fighter_8006cda4_compatible_families=
        _fighter_8006cda4_compatible_families(phase_advance_to_lt_threshold),
        roll_threshold=roll_threshold,
    )


def build_summary(observations: list[RngRowObservation]) -> dict:
    blockers = [
        obs
        for obs in observations
        if obs.role == "blocker"
    ]
    resolved_controls = [obs for obs in observations if obs.role == "resolved_control"]
    positive_controls = [obs for obs in observations if obs.role == "positive_control"]
    negative_controls = [obs for obs in observations if obs.role == "negative_control"]
    blocker_phase_groups: dict[str, list[dict]] = {}
    for obs in blockers:
        key = (
            f"phase_plus_{int(obs.phase_advance_to_lt_threshold)}"
            if obs.phase_advance_to_lt_threshold is not None
            else "unresolved_within_window"
        )
        blocker_phase_groups.setdefault(key, []).append(asdict(obs))
    unmodeled_pre_gate_blockers = [asdict(obs) for obs in blockers if obs.requires_unmodeled_pre_gate_consumer]
    family_details = _fighter_8006cda4_family_details()
    observed_phase_keys = sorted(
        {
            f"phase_plus_{int(obs.phase_advance_to_lt_threshold)}"
            for obs in observations
            if obs.phase_advance_to_lt_threshold is not None and int(obs.phase_advance_to_lt_threshold) > 0
        }
    )
    phase_family_details = {
        phase_key: _fighter_8006cda4_phase_family_details(int(phase_key.removeprefix("phase_plus_")))
        for phase_key in observed_phase_keys
    }
    consume_critical_gap_fields = sorted(
        {
            field
            for details in phase_family_details.values()
            for family in details
            for field in family["consume_critical_fields"]
        }
    )
    side_effect_only_fields = sorted(
        {
            field
            for details in phase_family_details.values()
            for family in details
            for field in family["side_effect_only_fields"]
        }
    )
    return {
        "case_count": int(len(observations)),
        "blocker_rows": [asdict(obs) for obs in blockers],
        "blocker_phase_groups": blocker_phase_groups,
        "unmodeled_pre_gate_blockers": unmodeled_pre_gate_blockers,
        "fighter_8006cda4_family_details": family_details,
        "phase_minimal_seed_families": phase_family_details,
        "consume_critical_schema_gap_fields": consume_critical_gap_fields,
        "consume_side_effect_only_fields": side_effect_only_fields,
        "resolved_controls": [asdict(obs) for obs in resolved_controls],
        "positive_controls": [asdict(obs) for obs in positive_controls],
        "negative_controls": [asdict(obs) for obs in negative_controls],
        "blocker": (
            "The common damage-owner family is now closed through an explicit "
            "`fighter_8006cda4_pre_gate_consume_count` seed lane. Decomp shows Fighter_8006CDA4 "
            "consumes pre-gate HSD_Randi samples from hidden held-item / x197C ownership branches "
            "before ftCo_8008DCE0 block_33 evaluates the DamageFlyRoll HSD_Randf gate. Those branch "
            "inputs depend on fighter internals that Slippi post-frames do not expose directly "
            "(`item_gobj`, subtype-3 projectile-empty gating, x197C presence, x2220_b3/x2220_b4, "
            "x2226_b2, and the ftCo_8008E984 guard boolean), so the final replay-facing "
            "representation is the total pre-gate consume count itself rather than a reconstructed "
            "hidden-pointer owner. x1978 remains side-effect-only for item-drop effects and does not "
            "change the pre-gate RNG consume count."
        )
        if not blockers
        else (
            "One Fighter_8006CDA4 pre-gate consumer still remains unmodeled. Site-1 admission "
            "fires on the blocker row, but the HSD_Randf sample at ftCo_8008DCE0 block_33 is still "
            "on the wrong side of the x240 threshold after all modeled pre-gate sites. The remaining "
            "consume-critical hidden fields are item-gobj ownership/non-heavy gating, subtype-3 "
            "projectile-empty gating, x197C presence, x2220_b3/x2220_b4, x2226_b2, and the "
            "ftCo_8008E984 guard boolean; x1978 is side-effect-only."
        ),
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--datasets-dir", type=Path, default=Path("datasets"))
    ap.add_argument("--json-out", type=Path, default=None)
    args = ap.parse_args()

    observations = [observe_case(case, datasets_dir=args.datasets_dir) for case in DEFAULT_CASES]
    summary = build_summary(observations)

    print(f"cases={summary['case_count']}")
    print("blocker_rows:")
    for obs in summary["blocker_rows"]:
        print(
            "  %(dataset)s:%(record)d:p%(p)d seed=%(seed_action_id)d ref=%(ref_action_id)d "
            "out=%(out_action_id)d attacker=%(attacker_seed_action_id)d:%(attacker_seed_action_frame)d "
            "site1=%(site1_count)d roll=%(site1_roll).6f threshold=%(roll_threshold).6f "
            "phase_lt_threshold=%(phase_advance_to_lt_threshold)s modeled_pre_gate=%(modeled_pre_gate_site_counts)s "
            "needs_new_site=%(requires_unmodeled_pre_gate_consumer)s fighter_8006cda4_totals=%(compatible_fighter_8006cda4_total_consumes)s "
            "families=%(fighter_8006cda4_compatible_families)s"
            % obs
        )
        print("    roll_window=%s" % ",".join(f"{float(v):.6f}" for v in obs["site1_roll_window"]))
    print("resolved_controls:")
    for obs in summary["resolved_controls"]:
        print(
            "  %(dataset)s:%(record)d:p%(p)d ref=%(ref_action_id)d out=%(out_action_id)d "
            "site1=%(site1_count)d phase_lt_threshold=%(phase_advance_to_lt_threshold)s "
            "fighter_8006cda4_totals=%(compatible_fighter_8006cda4_total_consumes)s "
            "families=%(fighter_8006cda4_compatible_families)s"
            % obs
        )
    print("phase_minimal_seed_families:")
    for phase_key, families in summary["phase_minimal_seed_families"].items():
        print(f"  {phase_key}:")
        for family in families:
            print(
                "    %(family)s consume_count=%(consume_count)s critical=%(consume_critical_fields)s "
                "side_effect_only=%(side_effect_only_fields)s"
                % family
            )
    print(f"consume_critical_schema_gap_fields={summary['consume_critical_schema_gap_fields']}")
    print(f"consume_side_effect_only_fields={summary['consume_side_effect_only_fields']}")
    print("positive_controls:")
    for obs in summary["positive_controls"]:
        print(
            "  %(dataset)s:%(record)d:p%(p)d ref=%(ref_action_id)d out=%(out_action_id)d "
            "site1=%(site1_count)d roll=%(site1_roll).6f threshold=%(roll_threshold).6f "
            "phase_lt_threshold=%(phase_advance_to_lt_threshold)s modeled_pre_gate=%(modeled_pre_gate_site_counts)s "
            "needs_new_site=%(requires_unmodeled_pre_gate_consumer)s fighter_8006cda4_totals=%(compatible_fighter_8006cda4_total_consumes)s"
            % obs
        )
        print("    roll_window=%s" % ",".join(f"{float(v):.6f}" for v in obs["site1_roll_window"]))
    print("negative_controls:")
    for obs in summary["negative_controls"]:
        print(
            "  %(dataset)s:%(record)d:p%(p)d ref=%(ref_action_id)d out=%(out_action_id)d "
            "site1=%(site1_count)d phase_lt_threshold=%(phase_advance_to_lt_threshold)s "
            "modeled_pre_gate=%(modeled_pre_gate_site_counts)s needs_new_site=%(requires_unmodeled_pre_gate_consumer)s "
            "fighter_8006cda4_totals=%(compatible_fighter_8006cda4_total_consumes)s"
            % obs
        )
    print(f"blocker: {summary['blocker']}")

    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(summary, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
