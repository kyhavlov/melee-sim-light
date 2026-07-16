#ifndef GALE01_IT_3F14
#define GALE01_IT_3F14

#include <platform.h>

#include "ft/forward.h"
#include "it/forward.h"

#include "it/items/types.h"
#include "it/types.h"

typedef struct it_804D6D40_t {
    /* 0x00 */ s32 x0;
    /* 0x04 */ f32 x4;
    /* 0x08 */ f32 x8;
    /* 0x0C */ f32 xC;
    /* 0x10 */ f32 x10;
    /* 0x14 */ f32 x14;
    /* 0x18 */ f32 x18;
} it_804D6D40_t;

typedef struct it_804D6D20_t {
    ItemCommonData* x0;
    Article** x4;
    Article** x8;
    Article** xC;
    it_804D6D40_t* x10;
    Fighter_804D653C_t* x14;
} it_804D6D20_t;

#ifdef MSL_CORE_HOSTED
typedef struct MslItemGameData {
    Fighter_804D653C_t* x04;
    it_804D6D20_t* x20;
    Article** x24;
    ItemCommonData* x28;
    Article** x30;
    Article** x38;
    it_804D6D40_t* x40;
    Article* character_articles[118];
} MslItemGameData;

typedef struct MslItemState {
    HSD_ObjAllocUnk alloc_state;
    Item_FtTrack fighter_track;
    S32Vec3 item_position;
    RandomItemSpawner random_spawner;
    ItemPickTable pick_table0;
    ItemPickTable pick_table1;
    DamageLogEntry damage_log[15];
    Article* stage_articles[30];
    s8 x00;
    s32 x08;
    s32 x0C;
    u32 x10;
    u32 x14;
    u32 x18;
    u8 x1C[4];
} MslItemState;

MslItemGameData* msl_core_item_game_data(void);
MslItemState* msl_core_item_state(void);
#endif

/* 3F1418 */ extern struct sdata_ItemGXLink it_803F1418[43];
/* 3F14C4 */ extern struct ItemLogicTable it_803F14C4[43];
/* 3F1ED8 */ extern char it_803F1ED8[];
/* 3F1EE4 */ extern char it_803F1EE4[];
/* 3F1EF0 */ extern char it_803F1EF0[];
#ifndef MSL_CORE_HOSTED
/* 4A0E30 */ extern RandomItemSpawner it_804A0E30;
/* 4A0E50 */ extern ItemPickTable it_804A0E50;
/* 4A0E60 */ extern ItemPickTable it_804A0E60;
/* 4A0E70 */ extern DamageLogEntry it_804A0E70[15];
/* 4A0F60 */ extern Article* it_804A0F60[30];
/* 4D6D00 */ extern s8 it_804D6D00;
/* 4D6D04 */ extern Fighter_804D653C_t* it_804D6D04;
/* 4D6D08 */ extern s32 it_804D6D08;
/* 4D6D0C */ extern s32 it_804D6D0C;
/* 4D6D10 */ extern u32 it_804D6D10;
/* 4D6D14 */ extern u32 it_804D6D14;
/* 4D6D18 */ extern u32 it_804D6D18;
/* 4D6D1C */ extern u8 it_804D6D1C[4];
/* 4D6D20 */ extern it_804D6D20_t* it_804D6D20;
/* 4D6D24 */ extern Article** it_804D6D24;
/* 4D6D28 */ extern ItemCommonData* it_804D6D28;
/* 4D6D30 */ extern Article** it_804D6D30;
/* 4D6D38 */ extern Article** it_804D6D38;
/* 4D6D40 */ extern it_804D6D40_t* it_804D6D40;
#else
#define it_804A0E30 (msl_core_item_state()->random_spawner)
#define it_804A0E50 (msl_core_item_state()->pick_table0)
#define it_804A0E60 (msl_core_item_state()->pick_table1)
#define it_804A0E70 (msl_core_item_state()->damage_log)
#define it_804A0F60 (msl_core_item_state()->stage_articles)
#define it_804D6D00 (msl_core_item_state()->x00)
#define it_804D6D08 (msl_core_item_state()->x08)
#define it_804D6D0C (msl_core_item_state()->x0C)
#define it_804D6D10 (msl_core_item_state()->x10)
#define it_804D6D14 (msl_core_item_state()->x14)
#define it_804D6D18 (msl_core_item_state()->x18)
#define it_804D6D1C (msl_core_item_state()->x1C)
#define it_804D6D04 (msl_core_item_game_data()->x04)
#define it_804D6D20 (msl_core_item_game_data()->x20)
#define it_804D6D24 (msl_core_item_game_data()->x24)
#define it_804D6D28 (msl_core_item_game_data()->x28)
#define it_804D6D30 (msl_core_item_game_data()->x30)
#define it_804D6D38 (msl_core_item_game_data()->x38)
#define it_804D6D40 (msl_core_item_game_data()->x40)
#endif

#endif
