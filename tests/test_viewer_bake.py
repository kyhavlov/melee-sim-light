"""Silhouette bake (tools/viewer/bake): model decode and manifest integrity."""
from __future__ import annotations

import hashlib
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'viewer'))
sys.path.insert(0, str(ROOT / 'tools' / 'build'))

DATA = ROOT / 'data' / 'raw'


def _manifest_rows():
    rows = {}
    for line in (ROOT / 'tools/viewer/assets/character_zips.tsv').read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        name, sha, url = line.split('\t')
        rows[name] = (sha, url)
    return rows


def test_baked_assets_match_manifest():
    rows = _manifest_rows()
    baked = {n: v for n, v in rows.items() if v[1].startswith('baked:')}
    assert 'mrGameAndWatch.zip' in baked
    assert 'kirby.zip' in baked
    assert 'roy.zip' in baked
    assert 'pichu.zip' in baked
    # One overlay per baked copy hat (tools/viewer/bake KIRBY_HATS).
    assert len([n for n in baked if n.startswith('kirbyHat')]) == 25
    for name, (sha, url) in baked.items():
        path = ROOT / url[len('baked:'):]
        assert path.is_file(), path
        assert hashlib.sha256(path.read_bytes()).hexdigest() == sha, name


@pytest.mark.skipif(not (DATA / 'PlGwNr.dat').is_file(), reason='needs extracted data/raw')
def test_gamewatch_model_and_visibility_decode():
    from bake.dat import load_dat_file
    from bake.model import load_model
    from bake.visibility import read_vis_tables

    dat = load_dat_file(DATA / 'PlGwNr.dat')
    model = load_model(dat, dat.roots['PlyGamewatch5K_Share_joint'])
    assert len(model.joints) == 53
    assert len(model.dobjs) == 116
    ftdata = load_dat_file(DATA / 'PlGw.dat')
    vis = read_vis_tables(ftdata, 'ftDataGamewatch', extra_item_slot=10)
    assert vis.model_num == 11
    # Every DObj is either body (unlisted) or in a lookup; the outline copies
    # (group 4) cover what the costume tables leave out.
    assert vis.listed() | {d for d in range(116) if d not in vis.listed()} == set(range(116))
    body = vis.visible(116, [0, -1, 0, 0, -1, -1, -1, -1, -1, -1, -1])
    assert {0, 17, 18, 19, 20, 21, 22, 36, 37, 38, 39, 40, 41} == body


@pytest.mark.skipif(not (DATA / 'PlGwAJ.dat').is_file(), reason='needs extracted data/raw')
def test_gamewatch_animation_matches_bind_translations():
    from bake.anim import FObjState, load_figatree, read_subactions
    from bake.dat import load_dat_file
    from bake.model import load_model
    from derive_gameplay_parts_mask import SUBACTION_COUNTS

    ftdata = load_dat_file(DATA / 'PlGw.dat')
    aj = (DATA / 'PlGwAJ.dat').read_bytes()
    dat = load_dat_file(DATA / 'PlGwNr.dat')
    model = load_model(dat, dat.roots['PlyGamewatch5K_Share_joint'])
    subs = read_subactions(ftdata, 'ftDataGamewatch', SUBACTION_COUNTS[24])
    wait = next(s for s in subs if s.name.endswith('_Wait1_figatree'))
    tree = load_figatree(aj[wait.anim_offset:wait.anim_offset + wait.anim_size], 'Wait1')
    assert len(tree.joint_tracks) == 53
    assert tree.frames == 60.0
    # Constant translation tracks reproduce the skeleton's bind translations.
    for joint in (3, 6, 9, 17, 19, 20, 24, 32, 51):
        tracks = {t.obj_type: t for t in tree.joint_tracks[joint]}
        for axis, kind in enumerate((5, 6, 7)):
            if kind in tracks:
                value = FObjState(tracks[kind]).evaluate(0.0)
                assert abs(value - model.joints[joint].translate[axis]) < 0.02, (joint, axis)


@pytest.mark.skipif(not (DATA / 'PlKbNr.dat').is_file(), reason='needs extracted data/raw')
def test_kirby_model_and_visibility_decode():
    from bake.dat import load_dat_file
    from bake.model import load_model
    from bake.visibility import read_vis_tables

    dat = load_dat_file(DATA / 'PlKbNr.dat')
    model = load_model(dat, dat.roots['PlyKirby5K_Share_joint'])
    assert len(model.joints) == 46
    assert len(model.dobjs) == 42
    ftdata = load_dat_file(DATA / 'PlKb.dat')
    vis = read_vis_tables(ftdata, 'ftDataKirby')
    assert vis.model_num == 2
    # ftKb_Init_OnDeath selects state 0 on both models: the plain body.
    assert vis.visible(42, [0, 0]) == {3, 4, 5, 6, 7, 18, 19}
    # Stone hides model 1 and swaps model 0 to one of the stone shapes.
    assert vis.visible(42, [2, -1]) == {20, 21, 22}


@pytest.mark.skipif(not (DATA / 'PlKbCpFx.dat').is_file(), reason='needs extracted data/raw')
def test_kirby_hat_trees_load():
    from bake.bake import KIRBY_HATS, costume_joint_map, load_hat
    from derive_gameplay_parts_mask import CommonData

    common = CommonData(str(DATA / 'PlCo.dat'))
    # Every hat carries drawable parts and its own vis table hides some of
    # them; costume swaps map onto Kirby's 46 base joints by part id, with
    # only their own accessory joints left unmapped.
    accessory = {'Dk': 0, 'Gw': 0, 'Fc': 2, 'Mt': 7, 'Pr': 4}
    for kind, (suffix, mode) in KIRBY_HATS.items():
        model, loaded_mode, visible = load_hat(DATA, kind)
        assert loaded_mode == mode
        assert 0 < len(visible) < len(model.dobjs), suffix
        if mode == 'costume':
            mapping = costume_joint_map(common, 4, model)
            assert len(mapping) == 46 + accessory[suffix], suffix
            assert [k for k in mapping if k is not None] == list(range(46)), suffix
