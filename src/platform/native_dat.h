#ifndef MSL_CORE_NATIVE_DAT_H
#define MSL_CORE_NATIVE_DAT_H

#include <baselib/forward.h>


#include <stddef.h>
#include <stdint.h>
#include <baselib/archive.h>

typedef struct _HSD_PSCmdList HSD_PSCmdList;
typedef struct EF_EffectDesc EF_EffectDesc;
typedef struct FigaTree FigaTree;

enum {
    MSL_NATIVE_ARCHIVE_CACHE_CAPACITY = 2048,
    MSL_NATIVE_DAT_ARENA_BYTES = 256 * 1024 * 1024,
};

typedef struct MslNativeArchiveCacheEntry {
    const uint8_t* source;
    size_t file_size;
    HSD_Archive archive;
} MslNativeArchiveCacheEntry;

typedef struct MslNativeDatContext {
    MslNativeArchiveCacheEntry
        archive_cache[MSL_NATIVE_ARCHIVE_CACHE_CAPACITY];
    uint32_t archive_cache_count;
    uint8_t* arena;
    size_t arena_used;
    uint8_t initialization_complete;
} MslNativeDatContext;

typedef enum MslDatKind {
    MSL_DAT_VOID,
    MSL_DAT_BASE,
    MSL_DAT_STRUCT,
    MSL_DAT_UNION,
    MSL_DAT_POINTER,
    MSL_DAT_ARRAY,
    MSL_DAT_FUNCTION,
    MSL_DAT_BITFIELD,
} MslDatKind;

typedef struct MslDatType MslDatType;

typedef struct MslDatField {
    uint32_t source_offset;
    uint32_t native_offset;
    const MslDatType* type;
} MslDatField;

struct MslDatType {
    MslDatKind kind;
    uint32_t source_size;
    uint32_t native_size;
    uint32_t count;
    uint32_t field_count;
    const MslDatField* fields;
    const MslDatType* element;
    uint32_t source_bit_offset;
    uint32_t native_bit_offset;
    uint32_t bit_size;
    const char* name;
    uint32_t base_encoding;
};

