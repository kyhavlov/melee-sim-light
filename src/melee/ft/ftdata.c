#include "ftdata.h"

#include "ft_0877.h"
#include "ft_459A.h"

#include <platform.h>

#ifdef MSL_CORE_NATIVE
#include "platform/memory.h"
#include "runtime/context.h"
#include "runtime/fighter_pose.h"
#endif

#include "ef/efasync.h"

#include "forward.h"

#include "ft/fighter.h"
#include "ft/ft_0852.h"
#include "ft/inlines.h"
#include "ft/types.h"
#include "ftCaptain/ftCa_Init.h"
#include "ftCaptain/ftCa_SpecialHi.h"
#include "ftCaptain/ftCa_SpecialLw.h"
#include "ftCaptain/ftCa_SpecialN.h"
#include "ftCaptain/ftCa_SpecialS.h"
#include "ftCLink/ftCl_Init.h"
#include "ftCrazyHand/ftCh_Init.h"
#include "ftDonkey/ftDk_Init.h"
#include "ftDonkey/ftDk_SpecialHi.h"
#include "ftDonkey/ftDk_SpecialLw.h"
#include "ftDonkey/ftDk_SpecialN.h"
#include "ftDonkey/ftDk_SpecialS.h"
#include "ftDrMario/ftDr_Init.h"
#include "ftEmblem/ftFe_Init.h"
#include "ftFalco/ftFc_Init.h"
#include "ftFox/ftFx_Init.h"
#include "ftFox/ftFx_SpecialHi.h"
#include "ftFox/ftFx_SpecialLw.h"
#include "ftFox/ftFx_SpecialN.h"
#include "ftFox/ftFx_SpecialS.h"
#include "ftGameWatch/ftGw_Init.h"
#include "ftGameWatch/ftGw_SpecialHi.h"
#include "ftGameWatch/ftGw_SpecialLw.h"
#include "ftGameWatch/ftGw_SpecialN.h"
#include "ftGameWatch/ftGw_SpecialS.h"
#include "ftGanon/ftGn_Init.h"
#include "ftGigaKoopa/ftGk_Init.h"
#include "ftKirby/ftkirby.h"
#include "ftKirby/ftkirbyspecialhi.h"
#include "ftKoopa/ftKp_Init.h"
#include "ftKoopa/ftKp_SpecialHi.h"
#include "ftKoopa/ftKp_SpecialLw.h"
#include "ftKoopa/ftKp_SpecialN.h"
#include "ftLink/ftLk_Init.h"
#include "ftLink/ftLk_SpecialHi.h"
#include "ftLink/ftLk_SpecialLw.h"
#include "ftLink/ftLk_SpecialN.h"
#include "ftLink/ftLk_SpecialS.h"
#include "ftDrMario/ftDr_Init.h"
#include "ftLuigi/ftLg_Init.h"
#include "ftLuigi/ftLg_SpecialHi.h"
#include "ftLuigi/ftLg_SpecialLw.h"
#include "ftLuigi/ftLg_SpecialN.h"
#include "ftLuigi/ftLg_SpecialS.h"
#include "ftMario/ftMr_Init.h"
#include "ftMario/ftMr_SpecialHi.h"
#include "ftMario/ftMr_SpecialLw.h"
#include "ftMario/ftMr_SpecialN.h"
#include "ftMario/ftMr_SpecialS.h"
#include "ftMario/ftMr_Init.h"
#include "ftMario/ftMr_SpecialHi.h"
#include "ftMario/ftMr_SpecialLw.h"
#include "ftMario/ftMr_SpecialN.h"
#include "ftMario/ftMr_SpecialS.h"
#include "ftMario/ftMr_Strings.h"
#include "ftMars/ftMs_Init.h"
#include "ftMars/ftMs_SpecialHi.h"
#include "ftMars/ftMs_SpecialLw.h"
#include "ftMars/ftMs_SpecialN.h"
#include "ftMars/ftMs_SpecialS.h"
#include "ftMasterHand/ftMh_Init.h"
#include "ftMewtwo/ftMt_Init.h"
#include "ftMewtwo/ftMt_SpecialHi.h"
#include "ftMewtwo/ftMt_SpecialLw.h"
#include "ftMewtwo/ftMt_SpecialN.h"
#include "ftMewtwo/ftMt_SpecialS.h"
#include "ftNana/ftNn_Init.h"
#include "ftNess/ftNs_Init.h"
#include "ftNess/ftNs_SpecialHi.h"
#include "ftNess/ftNs_SpecialLw.h"
#include "ftNess/ftNs_SpecialN.h"
#include "ftNess/ftNs_SpecialS.h"
#include "ftPeach/ftPe_Init.h"
#include "ftPeach/ftPe_SpecialHi.h"
#include "ftPeach/ftPe_SpecialLw.h"
#include "ftPeach/ftPe_SpecialN.h"
#include "ftPeach/ftPe_SpecialS.h"
#include "ftPichu/ftPc_Init.h"
#include "ftPikachu/ftPk_Init.h"
#include "ftPikachu/ftPk_SpecialHi.h"
#include "ftPikachu/ftPk_SpecialLw.h"
#include "ftPikachu/ftPk_SpecialN.h"
#include "ftPikachu/ftPk_SpecialS.h"
#include "ftPopo/ftPp_Init.h"
#include "ftPopo/ftPp_SpecialHi.h"
#include "ftPopo/ftPp_SpecialLw.h"
#include "ftPopo/ftPp_SpecialN.h"
#include "ftPopo/ftPp_SpecialS.h"
#include "ftPurin/ftPr_Init.h"
#include "ftPurin/ftPr_SpecialHi.h"
#include "ftPurin/ftPr_SpecialLw.h"
#include "ftPurin/ftPr_SpecialN.h"
#include "ftPurin/ftPr_SpecialS.h"
#include "ftSamus/ftSs_Init.h"
#include "ftSamus/ftSs_SpecialHi.h"
#include "ftSamus/ftSs_SpecialLw_1.h"
#include "ftSamus/ftSs_SpecialN.h"
#include "ftSamus/ftSs_SpecialS.h"
#include "ftSandbag/ftSb_Init.h"
#include "ftSeak/ftSk_Init.h"
#include "ftSeak/ftSk_SpecialHi.h"
#include "ftSeak/ftSk_SpecialLw.h"
#include "ftSeak/ftSk_SpecialN.h"
#include "ftSeak/ftSk_SpecialS.h"
#include "ftYoshi/ftYs_Guard.h"
#include "ftYoshi/ftYs_Init.h"
#include "ftYoshi/ftYs_SpecialHi.h"
#include "ftYoshi/ftYs_SpecialLw.h"
#include "ftYoshi/ftYs_SpecialN.h"
#include "ftYoshi/ftYs_SpecialS.h"
#include "ftZakoBoy/ftBo_Init.h"
#include "ftZakoGirl/ftGl_Init.h"
#include "ftZelda/ftZd_Init.h"
#include "ftZelda/ftZd_SpecialHi.h"
#include "ftZelda/ftZd_SpecialLw.h"
#include "ftZelda/ftZd_SpecialN.h"
#include "ftZelda/ftZd_SpecialS.h"
#include "lb/lbarchive.h"
#include "lb/lbarq.h"
#include "lb/lbdvd.h"
#include "lb/lbfile.h"
#include "pl/player.h"

#include <baselib/forward.h>

#include <baselib/debug.h>
#include <baselib/objalloc.h>

#ifndef MSL_CORE_HOSTED
extern int ft_8045996C[FTKIND_MAX];
#endif

#ifdef MSL_CORE_HOSTED
// The hosted core preserves the source registry's shape but stores its loaded
// animation-DAT pointer in GameData. The source DOL owns this as one process
// global; a host may destroy and recreate equivalent immutable GameData.
// refs/melee/src/melee/ft/ftdata.c:ftData_Table_Unk0
static const ftData_UnkCountStruct
    hosted_animation_data_template[FTKIND_MAX] = {
    [FTKIND_MEWTWO] = { 0, 314 },
    [FTKIND_GAMEWATCH] = { 0, 323 },
    [FTKIND_KOOPA] = { 0, 316 },
    [FTKIND_YOSHI] = { 0, 314 },
        [FTKIND_FOX] = { 0, 327 },   [FTKIND_CAPTAIN] = { 0, 318 },
        [FTKIND_SEAK] = { 0, 317 },  [FTKIND_PEACH] = { 0, 318 },
        [FTKIND_PURIN] = { 0, 327 }, [FTKIND_LUIGI] = { 0, 312 },
        [FTKIND_MARIO] = { 0, 303 }, [FTKIND_DRMARIO] = { 0, 303 },
        [FTKIND_SAMUS] = { 0, 313 },
        [FTKIND_POPO] = { 0, 321 },  [FTKIND_NANA] = { 0, 321 },
        [FTKIND_DONKEY] = { 0, 337 },
        [FTKIND_GANON] = { 0, 318 },
        [FTKIND_KIRBY] = { 0, 479 },
        [FTKIND_PIKACHU] = { 0, 320 },
        [FTKIND_PICHU] = { 0, 320 },
        [FTKIND_NESS] = { 0, 326 },
        [FTKIND_LINK] = { 0, 314 },  [FTKIND_CLINK] = { 0, 314 },
        [FTKIND_MARS] = { 0, 327 },
        [FTKIND_EMBLEM] = { 0, 327 },
        [FTKIND_ZELDA] = { 0, 311 }, [FTKIND_FALCO] = { 0, 327 },
    };

void msl_core_fighter_animation_data_init(void)
{
    memcpy(ftData_Table_Unk0, hosted_animation_data_template,
           sizeof(hosted_animation_data_template));
}
Event ftData_Table_Unk1[FTKIND_MAX] = {
    [FTKIND_PURIN] = ftPr_Init_8013C2F8,
};
HSD_GObjEvent ftData_OnLoad[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_OnLoad,
    [FTKIND_MEWTWO] = ftMt_Init_OnLoad,
    [FTKIND_GAMEWATCH] = ftGw_Init_OnLoad,
    [FTKIND_KOOPA] = ftKp_Init_OnLoad,
    [FTKIND_YOSHI] = ftYs_Init_OnLoad,
    [FTKIND_FOX] = ftFx_Init_OnLoad,
    [FTKIND_CAPTAIN] = ftCa_Init_OnLoad,
    [FTKIND_SEAK] = ftSk_Init_OnLoad,
    [FTKIND_PEACH] = ftPe_Init_OnLoad,
    [FTKIND_PURIN] = ftPr_Init_OnLoad,
    [FTKIND_SAMUS] = ftSs_Init_OnLoad,
    [FTKIND_POPO] = ftPp_Init_OnLoad,
    [FTKIND_NANA] = ftNn_Init_OnLoad,
    [FTKIND_DONKEY] = ftDk_Init_OnLoad,
    [FTKIND_GANON] = ftGn_Init_OnLoad,
    [FTKIND_PIKACHU] = ftPk_Init_OnLoad,
    [FTKIND_PICHU] = ftPc_Init_OnLoad,
    [FTKIND_NESS] = ftNs_Init_OnLoad,
    [FTKIND_LINK] = ftLk_Init_OnLoad,
    [FTKIND_CLINK] = ftCl_Init_OnLoad,
    [FTKIND_LUIGI] = ftLg_Init_OnLoad,
    [FTKIND_MARIO] = ftMr_Init_OnLoad,
    [FTKIND_DRMARIO] = ftDr_Init_OnLoad,
    [FTKIND_MARS] = ftMs_Init_OnLoad,
    [FTKIND_EMBLEM] = ftFe_Init_OnLoad,
    [FTKIND_ZELDA] = ftZd_Init_OnLoad,
    [FTKIND_FALCO] = ftFc_Init_OnLoad,
};
HSD_GObjEvent ftData_OnDeath[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_OnDeath,
    [FTKIND_MEWTWO] = ftMt_Init_OnDeath,
    [FTKIND_GAMEWATCH] = ftGw_Init_OnDeath,
    [FTKIND_KOOPA] = ftKp_Init_OnDeath,
    [FTKIND_YOSHI] = ftYs_Init_OnDeath,
    [FTKIND_FOX] = ftFx_Init_OnDeath,
    [FTKIND_CAPTAIN] = ftCa_Init_OnDeath,
    [FTKIND_SEAK] = ftSk_Init_OnDeath,
    [FTKIND_PEACH] = ftPe_Init_OnDeath,
    [FTKIND_PURIN] = ftPr_Init_OnDeath,
    [FTKIND_SAMUS] = ftSs_Init_OnDeath,
    [FTKIND_POPO] = ftPp_Init_OnDeath,
    [FTKIND_NANA] = ftNn_Init_OnDeath,
    [FTKIND_DONKEY] = ftDk_Init_OnDeath,
    [FTKIND_GANON] = ftGn_Init_OnDeath,
    [FTKIND_PIKACHU] = ftPk_Init_OnDeath,
    [FTKIND_PICHU] = ftPc_Init_OnDeath,
    [FTKIND_NESS] = ftNs_Init_OnDeath,
    [FTKIND_LINK] = ftLk_Init_OnDeath,
    [FTKIND_CLINK] = ftCl_Init_OnDeath,
    [FTKIND_LUIGI] = ftLg_Init_OnDeath,
    [FTKIND_MARIO] = ftMr_Init_OnDeath,
    [FTKIND_DRMARIO] = ftDr_Init_OnDeath,
    [FTKIND_MARS] = ftMs_Init_OnDeath,
    [FTKIND_EMBLEM] = ftFe_Init_OnDeath,
    [FTKIND_ZELDA] = ftZd_Init_OnDeath,
    [FTKIND_FALCO] = ftFc_Init_OnDeath,
};
HSD_GObjEvent ftData_OnUserDataRemove[FTKIND_MAX] = {
    [FTKIND_PURIN] = ftPr_Init_OnUserDataRemove,
};
MotionState* ftData_CharacterStateTables[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_MotionStateTable,
    [FTKIND_MEWTWO] = ftMt_Init_MotionStateTable,
    [FTKIND_GAMEWATCH] = ftGw_Init_MotionStateTable,
    [FTKIND_KOOPA] = ftKp_Init_MotionStateTable,
    [FTKIND_YOSHI] = ftYs_Init_MotionStateTable,
    [FTKIND_FOX] = ftFx_Init_MotionStateTable,
    [FTKIND_CAPTAIN] = ftCa_Init_MotionStateTable,
    [FTKIND_SEAK] = ftSk_Init_MotionStateTable,
    [FTKIND_PEACH] = ftPe_Init_MotionStateTable,
    [FTKIND_PURIN] = ftPr_Init_MotionStateTable,
    [FTKIND_SAMUS] = ftSs_Init_MotionStateTable,
    [FTKIND_POPO] = ftPp_Init_MotionStateTable,
    [FTKIND_NANA] = ftNn_Init_MotionStateTable,
    [FTKIND_DONKEY] = ftDk_Init_MotionStateTable,
    [FTKIND_GANON] = ftGn_Init_MotionStateTable,
    [FTKIND_PIKACHU] = ftPk_Init_MotionStateTable,
    [FTKIND_PICHU] = ftPc_Init_MotionStateTable,
    [FTKIND_NESS] = ftNs_Init_MotionStateTable,
    [FTKIND_LINK] = ftLk_Init_MotionStateTable,
    [FTKIND_CLINK] = ftCl_Init_MotionStateTable,
    [FTKIND_LUIGI] = ftLg_Init_MotionStateTable,
    [FTKIND_MARIO] = ftMr_Init_MotionStateTable,
    [FTKIND_DRMARIO] = ftDr_Init_MotionStateTable,
    [FTKIND_MARS] = ftMs_Init_MotionStateTable,
    [FTKIND_EMBLEM] = ftFe_Init_MotionStateTable,
    [FTKIND_ZELDA] = ftZd_Init_MotionStateTable,
    [FTKIND_FALCO] = ftFc_Init_MotionStateTable,
};
MotionState* ftData_UnkMotionStates0[FTKIND_MAX] = {
    [FTKIND_LUIGI] = ftLg_Init_UnkMotionStates0,
    [FTKIND_MARIO] = ftMr_Init_UnkMotionStates0,
};

