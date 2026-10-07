// The model shake of a fighter that is hit, with extracted data, the real
// scheduler and controller inputs. PlCo.dat's public pointer 9
// (Fighter_804D6530) is three pairs of a list of offsets and its length: for
// a hit taken in the air, on the ground, and from an electric attack. A hit
// stores the pair's length in dmg.x18FD (ftCo_80090594), Fighter_8006A360
// steps dmg.x18FC through the list once a frame while the shake lasts, and
// ftCo_80090690 gives the entry the model is shifted by. The lists and their
// lengths are read here from the file itself and compared with what the game
// was handed and with what each hit does. Pointer 10 (Fighter_804D652C) is one
// more pair, the shake of a held fighter that mashes; only a fighter a ReDead
// holds asks for it, so it is compared with the file and not driven.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::{
//   ftCo_80090594,ftCo_80090690}
// refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
// refs/melee/src/melee/ft/ftcommon.c::{ftCommon_InitGrab,ftCommon_GrabMash}
#include "runtime/scalar.h"
#include "ft/fighter.h"
#include "ft/ftcommon.h"
#include "ft/types.h"
#include "ftCommon/forward.h"
#include "ftCommon/ftCo_DamageFall.h"
#include "ftCommon/ftCo_Fall.h"
#include <dolphin/pad.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MSL_TEST_FOX = 1,
    MSL_TEST_PIKACHU = 12,
    MSL_TEST_STICK = 80,
    MSL_TEST_MAX_ENTRIES = 16,
};
enum { MSL_TEST_AIR, MSL_TEST_GROUND, MSL_TEST_ELECTRIC, MSL_TEST_TABLES };

typedef struct MslTestTable {
    uint32_t length;
    Vec2 entries[MSL_TEST_MAX_ENTRIES];
} MslTestTable;

static MslCoreGameData msl_test_game_data;
static MslCoreMatch msl_test_match;
static const MslCoreInput msl_test_neutral;
static MslTestTable msl_test_file[MSL_TEST_TABLES];
static MslTestTable msl_test_file_held;
static const char* const msl_test_names[MSL_TEST_TABLES] = { "air", "ground", "electric" };

#define MSL_TEST_CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); \
        return -1; \
    } \
} while (0)

