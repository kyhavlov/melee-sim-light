#include "baselib/archive.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int probe_archive(const char* path)
{
    FILE* file;
    long file_size;
    u8* bytes;
    HSD_Archive archive;
    u32 i;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return 1;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "%s: could not determine file size\n", path);
        fclose(file);
        return 1;
    }

    bytes = malloc((size_t) file_size);
    if (bytes == NULL) {
        fprintf(stderr, "%s: allocation failed\n", path);
        fclose(file);
        return 1;
    }
    if (fread(bytes, 1, (size_t) file_size, file) != (size_t) file_size) {
        fprintf(stderr, "%s: short read\n", path);
        free(bytes);
        fclose(file);
        return 1;
    }
    fclose(file);

    if (HSD_ArchiveParse(&archive, bytes, (size_t) file_size) != 0) {
        fprintf(stderr, "%s: HSD_ArchiveParse failed\n", path);
        free(bytes);
        return 1;
    }

    printf("%s: size=%lu data=%lu reloc=%lu public=%lu extern=%lu\n", path,
           archive.header.file_size, archive.header.data_size,
           archive.header.nb_reloc, archive.header.nb_public,
           archive.header.nb_extern);
    for (i = 0; i < archive.header.nb_public; i++) {
        const char* symbol = archive.symbols + archive.public_info[i].symbol;
        void* address = HSD_ArchiveGetPublicAddress(&archive, symbol);

        if (address != archive.data + archive.public_info[i].offset) {
            fprintf(stderr, "%s: public lookup failed for %s\n", path,
                    symbol);
            free(bytes);
            return 1;
        }
        printf("  public[%lu]=%s\n", i, symbol);
    }

    free(bytes);
    return 0;
}

int main(int argc, char** argv)
{
    int result = 0;
    int i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s ARCHIVE...\n", argv[0]);
        return 2;
    }
    for (i = 1; i < argc; i++) {
        result |= probe_archive(argv[i]);
    }
    return result;
}
