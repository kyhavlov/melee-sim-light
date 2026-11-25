#include "smoke_context.h"

#include "ft/types.h"
#include "lb/lbarchive.h"
#include "platform/files.h"

#include <stdio.h>
#include <string.h>

static int load_symbol(const char* file, const char* name)
{
    HSD_Archive* archive;
    void* symbol;

    archive = lbArchive_LoadSymbols(file, &symbol, name, NULL);
    if (archive == NULL || symbol == NULL) {
        fprintf(stderr, "%s: failed to load %s\n", file, name);
        return 1;
    }
    printf("%s: loaded %s\n", file, name);
    return 0;
}

static int msl_test_dynamics_counts(void)
{
    // PlPe.dat stores one count per chain, including zero and 0x100.
    static const s32 expected[] = { 1, 1, 0x100, 0x100, 0x100,
                                    0x100, 0x100, 0x100, 0 };
    void* symbol = NULL;
    if (lbArchive_LoadSymbols("PlPe.dat", &symbol, "ftDataPeach", NULL) == NULL ||
        symbol == NULL)
    {
        fprintf(stderr, "PlPe.dat: failed to load ftDataPeach\n");
        return 1;
    }
    const ftData* fighter = symbol;
    const ftDynamics* dynamics = fighter->x2C;
    if (dynamics == NULL || dynamics->dynamicsNum != 9 ||
        dynamics->x10 == NULL || dynamics->x10[0] == NULL ||
        memcmp(dynamics->x10[0], expected, sizeof(expected)) != 0)
    {
        fprintf(stderr, "PlPe.dat: incorrect animation joint counts\n");
        return 1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    static MslSmokeContext context;
    int result;

    if (argc != 2) {
        fprintf(stderr, "usage: %s GAME_DATA_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (msl_smoke_context_init(&context, argv[1]) != 0) {
        return 1;
    }
    result = load_symbol("PlCo.dat", "ftLoadCommonData") |
             load_symbol("PlFx.dat", "ftDataFox") |
             load_symbol("PlFxNr.dat", "PlyFox5K_Share_joint") |
             load_symbol("GrNLa.dat", "coll_data") |
             load_symbol("GrNLa.dat", "map_head") | msl_test_dynamics_counts();
    msl_smoke_context_destroy(&context);
    return result;
}
