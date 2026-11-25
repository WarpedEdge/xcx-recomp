// Named facial-hair component diagnostics; see docs/character-creator-beards.md.
#include "avatar_beard.h"
#include "host.h"
#include <stdlib.h>
#include <string.h>

void avatar_beard_component(Cpu *c) {
    const char *mode = getenv("XCX_BEARD_COMPONENTS");
    if (!mode || !*mode) return;
    uint64_t wrapper = c->x[0];
    uint32_t index = (uint32_t)c->x[1];
    if (index > 255) return;
    uint64_t entry = LD64(wrapper + 0x10) + (uint64_t)index * 0xb0;
    uint64_t name = LD64(entry);
    if (!name) return;
    char text[32];
    for (unsigned i = 0; i < sizeof text; i++) {
        text[i] = (char)LD8(name + i);
        if (!text[i]) break;
    }
    text[sizeof text - 1] = 0;
    if (strncmp(text, "hige", 4)) return;
    float value;
    memcpy(&value, &c->v[0], sizeof value);
    static unsigned logs;
    if (logs++ < 80) rt_log("avatar beard: component %s index %u value %.3f", text, index, value);
    if (!strcmp(mode, "show")) {
        value = 1.0f;
        memcpy(&c->v[0], &value, sizeof value);
    }
}
