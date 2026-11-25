#include "smoke_context.h"

#include "gr/ground.h"
#include "lb/lbarchive.h"
#include "mp/mpcoll.h"
#include "mp/mplib.h"
#include "mp/types.h"
#include "platform/files.h"

#include <math.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    static MslSmokeContext context;
    static const float spawn_x[] = { -60.0F, 0.0F, 60.0F };
    HSD_Archive* archive;
    MapCollData* coll_data = NULL;
    UnkStage6B0* ground_param = NULL;
    Vec3 contact = { 0 };
    Vec3 normal = { 0 };
    int line_id = -1;
    int i;
    u32 flags = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s GAME_DATA_DIRECTORY\n", argv[0]);
        return 2;
    }

    if (msl_smoke_context_init(&context, argv[1]) != 0) {
        return 1;
    }
    archive =
        lbArchive_LoadSymbols("GrNLa.dat", (void**) &coll_data, "coll_data",
                              (void**) &ground_param, "grGroundParam", NULL);
    if (archive == NULL || coll_data == NULL || ground_param == NULL) {
        fprintf(stderr, "failed to load Final Destination collision data\n");
        return 1;
    }

    stage_info.coll_data = coll_data;
    stage_info.param = ground_param;
    stage_info.internal_stage_id = LAST;
    fprintf(stderr,
            "FD data: data=%p reloc=%p coll=%p verts=%p lines=%p joints=%p "
            "verts=%d lines=%d joints=%d floor=%d@%d scale=%g\n",
            (void*) archive->data, (void*) archive->reloc_info,
            (void*) coll_data, (void*) coll_data->verts,
            (void*) coll_data->lines, (void*) coll_data->joints,
            coll_data->vert_count, coll_data->line_count,
            coll_data->joint_count, coll_data->floor_count,
            coll_data->floor_start, ground_param->x0);
    fflush(stderr);
    mpColl_80041C78();
    fprintf(stderr, "mpColl initialized\n");
    fflush(stderr);
    mpLibLoad(stage_info.coll_data);
    fprintf(stderr, "mpLib initialized\n");
    fflush(stderr);

    for (i = 0; i < (int) (sizeof(spawn_x) / sizeof(spawn_x[0])); ++i) {
        if (!mpCheckFloor(spawn_x[i], 50.0F, spawn_x[i], -50.0F, 0.0F,
                          &contact, &line_id, &flags, &normal, -1, -1, -1,
                          NULL, NULL))
        {
            fprintf(stderr,
                    "source collision did not find FD floor at x=%.3f\n",
                    spawn_x[i]);
            return 1;
        }
        if (fabsf(contact.x - spawn_x[i]) > 0.001F ||
            fabsf(contact.y) > 0.001F || normal.y < 0.99F)
        {
            fprintf(stderr,
                    "unexpected FD floor contact: line=%d pos=(%.6f,%.6f) "
                    "normal=(%.6f,%.6f)\n",
                    line_id, contact.x, contact.y, normal.x, normal.y);
            return 1;
        }

        printf("source FD floor: line=%d pos=(%.3f,%.3f) "
               "normal=(%.3f,%.3f)\n",
               line_id, contact.x, contact.y, normal.x, normal.y);
    }
#ifdef MSL_CORE_NATIVE
    // A bound collision transform need not move. Its stationary walls can
    // reject a distant ECB; moving and degenerate walls must remain admitted.
    {
        CollJoint* joint = mpGetGroundCollJoint();
        CollLine* line = &mpGetGroundCollLine()[joint->inner->left_wall_start];
        CollVtx* vertices = mpGetGroundCollVtx();
        CollVtx* v0 = &vertices[line->x0->v0_idx];
        CollVtx* v1 = &vertices[line->x0->v1_idx];
        CollVtx saved;
        if (joint->inner->left_wall_count == 0) return 1;
        for (i = 0; i < coll_data->vert_count; ++i) {
            vertices[i].x10 = vertices[i].pos.x;
            vertices[i].x14 = vertices[i].pos.y;
        }
        joint->flags |= CollJoint_B8;
        if (mpLib_LineBroadphase(-0.0F, 20, 0.0F, 30,
                                 CollLine_LeftWall, -1, -1) ||
            !mpLib_LineBroadphase(fminf(v0->pos.x, v1->pos.x) - 1,
                                  fminf(v0->pos.y, v1->pos.y) - 1,
                                  fmaxf(v0->pos.x, v1->pos.x) + 1,
                                  fmaxf(v0->pos.y, v1->pos.y) + 1,
                                  CollLine_LeftWall, -1, -1))
        {
            fprintf(stderr, "stationary transformed-wall broad phase failed\n");
            return 1;
        }
        v0->x10 += 1;
        if (!mpLib_LineBroadphase(-1, 20, 1, 30,
                                  CollLine_LeftWall, -1, -1))
        {
            fprintf(stderr, "moving wall was incorrectly rejected\n");
            return 1;
        }
        v0->x10 = v0->pos.x;
        saved = *v1;
        v1->pos = v0->pos;
        v1->x10 = v0->x10;
        v1->x14 = v0->x14;
        if (!mpLib_LineBroadphase(-1, 20, 1, 30,
                                  CollLine_LeftWall, -1, -1))
        {
            fprintf(stderr, "degenerate wall was incorrectly rejected\n");
            return 1;
        }
        *v1 = saved;
    }
#endif
    msl_smoke_context_destroy(&context);
    return 0;
}
