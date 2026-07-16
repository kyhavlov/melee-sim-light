#include "smoke_context.h"

#include "baselib/gobj.h"
#include "baselib/gobjplink.h"
#include "baselib/gobjproc.h"

#include <stdio.h>
#include <string.h>

static char calls[16];
static size_t call_count;

static void record_a(HSD_GObj* gobj)
{
    (void) gobj;
    calls[call_count++] = 'A';
}

static void record_b(HSD_GObj* gobj)
{
    (void) gobj;
    calls[call_count++] = 'B';
}

static void record_c(HSD_GObj* gobj)
{
    (void) gobj;
    calls[call_count++] = 'C';
}

int main(void)
{
    static MslSmokeContext context;
    HSD_GObjLibInitDataType init;
    HSD_GObj* first;
    HSD_GObj* second;

    if (msl_smoke_context_init(&context, NULL) != 0) {
        return 1;
    }
    HSD_GObj_803912E0(&init);
    init.gproc_pri_max = 0x18;
    HSD_GObj_80391304(&init);

    first = GObj_Create(1, 8, 0);
    second = GObj_Create(2, 8, 0);
    HSD_GObj_SetupProc(first, record_b, 4);
    HSD_GObj_SetupProc(first, record_c, 6);
    HSD_GObj_SetupProc(second, record_a, 4);

    HSD_GObj_80390CFC();
    calls[call_count] = '\0';
    if (strcmp(calls, "BAC") != 0) {
        fprintf(stderr, "unexpected source scheduler order: %s\n", calls);
        return 1;
    }
    printf("source scheduler order: %s\n", calls);
    msl_smoke_context_destroy(&context);
    return 0;
}
