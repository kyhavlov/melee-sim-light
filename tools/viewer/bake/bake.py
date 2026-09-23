"""Bake a fighter's animations into a slippilab-style viewer zip.

    python -m bake.bake --character gamewatch --out build/bake/mrGameAndWatch.zip

Output: one <Animation>.json per subaction name, an array of 201 SVG path
strings (frames 0..200) with exact repeats replaced by "frameN" references,
zipped like tools/viewer/assets/character_zips.tsv entries.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / 'build'))
sys.path.insert(0, str(HERE.parent))

from bake.anim import Pose, load_figatree, read_subactions  # noqa: E402
from bake.dat import load_dat_file  # noqa: E402
from bake.model import JOBJ_HIDDEN, load_model, mtx_srt, skinned_triangles, world_matrices  # noqa: E402
from bake.raster import project, rasterize  # noqa: E402
from bake.trace import trace  # noqa: E402
from bake.visibility import VisTables, read_lookup, read_vis_tables, simulate_script  # noqa: E402
from derive_gameplay_parts_mask import CommonData, SUBACTION_COUNTS, u32  # noqa: E402

FRAMES = 201
FTPART_TRANSN = 1  # canonical FtPart_TransN
SKIP_PREFIXES = ('TGamewatch', 'TMewtwo', 'TKirby', 'TFox', 'TFalco', 'TEmblem', 'TPichu')

CHARACTERS = {
    # name: (kind, file prefix, ftData root, model root, init selections, overrides)
    'gamewatch': dict(
        kind=24, prefix='Gw', root='ftDataGamewatch',
        model_root='PlyGamewatch5K_Share_joint',
        outline_slot=10,  # ftGw_Init_OnLoad: fp->x5AC.xC[4] = items[10]
        # ftGw_Init: models 0/2/3 shown, 1 and 4..10 hidden.
        defaults=[0, -1, 0, 0, -1, -1, -1, -1, -1, -1, -1],
        # Code-driven states not in the scripts: Oil Panic's bucket
        # (ftGw_SpecialLw_UpdateBucketModel sets model 5 to state 2 on entry).
        overrides={
            'SpecialLw': {5: 2}, 'SpecialLwCatch': {5: 2}, 'SpecialLwShoot': {5: 2},
            'SpecialAirLw': {5: 2}, 'SpecialAirLwCatch': {5: 2}, 'SpecialAirLwShoot': {5: 2},
        },
    ),
    'kirby': dict(
        kind=4, prefix='Kb', root='ftDataKirby',
        model_root='PlyKirby5K_Share_joint',
        # ftKb_Init_OnDeath: both models at state 0 (the plain body). Hats are
        # separate PlKbCp* joint trees and are not baked here.
        defaults=[0, 0],
        # Stone (ftKb_SpecialLw2 in ftkirbyspecialn.c): model 1 hidden and
        # model 0 on one of the five shapes from ftKb_Init_803CB4EC (2..6),
        # chosen at random in the game; bake the first shape. The 22-frame
        # entry flicker between Kirby and the stone is dropped.
        overrides={'SpecialLw2': {0: 2, 1: -1}, 'SpecialAirLw2': {0: 2, 1: -1}},
    ),
    'roy': dict(
        kind=26, prefix='Fe', root='ftDataEmblem',
        model_root='PlyEmblem5K_Share_joint',
        # ftFe_Init_OnDeath: models 0 and 1 at state 0, model 2 hidden; no
        # code-driven switches elsewhere in ftEmblem/ftMars.
        defaults=[0, 0, -1],
        overrides={},
    ),
    'pichu': dict(
        kind=23, prefix='Pc', root='ftDataPichu',
        model_root='PlyPichu5K_Share_joint',
        # ftPc_Init_OnDeath: model 0 shown; models 1..3 are the costume
        # accessories, all hidden on the plain (Nr) costume.
        defaults=[0, -1, -1, -1],
        overrides={},
    ),
}


# Kirby's copy hats. Twenty copies ship the hat as its own joint tree in
# PlKbCp<Xx>.dat (root word 0); the game loads it with HSD_JObjLoadJoint and
# every frame copies fp->parts[6]'s world matrix onto its root
# (ftKb_UnkMtxFunc0; part 6 is the head node of Kirby's part list, not the
# canonical FtPart id 6), so the hat rides the head. Five copies (DK, Falco,
# G&W, Mewtwo, Jigglypuff) replace the whole costume with PlKbNrCp<Xx>.dat,
# whose joint tree is Kirby's 59-node part list minus the other costumes'
# accessory parts (see costume_joint_map); the costume meshes hang off the
# root and skin to those joints.
KIRBY_HAT_PART = 6
KIRBY_HATS = {
    # fighter kind: (file suffix, 'tree' | 'costume')
    0: ('Mr', 'tree'), 1: ('Fx', 'tree'), 2: ('Ca', 'tree'), 3: ('Dk', 'costume'),
    5: ('Kp', 'tree'), 6: ('Lk', 'tree'), 7: ('Sk', 'tree'), 8: ('Ns', 'tree'),
    9: ('Pe', 'tree'), 10: ('Pp', 'tree'), 12: ('Pk', 'tree'), 13: ('Ss', 'tree'),
    14: ('Ys', 'tree'), 15: ('Pr', 'costume'), 16: ('Mt', 'costume'), 17: ('Lg', 'tree'),
    18: ('Ms', 'tree'), 19: ('Zd', 'tree'), 20: ('Cl', 'tree'), 21: ('Dr', 'tree'),
    22: ('Fc', 'costume'), 23: ('Pc', 'tree'), 24: ('Gw', 'costume'), 25: ('Gn', 'tree'),
    26: ('Fe', 'tree'),
}


def accessory_parts(common: CommonData, kind: int) -> dict[int, int]:
    """part -> costume mode for the parts ftParts_8007506C excludes from the
    plain skeleton (Fighter_804D6540 descriptors {part, attach, mode,
    preorder}). Only a chain's head carries the mode; the rest attach to the
    previous accessory part, so each part resolves through its chain."""
    t = u32(common.data, common.excl_arr + kind * 4)
    attach: dict[int, int] = {}
    mode: dict[int, int] = {}
    if t:
        arr, cnt = u32(common.data, t), u32(common.data, t + 4)
        for i in range(cnt):
            part, attach_part, m, _depth = common.data[arr + i * 4:arr + i * 4 + 4]
            attach[part], mode[part] = attach_part, m
    descs: dict[int, int] = {}
    for part in attach:
        head = part
        while mode[head] == 0:
            head = attach[head]
        descs[part] = mode[head]
    return descs


def base_joint_of_part(common: CommonData, kind: int, part: int) -> int:
    """Model joint (preorder index of the plain costume) for fp->parts[part]."""
    _, _, n = common.parts_table(kind)
    descs = accessory_parts(common, kind)
    return [p for p in range(n) if p not in descs].index(part)


def costume_joint_map(common: CommonData, kind: int, model) -> list[int | None]:
    """For each joint of a copy-costume model, the base (PlKbNr) joint that
    drives it, or None for an accessory joint that keeps its bind pose.

    PlCo's parts table for Kirby lists 59 nodes; ftParts_8007506C excludes
    13 accessory parts from the plain skeleton. Each accessory descriptor
    belongs to one costume (mode 1 Jigglypuff, 2 Mewtwo, 3 Falco), and that
    costume's joint tree is the full list with only its own accessory parts
    kept."""
    _, _, n = common.parts_table(kind)
    descs = accessory_parts(common, kind)
    base = [p for p in range(n) if p not in descs]
    nodes = base
    for mode in sorted(set(descs.values())):
        cand = [p for p in range(n) if p not in descs or descs[p] == mode]
        if len(cand) == len(model.joints):
            nodes = cand
            break
    if len(nodes) != len(model.joints):
        raise ValueError(f'costume has {len(model.joints)} joints; no part subset matches')
    base_index = {p: k for k, p in enumerate(base)}
    return [base_index.get(p) for p in nodes]


def world_matrices_rooted(model, rotation, scale, translate, root_world):
    """World matrices for a hat tree whose root matrix is replaced by the
    head bone's (JOBJ_USER_DEF_MTX): children hang off root_world."""
    world = []
    for j in model.joints:
        if j.parent < 0:
            world.append(root_world)
            continue
        local = mtx_srt(scale[j.index], rotation[j.index], translate[j.index], None)
        world.append(world[j.parent] @ local)
    return world