// Direct admitted-character rows from the source registries below. Keeping the
// indexed source shape is important: common action code owns these dispatches,
// while the domain projection merely prevents unsupported rows from retaining
// every other fighter implementation.
HSD_GObjEvent ftData_SpecialS[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_SpecialS_Enter,
    [FTKIND_MEWTWO] = ftMt_SpecialS_Enter,
    [FTKIND_GAMEWATCH] = ftGw_SpecialS_Enter,
    [FTKIND_KOOPA] = ftKp_SpecialS_Enter,
    [FTKIND_YOSHI] = ftYs_SpecialS_Enter,
    [FTKIND_FOX] = ftFx_SpecialSStart_Enter,
    [FTKIND_CAPTAIN] = ftCa_SpecialS_Enter,
    [FTKIND_SEAK] = ftSk_SpecialS_Enter,
    [FTKIND_PEACH] = ftPe_SpecialS_Enter,
    [FTKIND_PURIN] = ftPr_SpecialS_Enter,
    [FTKIND_SAMUS] = ftSs_SpecialS_Enter,
    [FTKIND_POPO] = ftPp_SpecialS_Enter,
    [FTKIND_DONKEY] = ftDk_SpecialS_Enter,
    [FTKIND_GANON] = ftCa_SpecialS_Enter,
    [FTKIND_PIKACHU] = ftPk_SpecialS_Enter,
    [FTKIND_PICHU] = ftPk_SpecialS_Enter,
    [FTKIND_NESS] = ftNs_SpecialS_Enter,
    [FTKIND_LINK] = ftLk_SpecialS_Enter,
    [FTKIND_CLINK] = ftLk_SpecialS_Enter,
    [FTKIND_LUIGI] = ftLg_SpecialS_Enter,
    [FTKIND_MARIO] = ftMr_SpecialS_Enter,
    [FTKIND_DRMARIO] = ftMr_SpecialS_Enter,
    [FTKIND_MARS] = ftMs_SpecialS_Enter,
    [FTKIND_EMBLEM] = ftMs_SpecialS_Enter,
    [FTKIND_ZELDA] = ftZd_SpecialS_Enter,
    [FTKIND_FALCO] = ftFx_SpecialSStart_Enter,
};
HSD_GObjEvent ftData_SpecialAirHi[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_SpecialAirHi_Enter,
    [FTKIND_MEWTWO] = ftMt_SpecialAirHiStart_Enter,
    [FTKIND_GAMEWATCH] = ftGw_SpecialAirHi_Enter,
    [FTKIND_KOOPA] = ftKp_SpecialAirHi_Enter,
    [FTKIND_YOSHI] = ftYs_SpecialAirHi_Enter,
    [FTKIND_FOX] = ftFx_SpecialAirHiStart_Enter,
    [FTKIND_CAPTAIN] = ftCa_SpecialAirHi_Enter,
    [FTKIND_SEAK] = ftSk_SpecialAirHi_Enter,
    [FTKIND_PEACH] = ftPe_SpecialAirHi_Enter,
    [FTKIND_PURIN] = ftPr_SpecialAirHi_Enter,
    [FTKIND_SAMUS] = ftSs_SpecialAirHi_Enter,
    [FTKIND_POPO] = ftPp_SpecialAirHi_Enter,
    [FTKIND_DONKEY] = ftDk_SpecialAirHi_Enter,
    [FTKIND_GANON] = ftCa_SpecialAirHi_Enter,
    [FTKIND_PIKACHU] = ftPk_SpecialAirHi_Enter,
    [FTKIND_PICHU] = ftPk_SpecialAirHi_Enter,
    [FTKIND_NESS] = ftNs_SpecialAirHiStart_Enter,
    [FTKIND_LINK] = ftLk_SpecialAirHi_Enter,
    [FTKIND_CLINK] = ftLk_SpecialAirHi_Enter,
    [FTKIND_LUIGI] = ftLg_SpecialAirHi_Enter,
    [FTKIND_MARIO] = ftMr_SpecialAirHi_Enter,
    [FTKIND_DRMARIO] = ftMr_SpecialAirHi_Enter,
    [FTKIND_MARS] = ftMs_SpecialAirHi_Enter,
    [FTKIND_EMBLEM] = ftMs_SpecialAirHi_Enter,
    [FTKIND_ZELDA] = ftZd_SpecialAirHi_Enter,
    [FTKIND_FALCO] = ftFx_SpecialAirHiStart_Enter,
};
HSD_GObjEvent ftData_SpecialAirLw[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_SpecialAirLw_Enter,
    [FTKIND_MEWTWO] = ftMt_SpecialAirLw_Enter,
    [FTKIND_GAMEWATCH] = ftGw_SpecialAirLw_Enter,
    [FTKIND_KOOPA] = ftKp_SpecialAirLw_Enter,
    [FTKIND_YOSHI] = ftYs_SpecialAirLw_Enter,
    [FTKIND_FOX] = ftFx_SpecialAirLw_Enter,
    [FTKIND_CAPTAIN] = ftCa_SpecialAirLw_Enter,
    [FTKIND_SEAK] = ftSk_SpecialAirLw_Enter,
    [FTKIND_PEACH] = ftPe_SpecialAirLw_Enter,
    [FTKIND_PURIN] = ftPr_SpecialAirLw_Enter,
    [FTKIND_SAMUS] = ftSs_SpecialAirLw_Enter,
    [FTKIND_POPO] = ftPp_SpecialAirLw_Enter,
    [FTKIND_NANA] = ftPp_SpecialAirLw_Enter,
    [FTKIND_GANON] = ftCa_SpecialAirLw_Enter,
    [FTKIND_PIKACHU] = ftPk_SpecialAirLw_Enter,
    [FTKIND_PICHU] = ftPk_SpecialAirLw_Enter,
    [FTKIND_NESS] = ftNs_SpecialAirLwStart_Enter,
    [FTKIND_LINK] = ftLk_SpecialAirLw_Enter,
    [FTKIND_CLINK] = ftLk_SpecialAirLw_Enter,
    [FTKIND_LUIGI] = ftLg_SpecialAirLw_Enter,
    [FTKIND_MARIO] = ftMr_SpecialAirLw_Enter,
    [FTKIND_DRMARIO] = ftMr_SpecialAirLw_Enter,
    [FTKIND_MARS] = ftMs_SpecialAirLw_Enter,
    [FTKIND_EMBLEM] = ftMs_SpecialAirLw_Enter,
    [FTKIND_ZELDA] = ftZd_SpecialAirLw_Enter,
    [FTKIND_FALCO] = ftFx_SpecialAirLw_Enter,
};
HSD_GObjEvent ftData_SpecialAirS[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_SpecialAirS_Enter,
    [FTKIND_MEWTWO] = ftMt_SpecialAirS_Enter,
    [FTKIND_GAMEWATCH] = ftGw_SpecialAirS_Enter,
    [FTKIND_KOOPA] = ftKp_SpecialAirS_Enter,
    [FTKIND_YOSHI] = ftYs_SpecialAirS_Enter,
    [FTKIND_FOX] = ftFx_SpecialAirSStart_Enter,
    [FTKIND_CAPTAIN] = ftCa_SpecialAirS_Enter,
    [FTKIND_SEAK] = ftSk_SpecialAirS_Enter,
    [FTKIND_PEACH] = ftPe_SpecialAirS_Enter,
    [FTKIND_PURIN] = ftPr_SpecialAirS_Enter,
    [FTKIND_SAMUS] = ftSs_SpecialAirS_Enter,
    [FTKIND_POPO] = ftPp_SpecialAirS_Enter,
    [FTKIND_DONKEY] = ftDk_SpecialAirS_Enter,
    [FTKIND_GANON] = ftCa_SpecialAirS_Enter,
    [FTKIND_PIKACHU] = ftPk_SpecialAirS_Enter,
    [FTKIND_PICHU] = ftPk_SpecialAirS_Enter,
    [FTKIND_NESS] = ftNs_SpecialAirS_Enter,
    [FTKIND_LINK] = ftLk_SpecialAirS_Enter,
    [FTKIND_CLINK] = ftLk_SpecialAirS_Enter,
    [FTKIND_LUIGI] = ftLg_SpecialAirS_Enter,
    [FTKIND_MARIO] = ftMr_SpecialAirS_Enter,
    [FTKIND_DRMARIO] = ftMr_SpecialAirS_Enter,
    [FTKIND_MARS] = ftMs_SpecialAirS_Enter,
    [FTKIND_EMBLEM] = ftMs_SpecialAirS_Enter,
    [FTKIND_ZELDA] = ftZd_SpecialAirS_Enter,
    [FTKIND_FALCO] = ftFx_SpecialAirSStart_Enter,
};
HSD_GObjEvent ftData_SpecialAirN[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_SpecialAirN_Enter,
    [FTKIND_MEWTWO] = ftMt_SpecialAirN_Enter,
    [FTKIND_GAMEWATCH] = ftGw_SpecialAirN_Enter,
    [FTKIND_KOOPA] = ftKp_SpecialAirN_Enter,
    [FTKIND_YOSHI] = ftYs_SpecialAirN_Enter,
    [FTKIND_FOX] = ftFx_SpecialAirN_Enter,
    [FTKIND_CAPTAIN] = ftCa_SpecialAirN_Enter,
    [FTKIND_SEAK] = ftSk_SpecialAirN_Enter,
    [FTKIND_PEACH] = ftPe_SpecialAirN_Enter,
    [FTKIND_PURIN] = ftPr_SpecialAirN_Enter,
    [FTKIND_SAMUS] = ftSs_SpecialAirN_Enter,
    [FTKIND_POPO] = ftPp_SpecialAirN_Enter,
    [FTKIND_NANA] = ftPp_SpecialAirN_Enter,
    [FTKIND_DONKEY] = ftDk_SpecialAirN_Enter,
    [FTKIND_GANON] = ftCa_SpecialAirN_Enter,
    [FTKIND_PIKACHU] = ftPk_SpecialAirN_Enter,
    [FTKIND_PICHU] = ftPk_SpecialAirN_Enter,
    [FTKIND_NESS] = ftNs_SpecialAirNStart_Enter,
    [FTKIND_LINK] = ftLk_SpecialAirN_Enter,
    [FTKIND_CLINK] = ftLk_SpecialAirN_Enter,
    [FTKIND_LUIGI] = ftLg_SpecialAirN_Enter,
    [FTKIND_MARIO] = ftMr_SpecialAirN_Enter,
    [FTKIND_DRMARIO] = ftMr_SpecialAirN_Enter,
    [FTKIND_MARS] = ftMs_SpecialAirN_Enter,
    [FTKIND_EMBLEM] = ftMs_SpecialAirN_Enter,
    [FTKIND_ZELDA] = ftZd_SpecialAirN_Enter,
    [FTKIND_FALCO] = ftFx_SpecialAirN_Enter,
};
HSD_GObjEvent ftData_SpecialN[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_SpecialN_Enter,
    [FTKIND_MEWTWO] = ftMt_SpecialN_Enter,
    [FTKIND_GAMEWATCH] = ftGw_SpecialN_Enter,
    [FTKIND_KOOPA] = ftKp_SpecialN_Enter,
    [FTKIND_YOSHI] = ftYs_SpecialN_Enter,
    [FTKIND_FOX] = ftFx_SpecialN_Enter,
    [FTKIND_CAPTAIN] = ftCa_SpecialN_Enter,
    [FTKIND_SEAK] = ftSk_SpecialN_Enter,
    [FTKIND_PEACH] = ftPe_SpecialN_Enter,
    [FTKIND_PURIN] = ftPr_SpecialN_Enter,
    [FTKIND_SAMUS] = ftSs_SpecialN_Enter,
    [FTKIND_POPO] = ftPp_SpecialN_Enter,
    [FTKIND_NANA] = ftPp_SpecialN_Enter,
    [FTKIND_DONKEY] = ftDk_SpecialN_Enter,
    [FTKIND_GANON] = ftCa_SpecialN_Enter,
    [FTKIND_PIKACHU] = ftPk_SpecialN_Enter,
    [FTKIND_PICHU] = ftPk_SpecialN_Enter,
    [FTKIND_NESS] = ftNs_SpecialNStart_Enter,
    [FTKIND_LINK] = ftLk_SpecialN_Enter,
    [FTKIND_CLINK] = ftLk_SpecialN_Enter,
    [FTKIND_LUIGI] = ftLg_SpecialN_Enter,
    [FTKIND_MARIO] = ftMr_SpecialN_Enter,
    [FTKIND_DRMARIO] = ftMr_SpecialN_Enter,
    [FTKIND_MARS] = ftMs_SpecialN_Enter,
    [FTKIND_EMBLEM] = ftMs_SpecialN_Enter,
    [FTKIND_ZELDA] = ftZd_SpecialN_Enter,
    [FTKIND_FALCO] = ftFx_SpecialN_Enter,
};
HSD_GObjEvent ftData_SpecialLw[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_SpecialLw_Enter,
    [FTKIND_MEWTWO] = ftMt_SpecialLw_Enter,
    [FTKIND_GAMEWATCH] = ftGw_SpecialLw_Enter,
    [FTKIND_KOOPA] = ftKp_SpecialLw_Enter,
    [FTKIND_YOSHI] = ftYs_SpecialLw_Enter,
    [FTKIND_FOX] = ftFx_SpecialLw_Enter,
    [FTKIND_CAPTAIN] = ftCa_SpecialLw_Enter,
    [FTKIND_SEAK] = ftSk_SpecialLw_Enter,
    [FTKIND_PEACH] = ftPe_SpecialLw_Enter,
    [FTKIND_PURIN] = ftPr_SpecialLw_Enter,
    [FTKIND_SAMUS] = ftSs_SpecialLw_Enter,
    [FTKIND_POPO] = ftPp_SpecialLw_Enter,
    [FTKIND_NANA] = ftPp_SpecialLw_Enter,
    [FTKIND_DONKEY] = ftDk_SpecialLw_Enter,
    [FTKIND_GANON] = ftCa_SpecialLw_Enter,
    [FTKIND_PIKACHU] = ftPk_SpecialLw_Enter,
    [FTKIND_PICHU] = ftPk_SpecialLw_Enter,
    [FTKIND_NESS] = ftNs_SpecialLwStart_Enter,
    [FTKIND_LINK] = ftLk_SpecialLw_Enter,
    [FTKIND_CLINK] = ftLk_SpecialLw_Enter,
    [FTKIND_LUIGI] = ftLg_SpecialLw_Enter,
    [FTKIND_MARIO] = ftMr_SpecialLw_Enter,
    [FTKIND_DRMARIO] = ftMr_SpecialLw_Enter,
    [FTKIND_MARS] = ftMs_SpecialLw_Enter,
    [FTKIND_EMBLEM] = ftMs_SpecialLw_Enter,
    [FTKIND_ZELDA] = ftZd_SpecialLw_Enter,
    [FTKIND_FALCO] = ftFx_SpecialLw_Enter,
};
HSD_GObjEvent ftData_SpecialHi[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_SpecialHi_Enter,
    [FTKIND_MEWTWO] = ftMt_SpecialHiStart_Enter,
    [FTKIND_GAMEWATCH] = ftGw_SpecialHi_Enter,
    [FTKIND_KOOPA] = ftKp_SpecialHi_Enter,
    [FTKIND_YOSHI] = ftYs_SpecialHi_Enter,
    [FTKIND_FOX] = ftFx_SpecialHi_Enter,
    [FTKIND_CAPTAIN] = ftCa_SpecialHi_Enter,
    [FTKIND_SEAK] = ftSk_SpecialHi_Enter,
    [FTKIND_PEACH] = ftPe_SpecialHi_Enter,
    [FTKIND_PURIN] = ftPr_SpecialHi_Enter,
    [FTKIND_SAMUS] = ftSs_SpecialHi_Enter,
    [FTKIND_POPO] = ftPp_SpecialHi_Enter,
    [FTKIND_DONKEY] = ftDk_SpecialHi_Enter,
    [FTKIND_GANON] = ftCa_SpecialHi_Enter,
    [FTKIND_PIKACHU] = ftPk_SpecialHi_Enter,
    [FTKIND_PICHU] = ftPk_SpecialHi_Enter,
    [FTKIND_NESS] = ftNs_SpecialHiStart_Enter,
    [FTKIND_LINK] = ftLk_SpecialHi_Enter,
    [FTKIND_CLINK] = ftLk_SpecialHi_Enter,
    [FTKIND_LUIGI] = ftLg_SpecialHi_Enter,
    [FTKIND_MARIO] = ftMr_SpecialHi_Enter,
    [FTKIND_DRMARIO] = ftMr_SpecialHi_Enter,
    [FTKIND_MARS] = ftMs_SpecialHi_Enter,
    [FTKIND_EMBLEM] = ftMs_SpecialHi_Enter,
    [FTKIND_ZELDA] = ftZd_SpecialHi_Enter,
    [FTKIND_FALCO] = ftFx_SpecialHi_Enter,
};
HSD_GObjEvent ftData_OnAbsorb[FTKIND_MAX] = {
    [FTKIND_GAMEWATCH] = ftGw_Init_OnAbsorb,
    [FTKIND_NESS] = ftNs_Init_OnAbsorb,
};
Fighter_ItemEvent ftData_OnItemPickupExt[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_OnItemPickup,
    [FTKIND_MEWTWO] = ftMt_Init_OnItemPickup,
    [FTKIND_GAMEWATCH] = ftGw_Init_OnItemPickup,
    [FTKIND_KOOPA] = ftKp_Init_OnItemPickup,
    [FTKIND_YOSHI] = ftYs_Init_OnItemPickup,
    [FTKIND_FOX] = ftFx_Init_OnItemPickup,
    [FTKIND_CAPTAIN] = ftCa_Init_OnItemPickup,
    [FTKIND_SEAK] = ftSk_Init_OnItemPickup,
    [FTKIND_PEACH] = ftPe_Init_OnItemPickup,
    [FTKIND_PURIN] = ftPr_Init_OnItemPickup,
    [FTKIND_SAMUS] = ftSs_Init_OnItemPickup,
    [FTKIND_POPO] = ftPp_Init_OnItemPickup,
    [FTKIND_NANA] = ftPp_Init_OnItemPickup,
    [FTKIND_DONKEY] = ftDk_Init_OnItemPickup,
    [FTKIND_GANON] = ftGn_Init_OnItemPickup,
    [FTKIND_PIKACHU] = ftPk_Init_OnItemPickup,
    [FTKIND_PICHU] = ftPc_Init_OnItemPickup,
    [FTKIND_NESS] = ftNs_Init_OnItemPickup,
    [FTKIND_LINK] = ftLk_Init_OnItemPickupExt,
    [FTKIND_CLINK] = ftCl_Init_OnItemPickupExt,
    [FTKIND_MARS] = ftMs_Init_OnItemPickup,
    [FTKIND_LUIGI] = ftLg_Init_OnItemPickup,
    [FTKIND_MARIO] = ftMr_Init_OnItemPickup,
    [FTKIND_DRMARIO] = ftDr_Init_OnItemPickup,
    [FTKIND_MARS] = ftMs_Init_OnItemPickup,
    [FTKIND_EMBLEM] = ftFe_Init_OnItemPickup,
    [FTKIND_ZELDA] = ftZd_Init_OnItemPickup,
    [FTKIND_FALCO] = ftFc_Init_OnItemPickup,
};
HSD_GObjEvent ftData_OnItemInvisible[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_OnItemInvisible,
    [FTKIND_MEWTWO] = ftMt_Init_OnItemInvisible,
    [FTKIND_GAMEWATCH] = ftGw_Init_OnItemInvisible,
    [FTKIND_KOOPA] = ftKp_Init_OnItemInvisible,
    [FTKIND_YOSHI] = ftYs_Init_OnItemInvisible,
    [FTKIND_FOX] = ftFx_Init_OnItemInvisible,
    [FTKIND_CAPTAIN] = ftCa_Init_OnItemInvisible,
    [FTKIND_SEAK] = ftSk_Init_OnItemInvisible,
    [FTKIND_PEACH] = ftPe_Init_OnItemInvisible,
    [FTKIND_PURIN] = ftPr_Init_OnItemInvisible,
    [FTKIND_SAMUS] = ftSs_Init_OnItemInvisible,
    [FTKIND_POPO] = ftPp_Init_OnItemInvisible,
    [FTKIND_NANA] = ftPp_Init_OnItemInvisible,
    [FTKIND_DONKEY] = ftDk_Init_OnItemInvisible,
    [FTKIND_GANON] = ftGn_Init_OnItemInvisible,
    [FTKIND_PIKACHU] = ftPk_Init_OnItemInvisible,
    [FTKIND_PICHU] = ftPc_Init_OnItemInvisible,
    [FTKIND_NESS] = ftNs_Init_OnItemInvisible,
    [FTKIND_LINK] = ftLk_Init_OnItemInvisible,
    [FTKIND_CLINK] = ftCl_Init_OnItemInvisible,
    [FTKIND_LUIGI] = ftLg_Init_OnItemInvisible,
    [FTKIND_MARIO] = ftMr_Init_OnItemInvisible,
    [FTKIND_DRMARIO] = ftDr_Init_OnItemInvisible,
    [FTKIND_MARS] = ftMs_Init_OnItemInvisible,
    [FTKIND_EMBLEM] = ftFe_Init_OnItemInvisible,
    [FTKIND_ZELDA] = ftZd_Init_OnItemInvisible,
    [FTKIND_FALCO] = ftFc_Init_OnItemInvisible,
};
HSD_GObjEvent ftData_OnItemVisible[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_OnItemVisible,
    [FTKIND_MEWTWO] = ftMt_Init_OnItemVisible,
    [FTKIND_GAMEWATCH] = ftGw_Init_OnItemVisible,
    [FTKIND_KOOPA] = ftKp_Init_OnItemVisible,
    [FTKIND_YOSHI] = ftYs_Init_OnItemVisible,
    [FTKIND_FOX] = ftFx_Init_OnItemVisible,
    [FTKIND_CAPTAIN] = ftCa_Init_OnItemVisible,
    [FTKIND_SEAK] = ftSk_Init_OnItemVisible,
    [FTKIND_PEACH] = ftPe_Init_OnItemVisible,
    [FTKIND_PURIN] = ftPr_Init_OnItemVisible,
    [FTKIND_SAMUS] = ftSs_Init_OnItemVisible,
    [FTKIND_POPO] = ftPp_Init_OnItemVisible,
    [FTKIND_NANA] = ftPp_Init_OnItemVisible,
    [FTKIND_DONKEY] = ftDk_Init_OnItemVisible,
    [FTKIND_GANON] = ftGn_Init_OnItemVisible,
    [FTKIND_PIKACHU] = ftPk_Init_OnItemVisible,
    [FTKIND_PICHU] = ftPc_Init_OnItemVisible,
    [FTKIND_NESS] = ftNs_Init_OnItemVisible,
    [FTKIND_LINK] = ftLk_Init_OnItemVisible,
    [FTKIND_CLINK] = ftCl_Init_OnItemVisible,
    [FTKIND_LUIGI] = ftLg_Init_OnItemVisible,
    [FTKIND_MARIO] = ftMr_Init_OnItemVisible,
    [FTKIND_DRMARIO] = ftDr_Init_OnItemVisible,
    [FTKIND_MARS] = ftMs_Init_OnItemVisible,
    [FTKIND_EMBLEM] = ftFe_Init_OnItemVisible,
    [FTKIND_ZELDA] = ftZd_Init_OnItemVisible,
    [FTKIND_FALCO] = ftFc_Init_OnItemVisible,
};
Fighter_ItemEvent ftData_OnItemDropExt[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_OnItemDrop,
    [FTKIND_MEWTWO] = ftMt_Init_OnItemDrop,
    [FTKIND_GAMEWATCH] = ftGw_Init_OnItemDrop,
    [FTKIND_KOOPA] = ftKp_Init_OnItemDrop,
    [FTKIND_YOSHI] = ftYs_Init_OnItemDrop,
    [FTKIND_FOX] = ftFx_Init_OnItemDrop,
    [FTKIND_CAPTAIN] = ftCa_Init_OnItemDrop,
    [FTKIND_SEAK] = ftSk_Init_OnItemDrop,
    [FTKIND_PEACH] = ftPe_Init_OnItemDrop,
    [FTKIND_PURIN] = ftPr_Init_OnItemDrop,
    [FTKIND_SAMUS] = ftSs_Init_OnItemDrop,
    [FTKIND_POPO] = ftPp_Init_OnItemDrop,
    [FTKIND_NANA] = ftPp_Init_OnItemDrop,
    [FTKIND_DONKEY] = ftDk_Init_OnItemDrop,
    [FTKIND_GANON] = ftGn_Init_OnItemDrop,
    [FTKIND_PIKACHU] = ftPk_Init_OnItemDrop,
    [FTKIND_PICHU] = ftPc_Init_OnItemDrop,
    [FTKIND_NESS] = ftNs_Init_OnItemDrop,
    [FTKIND_LINK] = ftLk_Init_OnItemDropExt,
    [FTKIND_CLINK] = ftCl_Init_OnItemDropExt,
    [FTKIND_MARS] = ftMs_Init_OnItemDrop,
    [FTKIND_LUIGI] = ftLg_Init_OnItemDrop,
    [FTKIND_MARIO] = ftMr_Init_OnItemDrop,
    [FTKIND_DRMARIO] = ftDr_Init_OnItemDrop,
    [FTKIND_MARS] = ftMs_Init_OnItemDrop,
    [FTKIND_EMBLEM] = ftFe_Init_OnItemDrop,
    [FTKIND_ZELDA] = ftZd_Init_OnItemDrop,
    [FTKIND_FALCO] = ftFc_Init_OnItemDrop,
};
Fighter_ItemEvent ftData_OnItemPickup[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_OnItemPickup,
    [FTKIND_MEWTWO] = ftMt_Init_OnItemPickup,
    [FTKIND_GAMEWATCH] = ftGw_Init_OnItemPickup,
    [FTKIND_KOOPA] = ftKp_Init_OnItemPickup,
    [FTKIND_YOSHI] = ftYs_Init_OnItemPickup,
    [FTKIND_FOX] = ftFx_Init_OnItemPickup,
    [FTKIND_CAPTAIN] = ftCa_Init_OnItemPickup,
    [FTKIND_SEAK] = ftSk_Init_OnItemPickup,
    [FTKIND_PEACH] = ftPe_Init_OnItemPickup,
    [FTKIND_PURIN] = ftPr_Init_OnItemPickup,
    [FTKIND_SAMUS] = ftSs_Init_OnItemPickup,
    [FTKIND_POPO] = ftPp_Init_OnItemPickup,
    [FTKIND_NANA] = ftPp_Init_OnItemPickup,
    [FTKIND_DONKEY] = ftDk_Init_OnItemPickup,
    [FTKIND_GANON] = ftGn_Init_OnItemPickup,
    [FTKIND_PIKACHU] = ftPk_Init_OnItemPickup,
    [FTKIND_PICHU] = ftPc_Init_OnItemPickup,
    [FTKIND_LINK] = ftLk_Init_OnItemPickup,
    [FTKIND_CLINK] = ftCl_Init_OnItemPickup,
    [FTKIND_LUIGI] = ftLg_Init_OnItemPickup,
    [FTKIND_MARIO] = ftMr_Init_OnItemPickup,
    [FTKIND_DRMARIO] = ftDr_Init_OnItemPickup,
    [FTKIND_MARS] = ftMs_Init_OnItemPickup,
    [FTKIND_EMBLEM] = ftFe_Init_OnItemPickup,
    [FTKIND_ZELDA] = ftZd_Init_OnItemPickup,
    [FTKIND_FALCO] = ftFc_Init_OnItemPickup,
};
Fighter_ItemEvent ftData_OnItemDrop[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_OnItemDrop,
    [FTKIND_MEWTWO] = ftMt_Init_OnItemDrop,
    [FTKIND_GAMEWATCH] = ftGw_Init_OnItemDrop,
    [FTKIND_KOOPA] = ftKp_Init_OnItemDrop,
    [FTKIND_YOSHI] = ftYs_Init_OnItemDrop,
    [FTKIND_FOX] = ftFx_Init_OnItemDrop,
    [FTKIND_CAPTAIN] = ftCa_Init_OnItemDrop,
    [FTKIND_SEAK] = ftSk_Init_OnItemDrop,
    [FTKIND_PEACH] = ftPe_Init_OnItemDrop,
    [FTKIND_PURIN] = ftPr_Init_OnItemDrop,
    [FTKIND_SAMUS] = ftSs_Init_OnItemDrop,
    [FTKIND_POPO] = ftPp_Init_OnItemDrop,
    [FTKIND_NANA] = ftPp_Init_OnItemDrop,
    [FTKIND_DONKEY] = ftDk_Init_OnItemDrop,
    [FTKIND_GANON] = ftGn_Init_OnItemDrop,
    [FTKIND_PIKACHU] = ftPk_Init_OnItemDrop,
    [FTKIND_PICHU] = ftPc_Init_OnItemDrop,
    [FTKIND_LINK] = ftLk_Init_OnItemDrop,
    [FTKIND_CLINK] = ftCl_Init_OnItemDrop,
    [FTKIND_LUIGI] = ftLg_Init_OnItemDrop,
    [FTKIND_MARIO] = ftMr_Init_OnItemDrop,
    [FTKIND_DRMARIO] = ftDr_Init_OnItemDrop,
    [FTKIND_MARS] = ftMs_Init_OnItemDrop,
    [FTKIND_EMBLEM] = ftFe_Init_OnItemDrop,
    [FTKIND_ZELDA] = ftZd_Init_OnItemDrop,
    [FTKIND_FALCO] = ftFc_Init_OnItemDrop,
};
HSD_GObjEvent ftData_UnkMotionStates1[FTKIND_MAX] = {
    [FTKIND_PIKACHU] = ftPk_Init_UnkMotionStates1,
};
HSD_GObjEvent ftData_UnkMotionStates2[FTKIND_MAX] = {
    [FTKIND_PIKACHU] = ftPk_Init_UnkMotionStates2,
};
HSD_GObjEvent ftData_OnKnockbackEnter[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_OnKnockbackEnter,
    [FTKIND_MEWTWO] = ftMt_Init_OnKnockbackEnter,
    [FTKIND_KOOPA] = ftKp_Init_OnKnockbackEnter,
    [FTKIND_YOSHI] = ftYs_Init_OnKnockbackEnter,
    [FTKIND_FOX] = ftFx_Init_OnKnockbackEnter,
    [FTKIND_SEAK] = ftSk_Init_OnKnockbackEnter,
    [FTKIND_PEACH] = ftPe_Init_OnKnockbackEnter,
    [FTKIND_PURIN] = ftPr_Init_OnKnockbackEnter,
    [FTKIND_POPO] = ftPp_Init_OnKnockbackEnter,
    [FTKIND_NANA] = ftPp_Init_OnKnockbackEnter,
    [FTKIND_DONKEY] = ftDk_Init_OnKnockbackEnter,
    [FTKIND_GANON] = ftGn_Init_OnKnockbackEnter,
    [FTKIND_PIKACHU] = ftPk_Init_OnKnockbackEnter,
    [FTKIND_PICHU] = ftPc_Init_OnKnockbackEnter,
    [FTKIND_NESS] = ftNs_Init_OnKnockbackEnter,
    [FTKIND_LINK] = ftLk_Init_OnKnockbackEnter,
    [FTKIND_CLINK] = ftCl_Init_OnKnockbackEnter,
    [FTKIND_LUIGI] = ftLg_Init_OnKnockbackEnter,
    [FTKIND_MARIO] = ftMr_Init_OnKnockbackEnter,
    [FTKIND_DRMARIO] = ftDr_Init_OnKnockbackEnter,
    [FTKIND_MARS] = ftMs_Init_OnKnockbackEnter,
    [FTKIND_EMBLEM] = ftFe_Init_OnKnockbackEnter,
    [FTKIND_ZELDA] = ftZd_Init_OnKnockbackEnter,
    [FTKIND_FALCO] = ftFc_Init_OnKnockbackEnter,
};
HSD_GObjEvent ftData_OnKnockbackExit[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_OnKnockbackExit,
    [FTKIND_MEWTWO] = ftMt_Init_OnKnockbackExit,
    [FTKIND_KOOPA] = ftKp_Init_OnKnockbackExit,
    [FTKIND_YOSHI] = ftYs_Init_OnKnockbackExit,
    [FTKIND_FOX] = ftFx_Init_OnKnockbackExit,
    [FTKIND_SEAK] = ftSk_Init_OnKnockbackExit,
    [FTKIND_PEACH] = ftPe_Init_OnKnockbackExit,
    [FTKIND_PURIN] = ftPr_Init_OnKnockbackExit,
    [FTKIND_POPO] = ftPp_Init_OnKnockbackExit,
    [FTKIND_NANA] = ftPp_Init_OnKnockbackExit,
    [FTKIND_DONKEY] = ftDk_Init_OnKnockbackExit,
    [FTKIND_GANON] = ftGn_Init_OnKnockbackExit,
    [FTKIND_PIKACHU] = ftPk_Init_OnKnockbackExit,
    [FTKIND_PICHU] = ftPc_Init_OnKnockbackExit,
    [FTKIND_NESS] = ftNs_Init_OnKnockbackExit,
    [FTKIND_LINK] = ftLk_Init_OnKnockbackExit,
    [FTKIND_CLINK] = ftCl_Init_OnKnockbackExit,
    [FTKIND_LUIGI] = ftLg_Init_OnKnockbackExit,
    [FTKIND_MARIO] = ftMr_Init_OnKnockbackExit,
    [FTKIND_DRMARIO] = ftDr_Init_OnKnockbackExit,
    [FTKIND_MARS] = ftMs_Init_OnKnockbackExit,
    [FTKIND_EMBLEM] = ftFe_Init_OnKnockbackExit,
    [FTKIND_ZELDA] = ftZd_Init_OnKnockbackExit,
    [FTKIND_FALCO] = ftFc_Init_OnKnockbackExit,
};
HSD_GObjEvent ftData_UnkMotionStates3[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_UnkMotionStates3,
    [FTKIND_KOOPA] = ftKp_Init_UnkMotionStates3,
};
HSD_GObjEvent ftData_UnkMotionStates4[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_UnkMotionStates4,
    [FTKIND_MEWTWO] = ftMt_Init_UnkMotionStates4,
    [FTKIND_GAMEWATCH] = ftGw_Init_UnkMotionStates4,
    [FTKIND_DONKEY] = ftDk_Init_UnkMotionStates4,
    [FTKIND_SEAK] = ftSk_Init_UnkMotionStates4,
    [FTKIND_SAMUS] = ftSs_Init_UnkMotionStates4,
};
HSD_GObjEvent ftKindCalcIndiviParamTable[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_LoadSpecialAttrs,
    [FTKIND_MEWTWO] = ftMt_Init_LoadSpecialAttrs,
    [FTKIND_GAMEWATCH] = ftGw_Init_LoadSpecialAttrs,
    [FTKIND_KOOPA] = ftKp_Init_LoadSpecialAttrs,
    [FTKIND_YOSHI] = ftYs_Init_LoadSpecialAttrs,
    [FTKIND_FOX] = ftFx_Init_LoadSpecialAttrs,
    [FTKIND_CAPTAIN] = ftCa_Init_LoadSpecialAttrs,
    [FTKIND_SEAK] = ftSk_Init_LoadSpecialAttrs,
    [FTKIND_PEACH] = ftPe_Init_LoadSpecialAttrs,
    [FTKIND_PURIN] = ftPr_Init_LoadSpecialAttrs,
    [FTKIND_SAMUS] = ftSs_Init_LoadSpecialAttrs,
    [FTKIND_POPO] = ftPp_Init_LoadSpecialAttrs,
    [FTKIND_NANA] = ftNn_Init_LoadSpecialAttrs,
    [FTKIND_DONKEY] = ftDk_Init_LoadSpecialAttrs,
    [FTKIND_GANON] = ftGn_Init_LoadSpecialAttrs,
    [FTKIND_PIKACHU] = ftPk_Init_LoadSpecialAttrs,
    [FTKIND_PICHU] = ftPc_Init_LoadSpecialAttrs,
    [FTKIND_NESS] = ftNs_Init_LoadSpecialAttrs,
    [FTKIND_LINK] = ftLk_Init_LoadSpecialAttrs,
    [FTKIND_CLINK] = ftCl_Init_LoadSpecialAttrs,
    [FTKIND_LUIGI] = ftLg_Init_LoadSpecialAttrs,
    [FTKIND_MARIO] = ftMr_Init_LoadSpecialAttrs,
    [FTKIND_DRMARIO] = ftDr_Init_LoadSpecialAttrs,
    [FTKIND_MARS] = ftMs_Init_LoadSpecialAttrs,
    [FTKIND_EMBLEM] = ftFe_Init_LoadSpecialAttrs,
    [FTKIND_ZELDA] = ftZd_Init_LoadSpecialAttrs,
    [FTKIND_FALCO] = ftFc_Init_LoadSpecialAttrs,
};

