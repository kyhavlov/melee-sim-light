#include "host/files.h"

#include "ft/fighter.h"
#include "gr/ground.h"
#include "lb/lbarchive.h"
#include "lb/lbspdisplay.h"
#include "mp/mpcoll.h"
#include "mp/mplib.h"
#include "mp/types.h"
#include "pl/player.h"
#include "pl/types.h"

#include <baselib/aobj.h>
#include <baselib/fobj.h>
#include <baselib/gobj.h>
#include <baselib/id.h>
#include <baselib/list.h>
#include <baselib/mtx.h>
#include <baselib/robj.h>

#include <stdio.h>

static void init_hsd(void)
{
    HSD_GObjLibInitDataType init;

    HSD_AObjInitAllocData();
    HSD_FObjInitAllocData();
    HSD_IDInitAllocData();
    HSD_ListInitAllocData();
    HSD_MtxInitAllocData();
    HSD_VecInitAllocData();
    HSD_RObjInitAllocData();
    HSD_GObj_803912E0(&init);
    init.gproc_pri_max = 0x18;
    HSD_GObj_80391304(&init);
}

int main(int argc, char** argv)
{
    HSD_Archive* stage_archive;
    MapCollData* coll_data = NULL;
    UnkStage6B0* ground_param = NULL;
    struct plAllocInfo alloc = { 0 };
    Fighter_GObj* fighters[2];
    Vec3 spawns[2] = {
        { -60.0F, 10.0F, 0.0F },
        { 60.0F, 10.0F, 0.0F },
    };
    int i;

    if (argc != 2) {
        fprintf(stderr, "usage: %s GAME_DATA_DIRECTORY\n", argv[0]);
        return 2;
    }
    msl_host_set_data_root(argv[1]);
    init_hsd();
    // Source match bootstrap initializes the fixed fighter-dynamics pool
    // before constructing any fighters (refs/melee/src/melee/gm/gm_1832.c).
    lb_8000FCDC();
    stage_archive = lbArchive_LoadSymbols(
        "GrNLa.dat", (void**) &coll_data, "coll_data",
        (void**) &ground_param, "grGroundParam", NULL);
    if (stage_archive == NULL || coll_data == NULL || ground_param == NULL) {
        fprintf(stderr, "failed to load Final Destination data\n");
        return 1;
    }
    stage_info.coll_data = coll_data;
    stage_info.param = ground_param;
    stage_info.internal_stage_id = LAST;
    mpColl_80041C78();
    mpLibLoad(stage_info.coll_data);
    for (i = 0; i < 2; ++i) {
        Player_InitOrResetPlayer(i);
        Player_SetPlayerCharacter(i, CKIND_FOX);
        Player_SetSlottype(i, Gm_PKind_Human);
        Player_SetFacingDirection(i, i == 0 ? 1.0F : -1.0F);
        Player_SetControllerIndex(i, i + 1);
        Player_80032768(i, &spawns[i]);
    }
    Fighter_FirstInitialize_80067A84();

    alloc.x5 = -1;
    for (i = 0; i < 2; ++i) {
        alloc.internal_id = FTKIND_FOX;
        alloc.slot = i;
        fighters[i] = Fighter_Create(&alloc);
        if (fighters[i] == NULL) {
            fprintf(stderr, "source Fighter_Create returned NULL for slot %d\n",
                    i);
            return 1;
        }
    }
    HSD_GObj_80390CFC();
    printf("two source Fox fighters booted and advanced one neutral frame\n");
    return 0;
}
