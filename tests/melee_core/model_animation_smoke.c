#include "smoke_context.h"

#include "lb/lbarchive.h"
#include "platform/files.h"

#include <stdio.h>
#include <baselib/aobj.h>
#include <baselib/fobj.h>
#include <baselib/id.h>
#include <baselib/jobj.h>
#include <baselib/list.h>
#include <baselib/mobj.h>
#include <baselib/mtx.h>
#include <baselib/robj.h>

static int count_joints(HSD_JObj* jobj)
{
    int count = 0;
    for (; jobj != NULL; jobj = jobj->next) {
        count += 1;
        if (!(jobj->flags & JOBJ_INSTANCE)) {
            count += count_joints(jobj->child);
        }
    }
    return count;
}

int main(int argc, char** argv)
{
    static MslSmokeContext context;
    HSD_Joint* joint_desc = NULL;
    HSD_MatAnimJoint* matanim_desc = NULL;
    HSD_Archive* archive;
    HSD_JObj* jobj;
    int joint_count;

    if (argc != 2) {
        fprintf(stderr, "usage: %s GAME_DATA_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (msl_smoke_context_init(&context, argv[1]) != 0) {
        return 1;
    }
    HSD_AObjInitAllocData();
    HSD_FObjInitAllocData();
    HSD_IDInitAllocData();
    HSD_ListInitAllocData();
    HSD_MtxInitAllocData();
    HSD_VecInitAllocData();
    HSD_RObjInitAllocData();
    archive = lbArchive_LoadSymbols(
        "PlFxNr.dat", (void**) &joint_desc, "PlyFox5K_Share_joint",
        (void**) &matanim_desc, "PlyFox5K_Share_matanim_joint", NULL);
    if (archive == NULL || joint_desc == NULL || matanim_desc == NULL) {
        fprintf(stderr, "failed to load Fox model descriptors\n");
        return 1;
    }

    jobj = HSD_JObjLoadJoint(joint_desc);
    if (jobj == NULL || (joint_count = count_joints(jobj)) < 2) {
        fprintf(stderr, "failed to instantiate Fox JObj tree\n");
        return 1;
    }
    HSD_JObjAddAnimAll(jobj, NULL, matanim_desc, NULL);
    HSD_JObjReqAnimAll(jobj, 0.0F);
    HSD_JObjAnimAll(jobj);

    printf("source Fox model/animation: %d JObjs\n", joint_count);
    msl_smoke_context_destroy(&context);
    return 0;
}