struct StringPair {
    char* a;
    char* b;
};
struct StringPair ftData_803C1F40[FTKIND_MAX] = {
    [FTKIND_KIRBY] = { ftKb_Init_DatFilename, ftKb_Init_DataName },
    [FTKIND_MEWTWO] = { ftMt_Init_DatFilename, ftMt_Init_DataName },
    [FTKIND_GAMEWATCH] = { ftGw_Init_DatFilename, ftGw_Init_DataName },
    [FTKIND_KOOPA] = { ftKp_Init_DatFilename, ftKp_Init_DataName },
    [FTKIND_YOSHI] = { ftYs_Init_DatFilename, ftYs_Init_DataName },
    [FTKIND_FOX] = { ftFx_Init_DatFilename, ftFx_Init_DataName },
    [FTKIND_CAPTAIN] = { ftCa_Init_DatFilename, ftCa_Init_DataName },
    [FTKIND_SEAK] = { ftSk_Init_DatFilename, ftSk_Init_DataName },
    [FTKIND_PEACH] = { ftPe_Init_DatFilename, ftPe_Init_DataName },
    [FTKIND_PURIN] = { ftPr_Init_DatFilename, ftPr_Init_DataName },
    [FTKIND_SAMUS] = { ftSs_Init_DatFilename, ftSs_Init_DataName },
    [FTKIND_POPO] = { ftPp_Init_DatFilename, ftPp_Init_DataName },
    [FTKIND_NANA] = { ftNn_Init_DatFilename, ftNn_Init_DataName },
    [FTKIND_DONKEY] = { ftDk_Init_DatFilename, ftDk_Init_DataName },
    [FTKIND_GANON] = { ftGn_Init_DatFilename, ftGn_Init_DataName },
    [FTKIND_PIKACHU] = { ftPk_Init_DatFilename, ftPk_Init_DataName },
    [FTKIND_PICHU] = { ftPc_Init_DatFilename, ftPc_Init_DataName },
    [FTKIND_NESS] = { ftNs_Init_DatFilename, ftNs_Init_DataName },
    [FTKIND_LINK] = { ftLk_Init_DatFilename, ftLk_Init_DataName },
    [FTKIND_CLINK] = { ftCl_Init_DatFilename, ftCl_Init_DataName },
    [FTKIND_LUIGI] = { ftLg_Init_DatFilename, ftLg_Init_DataName },
    [FTKIND_MARIO] = { ftMr_Init_DatFilename, ftMr_Init_DataName },
    [FTKIND_DRMARIO] = { ftDr_Init_DatFilename, ftDr_Init_DataName },
    [FTKIND_MARS] = { ftMs_Init_DatFilename, ftMs_Init_DataName },
    [FTKIND_EMBLEM] = { ftFe_Init_DatFilename, ftFe_Init_DataName },
    [FTKIND_ZELDA] = { ftZd_Init_DatFilename, ftZd_Init_DataName },
    [FTKIND_FALCO] = { ftFc_Init_DatFilename, ftFc_Init_DataName },
};
Event ftData_UnkMotionStates5[FTKIND_MAX];
Fighter_UnkMtxEvent ftData_UnkMtxFunc0[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_UnkMtxFunc0,
    [FTKIND_PURIN] = ftPr_Init_UnkMtxFunc0,
};
ftData_UnkModelStruct ftData_UnkIntBoolFunc0 = {
    .model_events = {
        [FTKIND_KIRBY] = ftKb_UnkIntBoolFunc0,
        [FTKIND_PURIN] = ftPr_Init_UnkIntBoolFunc0,
    },
    .getter = {
        [FTKIND_KIRBY] = ftKb_Init_UnkMotionStates6,
        [FTKIND_PURIN] = ftPr_Init_UnkMotionStates6,
    },
};
struct {
    HSD_GObjEvent x0;
    void (*x4)(Fighter_GObj*, int, float frame);
} ftData_UnkCallbackPairs0[FTKIND_MAX];
Fighter_CostumeStrings* ftData_803C2360[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_CostumeStrings,
    [FTKIND_MEWTWO] = ftMt_Init_CostumeStrings,
    [FTKIND_GAMEWATCH] = ftGw_Init_CostumeStrings,
    [FTKIND_KOOPA] = ftKp_Init_CostumeStrings,
    [FTKIND_YOSHI] = ftYs_Init_CostumeStrings,
    [FTKIND_FOX] = ftFx_Init_CostumeStrings,
    [FTKIND_CAPTAIN] = ftCa_Init_CostumeStrings,
    [FTKIND_SEAK] = ftSk_Init_CostumeStrings,
    [FTKIND_PEACH] = ftPe_Init_CostumeStrings,
    [FTKIND_PURIN] = ftPr_Init_CostumeStrings,
    [FTKIND_SAMUS] = ftSs_Init_CostumeStrings,
    [FTKIND_POPO] = ftPp_Init_CostumeStrings,
    [FTKIND_NANA] = ftNn_Init_CostumeStrings,
    [FTKIND_DONKEY] = ftDk_Init_CostumeStrings,
    [FTKIND_GANON] = ftGn_Init_CostumeStrings,
    [FTKIND_PIKACHU] = ftPk_Init_CostumeStrings,
    [FTKIND_PICHU] = ftPc_Init_CostumeStrings,
    [FTKIND_NESS] = ftNs_Init_CostumeStrings,
    [FTKIND_LINK] = ftLk_Init_CostumeStrings,
    [FTKIND_CLINK] = ftCl_Init_CostumeStrings,
    [FTKIND_LUIGI] = ftLg_Init_CostumeStrings,
    [FTKIND_MARIO] = ftMr_Init_CostumeStrings,
    [FTKIND_DRMARIO] = ftDr_Init_CostumeStrings,
    [FTKIND_MARS] = ftMs_Init_CostumeStrings,
    [FTKIND_EMBLEM] = ftFe_Init_CostumeStrings,
    [FTKIND_ZELDA] = ftZd_Init_CostumeStrings,
    [FTKIND_FALCO] = ftFc_Init_CostumeStrings,
};
char* ftData_803C23E4[FTKIND_MAX] = {
    [FTKIND_KIRBY] = ftKb_Init_AnimDatFilename,
    [FTKIND_MEWTWO] = ftMt_Init_AnimDatFilename,
    [FTKIND_GAMEWATCH] = ftGw_Init_AnimDatFilename,
    [FTKIND_KOOPA] = ftKp_Init_AnimDatFilename,
    [FTKIND_YOSHI] = ftYs_Init_AnimDatFilename,
    [FTKIND_FOX] = ftFx_Init_AnimDatFilename,
    [FTKIND_CAPTAIN] = ftCa_Init_AnimDatFilename,
    [FTKIND_SEAK] = ftSk_Init_AnimDatFilename,
    [FTKIND_PEACH] = ftPe_Init_AnimDatFilename,
    [FTKIND_PURIN] = ftPr_Init_AnimDatFilename,
    [FTKIND_SAMUS] = ftSs_Init_AnimDatFilename,
    [FTKIND_POPO] = ftPp_Init_AnimDatFilename,
    [FTKIND_NANA] = ftNn_Init_AnimDatFilename,
    [FTKIND_DONKEY] = ftDk_Init_AnimDatFilename,
    [FTKIND_GANON] = ftGn_Init_AnimDatFilename,
    [FTKIND_PIKACHU] = ftPk_Init_AnimDatFilename,
    [FTKIND_PICHU] = ftPc_Init_AnimDatFilename,
    [FTKIND_NESS] = ftNs_Init_AnimDatFilename,
    [FTKIND_LINK] = ftLk_Init_AnimDatFilename,
    [FTKIND_CLINK] = ftCl_Init_AnimDatFilename,
    [FTKIND_LUIGI] = ftLg_Init_AnimDatFilename,
    [FTKIND_MARIO] = ftMr_Init_AnimDatFilename,
    [FTKIND_DRMARIO] = ftDr_Init_AnimDatFilename,
    [FTKIND_MARS] = ftMs_Init_AnimDatFilename,
    [FTKIND_EMBLEM] = ftFe_Init_AnimDatFilename,
    [FTKIND_ZELDA] = ftZd_Init_AnimDatFilename,
    [FTKIND_FALCO] = ftFc_Init_AnimDatFilename,
};
Fighter_DemoStrings* ftData_803C2468[FTKIND_MAX] = {
    [FTKIND_KIRBY] = &ftKb_Init_DemoMotionFilenames,
    [FTKIND_MEWTWO] = &ftMt_Init_DemoMotionFilenames,
    [FTKIND_GAMEWATCH] = &ftGw_Init_DemoMotionFilenames,
    [FTKIND_KOOPA] = &ftKp_Init_DemoMotionFilenames,
    [FTKIND_YOSHI] = &ftYs_Init_DemoMotionFilenames,
    [FTKIND_FOX] = &ftFx_Init_DemoMotionFilenames,
    [FTKIND_CAPTAIN] = &ftCa_Init_DemoMotionFilenames,
    [FTKIND_SEAK] = &ftSk_Init_DemoMotionFilenames,
    [FTKIND_PEACH] = &ftPe_Init_DemoMotionFilenames,
    [FTKIND_PURIN] = &ftPr_Init_DemoMotionFilenames,
    [FTKIND_SAMUS] = &ftSs_Init_DemoMotionFilenames,
    [FTKIND_POPO] = &ftPp_Init_DemoMotionFilenames,
    [FTKIND_NANA] = &ftNn_Init_DemoMotionFilenames,
    [FTKIND_DONKEY] = &ftDk_Init_DemoMotionFilenames,
    [FTKIND_GANON] = &ftGn_Init_DemoMotionFilenames,
    [FTKIND_PIKACHU] = &ftPk_Init_DemoMotionFilenames,
    [FTKIND_PICHU] = &ftPc_Init_DemoMotionFilenames,
    [FTKIND_NESS] = &ftNs_Init_DemoMotionFilenames,
    [FTKIND_LINK] = &ftLk_Init_DemoMotionFilenames,
    [FTKIND_CLINK] = &ftCl_Init_DemoMotionFilenames,
    [FTKIND_LUIGI] = &ftLg_Init_DemoMotionFilenames,
    [FTKIND_MARIO] = &ftMr_Init_DemoMotionFilenames,
    [FTKIND_DRMARIO] = &ftDr_Init_DemoMotionFilenames,
    [FTKIND_MARS] = &ftMs_Init_DemoMotionFilenames,
    [FTKIND_EMBLEM] = &ftFe_Init_DemoMotionFilenames,
    [FTKIND_ZELDA] = &ftZd_Init_DemoMotionFilenames,
    [FTKIND_FALCO] = &ftFc_Init_DemoMotionFilenames,
};
Fighter_MotionFileStringGetter ftData_803C24EC[FTKIND_MAX] = {
    [FTKIND_LUIGI] = ftLg_Init_GetMotionFileString,
    [FTKIND_MARIO] = ftMr_Init_GetMotionFileString,
};
Fighter_UnkPtrEvent ftData_UnkDemoCallbacks0[FTKIND_MAX] = {
    [FTKIND_LUIGI] = ftLg_Init_UnkDemoCallbacks0,
    [FTKIND_MARIO] = ftMr_Init_UnkDemoCallbacks0,
};
ftData_UnkCountStruct ftData_UnkIntPairs[FTKIND_MAX] = {
    [FTKIND_KIRBY] = { 0, 18 },
    [FTKIND_MEWTWO] = { 0, 14 },
    [FTKIND_GAMEWATCH] = { 0, 14 },
    [FTKIND_KOOPA] = { 0, 14 },
    [FTKIND_YOSHI] = { 0, 14 },
    [FTKIND_FOX] = { 0, 14 },
    [FTKIND_CAPTAIN] = { 0, 14 },
    [FTKIND_SEAK] = { 0, 14 },
    [FTKIND_PEACH] = { 0, 14 },
    [FTKIND_PURIN] = { 0, 14 },
    [FTKIND_SAMUS] = { 0, 14 },
    [FTKIND_POPO] = { 0, 14 },
    [FTKIND_NANA] = { 0, 14 },
    [FTKIND_DONKEY] = { 0, 14 },
    [FTKIND_GANON] = { 0, 14 },
    [FTKIND_PIKACHU] = { 0, 14 },
    [FTKIND_PICHU] = { 0, 14 },
    [FTKIND_NESS] = { 0, 14 },
    [FTKIND_LINK] = { 0, 14 },
    [FTKIND_CLINK] = { 0, 14 },
    [FTKIND_LUIGI] = { 0, 16 },
    [FTKIND_MARIO] = { 0, 16 },
    [FTKIND_DRMARIO] = { 0, 14 },
    [FTKIND_MARS] = { 0, 14 },
    [FTKIND_EMBLEM] = { 0, 14 },
    [FTKIND_ZELDA] = { 0, 14 },
    [FTKIND_FALCO] = { 0, 14 },
};
u8 ftData_UnkBytePerCharacter[FTKIND_MAX] = {
    [FTKIND_KIRBY] = 5,
    [FTKIND_MEWTWO] = 13,
    [FTKIND_GAMEWATCH] = -1,
    [FTKIND_KOOPA] = 12,
    [FTKIND_YOSHI] = 9,
    [FTKIND_FOX] = 3,
    [FTKIND_CAPTAIN] = 4,
    [FTKIND_SEAK] = 17,
    [FTKIND_PEACH] = 15,
    [FTKIND_PURIN] = 11,
    [FTKIND_SAMUS] = 2,
    [FTKIND_POPO] = 14,
    [FTKIND_NANA] = 14,
    [FTKIND_DONKEY] = 8,
    [FTKIND_GANON] = 19,
    [FTKIND_PIKACHU] = 7,
    [FTKIND_PICHU] = 7,
    [FTKIND_NESS] = 10,
    [FTKIND_LINK] = 6,
    [FTKIND_CLINK] = 6,
    [FTKIND_LUIGI] = 18,
    [FTKIND_MARIO] = 1,
    [FTKIND_DRMARIO] = 1,
    [FTKIND_MARS] = 16,
    [FTKIND_EMBLEM] = 49,
    [FTKIND_ZELDA] = 17,
    [FTKIND_FALCO] = 3,
};
#else
/* 3C0EC0 */ struct UnkCostumeList CostumeListsForeachCharacter[FTKIND_MAX] = {
    { &lbl_804599F0, 5 },       // Mario
    { &ft_80459B28, 4 },        // Fox
    { &ft_80459A98, 6 },        // Captain
    { &ft_80459CA0, 5 },        // Donkey
    { &ft_80459C10, 6 },        // Kirby
    { &ft_8045A090, 4 },        // Koopa
    { &ftLk_Init_803C82EC, 5 }, // Link
    { &ft_80459D18, 5 },        // Seak
    { &ft_80459D90, 4 },        // Ness
    { &ft_80459DF0, 5 },        // Peach
    { &ft_80459E68, 4 },        // Popo
    { &ft_80459EC8, 4 },        // Nana
    { &ft_80459F28, 4 },        // Pikachu
    { &ft_80459F88, 5 },        // Samus
    { &ft_8045A000, 6 },        // Yoshi
    { &ft_8045A1F8, 5 },        // Purin
    { &ft_8045A2D0, 4 },        // Mewtwo
    { &ft_8045A270, 4 },        // Luigi
    { &ft_8045A0F0, 5 },        // Mars
    { &ft_8045A168, 5 },        // Zelda
    { &ft_8045A330, 5 },        // CLink
    { &ft_8045A3A8, 5 },        // DrMario
    { &ft_8045A420, 4 },        // Falco
    { &ft_8045A480, 4 },        // Pichu
    { &ft_8045A4E0, 4 },        // GameWatch
    { &ft_8045A540, 5 },        // Ganon
    { &ft_8045A5B8, 5 },        // Emblem
    { &ft_8045A690, 1 },        // MasterH
    { &ft_8045A6A8, 1 },        // CrezyH
    { &ft_8045A630, 1 },        // Boy
    { &ft_8045A648, 1 },        // Girl
    { &ft_8045A660, 1 },        // GKoops
    { &ft_8045A678, 1 }         // Sandbag
};

