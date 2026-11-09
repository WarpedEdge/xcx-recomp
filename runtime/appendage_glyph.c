// Verified opt-in HUD guides only. Generation records outlive copied command handles.
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

enum { GENERATIONS = 192, APPENDAGE_SLOTS = 3, NODE_SLOTS = 7 };
static const unsigned selectors[NODE_SLOTS] = {14,6,7,5,7,14,15};
typedef struct { uint32_t tid; uint64_t serial, storage; } GlyphGeneration;
static GlyphGeneration generations[GENERATIONS];
typedef struct { uint64_t node; unsigned slot; } LiveGlyph;
static LiveGlyph live[GENERATIONS];
static uint64_t live_node[APPENDAGE_SLOTS];
static unsigned generation_count, live_generation[APPENDAGE_SLOTS];
static PlatMutex glyph_lock = PLAT_MUTEX_INIT;
static PlatOnce once = PLAT_ONCE_INIT;
static int enabled;
static char control[1024], failure[24];
static uint64_t next_control;
static int control_on = 1;
static unsigned reported_generation[NODE_SLOTS], reported_count[NODE_SLOTS];
static int reported_selection[NODE_SLOTS];
static uint8_t first_binding[GENERATIONS];
static int fallback_observed, history_exhausted;
static unsigned rejected_guides;
typedef struct { const Cpu *cpu; unsigned generation, art; uint32_t tid; } BindScope;
static _Thread_local BindScope scope;
// Opt-in research only: observe these resources during the existing type-1 bind, without attaching nodes.
static const uint8_t probe_prefixes[4][32] = {
    {0x7b,0x2a,0xf2,0xb3,0x88,0xb5,0xe4,0xaa,0xc0,0xdc,0xaf,0x1a,0xd3,0x4b,0xcf,0xef,0xaa,0x21,0x7c,0x1a,0x70,0x59,0x7d,0x7c,0xd3,0x70,0x9b,0x29,0x35,0x8b,0xb1,0x35},
    {0x7a,0x8f,0xf9,0x24,0xca,0xd7,0x7d,0x57,0x75,0xce,0x28,0xe3,0x93,0x90,0xa2,0x1c,0xcf,0x99,0x93,0xb9,0x55,0xff,0x45,0x10,0xc5,0xdd,0x0e,0x8e,0x59,0x7d,0xa6,0xd6},
    {0xbb,0xe2,0xf0,0xce,0x2c,0x5a,0xda,0x43,0x5b,0x8b,0xc8,0x4c,0x1e,0xbe,0xdb,0xeb,0xa1,0x3d,0x54,0xc0,0x5f,0xbb,0xa5,0x39,0xff,0xf7,0x2c,0x12,0xe4,0xe8,0x7d,0x38},
    {0xcd,0x32,0x01,0x49,0x62,0x2f,0xbd,0xd1,0x60,0x89,0xee,0x10,0x88,0xc7,0x83,0x13,0xbb,0x78,0x5a,0xc5,0xd3,0xd7,0xdd,0x55,0x6f,0xcd,0xf9,0x72,0xaf,0xd6,0x53,0x4e}
};
static const char *const probe_names[4] = {"font_icon", "02110_judgering_btn", "icon_button02", "icon_button03"};
static int resource_probe;
static uint64_t probe_deadline;
static unsigned probe_counts[4], probe_events;
typedef struct { const Cpu *cpu; unsigned event; } ProbeScope;
static _Thread_local ProbeScope probe_scope;

