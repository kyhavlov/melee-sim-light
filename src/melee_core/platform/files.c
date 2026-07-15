#include "files.h"

#include <platform.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "baselib/archive.h"
#include "baselib/memory.h"

static const char* data_root = "data/raw";

void msl_host_set_data_root(const char* path)
{
    if (path != NULL && path[0] != '\0') {
        data_root = path;
    }
}

static void make_path(char* path, size_t capacity, const char* basename)
{
    while (*basename == '/') {
        basename++;
    }
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
}

bool lbFile_800168A0(s32 heap, const char* basename, u32* source, u32* length)
{
    size_t size = (size_t) lbFile_800163D8(basename);
    void* data = HSD_MemAlloc((ssize_t) size);
    (void) heap;
    lbFile_8001668C(basename, data, length);
    *source = (u32) data;
    return true;
}

void* lbHeap_80015BD0(int heap, int size)
{
    (void) heap;
    return HSD_MemAlloc(size);
}

void lbHeap_80015CA8(int heap, void* ptr)
{
    (void) heap;
    HSD_Free(ptr);
}

HSD_Archive* lbDvd_8001819C(const char* basename)
{
    (void) basename;
    return NULL;
}
