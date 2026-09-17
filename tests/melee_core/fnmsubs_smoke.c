#include "runtime/match.h"
#include "platform/compat.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static float msl_test_float(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t msl_test_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

int main(void)
{
    /* Retail 0x8006B9E4: -(decay * sin(angle) - kb_y), fused.
     * First row reproduces both July Mewtwo/Fox recordings' first mismatch.
     * Legacy Dolphin fuses kb_y - decay * sin(angle) instead. */
    static const struct {
        uint32_t a, b, c, retail, legacy;
    } msl_cases[] = {
        { 0x3d50e560, 0, 0, 0x80000000, 0 },
        { 0x3d50e560, 0x80000000, 0, 0, 0 },
        { 0x3d50e560, 0, 0x80000000, 0x80000000, 0x80000000 },
        { 0x3d50e560, 0x80000000, 0x80000000, 0x80000000, 0 },
        { 0xbd50e560, 0, 0, 0, 0 },
        { 0xbd50e560, 0x80000000, 0, 0x80000000, 0 },
        { 0x3f800000, 0x3f800000, 0x3f800000, 0x80000000, 0 },
        { 0x3f800000, 0x3f800000, 0x40000000, 0x3f800000, 0x3f800000 },
        { 0x3f800000, 0x3f800000, 0, 0xbf800000, 0xbf800000 },
        /* Rounded underflow is not exact cancellation. */
        { 1, 0x3f000000, 0, 0x80000000, 0x80000000 },
        { 0x80000001, 0x3f000000, 0, 0, 0 },
        { 2, 0x3f000000, 0, 0x80000001, 0x80000001 },
    };
    MslCoreMatchRules msl_rules = { 0 };
    unsigned int msl_profile;
    size_t msl_i;

    msl_core_bind_match_rules(&msl_rules);
    for (msl_profile = 0; msl_profile < 2; ++msl_profile) {
        msl_rules.online_fnmsubs_zero = msl_profile;
        for (msl_i = 0; msl_i < sizeof(msl_cases) / sizeof(msl_cases[0]); ++msl_i) {
            uint32_t msl_expected = msl_profile ? msl_cases[msl_i].legacy :
                                                msl_cases[msl_i].retail;
            uint32_t msl_actual = msl_test_bits(msl_dolphin_fnmsubs(
                msl_test_float(msl_cases[msl_i].a),
                msl_test_float(msl_cases[msl_i].b),
                msl_test_float(msl_cases[msl_i].c)));
            if (msl_actual != msl_expected) {
                fprintf(stderr, "fnmsubs profile=%u case=%zu: %08x != %08x\n",
                        msl_profile, msl_i, msl_actual, msl_expected);
                return 1;
            }
        }
    }
    msl_core_bind_match_rules(NULL);
    puts("fnmsubs: 12 operand cases pass under both arithmetic profiles");
    return 0;
}
