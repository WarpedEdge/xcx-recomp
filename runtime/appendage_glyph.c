// One opt-in Appendage guide only. Generation records outlive copied command handles.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "appendage_glyph.h"
#include "host.h"
#include "input.h"
#include "input_map.h"
#include "input_glyphs.h"
#include "layout.h"
#include "platform/platform.h"
#include "sha256.h"

enum { GENERATIONS = 192 };
static const unsigned selectors[3] = {14,6,7};
typedef struct { uint32_t tid; uint64_t serial, storage; } GlyphGeneration;
static GlyphGeneration generations[GENERATIONS];
static uint64_t live_node[3];
static unsigned generation_count, live_generation[3];
static PlatMutex glyph_lock = PLAT_MUTEX_INIT;
static PlatOnce once = PLAT_ONCE_INIT;
static int enabled;
static char control[1024], failure[24];
static uint64_t next_control;
static int control_on = 1;
static unsigned reported_generation, reported_count;
static int reported_selection = -1;
static int fallback_observed;
typedef struct { const Cpu *cpu; unsigned generation, art; uint32_t tid; } BindScope;
static _Thread_local BindScope scope;

static uint64_t rd64(uint64_t p) { uint64_t v; memcpy(&v, (void *)(uintptr_t)p, 8); return v; }
static uint32_t rd32(uint64_t p) { uint32_t v; memcpy(&v, (void *)(uintptr_t)p, 4); return v; }
static uint16_t rd16(uint64_t p) { uint16_t v; memcpy(&v, (void *)(uintptr_t)p, 2); return v; }
// The inspected resource header bounds the record table before the 0x1000 texture region.
static int records_match(uint64_t resource) {
    if (!resource || !plat_mem_readable(resource, 0x1000)) return 0;
    uint32_t base = rd32(resource + 0x2c);
    if (base > 0x1000 - 0x1c) return 0;
    uint32_t relative = rd32(resource + base + 0x18);
    if (relative > 0x1000 - base) return 0;
    base += relative;
    if (base > 0x1000 - 24 * 28) return 0;
    uint8_t records[3*28], hash[32];
    for (int i = 0; i < 3; i++) memcpy(records+i*28, (void *)(uintptr_t)(resource+base+selectors[i]*28),28);
    static const uint8_t expected[32] = {0x34,0x62,0x54,0xa9,0x27,0x7c,0x80,0xdf,0x8e,0x54,0x43,0x79,0xed,0x61,0xe8,0x4c,0xee,0x25,0xb0,0x27,0x45,0x3c,0x68,0xfc,0xa8,0x25,0x58,0x3b,0x34,0xb8,0x04,0xd5};
    sha256(records,sizeof records,hash);
    return !memcmp(hash,expected,sizeof hash);
}
static void init(void *unused) {
    (void)unused;
    const char *v = getenv("XCX_APPENDAGE_GLYPH");
    enabled = !v || strcmp(v, "0");
    v = getenv("XCX_APPENDAGE_GLYPH_CONTROL");
    if (v && strlen(v) < sizeof control) strcpy(control, v);
    v = getenv("XCX_APPENDAGE_GLYPH_FAIL");
    if (v && strlen(v) < sizeof failure) strcpy(failure, v);
    if (enabled) rt_log("appendage glyph: experimental Appendage styles; original rendering is the fallback");
}
int appendage_glyph_failure(const char *stage) {
    plat_once(&once, init, NULL);
    return enabled && !strcmp(failure, stage);
}

void appendage_glyph_retire(uint64_t node) {
    plat_once(&once, init, NULL);
    if (!enabled) return;
    plat_mutex_lock(&glyph_lock);
    for (int i = 0; i < 3; i++) if (node == live_node[i]) live_node[i] = 0, live_generation[i] = 0;
    plat_mutex_unlock(&glyph_lock);
}

void appendage_glyph_attach(const Cpu *c) {
    plat_once(&once, init, NULL);
    if (!enabled) return;
    int slot = c->x[30] == MOD_MAIN_BASE + 0x60f690 ? 0 : c->x[30] == MOD_MAIN_BASE + 0x60f6ac ? 1 :
               c->x[30] == MOD_MAIN_BASE + 0x60f6c8 ? 2 : -1;
    if (slot < 0 || c->x[1] != selectors[slot]) return;
    if (!slot) {
        plat_mutex_lock(&glyph_lock);
        memset(live_node, 0, sizeof live_node); memset(live_generation, 0, sizeof live_generation);
        plat_mutex_unlock(&glyph_lock);
    }
    if (!plat_mem_readable(c->x[0], 0x40)) return;
    uint64_t node = rd64(c->x[0] + 0x38);
    appendage_glyph_retire(node);
    if (!node || !plat_mem_readable(node, 0x350) || rd64(node) != MOD_MAIN_BASE + 0x1c94318 ||
        *(uint8_t *)(uintptr_t)(node + 0x18) != 1 || rd16(node + 0x34e) != 24) return;
    uint64_t resource = rd64(node + 0xe0), table = rd64(node + 0x330);
    if (!resource || !table || !plat_mem_readable(resource, 128) || !plat_mem_readable(table, 8)) return;
    uint8_t hash[32];
    static const uint8_t expected[32] = {0xed,0x91,0x64,0xc1,0x3b,0xf5,0x5b,0x99,0x9b,0xf7,0x8b,0xe9,0xd5,0x2d,0x3b,0x65,
        0x71,0x70,0x62,0xc6,0x22,0xdb,0xc8,0xb1,0x31,0x5a,0x30,0xe7,0xd9,0x7d,0x32,0xf7};
    sha256((void *)(uintptr_t)resource, 128, hash);
    if (memcmp(hash, expected, 32) || !records_match(resource)) return;
    uint64_t object = rd64(table);
    if (!object || !plat_mem_readable(object, 0x38)) return;
    uint32_t tid = rd32(rd64(MOD_MAIN_BASE + 0x1d64f18)) + rd32(object + 0x34);
    plat_mutex_lock(&glyph_lock);
    if (generation_count < GENERATIONS) {
        live_generation[slot] = ++generation_count;
        generations[live_generation[slot] - 1] = (GlyphGeneration){.tid = tid};
        live_node[slot] = node;
        rt_log("appendage glyph: generation %u attached, source texture %u", live_generation[slot], tid);
    }
    plat_mutex_unlock(&glyph_lock);
}