ftData_UnkCountStruct ftData_Table_Unk0[FTKIND_MAX] = {
    { 0, 303 }, { 0, 327 }, { 0, 318 }, { 0, 337 }, { 0, 479 }, { 0, 316 },
    { 0, 314 }, { 0, 317 }, { 0, 326 }, { 0, 318 }, { 0, 321 }, { 0, 321 },
    { 0, 320 }, { 0, 313 }, { 0, 314 }, { 0, 327 }, { 0, 314 }, { 0, 312 },
    { 0, 327 }, { 0, 311 }, { 0, 314 }, { 0, 303 }, { 0, 327 }, { 0, 320 },
    { 0, 323 }, { 0, 318 }, { 0, 327 }, { 0, 345 }, { 0, 344 }, { 0, 295 },
    { 0, 295 }, { 0, 316 }, { 0, 296 },
};

Event ftData_Table_Unk1[FTKIND_MAX] = {
    NULL,
    NULL,
    NULL,
    NULL,
    ftKb_Init_800EE528,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftPr_Init_8013C2F8,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

HSD_GObjEvent ftData_OnLoad[FTKIND_MAX] = {
    ftMr_Init_OnLoad, ftFx_Init_OnLoad, ftCa_Init_OnLoad, ftDk_Init_OnLoad,
    ftKb_Init_OnLoad, ftKp_Init_OnLoad, ftLk_Init_OnLoad, ftSk_Init_OnLoad,
    ftNs_Init_OnLoad, ftPe_Init_OnLoad, ftPp_Init_OnLoad, ftNn_Init_OnLoad,
    ftPk_Init_OnLoad, ftSs_Init_OnLoad, ftYs_Init_OnLoad, ftPr_Init_OnLoad,
    ftMt_Init_OnLoad, ftLg_Init_OnLoad, ftMs_Init_OnLoad, ftZd_Init_OnLoad,
    ftCl_Init_OnLoad, ftDr_Init_OnLoad, ftFc_Init_OnLoad, ftPc_Init_OnLoad,
    ftGw_Init_OnLoad, ftGn_Init_OnLoad, ftFe_Init_OnLoad, ftMh_Init_OnLoad,
    ftCh_Init_OnLoad, ftBo_Init_OnLoad, ftGl_Init_OnLoad, ftGk_Init_OnLoad,
    ftSb_Init_OnLoad,
};

HSD_GObjEvent ftData_OnDeath[FTKIND_MAX] = {
    ftMr_Init_OnDeath, ftFx_Init_OnDeath, ftCa_Init_OnDeath, ftDk_Init_OnDeath,
    ftKb_Init_OnDeath, ftKp_Init_OnDeath, ftLk_Init_OnDeath, ftSk_Init_OnDeath,
    ftNs_Init_OnDeath, ftPe_Init_OnDeath, ftPp_Init_OnDeath, ftNn_Init_OnDeath,
    ftPk_Init_OnDeath, ftSs_Init_OnDeath, ftYs_Init_OnDeath, ftPr_Init_OnDeath,
    ftMt_Init_OnDeath, ftLg_Init_OnDeath, ftMs_Init_OnDeath, ftZd_Init_OnDeath,
    ftCl_Init_OnDeath, ftDr_Init_OnDeath, ftFc_Init_OnDeath, ftPc_Init_OnDeath,
    ftGw_Init_OnDeath, ftGn_Init_OnDeath, ftFe_Init_OnDeath, ftMh_Init_OnDeath,
    ftCh_Init_OnDeath, ftBo_Init_OnDeath, ftGl_Init_OnDeath, ftGk_Init_OnDeath,
    ftSb_Init_OnDeath,
};

HSD_GObjEvent ftData_OnUserDataRemove[FTKIND_MAX] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, ftPr_Init_OnUserDataRemove,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL,
};