extern const MslDatType* const msl_dat_root_HSD_PSCmdList;
extern const MslDatType* const msl_dat_root_UnkStageDat;
extern const MslDatType* const msl_dat_root_MapCollData;
extern const MslDatType* const msl_dat_root_UnkStage6B0;
extern const MslDatType* const msl_dat_root_DynamicModelDesc;
extern const MslDatType* const msl_dat_root_HSD_Joint;
extern const MslDatType* const msl_dat_root_HSD_Spline;
extern const MslDatType* const msl_dat_root_HSD_MatAnimJoint;
extern const MslDatType* const msl_dat_root_EF_EffectDesc;
extern const MslDatType* const msl_dat_root_ftData;
extern const MslDatType* const msl_dat_root_FigaTree;
extern const MslDatType* const msl_dat_root_it_804D6D20_t;
extern const MslDatType* const msl_dat_root_ItemCommonData;
extern const MslDatType* const msl_dat_root_it_804D6D40_t;
extern const MslDatType* const msl_dat_root_Fighter_804D653C_t;
extern const MslDatType* const msl_dat_root_pl_804D6470_t;
extern const MslDatType* const msl_dat_root_ftCommonData;
extern const MslDatType* const msl_dat_root_MslDatIntPointer;
extern const MslDatType* const msl_dat_root_MslDatItemThrowAttrs;
extern const MslDatType* const msl_dat_root_MslDatFloat;
extern const MslDatType* const msl_dat_root_MslDatFloat5;
extern const MslDatType* const msl_dat_root_MslDatByte;
extern const MslDatType* const msl_dat_root_MslDatVec2Pointer;
extern const MslDatType* const msl_dat_root_MslDatFighterPartsPointer;
extern const MslDatType* const msl_dat_root_MslDatFighter6540Pointer;
extern const MslDatType* const msl_dat_root_MslDatBattlefieldParams;
extern const MslDatType* const msl_dat_root_MslDatPokemonStadiumParams;
extern const MslDatType* const msl_dat_root_MslDatFountainParams;
extern const MslDatType* const msl_dat_root_MslDatYoshisStoryParams;
extern const MslDatType* const msl_dat_root_MslDatDreamLandParams;
extern const MslDatType* const msl_dat_root_MslDatStageItemEntry;
extern const MslDatType* const msl_dat_root_MslDatHeihoAttrs;
extern const MslDatType* const msl_dat_root_Fighter_WaitAnimData;
extern const MslDatType* const msl_dat_root_MslDatAnimBytePair;
extern const MslDatType* const msl_dat_root_MslDatSpaceAnimalArticles;
extern const MslDatType* const msl_dat_root_MslDatSheikArticles;
extern const MslDatType* const msl_dat_root_MslDatPeachArticles;
extern const MslDatType* const msl_dat_root_MslDatZeldaArticles;
extern const MslDatType* const msl_dat_root_MslDatLuigiArticles;
extern const MslDatType* const msl_dat_root_MslDatCommonItemArticles;
extern const MslDatType* const msl_dat_root_itBombHeiAttributes;
extern const MslDatType* const msl_dat_root_itDoseiAttributes;
extern const MslDatType* const msl_dat_root_itSword_UnkArticle1;
extern const MslDatType* const msl_dat_root_MslDatPurinAuxList;
extern const MslDatType* const msl_dat_root_ftFox_DatAttrs;
extern const MslDatType* const msl_dat_root_ftCaptain_DatAttrs;
extern const MslDatType* const msl_dat_root_MarsAttributes;
extern const MslDatType* const msl_dat_root_ftPe_DatAttrs;
extern const MslDatType* const msl_dat_root_ftSeakAttributes;
extern const MslDatType* const msl_dat_root_ftZelda_DatAttrs;
extern const MslDatType* const msl_dat_root_ftPurinAttributes;
extern const MslDatType* const msl_dat_root_ftLuigiAttributes;
extern const MslDatType* const msl_dat_root_FoxLaserAttr;
extern const MslDatType* const msl_dat_root_FoxBlasterAttr;
extern const MslDatType* const msl_dat_root_FoxIllusionAttr;
extern const MslDatType* const msl_dat_root_itSeakNeedleThrownAttributes;
extern const MslDatType* const msl_dat_root_itSeakChain_Attrs;
extern const MslDatType* const msl_dat_root_MslDatPeachTurnipAttrs;
extern const MslDatType* const msl_dat_root_itPeachToadSporeAttributes;
extern const MslDatType* const msl_dat_root_MslDatZeldaDinFireAttrs;
extern const MslDatType* const msl_dat_root_itZeldaDinFireExplodeAttributes;
extern const MslDatType* const msl_dat_root_itUnkAttributes;
extern const MslDatType* const msl_dat_root_Fighter_804D6518_t;
extern const MslDatType* const msl_dat_root_Fighter_804D651C_t;
extern const MslDatType* const msl_dat_root_Fighter_804D6520_t;
extern const MslDatType* const msl_dat_root_Fighter_804D6524_t;
extern const MslDatType* const msl_dat_root_Fighter_804D6528_t;
extern const MslDatType* const msl_dat_root_CrowdConfig;
extern const MslDatType* const msl_dat_root_Fighter_804D64FC_t;
extern const MslDatType* const msl_dat_root_Fighter_804D6534_t;

int msl_native_archive_parse(HSD_Archive* archive, uint8_t* source,
                             size_t file_size);
void* msl_native_archive_get_public(HSD_Archive* archive, const char* symbol);
char* msl_native_archive_get_extern(HSD_Archive* archive, int index);
void msl_native_archive_locate_extern(HSD_Archive* archive,
                                      const char* symbol, void* address);

int msl_native_effect_bank(HSD_Archive* archive, const char* symbol,
                           int* count, HSD_PSCmdList*** commands);
EF_EffectDesc* msl_native_effect_models(HSD_Archive* archive,
                                        const char* symbol, int count);
void msl_native_dat_finish_initialization(void);
int msl_native_dat_owns(const void* pointer);
void msl_native_dat_context_destroy(MslNativeDatContext* context);
void msl_native_dat_for_each_figa(void (*visit)(FigaTree*, void*),
                                  void* context);

#endif
