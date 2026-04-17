from __future__ import annotations

import argparse
import csv
import re
from collections import Counter
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


CONTACT_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("hurtcap_id", "u1"),
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
    ],
    align=False,
)


CLASSIFIED_CONTACT_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("contact_kind", "u1"),
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


def _load_action_names(root: Path) -> dict[int, str]:
    text = (root / "src/action_ids.h").read_text(encoding="utf-8")
    out: dict[int, str] = {}
    for m in re.finditer(
        r"MSL_ACT_([A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)[^/]*(?://\s*ftCo_MS_([A-Za-z0-9_]+))?",
        text,
    ):
        out[int(m.group(2), 0)] = m.group(1)
    for val, name in (
        (0x41, "ATTACK_AIR_N"),
        (0x42, "ATTACK_AIR_F"),
        (0x43, "ATTACK_AIR_B"),
        (0x44, "ATTACK_AIR_HI"),
        (0x45, "ATTACK_AIR_LW"),
    ):
        out[val] = name
    return out


def _action_name(action_names: dict[int, str], value: int) -> str:
    return action_names.get(int(value), f"UNKNOWN_0x{int(value) & 0xFFFF:04X}")


def _dataset_path(root: Path, dataset: str) -> Path:
    candidates = [
        root / "datasets/aggregate_recent/replays/validation/aggregate_recent" / dataset,
        root / "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent" / dataset,
        root / "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent" / dataset,
    ]
    for path in candidates:
        if path.exists():
            return path
    raise FileNotFoundError(dataset)


def _group_family_rows(path: Path) -> list[dict[str, object]]:
    grouped: dict[tuple[str, int, str], dict[str, object]] = {}
    with path.open("r", encoding="utf-8", newline="") as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            key = (row["dataset"], int(row["record"]), row["subject"])
            cur = grouped.setdefault(key, {**row, "fields": []})
            cur["fields"].append(row["field"])  # type: ignore[index]
    return list(grouped.values())


def _outcome(ref_action: str, out_action: str) -> str:
    ref_damage = ref_action.startswith("DAMAGE")
    out_damage = out_action.startswith("DAMAGE")
    if out_damage and not ref_damage:
        return "sim_false_body_or_damage"
    if ref_damage and not out_damage:
        return "sim_missed_body_or_damage"
    if ref_damage and out_damage and ref_action != out_action:
        return "wrong_damage_selection"
    return "transition_or_scalar"


def _point_segment_dist(px: float, py: float, pz: float, ax: float, ay: float, az: float, bx: float, by: float, bz: float) -> float:
    abx = bx - ax
    aby = by - ay
    abz = bz - az
    apx = px - ax
    apy = py - ay
    apz = pz - az
    denom = abx * abx + aby * aby + abz * abz
    t = 0.0
    if denom > 0.0:
        t = (apx * abx + apy * aby + apz * abz) / denom
        t = max(0.0, min(1.0, t))
    qx = ax + t * abx
    qy = ay + t * aby
    qz = az + t * abz
    dx = px - qx
    dy = py - qy
    dz = pz - qz
    return float((dx * dx + dy * dy + dz * dz) ** 0.5)


def _simple_overlap_margin(contact: np.void | None) -> float | str:
    if contact is None:
        return ""
    dist = _point_segment_dist(
        float(contact["hitbox_x"]),
        float(contact["hitbox_y"]),
        float(contact["hitbox_z"]),
        float(contact["hurtcap_ax"]),
        float(contact["hurtcap_ay"]),
        float(contact["hurtcap_az"]),
        float(contact["hurtcap_bx"]),
        float(contact["hurtcap_by"]),
        float(contact["hurtcap_bz"]),
    )
    return float(contact["hitbox_radius"]) + float(contact["hurtcap_radius"]) - dist