MotionState* ftData_CharacterStateTables[FTKIND_MAX] = {
    ftMr_Init_MotionStateTable,
    ftFx_Init_MotionStateTable,
    ftCa_Init_MotionStateTable,
    ftDk_Init_MotionStateTable,
    ftKb_Init_MotionStateTable,
    ftKp_Init_MotionStateTable,
    ftLk_Init_MotionStateTable,
    ftSk_Init_MotionStateTable,
    ftNs_Init_MotionStateTable,
    ftPe_Init_MotionStateTable,
    ftPp_Init_MotionStateTable,
    ftNn_Init_MotionStateTable,
    ftPk_Init_MotionStateTable,
    ftSs_Init_MotionStateTable,
    ftYs_Init_MotionStateTable,
    ftPr_Init_MotionStateTable,
    ftMt_Init_MotionStateTable,
    ftLg_Init_MotionStateTable,
    ftMs_Init_MotionStateTable,
    ftZd_Init_MotionStateTable,
    ftCl_Init_MotionStateTable,
    ftDr_Init_MotionStateTable,
    ftFc_Init_MotionStateTable,
    ftPc_Init_MotionStateTable,
    ftGw_Init_MotionStateTable,
    ftGn_Init_MotionStateTable,
    ftFe_Init_MotionStateTable,
    ftMh_Init_MotionStateTable,
    ftCh_Init_MotionStateTable,
    NULL,
    NULL,
    ftGk_Init_MotionStateTable,
    ftSb_Init_MotionStateTable,
};

MotionState* ftData_UnkMotionStates0[FTKIND_MAX] = {
    ftMr_Init_UnkMotionStates0,
    NULL,
    NULL,
    NULL,
    ftKb_Init_UnkMotionStates0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftLg_Init_UnkMotionStates0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftGk_Init_UnkMotionStates0,
    NULL,
};

HSD_GObjEvent ftData_SpecialS[FTKIND_MAX] = {
    ftMr_SpecialS_Enter,
    ftFx_SpecialSStart_Enter,
    ftCa_SpecialS_Enter,
    ftDk_SpecialS_Enter,
    ftKb_SpecialS_Enter,
    ftKp_SpecialS_Enter,
    ftLk_SpecialS_Enter,
    ftSk_SpecialS_Enter,
    ftNs_SpecialS_Enter,
    ftPe_SpecialS_Enter,
    ftPp_SpecialS_Enter,
    NULL,
    ftPk_SpecialS_Enter,
    ftSs_SpecialS_Enter,
    ftYs_SpecialS_Enter,
    ftPr_SpecialS_Enter,
    ftMt_SpecialS_Enter,
    ftLg_SpecialS_Enter,
    ftMs_SpecialS_Enter,
    ftZd_SpecialS_Enter,
    ftLk_SpecialS_Enter,
    ftMr_SpecialS_Enter,
    ftFx_SpecialSStart_Enter,
    ftPk_SpecialS_Enter,
    ftGw_SpecialS_Enter,
    ftCa_SpecialS_Enter,
    ftMs_SpecialS_Enter,
    NULL,
    NULL,
    NULL,
    NULL,
    ftKp_SpecialS_Enter,
    NULL,
};

HSD_GObjEvent ftData_SpecialAirHi[FTKIND_MAX] = {
    ftMr_SpecialAirHi_Enter,
    ftFx_SpecialAirHiStart_Enter,
    ftCa_SpecialAirHi_Enter,
    ftDk_SpecialAirHi_Enter,
    ftKb_SpecialAirHi_Enter,
    ftKp_SpecialAirHi_Enter,
    ftLk_SpecialAirHi_Enter,
    ftSk_SpecialAirHi_Enter,
    ftNs_SpecialAirHiStart_Enter,
    ftPe_SpecialAirHi_Enter,
    ftPp_SpecialAirHi_Enter,
    NULL,
    ftPk_SpecialAirHi_Enter,
    ftSs_SpecialAirHi_Enter,
    ftYs_SpecialAirHi_Enter,
    ftPr_SpecialAirHi_Enter,
    ftMt_SpecialAirHiStart_Enter,
    ftLg_SpecialAirHi_Enter,
    ftMs_SpecialAirHi_Enter,
    ftZd_SpecialAirHi_Enter,
    ftLk_SpecialAirHi_Enter,
    ftMr_SpecialAirHi_Enter,
    ftFx_SpecialAirHiStart_Enter,
    ftPk_SpecialAirHi_Enter,
    ftGw_SpecialAirHi_Enter,
    ftCa_SpecialAirHi_Enter,
    ftMs_SpecialAirHi_Enter,
    NULL,
    NULL,
    NULL,
    NULL,
    ftKp_SpecialAirHi_Enter,
    NULL,
};

HSD_GObjEvent ftData_SpecialAirLw[FTKIND_MAX] = {
    ftMr_SpecialAirLw_Enter,
    ftFx_SpecialAirLw_Enter,
    ftCa_SpecialAirLw_Enter,
    NULL,
    ftKb_SpecialAirLw_Enter,
    ftKp_SpecialAirLw_Enter,
    ftLk_SpecialAirLw_Enter,
    ftSk_SpecialAirLw_Enter,
    ftNs_SpecialAirLwStart_Enter,
    ftPe_SpecialAirLw_Enter,
    ftPp_SpecialAirLw_Enter,
    ftPp_SpecialAirLw_Enter,
    ftPk_SpecialAirLw_Enter,
    ftSs_SpecialAirLw_Enter,
    ftYs_SpecialAirLw_Enter,
    ftPr_SpecialAirLw_Enter,
    ftMt_SpecialAirLw_Enter,
    ftLg_SpecialAirLw_Enter,
    ftMs_SpecialAirLw_Enter,
    ftZd_SpecialAirLw_Enter,
    ftLk_SpecialAirLw_Enter,
    ftMr_SpecialAirLw_Enter,
    ftFx_SpecialAirLw_Enter,
    ftPk_SpecialAirLw_Enter,
    ftGw_SpecialAirLw_Enter,
    ftCa_SpecialAirLw_Enter,
    ftMs_SpecialAirLw_Enter,
    NULL,
    NULL,
    NULL,
    NULL,
    ftKp_SpecialAirLw_Enter,
    NULL,
};

HSD_GObjEvent ftData_SpecialAirS[FTKIND_MAX] = {
    ftMr_SpecialAirS_Enter,
    ftFx_SpecialAirSStart_Enter,
    ftCa_SpecialAirS_Enter,
    ftDk_SpecialAirS_Enter,
    ftKb_SpecialAirS_Enter,
    ftKp_SpecialAirS_Enter,
    ftLk_SpecialAirS_Enter,
    ftSk_SpecialAirS_Enter,
    ftNs_SpecialAirS_Enter,
    ftPe_SpecialAirS_Enter,
    ftPp_SpecialAirS_Enter,
    NULL,
    ftPk_SpecialAirS_Enter,
    ftSs_SpecialAirS_Enter,
    ftYs_SpecialAirS_Enter,
    ftPr_SpecialAirS_Enter,
    ftMt_SpecialAirS_Enter,
    ftLg_SpecialAirS_Enter,
    ftMs_SpecialAirS_Enter,
    ftZd_SpecialAirS_Enter,
    ftLk_SpecialAirS_Enter,
    ftMr_SpecialAirS_Enter,
    ftFx_SpecialAirSStart_Enter,
    ftPk_SpecialAirS_Enter,
    ftGw_SpecialAirS_Enter,
    ftCa_SpecialAirS_Enter,
    ftMs_SpecialAirS_Enter,
    NULL,
    NULL,
    NULL,
    NULL,
    ftKp_SpecialAirS_Enter,
    NULL,
};