static uint64_t rd64(uint64_t p) { uint64_t v; memcpy(&v, (void *)(uintptr_t)p, 8); return v; }
static uint32_t rd32(uint64_t p) { uint32_t v; memcpy(&v, (void *)(uintptr_t)p, 4); return v; }
static uint16_t rd16(uint64_t p) { uint16_t v; memcpy(&v, (void *)(uintptr_t)p, 2); return v; }
// The inspected resource header bounds the record table before the 0x1000 texture region.
static int records_match(uint64_t resource, unsigned extra) {
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
    if (memcmp(hash,expected,sizeof hash)) return 0;
    if (extra) {
        static const uint8_t x_record[32] = {0xe2,0xc3,0x5a,0xe2,0x8e,0x55,0x45,0xec,0xfb,0x66,0x58,0xb2,0x50,0x3f,0x3e,0x06,0x2f,0xd4,0x21,0x6c,0x43,0x26,0x09,0x0e,0xaf,0x49,0x6e,0xee,0x3b,0x5d,0xa4,0x6d};
        static const uint8_t zl_record[32] = {0x7c,0xe0,0xa3,0xa3,0xa8,0x06,0x64,0x7b,0x28,0xe5,0x6f,0x64,0xb5,0x73,0x46,0xa6,0x36,0x05,0x00,0xbd,0xc5,0x42,0x05,0x25,0xca,0x7e,0x5d,0xe9,0x4f,0xad,0x4f,0xa9};
        sha256((void *)(uintptr_t)(resource+base+extra*28),28,hash);
        if (memcmp(hash,extra==5 ? x_record : zl_record,sizeof hash)) return 0;
    }
    return 1;
}
static void init(void *unused) {
    (void)unused;
    const char *v = getenv("XCX_APPENDAGE_GLYPH");
    enabled = !v || strcmp(v, "0");
    v = getenv("XCX_APPENDAGE_GLYPH_CONTROL");
    if (v && strlen(v) < sizeof control) strcpy(control, v);
    v = getenv("XCX_APPENDAGE_GLYPH_FAIL");
    if (v && strlen(v) < sizeof failure) strcpy(failure, v);
    v = getenv("XCX_GLYPH_RESOURCE_PROBE");
    resource_probe = enabled && v && !strcmp(v,"1");
    if (resource_probe) {
        probe_deadline = kern_now_ns() + 180000000000ull;
        rt_log("glyph resource probe: CAPTURE READY; existing type-1 route only, four resource prefixes, 8 samples each, 180 seconds; unchanged rendering");
    }
    if (enabled) rt_log("appendage glyph: experimental Appendage styles; original rendering is the fallback");
}

static ProbeScope probe_bind(const Cpu *c, uint64_t node) {
    ProbeScope result = {0};
    if (!resource_probe || kern_now_ns() >= probe_deadline) return result;
    plat_mutex_lock(&glyph_lock);
    int full=probe_events==32;
    plat_mutex_unlock(&glyph_lock);
    if (full || !plat_mem_readable(node,0x350) ||
        rd64(node)!=MOD_MAIN_BASE+0x1c94318 || *(uint8_t *)(uintptr_t)(node+0x18)!=1) return result;
    uint64_t resource=rd64(node+0xe0);
    if (!plat_mem_readable(resource,128)) return result;
    uint8_t hash[32]; sha256((void *)(uintptr_t)resource,128,hash);
    for (unsigned i=0;i<4;i++) if (!memcmp(hash,probe_prefixes[i],32)) {
        plat_mutex_lock(&glyph_lock);
        if (probe_counts[i]<8 && kern_now_ns()<probe_deadline) {
            probe_counts[i]++; result=(ProbeScope){c,++probe_events};
            // Addresses correlate this call only; no pointer is retained after the original bind returns.
            rt_log("glyph resource probe %u: resource %s prefix128 node %llx type 1 selector %u count %u texture-object %llx",
                   result.event,probe_names[i],(unsigned long long)node,rd16(node+0x34c),rd16(node+0x34e),
                   (unsigned long long)c->x[1]);
        }
        plat_mutex_unlock(&glyph_lock);
        break;
    }
    return result;
}
int appendage_glyph_failure(const char *stage) {
    plat_once(&once, init, NULL);
    return enabled && !strcmp(failure, stage);
}

void appendage_glyph_retire(uint64_t node) {
    plat_once(&once, init, NULL);
    if (!enabled) return;
    plat_mutex_lock(&glyph_lock);
    for (unsigned i=0;i<generation_count;i++) if (live[i].node==node) live[i].node=0;
    for (int i=0;i<APPENDAGE_SLOTS;i++) if (node==live_node[i]) live_node[i]=0, live_generation[i]=0;
    plat_mutex_unlock(&glyph_lock);
}

