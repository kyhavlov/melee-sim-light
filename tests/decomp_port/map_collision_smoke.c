#include "host/files.h"

#include "gr/ground.h"
#include "lb/lbarchive.h"
#include "mp/mpcoll.h"
#include "mp/mplib.h"
#include "mp/types.h"

#include <math.h>
#include <stdio.h>

int main(int argc, char** argv)
{
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

    msl_host_set_data_root(argv[1]);
    archive = lbArchive_LoadSymbols("GrNLa.dat", (void**) &coll_data,
                                    "coll_data", (void**) &ground_param,
                                    "grGroundParam", NULL);
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
    return 0;
}