HSD_GObjEvent ftData_SpecialAirN[FTKIND_MAX] = {
    ftMr_SpecialAirN_Enter,
    ftFx_SpecialAirN_Enter,
    ftCa_SpecialAirN_Enter,
    ftDk_SpecialAirN_Enter,
    ftKb_SpecialAirN_Enter,
    ftKp_SpecialAirN_Enter,
    ftLk_SpecialAirN_Enter,
    ftSk_SpecialAirN_Enter,
    ftNs_SpecialAirNStart_Enter,
    ftPe_SpecialAirN_Enter,
    ftPp_SpecialAirN_Enter,
    ftPp_SpecialAirN_Enter,
    ftPk_SpecialAirN_Enter,
    ftSs_SpecialAirN_Enter,
    ftYs_SpecialAirN_Enter,
    ftPr_SpecialAirN_Enter,
    ftMt_SpecialAirN_Enter,
    ftLg_SpecialAirN_Enter,
    ftMs_SpecialAirN_Enter,
    ftZd_SpecialAirN_Enter,
    ftLk_SpecialAirN_Enter,
    ftMr_SpecialAirN_Enter,
    ftFx_SpecialAirN_Enter,
    ftPk_SpecialAirN_Enter,
    ftGw_SpecialAirN_Enter,
    ftCa_SpecialAirN_Enter,
    ftMs_SpecialAirN_Enter,
    NULL,
    NULL,
    NULL,
    NULL,
    ftKp_SpecialAirN_Enter,
    NULL,
};

HSD_GObjEvent ftData_SpecialN[FTKIND_MAX] = {
    ftMr_SpecialN_Enter,
    ftFx_SpecialN_Enter,
    ftCa_SpecialN_Enter,
    ftDk_SpecialN_Enter,
    ftKb_SpecialN_Enter,
    ftKp_SpecialN_Enter,
    ftLk_SpecialN_Enter,
    ftSk_SpecialN_Enter,
    ftNs_SpecialNStart_Enter,
    ftPe_SpecialN_Enter,
    ftPp_SpecialN_Enter,
    ftPp_SpecialN_Enter,
    ftPk_SpecialN_Enter,
    ftSs_SpecialN_Enter,
    ftYs_SpecialN_Enter,
    ftPr_SpecialN_Enter,
    ftMt_SpecialN_Enter,
    ftLg_SpecialN_Enter,
    ftMs_SpecialN_Enter,
    ftZd_SpecialN_Enter,
    ftLk_SpecialN_Enter,
    ftMr_SpecialN_Enter,
    ftFx_SpecialN_Enter,
    ftPk_SpecialN_Enter,
    ftGw_SpecialN_Enter,
    ftCa_SpecialN_Enter,
    ftMs_SpecialN_Enter,
    NULL,
    NULL,
    NULL,
    NULL,
    ftKp_SpecialN_Enter,
    NULL,
};

HSD_GObjEvent ftData_SpecialLw[FTKIND_MAX] = {
    ftMr_SpecialLw_Enter,
    ftFx_SpecialLw_Enter,
    ftCa_SpecialLw_Enter,
    ftDk_SpecialLw_Enter,
    ftKb_SpecialLw_Enter,
    ftKp_SpecialLw_Enter,
    ftLk_SpecialLw_Enter,
    ftSk_SpecialLw_Enter,
    ftNs_SpecialLwStart_Enter,
    ftPe_SpecialLw_Enter,
    ftPp_SpecialLw_Enter,
    ftPp_SpecialLw_Enter,
    ftPk_SpecialLw_Enter,
    ftSs_SpecialLw_Enter,
    ftYs_SpecialLw_Enter,
    ftPr_SpecialLw_Enter,
    ftMt_SpecialLw_Enter,
    ftLg_SpecialLw_Enter,
    ftMs_SpecialLw_Enter,
    ftZd_SpecialLw_Enter,
    ftLk_SpecialLw_Enter,
    ftMr_SpecialLw_Enter,
    ftFx_SpecialLw_Enter,
    ftPk_SpecialLw_Enter,
    ftGw_SpecialLw_Enter,
    ftCa_SpecialLw_Enter,
    ftMs_SpecialLw_Enter,
    NULL,
    NULL,
    NULL,
    NULL,
    ftKp_SpecialLw_Enter,
    NULL,
};

HSD_GObjEvent ftData_SpecialHi[FTKIND_MAX] = {
    ftMr_SpecialHi_Enter,
    ftFx_SpecialHi_Enter,
    ftCa_SpecialHi_Enter,
    ftDk_SpecialHi_Enter,
    ftKb_SpecialHi_Enter,
    ftKp_SpecialHi_Enter,
    ftLk_SpecialHi_Enter,
    ftSk_SpecialHi_Enter,
    ftNs_SpecialHiStart_Enter,
    ftPe_SpecialHi_Enter,
    ftPp_SpecialHi_Enter,
    NULL,
    ftPk_SpecialHi_Enter,
    ftSs_SpecialHi_Enter,
    ftYs_SpecialHi_Enter,
    ftPr_SpecialHi_Enter,
    ftMt_SpecialHiStart_Enter,
    ftLg_SpecialHi_Enter,
    ftMs_SpecialHi_Enter,
    ftZd_SpecialHi_Enter,
    ftLk_SpecialHi_Enter,
    ftMr_SpecialHi_Enter,
    ftFx_SpecialHi_Enter,
    ftPk_SpecialHi_Enter,
    ftGw_SpecialHi_Enter,
    ftCa_SpecialHi_Enter,
    ftMs_SpecialHi_Enter,
    NULL,
    NULL,
    NULL,
    NULL,
    ftKp_SpecialHi_Enter,
    NULL,
};

HSD_GObjEvent ftData_OnAbsorb[FTKIND_MAX] = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftNs_Init_OnAbsorb,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftGw_Init_OnAbsorb,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

Fighter_ItemEvent ftData_OnItemPickupExt[FTKIND_MAX] = {
    ftMr_Init_OnItemPickup,
    ftFx_Init_OnItemPickup,
    ftCa_Init_OnItemPickup,
    ftDk_Init_OnItemPickup,
    ftKb_Init_OnItemPickup,
    ftKp_Init_OnItemPickup,
    ftLk_Init_OnItemPickupExt,
    ftSk_Init_OnItemPickup,
    ftNs_Init_OnItemPickup,
    ftPe_Init_OnItemPickup,
    ftPp_Init_OnItemPickup,
    ftPp_Init_OnItemPickup,
    ftPk_Init_OnItemPickup,
    ftSs_Init_OnItemPickup,
    ftYs_Init_OnItemPickup,
    ftPr_Init_OnItemPickup,
    ftMt_Init_OnItemPickup,
    ftLg_Init_OnItemPickup,
    ftMs_Init_OnItemPickup,
    ftZd_Init_OnItemPickup,
    ftCl_Init_OnItemPickupExt,
    ftDr_Init_OnItemPickup,
    ftFc_Init_OnItemPickup,
    ftPc_Init_OnItemPickup,
    ftGw_Init_OnItemPickup,
    ftGn_Init_OnItemPickup,
    ftFe_Init_OnItemPickup,
    NULL,
    NULL,
    ftBo_Init_OnItemPickup,
    ftGl_Init_OnItemPickup,
    ftGk_Init_OnItemPickup,
    NULL,
};

HSD_GObjEvent ftData_OnItemInvisible[FTKIND_MAX] = {
    ftMr_Init_OnItemInvisible,
    ftFx_Init_OnItemInvisible,
    ftCa_Init_OnItemInvisible,
    ftDk_Init_OnItemInvisible,
    ftKb_Init_OnItemInvisible,
    ftKp_Init_OnItemInvisible,
    ftLk_Init_OnItemInvisible,
    ftSk_Init_OnItemInvisible,
    ftNs_Init_OnItemInvisible,
    ftPe_Init_OnItemInvisible,
    ftPp_Init_OnItemInvisible,
    ftPp_Init_OnItemInvisible,
    ftPk_Init_OnItemInvisible,
    ftSs_Init_OnItemInvisible,
    ftYs_Init_OnItemInvisible,
    ftPr_Init_OnItemInvisible,
    ftMt_Init_OnItemInvisible,
    ftLg_Init_OnItemInvisible,
    ftMs_Init_OnItemInvisible,
    ftZd_Init_OnItemInvisible,
    ftCl_Init_OnItemInvisible,
    ftDr_Init_OnItemInvisible,
    ftFc_Init_OnItemInvisible,
    ftPc_Init_OnItemInvisible,
    ftGw_Init_OnItemInvisible,
    ftGn_Init_OnItemInvisible,
    ftFe_Init_OnItemInvisible,
    NULL,
    NULL,
    ftBo_Init_OnItemInvisible,
    ftGl_Init_OnItemInvisible,
    ftGk_Init_OnItemInvisible,
    NULL,
};

HSD_GObjEvent ftData_OnItemVisible[FTKIND_MAX] = {
    ftMr_Init_OnItemVisible,
    ftFx_Init_OnItemVisible,
    ftCa_Init_OnItemVisible,
    ftDk_Init_OnItemVisible,
    ftKb_Init_OnItemVisible,
    ftKp_Init_OnItemVisible,
    ftLk_Init_OnItemVisible,
    ftSk_Init_OnItemVisible,
    ftNs_Init_OnItemVisible,
    ftPe_Init_OnItemVisible,
    ftPp_Init_OnItemVisible,
    ftPp_Init_OnItemVisible,
    ftPk_Init_OnItemVisible,
    ftSs_Init_OnItemVisible,
    ftYs_Init_OnItemVisible,
    ftPr_Init_OnItemVisible,
    ftMt_Init_OnItemVisible,
    ftLg_Init_OnItemVisible,
    ftMs_Init_OnItemVisible,
    ftZd_Init_OnItemVisible,
    ftCl_Init_OnItemVisible,
    ftDr_Init_OnItemVisible,
    ftFc_Init_OnItemVisible,
    ftPc_Init_OnItemVisible,
    ftGw_Init_OnItemVisible,
    ftGn_Init_OnItemVisible,
    ftFe_Init_OnItemVisible,
    NULL,
    NULL,
    ftBo_Init_OnItemVisible,
    ftGl_Init_OnItemVisible,
    ftGk_Init_OnItemVisible,
    NULL,
};

Fighter_ItemEvent ftData_OnItemDropExt[FTKIND_MAX] = {
    ftMr_Init_OnItemDrop,
    ftFx_Init_OnItemDrop,
    ftCa_Init_OnItemDrop,
    ftDk_Init_OnItemDrop,
    ftKb_Init_OnItemDrop,
    ftKp_Init_OnItemDrop,
    ftLk_Init_OnItemDropExt,
    ftSk_Init_OnItemDrop,
    ftNs_Init_OnItemDrop,
    ftPe_Init_OnItemDrop,
    ftPp_Init_OnItemDrop,
    ftPp_Init_OnItemDrop,
    ftPk_Init_OnItemDrop,
    ftSs_Init_OnItemDrop,
    ftYs_Init_OnItemDrop,
    ftPr_Init_OnItemDrop,
    ftMt_Init_OnItemDrop,
    ftLg_Init_OnItemDrop,
    ftMs_Init_OnItemDrop,
    ftZd_Init_OnItemDrop,
    ftCl_Init_OnItemDropExt,
    ftDr_Init_OnItemDrop,
    ftFc_Init_OnItemDrop,
    ftPc_Init_OnItemDrop,
    ftGw_Init_OnItemDrop,
    ftGn_Init_OnItemDrop,
    ftFe_Init_OnItemDrop,
    NULL,
    NULL,
    ftBo_Init_OnItemDrop,
    ftGl_Init_OnItemDrop,
    ftGk_Init_OnItemDrop,
    NULL,
};

Fighter_ItemEvent ftData_OnItemPickup[FTKIND_MAX] = {
    ftMr_Init_OnItemPickup,
    ftFx_Init_OnItemPickup,
    ftCa_Init_OnItemPickup,
    ftDk_Init_OnItemPickup,
    ftKb_Init_OnItemPickup,
    ftKp_Init_OnItemPickup,
    ftLk_Init_OnItemPickup,
    ftSk_Init_OnItemPickup,
    ftNs_Init_OnItemPickup,
    ftPe_Init_OnItemPickup,
    ftPp_Init_OnItemPickup,
    ftPp_Init_OnItemPickup,
    ftPk_Init_OnItemPickup,
    ftSs_Init_OnItemPickup,
    ftYs_Init_OnItemPickup,
    ftPr_Init_OnItemPickup,
    ftMt_Init_OnItemPickup,
    ftLg_Init_OnItemPickup,
    ftMs_Init_OnItemPickup,
    ftZd_Init_OnItemPickup,
    ftCl_Init_OnItemPickup,
    ftDr_Init_OnItemPickup,
    ftFc_Init_OnItemPickup,
    ftPc_Init_OnItemPickup,
    ftGw_Init_OnItemPickup,
    ftGn_Init_OnItemPickup,
    ftFe_Init_OnItemPickup,
    NULL,
    NULL,
    ftBo_Init_OnItemPickup,
    ftGl_Init_OnItemPickup,
    ftGk_Init_OnItemPickup,
    NULL,
};

Fighter_ItemEvent ftData_OnItemDrop[FTKIND_MAX] = {
    ftMr_Init_OnItemDrop,
    ftFx_Init_OnItemDrop,
    ftCa_Init_OnItemDrop,
    ftDk_Init_OnItemDrop,
    ftKb_Init_OnItemDrop,
    ftKp_Init_OnItemDrop,
    ftLk_Init_OnItemDrop,
    ftSk_Init_OnItemDrop,
    ftNs_Init_OnItemDrop,
    ftPe_Init_OnItemDrop,
    ftPp_Init_OnItemDrop,
    ftPp_Init_OnItemDrop,
    ftPk_Init_OnItemDrop,
    ftSs_Init_OnItemDrop,
    ftYs_Init_OnItemDrop,
    ftPr_Init_OnItemDrop,
    ftMt_Init_OnItemDrop,
    ftLg_Init_OnItemDrop,
    ftMs_Init_OnItemDrop,
    ftZd_Init_OnItemDrop,
    ftCl_Init_OnItemDrop,
    ftDr_Init_OnItemDrop,
    ftFc_Init_OnItemDrop,
    ftPc_Init_OnItemDrop,
    ftGw_Init_OnItemDrop,
    ftGn_Init_OnItemDrop,
    ftFe_Init_OnItemDrop,
    NULL,
    NULL,
    ftBo_Init_OnItemDrop,
    ftGl_Init_OnItemDrop,
    ftGk_Init_OnItemDrop,
    NULL,
};

HSD_GObjEvent ftData_UnkMotionStates1[FTKIND_MAX] = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftPk_Init_UnkMotionStates1,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

HSD_GObjEvent ftData_UnkMotionStates2[FTKIND_MAX] = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftPk_Init_UnkMotionStates2,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

HSD_GObjEvent ftData_OnKnockbackEnter[FTKIND_MAX] = {
    ftMr_Init_OnKnockbackEnter,
    ftFx_Init_OnKnockbackEnter,
    NULL,
    ftDk_Init_OnKnockbackEnter,
    ftKb_Init_OnKnockbackEnter,
    ftKp_Init_OnKnockbackEnter,
    ftLk_Init_OnKnockbackEnter,
    ftSk_Init_OnKnockbackEnter,
    ftNs_Init_OnKnockbackEnter,
    ftPe_Init_OnKnockbackEnter,
    ftPp_Init_OnKnockbackEnter,
    ftPp_Init_OnKnockbackEnter,
    ftPk_Init_OnKnockbackEnter,
    NULL,
    ftYs_Init_OnKnockbackEnter,
    ftPr_Init_OnKnockbackEnter,
    ftMt_Init_OnKnockbackEnter,
    ftLg_Init_OnKnockbackEnter,
    ftMs_Init_OnKnockbackEnter,
    ftZd_Init_OnKnockbackEnter,
    ftCl_Init_OnKnockbackEnter,
    ftDr_Init_OnKnockbackEnter,
    ftFc_Init_OnKnockbackEnter,
    ftPc_Init_OnKnockbackEnter,
    NULL,
    ftGn_Init_OnKnockbackEnter,
    ftFe_Init_OnKnockbackEnter,
    NULL,
    NULL,
    NULL,
    NULL,
    ftGk_Init_OnKnockbackEnter,
    ftSb_Init_OnKnockbackEnter,
};