static uint32_t msl_test_be32(const uint8_t* p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static float msl_test_bef(const uint8_t* p)
{
    uint32_t bits = msl_test_be32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

// One (list, length) pair at `pair` in the archive's data section.
static int msl_test_read_pair(const uint8_t* data, uint32_t size, uint32_t pair, MslTestTable* out)
{
    uint32_t list;
    MSL_TEST_CHECK(pair + 8 <= size);
    list = msl_test_be32(data + pair);
    out->length = msl_test_be32(data + pair + 4);
    MSL_TEST_CHECK(out->length > 1 && out->length <= MSL_TEST_MAX_ENTRIES);
    MSL_TEST_CHECK(list + out->length * 8 <= size);
    for (uint32_t i = 0; i < out->length; ++i) {
        out->entries[i].x = msl_test_bef(data + list + i * 8);
        out->entries[i].y = msl_test_bef(data + list + i * 8 + 4);
    }
    return 0;
}

// PlCo.dat as the disc has it: a 0x20-byte header, the data section, the
// relocation table, then the one public symbol (ftLoadCommonData), whose
// tenth word points at the three pairs and whose eleventh at the held
// fighter's pair.
static int msl_test_read_file(const char* data_root)
{
    char path[1024];
    FILE* file;
    long file_size;
    uint8_t* raw;
    const uint8_t* data;
    uint32_t data_size, reloc_count, root, pairs;

    MSL_TEST_CHECK(snprintf(path, sizeof(path), "%s/PlCo.dat", data_root) < (int) sizeof(path));
    MSL_TEST_CHECK((file = fopen(path, "rb")) != NULL);
    MSL_TEST_CHECK(fseek(file, 0, SEEK_END) == 0 && (file_size = ftell(file)) > 0x20);
    MSL_TEST_CHECK(fseek(file, 0, SEEK_SET) == 0 && (raw = malloc((size_t) file_size)) != NULL);
    MSL_TEST_CHECK(fread(raw, 1, (size_t) file_size, file) == (size_t) file_size);
    fclose(file);
    data_size = msl_test_be32(raw + 4);
    reloc_count = msl_test_be32(raw + 8);
    MSL_TEST_CHECK(msl_test_be32(raw + 12) == 1);
    MSL_TEST_CHECK(0x20 + (uint64_t) data_size + (uint64_t) reloc_count * 4 + 8 <= (uint64_t) file_size);
    data = raw + 0x20;
    root = msl_test_be32(data + data_size + reloc_count * 4);
    MSL_TEST_CHECK(root + 23 * 4 <= data_size);
    pairs = msl_test_be32(data + root + 9 * 4);
    for (unsigned k = 0; k < MSL_TEST_TABLES; ++k) {
        MSL_TEST_CHECK(msl_test_read_pair(data, data_size, pairs + k * 8, &msl_test_file[k]) == 0);
    }
    MSL_TEST_CHECK(msl_test_read_pair(data, data_size, msl_test_be32(data + root + 10 * 4),
                             &msl_test_file_held) == 0);
    free(raw);
    return 0;
}

// What the game was handed against the file: each pair's length in its odd
// slot, as ftCo_80090594 reads it, and every entry of each list; then the
// held fighter's pair, as ftCommon_InitGrab reads it.
static int msl_test_loaded_tables(void)
{
    MSL_TEST_CHECK(Fighter_804D6530 != NULL);
    for (unsigned k = 0; k < MSL_TEST_TABLES; ++k) {
        const MslTestTable* file = &msl_test_file[k];
        const Vec2* list = Fighter_804D6530[k * 2];
        uint32_t length = (u8) (u32) (uintptr_t) Fighter_804D6530[k * 2 + 1];

        if (length != file->length) {
            fprintf(stderr, "%s list: the game was handed a length of %u, the file has %u\n",
                    msl_test_names[k], length, file->length);
            return -1;
        }
        MSL_TEST_CHECK(list != NULL);
        for (uint32_t i = 0; i < file->length; ++i) {
            MSL_TEST_CHECK(list[i].x == file->entries[i].x && list[i].y == file->entries[i].y);
        }
    }
    MSL_TEST_CHECK(Fighter_804D652C != NULL);
    if ((uint32_t) Fighter_804D652C->x4 != msl_test_file_held.length) {
        fprintf(stderr, "held list: the game was handed a length of %d, the file has %u\n",
                (int) Fighter_804D652C->x4, msl_test_file_held.length);
        return -1;
    }
    MSL_TEST_CHECK(Fighter_804D652C->x0 != NULL);
    for (uint32_t i = 0; i < msl_test_file_held.length; ++i) {
        MSL_TEST_CHECK(Fighter_804D652C->x0[i].x == msl_test_file_held.entries[i].x &&
              Fighter_804D652C->x0[i].y == msl_test_file_held.entries[i].y);
    }
    return 0;
}

static Fighter* msl_test_fighter(unsigned port)
{
    return msl_test_match.fighters[port]->user_data;
}

static int msl_test_step(const MslCoreInput* input)
{
    MSL_TEST_CHECK(msl_core_match_step(&msl_test_match, input, msl_test_match.random_seed,
                              &(MslCoreStageEvents) { 0 }) == 0);
    return 0;
}

static int msl_test_setup(unsigned attacker)
{
    MslCoreMatchConfig config = { 0 };
    config.stage_id = 32;
    config.frame_id = -123;
    config.initial_random_seed = config.frame_pre_random_seed = 1;
    config.match_damage_ratio = 1;
    config.num_players = 2;
    config.stock_count = 4;
    config.players[0].char_id = attacker;
    config.players[1].char_id = MSL_TEST_FOX;
    MSL_TEST_CHECK((msl_test_match.memory.arena == NULL
               ? msl_core_match_init(&msl_test_match, &msl_test_game_data, &config, &msl_test_neutral)
               : msl_core_match_reset(&msl_test_match, &msl_test_game_data, &config, &msl_test_neutral)) == 0);
    for (unsigned i = 0; i < 150; ++i) MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    return 0;
}

static void msl_test_place(unsigned port, float x, float y, float facing)
{
    Fighter* fp = msl_test_fighter(port);
    fp->cur_pos = (Vec3) { x, y, 0 };
    fp->prev_pos = fp->cur_pos;
    fp->facing_dir = facing;
    if (y > 0) {
        ftCommon_8007D5D4(fp);
        ftCo_Fall_Enter(msl_test_match.fighters[port]);
    }
}

// A forward smash into a Fox that stands beside the attacker, or that is
// dropped in front of it just before the swing lands. From the hit on, every
// frame of the shake is checked: which list, its length, the index one after
// the frame before's (back to 0 past the last entry), and the shift
// ftCo_80090690 gives against the file's entry at that index. The shake has
// to pass more than one entry of the list.
static int msl_test_hit(unsigned attacker, unsigned expected, int drop_frame)
{
    const MslTestTable* file = &msl_test_file[expected];
    MslCoreInput smash = { 0 };
    unsigned frames = 0, seen = 0;
    int last = -1;

    MSL_TEST_CHECK(msl_test_setup(attacker) == 0);
    msl_test_place(0, 0, 0, 1);
    msl_test_place(1, drop_frame < 0 ? 12 : 60, 0, -1);
    MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
    smash.p[0].buttons = PAD_BUTTON_A;
    smash.p[0].main_x = MSL_TEST_STICK;
    MSL_TEST_CHECK(msl_test_step(&smash) == 0);
    for (int frame = 0; frame < 90; ++frame) {
        Fighter* fp = msl_test_fighter(1);
        Vec2 shift = { 0 };
        const Vec2* entry;
        float x;

        if (frame == drop_frame) {
            msl_test_place(1, 12, 6, -1);
        }
        MSL_TEST_CHECK(msl_test_step(&msl_test_neutral) == 0);
        if (fp->dmg.x18fa_model_shift_frames == 0) {
            MSL_TEST_CHECK(ftCo_80090690(fp, &shift) == NULL);
            if (frames != 0) {
                break;
            }
            continue;
        }
        ++frames;
        MSL_TEST_CHECK(fp->dmg.x18F8 == expected);
        if (fp->dmg.x18FD != file->length) {
            fprintf(stderr, "%s hit: shaking with a list length of %u, the file has %u\n",
                    msl_test_names[expected], fp->dmg.x18FD, file->length);
            return -1;
        }
        MSL_TEST_CHECK(fp->dmg.x18FC < file->length);
        MSL_TEST_CHECK(last < 0 || fp->dmg.x18FC == (last + 1) % file->length);
        last = fp->dmg.x18FC;
        seen |= 1u << fp->dmg.x18FC;
        entry = &file->entries[fp->dmg.x18FC];
        MSL_TEST_CHECK(ftCo_80090690(fp, &shift) == &shift);
        x = entry->x * fp->facing_dir;
        if (expected == MSL_TEST_GROUND) {
            MSL_TEST_CHECK(shift.x == fp->dmg.x1904 * x);
            MSL_TEST_CHECK(shift.y == -fp->dmg.x1900 * x + entry->y);
        } else {
            MSL_TEST_CHECK(shift.x == x && shift.y == entry->y);
        }
    }
    fprintf(stderr, "%s hit: %u frames of shake, list length %u, entries seen %#x, damage %g\n",
            msl_test_names[expected], frames, file->length, seen,
            msl_test_fighter(1)->dmg.x1830_percent);
    MSL_TEST_CHECK(frames >= 2 && (seen & (seen - 1)) != 0);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 2 || msl_test_read_file(argv[1]) ||
        msl_core_game_data_init(&msl_test_game_data, argv[1]))
    {
        return 1;
    }
    // Every scenario runs, so a failing build names each thing that is wrong.
    int result = msl_test_setup(MSL_TEST_FOX) || msl_test_loaded_tables();
    result |= msl_test_hit(MSL_TEST_FOX, MSL_TEST_GROUND, -1);
    result |= msl_test_hit(MSL_TEST_FOX, MSL_TEST_AIR, 8);
    result |= msl_test_hit(MSL_TEST_PIKACHU, MSL_TEST_ELECTRIC, -1);
    msl_core_match_destroy(&msl_test_match);
    msl_core_game_data_deinit(&msl_test_game_data);
    return result != 0;
}