def audit(family_tsv: Path, out_dir: Path, *, root: Path) -> None:
    import msl_binding

    rows = _group_family_rows(family_tsv)
    action_names = _load_action_names(root)
    datasets: dict[Path, object] = {}
    out_rows: list[dict[str, object]] = []

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    for row in rows:
        dataset_name = str(row["dataset"])
        record = int(row["record"])
        subject = str(row["subject"])
        victim = int(subject[1:])
        attacker_guess = 1 - victim
        ds_path = _dataset_path(root, dataset_name)
        ds = datasets.get(ds_path)
        if ds is None:
            ds = read_dataset(str(ds_path))
            datasets[ds_path] = ds

        sample = ds.samples[record : record + 1]  # type: ignore[attr-defined]
        seed = sample["seed_t"][0]
        ref = sample["ref_t1"][0]
        seed_bytes = np.frombuffer(sample["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
        prev_bytes = np.frombuffer(sample["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        in_bytes = np.frombuffer(sample["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        handle = msl_binding.init(batch_size=1, num_players=int(ds.header["num_players"]))  # type: ignore[index]
        try:
            msl_binding.reseed_seed(handle, seed_bytes)
            msl_binding.debug_step_input_pre_combat(handle, prev_bytes, in_bytes)
            selected_raw, selected_count = msl_binding.debug_combat_select_body_hits(handle, 0, 256)
            classified_raw, classified_count = msl_binding.debug_combat_contacts_classified(handle, 0, 256)
            filtered_raw, filtered_count = msl_binding.debug_combat_contacts_classified_filtered(handle, 0, 256)
        finally:
            msl_binding.destroy(handle)

        handle = msl_binding.init(batch_size=1, num_players=int(ds.header["num_players"]))  # type: ignore[index]
        try:
            msl_binding.reseed_seed(handle, seed_bytes)
            msl_binding.step_input(handle, prev_bytes, in_bytes)
            msl_binding.write_compare(handle, out_bytes)
        finally:
            msl_binding.destroy(handle)

        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        selected = selected_raw.reshape(-1).view(CONTACT_DTYPE)[:selected_count]
        classified = classified_raw.reshape(-1).view(CLASSIFIED_CONTACT_DTYPE)[:classified_count]
        filtered = filtered_raw.reshape(-1).view(CLASSIFIED_CONTACT_DTYPE)[:filtered_count]
        selected_body = [c for c in selected if int(c["defender"]) == victim]
        body_candidates = [
            c for c in classified if int(c["defender"]) == victim and int(c["contact_kind"]) == 0
        ]
        filtered_body_candidates = [
            c for c in filtered if int(c["defender"]) == victim and int(c["contact_kind"]) == 0
        ]
        shield_candidates = [
            c for c in classified if int(c["defender"]) == victim and int(c["contact_kind"]) == 1
        ]

        victim_ref_action = _action_name(action_names, int(ref["action_id"][victim]))
        victim_out_action = _action_name(action_names, int(out["action_id"][victim]))
        first = selected_body[0] if selected_body else (body_candidates[0] if body_candidates else None)
        matrix_overlap: float | str = ""
        if first is not None:
            handle = msl_binding.init(batch_size=1, num_players=int(ds.header["num_players"]))  # type: ignore[index]
            try:
                msl_binding.reseed_seed(handle, seed_bytes)
                msl_binding.debug_step_input_pre_combat(handle, prev_bytes, in_bytes)
                matrix_overlap = float(
                    msl_binding.debug_body_matrix_overlap(
                        handle,
                        0,
                        int(first["attacker"]),
                        int(first["hitbox_id"]),
                        victim,
                        int(first["hurtcap_id"]),
                    )
                )
            finally:
                msl_binding.destroy(handle)
        out_rows.append(
            {
                "dataset": dataset_name,
                "record": record,
                "subject": subject,
                "outcome": _outcome(victim_ref_action, victim_out_action),
                "fields": ",".join(sorted(set(row["fields"]))),  # type: ignore[arg-type]
                "attacker_seed_action": _action_name(action_names, int(seed["action_id"][attacker_guess])),
                "victim_seed_action": _action_name(action_names, int(seed["action_id"][victim])),
                "victim_ref_action": victim_ref_action,
                "victim_out_action": victim_out_action,
                "selected_body_count": len(selected_body),
                "body_candidate_count": len(body_candidates),
                "filtered_body_candidate_count": len(filtered_body_candidates),
                "shield_candidate_count": len(shield_candidates),
                "first_msid": "" if first is None else int(first["attacker_msid"]),
                "first_hitbox": "" if first is None else int(first["hitbox_id"]),
                "first_hurtcap": "" if first is None else int(first["hurtcap_id"]),
                "first_simple_overlap": _simple_overlap_margin(first),
                "first_matrix_overlap": matrix_overlap,
            }
        )

    out_dir.mkdir(parents=True, exist_ok=True)
    sample_tsv = out_dir / "f08b_sample_debug.tsv"
    with sample_tsv.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(out_rows[0].keys()), delimiter="\t")
        writer.writeheader()
        writer.writerows(out_rows)

    def write_counter(fh, title: str, counter: Counter[object]) -> None:
        fh.write(f"\n{title}\n")
        for key, count in counter.most_common(40):
            fh.write(f"{count}\t{key}\n")

    with (out_dir / "summary.txt").open("w", encoding="utf-8") as fh:
        fh.write(f"sample_rows\t{len(out_rows)}\n")
        write_counter(fh, "outcome", Counter(row["outcome"] for row in out_rows))
        write_counter(
            fh,
            "attacker/victim seed/ref/out",
            Counter(
                (
                    row["attacker_seed_action"],
                    row["victim_seed_action"],
                    row["victim_ref_action"],
                    row["victim_out_action"],
                )
                for row in out_rows
            ),
        )
        write_counter(
            fh,
            "contact shape",
            Counter(
                (
                    row["outcome"],
                    row["selected_body_count"],
                    row["body_candidate_count"],
                    row["filtered_body_candidate_count"],
                    row["shield_candidate_count"],
                )
                for row in out_rows
            ),
        )
        write_counter(
            fh,
            "first selected msid/hitbox/hurtcap",
            Counter(
                (row["first_msid"], row["first_hitbox"], row["first_hurtcap"])
                for row in out_rows
                if row["first_hitbox"] != ""
            ),
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--family-tsv", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path("."))
    args = parser.parse_args()
    audit(args.family_tsv, args.out_dir, root=args.root.resolve())


if __name__ == "__main__":
    main()