HSD_GObjEvent ftData_OnKnockbackExit[FTKIND_MAX] = {
    ftMr_Init_OnKnockbackExit,
    ftFx_Init_OnKnockbackExit,
    NULL,
    ftDk_Init_OnKnockbackExit,
    ftKb_Init_OnKnockbackExit,
    ftKp_Init_OnKnockbackExit,
    ftLk_Init_OnKnockbackExit,
    ftSk_Init_OnKnockbackExit,
    ftNs_Init_OnKnockbackExit,
    ftPe_Init_OnKnockbackExit,
    ftPp_Init_OnKnockbackExit,
    ftPp_Init_OnKnockbackExit,
    ftPk_Init_OnKnockbackExit,
    NULL,
    ftYs_Init_OnKnockbackExit,
    ftPr_Init_OnKnockbackExit,
    ftMt_Init_OnKnockbackExit,
    ftLg_Init_OnKnockbackExit,
    ftMs_Init_OnKnockbackExit,
    ftZd_Init_OnKnockbackExit,
    ftCl_Init_OnKnockbackExit,
    ftDr_Init_OnKnockbackExit,
    ftFc_Init_OnKnockbackExit,
    ftPc_Init_OnKnockbackExit,
    NULL,
    ftGn_Init_OnKnockbackExit,
    ftFe_Init_OnKnockbackExit,
    NULL,
    NULL,
    NULL,
    NULL,
    ftGk_Init_OnKnockbackExit,
    ftSb_Init_OnKnockbackExit,
};

HSD_GObjEvent ftData_UnkMotionStates3[FTKIND_MAX] = {
    NULL,
    NULL,
    NULL,
    NULL,
    ftKb_Init_UnkMotionStates3,
    ftKp_Init_UnkMotionStates3,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftGk_Init_UnkMotionStates3,
    NULL,
};

