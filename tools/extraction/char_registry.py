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
    ftdata_symbol: str  # public symbol of the ftData root in pl_dat
    decomp_dir: str  # refs/melee/src/melee/ft/chara/<dir>
    decomp_prefix: str  # per-char function prefix (ftFx_, ftFc_, ftMs_, ...)
    # Directory whose forward.h holds the character submotion enum used by the init
    # MotionState table (clones reuse the donor's enum: Falco uses ftFox's ftFx_SM_*).
    submotion_dir: str
    submotion_prefix: str
    has_articles: bool  # source character spawns items/articles.
    exports_item_article_constants: bool  # supported by current compact MSLITAR1 exporter.


CHARS: dict[str, CharInfo] = {
    "fox": CharInfo(
        name="fox",
        internal_id=1,
        external_id=2,
        pl_dat="PlFx.dat",
        aj_dat="PlFxAJ.dat",
        ftdata_symbol="ftDataFox",
        decomp_dir="ftFox",
        decomp_prefix="ftFx_",
        submotion_dir="ftFox",
        submotion_prefix="ftFx_SM_",
        has_articles=True,
        exports_item_article_constants=True,
    ),
    "falco": CharInfo(
        name="falco",
        internal_id=22,
        external_id=20,
        pl_dat="PlFc.dat",
        aj_dat="PlFcAJ.dat",
        ftdata_symbol="ftDataFalco",
        decomp_dir="ftFalco",
        decomp_prefix="ftFc_",
        submotion_dir="ftFox",
        submotion_prefix="ftFx_SM_",
        has_articles=True,
        exports_item_article_constants=True,
    ),
    "marth": CharInfo(
        name="marth",
        internal_id=18,
        external_id=9,
        pl_dat="PlMs.dat",
        aj_dat="PlMsAJ.dat",
        ftdata_symbol="ftDataMars",
        decomp_dir="ftMars",
        decomp_prefix="ftMs_",
        submotion_dir="ftMars",
        submotion_prefix="ftMs_SM_",
        has_articles=False,
        exports_item_article_constants=False,
    ),
    "falcon": CharInfo(
        name="falcon",
        internal_id=2,
        external_id=0,
        pl_dat="PlCa.dat",
        aj_dat="PlCaAJ.dat",
        ftdata_symbol="ftDataCaptain",
        decomp_dir="ftCaptain",
        decomp_prefix="ftCa_",
        submotion_dir="ftCaptain",
        submotion_prefix="ftCa_SM_",
        has_articles=False,
        exports_item_article_constants=False,
    ),
    "puff": CharInfo(
        name="puff",
        internal_id=15,
        external_id=15,
        pl_dat="PlPr.dat",
        aj_dat="PlPrAJ.dat",
        ftdata_symbol="ftDataPurin",
        decomp_dir="ftPurin",
        decomp_prefix="ftPr_",
        submotion_dir="ftPurin",
        submotion_prefix="ftPr_SM_",
        has_articles=False,
        exports_item_article_constants=False,
    ),
    "sheik": CharInfo(
        name="sheik",
        internal_id=7,
        external_id=19,
        pl_dat="PlSk.dat",
        aj_dat="PlSkAJ.dat",
        ftdata_symbol="ftDataSeak",
        decomp_dir="ftSeak",
        decomp_prefix="ftSk_",
        submotion_dir="ftSeak",
        submotion_prefix="ftSk_SM_",
        has_articles=True,
        exports_item_article_constants=True,
    ),
    "zelda": CharInfo(
        name="zelda",
        internal_id=19,
        external_id=18,
        pl_dat="PlZd.dat",
        aj_dat="PlZdAJ.dat",
        ftdata_symbol="ftDataZelda",
        decomp_dir="ftZelda",
        decomp_prefix="ftZd_",
        submotion_dir="ftZelda",
        submotion_prefix="ftZd_SM_",
        has_articles=True,
        exports_item_article_constants=False,
    ),
}

CHAR_PL_DAT: dict[str, str] = {k: v.pl_dat for k, v in CHARS.items()}
CHAR_BY_INTERNAL_ID: dict[int, CharInfo] = {v.internal_id: v for v in CHARS.values()}
CHAR_BY_EXTERNAL_ID: dict[int, CharInfo] = {v.external_id: v for v in CHARS.values()}
