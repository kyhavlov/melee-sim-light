"""Central character registry for the extraction/preprocess pipeline.

One row per supported character; every tool that needs a per-character fact (Pl*.dat file,
ftData symbol, decomp source dir, ids) should consume this instead of carrying its own
fox/falco literals.

Id spaces:
- internal_id: Melee in-engine character kind (ft/types.h FighterKind order; Fox=1, Sheik=7,
  Marth=18, Zelda=19, Falco=22). This is the id the sim's seeds/binding use (char_id lanes).
- external_id: Slippi/CSS external character id (Fox=2, Marth=9, Sheik=19, Falco=20),
  what replay metadata carries.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class CharInfo:
    name: str  # pipeline name (file stem for data artifacts)
    internal_id: int
    external_id: int
    pl_dat: str  # main fighter data archive in _iso
    aj_dat: str  # animation archive in _iso
    costume_dat: str  # costume-0 model/skeleton archive in _iso
    costume_joint: str  # public root joint symbol in costume_dat
    ftdata_symbol: str  # public symbol of the ftData root in pl_dat
    decomp_dir: str  # refs/melee/src/melee/ft/chara/<dir>
    decomp_prefix: str  # per-char function prefix (ftFx_, ftFc_, ftMs_, ...)
    # Directory whose forward.h holds the character submotion enum used by the init
    # MotionState table (clones reuse the donor's enum: Falco uses ftFox's ftFx_SM_*).
    submotion_dir: str
    submotion_prefix: str
    has_articles: bool  # source character spawns items/articles.
    exports_item_article_constants: bool  # supported by current compact MSLITAR1 exporter.
    anim_table_count: int  # ftData_Table_Unk0[internal_id].count.
    extract_fox_blaster: bool  # ftData.x4 uses the spacie special-attribute family.
    special_attr_layout: str | None  # character-specific ftData.x4 decoder family.
    can_walljump: bool  # audited ft*_Init.c assignment to Fighter.can_walljump.


CHARS: dict[str, CharInfo] = {
    "fox": CharInfo(
        name="fox",
        internal_id=1,
        external_id=2,
        pl_dat="PlFx.dat",
        aj_dat="PlFxAJ.dat",
        costume_dat="PlFxNr.dat",
        costume_joint="PlyFox5K_Share_joint",
        ftdata_symbol="ftDataFox",
        decomp_dir="ftFox",
        decomp_prefix="ftFx_",
        submotion_dir="ftFox",
        submotion_prefix="ftFx_SM_",
        has_articles=True,
        exports_item_article_constants=True,
        anim_table_count=327,
        extract_fox_blaster=True,
        special_attr_layout=None,
        can_walljump=True,
    ),
    "falco": CharInfo(
        name="falco",
        internal_id=22,
        external_id=20,
        pl_dat="PlFc.dat",
        aj_dat="PlFcAJ.dat",
        costume_dat="PlFcNr.dat",
        costume_joint="PlyFalco5K_Share_joint",
        ftdata_symbol="ftDataFalco",
        decomp_dir="ftFalco",
        decomp_prefix="ftFc_",
        submotion_dir="ftFox",
        submotion_prefix="ftFx_SM_",
        has_articles=True,
        exports_item_article_constants=True,
        anim_table_count=327,
        extract_fox_blaster=True,
        special_attr_layout=None,
        can_walljump=True,
    ),
    "marth": CharInfo(
        name="marth",
        internal_id=18,
        external_id=9,
        pl_dat="PlMs.dat",
        aj_dat="PlMsAJ.dat",
        costume_dat="PlMsNr.dat",
        costume_joint="PlyMars5K_Share_joint",
        ftdata_symbol="ftDataMars",
        decomp_dir="ftMars",
        decomp_prefix="ftMs_",
        submotion_dir="ftMars",
        submotion_prefix="ftMs_SM_",
        has_articles=False,
        exports_item_article_constants=False,
        anim_table_count=327,
        extract_fox_blaster=False,
        special_attr_layout="mars_sword",
        can_walljump=False,
    ),
    "falcon": CharInfo(
        name="falcon",
        internal_id=2,
        external_id=0,
        pl_dat="PlCa.dat",
        aj_dat="PlCaAJ.dat",
        costume_dat="PlCaNr.dat",
        costume_joint="PlyCaptain5K_Share_joint",
        ftdata_symbol="ftDataCaptain",
        decomp_dir="ftCaptain",
        decomp_prefix="ftCa_",
        submotion_dir="ftCaptain",
        submotion_prefix="ftCa_SM_",
        has_articles=False,
        exports_item_article_constants=False,
        anim_table_count=318,
        extract_fox_blaster=False,
        special_attr_layout="captain_special",
        can_walljump=True,
    ),
    "sheik": CharInfo(
        name="sheik",
        internal_id=7,
        external_id=19,
        pl_dat="PlSk.dat",
        aj_dat="PlSkAJ.dat",
        costume_dat="PlSkNr.dat",
        costume_joint="PlySeak5K_Share_joint",
        ftdata_symbol="ftDataSeak",
        decomp_dir="ftSeak",
        decomp_prefix="ftSk_",
        submotion_dir="ftSeak",
        submotion_prefix="ftSk_SM_",
        has_articles=True,
        exports_item_article_constants=True,
        anim_table_count=317,
        extract_fox_blaster=False,
        special_attr_layout="seak_special",
        can_walljump=True,
    ),
    "zelda": CharInfo(
        name="zelda",
        internal_id=19,
        external_id=18,
        pl_dat="PlZd.dat",
        aj_dat="PlZdAJ.dat",
        costume_dat="PlZdNr.dat",
        costume_joint="PlyZelda5K_Share_joint",
        ftdata_symbol="ftDataZelda",
        decomp_dir="ftZelda",
        decomp_prefix="ftZd_",
        submotion_dir="ftZelda",
        submotion_prefix="ftZd_SM_",
        has_articles=True,
        exports_item_article_constants=False,
        anim_table_count=311,
        extract_fox_blaster=False,
        special_attr_layout="zelda_special",
        can_walljump=False,
    ),
}

CHAR_PL_DAT: dict[str, str] = {k: v.pl_dat for k, v in CHARS.items()}
CHAR_BY_INTERNAL_ID: dict[int, CharInfo] = {v.internal_id: v for v in CHARS.values()}
CHAR_BY_EXTERNAL_ID: dict[int, CharInfo] = {v.external_id: v for v in CHARS.values()}