HSD_GObjEvent ftData_UnkMotionStates4[FTKIND_MAX] = {
    NULL,
    NULL,
    NULL,
    ftDk_Init_UnkMotionStates4,
    ftKb_Init_UnkMotionStates4,
    NULL,
    NULL,
    ftSk_Init_UnkMotionStates4,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftSs_Init_UnkMotionStates4,
    NULL,
    NULL,
    ftMt_Init_UnkMotionStates4,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftGw_Init_UnkMotionStates4,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

HSD_GObjEvent ftKindCalcIndiviParamTable[FTKIND_MAX] = {
    ftMr_Init_LoadSpecialAttrs, ftFx_Init_LoadSpecialAttrs,
    ftCa_Init_LoadSpecialAttrs, ftDk_Init_LoadSpecialAttrs,
    ftKb_Init_LoadSpecialAttrs, ftKp_Init_LoadSpecialAttrs,
    ftLk_Init_LoadSpecialAttrs, ftSk_Init_LoadSpecialAttrs,
    ftNs_Init_LoadSpecialAttrs, ftPe_Init_LoadSpecialAttrs,
    ftPp_Init_LoadSpecialAttrs, ftNn_Init_LoadSpecialAttrs,
    ftPk_Init_LoadSpecialAttrs, ftSs_Init_LoadSpecialAttrs,
    ftYs_Init_LoadSpecialAttrs, ftPr_Init_LoadSpecialAttrs,
    ftMt_Init_LoadSpecialAttrs, ftLg_Init_LoadSpecialAttrs,
    ftMs_Init_LoadSpecialAttrs, ftZd_Init_LoadSpecialAttrs,
    ftCl_Init_LoadSpecialAttrs, ftDr_Init_LoadSpecialAttrs,
    ftFc_Init_LoadSpecialAttrs, ftPc_Init_LoadSpecialAttrs,
    ftGw_Init_LoadSpecialAttrs, ftGn_Init_LoadSpecialAttrs,
    ftFe_Init_LoadSpecialAttrs, ftMh_Init_LoadSpecialAttrs,
    ftCh_Init_LoadSpecialAttrs, ftBo_Init_LoadSpecialAttrs,
    ftGl_Init_LoadSpecialAttrs, ftGk_Init_LoadSpecialAttrs,
    ftSb_Init_LoadSpecialAttrs,
};

/// Standard Character .dat File Names
struct StringPair {
    char* a;
    char* b;
};

struct StringPair ftData_803C1F40[FTKIND_MAX] = {
    { ftMr_Init_DatFilename, ftMr_Init_DataName },
    { ftFx_Init_DatFilename, ftFx_Init_DataName },
    { ftCa_Init_DatFilename, ftCa_Init_DataName },
    { ftDk_Init_DatFilename, ftDk_Init_DataName },
    { ftKb_Init_DatFilename, ftKb_Init_DataName },
    { ftKp_Init_DatFilename, ftKp_Init_DataName },
    { ftLk_Init_DatFilename, ftLk_Init_DataName },
    { ftSk_Init_DatFilename, ftSk_Init_DataName },
    { ftNs_Init_DatFilename, ftNs_Init_DataName },
    { ftPe_Init_DatFilename, ftPe_Init_DataName },
    { ftPp_Init_DatFilename, ftPp_Init_DataName },
    { ftNn_Init_DatFilename, ftNn_Init_DataName },
    { ftPk_Init_DatFilename, ftPk_Init_DataName },
    { ftSs_Init_DatFilename, ftSs_Init_DataName },
    { ftYs_Init_DatFilename, ftYs_Init_DataName },
    { ftPr_Init_DatFilename, ftPr_Init_DataName },
    { ftMt_Init_DatFilename, ftMt_Init_DataName },
    { ftLg_Init_DatFilename, ftLg_Init_DataName },
    { ftMs_Init_DatFilename, ftMs_Init_DataName },
    { ftZd_Init_DatFilename, ftZd_Init_DataName },
    { ftCl_Init_DatFilename, ftCl_Init_DataName },
    { ftDr_Init_DatFilename, ftDr_Init_DataName },
    { ftFc_Init_DatFilename, ftFc_Init_DataName },
    { ftPc_Init_DatFilename, ftPc_Init_DataName },
    { ftGw_Init_DatFilename, ftGw_Init_DataName },
    { ftGn_Init_DatFilename, ftGn_Init_DataName },
    { ftFe_Init_DatFilename, ftFe_Init_DataName },
    { ftMh_Init_DatFilename, ftMh_Init_DataName },
    { ftCh_Init_DatFilename, ftCh_Init_DataName },
    { ftBo_Init_DatFilename, ftBo_Init_DataName },
    { ftGl_Init_DatFilename, ftGl_Init_DataName },
    { ftGk_Init_DatFilename, ftGk_Init_DataName },
    { ftSb_Init_DatFilename, ftSb_Init_DataName },
};

Event ftData_UnkMotionStates5[FTKIND_MAX] = {
    NULL, NULL, NULL, NULL, ftKb_Init_UnkMotionStates5,
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL,
};

Fighter_UnkMtxEvent ftData_UnkMtxFunc0[FTKIND_MAX] = {
    NULL,
    NULL,
    NULL,
    NULL,
    ftKb_UnkMtxFunc0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftPr_Init_UnkMtxFunc0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

/// Character model group (e.g. high poly, low poly, metal) visibility change
/// callbacks
ftData_UnkModelStruct ftData_UnkIntBoolFunc0 = {
    {
        NULL,
        NULL,
        NULL,
        NULL,
        ftKb_UnkIntBoolFunc0,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        ftPr_Init_UnkIntBoolFunc0,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
    },
    {
        NULL,
        NULL,
        NULL,
        NULL,
        ftKb_Init_UnkMotionStates6,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        ftPr_Init_UnkMotionStates6,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
    },
};

struct {
    HSD_GObjEvent x0;
    void (*x4)(Fighter_GObj*, int, float frame);
} ftData_UnkCallbackPairs0[FTKIND_MAX] = {
    { NULL, NULL },
    { NULL, NULL },
    { NULL, NULL },
    { NULL, NULL },
    { ftKb_Init_UnkCallbackPairs0_0, ftKb_Init_UnkCallbackPairs0_1 },
};

/// Costume and Joint Strings
Fighter_CostumeStrings* ftData_803C2360[FTKIND_MAX] = {
    ftMr_Init_CostumeStrings, ftFx_Init_CostumeStrings,
    ftCa_Init_CostumeStrings, ftDk_Init_CostumeStrings,
    ftKb_Init_CostumeStrings, ftKp_Init_CostumeStrings,
    ftLk_Init_CostumeStrings, ftSk_Init_CostumeStrings,
    ftNs_Init_CostumeStrings, ftPe_Init_CostumeStrings,
    ftPp_Init_CostumeStrings, ftNn_Init_CostumeStrings,
    ftPk_Init_CostumeStrings, ftSs_Init_CostumeStrings,
    ftYs_Init_CostumeStrings, ftPr_Init_CostumeStrings,
    ftMt_Init_CostumeStrings, ftLg_Init_CostumeStrings,
    ftMs_Init_CostumeStrings, ftZd_Init_CostumeStrings,
    ftCl_Init_CostumeStrings, ftDr_Init_CostumeStrings,
    ftFc_Init_CostumeStrings, ftPc_Init_CostumeStrings,
    ftGw_Init_CostumeStrings, ftGn_Init_CostumeStrings,
    ftFe_Init_CostumeStrings, ftMh_Init_CostumeStrings,
    ftCh_Init_CostumeStrings, ftBo_Init_CostumeStrings,
    ftGl_Init_CostumeStrings, ftGk_Init_CostumeStrings,
    ftSb_Init_CostumeStrings,

};

char* ftData_803C23E4[FTKIND_MAX] = {
    ftMr_Init_AnimDatFilename, ftFx_Init_AnimDatFilename,
    ftCa_Init_AnimDatFilename, ftDk_Init_AnimDatFilename,
    ftKb_Init_AnimDatFilename, ftKp_Init_AnimDatFilename,
    ftLk_Init_AnimDatFilename, ftSk_Init_AnimDatFilename,
    ftNs_Init_AnimDatFilename, ftPe_Init_AnimDatFilename,
    ftPp_Init_AnimDatFilename, ftNn_Init_AnimDatFilename,
    ftPk_Init_AnimDatFilename, ftSs_Init_AnimDatFilename,
    ftYs_Init_AnimDatFilename, ftPr_Init_AnimDatFilename,
    ftMt_Init_AnimDatFilename, ftLg_Init_AnimDatFilename,
    ftMs_Init_AnimDatFilename, ftZd_Init_AnimDatFilename,
    ftCl_Init_AnimDatFilename, ftDr_Init_AnimDatFilename,
    ftFc_Init_AnimDatFilename, ftPc_Init_AnimDatFilename,
    ftGw_Init_AnimDatFilename, ftGn_Init_AnimDatFilename,
    ftFe_Init_AnimDatFilename, ftMh_Init_AnimDatFilename,
    ftCh_Init_AnimDatFilename, ftBo_Init_AnimDatFilename,
    ftGl_Init_AnimDatFilename, ftGk_Init_AnimDatFilename,
    ftSb_Init_AnimDatFilename,
};

/// Demo Lookup Strings
Fighter_DemoStrings* ftData_803C2468[FTKIND_MAX] = {
    &ftMr_Init_DemoMotionFilenames,
    &ftFx_Init_DemoMotionFilenames,
    &ftCa_Init_DemoMotionFilenames,
    &ftDk_Init_DemoMotionFilenames,
    &ftKb_Init_DemoMotionFilenames,
    &ftKp_Init_DemoMotionFilenames,
    &ftLk_Init_DemoMotionFilenames,
    &ftSk_Init_DemoMotionFilenames,
    &ftNs_Init_DemoMotionFilenames,
    &ftPe_Init_DemoMotionFilenames,
    &ftPp_Init_DemoMotionFilenames,
    &ftNn_Init_DemoMotionFilenames,
    &ftPk_Init_DemoMotionFilenames,
    &ftSs_Init_DemoMotionFilenames,
    &ftYs_Init_DemoMotionFilenames,
    &ftPr_Init_DemoMotionFilenames,
    &ftMt_Init_DemoMotionFilenames,
    &ftLg_Init_DemoMotionFilenames,
    &ftMs_Init_DemoMotionFilenames,
    &ftZd_Init_DemoMotionFilenames,
    &ftCl_Init_DemoMotionFilenames,
    &ftDr_Init_DemoMotionFilenames,
    &ftFc_Init_DemoMotionFilenames,
    &ftPc_Init_DemoMotionFilenames,
    &ftGw_Init_DemoMotionFilenames,
    &ftGn_Init_DemoMotionFilenames,
    &ftFe_Init_DemoMotionFilenames,
    NULL,
    NULL,
    NULL,
    NULL,
    &ftGk_Init_DemoMotionFilenames,
    NULL,
};

Fighter_MotionFileStringGetter ftData_803C24EC[FTKIND_MAX] = {
    ftMr_Init_GetMotionFileString,
    NULL,
    NULL,
    NULL,
    ftKb_Init_GetMotionFileString,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftLg_Init_GetMotionFileString,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftGk_Init_GetMotionFileString,
    NULL,
};

Fighter_UnkPtrEvent ftData_UnkDemoCallbacks0[FTKIND_MAX] = {
    ftMr_Init_UnkDemoCallbacks0,
    NULL,
    NULL,
    NULL,
    ftKb_Init_UnkDemoCallbacks0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftLg_Init_UnkDemoCallbacks0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ftGk_Init_UnkDemoCallbacks0,
    NULL,
};

ftData_UnkCountStruct ftData_UnkIntPairs[FTKIND_MAX] = {
    { 0, 16 }, { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 18 }, { 0, 14 },
    { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 14 },
    { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 16 },
    { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 14 },
    { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 14 }, { 0, 14 },
    { 0, 14 }, { 0, 15 }, { 0, 14 },
};

u8 ftData_UnkBytePerCharacter[FTKIND_MAX] = {
    1,  3,  4,  8, 5, 12, 6, 17, 10, 15, 14, 14, 7,  2,  9,  11, 13,
    18, 16, 17, 6, 1, 3,  7, -1, 19, 49, -1, -1, -1, -1, 12, -1,
};
#endif

void ftData_80085560(int idx, int increment)
{
    ft_8045996C[idx] += increment;
    if (ft_8045996C[idx] < 0) {
        OSReport("fighter reference counter error!\n");
        HSD_ASSERT(1944, 0);
    }
}

char ftData_assert_msg_0[] = "cant get corps model array!\n";
char ftData_assert_msg_1[] = "HSD_ArchiveParse error!\n";

void ftData_800855C8(FighterKind kind, u8 color)
{
    int i;
    int lo;
    int hi;

    if (color != 0xFF &&
        color >= CostumeListsForeachCharacter[kind].numCostumes)
    {
        color = 0;
    }
    if (ftData_803C1F40[kind].a != NULL) {
        lbDvd_800178E8(2, ftData_803C1F40[kind].a, 4, 4, 0, 1, 4, 2, 0);
    }
    if (color == 0xFF) {
        lo = 0;
        hi = CostumeListsForeachCharacter[kind].numCostumes;
    } else {
        lo = color;
        hi = color + 1;
    }
    for (i = lo; i < hi; i++) {
        if (ftData_803C2360[kind][i].dat_filename != NULL) {
            lbDvd_800178E8(2, ftData_803C2360[kind][i].dat_filename, 4, 4, 0,
                           1, 3, 1, 0);
        }
    }
    if (ftData_UnkBytePerCharacter[kind] != -1) {
        efAsync_LoadAsync(ftData_UnkBytePerCharacter[kind]);
    }
    if (ftData_803C23E4[kind] != NULL) {
        lbDvd_800178E8(1, ftData_803C23E4[kind], 5, 5, 0, 0, 1, 8, 0);
    }
}

void ftData_8008572C(FighterKind kind)
{
    if (gFtDataList[kind] == NULL) {
        lbArchive_80017040(NULL, ftData_803C1F40[kind].a, &gFtDataList[kind],
                           ftData_803C1F40[kind].b, MSL_LBARCHIVE_END);
    }
}

void ftData_8008578C(int arg0, u8 color)
{
    if (color != 0xFF &&
        color >= CostumeListsForeachCharacter[FTKIND_KIRBY].numCostumes)
    {
        color = 0;
    }
    ftKb_SpecialN_800EEC34(
        arg0, color, CostumeListsForeachCharacter[FTKIND_KIRBY].numCostumes);
}

void ftData_800857E0(FighterKind kind)
{
    if (ftData_UnkMotionStates5[kind] != NULL) {
        ftData_UnkMotionStates5[kind]();
    }
}

void ftData_80085820(FighterKind kind, int costume_id)
{
    UnkCostumeStruct* temp_r5 =
        &CostumeListsForeachCharacter[kind].costume_list[costume_id];
    if (temp_r5->joint == NULL) {
        if (ftData_803C2360[kind][costume_id].matanim_joint_name != NULL) {
            lbArchive_80017040(
                &temp_r5->x14_archive,
                ftData_803C2360[kind][costume_id].dat_filename, temp_r5,
                ftData_803C2360[kind][costume_id].joint_name, &temp_r5->x4,
                ftData_803C2360[kind][costume_id].matanim_joint_name,
                MSL_LBARCHIVE_END);
        } else {
            lbArchive_80017040(
                &temp_r5->x14_archive,
                ftData_803C2360[kind][costume_id].dat_filename, temp_r5,
                ftData_803C2360[kind][costume_id].joint_name,
                MSL_LBARCHIVE_END,
                ftData_803C2360[kind][costume_id].matanim_joint_name);
            CostumeListsForeachCharacter[kind].costume_list[costume_id].x4 =
                NULL;
        }
    }
}

void ftData_800858E4(FighterKind kind, int costume_id)
{
    UnkCostumeStruct* temp_r5 =
        &CostumeListsForeachCharacter[kind].costume_list[costume_id];
    if (temp_r5->joint == NULL) {
        if (ftData_803C2360[kind][costume_id].matanim_joint_name != NULL) {
            lbArchive_80017040(
                &temp_r5->x14_archive,
                ftData_803C2360[kind][costume_id].dat_filename, temp_r5,
                ftData_803C2360[kind][costume_id].joint_name, &temp_r5->x4,
                ftData_803C2360[kind][costume_id].matanim_joint_name,
                MSL_LBARCHIVE_END);
        } else {
            lbArchive_80017040(
                &temp_r5->x14_archive,
                ftData_803C2360[kind][costume_id].dat_filename, temp_r5,
                ftData_803C2360[kind][costume_id].joint_name,
                MSL_LBARCHIVE_END,
                ftData_803C2360[kind][costume_id].matanim_joint_name);
            CostumeListsForeachCharacter[kind].costume_list[costume_id].x4 =
                NULL;
        }
    }
}

void ftData_800859A8(Fighter* fp)
{
    HSD_GObj* gobj;
    s8 temp_r6 = fp->x61C;
    if (temp_r6 == -1) {
        return;
    }
    for (gobj = HSD_GObj_Entities->fighters; gobj != NULL; gobj = gobj->next) {
        Fighter* cur_fp = GET_FIGHTER(gobj);
        if (fp != cur_fp && temp_r6 == cur_fp->x61C) {
            return;
        }
    }
    ft_8045993C[temp_r6].x6_b0 = false;
}

void ftData_80085A14(FighterKind kind)
{
    u32 sp18;
    u32 a_head;
    ftData* temp_r27 = gFtDataList[kind];
    u32 temp_r0;
    int i;
    u8 _[4];
    u32 sp10;

    PAD_STACK(4);

    if (ftData_Table_Unk0[kind].data == NULL) {
        lbFile_800168A0(1, ftData_803C23E4[kind], &sp18, &sp10);
        a_head = sp18;
        HSD_ASSERT(0x974, a_head);
        for (i = 0; i < (u32) ftData_Table_Unk0[kind].count; i++) {
            temp_r0 = temp_r27->xC[i].x8;
            if (temp_r0 != 0) {
                if (temp_r0 > 0x8000) {
                    HSD_ASSERTREPORT(0x9AF, 0, "fighter figatree over! %x\n",
                                     temp_r0);
                }
                temp_r27->xC[i].x14 = (a_head + temp_r27->xC[i].x4);
            }
        }
        ftData_Table_Unk0[kind].data = (void*) a_head;
    }
}

void ftData_80085B10(Fighter* fp)
{
    FighterKind kind = fp->kind;
#ifdef MSL_CORE_NATIVE
    // Hosted animation graphs are translated once from the immutable
    // PlFxAJ.dat subarchive below. Retail's two 32 KiB ARAM/file scratch
    // buffers are therefore never a native gameplay owner; the only source
    // branch that copies another fighter's scratch belongs to Nana, outside
    // the supported domain. Do not replicate 64 KiB of dead state per
    // fighter in every Match.
    // refs/melee/src/melee/ft/ftdata.c::{ftData_80085A14,
    //   ftData_80085CD8,ftData_80085E50,ftData_80086060}
    fp->x59C = NULL;
    fp->x5A0 = NULL;
#else
    fp->x59C = HSD_ObjAlloc(&fighter_x59C_alloc_data);
    fp->x5A0 = HSD_ObjAlloc(&fighter_x59C_alloc_data);
#endif
    fp->x5A4 = NULL;
    fp->x5A8 = NULL;
    fp->x58C = ftData_Table_Unk0[kind].count;
    ftData_80085A14(kind);
}

void ftData_80085B98(Fighter* fp, int arg1, int arg2)
{
    u32 temp_r30;
    int i;
    u32 temp_r0;
    struct Fighter_WaitAnimData* temp_r3;

    temp_r30 = (u32) ftData_UnkIntPairs[fp->kind].data;
#ifdef MSL_CORE_NATIVE
    fp->x59C = NULL;
    fp->x5A0 = NULL;
#else
    fp->x59C = HSD_ObjAlloc(&fighter_x59C_alloc_data);
    fp->x5A0 = HSD_ObjAlloc(&fighter_x59C_alloc_data);
#endif
    fp->x5A4 = 0;
    fp->x5A8 = 0;
    fp->x58C = ftData_UnkIntPairs[fp->kind].count;
    if (arg2 >= fp->x58C) {
        HSD_ASSERTREPORT(0x9D2, 0, "Demo Status error! %d\n", arg2);
    }
    if (temp_r30 != 0U) {
        for (i = arg1; i <= arg2; i++) {
            temp_r3 = &fp->ft_data->x14[i];
            temp_r0 = temp_r3->x8;
            if (temp_r3->x8 != 0U) {
                if (temp_r0 > 0xB000) {
                    HSD_ASSERTREPORT(0x9DC, 0, "fighter figatree over! %x\n",
                                     temp_r0);
                }
                temp_r3 = &fp->ft_data->x14[i];
                temp_r3->x14 = temp_r30 + temp_r3->x4;
            }
        }
        ftData_UnkIntPairs[fp->kind].data = 0;
    }
}

#ifdef MSL_CORE_NATIVE
enum {
    MSL_FIGHTER_MOTION_PROGRAM = 0x80000000U,
};

static FigaTree* native_motion_tree(struct Fighter_WaitAnimData* motion)
{
    HSD_Archive archive;
    FigaTree* tree;

    if (motion->x14 & MSL_FIGHTER_MOTION_PROGRAM) {
        return msl_fighter_pose_program_tree((uint16_t) motion->x14);
    }
    HSD_ASSERT(0x9E0,
               HSD_ArchiveParse(
                   &archive,
                   msl_memory_from_token(msl_core_game_memory_context(),
                                         motion->x14),
                   motion->x8) == 0);
    tree = HSD_ArchiveGetPublicAddress(&archive, motion->x0);
    HSD_ASSERT(0x9E1, tree != NULL);
    return tree;
}

void msl_ft_data_bind_motion_programs(void)
{
    int kind;

    for (kind = 0; kind < FTKIND_MAX; ++kind) {
        ftData* data = gFtDataList[kind];
        int count = ftData_Table_Unk0[kind].count;
        int motion;

        if (data == NULL || ftData_Table_Unk0[kind].data == NULL) {
            continue;
        }
        for (motion = 0; motion < count; ++motion) {
            struct Fighter_WaitAnimData* record = &data->xC[motion];
            uint16_t token;

            if (record->x14 == 0) {
                continue;
            }
            HSD_ASSERT(0x9E2,
                       !(record->x14 & MSL_FIGHTER_MOTION_PROGRAM));
            token = msl_fighter_pose_program_token(
                native_motion_tree(record));
            record->x14 = MSL_FIGHTER_MOTION_PROGRAM | token;
        }
    }
}
#endif

void ftData_80085CD8(Fighter* fp, Fighter* arg1, int msid)
{
#ifdef MSL_CORE_NATIVE
    if (msid < arg1->x58C) {
        struct Fighter_WaitAnimData* motion =
            ftData_80085FD4(arg1, msid);
        u32 token = motion->x14;

        if (token != (u32) fp->x5A4) {
            fp->x590 = token != 0 ? native_motion_tree(motion) : NULL;
            fp->x5A4 = (void*) (uintptr_t) token;
        }
    }
#else
    HSD_Archive sp14;
    Fighter* temp_r3_3;
    s32 temp_ret;
    s32 temp_ret_2;
    struct Fighter_x59C_t* temp_r4;
    struct Fighter_WaitAnimData* temp_r3;
    u32 temp_r3_2;
    u32 temp_r4_2;

    if (msid < arg1->x58C) {
        temp_r3 = (struct Fighter_WaitAnimData*) ftData_80085FD4(arg1, msid);
        temp_r3_2 = temp_r3->x14;
        if (temp_r3_2 != (u32) fp->x5A4) {
            if (temp_r3_2 != 0) {
                temp_r3_3 = ftData_80086060(fp);
#ifdef MSL_CORE_NATIVE
                // The leader-share fast path copies Nana's leader's parsed
                // x59C anim buffer and relocates it by host address delta.
                // Native builds never materialize x59C: x14 keys the same
                // immutable GameData subarchive for both climbers, so the
                // ordinary parse branch below yields the identical shared
                // graph through the translation cache.
                // refs/melee/src/melee/ft/ftdata.c::ftData_80085CD8
                temp_r3_3 = NULL;
#endif
                if ((temp_r3_3 != NULL) &&
                    ((u32) temp_r3->x14 == (u32) temp_r3_3->x5A4))
                {
                    memcpy(fp->x59C, temp_r3_3->x59C, temp_r3->x8);
                    temp_r4 = fp->x59C;
                    temp_ret = lbArchiveRelocate(
                        &sp14, temp_r4->x0, temp_r3->x8,
                        (intptr_t) temp_r4 - (intptr_t) temp_r3_3->x59C);
                    if (temp_ret == -1) {
                        HSD_ASSERTREPORT(
                            0x9FA, 0, "lbArchiveRelocate error! %x\n", msid);
                    }
                } else {
                    temp_r4_2 = temp_r3->x14;
#ifdef MSL_CORE_NATIVE
                    // The loaded PlFxAJ.dat subarchive is immutable and is the
                    // initialization-cache key for native graph translation.
                    // x14 carries its one-based GameData arena token
                    // (lbFile_800168A0 source + source offset).
                    // refs/melee/src/melee/ft/ftdata.c::ftData_80085A14
                    temp_ret_2 = HSD_ArchiveParse(
                        &sp14,
                        msl_memory_from_token(msl_core_game_memory_context(),
                                              temp_r4_2),
                        temp_r3->x8);
#else
#ifdef MSL_CORE_HOSTED
                    memcpy(fp->x59C, (void*) temp_r4_2, temp_r3->x8);
#else
                    if (temp_r4_2 < 0x80000000) {
                        lbArq_80014BD0(temp_r4_2, fp->x59C,
                                       OSRoundUp32B(temp_r3->x8), 0, 0);
                    } else {
                        memcpy(fp->x59C, (void*) temp_r4_2, temp_r3->x8);
                    }
#endif
                    temp_ret_2 =
                        HSD_ArchiveParse(&sp14, fp->x59C->x0, temp_r3->x8);
#endif
                    if (temp_ret_2 == -1) {
                        HSD_ASSERTREPORT(0xA0F, 0,
                                         "HSD_ArchiveParse error! %x\n", msid);
                    }
                }
                fp->x590 = HSD_ArchiveGetPublicAddress(&sp14, temp_r3->x0);
            } else {
                fp->x590 = NULL;
            }
            fp->x5A4 = (void*) temp_r3->x14;
        }
    }
#endif
}

FigaTree* ftData_80085E50(Fighter* arg0, int msid)
{
#ifdef MSL_CORE_NATIVE
    if (msid < arg0->x58C) {
        struct Fighter_WaitAnimData* motion =
            ftData_80085FD4(arg0, msid);
        u32 token = motion->x14;

        if (token != (u32) arg0->x5A8) {
            arg0->x598 = token != 0 ? native_motion_tree(motion) : NULL;
            arg0->x5A8 = (void*) (uintptr_t) token;
        }
        return arg0->x598;
    }
    return NULL;
#else
    HSD_Archive sp10;
    Fighter* temp_r3_3;
    int temp_ret;
    int temp_ret_2;
    struct Fighter_x59C_t* temp_r4;
    struct Fighter_WaitAnimData* temp_r3;
    u32 temp_r3_2;
    u32 temp_r4_2;

    if (msid < arg0->x58C) {
        temp_r3 = ftData_80085FD4(arg0, msid);
        temp_r3_2 = temp_r3->x14;
        if (temp_r3_2 != (u32) arg0->x5A8) {
            if (temp_r3_2 != 0) {
                temp_r3_3 = ftData_80086060(arg0);
#ifdef MSL_CORE_NATIVE
                // Same native x59C exclusion as ftData_80085CD8 above.
                temp_r3_3 = NULL;
#endif
                if ((temp_r3_3 != NULL) &&
                    ((u32) temp_r3->x14 == (u32) temp_r3_3->x5A4))
                {
                    memcpy(arg0->x59C, temp_r3_3->x59C, temp_r3->x8);
                    temp_r4 = arg0->x59C;
                    temp_ret = lbArchiveRelocate(
                        &sp10, temp_r4->x0, temp_r3->x8,
                        (intptr_t) temp_r4 - (intptr_t) temp_r3_3->x59C);
                    if (temp_ret == -1) {
                        HSD_ASSERTREPORT(
                            0xA30, 0, "lbArchiveRelocate error! %x\n", msid);
                    }
                } else {
                    temp_r4_2 = temp_r3->x14;
#ifdef MSL_CORE_NATIVE
                    // Share the same immutable subarchive translation cache
                    // with ftData_80085CD8's primary animation owner.
                    // refs/melee/src/melee/ft/ftdata.c::ftData_80085A14
                    temp_ret_2 = HSD_ArchiveParse(
                        &sp10,
                        msl_memory_from_token(msl_core_game_memory_context(),
                                              temp_r4_2),
                        temp_r3->x8);
#else
#ifdef MSL_CORE_HOSTED
                    memcpy(arg0->x5A0, (void*) temp_r4_2, temp_r3->x8);
#else
                    if (temp_r4_2 < 0x80000000) {
                        lbArq_80014BD0(temp_r4_2, arg0->x5A0,
                                       OSRoundUp32B(temp_r3->x8), 0, 0);
                    } else {
                        memcpy(arg0->x5A0, (void*) temp_r4_2, temp_r3->x8);
                    }
#endif
                    temp_ret_2 =
                        HSD_ArchiveParse(&sp10, arg0->x5A0->x0, temp_r3->x8);
#endif
                    if (temp_ret_2 == -1) {
                        HSD_ASSERTREPORT(0xA45, 0,
                                         "HSD_ArchiveParse error! %x\n", msid);
                    }
                }
                arg0->x598 = HSD_ArchiveGetPublicAddress(&sp10, temp_r3->x0);
            } else {
                arg0->x598 = 0;
            }
            arg0->x5A8 = (void*) temp_r3->x14;
        }
        return arg0->x598;
    }
    return NULL;
#endif
}

struct Fighter_WaitAnimData* ftData_80085FD4(Fighter* fp, int msid)
{
    if (fp->kind == FTKIND_NANA &&
        Player_GetPlayerSlotType(fp->player_id) != Gm_PKind_Demo &&
        fp->x24[msid].x14 == 0)
    {
        return &gFtDataList[FTKIND_POPO]->xC[msid];
    }
    return &fp->x24[msid];
}

Fighter* ftData_80086060(Fighter* fp)
{
    if (fp->kind == FTKIND_NANA &&
        Player_GetPlayerSlotType(fp->player_id) != Gm_PKind_Demo)
    {
        Fighter_GObj* gobj = Player_GetEntityAtIndex(fp->player_id, 0);
        if (gobj != NULL) {
            return GET_FIGHTER(gobj);
        }
    }
    return NULL;
}