def load_hat(data_dir: Path, kind: int):
    """(model, mode, visible DObj indices) for a copy hat.

    PlKbCp<Xx>.dat's root is the hat's KirbyHatStruct: a tree hat starts with
    its joint, then the FtPartsDesc {model_num, vis_table}; a costume hat has
    no joint, so the descriptor sits at the root. The vis table has the
    fighter layout (four lookups per costume row) and ftParts_8007487C sets
    it up for the hat's own DObj list; ftKb_UnkIntBoolFunc0 only toggles
    groups on rare motion events, so state 0 is baked. Group 2 (metal) and the
    second joint tree the costume hats carry for the metal box are skipped."""
    suffix, mode = KIRBY_HATS[kind]
    dat = load_dat_file(data_dir / f'PlKbCp{suffix}.dat')
    root = next(iter(dat.roots.values()))
    if mode == 'tree':
        joint = struct.unpack_from('>I', dat.data, root)[0]
        model = load_model(dat, joint)
        desc = root + 4
    else:
        costume = load_dat_file(data_dir / f'PlKbNrCp{suffix}.dat')
        model = load_model(costume, costume.roots[f'PlyKirby{suffix}_Share_joint'])
        desc = root
    model_num = dat.u32(desc)
    vis_table = dat.ptr(desc + 4)
    groups = []
    for g in range(4):
        lookup = dat.ptr(vis_table + g * 4) if vis_table else 0
        groups.append(read_lookup(dat, lookup, model_num) if lookup and g != 2 else [])
    visible = VisTables(model_num, groups).visible(len(model.dobjs), [0] * model_num)
    return model, mode, visible


