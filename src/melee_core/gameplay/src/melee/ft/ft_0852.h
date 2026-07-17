#ifndef GALE01_08521C
#define GALE01_08521C

#include "ft/forward.h"

#include "ft/types.h"

typedef struct ft_8045993C_t {
    /* +0 */ u32 pad_x0;
    /* +4 */ u8 pad_x4[0x2];
    /* +6:0 */ u16 x6_b0 : 1;
    /* +6:1-2 */ u16 x6_b1_b2 : 2;
} ft_8045993C_t;

#ifdef MSL_CORE_HOSTED
#if defined(MSL_CORE_NATIVE) && !defined(MSL_CORE_CONTEXT_IMPLEMENTATION)
#include <runtime/context.h>
#define msl_core_fighter_data_list()                                          \
    ((ftData**) msl_core_context_fighter_data_list)
#define msl_core_fighter_state()                                              \
    ((ft_8045993C_t*) msl_core_context_fighter_state)
#define msl_core_fighter_reference_counts()                                   \
    (msl_core_context_fighter_reference_counts)
#else
ftData** msl_core_fighter_data_list(void);
ft_8045993C_t* msl_core_fighter_state(void);
int* msl_core_fighter_reference_counts(void);
#endif
#define gFtDataList (msl_core_fighter_data_list())
#define ft_8045993C (msl_core_fighter_state())
#define ft_8045996C (msl_core_fighter_reference_counts())
#else
extern ftData* gFtDataList[FTKIND_MAX];
extern ft_8045993C_t ft_8045993C[6];
extern int ft_8045996C[FTKIND_MAX];
#endif

/* 08521C */ void ft_8008521C(Fighter_GObj* gobj);
/* 0852B0 */ void ft_800852B0(void);
/* 08549C */ void ft_8008549C(void);

#endif
