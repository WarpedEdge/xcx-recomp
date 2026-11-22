#include "online_coop_pose.h"
#include <math.h>
#include <string.h>

static void put32(uint8_t *o, uint32_t v) { o[0] = (uint8_t)v; o[1] = (uint8_t)(v >> 8); o[2] = (uint8_t)(v >> 16); o[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *i) { return (uint32_t)i[0] | (uint32_t)i[1] << 8 | (uint32_t)i[2] << 16 | (uint32_t)i[3] << 24; }
static void putf(uint8_t *o, float f) { uint32_t v; memcpy(&v, &f, 4); put32(o, v); }
static float getf(const uint8_t *i) { uint32_t v = get32(i); float f; memcpy(&f, &v, 4); return f; }

void coop_pose_encode(const CoopPose *p, uint8_t out[COOP_POSE_BYTES]) {
    put32(out, COOP_POSE_MAGIC); put32(out + 4, p->seq); put32(out + 8, p->area); put32(out + 12, p->area2);
    for (int k = 0; k < 3; ++k) putf(out + 16 + 4 * k, p->pos[k]);
    for (int k = 0; k < 12; ++k) putf(out + 28 + 4 * k, p->rot[k]);
}

int coop_pose_decode(const uint8_t *in, size_t n, CoopPose *p) {
    if (n != COOP_POSE_BYTES || get32(in) != COOP_POSE_MAGIC) return 0;
    CoopPose q = { get32(in + 4), get32(in + 8), get32(in + 12), {0}, {0} };
    for (int k = 0; k < 3; ++k) q.pos[k] = getf(in + 16 + 4 * k);
    for (int k = 0; k < 12; ++k) q.rot[k] = getf(in + 28 + 4 * k);
    for (int k = 0; k < 3; ++k) if (!isfinite(q.pos[k]) || fabsf(q.pos[k]) > 100000.0f) return 0;
    for (int k = 0; k < 12; ++k) if (!isfinite(q.rot[k]) || fabsf(q.rot[k]) > 100000.0f) return 0;
    *p = q;
    return 1;
}
