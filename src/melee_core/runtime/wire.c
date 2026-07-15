#include "runtime/wire.h"

#include <string.h>

uint16_t msl_core_get_le16(const void* ptr)
{
    const uint8_t* p = ptr;
    return (uint16_t) p[0] | (uint16_t) ((uint16_t) p[1] << 8);
}

uint32_t msl_core_get_le32(const void* ptr)
{
    const uint8_t* p = ptr;
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
           ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

float msl_core_get_lef32(const void* ptr)
{
    uint32_t bits = msl_core_get_le32(ptr);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void msl_core_put_le16(void* ptr, uint16_t value)
{
    uint8_t* p = ptr;
    p[0] = (uint8_t) value;
    p[1] = (uint8_t) (value >> 8);
}

void msl_core_put_le32(void* ptr, uint32_t value)
{
    uint8_t* p = ptr;
    p[0] = (uint8_t) value;
    p[1] = (uint8_t) (value >> 8);
    p[2] = (uint8_t) (value >> 16);
    p[3] = (uint8_t) (value >> 24);
}

void msl_core_put_lef32(void* ptr, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    msl_core_put_le32(ptr, bits);
}
