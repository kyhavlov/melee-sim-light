#include <dolphin/mtx.h>

#include <math.h>
#include <string.h>

// The Dolphin SDK implements these entry points with paired-single assembly.
// PPC Linux cannot assemble the GameCube-specific paired-single mnemonics, so
// hosted C preserves the source formulas and operation ordering where the SDK
// publishes a C equivalent.
void PSVECNormalize(Vec* src, Vec* unit)
{
    float mag = src->z * src->z + (src->x * src->x + src->y * src->y);
    float inv = 1.0F / sqrtf(mag);
    unit->x = src->x * inv;
    unit->y = src->y * inv;
    unit->z = src->z * inv;
}

void PSVECAdd(Vec* a, Vec* b, Vec* out)
{
    out->x = a->x + b->x;
    out->y = a->y + b->y;
    out->z = a->z + b->z;
}

void PSVECSubtract(Vec* a, Vec* b, Vec* out)
{
    out->x = a->x - b->x;
    out->y = a->y - b->y;
    out->z = a->z - b->z;
}

void PSVECScale(Vec* src, Vec* out, float scale)
{
    out->x = src->x * scale;
    out->y = src->y * scale;
    out->z = src->z * scale;
}

float PSVECDotProduct(Vec* a, Vec* b)
{
    return a->z * b->z + (a->x * b->x + a->y * b->y);
}

void PSVECCrossProduct(Vec* a, Vec* b, Vec* out)
{
    Vec value;
    value.x = a->y * b->z - a->z * b->y;
    value.y = a->z * b->x - a->x * b->z;
    value.z = a->x * b->y - a->y * b->x;
    *out = value;
}

float PSVECMag(Vec* value)
{
    return sqrtf(value->z * value->z +
                 (value->x * value->x + value->y * value->y));
}

void PSMTXIdentity(Mtx matrix)
{
    static const Mtx identity = {
        { 1.0F, 0.0F, 0.0F, 0.0F },
        { 0.0F, 1.0F, 0.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F, 0.0F },
    };
    memcpy(matrix, identity, sizeof(Mtx));
}

void PSMTXCopy(Mtx src, Mtx dst)
{
    if (src != dst) {
        memcpy(dst, src, sizeof(Mtx));
    }
}

void PSMTXConcat(Mtx a, Mtx b, Mtx out)
{
    Mtx temporary;
    float (*result)[4] = out == a || out == b ? temporary : out;
    int row;
    int column;

    for (row = 0; row != 3; row++) {
        for (column = 0; column != 3; column++) {
            result[row][column] =
                a[row][2] * b[2][column] +
                (a[row][0] * b[0][column] +
                 a[row][1] * b[1][column]);
        }
        result[row][3] =
            a[row][3] +
            (a[row][2] * b[2][3] +
             (a[row][0] * b[0][3] + a[row][1] * b[1][3]));
    }
    if (result == temporary) {
        memcpy(out, temporary, sizeof(Mtx));
    }
}

void PSMTXMultVec(Mtx44 matrix, Vec* src, Vec* dst)
{
    Vec value;
    value.x = matrix[0][3] +
              (matrix[0][2] * src->z +
               (matrix[0][0] * src->x + matrix[0][1] * src->y));
    value.y = matrix[1][3] +
              (matrix[1][2] * src->z +
               (matrix[1][0] * src->x + matrix[1][1] * src->y));
    value.z = matrix[2][3] +
              (matrix[2][2] * src->z +
               (matrix[2][0] * src->x + matrix[2][1] * src->y));
    *dst = value;
}

void PSMTXMultVecSR(Mtx44 matrix, Vec* src, Vec* dst)
{
    Vec value;
    value.x = matrix[0][2] * src->z +
              (matrix[0][0] * src->x + matrix[0][1] * src->y);
    value.y = matrix[1][2] * src->z +
              (matrix[1][0] * src->x + matrix[1][1] * src->y);
    value.z = matrix[2][2] * src->z +
              (matrix[2][0] * src->x + matrix[2][1] * src->y);
    *dst = value;
}

void PSMTXScale(Mtx matrix, float x, float y, float z)
{
    PSMTXIdentity(matrix);
    matrix[0][0] = x;
    matrix[1][1] = y;
    matrix[2][2] = z;
}

void PSMTXTrans(Mtx matrix, float x, float y, float z)
{
    PSMTXIdentity(matrix);
    matrix[0][3] = x;
    matrix[1][3] = y;
    matrix[2][3] = z;
}

void PSMTXRotAxisRad(Mtx matrix, Vec* axis, float radians)
{
    Vec normal;
    float sine = sinf(radians);
    float cosine = cosf(radians);
    float one_minus_cosine = 1.0F - cosine;
    float x;
    float y;
    float z;

    PSVECNormalize(axis, &normal);
    x = normal.x;
    y = normal.y;
    z = normal.z;
    matrix[0][0] = cosine + one_minus_cosine * x * x;
    matrix[0][1] = y * (one_minus_cosine * x) - sine * z;
    matrix[0][2] = z * (one_minus_cosine * x) + sine * y;
    matrix[0][3] = 0.0F;
    matrix[1][0] = y * (one_minus_cosine * x) + sine * z;
    matrix[1][1] = cosine + one_minus_cosine * y * y;
    matrix[1][2] = z * (one_minus_cosine * y) - sine * x;
    matrix[1][3] = 0.0F;
    matrix[2][0] = z * (one_minus_cosine * x) - sine * y;
    matrix[2][1] = z * (one_minus_cosine * y) + sine * x;
    matrix[2][2] = cosine + one_minus_cosine * z * z;
    matrix[2][3] = 0.0F;
}

void PSMTXQuat(Mtx matrix, Quaternion* q)
{
    float scale = 2.0F /
                  (q->w * q->w +
                   (q->z * q->z + (q->x * q->x + q->y * q->y)));
    float xs = q->x * scale;
    float ys = q->y * scale;
    float zs = q->z * scale;
    float wx = q->w * xs;
    float wy = q->w * ys;
    float wz = q->w * zs;
    float xx = q->x * xs;
    float xy = q->x * ys;
    float xz = q->x * zs;
    float yy = q->y * ys;
    float yz = q->y * zs;
    float zz = q->z * zs;

    matrix[0][0] = 1.0F - (yy + zz);
    matrix[0][1] = xy - wz;
    matrix[0][2] = xz + wy;
    matrix[0][3] = 0.0F;
    matrix[1][0] = xy + wz;
    matrix[1][1] = 1.0F - (xx + zz);
    matrix[1][2] = yz - wx;
    matrix[1][3] = 0.0F;
    matrix[2][0] = xz - wy;
    matrix[2][1] = yz + wx;
    matrix[2][2] = 1.0F - (xx + yy);
    matrix[2][3] = 0.0F;
}

double __fabs(double value)
{
    return fabs(value);
}

float __fabsf(float value)
{
    return fabsf(value);
}

float sqrtf__Ff(float value)
{
    return sqrtf(value);
}

double __frsqrte(double value)
{
    return 1.0 / sqrt(value);
}