static int attach_node(int slot, uint64_t node) {
    plat_once(&once, init, NULL);
    if (!enabled) return 0;
    if (!node || !plat_mem_readable(node, 0x350) || rd64(node) != MOD_MAIN_BASE + 0x1c94318 ||
        *(uint8_t *)(uintptr_t)(node + 0x18) != 1 || rd16(node + 0x34e) != 24) return 0;
    uint64_t resource = rd64(node + 0xe0), table = rd64(node + 0x330);
    if (!resource || !table || !plat_mem_readable(resource, 128) || !plat_mem_readable(table, 8)) return 0;
    uint8_t hash[32];
    static const uint8_t expected[32] = {0xed,0x91,0x64,0xc1,0x3b,0xf5,0x5b,0x99,0x9b,0xf7,0x8b,0xe9,0xd5,0x2d,0x3b,0x65,
        0x71,0x70,0x62,0xc6,0x22,0xdb,0xc8,0xb1,0x31,0x5a,0x30,0xe7,0xd9,0x7d,0x32,0xf7};
    sha256((void *)(uintptr_t)resource, 128, hash);
    if (memcmp(hash, expected, 32) || !records_match(resource,slot==3 ? 5 : slot==6 ? 15 : 0)) return 0;
    uint64_t object = rd64(table);
    if (!object || !plat_mem_readable(object, 0x38)) return 0;
    uint32_t tid = rd32(rd64(MOD_MAIN_BASE + 0x1d64f18)) + rd32(object + 0x34);
    plat_mutex_lock(&glyph_lock);
    // Every live shared-widget instance remains eligible. Generations contain only source identity;
    // a retained field node may change A/R/ZL without changing an already copied command's artwork.
    for (unsigned i=0;i<generation_count;i++) if (live[i].node==node) {
        if (slot>=APPENDAGE_SLOTS && live[i].slot>=APPENDAGE_SLOTS && generations[i].tid==tid) {
            live[i].slot=slot; plat_mutex_unlock(&glyph_lock); return 1;
        }
        live[i].node=0;
    }
    if (slot<APPENDAGE_SLOTS) live_node[slot]=0, live_generation[slot]=0;
    int attached=0;
    if (generation_count<GENERATIONS) {
        unsigned index=generation_count++;
        generations[index]=(GlyphGeneration){.tid=tid}; live[index]=(LiveGlyph){node,(unsigned)slot};
        if (slot<APPENDAGE_SLOTS) live_node[slot]=node, live_generation[slot]=index+1;
        rt_log("HUD glyph: slot %d selector %u generation %u attached, source texture %u",slot,selectors[slot],index+1,tid);
        attached=1;
    } else history_exhausted=1;
    plat_mutex_unlock(&glyph_lock);
    return attached;
}

void appendage_glyph_attach(const Cpu *c) {
    plat_once(&once, init, NULL);
    if (!enabled) return;
    int slot = c->x[30] == MOD_MAIN_BASE + 0x60f690 ? 0 : c->x[30] == MOD_MAIN_BASE + 0x60f6ac ? 1 :
               c->x[30] == MOD_MAIN_BASE + 0x60f6c8 ? 2 : -1;
    if (slot < 0 || c->x[1] != selectors[slot]) return;
    if (!slot) {
        plat_mutex_lock(&glyph_lock);
        for (unsigned i=0;i<generation_count;i++) if (live[i].slot<APPENDAGE_SLOTS) live[i].node=0;
        memset(live_node,0,sizeof live_node);
        memset(live_generation,0,sizeof *live_generation*APPENDAGE_SLOTS);
        plat_mutex_unlock(&glyph_lock);
    }
    if (!plat_mem_readable(c->x[0],0x40)) return;
    uint64_t node = rd64(c->x[0]+0x38);
    appendage_glyph_retire(node);
    attach_node(slot,node);
}

void appendage_glyph_lookup_attach(const Cpu *c, uint64_t frame) {
    if (c->x[30]!=MOD_MAIN_BASE+0x55ff20 || (frame&15) || !plat_mem_readable(frame,16)) return;
    // 55fec0 saves FP/LR at SP and 55fecc sets FP=SP; the tail-call from 5da974 preserves the parent LR.
    uint64_t caller=rd64(frame+8); int slot=-1;
    if (caller==MOD_MAIN_BASE+0x5a6660 && c->x[1]==5) slot=3;
    if (caller==MOD_MAIN_BASE+0x5c6ed8) slot=c->x[1]==7 ? 4 : c->x[1]==14 ? 5 : c->x[1]==15 ? 6 : -1;
    if (slot<0 || !plat_mem_readable(c->x[0],8)) return;
    // a1cfc0 constructs this temporary wrapper with the resolved rectangle at offset zero.
    if (!attach_node(slot,rd64(c->x[0]))) {
        plat_mutex_lock(&glyph_lock); unsigned bit=1u<<slot;
        if (!(rejected_guides&bit)) {
            rejected_guides|=bit;
            rt_log("HUD glyph: shared guide slot %d retained original; node/type/resource guard or history limit",slot);
        }
        plat_mutex_unlock(&glyph_lock);
    }
}

