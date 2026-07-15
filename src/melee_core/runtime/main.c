#include "runtime/scalar.h"
#include "runtime/wire.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

enum { MSL_CORE_IO_BUFFER_BYTES = 64 * 1024 };

static int read_exact_file(const char* path, void* dst, size_t size)
{
    FILE* file = fopen(path, "rb");
    int trailing;

    if (file == NULL) {
        fprintf(stderr, "could not open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fread(dst, 1, size, file) != size) {
        fprintf(stderr, "%s must contain exactly %lu bytes\n", path,
                (unsigned long) size);
        fclose(file);
        return -1;
    }
    trailing = fgetc(file);
    fclose(file);
    if (trailing != EOF) {
        fprintf(stderr, "%s contains more than one wire row\n", path);
        return -1;
    }
    return 0;
}

static int write_compare(FILE* output, const MslCoreCompare* compare)
{
    if (fwrite(compare, 1, sizeof(*compare), output) != sizeof(*compare)) {
        fprintf(stderr, "failed to write compare output: %s\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

static int init_match(MslCoreGameData* game_data, MslCoreMatch* match,
                      const char* data_root, const uint8_t* config_wire,
                      const MslCoreInput* previous_input)
{
    MslCoreMatchConfig config;

    msl_core_decode_match_config(&config, config_wire);
    if (msl_core_game_data_init(game_data, data_root) != 0) {
        return -1;
    }
    return msl_core_match_init(match, game_data, &config, previous_input);
}

static int run_stream(const char* data_root)
{
    char input_buffer[MSL_CORE_IO_BUFFER_BYTES];
    char output_buffer[MSL_CORE_IO_BUFFER_BYTES];
    uint8_t config_wire[sizeof(MslCoreMatchConfig)];
    MslCoreGameData game_data;
    MslCoreInput previous_input;
    MslCoreStreamFrame frame;
    MslCoreMatch match;
    size_t count;

    // Supply libc's stream storage before match initialization so normal
    // frame reads/writes cannot trigger a hidden stdio allocation.
    if (setvbuf(stdin, input_buffer, _IOFBF, sizeof(input_buffer)) != 0 ||
        setvbuf(stdout, output_buffer, _IOFBF, sizeof(output_buffer)) != 0)
    {
        fprintf(stderr, "could not configure Melee core stream buffers\n");
        return 1;
    }
    if (fread(config_wire, 1, sizeof(config_wire), stdin) !=
            sizeof(config_wire) ||
        fread(&previous_input, 1, sizeof(previous_input), stdin) !=
            sizeof(previous_input))
    {
        fprintf(stderr, "short Melee core stream header\n");
        return 1;
    }
    if (init_match(&game_data, &match, data_root, config_wire,
                   &previous_input) != 0)
    {
        return 1;
    }
    while ((count = fread(&frame, 1, sizeof(frame), stdin)) == sizeof(frame)) {
        uint32_t frame_seed =
            msl_core_get_le32(&frame.frame_pre_random_seed);
        MslCoreStageEvents stage_events;
        msl_core_decode_stage_events(
            &stage_events, (const uint8_t*) &frame.stage_events);
        if (msl_core_match_step(&match, &frame.input, frame_seed,
                                &stage_events) != 0 ||
            write_compare(stdout, msl_core_match_output(&match)) != 0)
        {
            return 1;
        }
    }
    if (count != 0 || ferror(stdin)) {
        fprintf(stderr, "short Melee core input stream\n");
        return 1;
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "failed to flush compare stream: %s\n",
                strerror(errno));
        return 1;
    }
    return 0;
}

static void usage(const char* argv0)
{
    fprintf(stderr,
            "usage: %s GAME_DATA CONFIG PREV_INPUT_OR_- INPUT_TAPE OUTPUT\n"
            "       %s GAME_DATA --stream\n",
            argv0, argv0);
}

int main(int argc, char** argv)
{
    char input_buffer[MSL_CORE_IO_BUFFER_BYTES];
    char output_buffer[MSL_CORE_IO_BUFFER_BYTES];
    uint8_t config_wire[sizeof(MslCoreMatchConfig)];
    MslCoreGameData game_data;
    MslCoreInput previous_input;
    MslCoreInput input;
    MslCoreMatch match;
    FILE* input_file;
    FILE* output_file;
    size_t count;
    int result = 1;

    if (argc == 3 && strcmp(argv[2], "--stream") == 0) {
        return run_stream(argv[1]);
    }
    if (argc != 6) {
        usage(argv[0]);
        return 2;
    }
    if (read_exact_file(argv[2], config_wire, sizeof(config_wire)) != 0) {
        return 2;
    }
    memset(&previous_input, 0, sizeof(previous_input));
    if (strcmp(argv[3], "-") != 0 &&
        read_exact_file(argv[3], &previous_input, sizeof(previous_input)) != 0)
    {
        return 2;
    }
    input_file = fopen(argv[4], "rb");
    if (input_file == NULL) {
        fprintf(stderr, "could not open %s: %s\n", argv[4], strerror(errno));
        return 2;
    }
    output_file = fopen(argv[5], "wb");
    if (output_file == NULL) {
        fprintf(stderr, "could not open %s: %s\n", argv[5], strerror(errno));
        fclose(input_file);
        return 2;
    }
    if (setvbuf(input_file, input_buffer, _IOFBF, sizeof(input_buffer)) != 0 ||
        setvbuf(output_file, output_buffer, _IOFBF, sizeof(output_buffer)) != 0)
    {
        fprintf(stderr, "could not configure Melee core file buffers\n");
        goto done;
    }
    if (init_match(&game_data, &match, argv[1], config_wire,
                   &previous_input) != 0)
    {
        goto done;
    }

    while ((count = fread(&input, 1, sizeof(input), input_file)) ==
           sizeof(input))
    {
        MslCoreStageEvents stage_events = { 0 };
        if (msl_core_match_step(&match, &input, match.random_seed,
                                &stage_events) != 0 ||
            write_compare(output_file, msl_core_match_output(&match)) != 0)
        {
            goto done;
        }
    }
    if (count != 0 || ferror(input_file)) {
        fprintf(stderr,
                "%s is not a whole-number sequence of MslCoreInput rows\n",
                argv[4]);
        goto done;
    }
    result = 0;

done:
    if (fclose(output_file) != 0) {
        result = 1;
    }
    fclose(input_file);
    return result;
}