void appendage_glyph_bind(Cpu *c, uint64_t node, GuestFn original) {
    BindScope saved = scope;
    scope = (BindScope){0};
    plat_once(&once, init, NULL);
    unsigned selected = 0; int slot = -1;
    if (enabled) {
        plat_mutex_lock(&glyph_lock);
        for (int i = 0; i < 3; i++) if (live_node[i] == node) { selected = live_generation[i]; slot = i; }
        if (!live_generation[0] || !live_generation[1] || !live_generation[2]) selected = 0;
        plat_mutex_unlock(&glyph_lock);
    }
    GlyphDecision decision = {.style = GLYPH_ORIGINAL};
    if (selected) decision = glyph_current_decision();
    if (selected && decision.style != GLYPH_ORIGINAL && decision.art[slot] >= 0) {
        plat_mutex_lock(&glyph_lock);
        if (control[0] && kern_now_ns() >= next_control) {
            next_control = kern_now_ns() + 100000000;
            FILE *f = fopen(control, "rb");
            control_on = f && fgetc(f) == '1';
            if (f) fclose(f);
        }
        // These fields are read only during the original node's live draw call.
        if (control_on && live_node[slot] == node && live_generation[slot] == selected &&
            rd64(node) == MOD_MAIN_BASE + 0x1c94318 && rd16(node + 0x34c) == selectors[slot] && records_match(rd64(node + 0xe0)))
            scope = (BindScope){c, selected, (unsigned)decision.art[slot], generations[selected - 1].tid};
        plat_mutex_unlock(&glyph_lock);
    }
    if (selected) {
        plat_mutex_lock(&glyph_lock);
        int on = scope.generation ? (int)scope.art + 1 : 0;
        if (reported_count < 48 && (reported_generation != selected || reported_selection != on)) {
            reported_generation = selected;
            reported_selection = on;
            reported_count++;
            rt_log("appendage glyph: generation %u requests %s binding", selected, on ? glyph_art_name(scope.art) : "original");
        }
        plat_mutex_unlock(&glyph_lock);
    }
    original(c);
    scope = saved;
}

uint64_t appendage_glyph_handle(const Cpu *c, uint64_t original) {
    if (scope.cpu != c || !scope.generation || c->x[30] != MOD_MAIN_BASE + 0x95c9cc ||
        original >> 32 || (original & 0xfffff) != scope.tid) return original;
    uint64_t tagged = APPENDAGE_GLYPH_TAG | (uint64_t)scope.generation << 44 | (uint64_t)scope.art << 32 | original;
    return vk_appendage_capture_source(tagged) ? tagged : original;
}

int appendage_glyph_source(uint64_t h, uint64_t serial, uint64_t storage) {
    if (!appendage_glyph_tagged(h) || appendage_glyph_art(h) >= glyph_art_count()) return 0;
    unsigned g = (unsigned)(h >> 44 & 0xfff);
    int valid = 0;
    plat_mutex_lock(&glyph_lock);
    if (g && g <= generation_count) {
        GlyphGeneration *s = &generations[g - 1];
        if ((h & 0xfffff) == s->tid && serial && storage) {
            if (!s->serial) s->serial = serial, s->storage = storage;
            valid = s->serial == serial && s->storage == storage;
        }
    }
    plat_mutex_unlock(&glyph_lock);
    return valid;
}

void appendage_glyph_note_fallback(void) {
    plat_mutex_lock(&glyph_lock); fallback_observed = 1; plat_mutex_unlock(&glyph_lock);
}
const char *appendage_glyph_runtime_note(void) {
    plat_mutex_lock(&glyph_lock);
    int complete = live_generation[0] && live_generation[1] && live_generation[2];
    const char *note = !enabled ? "Original virtual labels: experimental rendering disabled" :
        !complete && generation_count == GENERATIONS ? "Original virtual labels: glyph history exhausted; restart game to restore styles" :
        !complete ? "Original virtual labels: waiting for all three verified Appendage nodes" :
        fallback_observed ? "Rendering fallback was observed; original virtual labels are retained where needed" : NULL;
    plat_mutex_unlock(&glyph_lock);
    return note;
}