void appendage_glyph_bind(Cpu *c, uint64_t node, GuestFn original) {
    BindScope saved = scope;
    ProbeScope saved_probe = probe_scope;
    scope = (BindScope){0};
    plat_once(&once, init, NULL);
    probe_scope = probe_bind(c,node);
    unsigned selected = 0; int slot = -1;
    if (enabled) {
        plat_mutex_lock(&glyph_lock);
        for (unsigned i=0;i<generation_count;i++) if (live[i].node==node) { selected=i+1; slot=(int)live[i].slot; }
        if (slot < APPENDAGE_SLOTS && (!live_generation[0] || !live_generation[1] || !live_generation[2])) selected = 0;
        plat_mutex_unlock(&glyph_lock);
    }
    GlyphDecision decision = {.style = GLYPH_ORIGINAL};
    if (selected) decision = slot<APPENDAGE_SLOTS ? glyph_current_decision() : glyph_current_button(slot==3 ? B_X : slot==4 ? B_A : slot==5 ? B_R : B_ZL);
    int art = selected ? decision.art[slot<APPENDAGE_SLOTS ? slot : 0] : -1;
    if (selected && decision.style != GLYPH_ORIGINAL && art >= 0) {
        plat_mutex_lock(&glyph_lock);
        if (control[0] && kern_now_ns() >= next_control) {
            next_control = kern_now_ns() + 100000000;
            FILE *f = fopen(control, "rb");
            control_on = f && fgetc(f) == '1';
            if (f) fclose(f);
        }
        // These fields are read only during the original node's live draw call.
        if (control_on && live[selected-1].node == node && live[selected-1].slot == (unsigned)slot &&
            rd64(node) == MOD_MAIN_BASE + 0x1c94318 && rd16(node + 0x34c) == selectors[slot] && records_match(rd64(node + 0xe0),slot==3 ? 5 : slot==6 ? 15 : 0))
            scope = (BindScope){c, selected, (unsigned)art, generations[selected - 1].tid};
        plat_mutex_unlock(&glyph_lock);
    }
    if (selected) {
        plat_mutex_lock(&glyph_lock);
        int on = scope.generation ? (int)scope.art + 1 : 0;
        if (!first_binding[selected-1] || (reported_count[slot] < 8 && (reported_generation[slot] != selected || reported_selection[slot] != on))) {
            first_binding[selected-1]=1;
            reported_generation[slot] = selected;
            reported_selection[slot] = on;
            reported_count[slot]++;
            unsigned active=live[selected-1].node==node ? rd16(node+0x34c) : 0xffff;
            rt_log("HUD glyph: slot %d selector %u active %u generation %u requests %s binding", slot, selectors[slot], active, selected, on ? glyph_art_name(scope.art) : "original");
        }
        plat_mutex_unlock(&glyph_lock);
    }
    original(c);
    scope = saved;
    probe_scope = saved_probe;
}

uint64_t appendage_glyph_handle(const Cpu *c, uint64_t original) {
    if (probe_scope.cpu==c && probe_scope.event && c->x[30]==MOD_MAIN_BASE+0x95c9cc) {
        unsigned event=probe_scope.event; probe_scope.event=0;
        if (kern_now_ns()<probe_deadline) vk_glyph_resource_observe(original,event);
    }
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
    int complete=live_generation[0] && live_generation[1] && live_generation[2];
    for (unsigned i=0;i<generation_count;i++) if (live[i].node && live[i].slot>=APPENDAGE_SLOTS) complete=1;
    const char *note = !enabled ? "Original virtual labels: experimental rendering disabled" :
        history_exhausted ? "Original virtual labels: glyph history exhausted; restart game to restore rebuilt guides" :
        !complete ? "Original virtual labels: waiting for verified HUD nodes" :
        fallback_observed ? "Rendering fallback was observed; original virtual labels are retained where needed" : NULL;
    plat_mutex_unlock(&glyph_lock);
    return note;
}
