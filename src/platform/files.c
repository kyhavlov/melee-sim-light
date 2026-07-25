#include "files.h"

#include <platform.h>

#include "baselib/archive.h"
#include "baselib/memory.h"
#include "platform/memory.h"
#include "runtime/context.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MslRawFileEntry* raw_file_find(const char* basename)
{
    MslFileContext* context = msl_core_file_context();
    uint32_t i;
    for (i = 0; i < context->raw_count; ++i) {
        if (strcmp(context->raw[i].basename, basename) == 0) {
            return &context->raw[i];
        }
    }
    return NULL;
}

static void cache_name(char dst[MSL_FILE_BASENAME_CAPACITY],
                       const char* basename)
{
    size_t length = strlen(basename);
    if (length >= MSL_FILE_BASENAME_CAPACITY) {
        abort();
    }
    memcpy(dst, basename, length + 1);
}

void msl_host_set_data_root(const char* path)
{
    if (path != NULL && path[0] != '\0') {
        MslFileContext* context = msl_core_file_context();
        size_t length = strlen(path);
        if (length >= sizeof(context->root)) {
            abort();
        }
        memcpy(context->root, path, length + 1);
    }
}

static void make_path(char* path, size_t capacity, const char* basename)
{
    while (*basename == '/') {
        basename++;
    }
    const char* data_root = msl_core_file_context()->root;
    if (snprintf(path, capacity, "%s/%s", data_root, basename) >=
        (int) capacity)
    {
        fprintf(stderr, "game-data path is too long: %s/%s\n", data_root,
                basename);
        abort();
    }
}

s32 lbFile_800163D8(const char* basename)
{
#ifdef MSL_CORE_NATIVE
    MslRawFileEntry* cached = raw_file_find(basename);
    if (cached != NULL) {
        return (s32) cached->size;
    }
    if (msl_core_file_context()->sealed) {
        fprintf(stderr, "game-file lookup after GameData initialization: %s\n",
                basename);
        abort();
    }
#endif
    char path[1024];
    FILE* file;
    long size;

    make_path(path, sizeof(path), basename);
    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        abort();
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
        fprintf(stderr, "%s: could not determine file size\n", path);
        abort();
    }
    fclose(file);
    return (s32) size;
}

void lbFile_8001668C(const char* basename, u32* destination, u32* length)
{
#ifdef MSL_CORE_NATIVE
    MslRawFileEntry* cached = raw_file_find(basename);
    if (cached != NULL) {
        memcpy(destination, cached->data, cached->size);
        *length = cached->size;
        return;
    }
    if (msl_core_file_context()->sealed) {
        fprintf(stderr, "game-file read after GameData initialization: %s\n",
                basename);
        abort();
    }
#endif
    char path[1024];
    FILE* file;
    size_t size;

    make_path(path, sizeof(path), basename);
    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        abort();
    }
    size = (size_t) lbFile_800163D8(basename);
    if (fread(destination, 1, size, file) != size) {
        fprintf(stderr, "%s: short read\n", path);
        abort();
    }
    fclose(file);
    *length = (u32) size;
#ifdef MSL_CORE_NATIVE
    if (raw_file_find(basename) == NULL &&
        msl_memory_context_owns(msl_core_game_memory_context(), destination))
    {
        MslFileContext* context = msl_core_file_context();
        MslRawFileEntry* entry;
        if (context->raw_count == MSL_FILE_CACHE_CAPACITY) {
            abort();
        }
        entry = &context->raw[context->raw_count++];
        cache_name(entry->basename, basename);
        entry->data = (uint8_t*) destination;
        entry->size = (uint32_t) size;
    }
#endif
}

bool lbFile_800168A0(s32 heap, const char* basename, u32* source, u32* length)
{
#ifdef MSL_CORE_NATIVE
    MslRawFileEntry* cached = raw_file_find(basename);
    if (cached != NULL) {
        *source = (u32) (uintptr_t) cached->data;
        *length = cached->size;
        return true;
    }
#endif
    size_t size = (size_t) lbFile_800163D8(basename);
    void* data;
#ifdef MSL_CORE_NATIVE
    data = msl_memory_alloc(msl_core_game_memory_context(), size);
#else
    data = HSD_MemAlloc((ssize_t) size);
#endif
    (void) heap;
    lbFile_8001668C(basename, data, length);
    // Truncated low 32 bits of the GameData arena address; decoded back
    // via msl_memory_from_low32.
    *source = (u32) (uintptr_t) data;
    return true;
}

void* lbHeap_80015BD0(int heap, int size)
{
    (void) heap;
#ifdef MSL_CORE_NATIVE
    return size > 0 ? msl_memory_alloc(msl_core_game_memory_context(), size)
                    : NULL;
#else
    return HSD_MemAlloc(size);
#endif
}

void lbHeap_80015CA8(int heap, void* ptr)
{
    (void) heap;
    HSD_Free(ptr);
}

HSD_Archive* lbDvd_8001819C(const char* basename)
{
    return msl_host_archive_find(basename);
}

HSD_Archive* msl_host_archive_find(const char* basename)
{
#ifdef MSL_CORE_NATIVE
    MslFileContext* context = msl_core_file_context();
    uint32_t i;
    for (i = 0; i < context->archive_count; ++i) {
        if (strcmp(context->archives[i].basename, basename) == 0) {
            return context->archives[i].archive;
        }
    }
#else
    (void) basename;
#endif
    return NULL;
}

void msl_host_archive_store(const char* basename, HSD_Archive* archive)
{
#ifdef MSL_CORE_NATIVE
    MslFileContext* context = msl_core_file_context();
    MslArchiveEntry* entry;
    if (msl_host_archive_find(basename) != NULL) {
        return;
    }
    if (context->sealed) {
        fprintf(stderr,
                "archive publication after GameData initialization: %s\n",
                basename);
        abort();
    }
    if (context->archive_count == MSL_FILE_CACHE_CAPACITY) {
        abort();
    }
    entry = &context->archives[context->archive_count++];
    cache_name(entry->basename, basename);
    entry->archive = archive;
#else
    (void) basename;
    (void) archive;
#endif
}

void msl_host_finish_initialization(void)
{
#ifdef MSL_CORE_NATIVE
    msl_core_file_context()->sealed = 1;
#endif
}
