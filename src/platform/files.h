#ifndef MSL_CORE_PLATFORM_FILES_H
#define MSL_CORE_PLATFORM_FILES_H

#include <stddef.h>
#include <stdint.h>

typedef struct HSD_Archive HSD_Archive;

enum {
    MSL_FILE_PATH_CAPACITY = 1024,
    MSL_FILE_BASENAME_CAPACITY = 96,
    MSL_FILE_CACHE_CAPACITY = 512,
};

typedef struct MslRawFileEntry {
    char basename[MSL_FILE_BASENAME_CAPACITY];
    uint8_t* data;
    uint32_t size;
} MslRawFileEntry;

typedef struct MslArchiveEntry {
    char basename[MSL_FILE_BASENAME_CAPACITY];
    HSD_Archive* archive;
} MslArchiveEntry;

typedef struct MslFileContext {
    char root[MSL_FILE_PATH_CAPACITY];
    MslRawFileEntry raw[MSL_FILE_CACHE_CAPACITY];
    MslArchiveEntry archives[MSL_FILE_CACHE_CAPACITY];
    uint32_t raw_count;
    uint32_t archive_count;
    uint8_t sealed;
} MslFileContext;

void msl_host_set_data_root(const char* path);
HSD_Archive* msl_host_archive_find(const char* basename);
void msl_host_archive_store(const char* basename, HSD_Archive* archive);
void msl_host_finish_initialization(void);

#endif