def animation_name(subaction_name: str) -> str:
    m = subaction_name.split('_ACTION_', 1)
    tail = m[1] if len(m) == 2 else subaction_name
    return tail[:-len('_figatree')] if tail.endswith('_figatree') else tail


def bake_character(name: str, data_dir: Path, out_zip: Path, only: set[str] | None = None,
                   dump_dir: Path | None = None, hat: int | None = None) -> None:
    spec = CHARACTERS[name]
    hat_model, hat_mode, hat_visible = load_hat(data_dir, hat) if hat is not None else (None, None, None)
    ftdata = load_dat_file(data_dir / f'Pl{spec["prefix"]}.dat')
    model_dat = load_dat_file(data_dir / f'Pl{spec["prefix"]}Nr.dat')
    aj = (data_dir / f'Pl{spec["prefix"]}AJ.dat').read_bytes()
    model = load_model(model_dat, model_dat.roots[spec['model_root']])
    vis = read_vis_tables(ftdata, spec['root'], extra_item_slot=spec.get('outline_slot'))
    common = CommonData(str(data_dir / 'PlCo.dat'))
    hat_map = costume_joint_map(common, spec['kind'], hat_model) if hat_mode == 'costume' else None
    hat_root = base_joint_of_part(common, spec['kind'], KIRBY_HAT_PART) if hat_mode == 'tree' else None
    _, p2j, _ = common.parts_table(spec['kind'])
    transn = p2j[FTPART_TRANSN]
    subactions = read_subactions(ftdata, spec['root'], SUBACTION_COUNTS[spec['kind']])

    base_rot = [j.rotation for j in model.joints]
    base_scl = [j.scale for j in model.joints]
    base_tr = [j.translate for j in model.joints]
    parent = [j.parent for j in model.joints]

    def subtree(root: int) -> set[int]:
        out = {root}
        changed = True
        while changed:
            changed = False
            for j in model.joints:
                if j.parent in out and j.index not in out:
                    out.add(j.index)
                    changed = True
        return out

    done: dict[str, list[str]] = {}
    for sub in subactions:
        anim = animation_name(sub.name)
        if anim in done or (only and anim not in only):
            continue
        # Match slippilab's set: item-carry, heavy-item and victim-side throw
        # animations are never selected by the viewer maps.
        if anim.startswith(('Heavy', 'Item', 'T' + spec['prefix'])) or anim.startswith(SKIP_PREFIXES):
            continue
        if sub.anim_size == 0:
            continue
        tree = load_figatree(aj[sub.anim_offset:sub.anim_offset + sub.anim_size], anim)
        pose = Pose(tree, base_rot, base_scl, base_tr)
        override = spec['overrides'].get(anim, {})

        def apply_override(frame, selection, override=override):
            for k, v in override.items():
                if k < len(selection):
                    selection[k] = v
            return selection

        timeline = simulate_script(ftdata, sub.script, spec['defaults'], FRAMES, apply_override)
        frames: list[str] = []
        seen: dict[str, int] = {}
        for f in range(FRAMES):
            rot, scl, tr, hidden_nodes, hidden_branches = pose.evaluate(float(f))
            # slippilab locked TransN's translation so the fighter stays
            # centred; replay position supplies movement.
            tr[transn] = [0.0, 0.0, 0.0]
            world = world_matrices(model, rot, scl, tr)
            hidden_joints = set()
            for j in model.joints:
                if j.flags & JOBJ_HIDDEN:
                    hidden_joints |= subtree(j.index)
            for j in hidden_branches:
                hidden_joints |= subtree(j)
            hidden_joints |= hidden_nodes
            visible = vis.visible(len(model.dobjs), timeline.selections[f])
            visible = {d for d in visible if model.dobjs[d].joint not in hidden_joints}
            if hat_model is None:
                tris = skinned_triangles(model, world, visible)
            elif hat_mode == 'tree':
                hat_world = world_matrices_rooted(
                    hat_model, [j.rotation for j in hat_model.joints],
                    [j.scale for j in hat_model.joints], [j.translate for j in hat_model.joints],
                    world[hat_root])
                hat_hidden = set()
                for j in hat_model.joints:
                    if j.flags & JOBJ_HIDDEN:
                        hat_hidden |= {d for d in range(len(hat_model.dobjs)) if hat_model.dobjs[d].joint == j.index}
                tris = skinned_triangles(hat_model, hat_world, hat_visible - hat_hidden)
            else:
                # Costume swap: the game skins the costume meshes to Kirby's
                # own JObjs (msl_kirby_load_hat_dobjs resolves each costume
                # joint to a part), so mapped joints take Kirby's world
                # matrices outright (the costume tree lacks Kirby's classical-
                # scale flags); accessory joints hang off their parent in
                # their bind SRT.
                hat_world = []
                for j, k in zip(hat_model.joints, hat_map):
                    if k is not None:
                        hat_world.append(world[k])
                    else:
                        local = mtx_srt(j.scale, j.rotation, j.translate, None)
                        hat_world.append(hat_world[j.parent] @ local)
                c_hidden = {i for i, k in enumerate(hat_map) if k in hidden_joints}
                shown = {d for d in hat_visible if hat_model.dobjs[d].joint not in c_hidden}
                tris = skinned_triangles(hat_model, hat_world, shown)
            image = rasterize(project(tris, 2, 1.0))
            if dump_dir is not None:
                dump_dir.mkdir(parents=True, exist_ok=True)
                (dump_dir / f'{anim}_{f}.pgm').write_bytes(
                    b'P5\n1000 1000\n255\n' + ((1 - image) * 255).astype(np.uint8).tobytes())
            path = trace(image)
            ref = seen.get(path)
            if ref is not None:
                frames.append(f'frame{ref}')
            else:
                seen[path] = f
                frames.append(path)
        done[anim] = frames
        print(f'{anim}: {tree.frames:.0f} source frames', file=sys.stderr)

    out_zip.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out_zip, 'w', zipfile.ZIP_DEFLATED) as zf:
        for anim, frames in done.items():
            zf.writestr(f'{anim}.json', json.dumps(frames, separators=(',', ':')))
    print(f'wrote {out_zip} ({len(done)} animations)', file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--character', required=True, choices=sorted(CHARACTERS))
    ap.add_argument('--data', default=str(HERE.parents[2] / 'data' / 'raw'))
    ap.add_argument('--out', required=True)
    ap.add_argument('--only', default='', help='comma-separated animation names')
    ap.add_argument('--hat', type=int, default=None,
                    help="bake Kirby's copy hat for this fighter kind as an overlay")
    ap.add_argument('--all-hats', default='', metavar='DIR',
                    help='bake every Kirby hat overlay into DIR as kirbyHat<Xx>.zip')
    ap.add_argument('--dump', default='', help='directory for per-frame PGM dumps')
    args = ap.parse_args()
    if args.all_hats:
        from concurrent.futures import ProcessPoolExecutor
        out_dir = Path(args.all_hats)
        out_dir.mkdir(parents=True, exist_ok=True)
        jobs = {kind: out_dir / f'kirbyHat{suffix}.zip' for kind, (suffix, _) in KIRBY_HATS.items()}
        with ProcessPoolExecutor() as pool:
            futures = [pool.submit(bake_character, 'kirby', Path(args.data), out, None, None, kind)
                       for kind, out in jobs.items()]
            for f in futures:
                f.result()
        return 0
    only = {s for s in args.only.split(',') if s} or None
    bake_character(args.character, Path(args.data), Path(args.out), only,
                   Path(args.dump) if args.dump else None, args.hat)
    return 0


if __name__ == '__main__':
    sys.exit(main())
